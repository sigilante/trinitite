#include <stdint.h>
#include "noun.h"
#include "uart.h"
#include "memory.h"
#include "jam.h"
#include "nock.h"
#include "setjmp.h"
#include "kernel.h"

/* Effect tag cords (Urbit cord encoding: LSB = first char of name) */
#define CORD_OUT   7632239ULL      /* %out  = 'o','u','t'         */
#define CORD_BLIT  1953066082ULL   /* %blit = 'b','l','i','t'     */

extern jmp_buf nock_abort;   /* defined in nock.c */

/* ── UART noun framing ────────────────────────────────────────────────────── */

noun uart_recv_noun(void) {
    uint64_t nbytes = 0;
    for (int i = 0; i < 8; i++)
        nbytes |= (uint64_t)(uint8_t)uart_getc() << (i * 8);

    if (nbytes == 0) return NOUN_ZERO;
    if (nbytes > UART_RXBUF_SIZE) nbytes = UART_RXBUF_SIZE;

    uint8_t *buf = (uint8_t *)UART_RXBUF_BASE;
    uart_read_bytes(buf, nbytes);

    /* zero-pad to 8-byte limb boundary */
    uint64_t nbytes_padded = (nbytes + 7) & ~(uint64_t)7;
    for (uint64_t i = nbytes; i < nbytes_padded; i++) buf[i] = 0;

    uint64_t nlimbs = nbytes_padded / 8;
    while (nlimbs > 1 && ((uint64_t *)buf)[nlimbs - 1] == 0) nlimbs--;

    noun jam_atom = make_atom((uint64_t *)buf, nlimbs);
    return cue(jam_atom);
}

void uart_send_noun(noun n) {
    noun a = jam(n);
    const uint8_t *data;
    uint64_t nbytes;
    uint8_t direct_bytes[8];

    if (noun_is_direct(a)) {
        uint64_t val = direct_val(a);
        nbytes = 0;
        for (int i = 0; i < 8; i++) {
            direct_bytes[i] = (uint8_t)(val & 0xFF);
            if (direct_bytes[i]) nbytes = (uint64_t)i + 1;
            val >>= 8;
        }
        if (nbytes == 0) nbytes = 1;
        data = direct_bytes;
    } else {
        atom_t *at = atom_store_get(indirect_hash(a));
        if (!at) return;
        data   = (const uint8_t *)at->limbs;
        nbytes = at->size * 8;
        while (nbytes > 1 && data[nbytes - 1] == 0) nbytes--;
    }

    /* write 8-byte LE length header then raw bytes */
    uint8_t hdr[8];
    for (int i = 0; i < 8; i++) hdr[i] = (uint8_t)(nbytes >> (i * 8));
    uart_write_bytes(hdr, 8);
    uart_write_bytes(data, nbytes);
}

/* ── Effect dispatch ──────────────────────────────────────────────────────── */

static void atom_print_uart(noun a) {
    if (noun_is_direct(a)) {
        uint64_t val = direct_val(a);
        while (val) {
            uart_putc((char)(val & 0xFF));
            val >>= 8;
        }
    } else if (noun_is_indirect(a)) {
        atom_t *at = atom_store_get(indirect_hash(a));
        if (!at) return;
        const uint8_t *bytes = (const uint8_t *)at->limbs;
        uint64_t nbytes = at->size * 8;
        while (nbytes > 0 && bytes[nbytes - 1] == 0) nbytes--;
        for (uint64_t i = 0; i < nbytes; i++)
            uart_putc((char)bytes[i]);
    }
}

static void dispatch_one(noun tag, noun data) {
    if (!noun_is_atom(tag)) return;
    uint64_t t = noun_is_direct(tag) ? direct_val(tag) : 0;
    if (t == CORD_OUT || t == CORD_BLIT)
        atom_print_uart(data);
    /* unknown tags: silent ignore */
}

void dispatch_effects(noun effects) {
    while (noun_is_cell(effects)) {
        cell_t *list = (cell_t *)(uintptr_t)cell_ptr(effects);
        noun head    = list->head;
        effects      = list->tail;
        if (noun_is_cell(head)) {
            cell_t *fx = (cell_t *)(uintptr_t)cell_ptr(head);
            dispatch_one(fx->head, fx->tail);
        }
    }
}

/* ── Standard Hoon gate slam formula ─────────────────────────────────────── */
/*
 * [9 2 [10 [6 [0 3]] [0 2]]]
 * Subject = [gate event]:
 *   take gate (slot 2), replace sample (slot 6) with event (slot 3),
 *   run battery (slot 2 of modified gate).
 */
static noun build_slam_formula(void) {
    return alloc_cell(direct(9),
           alloc_cell(direct(2),
           alloc_cell(direct(10),
           alloc_cell(
               alloc_cell(direct(6), alloc_cell(direct(0), direct(3))),
               alloc_cell(direct(0), direct(2))))));
}

/* ── Kernel event loops ───────────────────────────────────────────────────── */

void arvo_loop(noun kernel_init) {
    volatile noun kernel = kernel_init;
    noun slam = build_slam_formula();
    uart_puts("\r\nfock arvo\r\n");

    for (;;) {
        if (setjmp(nock_abort) != 0) {
            uart_puts("\r\nkernel crash\r\n");
            continue;
        }
        noun event   = uart_recv_noun();
        noun subject = alloc_cell(kernel, event);
        noun result  = nock(subject, slam);
        if (!noun_is_cell(result)) { uart_puts("bad result\r\n"); continue; }
        cell_t *r    = (cell_t *)(uintptr_t)cell_ptr(result);
        dispatch_effects(r->head);
        kernel = r->tail;
    }
}

void shrine_loop(noun kernel_init) {
    volatile noun kernel = kernel_init;
    volatile noun causes = NOUN_ZERO;
    noun slam = build_slam_formula();
    uart_puts("\r\nfock shrine\r\n");

    for (;;) {
        if (setjmp(nock_abort) != 0) {
            uart_puts("\r\nkernel crash\r\n");
            causes = NOUN_ZERO;
            continue;
        }

        noun event;
        if (noun_is_cell(causes)) {
            /* drain pending causes before blocking on UART */
            cell_t *cl = (cell_t *)(uintptr_t)cell_ptr(causes);
            event  = cl->head;
            causes = cl->tail;
        } else {
            event = uart_recv_noun();
        }

        noun subject = alloc_cell(kernel, event);
        noun result  = nock(subject, slam);
        if (!noun_is_cell(result)) { uart_puts("bad result\r\n"); continue; }
        cell_t *r  = (cell_t *)(uintptr_t)cell_ptr(result);
        noun effects = r->head;
        if (!noun_is_cell(r->tail)) { uart_puts("bad result\r\n"); continue; }
        cell_t *r2   = (cell_t *)(uintptr_t)cell_ptr(r->tail);
        kernel       = r2->head;
        causes       = r2->tail;   /* TODO: proper FIFO queue append */
        dispatch_effects(effects);
    }
}

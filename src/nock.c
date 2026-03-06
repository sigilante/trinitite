#include <stdint.h>
#include "noun.h"
#include "nock.h"
#include "uart.h"

/* ── Crash ───────────────────────────────────────────────────────────────── */

static void nock_crash(const char *msg) {
    uart_puts("\r\nnock crash: ");
    uart_puts(msg);
    uart_puts("\r\n");
    for (;;) {}     /* halt — Phase 3b will longjmp to QUIT instead */
}

/* ── Slot  (/[axis subject]) ─────────────────────────────────────────────── */

/*
 * Axis encodes a path through the binary tree.  The leading 1-bit is a
 * sentinel; the remaining bits, read MSB-first, form the path:  0 = head,
 * 1 = tail.
 *
 *   axis 1      → root (whole subject)
 *   axis 2      → head           (path: 0)
 *   axis 3      → tail           (path: 1)
 *   axis 4      → head of head   (path: 0 0)
 *   axis 5      → tail of head   (path: 0 1)
 *   axis 6      → head of tail   (path: 1 0)
 *   axis 7      → tail of tail   (path: 1 1)
 */
noun slot(noun axis, noun subject) {
    if (!noun_is_direct(axis))
        nock_crash("slot axis not direct");

    uint64_t a = direct_val(axis);
    if (a == 0)
        nock_crash("slot axis 0");

    /* find depth = floor(log2(a)) — the number of path bits */
    int depth = 0;
    uint64_t tmp = a;
    while (tmp > 1) { tmp >>= 1; depth++; }

    /* follow path bits from bit (depth-1) down to bit 0 */
    for (int i = depth - 1; i >= 0; i--) {
        if (!noun_is_cell(subject))
            nock_crash("slot in atom");
        cell_t *c = (cell_t *)(uintptr_t)cell_ptr(subject);
        if ((a >> i) & 1)
            subject = c->tail;
        else
            subject = c->head;
    }
    return subject;
}

/* ── Nock eval ───────────────────────────────────────────────────────────── */

noun nock(noun subject, noun formula) {
loop:
    if (!noun_is_cell(formula))
        nock_crash("nock atom");

    cell_t *f = (cell_t *)(uintptr_t)cell_ptr(formula);
    noun head = f->head;
    noun tail = f->tail;

    /* ── Distribution rule: *[a [b c] d] = [*[a b c] *[a d]] ── */
    if (noun_is_cell(head)) {
        noun left  = nock(subject, head);
        noun right = nock(subject, tail);
        return alloc_cell(left, right);
    }

    /* head is an atom — it's the opcode */
    if (!noun_is_direct(head))
        nock_crash("opcode not direct");
    uint64_t op = direct_val(head);

    switch (op) {

    /* ── 0  *[a 0 b]  =  /[b a]  ── */
    case 0:
        return slot(tail, subject);

    /* ── 1  *[a 1 b]  =  b  ── */
    case 1:
        return tail;

    /* ── 2  *[a 2 b c]  =  *[*[a b] *[a c]]  (TCO: loop) ── */
    case 2: {
        if (!noun_is_cell(tail))
            nock_crash("op2 tail not cell");
        cell_t *args = (cell_t *)(uintptr_t)cell_ptr(tail);
        noun new_subj = nock(subject, args->head);
        noun new_form = nock(subject, args->tail); /* still old subject */
        subject = new_subj;
        formula = new_form;
        goto loop;
    }

    /* ── 3  *[a 3 b]  =  ?*[a b]  (wut: 0=cell, 1=atom) ── */
    case 3: {
        noun r = nock(subject, tail);
        return noun_is_cell(r) ? NOUN_YES : NOUN_NO;
    }

    /* ── 4  *[a 4 b]  =  +*[a b]  (lus: increment atom) ── */
    case 4: {
        noun r = nock(subject, tail);
        if (!noun_is_direct(r))
            nock_crash("op4 increment non-direct (bignum NYI)");
        return direct(direct_val(r) + 1);
    }

    /* ── 5  *[a 5 b c]  =  =[*[a b] *[a c]]  (tis: 0=equal, 1=not) ── */
    case 5: {
        if (!noun_is_cell(tail))
            nock_crash("op5 tail not cell");
        cell_t *args = (cell_t *)(uintptr_t)cell_ptr(tail);
        noun left  = nock(subject, args->head);
        noun right = nock(subject, args->tail);
        return noun_eq(left, right) ? NOUN_YES : NOUN_NO;
    }

    default:
        nock_crash("unimplemented opcode");
        return NOUN_ZERO; /* unreachable */
    }
}

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

/* ── Hax  (#[axis val target]) ──────────────────────────────────────────── */

/*
 * Tree edit: replace the noun at axis `a` within `target` with `new_val`.
 * Mirrors the slot path traversal but rebuilds cells on the way back up.
 *
 *   #[1 v t]     = v
 *   #[2 v [h t]] = [v t]
 *   #[3 v [h t]] = [h v]
 *   #[2k v t]    = #[k/even-step …]  (recurse into head subtree)
 *   #[2k+1 v t]  = #[k/odd-step …]   (recurse into tail subtree)
 */
static noun hax(uint64_t a, noun new_val, noun target) {
    if (a == 0)
        nock_crash("edit axis 0");
    if (a == 1)
        return new_val;
    if (!noun_is_cell(target))
        nock_crash("edit in atom");

    cell_t *t = (cell_t *)(uintptr_t)cell_ptr(target);

    /* depth = floor(log2(a)); first path bit selects head(0) vs tail(1) */
    int d = 0;
    uint64_t tmp = a;
    while (tmp > 1) { tmp >>= 1; d++; }

    int first = (int)((a >> (d - 1)) & 1);

    /* sub-axis within the chosen child: strip the leading 1-bit and the
     * first path bit, then re-attach the sentinel 1-bit. */
    uint64_t sub = (a & ((1ULL << (d - 1)) - 1)) | (1ULL << (d - 1));

    if (first == 0)
        return alloc_cell(hax(sub, new_val, t->head), t->tail);
    else
        return alloc_cell(t->head, hax(sub, new_val, t->tail));
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

    /* ── 9  *[a 9 b c]  =  *[*[a c] 0 b]  (arm invocation, TCO) ──
     *
     * Evaluate c against subject to get a core, pull the arm formula
     * at axis b, then evaluate that arm with the core as its own subject.
     * This is every function call in Hoon.
     */
    case 9: {
        if (!noun_is_cell(tail))
            nock_crash("op9 tail not cell");
        cell_t *args = (cell_t *)(uintptr_t)cell_ptr(tail);
        noun b    = args->head;         /* arm axis */
        noun core = nock(subject, args->tail); /* evaluate core expression */
        noun arm  = slot(b, core);      /* pull arm formula from core */
        subject = core;                 /* core is its own subject */
        formula = arm;
        goto loop;                      /* TCO */
    }

    /* ── 6  *[a 6 b c d]  =  if *[a b] then *[a c] else *[a d]  (TCO) ── */
    case 6: {
        if (!noun_is_cell(tail))
            nock_crash("op6 tail not cell");
        cell_t *args = (cell_t *)(uintptr_t)cell_ptr(tail);
        noun b = args->head;                    /* condition formula */
        if (!noun_is_cell(args->tail))
            nock_crash("op6 missing branches");
        cell_t *branches = (cell_t *)(uintptr_t)cell_ptr(args->tail);
        noun cond = nock(subject, b);
        if (noun_eq(cond, NOUN_YES)) {
            formula = branches->head;           /* then-branch c */
            goto loop;
        } else if (noun_eq(cond, NOUN_NO)) {
            formula = branches->tail;           /* else-branch d */
            goto loop;
        } else {
            nock_crash("op6 condition not 0 or 1");
            return NOUN_ZERO;
        }
    }

    /* ── 7  *[a 7 b c]  =  *[*[a b] c]  (compose, TCO) ── */
    case 7: {
        if (!noun_is_cell(tail))
            nock_crash("op7 tail not cell");
        cell_t *args = (cell_t *)(uintptr_t)cell_ptr(tail);
        subject = nock(subject, args->head);
        formula = args->tail;
        goto loop;
    }

    /* ── 8  *[a 8 b c]  =  *[[*[a b] a] c]  (pin, TCO) ── */
    case 8: {
        if (!noun_is_cell(tail))
            nock_crash("op8 tail not cell");
        cell_t *args = (cell_t *)(uintptr_t)cell_ptr(tail);
        noun pinned = nock(subject, args->head);
        subject = alloc_cell(pinned, subject);  /* [*[a b] a] */
        formula = args->tail;
        goto loop;
    }

    /* ── 10  tree edit / hint ─────────────────────────────────────────────
     *
     *  *[a 10 [b c] d]  =  #[b *[a c] *[a d]]   (tree edit)
     *  *[a 10 b c]      =  *[a c]                 (static hint, ignore b)
     */
    case 10: {
        if (!noun_is_cell(tail))
            nock_crash("op10 tail not cell");
        cell_t *args = (cell_t *)(uintptr_t)cell_ptr(tail);
        noun hint = args->head;
        noun d    = args->tail;

        if (noun_is_cell(hint)) {
            /* dynamic: hint = [b c]; edit target at axis b with *[a c] */
            cell_t *hc = (cell_t *)(uintptr_t)cell_ptr(hint);
            noun b = hc->head;
            if (!noun_is_direct(b))
                nock_crash("op10 edit axis not direct");
            noun val    = nock(subject, hc->tail);   /* *[a c] */
            noun target = nock(subject, d);           /* *[a d] */
            return hax(direct_val(b), val, target);
        } else {
            /* static hint: just evaluate d, drop hint atom b */
            formula = d;
            goto loop;
        }
    }

    default:
        nock_crash("unimplemented opcode");
        return NOUN_ZERO; /* unreachable */
    }
}

#include <stdint.h>
#include <stddef.h>
#include "noun.h"
#include "memory.h"

/*
 * Noun heap allocator — bump allocator within HEAP_BASE..HEAP_TOP.
 *
 * For Phase 2 this is intentionally simple: a single bump pointer with no
 * free list.  Refcounting (cell_dec) tracks live cells; full compaction is
 * deferred until a GC pass is needed (Phase 3+).
 *
 * Alignment: all allocations are 8-byte aligned.
 */

static uint8_t *heap_ptr;

void noun_heap_init(void) {
    heap_ptr = (uint8_t *)HEAP_BASE;
}

static void *heap_alloc(size_t bytes) {
    /* round up to 8-byte alignment */
    bytes = (bytes + 7) & ~(size_t)7;
    uint8_t *p = heap_ptr;
    heap_ptr += bytes;
    /* TODO: check heap_ptr < HEAP_TOP and panic on OOM */
    return p;
}

/* ── Cells ──────────────────────────────────────────────────────────────────── */

noun alloc_cell(noun head, noun tail) {
    cell_t *c = heap_alloc(sizeof(cell_t));
    c->refcount = 1;
    c->_pad     = 0;
    c->head     = head;
    c->tail     = tail;
    /* inc refcounts of children */
    if (noun_is_cell(head)) cell_inc(head);
    if (noun_is_cell(tail)) cell_inc(tail);
    return cell_noun((uint32_t)(uintptr_t)c);
}

void cell_inc(noun n) {
    if (!noun_is_cell(n)) return;
    cell_t *c = (cell_t *)(uintptr_t)cell_ptr(n);
    c->refcount++;
}

void cell_dec(noun n) {
    if (!noun_is_cell(n)) return;
    cell_t *c = (cell_t *)(uintptr_t)cell_ptr(n);
    if (--c->refcount == 0) {
        cell_dec(c->head);
        cell_dec(c->tail);
        /* Note: we do not free memory back to bump allocator in Phase 2.
           A compacting pass or slab allocator can reclaim it later. */
    }
}

/* ── Atoms ──────────────────────────────────────────────────────────────────── */

noun alloc_direct(uint64_t val) {
    return direct(val);
}

noun alloc_indirect(uint64_t size_limbs) {
    size_t bytes = sizeof(atom_t) + size_limbs * sizeof(uint64_t);
    atom_t *a = heap_alloc(bytes);
    a->size = size_limbs;
    /* blake3 hash left all-zero until hash_atom() is called (Phase 4b) */
    for (int i = 0; i < 8; i++) a->blake3[i] = 0;
    return indirect((uint32_t)(uintptr_t)a, 0);
}

/* ── Equality ───────────────────────────────────────────────────────────────── */

int noun_eq(noun a, noun b) {
    /* Identical words → equal (covers direct atoms and same-pointer cells) */
    if (a == b) return 1;

    /* Different tags → not equal */
    if ((a & TAG_MASK) != (b & TAG_MASK)) return 0;

    if (noun_is_direct(a)) {
        /* direct atoms: already compared by word equality above */
        return 0;
    }

    if (noun_is_indirect(a) && noun_is_indirect(b)) {
        /* Fast path: compare 30-bit hash prefixes if both non-zero */
        uint32_t pa = indirect_hash_prefix(a);
        uint32_t pb = indirect_hash_prefix(b);
        if (pa && pb && pa != pb) return 0;
        /* Slow path: compare limbs */
        atom_t *aa = (atom_t *)(uintptr_t)indirect_ptr(a);
        atom_t *ba = (atom_t *)(uintptr_t)indirect_ptr(b);
        if (aa->size != ba->size) return 0;
        for (uint64_t i = 0; i < aa->size; i++) {
            if (aa->limbs[i] != ba->limbs[i]) return 0;
        }
        return 1;
    }

    if (noun_is_content(a) && noun_is_content(b)) {
        /* content atoms: identity IS the 62-bit hash */
        return (content_hash(a) == content_hash(b));
    }

    if (noun_is_cell(a) && noun_is_cell(b)) {
        /* structural equality: recurse */
        cell_t *ca = (cell_t *)(uintptr_t)cell_ptr(a);
        cell_t *cb = (cell_t *)(uintptr_t)cell_ptr(b);
        return noun_eq(ca->head, cb->head) && noun_eq(ca->tail, cb->tail);
    }

    return 0;
}

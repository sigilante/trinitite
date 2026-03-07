#pragma once
#include <stdint.h>
#include "noun.h"

/*
 * Bignum arithmetic for indirect (tag=10) atoms.
 *
 * Representation (maintained as invariants by every function here):
 *   - Limbs are little-endian uint64_t: limbs[0] = least significant 64 bits.
 *   - No trailing zero limbs: limbs[size-1] != 0 when size > 1.
 *   - Canonical form: any value < 2^62 is ALWAYS a direct atom.
 *     alloc_indirect is never called when the value fits in 62 bits.
 *
 * The boundary is exactly 2^62 = 0x4000_0000_0000_0000.
 * Incrementing a direct atom with value 2^62-1 produces an indirect atom
 * with size=1, limbs[0]=2^62.
 *
 * All functions return a properly tagged, canonical noun.
 */

/* ── Core ────────────────────────────────────────────────────────────────── */

/*
 * bn_normalize: given a scratch limb array `limbs[0..size-1]`, produce a
 * canonical noun.  Strips trailing zero limbs, then promotes to direct if
 * the value fits in 62 bits, otherwise allocates a fresh indirect atom.
 *
 * The scratch array is read-only; a new atom_t is always allocated when
 * the result must be indirect.
 */
noun bn_normalize(uint64_t *limbs, uint64_t size);

/*
 * bn_inc: Nock op 4 — increment an atom by 1.
 * Handles direct→direct, direct→indirect boundary, and multi-limb carry.
 * Crashes (via nock_crash) if `a` is a cell or unsupported atom type.
 */
noun bn_inc(noun a);

/* ── Phase 4b (decimal I/O) — declared here, implemented later ───────────── */

/*
 * bn_to_decimal: write decimal representation of atom `a` into `buf`.
 * Returns the number of bytes written (no NUL terminator).
 * `buflen` must be at least BN_DECIMAL_MAX.
 */
#define BN_DECIMAL_MAX 512
int  bn_to_decimal(noun a, char *buf, int buflen);

/*
 * bn_from_decimal: parse `len` ASCII decimal digits into a noun atom.
 * No sign, no prefix, no whitespace.  Returns NOUN_ZERO on empty input.
 */
noun bn_from_decimal(const char *buf, int len);

/* ── Phase 11d (jets) — declared here, implemented with jet registration ─── */

noun bn_add(noun a, noun b);   /* addition */
noun bn_dec(noun a);           /* decrement; crashes on zero */

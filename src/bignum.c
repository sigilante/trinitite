#include <stdint.h>
#include "bignum.h"
#include "noun.h"
#include "nock.h"   /* nock_crash (noreturn) */

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Convenience: atom_t* from an indirect noun; looks up atom store. */
static inline atom_t *atom_of(noun n) {
    return atom_store_get(indirect_hash(n));
}

/* ── bn_normalize ────────────────────────────────────────────────────────── */

noun bn_normalize(uint64_t *limbs, uint64_t size) {
    return make_atom(limbs, size);
}

/* ── bn_inc ──────────────────────────────────────────────────────────────── */

/*
 * Increment an atom by 1.
 *
 * Direct atom:
 *   value < 2^63-1  →  direct(value+1)                   (no allocation)
 *   value = 2^63-1  →  indirect, size=1, limbs[0]=2^63   (boundary case)
 *
 * Indirect atom — carry propagation:
 *   Walk limbs from LSL to find first limb that is not UINT64_MAX.
 *   Everything below it wraps to 0.  That limb gets +1.  If all limbs
 *   saturate, extend by one new limb (= 1) at the top.
 *   Result value is always ≥ 2^63+1, so it stays indirect.
 */
noun bn_inc(noun a) {
    /* ── Direct atom ── */
    if (noun_is_direct(a)) {
        uint64_t v = direct_val(a);
        if (v < (1ULL << 63) - 1)
            return direct(v + 1);

        /* Boundary: v == 2^63-1, next value is 2^63 which needs indirect */
        uint64_t limb = 1ULL << 63;
        return make_atom(&limb, 1);
    }

    /* ── Indirect atom ── */
    if (!noun_is_indirect(a))
        nock_crash("bn_inc: unsupported atom type");

    atom_t *src  = atom_of(a);
    uint64_t size = src->size;

    /* Find first limb that will not carry through */
    uint64_t i = 0;
    while (i < size && src->limbs[i] == UINT64_MAX)
        i++;
    /* i == size means all limbs saturated: result needs one more limb */

    uint64_t new_size = (i == size) ? size + 1 : size;
    uint64_t scratch[BN_MAX_LIMBS + 1];

    /* Limbs [0..i-1] wrapped to 0 */
    for (uint64_t j = 0; j < i; j++)
        scratch[j] = 0;

    /* Limb i: incremented, or new MSL from carry-out */
    scratch[i] = (i < size) ? src->limbs[i] + 1 : 1;

    /* Limbs [i+1..size-1] unchanged */
    for (uint64_t j = i + 1; j < size; j++)
        scratch[j] = src->limbs[j];

    /* Result is always ≥ 2^63 (input was indirect with value ≥ 2^63, plus 1).
       make_atom handles canonical form; MSL is non-zero by construction. */
    return make_atom(scratch, new_size);
}

/* ── Internal scratch helpers ─────────────────────────────────────────────── */

/* Extract limbs and size from any atom into a caller-provided scratch array.
   Returns size; caller must ensure scratch has at least BN_MAX_LIMBS slots. */
static uint64_t atom_limbs(noun a, uint64_t *out) {
    if (noun_is_direct(a)) {
        out[0] = direct_val(a);
        return 1;
    }
    atom_t *at = atom_of(a);
    uint64_t sz = at->size < BN_MAX_LIMBS ? at->size : BN_MAX_LIMBS;
    for (uint64_t i = 0; i < sz; i++) out[i] = at->limbs[i];
    return sz;
}

/* ── bn_cmp ───────────────────────────────────────────────────────────────── */

int bn_cmp(noun a, noun b) {
    uint64_t la[BN_MAX_LIMBS], lb[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la);
    uint64_t sb = atom_limbs(b, lb);
    if (sa != sb) return sa < sb ? -1 : 1;
    for (int64_t i = (int64_t)sa - 1; i >= 0; i--) {
        if (la[i] != lb[i]) return la[i] < lb[i] ? -1 : 1;
    }
    return 0;
}

/* ── bn_dec ───────────────────────────────────────────────────────────────── */

noun bn_dec(noun a) {
    if (noun_is_direct(a)) {
        uint64_t v = direct_val(a);
        if (v == 0) nock_crash("bn_dec: underflow");
        return direct(v - 1);
    }
    if (!noun_is_indirect(a)) nock_crash("bn_dec: unsupported atom type");

    atom_t *src = atom_of(a);
    uint64_t size = src->size;

    /* Find first non-zero limb — that is where the borrow stops. */
    uint64_t i = 0;
    while (i < size && src->limbs[i] == 0) i++;
    if (i == size) nock_crash("bn_dec: underflow (indirect zero)");

    uint64_t scratch[BN_MAX_LIMBS];
    for (uint64_t j = 0; j < size; j++) scratch[j] = src->limbs[j];

    /* Limbs [0..i-1] borrow-wrap 0 → UINT64_MAX */
    for (uint64_t j = 0; j < i; j++) scratch[j] = UINT64_MAX;
    scratch[i]--;

    return bn_normalize(scratch, size);
}

/* ── bn_add ───────────────────────────────────────────────────────────────── */

noun bn_add(noun a, noun b) {
    uint64_t la[BN_MAX_LIMBS], lb[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la);
    uint64_t sb = atom_limbs(b, lb);
    uint64_t sr = (sa > sb ? sa : sb) + 1;

    uint64_t lr[BN_MAX_LIMBS + 1];
    uint64_t carry = 0;
    for (uint64_t i = 0; i < sr; i++) {
        uint64_t x = i < sa ? la[i] : 0;
        uint64_t y = i < sb ? lb[i] : 0;
        __uint128_t sum = (__uint128_t)x + y + carry;
        lr[i]  = (uint64_t)sum;
        carry  = (uint64_t)(sum >> 64);
    }
    return bn_normalize(lr, sr);
}

/* ── bn_sub ───────────────────────────────────────────────────────────────── */

noun bn_sub(noun a, noun b) {
    if (bn_cmp(a, b) < 0) nock_crash("bn_sub: underflow");

    uint64_t la[BN_MAX_LIMBS], lb[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la);
    uint64_t sb = atom_limbs(b, lb);

    uint64_t lr[BN_MAX_LIMBS];
    uint64_t borrow = 0;
    for (uint64_t i = 0; i < sa; i++) {
        uint64_t x = la[i];
        uint64_t y = i < sb ? lb[i] : 0;
        uint64_t diff = x - y - borrow;
        borrow = (x < y + borrow || (borrow && y == UINT64_MAX)) ? 1 : 0;
        lr[i] = diff;
    }
    return bn_normalize(lr, sa);
}

/* ── Decimal I/O ──────────────────────────────────────────────────────────── */

/* Divide scratch[0..sz-1] by 10 in-place; return remainder (0–9). */
static uint64_t bn_div10_inplace(uint64_t *limbs, uint64_t sz) {
    uint64_t rem = 0;
    for (int64_t i = (int64_t)sz - 1; i >= 0; i--) {
        __uint128_t cur = ((__uint128_t)rem << 64) | limbs[i];
        limbs[i] = (uint64_t)(cur / 10);
        rem      = (uint64_t)(cur % 10);
    }
    return rem;
}

int bn_to_decimal(noun a, char *buf, int buflen) {
    uint64_t scratch[BN_MAX_LIMBS];
    uint64_t sz = atom_limbs(a, scratch);

    /* Special case: zero */
    if (sz == 1 && scratch[0] == 0) {
        if (buflen < 1) return 0;
        buf[0] = '0';
        return 1;
    }

    /* Collect digits in reverse order. */
    char tmp[BN_DECIMAL_MAX];
    int pos = 0;
    while (sz > 1 || scratch[0] != 0) {
        uint64_t rem = bn_div10_inplace(scratch, sz);
        while (sz > 1 && scratch[sz - 1] == 0) sz--;
        if (pos < BN_DECIMAL_MAX) tmp[pos++] = (char)('0' + rem);
    }

    if (pos > buflen) return 0;
    for (int i = 0; i < pos; i++) buf[i] = tmp[pos - 1 - i];
    return pos;
}

/* Global buffer used by the Forth N. word (not reentrant). */
char bn_decimal_buf[BN_DECIMAL_MAX];

int bn_to_decimal_fill(noun a) {
    return bn_to_decimal(a, bn_decimal_buf, BN_DECIMAL_MAX);
}

/* Multiply limbs[0..sz-1] by 10 and add digit d (0–9).
   Writes result into out[0..sz] (at most sz+1 limbs). Returns new size. */
static uint64_t do_mul10_add(const uint64_t *limbs, uint64_t sz,
                              uint8_t d, uint64_t *out) {
    uint64_t carry = d;
    for (uint64_t i = 0; i < sz; i++) {
        __uint128_t prod = (__uint128_t)limbs[i] * 10 + carry;
        out[i] = (uint64_t)prod;
        carry  = (uint64_t)(prod >> 64);
    }
    if (carry) { out[sz] = carry; return sz + 1; }
    return sz;
}

noun bn_from_decimal(const char *buf, int len) {
    if (len <= 0) return NOUN_ZERO;

    uint64_t scratch[BN_MAX_LIMBS + 1];
    scratch[0] = 0;
    uint64_t sz = 1;

    for (int i = 0; i < len; i++) {
        uint8_t d = (uint8_t)(buf[i] - '0');
        uint64_t tmp[BN_MAX_LIMBS + 1];
        sz = do_mul10_add(scratch, sz, d, tmp);
        if (sz > BN_MAX_LIMBS) sz = BN_MAX_LIMBS;
        for (uint64_t j = 0; j < sz; j++) scratch[j] = tmp[j];
    }
    return bn_normalize(scratch, sz);
}

/* ── bn_met ───────────────────────────────────────────────────────────────── */

uint64_t bn_met(noun a) {
    if (!noun_is_atom(a)) nock_crash("bn_met: cell");
    if (noun_is_direct(a)) {
        uint64_t v = direct_val(a);
        if (v == 0) return 0;
        return 64 - (uint64_t)__builtin_clzll(v);
    }
    atom_t *at = atom_of(a);
    uint64_t top = at->limbs[at->size - 1];
    return 64 * (at->size - 1) + (64 - (uint64_t)__builtin_clzll(top));
}

/* ── bn_bex ───────────────────────────────────────────────────────────────── */

noun bn_bex(uint64_t k) {
    uint64_t word_idx = k / 64;
    uint64_t bit_idx  = k % 64;
    if (word_idx >= BN_MAX_LIMBS) nock_crash("bn_bex: exponent too large");
    uint64_t scratch[BN_MAX_LIMBS];
    for (uint64_t i = 0; i < word_idx + 1; i++) scratch[i] = 0;
    scratch[word_idx] = 1ULL << bit_idx;
    return bn_normalize(scratch, word_idx + 1);
}

/* ── bn_lsh ───────────────────────────────────────────────────────────────── */

noun bn_lsh(noun a, uint64_t k) {
    if (k == 0) return a;
    uint64_t la[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la);
    uint64_t word_shift = k / 64;
    uint64_t bit_shift  = k % 64;
    uint64_t result_size = sa + word_shift + (bit_shift > 0 ? 1 : 0);
    if (result_size > BN_MAX_LIMBS) nock_crash("bn_lsh: result too large");
    uint64_t result[BN_MAX_LIMBS];
    for (uint64_t i = 0; i < result_size; i++) result[i] = 0;
    if (bit_shift == 0) {
        for (uint64_t i = 0; i < sa; i++) result[i + word_shift] = la[i];
    } else {
        uint64_t carry = 0;
        for (uint64_t i = 0; i < sa; i++) {
            result[i + word_shift] = (la[i] << bit_shift) | carry;
            carry = la[i] >> (64 - bit_shift);
        }
        if (carry) result[sa + word_shift] = carry;
    }
    return bn_normalize(result, result_size);
}

/* ── bn_rsh ───────────────────────────────────────────────────────────────── */

noun bn_rsh(noun a, uint64_t k) {
    if (k == 0) return a;
    uint64_t la[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la);
    uint64_t word_shift = k / 64;
    uint64_t bit_shift  = k % 64;
    if (word_shift >= sa) return NOUN_ZERO;
    uint64_t result_size = sa - word_shift;
    uint64_t result[BN_MAX_LIMBS];
    if (bit_shift == 0) {
        for (uint64_t i = 0; i < result_size; i++)
            result[i] = la[i + word_shift];
    } else {
        for (uint64_t i = 0; i < result_size; i++) {
            result[i] = la[i + word_shift] >> bit_shift;
            if (i + word_shift + 1 < sa)
                result[i] |= la[i + word_shift + 1] << (64 - bit_shift);
        }
    }
    return bn_normalize(result, result_size);
}

/* ── Bitwise ops ─────────────────────────────────────────────────────────── */

noun bn_or(noun a, noun b) {
    uint64_t la[BN_MAX_LIMBS], lb[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la), sb = atom_limbs(b, lb);
    uint64_t sr = sa > sb ? sa : sb;
    uint64_t result[BN_MAX_LIMBS];
    for (uint64_t i = 0; i < sr; i++)
        result[i] = (i < sa ? la[i] : 0) | (i < sb ? lb[i] : 0);
    return bn_normalize(result, sr);
}

noun bn_and(noun a, noun b) {
    uint64_t la[BN_MAX_LIMBS], lb[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la), sb = atom_limbs(b, lb);
    uint64_t sr = sa < sb ? sa : sb;  /* AND: zero-extension kills higher bits */
    uint64_t result[BN_MAX_LIMBS];
    for (uint64_t i = 0; i < sr; i++) result[i] = la[i] & lb[i];
    return bn_normalize(result, sr);
}

noun bn_xor(noun a, noun b) {
    uint64_t la[BN_MAX_LIMBS], lb[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la), sb = atom_limbs(b, lb);
    uint64_t sr = sa > sb ? sa : sb;
    uint64_t result[BN_MAX_LIMBS];
    for (uint64_t i = 0; i < sr; i++)
        result[i] = (i < sa ? la[i] : 0) ^ (i < sb ? lb[i] : 0);
    return bn_normalize(result, sr);
}

/* ── Division helpers ────────────────────────────────────────────────────── */

/*
 * divlu64: compute floor((u1:u0) / v) where u1 < v (quotient fits in 64 bits).
 * Stores remainder in *rem_out if non-NULL.
 *
 * Uses restoring binary long division — 64 iterations, no 128-bit division.
 * libgcc (__udivti3) is unavailable in our freestanding environment, so we
 * avoid __uint128_t / and % entirely.
 *
 * Correctness of the 64-bit wrap-around:
 *   The invariant 0 <= r < v is maintained throughout.  When carry=1 the
 *   mathematical intermediate is 2^64 + r_shifted >= 2^64 > v, so we always
 *   subtract.  The invariant guarantees r_shifted < v in that case, so
 *   r_shifted - v wraps to 2^64 + r_shifted - v, which is the correct
 *   mathematical remainder (and < v < 2^64, so it fits in 64 bits).
 */
static uint64_t divlu64(uint64_t u1, uint64_t u0, uint64_t v, uint64_t *rem_out)
{
    uint64_t q = 0, r = u1;
    for (int i = 63; i >= 0; i--) {
        uint64_t carry = r >> 63;
        r = (r << 1) | ((u0 >> i) & 1);
        if (carry || r >= v) {
            r -= v;
            q |= (1ULL << i);
        }
    }
    if (rem_out) *rem_out = r;
    return q;
}

/*
 * div1: divide u[0..m-1] by single 64-bit limb v.
 * Writes quotient into q[0..m-1].  Returns remainder (< v).
 * Each step maintains rem < v, so divlu64's precondition is satisfied.
 */
static uint64_t div1(const uint64_t *u, uint64_t m, uint64_t v, uint64_t *q) {
    uint64_t rem = 0;
    for (int64_t i = (int64_t)m - 1; i >= 0; i--)
        q[i] = divlu64(rem, u[i], v, &rem);
    return rem;
}

/*
 * divmod_knuth: Knuth Algorithm D (TAOCP §4.3.1) for n >= 2.
 *
 * u[0..m+n]: normalized dividend (u[m+n] is the extra leading limb, may be 0).
 *            Modified in place; u[0..n-1] holds the (still-shifted) remainder
 *            on return.
 * v[0..n-1]: normalized divisor with v[n-1] >= 2^63.
 * q[0..m]:   receives quotient limbs (q[m] is always 0 for normalized input).
 *
 * Caller un-shifts the remainder by the D1 shift value.
 *
 * Normalization guarantees u[j+n] < v[n-1] at every step, so divlu64's
 * precondition (numerator_high < denominator) is always satisfied.
 */
static void divmod_knuth(uint64_t *u, uint64_t m,
                         const uint64_t *v, uint64_t n,
                         uint64_t *q)
{
    for (int64_t j = (int64_t)m; j >= 0; j--) {
        /* D3: estimate qhat = floor((u[j+n]:u[j+n-1]) / v[n-1])
         * u[j+n] < v[n-1] by normalization, so divlu64 precondition holds. */
        uint64_t qhat = divlu64(u[j+n], u[j+n-1], v[n-1], NULL);
        __uint128_t rhat = ((__uint128_t)u[j+n] << 64) | u[j+n-1];
        rhat -= (__uint128_t)qhat * v[n-1];

        /* D3 refinement: while qhat*v[n-2] > B*rhat + u[j+n-2], decrement */
        while (rhat < ((__uint128_t)1 << 64)) {
            __uint128_t lhs = (__uint128_t)qhat * v[n-2];
            __uint128_t rhs = (rhat << 64) | u[j+n-2];
            if (lhs <= rhs) break;
            qhat--;
            rhat += v[n-1];
        }

        /* D4: u[j..j+n] -= qhat * v[0..n-1]  (tracked with signed 128-bit) */
        __int128_t borrow = 0;
        for (uint64_t i = 0; i <= n; i++) {
            uint64_t vi = (i < n) ? v[i] : 0;
            __int128_t t = (__int128_t)u[j+i] - (__int128_t)qhat * vi + borrow;
            u[j+i] = (uint64_t)t;
            borrow  = t >> 64;
        }

        /* D5: if borrow < 0, qhat was 1 too large — add v back once */
        if (borrow < 0) {
            qhat--;
            uint64_t carry = 0;
            for (uint64_t i = 0; i < n; i++) {
                __uint128_t s = (__uint128_t)u[j+i] + v[i] + carry;
                u[j+i] = (uint64_t)s;
                carry   = (uint64_t)(s >> 64);
            }
            u[j+n] += carry;
        }

        q[j] = qhat;
    }
}

/*
 * Internal: divmod_impl — compute quotient and/or remainder.
 * Both a and b must be atoms.  Crashes on division by zero.
 */
static void bn_divmod_impl(noun a, noun b, noun *q_out, noun *r_out) {
    if (!noun_is_atom(a) || !noun_is_atom(b))
        nock_crash("bn_divmod: non-atom");

    uint64_t lb[BN_MAX_LIMBS];
    uint64_t sb = atom_limbs(b, lb);
    if (sb == 1 && lb[0] == 0)
        nock_crash("bn_divmod: division by zero");

    /* Trivial: a < b → quotient 0, remainder a */
    if (bn_cmp(a, b) < 0) {
        if (q_out) *q_out = NOUN_ZERO;
        if (r_out) *r_out = a;
        return;
    }

    uint64_t la[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la);

    /* Single-limb divisor: fast path using div1 */
    if (sb == 1) {
        uint64_t qscratch[BN_MAX_LIMBS];
        uint64_t rem = div1(la, sa, lb[0], qscratch);
        if (q_out) *q_out = bn_normalize(qscratch, sa);
        if (r_out) *r_out = make_atom(&rem, 1);
        return;
    }

    /* Multi-limb: Knuth Algorithm D */
    uint64_t n = sb;                /* divisor limb count, n >= 2 */
    uint64_t m = sa - n;           /* sa >= n guaranteed by bn_cmp check above */

    /* D1: normalize — shift so v[n-1] has its high bit set */
    uint64_t shift = (uint64_t)__builtin_clzll(lb[n-1]);

    uint64_t v[BN_MAX_LIMBS];
    if (shift == 0) {
        for (uint64_t i = 0; i < n; i++) v[i] = lb[i];
    } else {
        v[0] = lb[0] << shift;
        for (uint64_t i = 1; i < n; i++)
            v[i] = (lb[i] << shift) | (lb[i-1] >> (64 - shift));
    }

    /* Dividend u[0..m+n]: shifted, with one extra limb u[sa] = 0 or carry */
    uint64_t u[BN_MAX_LIMBS + 1];
    if (shift == 0) {
        for (uint64_t i = 0; i < sa; i++) u[i] = la[i];
        u[sa] = 0;
    } else {
        u[0] = la[0] << shift;
        for (uint64_t i = 1; i < sa; i++)
            u[i] = (la[i] << shift) | (la[i-1] >> (64 - shift));
        u[sa] = la[sa-1] >> (64 - shift);
    }

    uint64_t q[BN_MAX_LIMBS + 1];
    for (uint64_t i = 0; i <= m; i++) q[i] = 0;

    divmod_knuth(u, m, v, n, q);

    if (q_out) *q_out = bn_normalize(q, m + 1);

    if (r_out) {
        if (shift == 0) {
            *r_out = bn_normalize(u, n);
        } else {
            uint64_t r[BN_MAX_LIMBS];
            for (uint64_t i = 0; i < n - 1; i++)
                r[i] = (u[i] >> shift) | (u[i+1] << (64 - shift));
            r[n-1] = u[n-1] >> shift;
            *r_out = bn_normalize(r, n);
        }
    }
}

/* ── bn_mul ──────────────────────────────────────────────────────────────── */

noun bn_mul(noun a, noun b) {
    uint64_t la[BN_MAX_LIMBS], lb[BN_MAX_LIMBS];
    uint64_t sa = atom_limbs(a, la), sb = atom_limbs(b, lb);

    /* Zero fast path */
    if (sa == 1 && la[0] == 0) return NOUN_ZERO;
    if (sb == 1 && lb[0] == 0) return NOUN_ZERO;

    uint64_t sr = sa + sb;
    uint64_t result[BN_MAX_LIMBS * 2];
    for (uint64_t i = 0; i < sr; i++) result[i] = 0;

    for (uint64_t i = 0; i < sa; i++) {
        uint64_t carry = 0;
        for (uint64_t j = 0; j < sb; j++) {
            __uint128_t prod = (__uint128_t)la[i] * lb[j] + result[i+j] + carry;
            result[i+j] = (uint64_t)prod;
            carry       = (uint64_t)(prod >> 64);
        }
        /* Propagate remaining carry upward */
        for (uint64_t k = i + sb; carry; k++) {
            __uint128_t sum = (__uint128_t)result[k] + carry;
            result[k] = (uint64_t)sum;
            carry     = (uint64_t)(sum >> 64);
        }
    }
    return bn_normalize(result, sr);
}

/* ── bn_div / bn_mod ─────────────────────────────────────────────────────── */

noun bn_div(noun a, noun b) {
    noun q;
    bn_divmod_impl(a, b, &q, NULL);
    return q;
}

noun bn_mod(noun a, noun b) {
    noun r;
    bn_divmod_impl(a, b, NULL, &r);
    return r;
}

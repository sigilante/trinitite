#!/usr/bin/env bash
# Trinitite jet benchmarks — informational, always exits 0
#
# Boots the kernel in QEMU and runs BENCH on each target word ITERS times,
# printing elapsed CNTVCT_EL0 ticks.  No assertions are made — ticks in QEMU
# are not real-time, but are consistent within a single run and useful for
# spotting gross regressions or improvements.
#
# Benchmark targets:
#   NOOP  — baseline (loop overhead only)
#   BCONS — noun allocation: 1 >NOUN 2 >NOUN CONS DROP
#   BNOCK — plain nock() via NOCK word, op1 (quote 42)
#   BSKNK — ska_nock() via SKNOCK word, op1 (quote 42)
#   BDEC  — C jet dec via NOCK+%wild (hot_state dispatch)
#   BSDEC — Forth jet dec via SKNOCK (find_by_cord dispatch)
#   BADD  — C jet add via NOCK+%wild
#   BSADD — Forth jet add via SKNOCK
#
# Usage: ./tests/run_bench.sh [--verbose]

set -euo pipefail
cd "$(dirname "$0")/.."

ITERS=1000
VERBOSE=0
[[ "${1-}" == "--verbose" ]] && VERBOSE=1

# ── Preamble: helpers + Forth jet definitions ─────────────────────────────
PREAMBLE=': N>N >NOUN ;
: JCORE1 0 N>N CONS 0 N>N SWAP CONS ;
: JCORE2 CONS 0 N>N CONS 0 N>N SWAP CONS ;
: JD 1 N>N SWAP CONS 2 N>N SWAP CONS 9 N>N SWAP CONS ;
: JWRAP SWAP 1 N>N 0 N>N CONS CONS 0 N>N CONS 1 N>N SWAP CONS 1684826487 N>N SWAP CONS SWAP CONS 11 N>N SWAP CONS ;
: NOOP ;
: dec 6 >NOUN SWAP SLOT NOUN> 1 - >NOUN ;
: add DUP 12 >NOUN SWAP SLOT NOUN> SWAP 13 >NOUN SWAP SLOT NOUN> + >NOUN ;'

# ── Benchmark word definitions ─────────────────────────────────────────────
# All words must be stack-neutral (consume and produce nothing).
# cord("dec") = 6514020  cord("add") = 6579297  cord(%wild) = 1684826487
DEFS=': BCONS 1 >NOUN 2 >NOUN CONS DROP ;
: BNOCK 0 N>N 1 N>N 42 N>N CONS NOCK DROP ;
: BSKNK 0 N>N 1 N>N 42 N>N CONS SKNOCK DROP ;
: BDEC 0 N>N 6514020 N>N 5 N>N JCORE1 JD JWRAP NOCK DROP ;
: BSDEC 0 N>N 6514020 N>N 5 N>N JCORE1 JD JWRAP SKNOCK DROP ;
: BADD 0 N>N 6579297 N>N 3 N>N 4 N>N JCORE2 JD JWRAP NOCK DROP ;
: BSADD 0 N>N 6579297 N>N 3 N>N 4 N>N JCORE2 JD JWRAP SKNOCK DROP ;'

# ── Benchmark labels and commands ─────────────────────────────────────────
BENCH_LABELS=(
    "NOOP  (loop baseline)"
    "BCONS (noun alloc)"
    "BNOCK (plain nock/op1)"
    "BSKNK (ska_nock/op1)"
    "BDEC  (C jet dec)"
    "BSDEC (Forth jet dec)"
    "BADD  (C jet add)"
    "BSADD (Forth jet add)"
)

BENCH_CMDS=(
    "' NOOP  ${ITERS} BENCH ."
    "' BCONS ${ITERS} BENCH ."
    "' BNOCK ${ITERS} BENCH ."
    "' BSKNK ${ITERS} BENCH ."
    "' BDEC  ${ITERS} BENCH ."
    "' BSDEC ${ITERS} BENCH ."
    "' BADD  ${ITERS} BENCH ."
    "' BSADD ${ITERS} BENCH ."
)

# ── Build input ────────────────────────────────────────────────────────────
INPUT="$PREAMBLE
$DEFS"
for cmd in "${BENCH_CMDS[@]}"; do
    INPUT+=$'\n'"$cmd"
done

[[ $VERBOSE -eq 1 ]] && echo "=== Input ===" && printf '%s\n' "$INPUT" && echo "============="

# ── Run QEMU ───────────────────────────────────────────────────────────────
RAW=$({ printf '%s\n' "$INPUT"; sleep 3; printf '\001x'; } | \
    timeout 60 qemu-system-aarch64 -machine raspi4b -kernel kernel8.img \
        -display none -nographic 2>/dev/null || true)

[[ $VERBOSE -eq 1 ]] && echo "=== Raw output ===" && printf '%s\n' "$RAW" && echo "=================="

# ── Parse hex outputs ──────────────────────────────────────────────────────
RESULTS=()
while IFS= read -r line; do
    line="${line%$'\r'}"
    if [[ "$line" =~ ^([0-9A-Fa-f]{16})[[:space:]]+ok ]]; then
        RESULTS+=("${BASH_REMATCH[1]^^}")
    fi
done <<< "$RAW"

# ── Print results table ────────────────────────────────────────────────────
echo "=== Trinitite Jet Benchmarks (${ITERS} iterations, CNTVCT_EL0 ticks) ==="
echo ""
printf "%-26s  %s\n" "Benchmark" "Ticks (hex)"
printf "%-26s  %s\n" "--------------------------" "----------------"

N="${#BENCH_LABELS[@]}"
MISSING=0
for (( i=0; i<N; i++ )); do
    ticks="${RESULTS[$i]-MISSING}"
    [[ "$ticks" == "MISSING" ]] && (( ++MISSING )) || true
    printf "%-26s  %s\n" "${BENCH_LABELS[$i]}" "$ticks"
done

echo ""
echo "(Lower = faster.  QEMU ticks are not wall-clock time but are"
echo " consistent within a run — useful for relative comparison.)"

if (( MISSING > 0 )); then
    echo ""
    echo "WARNING: $MISSING benchmark(s) produced no output"
fi

exit 0

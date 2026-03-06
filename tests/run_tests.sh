#!/usr/bin/env bash
# Fock regression test suite — Nock opcodes 0-5 + SLOT
# Usage: ./tests/run_tests.sh [--verbose]
#
# Each test feeds one Forth expression to the REPL and checks the hex
# output value.  All tests run in a single QEMU session for speed.

set -euo pipefail
cd "$(dirname "$0")/.."

VERBOSE=0
[[ "${1-}" == "--verbose" ]] && VERBOSE=1

RED='\033[0;31m'
GRN='\033[0;32m'
YEL='\033[0;33m'
NC='\033[0m'

PASS=0
FAIL=0

# ── Test registry ──────────────────────────────────────────────────────────
# Parallel arrays: TNAMES[], TEXPECT[], TLINES[]
TNAMES=()
TEXPECT=()
TLINES=()

T() {   # T  "description"  "expected-hex-uppercase-16"  "forth expression"
    TNAMES+=("$1")
    TEXPECT+=("$2")
    TLINES+=("$3")
}

# ── Preamble (defines helpers, produces no numeric output) ─────────────────
PREAMBLE=': N>N >NOUN ;
: C>N N>N SWAP N>N SWAP CONS ;'

# ── Slot / op 0 ────────────────────────────────────────────────────────────
# SLOT word direct
T "SLOT axis 1 (root atom)"     "000000000000002A" "1 N>N  42 N>N  SLOT NOUN> ."
T "SLOT axis 2 (head)"          "0000000000000001" "2 N>N  1 2 C>N  SLOT NOUN> ."
T "SLOT axis 3 (tail)"          "0000000000000002" "3 N>N  1 2 C>N  SLOT NOUN> ."
T "SLOT axis 4 (head.head)"     "0000000000000001" "4 N>N  1 2 C>N  3 4 C>N CONS  SLOT NOUN> ."
T "SLOT axis 5 (tail.head)"     "0000000000000002" "5 N>N  1 2 C>N  3 4 C>N CONS  SLOT NOUN> ."
T "SLOT axis 6 (head.tail)"     "0000000000000003" "6 N>N  1 2 C>N  3 4 C>N CONS  SLOT NOUN> ."
T "SLOT axis 7 (tail.tail)"     "0000000000000004" "7 N>N  1 2 C>N  3 4 C>N CONS  SLOT NOUN> ."

# Via NOCK op 0
T "op0: *[42 [0 1]] = 42"       "000000000000002A" \
    "42 N>N  0 N>N 1 N>N CONS  NOCK NOUN> ."
T "op0: *[[1 2] [0 2]] = 1"     "0000000000000001" \
    "1 2 C>N  0 N>N 2 N>N CONS  NOCK NOUN> ."
T "op0: *[[1 2] [0 3]] = 2"     "0000000000000002" \
    "1 2 C>N  0 N>N 3 N>N CONS  NOCK NOUN> ."
T "op0: axis 4 (head.head)"     "0000000000000001" \
    "1 2 C>N  3 4 C>N CONS  0 N>N 4 N>N CONS  NOCK NOUN> ."
T "op0: axis 5 (tail.head)"     "0000000000000002" \
    "1 2 C>N  3 4 C>N CONS  0 N>N 5 N>N CONS  NOCK NOUN> ."
T "op0: axis 6 (head.tail)"     "0000000000000003" \
    "1 2 C>N  3 4 C>N CONS  0 N>N 6 N>N CONS  NOCK NOUN> ."
T "op0: axis 7 (tail.tail)"     "0000000000000004" \
    "1 2 C>N  3 4 C>N CONS  0 N>N 7 N>N CONS  NOCK NOUN> ."

# ── Op 1: quote ────────────────────────────────────────────────────────────
T "op1: *[_ [1 42]] = 42"       "000000000000002A" \
    "0 N>N  1 N>N 42 N>N CONS  NOCK NOUN> ."
T "op1: *[_ [1 0]] = 0"         "0000000000000000" \
    "0 N>N  1 N>N 0 N>N CONS  NOCK NOUN> ."
T "op1: quoted cell head = 1"   "0000000000000001" \
    "0 N>N  1 N>N 1 2 C>N CONS  NOCK CAR NOUN> ."
T "op1: quoted cell tail = 2"   "0000000000000002" \
    "0 N>N  1 N>N 1 2 C>N CONS  NOCK CDR NOUN> ."

# ── Op 2: compose/eval (TCO) ───────────────────────────────────────────────
# *[99 [2 [1 42] [1 [0 1]]]] = *[42 [0 1]] = 42
T "op2: compose atom result"    "000000000000002A" \
    "99 N>N  2 N>N  1 N>N 42 N>N CONS  1 N>N 0 N>N 1 N>N CONS CONS  CONS  CONS  NOCK NOUN> ."
# *[_ [2 [1 5] [1 [4 [0 1]]]]] = *[5 [4 [0 1]]] = +5 = 6
T "op2: compose then lus"       "0000000000000006" \
    "0 N>N  2 N>N  1 N>N 5 N>N CONS  1 N>N 4 N>N 0 N>N 1 N>N CONS CONS CONS  CONS  CONS  NOCK NOUN> ."

# ── Op 3: wut (is cell?) ───────────────────────────────────────────────────
T "op3: ?atom = 1 (no)"         "0000000000000001" \
    "42 N>N  3 N>N  0 N>N 1 N>N CONS  CONS  NOCK NOUN> ."
T "op3: ?cell = 0 (yes)"        "0000000000000000" \
    "1 2 C>N  3 N>N  0 N>N 1 N>N CONS  CONS  NOCK NOUN> ."
T "op3: ?quoted-cell = 0"       "0000000000000000" \
    "0 N>N  3 N>N  1 N>N 1 2 C>N CONS  CONS  NOCK NOUN> ."

# ── Op 4: lus (increment atom) ─────────────────────────────────────────────
T "op4: +0 = 1"                 "0000000000000001" \
    "0 N>N  4 N>N  0 N>N 1 N>N CONS  CONS  NOCK NOUN> ."
T "op4: +100 = 101"             "0000000000000065" \
    "100 N>N  4 N>N  0 N>N 1 N>N CONS  CONS  NOCK NOUN> ."
T "op4: +255 = 256"             "0000000000000100" \
    "255 N>N  4 N>N  0 N>N 1 N>N CONS  CONS  NOCK NOUN> ."

# ── Op 5: tis (equality) ───────────────────────────────────────────────────
T "op5: =[42 42] = 0 (yes)"     "0000000000000000" \
    "42 N>N  5 N>N  0 N>N 1 N>N CONS  0 N>N 1 N>N CONS  CONS CONS  NOCK NOUN> ."
T "op5: =[1 2] = 1 (no)"        "0000000000000001" \
    "1 2 C>N  5 N>N  0 N>N 2 N>N CONS  0 N>N 3 N>N CONS  CONS CONS  NOCK NOUN> ."
# =[43 +42] = =[43 43] = 0 (yes)
T "op5: =[43 +42] = 0 (yes)"   "0000000000000000" \
    "42 N>N  5 N>N  1 N>N 43 N>N CONS  4 N>N 0 N>N 1 N>N CONS CONS  CONS CONS  NOCK NOUN> ."
# =[99 +42] = =[99 43] = 1 (no)
T "op5: =[99 +42] = 1 (no)"    "0000000000000001" \
    "42 N>N  5 N>N  1 N>N 99 N>N CONS  4 N>N 0 N>N 1 N>N CONS CONS  CONS CONS  NOCK NOUN> ."

# ── Distribution rule (autocons) ───────────────────────────────────────────
# *[42 [[0 1] [4 [0 1]]]] = [42 43]
T "distrib: head = 42"          "000000000000002A" \
    "42 N>N  0 N>N 1 N>N CONS  4 N>N 0 N>N 1 N>N CONS CONS  CONS  NOCK CAR NOUN> ."
T "distrib: tail = 43"          "000000000000002B" \
    "42 N>N  0 N>N 1 N>N CONS  4 N>N 0 N>N 1 N>N CONS CONS  CONS  NOCK CDR NOUN> ."
# *[_ [[1 7] [1 8]]] = [7 8]
T "distrib: two constants"      "0000000000000007" \
    "0 N>N  1 N>N 7 N>N CONS  1 N>N 8 N>N CONS  CONS  NOCK CAR NOUN> ."

# ── Build input and run ────────────────────────────────────────────────────
INPUT="$PREAMBLE"
for line in "${TLINES[@]}"; do
    INPUT+=$'\n'"$line"
done

RAW=$(printf '%s\n' "$INPUT" | \
    timeout 15 qemu-system-aarch64 -machine raspi3b -kernel kernel8.img \
        -display none -nographic 2>/dev/null || true)

# Extract 16-char uppercase hex values that appear before "  ok"
ACTUAL=()
while IFS= read -r line; do
    line="${line%$'\r'}"   # strip CR from CRLF
    if [[ "$line" =~ ^([0-9A-Fa-f]{16})[[:space:]]+ok ]]; then
        ACTUAL+=("${BASH_REMATCH[1]^^}")  # uppercase
    fi
done <<< "$RAW"

# Compare
N="${#TNAMES[@]}"
ACTUAL_N="${#ACTUAL[@]}"

if (( ACTUAL_N != N )); then
    echo -e "${YEL}WARNING: expected $N results, got $ACTUAL_N${NC}"
fi

for (( i=0; i<N; i++ )); do
    exp="${TEXPECT[$i]}"
    got="${ACTUAL[$i]-MISSING}"
    if [[ "$got" == "$exp" ]]; then
        (( PASS++ ))
        [[ $VERBOSE -eq 1 ]] && echo -e "${GRN}PASS${NC}  ${TNAMES[$i]}"
    else
        (( FAIL++ ))
        echo -e "${RED}FAIL${NC}  ${TNAMES[$i]}"
        echo "      expected: $exp"
        echo "      got:      $got"
    fi
done

echo ""
TOTAL=$(( PASS + FAIL ))
if (( FAIL == 0 )); then
    echo -e "${GRN}$PASS/$TOTAL passed${NC}"
else
    echo -e "${RED}$PASS/$TOTAL passed, $FAIL failed${NC}"
    exit 1
fi

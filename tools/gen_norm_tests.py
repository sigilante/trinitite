#!/usr/bin/env python3
"""Generate Forth test lines from Nock reference tests (tests.json).

Reads the canonical test vectors and outputs T/BEFORE lines compatible
with tests/run_tests.sh.  Each test builds subject + formula as Forth
nouns, runs NOCK, builds the expected result, and compares with =NOUN.

Crash tests (result=null) use the BEFORE mechanism: the crash line
precedes a recovery-verification test.

Usage:
    python3 tools/gen_norm_tests.py /path/to/tests.json
"""

import json, sys


def tokenize(s):
    tokens = []
    i = 0
    while i < len(s):
        if s[i] in ' \t\n':
            i += 1
        elif s[i] in '[]':
            tokens.append(s[i])
            i += 1
        else:
            j = i
            while j < len(s) and s[j] not in ' \t\n[]':
                j += 1
            tokens.append(s[i:j])
            i = j
    return tokens


def parse(tokens, pos=0):
    """Parse noun notation → tree (int or (head, tail) tuple)."""
    if tokens[pos] == '[':
        pos += 1
        elems = []
        while tokens[pos] != ']':
            elem, pos = parse(tokens, pos)
            elems.append(elem)
        pos += 1  # skip ']'
        # Right-associate: [a b c] = (a, (b, c))
        result = elems[-1]
        for i in range(len(elems) - 2, -1, -1):
            result = (elems[i], result)
        return result, pos
    else:
        return int(tokens[pos]), pos + 1


def to_forth(tree):
    """Convert noun tree → Forth code that builds it on the data stack."""
    if isinstance(tree, int):
        return f"{tree} N>N"
    head, tail = tree
    return f"{to_forth(head)}  {to_forth(tail)}  CONS"


def parse_noun(s):
    tokens = tokenize(s)
    tree, _ = parse(tokens)
    return tree


def escape_desc(s):
    """Sanitize description for bash double-quoted string."""
    return s.replace('"', "'").replace('\\', '\\\\')


def main():
    if len(sys.argv) < 2:
        print("Usage: gen_norm_tests.py <tests.json>", file=sys.stderr)
        sys.exit(1)

    tests = json.load(open(sys.argv[1]))

    print("# ── Nock Reference Tests (generated from norm/tests.json) ────────────────")
    print("# Each test builds subject+formula, runs NOCK, compares result with =NOUN.")
    print()

    for t in tests:
        desc = escape_desc(t['description'])
        subj_forth = to_forth(parse_noun(t['subject']))
        form_forth = to_forth(parse_noun(t['formula']))

        if t['result'] is None:
            # Crash test: BEFORE triggers crash, T verifies recovery
            print(f'BEFORE "{subj_forth}  {form_forth}  NOCK DROP"')
            print(f'T "norm: {desc} (crash recovers)" "000000000000002A" "42 ."')
        else:
            result_forth = to_forth(parse_noun(t['result']))
            expr = f"{subj_forth}  {form_forth}  NOCK  {result_forth}  =NOUN ."
            print(f'T "norm: {desc}" "FFFFFFFFFFFFFFFF" \\')
            print(f'    "{expr}"')

    print()
    print(f"# {len(tests)} norm tests generated")


if __name__ == '__main__':
    main()

# Fock — Agent Briefing

## What This Project Is

**Fock** is a bare-metal Nock VM implemented as a Forth kernel on AArch64 (Raspberry Pi 3/4).
The name reflects the dual nature: **Fo**rth + No**ck**.

The research thesis: a Nock 4K evaluator can run as Forth words on bare-metal AArch64, with
jets implemented as live-patchable Forth words registered in the Forth dictionary. This is
architecturally novel relative to existing Nock runtimes (Vere, Sword) which use static C jet
tables requiring recompilation.

This is a solo, unfunded research artifact. The goal is a working proof of concept, not a
production system.

## Repository Layout

```
nockout/                    ← project root (git repo)
├── Makefile                ← build system (lives here, not in src/)
├── AGENTS.md               ← this file
└── src/
    ├── boot.s              ← AArch64 entry point, core parking, BSS zero
    ├── uart.c              ← PL011 UART driver (for QEMU; real Pi needs GPIO mux)
    ├── uart.h
    ├── memory.h            ← physical memory map constants (single source of truth)
    ├── main.c              ← C entry: uart_init, stack canary, calls forth_main
    ├── forth.s             ← THE MAIN FILE: entire Forth kernel in AArch64 asm
    └── linker.ld           ← linker script, entry at 0x80000
```

## Build System

```bash
make          # build kernel8.img
make run      # run in QEMU (Ctrl-A X to quit)
make debug    # QEMU + GDB server on :1234, breaks at main
make deploy   # copy kernel8.img to /private/tftpboot for Pi netboot
make clean
```

Toolchain: `aarch64-elf-gcc`, `aarch64-elf-ld`, `aarch64-elf-objcopy`, `aarch64-elf-gdb`
Install: `brew install aarch64-elf-gcc aarch64-elf-binutils aarch64-elf-gdb qemu tio`

QEMU target: `-machine raspi3b`. UART maps to stdio via `-nographic`.

## Memory Map (src/memory.h)

```
0x00080000  kernel load address / boot stack (grows down, small)
0x00089000  TIB (terminal input buffer, 256 bytes)
0x00090000  Forth dictionary base — grows UP           (4MB)
0x00470000  Stack guard / canary (STACK_CANARY = 0xDEADF0C4)
0x00470000  Forth data stack top — grows DOWN          (64KB)
0x00480000  Forth return stack top — grows DOWN        (64KB)
0x00490000  Noun event arena base — bump alloc         (32MB)
0x02490000  Noun persistent heap base — refcounted     (64MB)
0x3F000000  MMIO (do not use for heap)
```

All addresses are absolute physical. Noun cell pointers are always absolute —
never region-relative offsets. This is deliberate: it allows the heap to grow
without rewriting existing pointers.

## AArch64 Register Conventions (src/forth.s)

**RESERVED — never clobber:**
```
x27  IP   — Instruction Pointer (next word in current definition)
x26  DSP  — Data Stack Pointer (grows down, points TO top item)
x25  RSP  — Return Stack Pointer (grows down, points TO top item)
x24  W    — Working register (current dictionary entry address)
```

**RESERVED for future noun heap (Phase 2+):**
```
x19-x23   — available for noun heap pointers, GC roots, etc.
```

**Scratch — any word may clobber:**
```
x0-x18
```

Stack push/pop convention:
```asm
// Push x0:
str x0, [DSP, #-8]!   // pre-decrement then store

// Pop into x0:
ldr x0, [DSP], #8     // load then post-increment
```

## Dictionary Entry Layout (src/forth.s)

Every word (primitive or colon definition) has this exact header:

```
offset  0 : link      [8 bytes] — pointer to previous entry (0 = end of chain)
offset  8 : flags|len [8 bytes] — low byte = name length, bits 15:8 = flags
offset 16 : name      [8 bytes] — ASCII name, zero-padded to 8 bytes (max 7 chars)
offset 24 : codeword  [8 bytes] — pointer to machine code to execute
offset 32 : body               — varies by word type (see below)
```

Flag bits (applied to the HIGH byte of the flags|len field, i.e. shifted << 8):
```
F_IMMEDIATE = 0x80
F_HIDDEN    = 0x40
```

Body interpretation:
- **defcode** (primitive): body is native AArch64 machine code, ends with NEXT
- **DOCON** words: body[0] is the constant value
- **DOVAR** words: body[0] is the variable's storage cell
- **DOCOL** words (colon defs): body is a list of entry addresses (execution tokens)

## Inner Interpreter

**NEXT macro** — dispatches to next word in current definition:
```asm
ldr  W, [IP], #8      // W = entry address at IP; IP advances by 8
ldr  x0, [W, #24]     // x0 = codeword (at entry + 24)
br   x0               // jump to codeword
```

**DOCOL** — enters a colon definition:
```asm
str  IP, [RSP, #-8]!  // push current IP to return stack
add  IP, W, #32       // IP = body of this word (W + 32)
NEXT                   // dispatch first word in body
```

**EXIT** — leaves a colon definition:
```asm
ldr  IP, [RSP], #8    // pop saved IP from return stack
NEXT                   // resume caller
```

**EXECUTE** — execute word whose entry address is on the data stack:
```asm
ldr  W, [DSP], #8     // pop entry address into W
ldr  x0, [W, #24]     // load codeword
br   x0               // dispatch (no NEXT — executed word calls its own NEXT)
```

## Assembler Macros (src/forth.s)

```asm
defcode "NAME", len, label, flags
    // Creates header + codeword pointing to immediately following asm
    // Asm code follows; must end with NEXT
    // Entry address: word_<label>
    // Code address:  code_<label>

defvar "NAME", len, label, flags, initial_value
    // Creates header with codeword = DOVAR and one storage cell
    // Executing the word pushes the ADDRESS of the storage cell
    // Storage cell address: word_<label> + 32

defconst "NAME", len, label, flags, value
    // Creates header with codeword = DOCON and one value cell
    // Executing the word pushes the VALUE
```

The `link` assembler symbol is updated by each `defword` invocation and ends up
holding the address of the last defined word. `forth_main` patches this into
LATEST's storage cell at runtime.

## QUIT Loop (the outer interpreter)

QUIT is a `defcode` (primitive), not a colon definition. It implements the
standard Forth outer interpreter:

1. Reset both stacks (also serves as ABORT target)
2. Read a line from UART into TIB (inline REFILL)
3. For each space-delimited token:
   - Search dictionary (FIND): if found, execute or compile depending on STATE
     and F_IMMEDIATE flag
   - If not found, try NUMBER: if parseable, push or compile LIT+value
   - If neither: print " ?" and restart
4. At end of line: print " ok" (if interpreting), print prompt, go to 2

**Trampoline mechanism**: When QUIT executes a word, it sets IP to
`trampoline_quit` (a cell containing `word_quit`'s address) before branching to
the codeword. This gives NEXT a valid IP to return to after the word completes,
causing it to loop back into QUIT.

## Current Status

**Phase 0: COMPLETE**
- QEMU boots, UART works, netboot configured.

**Phase 1: COMPLETE**
- REPL boots: `Fock v0.1  AArch64 Forth` banner + `> ` prompt.
- Full primitive set: arithmetic, stack ops, comparisons, memory, I/O.
- Colon definitions (`: ;`), RECURSE.
- Control flow: `IF ELSE THEN`, `BEGIN UNTIL AGAIN`, `BEGIN WHILE REPEAT`.
- `.` prints hex intentionally — decimal requires bignum (Phase 4). Do not change.

**Phase 2: COMPLETE**
- noun.h/noun.c: 4-type tagged noun, bump allocator, refcount, noun_eq.
- Forth words: `>NOUN`, `NOUN>`, `CONS`, `CAR`, `CDR`, `ATOM?`, `CELL?`, `=NOUN`.
- Bug fixed: `noun_is_atom` and `ATOM?`/`CELL?` checked bit63 only, missing direct atoms (tag=01).
  Corrected to `(n >> 62) != 0`.

**Phase 3: COMPLETE (opcodes 0–5)**
- `src/nock.c`: `slot()` and `nock()` implementing opcodes 0–5.
- Opcode 2 uses `goto loop` for TCO (no stack growth on compose/eval).
- Crash behaviour: `nock_crash()` prints to UART and halts; longjmp recovery is Phase 3b.
- Forth words: `SLOT ( axis noun -- result )`, `NOCK ( subject formula -- product )`.
- All opcodes 0–5 smoke-tested via REPL.
- Next: opcodes 6–9 (compound), then 10 (edit/hint), 11 (hint/dynamic).

## Immediate Tasks for This Agent

1. Create `src/noun.h` — tag constants and pack/unpack macros for all four noun types.
2. Create `src/noun.c` — `alloc_cell()`, `cell_inc()`, `cell_dec()`,
   `alloc_indirect()` for type-10 atoms (hash field zeroed initially).
3. Add Forth words (in `src/forth.s`): `CONS`, `CAR`, `CDR`, `ATOM?`, `CELL?`, `=noun`.
4. Smoke-test via REPL: `1 2 CONS CDR .` etc.

## Noun Representation

Every noun is a 64-bit word. The top two bits are the tag:

```
Bits 63:62  tag
  00  cell            bits 61:0 = 32-bit heap pointer to [head, tail] pair
                      bits 61:32 = 30 bits reserved (GC metadata, TBD)
  01  direct atom     bits 61:0 = value  (0 .. 2^62-1)
  10  indirect atom   bits 61:32 = low 30 bits of BLAKE3 hash (fast equality pre-check)
                      bits 31:0  = 32-bit pointer to atom struct in RAM
  11  content atom    bits 61:0  = 62-bit BLAKE3 prefix (identity IS the hash)
                      actual limb data lives in the atom store (RAM cache / SD card)
```

Heap struct for type-10 (indirect) atoms:
```c
struct atom {
    uint64_t  size;      // number of 64-bit limbs
    uint32_t  blake3[8]; // full 256-bit BLAKE3 hash (all-zero = not yet computed)
    uint64_t  limbs[];   // little-endian limb data
};
```

Tag constants (defined in `src/noun.h`):
```c
#define TAG_CELL     (0ULL << 62)
#define TAG_DIRECT   (1ULL << 62)
#define TAG_INDIRECT (2ULL << 62)
#define TAG_CONTENT  (3ULL << 62)
#define TAG_MASK     (3ULL << 62)
```

Notes:
- Direct atoms cover all values up to ~4.6×10^18 — sufficient for most Nock programs.
- Type-11 (content atom) is reserved for Phase 4b; stubs only in Phase 2.
- The 30-bit hash prefix in type-10 words is the low 30 bits of the full BLAKE3 hash,
  written once when `hash_atom()` is first called on that atom.
- For large atoms (4GB+) that cannot be RAM-resident, type-11 content addressing is
  the correct representation. The atom store hot cache maps 62-bit hash → atom struct;
  the cold store (Phase 6) backs this with SD card block I/O.

## BLAKE3 Plan

- **Hash function**: BLAKE3, truncated to 62 bits for type-11 content atoms.
- **Why BLAKE3**: Merkle tree structure allows hashing 4GB+ atoms in 1KB streaming
  chunks without full RAM residency. Same hash regardless of chunk boundaries.
- **Implementation**: Roll our own C implementation from the BLAKE3 spec (Nockchain
  uses Rust; the official reference C implementation is the closest starting point).
  Target: `src/blake3.c` + `src/blake3.h`, no libc, no SIMD required initially.
  Core is ~400 lines: G mixing function (7 rounds), chunk compression, Merkle parents.
- **When needed**: Phase 4b (after bignum arithmetic). Phase 2 allocates type-10 atoms
  with the hash field zeroed; hashing is deferred until Phase 4b.

## Phase 2 Implementation Order

```
Phase 2a  src/noun.h      — tag constants, NOUN typedef, pack/unpack macros
Phase 2b  src/noun.c      — alloc_cell(), cell_inc(), cell_dec()
                            alloc_indirect() — hash field left zero
Phase 2c  src/forth.s     — CONS, CAR, CDR, ATOM?, CELL?, =noun Forth words
Phase 2d  smoke test       — 1 2 CONS CDR . → 2 etc.
```

## Subject Knowledge Analysis (Phase 5 Design)

Reference: Afonin ~dozreg-toplud, "Subject Knowledge Analysis", UTJ Vol. 3 Issue 1.

SKA is a static analysis pass that takes a `(subject, formula)` pair and produces:
1. A **call graph** — every Nock 2/9 site annotated as *direct* (formula statically known) or *indirect*
2. A **subject mask** (`$cape`) — which axes of the subject are used *as code*

**How it works**: Run a partial Nock interpreter symbolically. Subject is `$sock = (cape, data)` where
unknown parts are stubbed with 0. Propagate known information through the formula tree. At each
Nock 2 site, if the formula operand is fully known, record the callee and enter a new analysis frame;
if not, mark the call indirect and return unknown result.

**Why it matters for Fock**:

- **Jet matching becomes compile-time**: Walk the call graph once; at each direct call site compute
  the battery hash and look it up in the Forth dictionary. If found, annotate the call with a direct
  Forth word pointer. No per-call hash lookup at runtime.
- **Direct calls**: Annotated direct calls skip the full `nock()` eval and dispatch straight to the
  callee's Forth word or compiled bytecode. Paper reports ~1.7× speedup.
- **Correct cache keying**: Cache key is `(masked_subject, formula)` not `(full_subject, formula)`.
  Without the mask, changing a counter value in the subject causes a cache miss even when the code
  hasn't changed. The mask says "only these axes matter for the call graph."
- **Nomm**: SKA output is annotated Nock where Nock 2 sites carry `info=(unit [sock formula])`.
  Our equivalent: a Forth word whose body contains direct `bl` instructions to known call targets.

**Loop handling (required, not optional)**: A naive partial evaluator loops forever on `dec`.
Must detect backedges (Tarjan SCC), defer fixpoint search to SCC exit, then validate.

**When to implement**: Phase 5. Entry point is the `%fast` hint (cold jet registration) and
eventually `%ska` hint. The `$cape` boolean-tree type needs to be added to `noun.h`.

**Indirect call exceptions** (rare): over-the-wire code, Nock 12, vase-mode Hoon compiler.
All other Arvo/Gall code is fully analyzable statically.

## Architecture Decisions (do not reverse without understanding the rationale)

| Decision | Choice | Rationale |
|---|---|---|
| UART driver | PL011 at 0x3F201000 | QEMU raspi3b emulates PL011, not mini-UART |
| Atom tag scheme | 2-bit tag, 4 types | Covers direct/indirect/content; see Noun Representation |
| Large atom identity | BLAKE3 content hash (62 bits) | Enables O(1) equality, structural sharing, SD-card backing for 4GB+ atoms |
| Memory model | Arena + refcount heap | No stop-world GC; event arena reset after each +poke |
| Noun stack | Separate from Forth stacks | GC root discipline; no mixed-type stack bugs |
| Jets | Forth dictionary entries + SKA | Live-patchable; compile-time matching via subject mask |
| Loom/road | REJECTED | 32-bit legacy; replaced by 64-bit arena+refcount |
| Bignum | Roll our own (Phase 4) | FSL bignum is wrong license; other options unsuitable |
| BLAKE3 | Roll our own C (Phase 4b) | Nockchain is Rust; reference C impl is the base |
| Forth kernel | Hand-written AArch64 asm | No suitable MIT-licensed vendorable option |
| Kernel noun shape | Deferred | Arvo (+poke) vs Shrine (x/y/z queries) — decide before Phase 6 |

## Phase Plan Summary

| Phase | Description | Status |
|---|---|---|
| 0 | Toolchain, QEMU, Hello UART | DONE |
| 1 | Forth kernel: inner interpreter + REPL + control flow | DONE |
| 2 | Noun heap: tagged pointers, cell alloc, refcount | IN PROGRESS |
| 3 | Nock 4K eval loop (all 12 opcodes, TCO trampoline) | TODO |
| 4 | Bignum atoms | TODO |
| 4b | BLAKE3 implementation + hash_atom() + intern() | TODO |
| 5 | Jet registry + SKA (compile-time jet matching, direct calls, subject mask) | TODO |
| 6 | jam/cue + SD card load + atom cold store | TODO |
| 7 | Kernel loop: +poke event dispatch, effects vocabulary | TODO |
| N | North integration (parallel track) | ONGOING |

PoC gate: Phases 0-5. Everything after is "turning it into an OS."

## Key Source References in the Codebase

- `src/memory.h` — single source of truth for all region boundaries
- `src/noun.h`   — noun tag constants, NOUN typedef, pack/unpack macros (Phase 2+)
- `src/noun.c`   — cell/atom allocators, refcount (Phase 2+)
- `src/forth.s`  — Forth kernel: inner interpreter, primitives, QUIT, control flow
- `src/boot.s`   — entry at `_start`, parks cores 1-3, zeros BSS, calls `main`
- `src/uart.c`   — PL011 UART init/read/write (no GPIO mux — QEMU doesn't need it)
- `src/main.c`   — writes stack canary, calls `forth_main`

## What This Is NOT

- Not a port of any existing Forth (not lbForth, not zForth, not jonesforth)
- Not GPL-encumbered (everything is written from scratch, MIT)
- Not targeting a hosted OS (bare metal only, no libc, no syscalls)
- Not Arvo (kernel noun shape TBD; may be Shrine instead)
- Not trying to be ANS Forth compliant (standard-ish but pragmatic)

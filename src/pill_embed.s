// Embedded PILL — linked into .rodata so it is present on real hardware
// where the QEMU -device loader is unavailable.
//
// pill_load() in noun.c checks PILL_BASE first (QEMU override), then
// falls back to this embedded copy.  Build with an empty pill.bin stub
// when no pill is available.
//
// Symbols:
//   _pill_embed_start  — first byte of pill.bin data
//   _pill_embed_end    — one past last byte

    .section .rodata
    .balign 8
    .global _pill_embed_start
_pill_embed_start:
    .incbin "pill.bin"
    .global _pill_embed_end
_pill_embed_end:

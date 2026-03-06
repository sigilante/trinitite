.section ".text.boot"
.global _start

_start:
    // Check core ID — only core 0 runs
    // Cores 1-3 park in WFE loop
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbnz    x0, .Lpark

    // Set up stack pointer
    // Stack grows down from our load address
    ldr     x0, =_start
    mov     sp, x0

    // Zero BSS segment
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
.Lzero_bss:
    cmp     x0, x1
    b.ge    .Lbss_done
    str     xzr, [x0], #8
    b       .Lzero_bss
.Lbss_done:

    // Call C entry point
    bl      main
    // If main returns (it shouldn't), park
    b       .Lpark

.Lpark:
    wfe
    b       .Lpark

.global _start
.section .text.boot

_start:
    // Only allow primary core (CPU 0) to boot; spin others
    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    cbnz x0, 3f

    // Set up stack pointer in high RAM (QEMU virt RAM starts at 0x40000000)
    ldr x1, =0x48000000
    mov sp, x1

    // Zero out the BSS section
    ldr x1, =__bss_start
    ldr x2, =__bss_end
1:  cmp x1, x2
    b.ge 2f
    str wzr, [x1], #4
    b 1b

2:
    // Jump to C++ entry point
    bl kernel_main

3:
    // Hang secondary cores in low-power state
    wfi
    b 3b

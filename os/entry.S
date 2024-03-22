    .section .text.entry
    .globl _entry
_entry:
    fence.i
    // if we use the `load` command in gdb to reload the kernel image,
    //   the satp is already set by last initialization.
    //   we should clear the satp.
    csrw satp, zero
    la sp, boot_stack_top
    call main

    .section .bss.stack
    .globl boot_stack
boot_stack:
    .space 4096 * 16
    .globl boot_stack_top
boot_stack_top:

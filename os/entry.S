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

.globl secondary_cpu_entry
secondary_cpu_entry:
    # RISC-V SBI Spec: Ch.9 Hart State Management Extension
    #    sbi_hart_start: a0: hartid, a1: opaque (sp)
    fence.i
    la sp, boot_stack_top
    call secondary_main_pa

    .section .bss.stack
    .globl boot_stack
boot_stack:
    .space 4096
    .globl boot_stack_top
boot_stack_top:

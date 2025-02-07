#include "trap.h"

#include "console.h"
#include "debug.h"
#include "plic.h"
#include "timer.h"
#include "defs.h"

void plic_handle() {
    int irq = plic_claim();
    if (irq == UART0_IRQ) {
        uart_intr();
        // printf("intr %d: UART0\n", r_tp());
    }

    if (irq)
        plic_complete(irq);
}

void kernel_trap(struct ktrapframe *ktf) {
    assert(!intr_get());

    if ((r_sstatus() & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");

    if (mycpu()->inkernel_trap) {
        // Prevent nested kernel trap, including nested interrupt, and exception during kernel_trap (called `Double Fault` in x86)
        print_sysregs(true);
        print_ktrapframe(ktf);
        panic("nested kerneltrap");
    }
    mycpu()->inkernel_trap = 1;

    uint64 cause          = r_scause();
    uint64 exception_code = cause & SCAUSE_EXCEPTION_CODE_MASK;
    if (cause & SCAUSE_INTERRUPT) {
        switch (exception_code) {
            case SupervisorTimer:
                tracef("kernel timer interrupt, cycle: %d", r_time());
                set_next_timer();
                // we never preempt kernel threads.
                goto free;
            case SupervisorExternal:
                tracef("s-external interrupt from kerneltrap!");
                plic_handle();
                goto free;
            default:
                panic("kerneltrap entered with unhandled interrupt. %p", cause);
        }
    }

    print_sysregs(true);
    print_ktrapframe(ktf);

    panic("trap from kernel");

free:
    assert(!intr_get());
    mycpu()->inkernel_trap = 0;
    return;
}

void set_kerneltrap() {
    assert(IS_ALIGNED((uint64)kernel_trap_entry, 4));
    w_stvec((uint64)kernel_trap_entry);  // DIRECT
}

// set up to take exceptions and traps while in the kernel.
void trap_init() {
    set_kerneltrap();
}

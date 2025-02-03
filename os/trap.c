#include "trap.h"
#include "defs.h"
#include "console.h"
#include "loader.h"
#include "debug.h"
#include "syscall.h"
#include "timer.h"

extern char trampoline[], uservec[];
extern char userret[];

void plic_handle()
{
	int irq = plic_claim();
	if (irq == UART0_IRQ) {
		uart_intr();
		// printf("intr %d: UART0\n", r_tp());
	}

	if (irq)
		plic_complete(irq);
}

void kernel_trap(struct ktrapframe *ktf)
{
	assert(!intr_get());

	if ((r_sstatus() & SSTATUS_SPP) == 0)
		panic("kerneltrap: not from supervisor mode");

	if (mycpu()->inkernel_trap) {
		print_sysregs(true);
		print_ktrapframe(ktf);
		panic("nested kerneltrap");
	}
	mycpu()->inkernel_trap = 1;

	// in case some push_off/pop_off accidentally open the interrupt
	int interrupt_save = mycpu()->interrupt_on;
	mycpu()->interrupt_on = false;

	uint64 cause = r_scause();
	uint64 exception_code = cause & SCAUSE_EXCEPTION_CODE_MASK;
	if (cause & SCAUSE_INTERRUPT) {
		switch (exception_code) {
		case SupervisorTimer:
			tracef("kernel timer interrupt");
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
	mycpu()->interrupt_on = interrupt_save;
	return;
}

extern char kernel_trap_entry[];
void set_kerneltrap()
{
	w_stvec((uint64)kernel_trap_entry & ~0x3); // DIRECT
}

// set up to take exceptions and traps while in the kernel.
void trap_init()
{
	set_kerneltrap();
}

void unknown_trap()
{
	errorf("unknown trap: %p, stval = %p", r_scause(), r_stval());
	exit(-1);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap()
{
	set_kerneltrap();
	// assert(mycpu()->interrupt_on == true);
	assert(mycpu()->noff == 0);
	assert(!intr_get());

	struct trapframe *trapframe = curr_proc()->trapframe;
	tracef("trap from user epc = %p", trapframe->epc);
	// print_trapframe(trapframe);
	if ((r_sstatus() & SSTATUS_SPP) != 0)
		panic("usertrap: not from user mode");

	uint64 cause = r_scause();
	if (cause & (1ULL << 63)) {
		// check the 63-bit of scause: Interrupt
		cause &= ~(1ULL << 63);
		switch (cause) {
		case SupervisorTimer:
			tracef("time interrupt!");
			set_next_timer();
			yield();
			break;
		case SupervisorExternal:
			tracef("s-external interrupt from usertrap!");
			plic_handle();
			break;
		default:
			unknown_trap();
			break;
		}
	} else {
		switch (cause) {
		case UserEnvCall:
			trapframe->epc += 4;
			intr_on();
			syscall();
			intr_off();
			break;
		case LoadPageFault:
		case StorePageFault:
		case InstructionPageFault: {
			uint64 addr = r_stval();
			pagetable_t pgt = curr_proc()->mm->pgt;
			// vm_print(pgt);
			pte_t *pte = walk(curr_proc()->mm, addr, 0);
			if (pte != NULL && (*pte & PTE_V)) {
				*pte |= PTE_A;
				if (cause == StorePageFault)
					*pte |= PTE_D;
				sfence_vma();
				break;
			}
		}
		case StoreMisaligned:
		case InstructionMisaligned:
		case LoadMisaligned:
			errorf("%d in application, bad addr = %p, bad instruction = %p, "
			       "core dumped.",
			       cause, r_stval(), trapframe->epc);
			vm_print(curr_proc()->mm->pgt);
			exit(-2);
			break;
		case IllegalInstruction:
			errorf("IllegalInstruction in application, core dumped.");
			exit(-3);
			break;
		default:
			unknown_trap();
			break;
		}
	}
	usertrapret();
}

//
// return to user space
//
void usertrapret()
{
	if (intr_get())
		panic("usertrapret entered with intr on");

	struct trapframe *trapframe = curr_proc()->trapframe;
	trapframe->kernel_satp = r_satp(); // kernel page table
	trapframe->kernel_sp = curr_proc()->kstack + KERNEL_STACK_SIZE; // process's kernel stack
	trapframe->kernel_trap = (uint64)usertrap;
	trapframe->kernel_hartid = r_tp(); // unuesd

	w_sepc(trapframe->epc);
	// set up the registers that trampoline.S's sret will use
	// to get to user space.

	// set S Previous Privilege mode to User.
	uint64 x = r_sstatus();
	x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
	x |= SSTATUS_SPIE; // enable interrupts in user mode
	w_sstatus(x);

	// tell trampoline.S the user page table to switch to.
	uint64 satp = MAKE_SATP(KVA_TO_PA(curr_proc()->mm->pgt));
	uint64 stvec = (TRAMPOLINE + (uservec - trampoline)) & ~0x3;

	uint64 fn = TRAMPOLINE + (userret - trampoline);
	tracef("return to user @%p, fn %p", trapframe->epc);
	((void (*)(uint64, uint64, uint64))fn)(TRAPFRAME, satp, stvec);
}
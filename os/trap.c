#include "trap.h"
#include "defs.h"
#include "loader.h"
#include "syscall.h"
#include "timer.h"

extern char trampoline[], uservec[];
extern char userret[];

void kerneltrap() __attribute__((aligned(16)))
__attribute__((interrupt("supervisor")));	// gcc will generate codes for context saving and restoring.

void kerneltrap()
{
	if ((r_sstatus() & SSTATUS_SPP) == 0)
		panic("kerneltrap: not from supervisor mode");

	uint64 cause = r_scause();
	if (cause & (1ULL << 63)) {
		panic("kerneltrap enter with interrupt scause");
	}
	uint64 addr = r_stval();
	pagetable_t pgt = SATP_TO_PGTABLE(r_satp());
	pte_t *pte = walk(pgt, addr, 0);
	if (pte == NULL)
		panic("kernel pagefault at %p", addr);
	switch (cause) {
	case InstructionPageFault:
	case LoadPageFault:
		*pte |= PTE_A;
		break;
	case StorePageFault:
		*pte |= PTE_A | PTE_D;
		break;

	default:
		panic("trap from kernel");
	}
}

// set up to take exceptions and traps while in the kernel.
void set_usertrap()
{
	w_stvec(((uint64)TRAMPOLINE + (uservec - trampoline)) & ~0x3); // DIRECT
}

void set_kerneltrap()
{
	w_stvec((uint64)kerneltrap & ~0x3); // DIRECT
}

// set up to take exceptions and traps while in the kernel.
void trap_init()
{
	// intr_on();
	set_kerneltrap();
}

void unknown_trap()
{
	errorf("unknown trap: %p, stval = %p", r_scause(), r_stval());
	exit(-1);
}

void print_trapframe(struct trapframe* tf) {
	printf("trapframe at %p\n", tf);
	printf("ra :%p  sp :%p  gp: %p  tp: %p\n", tf->ra, tf->sp, tf->gp, tf->tp);
	printf("t0 :%p  t1 :%p  t2: %p  s0: %p\n", tf->t0, tf->t1, tf->t2, tf->s0);
	printf("s1 :%p  a0 :%p  a1: %p  a2: %p\n", tf->s1, tf->a0, tf->a1, tf->a2);
	printf("a3 :%p  a4 :%p  a5: %p  a6: %p\n", tf->a3, tf->a4, tf->a5, tf->a6);
	printf("a7 :%p  s2 :%p  s3: %p  s4: %p\n", tf->a7, tf->s2, tf->s3, tf->s4);
	printf("s5 :%p  s6 :%p  s7: %p  s8: %p\n", tf->s5, tf->s6, tf->s7, tf->s8);
	printf("s9 :%p  s10:%p  s11:%p  t3: %p\n", tf->s9, tf->s10, tf->s11, tf->t3);
	printf("t4 :%p  t5:%p   t6 :%p  \n\n", tf->t4, tf->t5, tf->t6);
}

void print_sysregs() {
	uint64 sstatus = r_sstatus();
	uint64 sie = r_sie();
	uint64 sepc = r_sepc();
	uint64 stval = r_stval();
	uint64 sip = r_sip();
	uint64 satp = r_satp();
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap()
{
	set_kerneltrap();
	struct trapframe *trapframe = curr_proc()->trapframe;
	tracef("trap from user epc = %p", trapframe->epc);
	// print_trapframe(trapframe);
	if ((r_sstatus() & SSTATUS_SPP) != 0)
		panic("usertrap: not from user mode");

	uint64 cause = r_scause();
	if (cause & (1ULL << 63)) {
		cause &= ~(1ULL << 63);
		switch (cause) {
		case SupervisorTimer:
			tracef("time interrupt!");
			set_next_timer();
			yield();
			break;
		default:
			unknown_trap();
			break;
		}
	} else {
		switch (cause) {
		case UserEnvCall:
			trapframe->epc += 4;
			syscall();
			break;
		case LoadPageFault:
		case StorePageFault:
		case InstructionPageFault: {
			uint64 addr = r_stval();
			pagetable_t pgt = curr_proc()->pagetable;
			// vm_print(pgt);
			pte_t *pte = walk(pgt, addr, 0);
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
			vm_print(curr_proc()->pagetable);
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
	set_usertrap();
	struct trapframe *trapframe = curr_proc()->trapframe;
	trapframe->kernel_satp = r_satp(); // kernel page table
	trapframe->kernel_sp =
		curr_proc()->kstack + KSTACK_SIZE; // process's kernel stack
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
	uint64 satp = MAKE_SATP(curr_proc()->pagetable);
	uint64 fn = TRAMPOLINE + (userret - trampoline);
	tracef("return to user @ %p", trapframe->epc);
	((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}
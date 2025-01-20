#include "trap.h"
#include "defs.h"
#include "loader.h"
#include "syscall.h"
#include "timer.h"

extern char trampoline[], uservec[];
extern char userret[];

void kerneltrap() __attribute__((aligned(16))) __attribute__((interrupt("supervisor")));
// gcc will generate codes for context saving and restoring.

static int in_kerneltrap = 0;
void print_sysregs();

void kerneltrap()
{
	register uint64 *saved_regs;
	asm volatile("mv %0, s0" : "=r"(saved_regs));
	uint64 *gprs = saved_regs - 17;

	printf("kernel trap: %p\n", saved_regs);
	print_sysregs();
	printf("ra: %p\n", gprs[17]);
	printf("ra: %p\n", gprs[16]);

	if ((r_sstatus() & SSTATUS_SPP) == 0)
		panic("kerneltrap: not from supervisor mode");

	uint64 cause = r_scause();
	if (cause & (1ULL << 63))
		panic("kerneltrap entered with interrupt scause");

	if (in_kerneltrap)
		panic("nested kerneltrap");
	in_kerneltrap = 1;

	panic("trap from kernel");

	// switch (cause) {
	// case InstructionPageFault:
	// case LoadPageFault:
	// case StorePageFault: {
	// 	uint64 addr = r_stval();
	// 	pagetable_t pgt = SATP_TO_PGTABLE(r_satp());
	// 	pte_t *pte = walk(pgt, addr, 0);
	// 	if (pte == NULL)
	// 		panic("kernel pagefault at %p", addr);
	// 	if (cause == StorePageFault)
	// 		*pte |= PTE_A | PTE_D;
	// 	else
	// 		*pte |= PTE_A;
	// }
	// default:
	// }
}

void set_kerneltrap()
{
	w_stvec((uint64)kerneltrap & ~0x3); // DIRECT
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
	struct trapframe *trapframe = curr_proc()->trapframe;
	trapframe->kernel_satp = r_satp(); // kernel page table
	trapframe->kernel_sp = curr_proc()->kstack + KSTACK_SIZE; // process's kernel stack
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
	tracef("return to user @ %p, fn %p", trapframe->epc, fn);
	((void (*)(uint64, uint64, uint64))fn)(TRAPFRAME, satp, stvec);
}
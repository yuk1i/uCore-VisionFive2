#include "proc.h"
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "vm.h"
#include "queue.h"

static struct proc *pool[NPROC];
// __attribute__((aligned(16))) char kstack[NPROC][PAGE_SIZE];
// __attribute__((aligned(4096))) char trapframe[NPROC][TRAP_PAGE_SIZE];

extern char boot_stack_top[];
struct proc *current_proc;
struct proc idle;
struct queue task_queue;

int threadid()
{
	if (!current_proc)
		return -1;
	return curr_proc()->pid;
}

struct proc *curr_proc()
{
	return current_proc;
}

static allocator_t proc_allocator;

// initialize the proc table at boot time.
void proc_init()
{
	allocator_init(&proc_allocator, "proc", sizeof(struct proc), NPROC);
	struct proc *p;

	for (int i = 0; i < NPROC; i++) {
		p = kalloc(&proc_allocator);
		memset(p, 0, sizeof(*p));
		p->index = i;
		p->state = UNUSED;
		p->kstack = PA_TO_KVA(kallocpage());
		p->trapframe = (struct trapframe *)PA_TO_KVA(kallocpage());
		pool[i] = p;
	}
	idle.kstack = (uint64)boot_stack_top;
	idle.pid = IDLE_PID;
	current_proc = &idle;
	init_queue(&task_queue);
}

int allocpid()
{
	static int PID = 1;
	return PID++;
}

struct proc *fetch_task()
{
	int index = pop_queue(&task_queue);
	if (index < 0) {
		debugf("No task to fetch\n");
		return NULL;
	}
	debugf("fetch task %d(pid=%d) to task queue\n", index, pool[index]->pid);
	return pool[index];
}

void add_task(struct proc *p)
{
	push_queue(&task_queue, p->index);
	debugf("add task %d(pid=%d) to task queue\n", p->index, p->pid);
}

extern char trampoline[];

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc *allocproc()
{
	struct proc *p;
	for (int i = 0; i < NPROC; i++) {
		p = pool[i];
		if (p->state == UNUSED) {
			goto found;
		}
	}
	return 0;

found:
	// init proc
	tracef("init proc %p", p);
	p->pid = allocpid();
	p->state = USED;
	p->mm = mm_create();
	if (!p->mm)
		panic("mm");
	p->vma_ustack = NULL;
	p->vma_brk = NULL;
	// only allocate trampoline and trapframe here.
	p->vma_trampoline = mm_mappagesat(p->mm, TRAMPOLINE, KIVA_TO_PA(trampoline), PTE_A | PTE_R | PTE_X, false);
	uint64 __pa tf = (uint64)kallocpage();
	if (!tf)
		panic("tf");
	p->vma_trapframe = mm_mappagesat(p->mm, TRAPFRAME, tf, PTE_A | PTE_D | PTE_R | PTE_W | PTE_X, false);
	p->trapframe = (struct trapframe *)PA_TO_KVA(tf);
	p->parent = NULL;
	p->exit_code = 0;
	memset(&p->context, 0, sizeof(p->context));
	memset((void *)p->kstack, 0, KSTACK_SIZE);
	memset((void *)p->trapframe, 0, TRAP_PAGE_SIZE);
	p->context.ra = (uint64)usertrapret;
	p->context.sp = p->kstack + KSTACK_SIZE;
	return p;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler()
{
	struct proc *p;
	for (;;) {
		/*int has_proc = 0;
		for (p = pool; p < &pool[NPROC]; p++) {
			if (p->state == RUNNABLE) {
				has_proc = 1;
				tracef("swtich to proc %d", p - pool);
				p->state = RUNNING;
				current_proc = p;
				swtch(&idle.context, &p->context);
			}
		}
		if(has_proc == 0) {
			panic("all app are over!\n");
		}*/
		p = fetch_task();
		if (p == NULL) {
			panic("all app are over!\n");
		}
		infof("swtich to proc %d(%d)", p->index, p->pid);
		p->state = RUNNING;
		current_proc = p;
		swtch(&idle.context, &p->context);
	}
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched()
{
	struct proc *p = curr_proc();
	if (p->state == RUNNING)
		panic("sched running");
	swtch(&p->context, &idle.context);
}

// Give up the CPU for one scheduling round.
void yield()
{
	current_proc->state = RUNNABLE;
	add_task(current_proc);
	sched();
}

void freeproc(struct proc *p)
{
	freevma(p->vma_trampoline, false);
	freevma(p->vma_trapframe, true);
	p->vma_trampoline = NULL;
	p->vma_trapframe = NULL;
	if (p->mm)
		mm_free(p->mm);
	p->vma_brk = NULL;
	p->vma_ustack = NULL;
	p->state = UNUSED;
}

int fork()
{
	struct proc *np;
	struct proc *p = curr_proc();
	// Allocate process.
	if ((np = allocproc()) == NULL) {
		panic("allocproc\n");
	}
	if (mm_copy(p->mm, np->mm)) {
		panic("mm_copy");
	}
	// Copy user memory from parent to child.
	// if (uvmcopy(p->pagetable, np->pagetable, PGROUNDUP(p->program_brk) / PGSIZE) < 0) {
	// 	panic("uvmcopy\n");
	// }
	// np->program_brk = p->program_brk;
	// copy saved user registers.
	*(np->trapframe) = *(p->trapframe);
	// Cause fork to return 0 in the child.
	np->trapframe->a0 = 0;
	np->parent = p;
	np->state = RUNNABLE;
	add_task(np);
	return np->pid;
}

int exec(char *name)
{
	struct user_app *app = get_elf(name);
	if (app == NULL)
		return -1;
	struct proc *p = curr_proc();
	// execve does not preserve memory mappings:
	//  free memory below program_brk, and ustack
	//  but keep trapframe and trampoline, because it belongs to curr_proc().
	mm_free_pages(p->mm);

	load_user_elf(app, p);
	return 0;
}

int wait(int pid, int *code)
{
	struct proc *np;
	int havekids;
	struct proc *p = curr_proc();

	for (;;) {
		// Scan through table looking for exited children.
		havekids = 0;
		for (int i = 0; i < NPROC; i++) {
			np = pool[i];
			if (np->state != UNUSED && np->parent == p && (pid <= 0 || np->pid == pid)) {
				havekids = 1;
				if (np->state == ZOMBIE) {
					// Found one.
					np->state = UNUSED;
					pid = np->pid;
					*code = np->exit_code;
					return pid;
				}
			}
		}
		if (!havekids) {
			return -1;
		}
		p->state = RUNNABLE;
		add_task(p);
		sched();
	}
}

// Exit the current process.
void exit(int code)
{
	struct proc *p = curr_proc();
	p->exit_code = code;
	debugf("proc %d exit with %d\n", p->pid, code);
	freeproc(p);
	if (p->parent != NULL) {
		// Parent should `wait`
		p->state = ZOMBIE;
	}
	// Set the `parent` of all children to NULL
	struct proc *np;
	for (int i = 0; i < NPROC; i++) {
		np = pool[i];
		if (np->parent == p) {
			np->parent = NULL;
		}
	}
	sched();
}

// Grow or shrink user memory by n bytes.
// Return 0 on succness, -1 on failure.
int growproc(int n)
{
	uint64 program_brk;
	panic("qwq");
	// struct proc *p = curr_proc();
	// program_brk = p->program_brk;
	// int64 new_brk = program_brk + n;
	// if (new_brk < 0) {
	// 	return -1;
	// }
	// if (n > 0) {
	// 	if ((program_brk = uvmalloc(p->pagetable, program_brk, program_brk + n, PTE_W)) == 0) {
	// 		return -1;
	// 	}
	// } else if (n < 0) {
	// 	program_brk = uvmdealloc(p->pagetable, program_brk, program_brk + n);
	// }
	// p->program_brk = program_brk;
	return 0;
}

#include "proc.h"

#include "defs.h"
#include "queue.h"
#include "trap.h"
#include "kalloc.h"
#include "loader.h"

static struct proc *pool[NPROC];
static struct proc *init_proc;
static allocator_t proc_allocator;
static struct queue task_queue;

static int allocated_pid = 1;
static spinlock_t lock_pid;

static spinlock_t wait_lock;

struct proc *curr_proc() {
    // push_off();
    struct cpu *cpu   = mycpu();
    struct proc *proc = cpu->proc;
    // pop_off();
    return proc;
}

int threadid() {
    if (!curr_proc())
        return -1;
    return curr_proc()->pid;
}

static uint64 proc_kstack = KERNEL_STACK_PROCS;
extern pagetable_t kernel_pagetable;

// initialize the proc table at boot time.
void proc_init() {
    // we only init once.
    static int proc_inited = 0;
    assert(proc_inited == 0);
    proc_inited = 1;

    spinlock_init(&lock_pid, "pid");
    spinlock_init(&wait_lock, "wait");

    allocator_init(&proc_allocator, "proc", sizeof(struct proc), NPROC);
    struct proc *p;

    for (int i = 0; i < NPROC; i++) {
        p = kalloc(&proc_allocator);
        memset(p, 0, sizeof(*p));
        spinlock_init(&p->lock, "proc");
        p->index = i;
        p->state = UNUSED;

        p->kstack = proc_kstack;
        for (uint64 va = proc_kstack; va < proc_kstack + KERNEL_STACK_SIZE; va += PGSIZE) {
            uint64 __pa newpg = (uint64)kallocpage();
            kvmmap(kernel_pagetable, va, newpg, PGSIZE, PTE_A | PTE_D | PTE_R | PTE_W);
        }
        sfence_vma();
        proc_kstack += 2 * KERNEL_STACK_SIZE;

        p->trapframe = (struct trapframe *)PA_TO_KVA(kallocpage());
        pool[i]      = p;
    }
    init_queue(&task_queue);

    init_proc = pool[0];
}

int allocpid() {
    static int PID = 1;
    return PID++;
}

struct proc *fetch_task() {
    struct proc *proc = pop_queue(&task_queue);
    if (proc != NULL)
        debugf("fetch task (pid=%d) from task queue", proc->pid);
    return proc;
}

void add_task(struct proc *p) {
    push_queue(&task_queue, p);
    debugf("add task (pid=%d) to task queue", p->pid);
}

static void first_sched_ret(void);

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc *allocproc() {
    struct proc *p;
    for (int i = 0; i < NPROC; i++) {
        p = pool[i];
        acquire(&p->lock);
        if (p->state == UNUSED) {
            goto found;
        }
        release(&p->lock);
    }
    return 0;

found:
    // initialize a proc
    tracef("init proc %p", p);
    p->pid   = allocpid();
    p->state = USED;
    p->mm    = mm_create();
    if (!p->mm)
        panic("mm");
    p->vma_ustack = NULL;
    p->vma_brk    = NULL;
    // only allocate trampoline and trapframe here.
    p->vma_trampoline = mm_mappagesat(p->mm, TRAMPOLINE, KIVA_TO_PA(trampoline), PTE_A | PTE_R | PTE_X, false);
    uint64 __pa tf    = (uint64)kallocpage();
    if (!tf)
        panic("tf");
    p->vma_trapframe = mm_mappagesat(p->mm, TRAPFRAME, tf, PTE_A | PTE_D | PTE_R | PTE_W | PTE_X, false);
    p->trapframe     = (struct trapframe *)PA_TO_KVA(tf);
    p->parent        = NULL;
    p->exit_code     = 0;
    memset(&p->context, 0, sizeof(p->context));
    memset((void *)p->kstack, 0, KERNEL_STACK_SIZE);
    memset((void *)p->trapframe, 0, PGSIZE);
    p->context.ra = (uint64)first_sched_ret;
    p->context.sp = p->kstack + KERNEL_STACK_SIZE;

    assert(holding(&p->lock));
    return p;
}

static void first_sched_ret(void) {
    release(&curr_proc()->lock);
    intr_off();
    usertrapret();
}

static int all_dead() {
    push_off();
    int alive = 0;
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = pool[i];
        acquire(&p->lock);
        if (p->state != UNUSED)
            alive = true;
        release(&p->lock);
        if (alive)
            break;
    }
    pop_off();
    return !alive;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler() {
    struct proc *p;
    struct cpu *c = mycpu();

    // We only get here once.
    // After each cpu boots, it calls scheduler().
    // If this scheduler finds any possible process to run, it will switch to it.
    // 	And the scheduler context is saved on "mycpu()->sched_context"

    for (;;) {
        // intr may be on here.

        p = fetch_task();
        if (p == NULL) {
            // if we cannot find a process in the task_queue
            //  maybe some processes are SLEEPING and some are RUNNABLE
            if (all_dead()) {
                panic("[cpu %d] scheduler dead.", c->cpuid);
            } else {
                // nothing to run; stop running on this core until an interrupt.
                intr_on();
                asm volatile("wfi");
                intr_off();
                continue;
            }
        }

        acquire(&p->lock);
        assert(p->state == RUNNABLE);
        infof("switch to proc %d(%d)", p->index, p->pid);
        p->state = RUNNING;
        c->proc  = p;
        swtch(&c->sched_context, &p->context);

        // When we get back here, someone must have called swtch(..., &c->sched_context);
        assert(c->proc == p);
        assert(!intr_get());        // scheduler should never have intr_on()
        assert(holding(&p->lock));  // whoever switch to us must acquire p->lock
        c->proc = NULL;

        if (p->state == RUNNABLE) {
            add_task(p);
        }
        release(&p->lock);
    }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched() {
    int interrupt_on;
    struct proc *p = curr_proc();

    if (!holding(&p->lock))
        panic("not holding p->lock");
    if (mycpu()->noff != 1)
        panic("holding another locks");
    if (p->state == RUNNING)
        panic("sched running process");
    if (mycpu()->inkernel_trap)
        panic("sched should never be called in kernel trap context.");
    assert(!intr_get());

    interrupt_on = mycpu()->interrupt_on;
    debugf("switch to scheduler %d(%d)", p->index, p->pid);
    swtch(&p->context, &mycpu()->sched_context);
    mycpu()->interrupt_on = interrupt_on;

    // if scheduler returns here: p->lock must be holding.
    if (!holding(&p->lock))
        panic("not holding p->lock after sched.swtch returns");
}

// Give up the CPU for one scheduling round.
void yield() {
    struct proc *p = curr_proc();
    debugf("yield: (%d)%p", p->pid, p);

    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

void freeproc(struct proc *p) {
    assert(holding(&p->lock));

    p->state      = UNUSED;
    p->pid        = -1;
    p->exit_code  = 0xdeadbeef;
    p->sleep_chan = NULL;
    p->killed     = 0;
    p->parent     = NULL;

    freevma(p->vma_trampoline, false);
    freevma(p->vma_trapframe, true);
    p->vma_trampoline = NULL;
    p->vma_trapframe  = NULL;
    mm_free(p->mm);
    p->vma_brk    = NULL;
    p->vma_ustack = NULL;
}

void sleep(void *chan, spinlock_t *lk) {
    struct proc *p = curr_proc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock);  // DOC: sleeplock1
    release(lk);

    // Go to sleep.
    p->sleep_chan = chan;
    p->state      = SLEEPING;

    sched();

    // p get waking up, Tidy up.
    p->sleep_chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}

void wakeup(void *chan) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = pool[i];
        acquire(&p->lock);
        if (p->state == SLEEPING && p->sleep_chan == chan) {
            p->state = RUNNABLE;
            add_task(p);
        }
        release(&p->lock);
    }
}

int fork() {
    struct proc *np;
    // Allocate process.
    if ((np = allocproc()) == NULL) {
        panic("allocproc");
    }

    struct proc *p = curr_proc();
    acquire(&p->lock);

    // Copy user memory from parent to child.
    if (mm_copy(p->mm, np->mm))
        panic("mm_copy");

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;
    np->parent        = p;
    np->state         = RUNNABLE;
    add_task(np);
    release(&np->lock);
    release(&p->lock);

    return np->pid;
}

int exec(char *name) {
    struct user_app *app = get_elf(name);
    if (app == NULL)
        return -1;
    struct proc *p = curr_proc();

    acquire(&p->lock);

    // execve does not preserve memory mappings:
    //  free memory below program_brk, and ustack
    //  but keep trapframe and trampoline, because it belongs to curr_proc().
    mm_free_pages(p->mm);

    load_user_elf(app, p);

    release(&p->lock);
    return 0;
}

int wait(int pid, int *code) {
    struct proc *child;
    int havekids;
    struct proc *p = curr_proc();

    acquire(&wait_lock);

    for (;;) {
        // Scan through table looking for exited children.
        havekids = 0;
        for (int i = 0; i < NPROC; i++) {
            child = pool[i];
            if (child == p)
                continue;

            acquire(&child->lock);
            if (child->parent == p) {
                havekids = 1;
                if (child->state == ZOMBIE && (pid <= 0 || child->pid == pid)) {
                    int cpid = child->pid;
                    // Found one.
                    if (code)
                        *code = child->exit_code;
                    freeproc(child);
                    release(&child->lock);
                    release(&wait_lock);
                    return cpid;
                }
            }
            release(&child->lock);
        }

        // No waiting if we don't have any children.
        if (!havekids || p->killed) {
            release(&wait_lock);
            return -1;
        }

        infof("pid %d sleeps for wait", p->pid);
        // Wait for a child to exit.
        sleep(p, &wait_lock);  // DOC: wait-sleep
    }
}

// Exit the current process.
void exit(int code) {
    struct proc *p = curr_proc();

    acquire(&wait_lock);

    // wakeup wait-ing parent.
    //  There is no race because locking against "wait_lock"
    wakeup(p->parent);

    acquire(&p->lock);

    // reparent
    for (int i = 0; i < NPROC; i++) {
        struct proc *np = pool[i];
        if (np == p)
            continue;
        acquire(&np->lock);
        if (np->parent == p)
            np->parent = init_proc;
        release(&np->lock);
    }

    p->exit_code = code;
    p->state     = ZOMBIE;

    release(&wait_lock);

    sched();
    panic("exit should never return");
}

// Grow or shrink user memory by n bytes.
// Return 0 on succness, -1 on failure.
int growproc(int n) {
    uint64 program_brk;
    panic("qwq");
    return 0;
}

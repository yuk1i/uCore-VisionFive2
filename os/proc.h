#ifndef PROC_H
#define PROC_H

#include "riscv.h"
#include "types.h"
#include "queue.h"
#include "vm.h"

#define NPROC (512)
#define FD_BUFFER_SIZE (16)

struct file;

// Saved registers for kernel context switches.
struct context {
	uint64 ra;
	uint64 sp;

	// callee-saved
	uint64 s0;
	uint64 s1;
	uint64 s2;
	uint64 s3;
	uint64 s4;
	uint64 s5;
	uint64 s6;
	uint64 s7;
	uint64 s8;
	uint64 s9;
	uint64 s10;
	uint64 s11;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
	int index;

	enum procstate state; // Process state
	int pid; // Process ID
	struct mm* mm;
	struct vma* vma_ustack;
	struct vma* vma_brk;
	struct vma* vma_trapframe;
	struct vma* vma_trampoline;

	struct trapframe * __kva trapframe; // data page for trampoline.S
	uint64 __kva kstack; // Virtual address of kernel stack
	struct context context; // swtch() here to run process
	struct proc *parent; // Parent process
	uint64 exit_code;
};

int cpuid();
struct proc *curr_proc();
void exit(int);
void proc_init();
void scheduler() __attribute__((noreturn));
void sched();
void yield();
int fork();
int exec(char *);
int wait(int, int *);
void add_task(struct proc *);
struct proc *pop_task();
struct proc *allocproc();
int fdalloc(struct file *);
// swtch.S
void swtch(struct context *, struct context *);

int growproc(int n);

#endif // PROC_H

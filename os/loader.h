#ifndef LOADER_H
#define LOADER_H

#include "const.h"
#include "proc.h"
#include "types.h"

void loader_init();
int load_init_app();
struct user_app *get_elf(char *name);
int load_user_elf(struct user_app *, struct proc *);

#define USTACK_START 0xff8000
#define USTACK_SIZE (PAGE_SIZE * 8)
#define KSTACK_SIZE (PAGE_SIZE)
#define TRAP_PAGE_SIZE (PAGE_SIZE)

struct user_app
{
    char *name;
    uint64 elf_address;
    uint64 elf_length;
};

extern struct user_app user_apps[];

#endif // LOADER_H


/*

int bin_loader(uint64 start, uint64 end, struct proc *p)
{
	if (p == NULL || p->state == UNUSED)
		panic("...");
	void *page;
	uint64 pa_start = PGROUNDDOWN(start);
	uint64 pa_end = PGROUNDUP(end);
	uint64 length = pa_end - pa_start;
	uint64 va_start = BASE_ADDRESS;
	uint64 va_end = BASE_ADDRESS + length;
	for (uint64 va = va_start, pa = pa_start; pa < pa_end;
	     va += PGSIZE, pa += PGSIZE) {
		page = kalloc();
		if (page == 0) {
			panic("...");
		}
		memmove(page, (const void *)pa, PGSIZE);
		if (pa < start) {
			memset(page, 0, start - va);
		} else if (pa + PAGE_SIZE > end) {
			memset(page + (end - pa), 0, PAGE_SIZE - (end - pa));
		}
		if (mappages(p->pagetable, va, PGSIZE, (uint64)page,
			     PTE_U | PTE_R | PTE_W | PTE_X) != 0)
			panic("...");
	}
	// map ustack
	p->ustack = va_end + PAGE_SIZE;
	for (uint64 va = p->ustack; va < p->ustack + USTACK_SIZE;
	     va += PGSIZE) {
		page = kalloc();
		if (page == 0) {
			panic("...");
		}
		memset(page, 0, PGSIZE);
		if (mappages(p->pagetable, va, PGSIZE, (uint64)page,
			     PTE_U | PTE_R | PTE_W) != 0)
			panic("...");
	}
	p->trapframe->sp = p->ustack + USTACK_SIZE;
	p->trapframe->epc = va_start;
	p->max_page = PGROUNDUP(p->ustack + USTACK_SIZE - 1) / PAGE_SIZE;
	p->program_brk = p->ustack + USTACK_SIZE;
        p->heap_bottom = p->ustack + USTACK_SIZE;
	p->state = RUNNABLE;
	return 0;
}

int loader(int app_id, struct proc *p)
{
	return bin_loader(app_info_ptr[app_id], app_info_ptr[app_id + 1], p);
}

*/
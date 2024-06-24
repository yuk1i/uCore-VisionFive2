#include "loader.h"
#include "defs.h"
#include "trap.h"
#include "elf.h"

// Get user progs' infomation through pre-defined symbol in `link_app.S`
void loader_init()
{
	printf("applist:\n");
	for (struct user_app *app = user_apps; app->name != NULL; app++) {
		printf("\t%s\n", app->name);
		Elf64_Ehdr *ehdr = (Elf64_Ehdr *)app->elf_address;
		assert_str(ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E' &&
				   ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F',
			   "invalid elf header: %s", app->name);
		assert_equals(ehdr->e_phentsize, sizeof(Elf64_Phdr), "invalid program header size");
	}
}

struct user_app *get_elf(char *name)
{
	for (struct user_app *app = user_apps; app->name != NULL; app++) {
		if (strncmp(name, app->name, strlen(name)) == 0)
			return app;
	}
	return NULL;
}

int load_user_elf(struct user_app *app, struct proc *p)
{
	if (p == NULL || p->state == UNUSED)
		panic("...");
	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)app->elf_address;
	Elf64_Phdr *phdr_base = (Elf64_Phdr *)(app->elf_address + ehdr->e_phoff);
	uint64 max_va_end = 0;
	for (int i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr *phdr = &phdr_base[i];
		// we only load from PT_LOAD phdrs
		if (phdr->p_type != PT_LOAD)
			continue;
		// resolve the permission of PTE for this phdr
		int pte_perm = PTE_U;
		if (phdr->p_flags & PF_R)
			pte_perm |= PTE_R;
		if (phdr->p_flags & PF_W)
			pte_perm |= PTE_W;
		if (phdr->p_flags & PF_X)
			pte_perm |= PTE_X;

		uint64 va = phdr->p_vaddr; // The ELF requests this phdr loaded to p_vaddr
		uint64 va_end = PGROUNDUP(va + phdr->p_memsz);
		uint64 file_off = 0;
		uint64 file_remains = phdr->p_filesz;
		while (va < va_end) {
			// allocate a physical page, copy data from elf file.
			void *page = kalloc();
			if (!page)
				panic("kalloc");
			uint64 copy_size = MIN(file_remains, PGSIZE);
			if (copy_size > 0) // p_memsz may be larger than p_filesz
				memmove(page, (void*)(app->elf_address + phdr->p_offset + file_off), copy_size);
			if (mappages(p->pagetable, va, PGSIZE, (uint64)page, pte_perm) != 0)
				panic("mappages");
			file_off += copy_size;
			file_remains -= copy_size;
			va += PGSIZE;
		}
		assert(file_remains == 0);
		max_va_end = MAX(max_va_end, va_end);
	}

	p->ustack = USTACK_START;
	for (uint64 va = p->ustack; va < p->ustack + USTACK_SIZE; va += PGSIZE) {
		void *page = kalloc();
		if (!page)
			panic("kalloc");
		if (mappages(p->pagetable, va, PGSIZE, (uint64)page, PTE_U | PTE_R | PTE_W) != 0)
			panic("mappages");
	}
	// setup trapframe
	p->trapframe->sp = p->ustack + USTACK_SIZE;
	p->trapframe->epc = ehdr->e_entry;
	p->program_brk = MAX(PGROUNDUP(p->ustack + USTACK_SIZE), PGROUNDUP(max_va_end + PGSIZE));
	p->state = RUNNABLE;
	return 0;
}

#ifndef INIT_PROC
#warning INIT_PROC is not defined, use "usershell"
#define INIT_PROC "usershell"
#endif

// load all apps and init the corresponding `proc` structure.
int load_init_app()
{
	struct user_app *app = get_elf(INIT_PROC);
	if (app == NULL) {
		panic("fail to lookup init elf %s", INIT_PROC);
	}

	struct proc *p = allocproc();
	if (p == NULL) {
		panic("allocproc\n");
	}
	debugf("load init proc %s", INIT_PROC);

	if (load_user_elf(app, p) < 0) {
		panic("fail to load init elf.");
	}
	add_task(p);
	return 0;
}

#include "loader.h"

#include "defs.h"
#include "elf.h"
#include "proc.h"
#include "trap.h"

// Get user progs' infomation through pre-defined symbol in `link_app.S`
void loader_init() {
    printf("applist:\n");
    for (struct user_app *app = user_apps; app->name != NULL; app++) {
        printf("\t%s\n", app->name);
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)app->elf_address;
        assert_str(ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E' && ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F', "invalid elf header: %s", app->name);
        assert_str(ehdr->e_phentsize == sizeof(Elf64_Phdr), "invalid program header size");
    }
}

struct user_app *get_elf(char *name) {
    for (struct user_app *app = user_apps; app->name != NULL; app++) {
        if (strncmp(name, app->name, strlen(name)) == 0)
            return app;
    }
    return NULL;
}

int load_user_elf(struct user_app *app, struct proc *p) {
    if (p == NULL || p->state == UNUSED)
        panic("...");
    Elf64_Ehdr *ehdr      = (Elf64_Ehdr *)app->elf_address;
    Elf64_Phdr *phdr_base = (Elf64_Phdr *)(app->elf_address + ehdr->e_phoff);
    uint64 max_va_end     = 0;
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

        struct vma *vma = mm_create_vma(p->mm);
        vma->vm_start   = PGROUNDDOWN(phdr->p_vaddr);  // The ELF requests this phdr loaded to p_vaddr;
        vma->vm_end     = PGROUNDUP(vma->vm_start + phdr->p_memsz);
        vma->pte_flags  = pte_perm;

        if (mm_mappages(vma)) {
            panic("mm_mappages");
        }

        int64 file_off      = 0;
        uint64 file_remains = phdr->p_filesz;
        uint64 va_off       = phdr->p_vaddr - vma->vm_start;

        for (uint64 va = vma->vm_start; va < vma->vm_end; va += PGSIZE) {
            void *__kva pa = (void *)PA_TO_KVA(walkaddr(p->mm, va));
            void *src      = (void *)(app->elf_address + phdr->p_offset + file_off);
            uint64 copy_size;

            if (va_off != 0) {
                assert(va_off < PGSIZE);
                // first clean the [vm_start, va_off)
                memset(pa, 0, va_off);
                copy_size = MIN(file_remains, PGSIZE - va_off);
                memmove(pa + va_off, src, copy_size);
                if (copy_size < PGSIZE)
                    memset(pa + va_off + copy_size, 0, PGSIZE - va_off - copy_size);
                va_off = 0;
            } else {
                copy_size = MIN(file_remains, PGSIZE);
                memmove(pa + va_off, src, copy_size);
                if (copy_size < PGSIZE) {
                    // clear remaining bytes
                    memset(pa + copy_size, 0, PGSIZE - copy_size);
                }
            }

            file_off += copy_size;
            file_remains -= copy_size;
        }

        if (phdr->p_memsz > phdr->p_filesz) {
            // ELF requests larger memory than filesz
            // 	these are .bss segment, clear it to zero.

            uint64 bss_start = phdr->p_vaddr + phdr->p_filesz;
            uint64 bss_end   = phdr->p_vaddr + phdr->p_memsz;

            for (uint64 va = bss_start; va < bss_end; va = PGROUNDDOWN(va) + PGSIZE) {
                // set [va, page boundary of ba) to zero
                uint64 page_off   = va - PGROUNDDOWN(va);
                uint64 clear_size = PGROUNDUP(va) - va;
                void *__kva pa    = (void *)PA_TO_KVA(walkaddr(p->mm, PGROUNDDOWN(va)));
                memset(pa + page_off, 0, clear_size);
            }
        }

        assert(file_remains == 0);
        max_va_end = MAX(max_va_end, PGROUNDUP(phdr->p_vaddr + phdr->p_memsz));
    }

    p->vma_brk            = mm_create_vma(p->mm);
    p->vma_brk->vm_start  = max_va_end;
    p->vma_brk->vm_end    = p->vma_brk->vm_start;
    p->vma_brk->pte_flags = PTE_R | PTE_W | PTE_U;
    mm_mappages(p->vma_brk);

    p->vma_ustack            = mm_create_vma(p->mm);
    p->vma_ustack->vm_start  = USTACK_START - USTACK_SIZE;
    p->vma_ustack->vm_end    = USTACK_START;
    p->vma_ustack->pte_flags = PTE_R | PTE_W | PTE_U;
    mm_mappages(p->vma_ustack);

    // vm_print(p->mm->pgt);

    for (uint64 va = p->vma_ustack->vm_start; va < p->vma_ustack->vm_end; va += PGSIZE) {
        void *__kva pa = (void *)PA_TO_KVA(walkaddr(p->mm, va));
        memset(pa, 0, PGSIZE);
    }

    // setup trapframe
    p->trapframe->sp  = p->vma_ustack->vm_end;
    p->trapframe->epc = ehdr->e_entry;
    p->state          = RUNNABLE;

    // vm_print(p->mm->pgt);
    return 0;
}

#ifndef INIT_PROC
#warning INIT_PROC is not defined, use "usershell"
#define INIT_PROC "usershell"
#endif

// load all apps and init the corresponding `proc` structure.
int load_init_app() {
    struct user_app *app = get_elf(INIT_PROC);
    if (app == NULL) {
        panic("fail to lookup init elf %s", INIT_PROC);
    }

    struct proc *p = allocproc();
    if (p == NULL) {
        panic("allocproc\n");
    }
    infof("load init proc %s", INIT_PROC);

    if (load_user_elf(app, p) < 0) {
        panic("fail to load init elf.");
    }
    add_task(p);
    release(&p->lock);
    return 0;
}

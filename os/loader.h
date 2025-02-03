#ifndef LOADER_H
#define LOADER_H

#include "const.h"
#include "proc.h"
#include "types.h"

void loader_init();
int load_init_app();
struct user_app *get_elf(char *name);
int load_user_elf(struct user_app *, struct proc *);

#define USTACK_START 0xffff0000
#define USTACK_SIZE (PAGE_SIZE * 8)
#define TRAP_PAGE_SIZE (PAGE_SIZE)

struct user_app
{
    char *name;
    uint64 elf_address;
    uint64 elf_length;
};

extern struct user_app user_apps[];

#endif // LOADER_H
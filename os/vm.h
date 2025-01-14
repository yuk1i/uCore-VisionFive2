#ifndef VM_H
#define VM_H

#include "riscv.h"
#include "types.h"

void kvm_init();
void kvmmap(pagetable_t, uint64, uint64, uint64, int);
int mappages(pagetable_t, uint64, uint64, uint64, int);
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
pagetable_t uvmcreate(uint64);
int uvmcopy(pagetable_t, pagetable_t, uint64);
void uvmfree(pagetable_t, uint64);
void uvmunmap(pagetable_t, uint64, uint64, int);
uint64 walkaddr(pagetable_t, uint64);
uint64 useraddr(pagetable_t, uint64);
int copyout(pagetable_t, uint64, char *, uint64);
int copyin(pagetable_t, char *, uint64, uint64);
int copyinstr(pagetable_t, char *, uint64, uint64);

uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);

void vm_print(pagetable_t pagetable);

#endif // VM_H

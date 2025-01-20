#include "vm.h"
#include "trap.h"


void print_trapframe(struct trapframe *tf);
void print_sysregs();
void vm_print(pagetable_t __kva pagetable);
void mm_print(struct mm* mm);

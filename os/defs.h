#ifndef DEFS_H
#define DEFS_H

// defs.h contains most common definitions and declarations.

// ask clang-format do not sort the includes
// clang-format off

#include "types.h"
#include "riscv.h"
#include "log.h"
#include "memlayout.h"
#include "string.h"
#include "vm.h"
#include "proc.h"
#include "lock.h"
#include "kalloc.h"

// clang-format on

// Kernel Defines
#define ENABLE_SMP     (1)
#define NCPU           (4)
#define NPROC          (512)
#define FD_BUFFER_SIZE (16)

#define MIN(a, b)      (a < b ? a : b)
#define MAX(a, b)      (a > b ? a : b)
#define MEMORY_FENCE() __sync_synchronize()

// kernel image symbols, defined in kernel.ld
extern char skernel[], ekernel[];
extern char s_rodata[], e_rodata[];
extern char s_text[], e_text[];
extern char s_data[], e_data[];
extern char s_bss[], e_bss[];

// entry.S
extern char _entry[];
extern char secondary_cpu_entry[];
extern char kernel_trap_entry[];
extern char boot_stack[], boot_stack_top[];

// trampoline.S
extern char trampoline[];
extern char uservec[];
extern char userret[];

#endif  // DEFS_H
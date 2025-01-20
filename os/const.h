#ifndef CONST_H
#define CONST_H

#include "types.h"

#define PAGE_SIZE (0x1000)

enum {
	STDIN = 0,
	STDOUT = 1,
	STDERR = 2,
};

// Kernel Memory Layout:

#define RISCV_DDR_BASE 0x80000000ull
#define PHYS_MEM_SIZE	(64ull * 1024 * 1024)
#define VALID_PHYS_ADDR(pa) (((pa) >= KERNEL_PHYS_BASE && (pa) <= RISCV_DDR_BASE + PHYS_MEM_SIZE))

#define KERNEL_VIRT_BASE 0xffffffff80200000ull
#define KERNEL_PHYS_BASE 0x80200000ull
#define KERNEL_OFFSET	 ((uint64)(KERNEL_VIRT_BASE - KERNEL_PHYS_BASE))
#define KERNEL_DIRECT_MAPPING_BASE 	0xffffffc000000000ull
#define KERNEL_ALLOCATOR_BASE 		0xfffffffd00000000ull
#define KERNEL_ALLOCATOR_GAP  		0x0000000001000000ull

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
// #define KERNBASE 0x80200000L
// #define PHYSTOP (0x80000000 + 128 * 1024 * 1024) // we have 128M memroy

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

// map the trampoline page to the highest address,
// in both user and kernel space.
#define USER_TOP (MAXVA)
#define TRAMPOLINE (USER_TOP - PGSIZE)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

// memory layout end

#define MAX_APP_NUM (32)
#define MAX_STR_LEN (200)
#define IDLE_PID (0)

#endif // CONST_H
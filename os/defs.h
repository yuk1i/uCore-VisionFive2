#ifndef DEFS_H
#define DEFS_H

#include "const.h"
#include "kalloc.h"
#include "log.h"
#include "printf.h"
#include "proc.h"
#include "riscv.h"
#include "sbi.h"
#include "string.h"
#include "types.h"
#include "vm.h"

void smp_init(uint64 boot_hartid);


// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

#define NULL ((void *)0)

#endif // DEF_H

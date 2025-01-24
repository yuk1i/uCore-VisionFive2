#include "defs.h"
#include "proc.h"
#include "const.h"

static struct cpu cpus[NCPU];

void smp_init(uint64 boot_haltid)
{
	memset(cpus, 0, sizeof(cpus));
	w_tp(boot_haltid);
	cpus[cpuid()].cpuid = cpuid();
	cpus[cpuid()].mhart_id = 0x114514;
}

struct cpu *mycpu()
{
	return &cpus[cpuid()];
}
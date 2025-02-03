#include "defs.h"
#include "proc.h"
#include "const.h"

static struct cpu cpus[NCPU];

void smp_init(uint64 boot_haltid)
{
	memset(cpus, 0, sizeof(cpus));
	cpus[cpuid()].cpuid = cpuid();
	cpus[cpuid()].mhart_id = boot_haltid;
	cpus[cpuid()].sched_kstack_top = 0;
}

struct cpu *mycpu()
{
	return &cpus[cpuid()];
}

struct cpu* getcpu(int i) {
	assert(i >= 0 && i<NCPU);
	return &cpus[i];
}
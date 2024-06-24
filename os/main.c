#include "console.h"
#include "defs.h"
#include "loader.h"
#include "timer.h"
#include "trap.h"

void clean_bss()
{
	extern char s_bss[];
	extern char e_bss[];
	memset(s_bss, 0, e_bss - s_bss);
}

void main()
{
	clean_bss();
	extern char e_text[]; // kernel.ld sets this to end of kernel code.
	printf("etext: %p\n", e_text);
	printf("Kernel is initializing...\n");
	proc_init();
	printf("proc_init done!\n");
	kinit();
	trap_init();
	printf("trap_init done!\n");
	kvm_init();
	printf("kvm_init done!\n");
	loader_init();
	printf("loader done!\n");
	timer_init();
	printf("timer_init done!\n");
	load_init_app();
	infof("start scheduler!");
	scheduler();
}
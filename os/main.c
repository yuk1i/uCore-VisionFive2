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
	printf("hello world!\n");
	proc_init();
	printf("hello world!\n");
	kinit();
	trap_init();
	printf("hello world!\n");
	kvm_init();
	printf("hello world!\n");
	loader_init();
	printf("hello world!\n");
	printf("hello world!\n");
	timer_init();
	printf("hello world!\n");
	load_init_app();
	infof("start scheduler!");
	scheduler();
}
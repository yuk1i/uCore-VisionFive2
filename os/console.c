#include "console.h"
#include "sbi.h"

void consputc(int c)
{
	console_putchar(c);
}

void console_init()
{
	// DO NOTHING
}

int consgetc()
{
	int ret;
	do {
		ret = console_getchar();
	} while (ret == -1);
	return ret;
}
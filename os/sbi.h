#ifndef SBI_H
#define SBI_H

#include "types.h"

void sbi_putchar(int);
void shutdown();
void set_timer(uint64 stime);

#endif // SBI_H

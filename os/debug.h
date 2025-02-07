#include "vm.h"
#include "trap.h"


void print_trapframe(struct trapframe *tf);
void print_ktrapframe(struct ktrapframe *tf);
void print_sysregs(int explain);

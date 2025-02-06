#include "syscall.h"

#include "console.h"
#include "defs.h"
#include "loader.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len) {
    debugf("sys_write fd = %d str = %p, len = %d", fd, va, len);
    if (fd != STDOUT)
        return -1;
    return user_console_write(va, len);
}

uint64 sys_read(int fd, uint64 va, uint64 len) {
    debugf("sys_read fd = %d str = %p, len = %d", fd, va, len);
    if (fd != STDIN)
        return -1;

    return user_console_read(va, len);
}

__noreturn void sys_exit(int code) {
    exit(code);
    __builtin_unreachable();
}

uint64 sys_sched_yield() {
    yield();
    return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz) {
    struct proc *p = curr_proc();
    uint64 cycle   = get_cycle();
    TimeVal t;
    t.sec  = cycle / CPU_FREQ;
    t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
    copy_to_user(p->mm, val, (char *)&t, sizeof(TimeVal));
    return 0;
}

uint64 sys_getpid() {
    return curr_proc()->pid;
}

uint64 sys_getppid() {
    struct proc *p = curr_proc();
    return p->parent == NULL ? 0 : p->parent->pid;
}

uint64 sys_clone() {
    debugf("fork!\n");
    return fork();
}

uint64 sys_exec(uint64 va) {
    struct proc *p = curr_proc();
    char name[200];
    copystr_from_user(p->mm, name, va, 200);
    debugf("sys_exec %s\n", name);
    return exec(name);
}

uint64 sys_wait(int pid, uint64 va) {
    struct proc *p = curr_proc();

    acquire(&p->lock);
    acquire(&p->mm->lock);
    uint64 pa = useraddr(p->mm, va);
    release(&p->mm->lock);
    release(&p->lock);

    int *code = (int *)PA_TO_KVA(pa);
    return wait(pid, code);
}

uint64 sys_spawn(uint64 va) {
    // TODO: your job is to complete the sys call
    return -1;
}

uint64 sys_set_priority(long long prio) {
    // TODO: your job is to complete the sys call
    return -1;
}

uint64 sys_sbrk(int n) {
    uint64 addr;
    struct proc *p = curr_proc();
    panic("sbrk");
    // addr = p->program_brk;
    // if (growproc(n) < 0)
    // return -1;
    return addr;
}

void syscall() {
    struct trapframe *trapframe = curr_proc()->trapframe;
    int id                      = trapframe->a7, ret;
    uint64 args[6]              = {trapframe->a0, trapframe->a1, trapframe->a2, trapframe->a3, trapframe->a4, trapframe->a5};
    tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0], args[1], args[2], args[3], args[4], args[5]);
    switch (id) {
        case SYS_write:
            ret = sys_write(args[0], args[1], args[2]);
            break;
        case SYS_read:
            ret = sys_read(args[0], args[1], args[2]);
            break;
        case SYS_exit:
            sys_exit(args[0]);
            // __builtin_unreachable();
        case SYS_sched_yield:
            ret = sys_sched_yield();
            break;
        case SYS_gettimeofday:
            ret = sys_gettimeofday(args[0], args[1]);
            break;
        case SYS_getpid:
            ret = sys_getpid();
            break;
        case SYS_getppid:
            ret = sys_getppid();
            break;
        case SYS_clone:  // SYS_fork
            ret = sys_clone();
            break;
        case SYS_execve:
            ret = sys_exec(args[0]);
            break;
        case SYS_wait4:
            ret = sys_wait(args[0], args[1]);
            break;
        case SYS_spawn:
            ret = sys_spawn(args[0]);
            break;
        case SYS_sbrk:
            ret = sys_sbrk(args[0]);
            break;
        default:
            ret = -1;
            errorf("unknown syscall %d", id);
    }
    trapframe->a0 = ret;
    tracef("syscall ret %d", ret);
}

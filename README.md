# uCore-Tutorial-Code

Course project for THU-OS.

对标 [rCore-Tutorial-v3](https://github.com/rcore-os/rCore-Tutorial-v3/) 的 C 版本代码。

主要参考 [xv6-riscv](https://github.com/mit-pdos/xv6-riscv), [uCore-SMP](https://github.com/TianhuaTao/uCore-SMP)。

实验 lab1-lab5 基准代码分别位于 ch3-ch8　分支下。

注：为了兼容清华 Git 的需求、避免同学在主分支写代码、明确主分支的功能性，特意单独建了仅包含 README 与 LICENSE 的 master 分支，完成课程实验时请在 clone 仓库后先 push master 分支到清华 Git，然后切到自己开发所需的分支进行后续操作。

## 本地开发测试

在本地开发并测试时，需要拉取 uCore-Tutorial-Test-2022A 到 `user` 文件夹。你可以根据网络情况和个人偏好选择下列一项执行：

```bash
# 清华 git 使用 https
git clone https://git.tsinghua.edu.cn/os-lab/public/ucore-tutorial-test-2022a.git user
# 清华 git 使用 ssh
git clone git@git.tsinghua.edu.cn:os-lab/public/ucore-tutorial-test-2022a.git user
# GitHub 使用 https
git clone https://github.com/LearningOS/uCore-Tutorial-Test-2022A.git user
# GitHub 使用 ssh
git clone git@github.com:LearningOS/uCore-Tutorial-Test-2022A.git user
```

注意：`user` 已添加至 `.gitignore`，你无需将其提交，ci 也不会使用它]


## uCore for VisionFive2

This repo contains my fixes to uCore-Tutorial-Code for running it on a VisionFive2 board.

See VisionFive2 Notes in codes.

### Checklist

- pagetable
- interrupt
- userspace
- SMP

### gdb & openocd

This configuration uses a jlink. 

- https://github.com/starfive-tech/edk2/wiki/How-to-flash-and-debug-with-JTAG#connect-to-visionfive-2
- https://dram.page/p/visionfive-jtag-1/

clone https://github.com/riscv-collab/riscv-openocd

configure with: ` ./configure  --enable-verbose --prefix /data/os-riscv/vf2-debug --enable-ftdi  --enable-stlink --enable-ft232r   --enable-ftdi-cjtag  --enable-jtag_vpi --enable-jtag_dpi --enable-openjtag --enable-jlink --enable-cmsis-dap --enable-nulink`

`make && make install`

openocd conf:

```
gdb_memory_map disable

# JTAG adapter setup
adapter driver jlink
# adapter driver cmsis-dap
# use cmsis-dap for muselab's nanoDAP debugger

adapter speed 20000
transport select jtag

set _CHIPNAME riscv
jtag newtap $_CHIPNAME cpu0 -irlen 5
jtag newtap $_CHIPNAME cpu1 -irlen 5
set _TARGETNAME_1 $_CHIPNAME.cpu1
set _TARGETNAME_2 $_CHIPNAME.cpu2
set _TARGETNAME_3 $_CHIPNAME.cpu3
set _TARGETNAME_4 $_CHIPNAME.cpu4

target create $_TARGETNAME_1 riscv -chain-position $_CHIPNAME.cpu1 -coreid 1 -rtos hwthread
#target create $_TARGETNAME_2 riscv -chain-position $_CHIPNAME.cpu1 -coreid 2
#target create $_TARGETNAME_3 riscv -chain-position $_CHIPNAME.cpu1 -coreid 3
#target create $_TARGETNAME_4 riscv -chain-position $_CHIPNAME.cpu1 -coreid 4

#target smp $_TARGETNAME_1 $_TARGETNAME_2 $_TARGETNAME_3 $_TARGETNAME_4

# we only debug one core.

init
```

gdbinit:

```
set confirm off
source /data/os-riscv/gef/gef.py
set architecture riscv:rv64
file build/kernel
gef config context.show_registers_raw 1
#target remote 127.0.0.1:3333
gef-remote --qemu-user --qemu-binary build/kernel 127.0.0.1 3333
set riscv use-compressed-breakpoints yes
b *0x80200000
```

### PageTable PTE_A and PTE_D

```c
vm.c:kvmmake
// 	if PTE_A is not set here, it will trigger an instruction page fault scause 0xc for the first time-accesses.
//		Then the trap-handler traps itself.
//		Because page fault handler should handle the PTE_A and PTE_D bits in VF2
//		QEMU works without PTE_A here.
//	see: https://www.reddit.com/r/RISCV/comments/14psii6/comment/jqmad6g
//	docs: Volume II: RISC-V Privileged Architectures V1.10, Page 61, 
//		> Two schemes to manage the A and D bits are permitted:
// 			- ..., the implementation sets the corresponding bit in the PTE.
//			- ..., a page-fault exception is raised.
//		> Standard supervisor software should be written to assume either or both PTE update schemes may be in effect.
```

为了使用 kerneltrap 处理 A/D bits 的 pagefault，需要对其做一定更改

```c
void kerneltrap() __attribute__((aligned(16)))
__attribute__((interrupt("supervisor")));	// gcc will generate codes for context saving and restoring.

void kerneltrap()
{
	if ((r_sstatus() & SSTATUS_SPP) == 0)
		panic("kerneltrap: not from supervisor mode");

	uint64 cause = r_scause();
	if (cause & (1ULL << 63)) {
		panic("kerneltrap enter with interrupt scause");
	}
	uint64 addr = r_stval();
	pagetable_t pgt = SATP_TO_PGTABLE(r_satp());
	pte_t *pte = walk(pgt, addr, 0);
	if (pte == NULL)
		panic("kernel pagefault at %p", addr);
	switch (cause) {
	case InstructionPageFault:
	case LoadPageFault:
		*pte |= PTE_A;
		break;
	case StorePageFault:
		*pte |= PTE_A | PTE_D;
		break;

	default:
		panic("trap from kernel");
	}
}
```

相关的ISA： svadu, https://github.com/riscvarchive/riscv-svadu, 已经被合并进 Privileged Specification.

VisionFive2 没有处理 PTE 中 A/D bits 的硬件，需要使用 pagefault 来处理。而 QEMU 则实现了 svadu ，可以直接设置 A/D bits.

为了保持 QEMU 和 VisionFive2 行为一致性方便调试，QEMU 中 svadu 可以通过 cpu flags 关掉：

```
QEMUOPTS = \
	-nographic \
	-machine virt \
	-cpu rv64,svadu=off \
	-kernel build/kernel	\
```

挂上 gdb 后通过 `info register menvcfg` 确认：

```
(qemu) gef➤  i r menvcfg 
menvcfg        0x80000000000000f0       0x80000000000000f0
```

注1：menvcfg 是 rv-privileged spec v1.12 的东西，最新的 spec 文档需要在 https://github.com/riscv/riscv-isa-manual/releases/ 中下载。
目前 "Privileged Specification version 20211203" 中并没有 menvcfg 的 ADUE 定义。

注2：QEMU 中相关的源代码：

target/riscv/cpu_helper.c: get_physical_address:

```c
    bool svade = riscv_cpu_cfg(env)->ext_svade;
    bool svadu = riscv_cpu_cfg(env)->ext_svadu;
    bool adue = svadu ? env->menvcfg & MENVCFG_ADUE : !svade;

    /*
     * If ADUE is enabled, set accessed and dirty bits.
     * Otherwise raise an exception if necessary.
     */
    if (adue) {
        updated_pte |= PTE_A | (access_type == MMU_DATA_STORE ? PTE_D : 0);
    } else if (!(pte & PTE_A) ||
               (access_type == MMU_DATA_STORE && !(pte & PTE_D))) {
        return TRANSLATE_FAIL;
    }
```

target/riscv/cpu_bits.h:

```c
/* Execution environment configuration bits */
#define MENVCFG_FIOM                       BIT(0)
#define MENVCFG_CBIE                       (3UL << 4)
#define MENVCFG_CBCFE                      BIT(6)
#define MENVCFG_CBZE                       BIT(7)
#define MENVCFG_ADUE                       (1ULL << 61)
#define MENVCFG_PBMTE                      (1ULL << 62)
#define MENVCFG_STCE                       (1ULL << 63)
```
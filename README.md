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
make && make install

openocd conf:

```
gdb_memory_map disable

# JTAG adapter setup
adapter speed 20000
adapter driver jlink
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

### PageTable PTE_A and PTE_W

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
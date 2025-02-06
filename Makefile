.PHONY: clean build user run debug test .FORCE
all: build

K = os

TOOLPREFIX = riscv64-unknown-elf-
CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump
PY = python3
GDB = $(TOOLPREFIX)gdb
CP = cp
BUILDDIR = build
C_SRCS := $(wildcard $K/*.c) $(wildcard $K/fs/*.c)
AS_SRCS := $(wildcard $K/*.S)

ifeq (,$(findstring $K/link_app.S,$(AS_SRCS)))
    AS_SRCS += $K/link_app.S
endif

C_OBJS  := $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(C_SRCS))))
AS_OBJS := $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(AS_SRCS))))
OBJS 	:= $(C_OBJS) $(AS_OBJS)

HEADER_DEP := $(addsuffix .d, $(basename $(C_OBJS)))

-include $(HEADER_DEP)

CFLAGS := -no-pie -Wall -Wno-unused-variable -Werror -O -fno-omit-frame-pointer -ggdb -march=rv64g
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I$K
CFLAGS += -std=gnu17
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

LDFLAGS := -static --no-relax -no-pie -z max-page-size=4096 -nostdlib

LOG ?= error

ifeq ($(LOG), error)
CFLAGS += -D LOG_LEVEL_ERROR
else ifeq ($(LOG), warn)
CFLAGS += -D LOG_LEVEL_WARN
else ifeq ($(LOG), info)
CFLAGS += -D LOG_LEVEL_INFO
else ifeq ($(LOG), debug)
CFLAGS += -D LOG_LEVEL_DEBUG
else ifeq ($(LOG), trace)
CFLAGS += -D LOG_LEVEL_TRACE
endif

INIT_PROC ?= usershell
CFLAGS += -DINIT_PROC=\"$(INIT_PROC)\"

# # Disable PIE when possible (for Ubuntu 16.10 toolchain)
# ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
# CFLAGS += -fno-pie -no-pie
# endif
# ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
# CFLAGS += -fno-pie -nopie
# endif

# empty target
.FORCE:

$(AS_OBJS): $(BUILDDIR)/$K/%.o : $K/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(C_OBJS): $(BUILDDIR)/$K/%.o : $K/%.c  $(BUILDDIR)/$K/%.d
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(HEADER_DEP): $(BUILDDIR)/$K/%.d : $K/%.c
	@mkdir -p $(@D)
	@set -e; rm -f $@; $(CC) -MM $< $(CFLAGS) -o $@.$$$$; \
        sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
        rm -f $@.$$$$

$K/link_app.S: scripts/pack.py .FORCE
	$(PY) scripts/pack.py

build: build/kernel

build/kernel: $(OBJS) os/kernel.ld
	$(LD) $(LDFLAGS) -T os/kernel.ld -o $(BUILDDIR)/kernel $(OBJS)
	$(OBJCOPY) -O binary $(BUILDDIR)/kernel $(BUILDDIR)/kernel.bin
	$(OBJDUMP) -S $(BUILDDIR)/kernel > $(BUILDDIR)/kernel.asm
	$(OBJDUMP) -t $(BUILDDIR)/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILDDIR)/kernel.sym
	@echo 'Build kernel done'
	cp $(BUILDDIR)/kernel /data/os-riscv/tftp-root/
	cp $(BUILDDIR)/kernel.bin /data/os-riscv/tftp-root/
	@echo 'Copy kernel to TFTP'
	ls -lha /data/os-riscv/tftp-root/

clean:
	rm -rf $(BUILDDIR) os/kernel_app.ld os/link_app.S

# BOARD
BOARD		?= qemu
SBI			?= rustsbi
BOOTLOADER	:= ./bootloader/rustsbi-qemu.bin

QEMU = qemu-system-riscv64
QEMUOPTS = \
	-nographic \
	-machine virt \
	-cpu rv64,svadu=off \
	-m 512M \
	-kernel build/kernel	\
	-drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

run: build/kernel
	$(QEMU) $(QEMUOPTS)

runsmp: build/kernel
	$(QEMU) -smp 4 $(QEMUOPTS)

# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::3333"; \
	else echo "-s -p 3333"; fi)

debug: build/kernel .gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)
	# sleep 1
	# $(GDB)

CHAPTER ?= $(shell git rev-parse --abbrev-ref HEAD | grep -oP 'ch\K[0-9]')

user:
	make -C user CHAPTER=$(CHAPTER) BASE=$(BASE)

test: user run


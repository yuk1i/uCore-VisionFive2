set confirm off
source /data/os-riscv/gef/gef.py
set architecture riscv:rv64
file build/kernel
gef config context.show_registers_raw 1
#target remote 127.0.0.1:3333
gef-remote --qemu-user --qemu-binary build/kernel 127.0.0.1 3333
set riscv use-compressed-breakpoints yes
b *0x80200000

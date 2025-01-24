#include "console.h"
#include "defs.h"
#include "loader.h"
#include "timer.h"
#include "trap.h"

extern char e_text[]; // kernel.ld sets this to end of kernel code.
extern char s_bss[];
extern char e_bss[];
extern char ekernel[], skernel[];

#define KIVA_TO_PA(x) (((uint64)(x)) - KERNEL_OFFSET)
#define PA_TO_KIVA(x) (((uint64)(x)) + KERNEL_OFFSET)

uint64 __pa kernel_image_end_4k;
uint64 __pa kernel_image_end_2M;

static char relocate_pagetable[PGSIZE] __attribute__((aligned(PGSIZE)));
static char relocate_pagetable_level1_ident[PGSIZE] __attribute__((aligned(PGSIZE)));
static char relocate_pagetable_level1_direct_mapping[PGSIZE] __attribute__((aligned(PGSIZE)));
static char relocate_pagetable_level1_high[PGSIZE] __attribute__((aligned(PGSIZE)));

static void relocation_start();

uint64 read_pc()
{
	uint64 pc;
	asm volatile("auipc %0, 0\n" // 将当前 PC 存入寄存器
		     : "=r"(pc) // 输出到变量 pc
		     : // 无输入
		     : // 无额外寄存器约束
	);
	return pc;
}

void main(int mhartid)
{
	printf("clean bss: %p - %p\n", s_bss, e_bss);
	memset(s_bss, 0, e_bss - s_bss);
	printf("Kernel is Relocating...\nWe are at %p now\n", read_pc());
	printf("Boot hart %d\n", mhartid);
	smp_init(mhartid);
	infof("basic smp inited, thread_id available now, we are cpu 0: %p", mycpu());
	relocation_start();
}

void main_relocated()
{
	printf("We are at high address now! PC: %p\n", read_pc());

	// Step 4. Rebuild final kernel pagetable
	// vm_print(SATP_TO_PGTABLE(r_satp()));
	kvm_init();
	vm_print((pagetable_t)PA_TO_KVA(SATP_TO_PGTABLE(r_satp())));

	memset(relocate_pagetable, 0xde, PGSIZE);
	memset(relocate_pagetable_level1_ident, 0xde, PGSIZE);
	memset(relocate_pagetable_level1_direct_mapping, 0xde, PGSIZE);
	memset(relocate_pagetable_level1_high, 0xde, PGSIZE);

	infof("Relocated.\n");
	infof("re-init smp");
	trap_init();
	console_init();
	printf("UART inited.\n");

	plicinit();
	plicinithart();
	
	kpgmgrinit();
	uvm_init();
	proc_init();
	loader_init();
	timer_init();
	load_init_app();
	infof("start scheduler!");
	scheduler();
}

static void relocation_start()
{
	assert(IS_ALIGNED(KERNEL_PHYS_BASE, PGSIZE_2M));
	// Although the kernel is compiled against VMA 0xffffffff80200000,
	//  we are still running under the Physical Address 0x80200000.

	// Step. 1: Setup a temporary pagetable.
	memset(relocate_pagetable, 0, PGSIZE);
	memset(relocate_pagetable_level1_ident, 0, PGSIZE);
	memset(relocate_pagetable_level1_direct_mapping, 0, PGSIZE);
	memset(relocate_pagetable_level1_high, 0, PGSIZE);

	pagetable_t pgt_root = (pagetable_t)relocate_pagetable;
	pagetable_t pgt_ident = (pagetable_t)relocate_pagetable_level1_ident;
	pagetable_t pgt_direct = (pagetable_t)relocate_pagetable_level1_direct_mapping;
	pagetable_t pgt_kernimg = (pagetable_t)relocate_pagetable_level1_high;

	// Calculate Kernel image size, and round up to 2MiB.
	uint64 kernel_size = (uint64)ekernel - (uint64)skernel;
	uint64 kernel_size_4K = ROUNDUP_2N(kernel_size, PGSIZE);
	uint64 kernel_size_2M = ROUNDUP_2N(kernel_size, PGSIZE_2M);

	kernel_image_end_4k = KERNEL_PHYS_BASE + kernel_size_4K;
	kernel_image_end_2M = KERNEL_PHYS_BASE + kernel_size_2M;

	printf("Kernel size: %p, Rounded to 2MiB: %p\n", kernel_size, kernel_size_2M);

	uint64 kernel_phys_base = KERNEL_PHYS_BASE;
	uint64 kernel_phys_end = kernel_phys_base + kernel_size_2M;
	uint64 kernel_virt_base = KERNEL_VIRT_BASE;
	uint64 kernel_virt_end = kernel_virt_base + kernel_size_2M;
	uint64 kernel_la_phy_base = kernel_image_end_2M;
	uint64 kernel_la_base = KERNEL_DIRECT_MAPPING_BASE + kernel_la_phy_base;
	uint64 kernel_la_end = kernel_la_base + PGSIZE_2M;

	infof("Kernel phy_base: %p, phy_end_4k:%p, phy_end_2M %p", kernel_phys_base, kernel_image_end_4k, kernel_phys_end);

	// We will still have some instructions executed on pc 0x8020xxxx before jumping to KIVA.
	// Step 2. Setup Identity Mapping for 0x80200000 -> 0x80200000, using 2MiB huge page.
	{
		uint64 VPN2 = PX(2, kernel_phys_base);
		pgt_root[VPN2] = MAKE_PTE((uint64)pgt_ident, 0);

		for (uint64 pa = kernel_phys_base; pa < kernel_phys_end; pa += PGSIZE_2M) {
			uint64 va = pa;
			uint64 vpn1 = PX(1, va);
			pgt_ident[vpn1] = MAKE_PTE(pa, PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
			printf("Mapping Identity: %p to %p\n", va, pa);
		}
	}

	// Step 3. Setup Kernel Image Mapping at high address
	{
		uint64 vpn2 = PX(2, kernel_virt_base);
		pgt_root[vpn2] = MAKE_PTE((uint64)pgt_kernimg, 0);

		for (uint64 pa = kernel_phys_base; pa < kernel_phys_end; pa += PGSIZE_2M) {
			uint64 va = pa + KERNEL_OFFSET;
			uint64 vpn1 = PX(1, va);
			pgt_kernimg[vpn1] = MAKE_PTE(pa, PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
			printf("Mapping kernel image: %p to %p\n", va, pa);
		}
	}

	// Step 4. Setup Kernel Direct Mapping (Partially)
	{
		// This Direct Mapping area is used in kvmmake.
		// Only map one 2MiB [kernel_la_base - kernel_la_end]
		uint64 vpn2 = PX(2, kernel_la_base);
		pgt_root[vpn2] = MAKE_PTE((uint64)pgt_direct, 0);
		uint64 vpn1 = PX(1, kernel_la_base);
		pgt_direct[vpn1] = MAKE_PTE(kernel_la_phy_base, PTE_R | PTE_W | PTE_A | PTE_D);
		printf("Mapping Direct Mapping: %p to %p\n", kernel_la_base, kernel_la_phy_base);
	}

	// Step 4. Enable SATP and jump to higher VirtAddr.
	printf("Enable SATP on temporary pagetable.\n");
	w_satp(MAKE_SATP(pgt_root));
	sfence_vma();

	uint64 jump = (uint64)&main_relocated + KERNEL_OFFSET;

	asm volatile ("mv a1, %0\n" :: "r"(jump));
	asm volatile ("la sp, boot_stack_top");
	asm volatile ("add sp, sp, %0" :: "r"(KERNEL_OFFSET));
	asm volatile ("jr a1");
	// jump();
}
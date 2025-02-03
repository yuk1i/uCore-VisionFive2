#include "console.h"
#include "debug.h"
#include "defs.h"
#include "loader.h"
#include "timer.h"
#include "trap.h"

extern char e_text[];  // kernel.ld sets this to end of kernel code.
extern char s_bss[];
extern char e_bss[];
extern char ekernel[], skernel[];
extern char boot_stack_top[];
extern char secondary_cpu_entry[];

#define KIVA_TO_PA(x) (((uint64)(x)) - KERNEL_OFFSET)
#define PA_TO_KIVA(x) (((uint64)(x)) + KERNEL_OFFSET)

uint64 __pa kernel_image_end_4k;
uint64 __pa kernel_image_end_2M;

static char relocate_pagetable[PGSIZE] __attribute__((aligned(PGSIZE)));
static char relocate_pagetable_level1_ident[PGSIZE] __attribute__((aligned(PGSIZE)));
static char relocate_pagetable_level1_direct_mapping[PGSIZE] __attribute__((aligned(PGSIZE)));
static char relocate_pagetable_level1_high[PGSIZE] __attribute__((aligned(PGSIZE)));

static void relocation_start();
static void main_relocated();
static void main_relocated2();
static void secondary_main_relocated();
static void secondary_main_relocated2();

uint64 read_pc() {
    uint64 pc;
    asm volatile("auipc %0, 0\n"  // 将当前 PC 存入寄存器
                 : "=r"(pc)       // 输出到变量 pc
                 :                // 无输入
                 :                // 无额外寄存器约束
    );
    return pc;
}

void main(int mhartid) {
    w_tp(mhartid);

    extern char boot_stack[];
    printf("\n\n=====\nHello World!\n=====\n\nBoot stack: %p\nclean bss: %p - %p\n", boot_stack, s_bss, e_bss);
    memset(s_bss, 0, e_bss - s_bss);
    printf("Boot cpuid 0, mhartid %d\n", mhartid);
    smp_init(mhartid);
    infof("basic smp inited, thread_id available now, we are cpu %d: %p", mhartid, mycpu());

    printf("Kernel is Relocating...\n");
    relocation_start();

    // We will jump to kernel's real pagetable in relocation_start.
    __builtin_unreachable();
}

static void main_relocated() {
    printf("Boot HART Relocated. We are at high address now! PC: %p\n", read_pc());

    // Step 4. Rebuild final kernel pagetable
    kvm_init();
    // vm_print(SATP_TO_PGTABLE(r_satp()));

    uint64 new_sp = mycpu()->sched_kstack_top;
    asm volatile("mv sp, %0" ::"r"(new_sp));
    main_relocated2();
}

static volatile int booted_count = 0;
static volatile int halt_specific_init = 0;

#define ENABLE_SMP 1

static void main_relocated2() {
    printf("Relocated. Boot halt sp at %p\n", r_sp());

#ifdef ENABLE_SMP
    printf("Boot another cpus.\n");

    for (int i = 0; i < NCPU; i++) {
        if (i == mycpu()->mhart_id)
            continue;
        printf("- booting hart %d", i);
        int booted_cnt = booted_count;

        int ret = sbi_hsm_hart_start(i, KIVA_TO_PA(secondary_cpu_entry), 0x114514);
        printf(" = %d. waiting for hart online\n", ret);

        while (booted_count == booted_cnt);
    }
#endif

    memset(relocate_pagetable, 0xde, PGSIZE);
    memset(relocate_pagetable_level1_ident, 0xde, PGSIZE);
    memset(relocate_pagetable_level1_direct_mapping, 0xde, PGSIZE);
    memset(relocate_pagetable_level1_high, 0xde, PGSIZE);

    trap_init();
    console_init();
    printf("UART inited.\n");
    plicinit();
    kpgmgrinit();
    uvm_init();
    proc_init();
    loader_init();
    load_init_app();

    timer_init();
    plicinithart();

    MEMORY_FENCE();
    halt_specific_init = 1;
    MEMORY_FENCE();

    infof("start scheduler!", mycpu()->mhart_id);
    scheduler();
}

void secondary_main_pa(int hartid) {
    printf("halt %d booting. Relocating\n", hartid);

    // init mycpu()
    w_tp(hartid);
    getcpu(hartid)->mhart_id = hartid;

    // switch to temporary pagetable.
    w_satp(MAKE_SATP(relocate_pagetable));
    sfence_vma();

    // jump to kernel's high address
    uint64 fn = (uint64)&secondary_main_relocated + KERNEL_OFFSET;

    asm volatile("mv a1, %0\n" ::"r"(fn));
    asm volatile("la sp, boot_stack_top");
    asm volatile("add sp, sp, %0" ::"r"(KERNEL_OFFSET));
    asm volatile("jr a1");

    __builtin_unreachable();
}

static void secondary_main_relocated() {
    extern pagetable_t kernel_pagetable;
    w_satp(MAKE_SATP(KVA_TO_PA(kernel_pagetable)));

    uint64 new_sp = mycpu()->sched_kstack_top;
    asm volatile("mv sp, %0" ::"r"(new_sp));
    secondary_main_relocated2();
}

static void secondary_main_relocated2() {
    printf("halt %d booted. sp: %p\n", mycpu()->mhart_id, r_sp());
    booted_count++;
    while (!halt_specific_init);

    trap_init();
    timer_init();
    plicinithart();
    infof("start scheduler!", mycpu()->mhart_id);
    scheduler();
}

static void relocation_start() {
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

    // Calculate Kernel Mapping Base & End
    uint64 kernel_phys_base = KERNEL_PHYS_BASE;
    uint64 kernel_phys_end = kernel_phys_base + kernel_size_2M;
    uint64 kernel_virt_base = KERNEL_VIRT_BASE;
    uint64 kernel_virt_end = kernel_virt_base + kernel_size_2M;

    // Calculate the first Direct Mapping Base & End
    uint64 kernel_la_phy_base = kernel_image_end_2M;
    uint64 kernel_la_base = KERNEL_DIRECT_MAPPING_BASE + kernel_la_phy_base;
    uint64 kernel_la_end = kernel_la_base + PGSIZE_2M;

    infof("Kernel phy_base: %p, phy_end_4k:%p, phy_end_2M %p",
          kernel_phys_base,
          kernel_image_end_4k,
          kernel_phys_end);

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

#ifdef LOG_LEVEL_DEBUG
    vm_print_tmp(pgt_root);
#endif

    // Step 4. Enable SATP and jump to higher VirtAddr.
    printf("Enable SATP on temporary pagetable.\n");
    w_satp(MAKE_SATP(pgt_root));
    sfence_vma();

    uint64 jump = (uint64)&main_relocated + KERNEL_OFFSET;
    uint64 new_sp = (uint64)&boot_stack_top + KERNEL_OFFSET;

    asm volatile("mv a1, %0\n" ::"r"(jump));
    asm volatile("la sp, boot_stack_top");
    asm volatile("add sp, sp, %0" ::"r"(KERNEL_OFFSET));
    asm volatile("jr a1");
    // jump();
}
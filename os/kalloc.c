#include "kalloc.h"
#include "defs.h"
#include "riscv.h"

extern char ekernel[];

struct linklist {
	struct linklist *next;
};

struct {
	struct linklist *freelist;
} kmem;

int kalloc_inited = 0;

void freerange(void *kpgva_start, void *kpgva_end)
{
	assert(PGALIGNED((uint64)kpgva_start));
	assert(PGALIGNED((uint64)kpgva_end));

	for (uint64 p = (uint64)kpgva_end - PGSIZE; p >= (uint64)kpgva_start; p -= PGSIZE)
		kfreepage((void *)KVA_TO_PA(p));
	kalloc_inited = 1;
}

extern uint64 __kva kpage_allocator_base;
extern uint64 __kva kpage_allocator_size;
static spinlock_t kpagelock;

void kpgmgrinit()
{
	spinlock_init(&kpagelock, "pageallocator");
	infof("page allocator init: base: %p, stop: %p", kpage_allocator_base, kpage_allocator_base + kpage_allocator_size);
	freerange((void *)kpage_allocator_base, (void *)(kpage_allocator_base + kpage_allocator_size));
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfreepage(void *__pa pa)
{
	struct linklist *l;

	acquire(&kpagelock);

	uint64 __kva kvaddr = PA_TO_KVA(pa);
	if (!PGALIGNED((uint64)pa) || !(kpage_allocator_base <= kvaddr && kvaddr < kpage_allocator_base + kpage_allocator_size))
		panic("kfree: invalid page %p", pa);
	// Fill with junk to catch dangling refs.
	if (kalloc_inited)
		debugf("free : %p", pa);
	memset((void *)kvaddr, 0xdd, PGSIZE);
	l = (struct linklist *)kvaddr;
	l->next = kmem.freelist;
	kmem.freelist = l;

	release(&kpagelock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *__pa kallocpage()
{
	uint64 ra;
	asm volatile("mv %0, ra\n": "=r"(ra));
	
	acquire(&kpagelock);
	struct linklist *l;
	l = kmem.freelist;
	if (l) {
		kmem.freelist = l->next;
		memset((char *)l, 0xaf, PGSIZE); // fill with junk
	}
	debugf("alloc: %p, by %p", l, ra);
	release(&kpagelock);
	return (void *)KVA_TO_PA((uint64)l);
}

// Object Allocator
static uint64 allocator_mapped_va = KERNEL_ALLOCATOR_BASE;
extern pagetable_t kernel_pagetable;

static inline void bit_set(uint8 *bitmap, uint64 index)
{
	bitmap[index / 8] |= (1 << (index % 8));
}

static inline void bit_clear(uint8 *bitmap, uint64 index)
{
	bitmap[index / 8] &= ~(1 << (index % 8));
}

static inline int is_bit_set(const uint8 *bitmap, uint64 index)
{
	return bitmap[index / 8] & (1 << (index % 8));
}

void allocator_init(struct allocator *alloc, char *name, uint64 object_size, uint64 count)
{
	memset(alloc, 0, sizeof(*alloc));
	// record basic properties of the allocator
	alloc->name = name;
	spinlock_init(&alloc->lock, "allocator");
	alloc->object_size = object_size;
	alloc->object_size_aligned = ROUNDUP_2N(object_size, 16);
	alloc->max_count = count;

	assert(count <= PGSIZE * 8);

	// calculate how many pages do we need
	uint64 total_size = alloc->object_size_aligned * alloc->max_count;
	total_size = PGROUNDUP(total_size);

	// calculate the pool base and end.
	alloc->pool_base = allocator_mapped_va;
	alloc->pool_end = alloc->pool_base + total_size;

	infof("allocator %s inited base %p", name, alloc->pool_base);

	// add a significant gap between different types of objects.
	allocator_mapped_va += ROUNDUP_2N(total_size, KERNEL_ALLOCATOR_GAP);

	// allocate physical pages and kvmmap [pool_base, pool_end)
	for (uint64 va = alloc->pool_base; va < alloc->pool_end; va += PGSIZE) {
		void *__pa pg = kallocpage();
		if (pg == NULL)
			panic("kallocpage");
		memset((void *)PA_TO_KVA(pg), 0xf8, PGSIZE);
		kvmmap(kernel_pagetable, va, (uint64)pg, PGSIZE, PTE_A | PTE_D | PTE_R | PTE_W);
	}
	sfence_vma();

	// allocate the bitmap page:
	alloc->bitmap = (uint8 *)PA_TO_KVA(kallocpage());

	// mark all bits are allocated
	memset(alloc->bitmap, 0xff, PGSIZE);

	// mark [count] objects are freed
	for (uint64 i = 0; i < alloc->max_count; i++) {
		bit_clear(alloc->bitmap, i);
	}
	alloc->available_count = alloc->max_count;
	alloc->allocated_count = 0;
}

void *kalloc(struct allocator *alloc)
{
	assert(alloc);
	acquire(&alloc->lock);
	if (alloc->available_count == 0)
		panic("unavailable");
	alloc->available_count--;
	for (uint64 i = 0; i < alloc->max_count; i++) {
		if (!is_bit_set(alloc->bitmap, i)) {
			bit_set(alloc->bitmap, i);
			alloc->allocated_count++;
			uint8* ret = (uint8 *)(alloc->pool_base + (i * alloc->object_size_aligned));
			assert(alloc->allocated_count + alloc->available_count == alloc->max_count);
			
			memset(ret, 0xf9, alloc->object_size_aligned);
			tracef("kalloc(%s) returns %p", alloc->name, ret);
			release(&alloc->lock);
			return ret;
		}
	}
	panic("should not happen");
}

void kfree(struct allocator *alloc, void *obj)
{
	if (obj == NULL)
		return;
	acquire(&alloc->lock);
	assert(alloc);
	assert(alloc->pool_base <= (uint64)obj && (uint64)obj < alloc->pool_end);

	uint64 index = ((uint64)obj - alloc->pool_base) / alloc->object_size_aligned;
	if (!is_bit_set(alloc->bitmap, index)) {
		panic("double free: %p", obj);
	}

	bit_clear(alloc->bitmap, index);
	alloc->allocated_count--;
	alloc->available_count++;
	assert(alloc->allocated_count + alloc->available_count == alloc->max_count);
	memset(obj, 0xfa, alloc->object_size_aligned);
	release(&alloc->lock);
}
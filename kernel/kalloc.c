// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// --- Superpage (2MB) allocator ---------------------------------------------
#ifndef N_SUPER
#define N_SUPER 8                                   // reserve a handful
#endif

struct {
  struct spinlock lock;
  void *base[N_SUPER];   // KVA bases of reserved 2MB chunks
  char  free[N_SUPER];   // 1 = free, 0 = used
  int   n;               // how many reserved
} kmemsuper;

static void
superinit(void)
{
  initlock(&kmemsuper.lock, "kmemsuper");
  kmemsuper.n = 0;
  for (int i = 0; i < N_SUPER; i++) kmemsuper.free[i] = 0;
}

void*
superalloc(void)
{
  acquire(&kmemsuper.lock);
  for (int i = 0; i < kmemsuper.n; i++) {
    if (kmemsuper.free[i]) {
      kmemsuper.free[i] = 0;
      void *p = kmemsuper.base[i];
      release(&kmemsuper.lock);
      return p;
    }
  }
  release(&kmemsuper.lock);
  return 0;
}

void
superfree(void *pa)
{
  if (pa == 0 || !IS_MEGA_ALIGNED(pa))
    panic("superfree: bad addr");

  acquire(&kmemsuper.lock);
  int found = 0;
  for (int i = 0; i < kmemsuper.n; i++) {
    if (kmemsuper.base[i] == pa) {
      if (kmemsuper.free[i])
        panic("superfree: double free");
      kmemsuper.free[i] = 1;
      found = 1;
      break;
    }
  }
  release(&kmemsuper.lock);
  if (!found)
    panic("superfree: unknown chunk");
}

// Forward decls for functions used below
void freerange(void *pa_start, void *pa_end);
extern char end[]; // first address after kernel. defined by kernel.ld.
void kfree(void *pa);

// One-time reservation of 2MB chunks from [pa_start, pa_end),
// skipping them from the 4KB freelist.
static void
reserve_super_spans(void *pa_start, void *pa_end)
{
  char *p = (char*)PGROUNDUP((uint64)pa_start);
  char *lim = (char*)pa_end;
  for (; p + PGSIZE <= lim; ) {
    if (kmemsuper.n < N_SUPER && IS_MEGA_ALIGNED(p) && p + MEGAPGSIZE <= lim) {
      kmemsuper.base[kmemsuper.n] = p;
      kmemsuper.free[kmemsuper.n] = 1;
      kmemsuper.n++;
      p += MEGAPGSIZE;
    } else {
      kfree(p);            // feed one 4KB page into the normal freelist
      p += PGSIZE;
    }
  }
}

// --- Canonical xv6 4KB allocator -------------------------------------------

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit(void)
{
  initlock(&kmem.lock, "kmem");
  // superpages: reserve a handful of 2MB chunks, and feed the rest to 4KB freelist.
  superinit();
  reserve_super_spans(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a call to kalloc().
// (The exception is when initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

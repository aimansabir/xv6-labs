#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
/*
 * create a direct-map page table for the kernel.
 */
/*
 * create a direct-map page table for the kernel.
 */
/*
 * create a direct-map page table for the kernel.
 */
/*
 * create a direct-map page table for the kernel.
 */
/*
 * create a direct-map page table for the kernel.
 */
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif 

  // --- SUPERPAGE MODIFICATION ---

  // PLIC (map 64MB with 2MB superpages)
  // 0x4000000 is 64MB, or 32 * 2MB pages
  for(uint64 pa = PLIC; pa < PLIC + 0x4000000; pa += MEGAPGSIZE) {
    mappages_super(kpgtbl, pa, MEGAPGSIZE, pa, PTE_R | PTE_W);
  }

  // map kernel text executable and read-only (4KB pages)
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  
  // Find the 2MB boundary *after* etext.
  uint64 pa_2m_boundary = (PGROUNDUP((uint64)etext) + MEGAPGSIZE - 1) & ~(MEGAPGSIZE - 1);
  
  // 1. Map 4KB pages from etext up to that 2MB boundary.
  uint64 size = pa_2m_boundary - (uint64)etext;
  if(size > 0)
    kvmmap(kpgtbl, (uint64)etext, (uint64)etext, size, PTE_R | PTE_W);

  // 2. Map 2MB superpages starting from the boundary
  uint64 pa = pa_2m_boundary;
  for(; pa + MEGAPGSIZE <= PHYSTOP; pa += MEGAPGSIZE) {
    mappages_super(kpgtbl, pa, MEGAPGSIZE, pa, PTE_R | PTE_W);
  }
  
  // 3. map remaining 4KB pages
  size = PHYSTOP - pa;
  if(size > 0)
    kvmmap(kpgtbl, pa, pa, size, PTE_R | PTE_W);
  
  // --- END SUPERPAGE MODIFICATION ---

  // map the trampoline for trap entry/exit
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}
// Initialize the kernel_pagetable, shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
#ifdef LAB_PGTBL
      if(PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  // Manually walk the 3-level page table.
  
  // L2
  pte = &pagetable[PX(2, va)];
  if((*pte & PTE_V) == 0)
    return 0;
  // NO PTE_U check at L2
  if(PTE_LEAF(*pte)) {
    // L2 leaf (1G page) - not used in this lab
    // pa = PTE2PA(*pte);
    // return pa + (va & (GIGAPGSIZE - 1));
    panic("walkaddr: 1G page not supported");
  }
  pagetable = (pagetable_t)PTE2PA(*pte);

  // L1
  pte = &pagetable[PX(1, va)];
  if((*pte & PTE_V) == 0)
    return 0;
  // NO PTE_U check at L1
  if(PTE_LEAF(*pte)) {
    // L1 leaf (2M superpage)
    if((*pte & PTE_U) == 0) // Check PTE_U at leaf
      return 0;
    pa = PTE2PA(*pte);
    return pa + (va & (MEGAPGSIZE - 1)); // Add offset
  }
  pagetable = (pagetable_t)PTE2PA(*pte);

  // L0
  pte = &pagetable[PX(0, va)];
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0) // Check PTE_U at leaf
    return 0;
  
  // L0 leaf (4K page)
  pa = PTE2PA(*pte);
  return pa + (va & (PGSIZE - 1)); // Add offset
}
  
#if defined(LAB_PGTBL) || defined(SOL_MMAP) || defined(SOL_COW)
// Recursive helper function for vmprint
// Print a page table (3-level Sv39), in the format the lab expects.
// Recursive helper for vmprint.
static void
vmprint_walk(pagetable_t pagetable, int level, uint64 va_base)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) == 0) // Skip invalid PTEs
      continue;

    uint64 pa = PTE2PA(pte);
    
    // Compute the full VA for this entry.
    // L2 (level=2) shift is 30 (12 + 9*2).
    // L1 (level=1) shift is 21 (12 + 9*1).
    // L0 (level=0) shift is 12 (12 + 9*0).
    uint64 shift = 12 + 9*level;
    uint64 va = va_base | ((uint64)i << shift);

    // Indentation: 1 for L2, 2 for L1, 3 for L0.
    for (int d = 0; d < 3 - level; d++)
      printf(" ..");

    // Print the VA, PTE, and PA.
    // Must cast to (void *) for %p format specifier.
    printf("%p: pte 0x%p pa 0x%p\n", (void *)va, (void *)pte, (void *)pa);

    // If this PTE points to a lower-level page table (not a leaf):
    if ((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
      // Recurse down to the next level (level-1).
      vmprint_walk((pagetable_t)pa, level-1, va);
    }
    // Otherwise, it's a leaf (4K or 2M), so we stop recursing.
  }
}

// Print the page table.
void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", (void *)pagetable);
  // Start the walk at the top level (L2)
  vmprint_walk(pagetable, 2, 0);
}
#endif



// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
//
// This is the SUPERPAGE-AWARE uvmunmap with demotion.
// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
//
// This is the SUPERPAGE-AWARE uvmunmap with demotion.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; ){
    pte_t *l2pte = &pagetable[PX(2, a)];
    
    if((*l2pte & PTE_V) == 0) {
      // L2 PTE not valid, skip to the next 2MB boundary (was GIGAPGSIZE)
      a = (a + MEGAPGSIZE) & ~(MEGAPGSIZE - 1); // <--- FIX 1
      continue;
    }
    
    if(PTE_LEAF(*l2pte)) {
      panic("uvmunmap: 1G pages not supported");
    }

    pagetable_t l1table = (pagetable_t)PTE2PA(*l2pte);
    pte_t *l1pte = &l1table[PX(1, a)];

    if((*l1pte & PTE_V) == 0) {
      // L1 PTE not valid, skip 2M
      a = (a + MEGAPGSIZE) & ~(MEGAPGSIZE - 1);
      continue;
    }
    
    if(PTE_LEAF(*l1pte)) {
      // This is a 2M superpage.
      uint64 super_va = a & ~(MEGAPGSIZE - 1);
      // uint64 pages_in_super = MEGAPGSIZE / PGSIZE; // <--- FIX 2: Removed unused variable
      uint64 unmap_end = va + npages*PGSIZE;

      // Check if we are unmapping the *entire* superpage
      if ( (a == super_va) && (unmap_end >= (super_va + MEGAPGSIZE)) ) {
        // Yes, free the whole thing
        if(do_free){
          uint64 pa = PTE2PA(*l1pte);
          superfree((void*)pa);
        }
        *l1pte = 0; // Clear the PTE
        a += MEGAPGSIZE;
        continue;
      } else {
        // NO, we are unmapping a *part* of it. We must demote.
        
        // 1. Get superpage PA and permissions
        uint64 spa = PTE2PA(*l1pte);
        int perm = PTE_FLAGS(*l1pte);

        // 2. Allocate new L0 table
        pagetable_t l0table = (pagetable_t)kalloc();
        if(l0table == 0)
          panic("uvmunmap: demote kalloc failed");
        memset(l0table, 0, PGSIZE);

        // 3. Allocate 512 4K pages, copy data, map them in L0
        for (int i = 0; i < 512; i++) {
          char *page = kalloc();
          if(page == 0)
            panic("uvmunmap: demote kalloc failed");
          memmove(page, (void*)(spa + i*PGSIZE), PGSIZE);
          l0table[i] = PA2PTE((uint64)page) | perm | PTE_V;
        }
        
        // 4. Install new L0 table (make L1 PTE a pointer, not a leaf)
        *l1pte = PA2PTE(l0table) | PTE_V;

        // 5. Free the original 2MB superpage
        if(do_free) {
          superfree((void*)spa);
        }

        // 6. DO NOT continue. Fall through to the 4KB-page-freeing logic
        // which will now run on the page we *just* created.
      }
    }
    
// Not a superpage OR demoted, so it must be a 4K page.
    pte = walk(pagetable, a, 0);
    
    if(pte == 0) {
        // This shouldn't happen given our L1/L2 checks, but be safe.
        a += PGSIZE;
        continue;
    }

    if((*pte & PTE_V) == 0) {
        // Page is not mapped. This is normal, not an error.
        // Just skip it and advance to the next page.
        a += PGSIZE;
        continue;
    }

    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0; // Clear the PTE
    a += PGSIZE;
  }
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;
  int sz;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += sz){
    sz = PGSIZE;
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
#ifndef LAB_SYSCALL
    memset(mem, 0, sz);
 #endif
    if(mappages(pagetable, a, sz, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // backtrace();
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
//
// This is the SUPERPAGE-AWARE uvmcopy.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte, *l1pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz;){
    // Check for 2MB superpage first
    pte_t *l2pte = &old[PX(2, i)];
    if((*l2pte & PTE_V) != 0) {
      pagetable_t l1table = (pagetable_t)PTE2PA(*l2pte);
      l1pte = &l1table[PX(1, i)];

      // Is it a 2MB superpage? (L1 leaf)
      if((*l1pte & PTE_V) != 0 && (*l1pte & (PTE_R|PTE_W|PTE_X)) != 0) {
        pa = PTE2PA(*l1pte);
        flags = PTE_FLAGS(*l1pte);

        // Check if the entire 2MB page is within the process size
        if (i + MEGAPGSIZE <= sz) {
          // Try to allocate a 2MB page
          mem = superalloc();
          if(mem != 0) {
            // superalloc SUCCEEDED
            memmove(mem, (char*)pa, MEGAPGSIZE);
            if(mappages_super(new, i, MEGAPGSIZE, (uint64)mem, flags) != 0){
              superfree(mem);
              goto err;
            }
            i += MEGAPGSIZE; // Advance by 2MB
            continue; // Go to next loop iteration
          }
          // superalloc FAILED.
          // Fall through to the 4KB copy path below.
        }
        // If not fully within sz, fall through to 4KB copy path
      }
    }

    // Fallback: 4KB page copy
    if((pte = walk(old, i, 0)) == 0) {
      // Page isn't mapped, skip it
      i += PGSIZE;
      continue;
    }
    if((*pte & PTE_V) == 0) {
      // Page isn't valid, skip it
      i += PGSIZE;
      continue;
    }

    // --- THE FIX ---
    // We cannot just use PTE2PA(*pte), as 'pte' might be an L1
    // superpage entry. walkaddr() will find the *correct*
    // physical address for the 4KB page at virtual address 'i'.
    pa = walkaddr(old, i);
    if(pa == 0) {
      // Should be impossible if walk succeeded, but good to check
      i += PGSIZE;
      continue;
    }
    // --- END FIX ---

    flags = PTE_FLAGS(*pte); // Flags are the same for the whole page/superpage
    if((mem = kalloc()) == 0) // Allocate 4KB page
      goto err;

    // 'pa' now points to the correct 4KB block of physical memory
    memmove(mem, (char*)pa, PGSIZE); // Copy 4KB

    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
    i += PGSIZE; // Advance by 4KB
  }
  return 0;

 err:
  uvmunmap(new, 0, PGROUNDUP(i)/PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    if((pte = walk(pagetable, va0, 0)) == 0) {
      // printf("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }


    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
    
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;
  
  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}




// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64
vmfault(pagetable_t pagetable, uint64 va, int read)
{
  uint64 mem;
  struct proc *p = myproc();
  

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if(ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64) kalloc();
  if(mem == 0)
    return 0;
  memset((void *) mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

int
ismapped(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}



#ifdef LAB_PGTBL
pte_t*
pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0);
}
#endif

// In kernel/vm.c

// Create a 2MB superpage mapping at L1.
// va and pa must be 2MB-aligned.
// size must be MEGAPGSIZE.
// perm is the PTE_R, PTE_W, PTE_X, PTE_U bits.
// Returns 0 on success, -1 on failure.
int
mappages_super(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  pte_t *l2pte;
  pagetable_t l1table;

  if (size != MEGAPGSIZE)
    panic("mappages_super: size not MEGAPGSIZE");
  if (va % MEGAPGSIZE != 0)
    panic("mappages_super: va not aligned");
  if (pa % MEGAPGSIZE != 0)
    panic("mappages_super: pa not aligned");

  // Get the L2 PTE for this virtual address
  l2pte = &pagetable[PX(2, va)];

  // Get the L1 page table
  if (*l2pte & PTE_V) {
    // L1 table already exists
    l1table = (pagetable_t)PTE2PA(*l2pte);
  } else {
    // L1 table does not exist, allocate one
    l1table = (pagetable_t)kalloc();
    if (l1table == 0)
      return -1;
    memset(l1table, 0, PGSIZE);
    // Make the L2 PTE point to the new L1 table
    *l2pte = PA2PTE(l1table) | PTE_V;
  }

  // Get the L1 PTE
  pte_t *l1pte = &l1table[PX(1, va)];

  // This L1 entry should not already be in use
  if (*l1pte & PTE_V) {
    // If it is, something is wrong. We are "re-mapping" a page.
    // In a real OS, we might free the old page. For this lab,
    // it indicates a logic error in how we call this function.
    panic("mappages_super: remap");
  }
  
  // Install the superpage leaf entry.
  // This PTE points directly to the 2MB physical page.
  // The hardware knows it's a leaf because PTE_R/W/X bits are set.
  *l1pte = PA2PTE(pa) | perm | PTE_V;

  return 0;
}

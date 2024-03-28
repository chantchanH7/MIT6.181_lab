// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // each cpu with a freelist and a lock

void
kinit()
{
//  printf("kinit in\n");
  initlock(&kmem[0].lock, "kmem_0");
  initlock(&kmem[1].lock, "kmem_1");
  initlock(&kmem[2].lock, "kmem_2");
  initlock(&kmem[3].lock, "kmem_3");
  initlock(&kmem[4].lock, "kmem_4");
  initlock(&kmem[5].lock, "kmem_5");
  initlock(&kmem[6].lock, "kmem_6");
  initlock(&kmem[7].lock, "kmem_7");
//  printf("kinit: init all locks\n");
  freerange(end, (void*)PHYSTOP);
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
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  // 拿到物理地址对应的cpu的索引
  int index = (((uint64)pa - (uint64)KERNBASE) * NCPU) / (PHYSTOP - (uint64)KERNBASE); // get the cpuid where the pa should be in its freelist
  if(index < 0 || index > 7) {
      panic("func > kfree: index out of bounds\n");
  }

  // 把物理地址放入对应的cpu的freelist中
  acquire(&kmem[index].lock);
  r->next = kmem[index].freelist;
  kmem[index].freelist = r;

  release(&kmem[index].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // 获取当前cpu的id
  // the call to cpuid requires interrupts turned off
  push_off();
  int index = cpuid(); // get the id of current cpu
  pop_off();

  acquire(&kmem[index].lock); // 请求当前cpu freelist 的锁
  r = kmem[index].freelist;
  if(r)
    kmem[index].freelist = r->next;
  release(&kmem[index].lock); // 释放当前cpu freelist 的锁

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  // 判断一下当前cpu维护的freelist是否已经是空了，如果是空的，我们要向其它cpu的freelist申请空间
  if(!r) {
      for(int i = 0; i < NCPU; ++i) {
          acquire(&kmem[i].lock); // 请求遍历到的cpu的锁
          if(kmem[i].freelist) {
              r = kmem[i].freelist;
              kmem[i].freelist = r->next;
              memset((char*)r, 5, PGSIZE); // fill with junk
              release(&kmem[i].lock);
              return (void*)r;
          } else {
              release(&kmem[i].lock); // 释放遍历到的cpu的锁
          }
      }
  }




  return (void*)r;
}

// 物理内存分配器。
// 它管理用户进程、内核栈、页表页和管道缓冲区使用的 4096 字节物理页。
// 实现方式是简单的空闲链表：每页空闲内存本身作为一个 run 节点，
// 通过 kmem.freelist 串起来；申请时从头弹出，释放时重新压回头部。
// 所有操作都在 kmem.lock 保护下进行，保证多核并发安全。

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
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // 把链接脚本给出的 end（内核代码/数据结束地址）到 PHYSTOP
  // 之间的所有物理页加入空闲链表。
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  // 起点向上对齐到页边界，确保每次释放一整页。
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // 跳过动态模块保留区域，防止被普通物理页分配器使用。
    if((uint64)p >= DYNMOD_BASE && (uint64)p < DYNMOD_BASE + DYNMOD_AREA_SIZE)
      continue;
    kfree(p);
  }
}

// 释放 pa 指向的物理页。
// 正常情况下 pa 来自 kalloc()；初始化分配器时 freerange() 也会调用它。
void
kfree(void *pa)
{
  struct run *r;

  // 校验地址按页对齐且在可用物理内存范围内，否则内核存在内存管理错误。
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 用垃圾数据填充页面，便于后续发现悬空指针或未初始化内存。
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 在锁内把该页插到空闲链表头部。
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// 分配一个 4096 字节的物理页。
// 成功时返回可用的内核虚拟地址，失败（内存耗尽）时返回 0。
void *
kalloc(void)
{
  struct run *r;

  // 从空闲链表头部取出一页；头插法让刚释放的页优先被复用。
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // 同样用垃圾值填充，便于发现未初始化访问
  return (void*)r;
}

// 返回当前空闲物理内存字节数。
// 通过遍历空闲链表统计，供 sysinfo 等模块读取。
uint64
freemem(void)
{
  uint64 n = 0;
  struct run *r;

  acquire(&kmem.lock);
  for(r = kmem.freelist; r; r = r->next)
    n++;
  release(&kmem.lock);

  return n * PGSIZE;
}

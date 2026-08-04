// 虚拟内存管理。
//
// 内核页表使用直接映射：虚拟地址与物理地址相同，
// 便于内核通过简单指针访问全部物理内存和设备寄存器。
// 用户进程则使用独立的页表，把连续虚拟地址映射到分散的物理页，
// 并让用户代码无法访问内核区域。
//
// RISC-V Sv39 使用三级页表，每级 512 项，每项 8 字节：
// 39..63 位必须为 0，30..38 / 21..29 / 12..20 分别是三级索引，
// 0..11 位是页内偏移。walk() 实现了这条三级查找路径。
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
 * 内核页表，所有 CPU 共享同一个地址空间。
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// 创建内核直接映射页表，并在启动阶段完成全部内核映射。
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // 映射串口寄存器，printf 最终写到这些寄存器。
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // 映射 virtio 磁盘的 MMIO 控制接口。
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 映射中断控制器 PLIC 的寄存器区域。
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // 内核代码段映射为可读可执行、不可写，防止内核代码被意外篡改。
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 内核数据段映射为可读写。
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, DYNMOD_BASE-(uint64)etext, PTE_R | PTE_W);

  // 动态模块区域单独映射为可读可写可执行。
  kvmmap(kpgtbl, DYNMOD_BASE, DYNMOD_BASE, DYNMOD_SIZE, PTE_R | PTE_W | PTE_X);

  // 动态模块区域之后的 RAM 映射为可读写。
  kvmmap(kpgtbl, DYNMOD_BASE + DYNMOD_SIZE, DYNMOD_BASE + DYNMOD_SIZE,
         PHYSTOP - (DYNMOD_BASE + DYNMOD_SIZE), PTE_R | PTE_W);

  // trampoline 映射在最高虚拟地址，用户和内核页表映射同一物理页，
  // 使陷阱发生时切换页表不会导致执行流“丢失”。
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // 为每个进程预分配并映射内核栈，栈底与 trampoline 之间留保护页。
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// 向内核页表添加一段映射，仅在启动阶段使用；
// 它不会刷新 TLB，也不会开启分页。
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// 初始化所有 CPU 共享的内核页表。
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// 把当前 CPU 的页表基址写入 satp 寄存器并开启分页。
void
kvminithart()
{
  // 确保页表内容写入对所有内存访问可见。
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // 切换页表后 TLB 中可能残留旧映射，需要冲刷。
  sfence_vma();
}

// 返回虚拟地址 va 在页表中的叶子 PTE 地址。
// 若 alloc 非 0，缺失的中间页表页会自动分配。
// RISC-V Sv39 采用三级页表：每页 512 项 64 位 PTE，
// 64 位虚拟地址被划分为：
//   39..63 -- 必须为 0
//   30..38 -- 第 2 级索引
//   21..29 -- 第 1 级索引
//   12..20 -- 第 0 级索引
//    0..11 -- 页内偏移
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// 通过虚拟地址查找物理地址；未映射时返回 0。
// 只用于查询用户页：必须满足 PTE_U，防止内核被用户映射欺骗。
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// 为从 va 开始的虚拟地址范围创建 PTE，使其映射到从 pa 开始的物理地址。
// va 和 size 必须是页对齐的。
// 成功返回 0；若中间页表页分配失败返回 -1。
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

// 创建一个空的用户页表；内存不足时返回 0。
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

// 删除从 va 开始的 npages 个映射。va 必须页对齐；
// 映射不存在时直接跳过。do_free 为真时同时释放物理页。
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;   
    if((*pte & PTE_V) == 0)  // has physical page been allocated?
      continue;
    if(do_free && (*pte & PTE_SHM) == 0){
      uint64 pa = PTE2PA(*pte);
      if(*pte & PTE_COW)
        cow_release(pa);
      else
        kfree((void*)pa);
    }
    *pte = 0;
  }
}

// 为进程分配新页并建立映射，把进程大小从 oldsz 扩展到 newsz。
// newsz 无需页对齐。失败时回滚已分配页并返回 0。
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// 释放用户页，把进程大小从 oldsz 收缩到 newsz。
// 参数无需页对齐，允许 newsz 大于 oldsz（此时保持原大小）。
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

// 递归释放所有页表页。
// 调用前必须已删除全部叶子映射，否则表示仍有用户页未被释放。
void
freewalk(pagetable_t pagetable)
{
  // 每级页表包含 2^9 = 512 项 PTE。
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // 该 PTE 指向下一级页表，先递归释放子表。
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // 叶子页仍有效说明用户内存没有先释放，属于错误。
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// 先释放用户内存页，再递归释放页表页。
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// 把父进程页表的内存复制到子进程页表。
// 这里同时复制物理内存，而不是共享页面；
// 因此 fork 后父子进程拥有各自独立的地址空间。
// 成功返回 0；失败时释放已分配的页并返回 -1。
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;   // 该页表项尚未分配，跳过
    if((*pte & PTE_V) == 0)
      continue;   // 物理页尚未分配，跳过
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if(flags & PTE_SHM){
      // 共享页：子进程直接映射同一物理页，不复制内容。
      shm_addref_pa(pa);
      if(mappages(new, i, PGSIZE, pa, flags) != 0){
        shm_subref_pa(pa);
        goto err;
      }
      continue;
    }
    if(flags & PTE_COW){
      // 已经是 COW 页：继续共享同一物理页。
      cow_add(pa);
      if(mappages(new, i, PGSIZE, pa, flags) != 0){
        cow_release(pa);
        goto err;
      }
      continue;
    }
    if(flags & PTE_W){
      // 可写页转为 COW：父进程与子进程都变成只读共享。
      uint64 oldflags = PTE_FLAGS(*pte);
      *pte = (*pte & ~PTE_W) | PTE_COW;
      flags = PTE_FLAGS(*pte);
      cow_add(pa); // 父进程引用
      cow_add(pa);
      // 子进程引用
      if(mappages(new, i, PGSIZE, pa, flags) != 0){
        // 回滚父进程页表并释放本次新增的两个引用，避免引用计数泄漏。
        *pte = PA2PTE(pa) | oldflags;
        cow_release(pa);
        cow_release(pa);
        goto err;
      }
      continue;
    }
    if((mem = kalloc()) == 0)
      goto err;
    // 复制整页内容，保持父子进程数据互不影响。
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }

  // 共享内存映射在用户堆大小之外，fork 时必须单独复制。
  for(i = SHM_BASE; i < TRAPFRAME; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;
    if((*pte & PTE_SHM) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    shm_addref_pa(pa);
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      shm_subref_pa(pa);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// 清除 PTE 的用户访问位，使该页对用户不可访问。
// exec 用它把用户栈上方的保护页标记为不可访问，
// 从而捕获栈溢出。
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// 从内核缓冲区拷贝 len 字节到用户虚拟地址 dstva。
// 用户地址可能跨页，因此逐页解析物理地址。
// 若遇到惰性分配缺失的页面，则通过 vmfault() 补上。
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
  
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    pte = walk(pagetable, va0, 0);
    // 禁止内核向用户只读代码页写数据；
    // COW 页则先复制并转为可写。
    if((*pte & PTE_W) == 0){
      if((*pte & PTE_COW) && cow_handle(pagetable, va0) == 0){
        pte = walk(pagetable, va0, 0);
        pa0 = PTE2PA(*pte);
      } else
        return -1;
    }
      
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

// 从用户虚拟地址 srcva 拷贝 len 字节到内核缓冲区 dst。
// 与 copyout 类似，逐页验证并翻译用户地址。
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

// 从用户空间拷贝一个以 '\0' 结尾的字符串到内核缓冲区。
// 每次最多读取 max 字节，避免未验证的用户指针造成越界访问。
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

// 用户进程访问尚未分配的惰性内存页时，通过缺页处理分配并映射该页。
// sys_sbrk() 只增加虚拟地址空间大小，不立即分配物理页；
// 首次访问时由这里的 vmfault() 补齐物理页。
// 地址非法、页已映射或内存不足时返回 0；成功返回物理地址。
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
ismapped(pagetable_t pagetable, uint64 va)
{
  // 检查虚拟地址是否已有有效 PTE，供惰性分配判断使用。
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}

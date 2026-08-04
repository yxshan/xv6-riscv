// 共享内存。
//
// 共享内存段在创建时分配物理页，shmat 把同一组物理页映射到
// 多个进程的用户地址空间。共享页用 PTE_SHM 标记，避免进程退出时
// 被当作普通用户页释放。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "shm.h"

static struct shmseg shmsegs[NSHM];
static struct spinlock shmlock;
static int nextshmid = 1;
static uint64 shm_nextva = SHM_BASE;

static struct shmseg*
shm_find(int id)
{
  for(int i = 0; i < NSHM; i++){
    if(shmsegs[i].used && shmsegs[i].id == id)
      return &shmsegs[i];
  }
  return 0;
}

int
shmget(int key, int size)
{
  struct shmseg *seg;
  int npages;

  if(key <= 0 || size <= 0 || size > SHM_MAX_SIZE)
    return -1;

  acquire(&shmlock);

  // 已存在的 key 返回原段，但大小必须一致。
  for(int i = 0; i < NSHM; i++){
    if(shmsegs[i].used && shmsegs[i].key == key){
      if(shmsegs[i].npages * PGSIZE != PGROUNDUP(size)){
        release(&shmlock);
        return -1;
      }
      int id = shmsegs[i].id;
      release(&shmlock);
      return id;
    }
  }

  seg = 0;
  for(int i = 0; i < NSHM; i++){
    if(!shmsegs[i].used){
      seg = &shmsegs[i];
      break;
    }
  }
  if(seg == 0){
    release(&shmlock);
    return -1;
  }

  npages = PGROUNDUP(size) / PGSIZE;
  if(shm_nextva + npages*PGSIZE > TRAPFRAME){
    release(&shmlock);
    return -1;
  }

  for(int i = 0; i < npages; i++){
    seg->pages[i] = (uint64)kalloc();
    if(seg->pages[i] == 0){
      for(int j = 0; j < i; j++)
        kfree((void*)seg->pages[j]);
      release(&shmlock);
      return -1;
    }
    memset((void*)seg->pages[i], 0, PGSIZE);
  }

  seg->used = 1;
  seg->id = nextshmid++;
  seg->key = key;
  seg->ref = 0;
  seg->npages = npages;
  seg->va = shm_nextva;
  shm_nextva += npages * PGSIZE;

  int id = seg->id;
  release(&shmlock);
  return id;
}

uint64
shmat(int id)
{
  struct proc *p = myproc();
  struct shmseg *seg;
  uint64 va = 0;

  acquire(&shmlock);
  seg = shm_find(id);
  if(seg == 0){
    release(&shmlock);
    return 0;
  }

  // 已映射时直接返回原地址，不重复计数。
  if(walkaddr(p->pagetable, seg->va) != 0){
    va = seg->va;
    release(&shmlock);
    return va;
  }

  for(int i = 0; i < seg->npages; i++){
    if(mappages(p->pagetable, seg->va + i*PGSIZE, PGSIZE,
                seg->pages[i], PTE_R|PTE_W|PTE_U|PTE_SHM) != 0){
      uvmunmap(p->pagetable, seg->va, i, 0);
      release(&shmlock);
      return 0;
    }
  }

  seg->ref++;
  va = seg->va;
  release(&shmlock);
  return va;
}

int
shmdt(int id)
{
  struct proc *p = myproc();
  struct shmseg *seg;

  acquire(&shmlock);
  seg = shm_find(id);
  if(seg == 0 || seg->ref <= 0 || walkaddr(p->pagetable, seg->va) == 0){
    release(&shmlock);
    return -1;
  }

  uvmunmap(p->pagetable, seg->va, seg->npages, 0);
  seg->ref--;
  release(&shmlock);
  return 0;
}

int
shmrm(int id)
{
  struct shmseg *seg;

  acquire(&shmlock);
  seg = shm_find(id);
  if(seg == 0 || seg->ref > 0){
    release(&shmlock);
    return -1;
  }

  for(int i = 0; i < seg->npages; i++)
    kfree((void*)seg->pages[i]);
  seg->used = 0;
  release(&shmlock);
  return 0;
}

void
shm_release_pagetable(pagetable_t pagetable)
{
  acquire(&shmlock);
  for(int i = 0; i < NSHM; i++){
    struct shmseg *seg = &shmsegs[i];
    pte_t *pte;

    if(!seg->used)
      continue;
    pte = walk(pagetable, seg->va, 0);
    if(pte && (*pte & PTE_SHM)){
      uvmunmap(pagetable, seg->va, seg->npages, 0);
      if(seg->ref > 0)
        seg->ref--;
    }
  }
  release(&shmlock);
}

void
shm_addref_pa(uint64 pa)
{
  acquire(&shmlock);
  for(int i = 0; i < NSHM; i++){
    struct shmseg *seg = &shmsegs[i];
    if(!seg->used)
      continue;
    for(int j = 0; j < seg->npages; j++){
      if(seg->pages[j] == pa){
        seg->ref++;
        release(&shmlock);
        return;
      }
    }
  }
  release(&shmlock);
}

void
shm_subref_pa(uint64 pa)
{
  acquire(&shmlock);
  for(int i = 0; i < NSHM; i++){
    struct shmseg *seg = &shmsegs[i];
    if(!seg->used)
      continue;
    for(int j = 0; j < seg->npages; j++){
      if(seg->pages[j] == pa){
        if(seg->ref > 0)
          seg->ref--;
        release(&shmlock);
        return;
      }
    }
  }
  release(&shmlock);
}

int
shm_selftest(void)
{
  acquire(&shmlock);
  for(int i = 0; i < NSHM; i++){
    struct shmseg *seg = &shmsegs[i];

    if(!seg->used)
      continue;
    if(seg->id <= 0 || seg->ref < 0 ||
       seg->npages <= 0 || seg->npages > SHM_MAX_PAGES ||
       seg->va < SHM_BASE || seg->va + seg->npages * PGSIZE > TRAPFRAME){
      release(&shmlock);
      return -1;
    }
  }
  release(&shmlock);
  return 0;
}

uint64
sys_shmget(void)
{
  int key, size;
  argint(0, &key);
  argint(1, &size);
  return shmget(key, size);
}

uint64
sys_shmat(void)
{
  int id;
  argint(0, &id);
  return shmat(id);
}

uint64
sys_shmdt(void)
{
  int id;
  argint(0, &id);
  return shmdt(id);
}

uint64
sys_shmrm(void)
{
  int id;
  argint(0, &id);
  return shmrm(id);
}

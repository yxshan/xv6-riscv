// System V 风格信号量。
//
// semget 创建/获取信号量集合，semop 原子执行 P/V，semctl 支持
// GETVAL / SETVAL / IPC_RMID。等待者按集合内每个信号量的地址睡眠。
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "sem.h"

struct xsem {
  int val;
  int waiters;
};

struct xsem_set {
  int used;
  int key;
  int nsems;
  struct spinlock lock;
  struct xsem sems[SEM_NSEMS_MAX];
};

static struct xsem_set sets[SEM_SET_MAX];
static struct spinlock semtable_lock;

void
seminit(void)
{
  initlock(&semtable_lock, "semtable");
  for(int i = 0; i < SEM_SET_MAX; i++)
    initlock(&sets[i].lock, "sem");
}

static struct xsem_set*
sem_find(int key)
{
  for(int i = 0; i < SEM_SET_MAX; i++){
    if(sets[i].used && sets[i].key == key)
      return &sets[i];
  }
  return 0;
}

uint64
sys_semget(void)
{
  int key, nsems, flags;

  argint(0, &key);
  argint(1, &nsems);
  argint(2, &flags);
  if(nsems <= 0 || nsems > SEM_NSEMS_MAX)
    return -1;

  acquire(&semtable_lock);
  struct xsem_set *s = sem_find(key);
  if(s){
    int id = s - sets;
    release(&semtable_lock);
    return id;
  }
  if((flags & IPC_CREAT) == 0 && key != IPC_PRIVATE){
    release(&semtable_lock);
    return -1;
  }
  for(int i = 0; i < SEM_SET_MAX; i++){
    if(!sets[i].used){
      sets[i].used = 1;
      sets[i].key = key;
      sets[i].nsems = nsems;
      for(int j = 0; j < nsems; j++){
        sets[i].sems[j].val = 0;
        sets[i].sems[j].waiters = 0;
      }
      release(&semtable_lock);
      return i;
    }
  }
  release(&semtable_lock);
  return -1;
}

uint64
sys_semop(void)
{
  struct xsem_set *s;
  struct sembuf ops[SEM_NSEMS_MAX];
  uint64 opsaddr;
  int semid, nsops;

  argint(0, &semid);
  argaddr(1, &opsaddr);
  argint(2, &nsops);
  if(semid < 0 || semid >= SEM_SET_MAX || nsops <= 0 ||
     nsops > SEM_NSEMS_MAX)
    return -1;
  if(copyin(myproc()->pagetable, (char*)ops, opsaddr,
            nsops * sizeof(struct sembuf)) < 0)
    return -1;

  s = &sets[semid];
  acquire(&s->lock);
  if(!s->used){
    release(&s->lock);
    return -1;
  }

  for(int i = 0; i < nsops; i++){
    int idx = ops[i].sem_num;
    struct xsem *x;

    if(idx < 0 || idx >= s->nsems){
      release(&s->lock);
      return -1;
    }
    x = &s->sems[idx];

    if(ops[i].sem_op < 0){
      int need = -ops[i].sem_op;
      while(x->val < need){
        if(ops[i].sem_flg & IPC_NOWAIT){
          release(&s->lock);
          return -1;
        }
        x->waiters++;
        sleep(&x->val, &s->lock);
        x->waiters--;
        if(killed(myproc())){
          release(&s->lock);
          return -1;
        }
      }
      x->val -= need;
    } else if(ops[i].sem_op > 0){
      x->val += ops[i].sem_op;
      wakeup(&x->val);
    } else {
      while(x->val != 0){
        if(ops[i].sem_flg & IPC_NOWAIT){
          release(&s->lock);
          return -1;
        }
        x->waiters++;
        sleep(&x->val, &s->lock);
        x->waiters--;
      }
    }
  }
  release(&s->lock);
  return 0;
}

uint64
sys_semctl(void)
{
  struct xsem_set *s;
  int semid, semnum, cmd, val;

  argint(0, &semid);
  argint(1, &semnum);
  argint(2, &cmd);
  argint(3, &val);
  if(semid < 0 || semid >= SEM_SET_MAX)
    return -1;

  s = &sets[semid];
  acquire(&s->lock);
  if(!s->used){
    release(&s->lock);
    return -1;
  }
  switch(cmd){
  case GETVAL:
    if(semnum < 0 || semnum >= s->nsems){
      release(&s->lock);
      return -1;
    }
    val = s->sems[semnum].val;
    release(&s->lock);
    return val;
  case SETVAL:
    if(semnum < 0 || semnum >= s->nsems || val < 0){
      release(&s->lock);
      return -1;
    }
    s->sems[semnum].val = val;
    wakeup(&s->sems[semnum].val);
    release(&s->lock);
    return 0;
  case IPC_RMID:
    s->used = 0;
    s->key = 0;
    s->nsems = 0;
    release(&s->lock);
    return 0;
  default:
    release(&s->lock);
    return -1;
  }
}

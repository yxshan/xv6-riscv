// 模块基础设施。
//
// 负责几件事：
// 1. 遍历 .kmods 链接段中的模块描述符，按优先级执行模块 init；
// 2. 提供 module_call 系统调用，把模块 ID 和命令分发给 .kmod_sys 段中的处理器。
// 3. 遍历 .kmod_hooks 链接段，向核心路径提供事件通知。
// 4. 提供基于 devsw[] 的内核伪设备注册接口。
// 5. 提供动态模块加载/卸载，支持运行时注册 module_call 处理器。
//
// 新增模块时只需要新建文件并注册描述符，不需要修改本文件。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "elf.h"
#include "module.h"
#include "dynmod.h"

extern struct kmod __kmods_start[], __kmods_end[];
extern struct sysmod __kmod_sys_start[], __kmod_sys_end[];
extern const struct kmod_hooks __kmod_hooks_start[], __kmod_hooks_end[];

static struct kmod *order[KMOD_MAX_MODULES];
static int nmods;
static int initialized;
static int hooks_ready;

static struct sysmod dynsys[KMOD_MAX_DYNAMIC];
static struct spinlock dynlock;
static int ndyn;
static int current_dynslot;

struct dynslot {
  int used;
  void (*exit)(void);
};
static struct dynslot dynslots[DYNMOD_NUM];

#define ET_EXEC 2
#define EM_RISCV 243

static void
sort_modules(void)
{
  // 使用简单插入排序，保证 priority 小的模块先初始化。
  for(int i = 1; i < nmods; i++){
    struct kmod *key = order[i];
    int j = i - 1;
    while(j >= 0 && order[j]->priority > key->priority){
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }
}

void
module_init_all(void)
{
  if(initialized)
    return;
  initialized = 1;

  initlock(&dynlock, "dynmod");

  for(struct kmod *m = __kmods_start; m < __kmods_end; m++){
    if(nmods >= KMOD_MAX_MODULES){
      printf("module_init_all: too many modules\n");
      break;
    }
    order[nmods++] = m;
  }

  sort_modules();

  for(int i = 0; i < nmods; i++){
    if(order[i]->init){
      int r = order[i]->init();
      if(r != 0)
        printf("module %s init failed: %d\n", order[i]->name, r);
    }
  }

  hooks_ready = 1;
}

uint64
module_dispatch(int id, int cmd, uint64 arg0, uint64 arg1)
{
  for(struct sysmod *m = __kmod_sys_start; m < __kmod_sys_end; m++){
    if(m->id == id){
      if(m->handle)
        return m->handle(cmd, arg0, arg1);
      return -1;
    }
  }
  for(int i = 0; i < ndyn; i++){
    if(dynsys[i].id == id){
      if(dynsys[i].handle)
        return dynsys[i].handle(cmd, arg0, arg1);
      return -1;
    }
  }
  return -1;
}

int
module_register(int id, const char *name, uint64 (*handler)(int, uint64, uint64))
{
  acquire(&dynlock);
  for(struct sysmod *m = __kmod_sys_start; m < __kmod_sys_end; m++){
    if(m->id == id){
      release(&dynlock);
      return -1;
    }
  }
  for(int i = 0; i < ndyn; i++){
    if(dynsys[i].id == id){
      release(&dynlock);
      return -1;
    }
  }
  if(ndyn >= KMOD_MAX_DYNAMIC){
    release(&dynlock);
    return -1;
  }
  dynsys[ndyn].id = id;
  dynsys[ndyn].name = name;
  dynsys[ndyn].handle = handler;
  dynsys[ndyn].slot = current_dynslot;
  ndyn++;
  release(&dynlock);
  return 0;
}

int
module_unregister(int id)
{
  acquire(&dynlock);
  for(int i = 0; i < ndyn; i++){
    if(dynsys[i].id == id){
      dynsys[i] = dynsys[ndyn - 1];
      ndyn--;
      release(&dynlock);
      return 0;
    }
  }
  release(&dynlock);
  return -1;
}

int
module_registry_check(void)
{
  int fail = 0;

  acquire(&dynlock);
  for(struct sysmod *m = __kmod_sys_start; m < __kmod_sys_end; m++){
    if(m->id <= 0 || m->name == 0 || m->handle == 0){
      fail = 1;
      goto out;
    }
    for(struct sysmod *n = m + 1; n < __kmod_sys_end; n++){
      if(n->id == m->id){
        fail = 1;
        goto out;
      }
    }
  }
  for(int i = 0; i < ndyn; i++){
    if(dynsys[i].id <= 0 || dynsys[i].name == 0 || dynsys[i].handle == 0){
      fail = 1;
      goto out;
    }
    for(int j = i + 1; j < ndyn; j++){
      if(dynsys[j].id == dynsys[i].id){
        fail = 1;
        goto out;
      }
    }
    for(struct sysmod *m = __kmod_sys_start; m < __kmod_sys_end; m++){
      if(m->id == dynsys[i].id){
        fail = 1;
        goto out;
      }
    }
  }
out:
  release(&dynlock);
  return fail ? -1 : 0;
}

int
module_exit_register(void (*exit_fn)(void))
{
  if(exit_fn == 0)
    return -1;
  if(current_dynslot <= 0 || current_dynslot > DYNMOD_NUM)
    return -1;
  acquire(&dynlock);
  if(dynslots[current_dynslot - 1].exit != 0){
    release(&dynlock);
    return -1;
  }
  dynslots[current_dynslot - 1].exit = exit_fn;
  release(&dynlock);
  return 0;
}

static void
dyn_remove_slot_locked(int slot)
{
  int j = 0;

  for(int i = 0; i < ndyn; i++){
    if(dynsys[i].slot != slot)
      dynsys[j++] = dynsys[i];
  }
  ndyn = j;
}

static int
module_load_elf(uint64 src, int len, uint64 base)
{
  struct elfhdr eh;
  struct proghdr ph;
  struct kmod_api api;
  uint64 entry;
  int r;

  if(len < sizeof(eh))
    return -1;
  if(copyin(myproc()->pagetable, (char*)&eh, src, sizeof(eh)) < 0)
    return -1;
  if(eh.magic != ELF_MAGIC || eh.type != ET_EXEC || eh.machine != EM_RISCV)
    return -1;
  if(eh.phnum <= 0)
    return -1;
  if(eh.phoff + (uint64)eh.phnum * eh.phentsize > (uint64)len)
    return -1;
  if(eh.entry < DYNMOD_BASE || eh.entry >= DYNMOD_BASE + DYNMOD_SIZE)
    return -1;

  for(int i = 0; i < eh.phnum; i++){
    uint64 phoff = eh.phoff + (uint64)i * eh.phentsize;

    if(phoff + sizeof(ph) > (uint64)len)
      return -1;
    if(copyin(myproc()->pagetable, (char*)&ph, src + phoff, sizeof(ph)) < 0)
      return -1;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.vaddr < DYNMOD_BASE ||
       ph.vaddr + ph.memsz < ph.vaddr ||
       ph.vaddr + ph.memsz > DYNMOD_BASE + DYNMOD_SIZE ||
       ph.filesz > ph.memsz)
      return -1;
    if(ph.off + ph.filesz > (uint64)len)
      return -1;
    if(ph.filesz > 0 &&
       copyin(myproc()->pagetable,
              (char*)(base + (ph.vaddr - DYNMOD_BASE)),
              src + ph.off, ph.filesz) < 0)
      return -1;
    if(ph.memsz > ph.filesz)
      memset((void*)(base + (ph.vaddr - DYNMOD_BASE) + ph.filesz), 0,
             ph.memsz - ph.filesz);
  }

  entry = base + (eh.entry - DYNMOD_BASE);
  api.printf = printf;
  api.module_register = module_register;
  api.module_unregister = module_unregister;
  api.module_exit_register = module_exit_register;
  api.proccount = proccount;
  api.freemem = freemem;
  api.ticks = ticks;

  r = ((int (*)(struct kmod_api*))entry)(&api);
  return r;
}

int
module_load(uint64 src, int len)
{
  int r, slot = -1;
  uint64 base;

  if(len <= 0 || len > DYNMOD_SIZE)
    return -1;
  for(int i = 0; i < DYNMOD_NUM; i++){
    if(!dynslots[i].used){
      slot = i;
      break;
    }
  }
  if(slot < 0)
    return -1;

  base = DYNMOD_BASE + (uint64)slot * DYNMOD_SIZE;
  memset((void*)base, 0, DYNMOD_SIZE);
  current_dynslot = slot + 1;
  if((r = module_load_elf(src, len, base)) < 0){
    acquire(&dynlock);
    dyn_remove_slot_locked(slot + 1);
    release(&dynlock);
    dynslots[slot].used = 0;
    dynslots[slot].exit = 0;
    current_dynslot = 0;
    memset((void*)base, 0, DYNMOD_SIZE);
    return -1;
  }

  dynslots[slot].used = 1;
  current_dynslot = 0;
  return slot;
}

int
module_unload(int slot)
{
  uint64 base;

  if(slot < 0 || slot >= DYNMOD_NUM || !dynslots[slot].used)
    return -1;

  base = DYNMOD_BASE + (uint64)slot * DYNMOD_SIZE;
  if(dynslots[slot].exit)
    dynslots[slot].exit();
  acquire(&dynlock);
  dyn_remove_slot_locked(slot + 1);
  release(&dynlock);
  dynslots[slot].used = 0;
  dynslots[slot].exit = 0;
  memset((void*)base, 0, DYNMOD_SIZE);
  return 0;
}

uint64
sys_module_call(void)
{
  int id, cmd;
  uint64 arg0, arg1;

  argint(0, &id);
  argint(1, &cmd);
  argaddr(2, &arg0);
  argaddr(3, &arg1);

  return module_dispatch(id, cmd, arg0, arg1);
}

uint64
sys_module_load(void)
{
  uint64 src;
  int len;

  argaddr(0, &src);
  argint(1, &len);
  return module_load(src, len);
}

uint64
sys_module_unload(void)
{
  int slot;

  argint(0, &slot);
  return module_unload(slot);
}

void
module_notify_tick(void)
{
  if(!hooks_ready)
    return;
  for(const struct kmod_hooks *h = __kmod_hooks_start; h < __kmod_hooks_end; h++){
    if(h->tick)
      h->tick();
  }
}

void
module_notify_syscall_enter(int num)
{
  if(!hooks_ready)
    return;
  for(const struct kmod_hooks *h = __kmod_hooks_start; h < __kmod_hooks_end; h++){
    if(h->syscall_enter)
      h->syscall_enter(num);
  }
}

void
module_notify_syscall_exit(int num, uint64 ret)
{
  if(!hooks_ready)
    return;
  for(const struct kmod_hooks *h = __kmod_hooks_start; h < __kmod_hooks_end; h++){
    if(h->syscall_exit)
      h->syscall_exit(num, ret);
  }
}

void
module_notify_proc_fork(struct proc *child)
{
  if(!hooks_ready)
    return;
  for(const struct kmod_hooks *h = __kmod_hooks_start; h < __kmod_hooks_end; h++){
    if(h->proc_fork)
      h->proc_fork(child);
  }
}

void
module_notify_proc_exit(struct proc *p)
{
  if(!hooks_ready)
    return;
  for(const struct kmod_hooks *h = __kmod_hooks_start; h < __kmod_hooks_end; h++){
    if(h->proc_exit)
      h->proc_exit(p);
  }
}

int
module_device_register(struct kmod_device *dev)
{
  if(dev == 0 || dev->major <= 0 || dev->major >= NDEV)
    return -1;
  if(devsw[dev->major].read || devsw[dev->major].write)
    return -1;
  devsw[dev->major].open = dev->open;
  devsw[dev->major].read = dev->read;
  devsw[dev->major].write = dev->write;
  devsw[dev->major].close = dev->close;
  return 0;
}

int
module_device_unregister(int major)
{
  if(major <= 0 || major >= NDEV)
    return -1;
  devsw[major].open = 0;
  devsw[major].read = 0;
  devsw[major].write = 0;
  devsw[major].close = 0;
  return 0;
}

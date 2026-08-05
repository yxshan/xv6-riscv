#ifndef MODULE_H
#define MODULE_H

#include "types.h"

#define KMOD_MAX_MODULES 64
#define KMOD_MAX_HOOKS 32
#define KMOD_MAX_SYSCOUNTS 64
#define KMOD_MAX_DYNAMIC 8
#define KMOD_PRIORITY_DEFAULT 100

struct proc;
struct file;

// 内核模块描述符。模块通过 KMOD_REGISTER 放入 .kmods 链接段，
// module_init_all() 会在文件系统就绪后按 priority 升序调用 init。
struct kmod {
  const char *name;
  int priority;
  int (*init)(void);
  int (*deinit)(void);
};

// module_call 系统调用的命令处理器。
// handle(cmd, arg0, arg1) 返回结果，失败返回 -1。
struct sysmod {
  int id;
  const char *name;
  uint64 (*handle)(int cmd, uint64 arg0, uint64 arg1);
  int slot; // 动态模块槽位，静态模块为 0
};

// 事件钩子集合。模块通过 KMOD_HOOKREG 注册到 .kmod_hooks 链接段。
// 钩子执行环境可能是进程上下文或中断上下文，模块不得在其中睡眠。
struct kmod_hooks {
  void (*tick)(void);
  void (*syscall_enter)(int num);
  void (*syscall_exit)(int num, uint64 ret);
  void (*proc_fork)(struct proc *child);
  void (*proc_exit)(struct proc *p);
};

// 内核伪设备注册描述符。复用 devsw[] 设备表。
struct kmod_device {
  int major;
  const char *name;
  int (*open)(struct file *f);
  int (*read)(struct file *f, int user_dst, uint64 dst, int n);
  int (*write)(struct file *f, int user_src, uint64 src, int n);
  int (*close)(struct file *f);
};

// 把模块描述符放到专用链接段，避免修改模块注册中心。
#define KMOD_REGISTER(modname, initfn, deinitfn, prio) \
  static struct kmod __kmod_##modname \
    __attribute__((used, section(".kmods"))) = { \
      #modname, prio, initfn, deinitfn \
    }

// 把命令处理器注册到 module_call 分发表。
#define KMOD_SYSREG(modid, modname, handler) \
  static struct sysmod __sysmod_##modname \
    __attribute__((used, section(".kmod_sys"))) = { \
      modid, #modname, handler \
    }

// 把事件钩子集合放入 .kmod_hooks 链接段。
#define KMOD_HOOKREG(modname, hooks) \
  static const struct kmod_hooks __hooks_##modname \
    __attribute__((used, section(".kmod_hooks"))) = hooks

void module_init_all(void);
uint64 module_dispatch(int id, int cmd, uint64 arg0, uint64 arg1);
uint64 sys_module_call(void);
void module_notify_tick(void);
void module_notify_syscall_enter(int num);
void module_notify_syscall_exit(int num, uint64 ret);
void module_notify_proc_fork(struct proc *child);
void module_notify_proc_exit(struct proc *p);
int module_device_register(struct kmod_device *dev);
int module_device_unregister(int major);
int module_register(int id, const char *name, uint64 (*handler)(int, uint64, uint64));
int module_unregister(int id);
int module_registry_check(void);
int module_load(uint64 src, int len);
int module_unload(int slot);

#endif

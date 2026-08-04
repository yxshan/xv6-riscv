#ifndef DYNMOD_H
#define DYNMOD_H

// 动态模块 ABI。
//
// 动态模块被链接到固定地址 DYNMOD_BASE，加载器把二进制复制到该地址后，
// 以 a0 传入 struct kmod_api * 调用 module_entry()。
// 模块代码只能通过 api 访问内核功能，不能直接链接内核符号。

typedef unsigned long kmod_u64;

typedef kmod_u64 (*kmod_handler_t)(int cmd, kmod_u64 arg0, kmod_u64 arg1);

struct kmod_api {
  int (*printf)(char *fmt, ...);
  int (*module_register)(int id, const char *name, kmod_handler_t handler);
  int (*module_unregister)(int id);
  int (*proccount)(void);
  kmod_u64 (*freemem)(void);
  kmod_u64 ticks;
};

#endif

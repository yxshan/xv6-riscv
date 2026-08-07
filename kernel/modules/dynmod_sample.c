// 动态模块示例。
//
// 它被链接为固定地址 DYNMOD_BASE 的 ELF 可执行文件 dynmod。
// 用户程序 modload 把文件读入内核，加载器调用 module_entry()。
// 模块通过 api 指针注册自己的 module_call 处理器。

#include "dynmod.h"
#include "module_ids.h"

static int (*gprintf)(char *fmt, ...);
static kmod_u64 gparam = 7;

static void
dyn_exit_fn(void)
{
  if(gprintf)
    gprintf("dynmod unloaded\n");
}

static kmod_u64
dyn_handler(int cmd, kmod_u64 arg0, kmod_u64 arg1)
{
  (void)arg0;
  (void)arg1;

  switch(cmd){
  case 1:
    return 0x1234;
  case 2:
    return 0x5678;
  case 3:
    return gparam;
  case 4:
    gparam = arg0;
    return gparam;
  default:
    return -1;
  }
}

__attribute__((section(".text.entry")))
int
module_entry(struct kmod_api *api)
{
  gprintf = api->printf;
  int r = api->module_register(KMOD_DYN_SAMPLE, "dynmod", dyn_handler);
  if(r == 0 && api->param_register("sample_value", &gparam) != 0)
    r = -1;
  if(r == 0 && api->module_exit_register(dyn_exit_fn) != 0)
    r = -1;
  if(r == 0)
    api->printf("dynmod loaded\n");
  return r;
}

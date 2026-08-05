// 第二个动态模块示例，用于验证多槽位加载与独立卸载。

#include "dynmod.h"
#include "module_ids.h"

static int (*gprintf2)(char *fmt, ...);

static void
dyn_two_exit(void)
{
  if(gprintf2)
    gprintf2("dynmod2 unloaded\n");
}

static kmod_u64
dyn_two_handler(int cmd, kmod_u64 arg0, kmod_u64 arg1)
{
  (void)arg0;
  (void)arg1;

  if(cmd == 1)
    return 0xABCD;
  return -1;
}

__attribute__((section(".text.entry")))
int
module_entry(struct kmod_api *api)
{
  gprintf2 = api->printf;
  int r = api->module_register(KMOD_DYN_TWO, "dynmod2", dyn_two_handler);
  if(r == 0 && api->module_exit_register(dyn_two_exit) != 0)
    r = -1;
  if(r == 0)
    api->printf("dynmod2 loaded\n");
  return r;
}

// 第二个动态模块示例，用于验证多槽位加载与独立卸载。

#include "dynmod.h"
#include "module_ids.h"

static int (*gprintf2)(char *fmt, ...);
static int (*grelease)(int id);
static kmod_u64 (*gproccount)(void);
static const char *gname = "dynmod2";

static void
dyn_two_exit(void)
{
  if(gprintf2)
    gprintf2("dynmod2 unloaded\n");
  if(grelease)
    grelease(KMOD_DYN_SAMPLE);
}

static kmod_u64
dyn_two_handler(int cmd, kmod_u64 arg0, kmod_u64 arg1)
{
  (void)arg0;
  (void)arg1;

  if(cmd == 1)
    return 0xABCD;
  if(cmd == 2)
    return (kmod_u64)gname[0];
  if(cmd == 3 && gproccount)
    return gproccount();
  return -1;
}

__attribute__((section(".text.entry")))
int
module_entry(struct kmod_api *api)
{
  gprintf2 = api->printf;
  grelease = api->module_release;
  kmod_u64 sym = api->lookup_symbol("proccount");
  gproccount = (kmod_u64 (*)(void))sym;
  int r = api->module_require(KMOD_DYN_SAMPLE);
  if(r == 0)
    r = api->module_register(KMOD_DYN_TWO, "dynmod2", dyn_two_handler);
  if(r == 0 && api->module_exit_register(dyn_two_exit) != 0)
    r = -1;
  if(r == 0)
    api->printf("dynmod2 loaded\n");
  return r;
}

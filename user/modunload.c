// modunload：卸载当前动态模块。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int r = module_unload();
  printf("module_unload = %d\n", r);
  exit(0);
}

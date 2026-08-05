// modunload：卸载指定槽位的动态模块，默认槽位 0。

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int slot = 0;

  if(argc > 1)
    slot = atoi(argv[1]);
  int r = module_unload(slot);
  printf("module_unload = %d\n", r);
  exit(0);
}

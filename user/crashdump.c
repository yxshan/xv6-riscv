// crashdump：主动输出内核崩溃转储信息。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int r = dumpstate();
  printf("dumpstate = %d\n", r);
  exit(0);
}

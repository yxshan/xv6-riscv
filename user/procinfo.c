// procinfo：打印进程列表，相当于最小版 /proc。

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/module/module_ids.h"

int
main(void)
{
  char buf[1024];
  int n = (int)module_call(KMOD_SYSINFO, SYSINFO_CMD_DUMP, (uint64)buf, sizeof(buf));

  if(n < 0){
    printf("procinfo: dump failed\n");
    exit(1);
  }
  if(n > 0)
    write(1, buf, n);
  exit(0);
}

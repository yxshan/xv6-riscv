// procinfo：打印进程列表，相当于最小版 /proc。

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/module/module_ids.h"

int
main(void)
{
  char *buf = malloc(4096);
  int n = (int)module_call(KMOD_SYSINFO, SYSINFO_CMD_DUMP, (uint64)buf, 4096);

  if(buf == 0){
    printf("procinfo: malloc failed\n");
    exit(1);
  }
  if(n < 0){
    printf("procinfo: dump failed\n");
    free(buf);
    exit(1);
  }
  if(n > 0)
    write(1, buf, n);
  free(buf);
  exit(0);
}

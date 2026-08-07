// init：第一个用户级进程，也是所有孤儿进程的“父进程”。
// 它负责确保控制台设备存在并挂到标准输入输出，然后启动 shell；
// 若 shell 退出，init 会重新创建它，保证系统始终有 shell 可用。

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/module/module_ids.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", CONSOLE, 0);
    open("console", O_RDWR);
  }
  // 创建模块伪设备节点；若镜像中已存在则忽略失败。
  mknod("sysinfo", KMOD_SYSINFO_MAJOR, 0);
  mknod("stats", KMOD_TRACE_MAJOR, 0);
  mknod("proc", KMOD_PROC_MAJOR, 0);
  mknod("zero", ZERO_DEV, 0);
  mknod("null", NULL_DEV, 0);
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}

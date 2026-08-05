// id：打印当前进程的用户与组凭证。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  printf("uid=%d gid=%d euid=%d egid=%d\n",
         getuid(), getgid(), geteuid(), getegid());
  exit(0);
}

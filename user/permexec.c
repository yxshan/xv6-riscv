// permexec：供权限测试使用的可执行文件，成功执行后以 42 退出。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  exit(42);
}

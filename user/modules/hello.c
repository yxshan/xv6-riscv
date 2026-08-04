// 示例用户模块。
// 只要放入 user/modules/，Makefile 会自动生成 user/_hello 并加入 fs.img。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  printf("hello from user module\n");
  exit(0);
}

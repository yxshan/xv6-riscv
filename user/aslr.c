// aslr：打印当前用户栈地址，用于验证 ASLR 随机化。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  uint64 sp;

  asm volatile("mv %0, sp" : "=r"(sp));
  printf("aslr stack %lx\n", sp);
  exit(0);
}

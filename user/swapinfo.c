// swapinfo：查看交换空间统计。
//
// 用法：
//   swapinfo

#include "kernel/types.h"
#include "kernel/swap.h"
#include "user/user.h"

int
main(void)
{
  struct swapinfo si;

  if(swapinfo(&si) < 0){
    printf("swapinfo: failed\n");
    exit(1);
  }
  printf("swap total %d free %d swapouts %d swapins %d\n",
         (int)si.total_pages, (int)si.free_pages,
         (int)si.swapouts, (int)si.swapins);
  exit(0);
}

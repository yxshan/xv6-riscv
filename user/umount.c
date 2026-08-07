// umount：卸载指定挂载点。
//
// 用法：
//   umount <mountpoint>

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int r;

  if(argc != 2){
    printf("usage: umount <mountpoint>\n");
    exit(1);
  }

  r = umount(argv[1]);
  printf("umount(%s) = %d\n", argv[1], r);
  exit(r == 0 ? 0 : 1);
}

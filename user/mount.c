// mount：把指定设备的根文件系统挂载到目录。
//
// 用法：
//   mount <dev> <mountpoint>

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int dev, r;

  if(argc != 3){
    printf("usage: mount <dev> <mountpoint>\n");
    exit(1);
  }

  dev = atoi(argv[1]);
  r = mount(dev, argv[2]);
  printf("mount(%d, %s) = %d\n", dev, argv[2], r);
  exit(r == 0 ? 0 : 1);
}

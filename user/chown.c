// chown：修改文件所有者与所属组。

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 4){
    fprintf(2, "Usage: chown <uid> <gid> <file>\n");
    exit(1);
  }
  if(chown(argv[3], atoi(argv[1]), atoi(argv[2])) < 0){
    fprintf(2, "chown: %s failed\n", argv[3]);
    exit(1);
  }
  exit(0);
}

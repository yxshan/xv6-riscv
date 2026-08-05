// chmod：修改文件权限位，mode 使用八进制写法。

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int
octval(char *s)
{
  int n = 0;

  for(; *s; s++){
    if(*s < '0' || *s > '7')
      return -1;
    n = n * 8 + (*s - '0');
  }
  return n;
}

int
main(int argc, char *argv[])
{
  int mode;

  if(argc != 3){
    fprintf(2, "Usage: chmod <octal-mode> <file>\n");
    exit(1);
  }
  mode = octval(argv[1]);
  if(mode < 0 || mode > PERM_MASK){
    fprintf(2, "chmod: invalid mode %s\n", argv[1]);
    exit(1);
  }
  if(chmod(argv[2], mode) < 0){
    fprintf(2, "chmod: %s failed\n", argv[2]);
    exit(1);
  }
  exit(0);
}

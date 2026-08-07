#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_SYMLINK 4   // Symbolic link
#define T_FIFO    5   // Named pipe

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  ushort mode; // Permission bits (rwxrwxrwx)
  ushort uid;  // Owner user ID
  ushort gid;  // Owner group ID
  uint64 size; // Size of file in bytes
};

#ifndef S_IRUSR
#define S_IRUSR 0400
#endif
#ifndef S_IWUSR
#define S_IWUSR 0200
#endif
#ifndef S_IXUSR
#define S_IXUSR 0100
#endif
#ifndef S_IRGRP
#define S_IRGRP 0040
#endif
#ifndef S_IWGRP
#define S_IWGRP 0020
#endif
#ifndef S_IXGRP
#define S_IXGRP 0010
#endif
#ifndef S_IROTH
#define S_IROTH 0004
#endif
#ifndef S_IWOTH
#define S_IWOTH 0002
#endif
#ifndef S_IXOTH
#define S_IXOTH 0001
#endif
#define PERM_MASK 0777

// wait/waitpid 状态编码：低 8 位为退出码；0x7f 表示子进程已停止。
#define XV6_WSTOPPED 0x7f
#define XV6_WIFSTOPPED(x) ((x) == XV6_WSTOPPED)
#define XV6_WCONTINUED 0xffff
#define XV6_WIFCONTINUED(x) ((x) == XV6_WCONTINUED)

#define WUNTRACED 1
#define WCONTINUED 2
#define WNOHANG 4

// 用户进程的虚拟内存区域（VMA）描述。
//
// mmap 映射通过固定数组挂在 struct proc 上；文件映射持有 inode 引用，
// 缺页时从文件按需读取，私有写页在物理内存中独立保存。
#ifndef XV6_VMA_H
#define XV6_VMA_H

#include "types.h"

struct inode;

#define NVMA 16
#define VMA_PAGE_COUNT 256

struct vma {
  int used;
  uint64 start;    // 页对齐起始地址
  uint64 end;      // 页对齐结束地址
  int prot;        // PROT_READ / PROT_WRITE / PROT_EXEC
  int flags;       // MAP_PRIVATE / MAP_ANONYMOUS
  struct inode *ip; // 文件映射的 inode，匿名映射为 0
  uint off;        // 文件内起始偏移
};

#endif

// 用户态测试套件公共头文件。
//
// usertests 由 user/tests 下多个测试文件组成：
// 每个文件按子系统提供 quick/slow 测试数组，驱动文件统一执行。
#ifndef XV6_TESTS_H
#define XV6_TESTS_H

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

#define BUFSZ ((MAXOPBLOCKS+2)*BSIZE)

extern char buf[BUFSZ];

struct test {
  void (*f)(char *);
  char *s;
};

extern struct test syscall_quicktests[];
extern struct test syscall_slowtests[];
extern struct test mem_quicktests[];
extern struct test mem_slowtests[];
extern struct test fs_quicktests[];
extern struct test fs_slowtests[];
extern struct test proc_quicktests[];
extern struct test proc_slowtests[];
extern struct test module_quicktests[];
extern struct test module_slowtests[];
extern struct test perm_quicktests[];
extern struct test perm_slowtests[];
extern struct test mmap_quicktests[];
extern struct test mmap_slowtests[];
extern struct test thread_quicktests[];
extern struct test thread_slowtests[];

#endif

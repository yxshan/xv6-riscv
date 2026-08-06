#ifndef SYSCALL_NAMES_H
#define SYSCALL_NAMES_H

#include "syscall.h"

// 系统调用编号到名称的共享宏表。
// 内核 trace 模块和用户态 strace 都通过它生成名称数组。
#define SYSCALL_NAME_TABLE(X) \
  X(SYS_fork, fork) \
  X(SYS_exit, exit) \
  X(SYS_wait, wait) \
  X(SYS_pipe, pipe) \
  X(SYS_read, read) \
  X(SYS_kill, kill) \
  X(SYS_exec, exec) \
  X(SYS_fstat, fstat) \
  X(SYS_chdir, chdir) \
  X(SYS_dup, dup) \
  X(SYS_getpid, getpid) \
  X(SYS_sbrk, sbrk) \
  X(SYS_pause, pause) \
  X(SYS_uptime, uptime) \
  X(SYS_open, open) \
  X(SYS_write, write) \
  X(SYS_mknod, mknod) \
  X(SYS_unlink, unlink) \
  X(SYS_link, link) \
  X(SYS_mkdir, mkdir) \
  X(SYS_close, close) \
  X(SYS_module_call, module_call) \
  X(SYS_module_load, module_load) \
  X(SYS_module_unload, module_unload) \
  X(SYS_setpriority, setpriority) \
  X(SYS_symlink, symlink) \
  X(SYS_mkfifo, mkfifo) \
  X(SYS_dumpstate, dumpstate) \
  X(SYS_shmget, shmget) \
  X(SYS_shmat, shmat) \
  X(SYS_shmdt, shmdt) \
  X(SYS_shmrm, shmrm) \
  X(SYS_signal, signal) \
  X(SYS_sigkill, sigkill) \
  X(SYS_sigreturn, sigreturn) \
  X(SYS_chmod, chmod) \
  X(SYS_chown, chown) \
  X(SYS_getuid, getuid) \
  X(SYS_geteuid, geteuid) \
  X(SYS_getgid, getgid) \
  X(SYS_getegid, getegid) \
  X(SYS_setuid, setuid) \
  X(SYS_setgid, setgid) \
  X(SYS_umask, umask) \
  X(SYS_mmap, mmap) \
  X(SYS_munmap, munmap) \
  X(SYS_mount, mount) \
  X(SYS_umount, umount) \
  X(SYS_swapout, swapout) \
  X(SYS_swapinfo, swapinfo) \
  X(SYS_clone, clone) \
  X(SYS_futex_wait, futex_wait) \
  X(SYS_futex_wake, futex_wake) \
  X(SYS_gettid, gettid) \
  X(SYS_waitpid, waitpid)

#endif

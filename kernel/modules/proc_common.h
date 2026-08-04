#ifndef PROC_COMMON_H
#define PROC_COMMON_H

#include "types.h"

int kfmt_uint(char *buf, int max, uint64 x);
int kappend_str(char *buf, int max, int off, const char *s);
int kbuild_sysinfo(char *buf, int max);
int kbuild_proclist(char *buf, int max);
int kbuild_proc_all(char *buf, int max);

#endif

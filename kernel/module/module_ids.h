#ifndef MODULE_IDS_H
#define MODULE_IDS_H

// 模块 ID 集中登记，避免冲突。
#define KMOD_SYSINFO 1
#define KMOD_TRACE   2
#define KMOD_DYN_SAMPLE 3
#define KMOD_SELFTEST 4

// sysinfo 设备主设备号（预留）。
#define KMOD_SYSINFO_MAJOR 2
#define KMOD_TRACE_MAJOR   3
#define KMOD_PROC_MAJOR    4

// sysinfo 模块命令。
#define SYSINFO_CMD_PROC  1
#define SYSINFO_CMD_MEM   2
#define SYSINFO_CMD_TICKS 3
#define SYSINFO_CMD_DUMP  4

// trace 模块命令。
#define TRACE_CMD_TOTAL  1
#define TRACE_CMD_SYS    2
#define TRACE_CMD_RESET  3
#define TRACE_CMD_FORKS  4
#define TRACE_CMD_EXITS  5
#define TRACE_CMD_TICKS  6

// selftest 模块命令。
#define SELFTEST_CMD_BASIC 1
#define SELFTEST_CMD_REGISTRY 2
#define SELFTEST_CMD_SIGNAL 3
#define SELFTEST_CMD_MEMORY 4

#endif

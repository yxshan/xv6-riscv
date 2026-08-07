// 内核符号导出表。
//
// 动态模块可以通过 kmod_api.lookup_symbol() 按名称解析内核函数，
// 避免把所有能力都塞进 API 结构体。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "module.h"
#include "symbol.h"

KMOD_EXPORT(printf);
KMOD_EXPORT(proccount);
KMOD_EXPORT(freemem);
KMOD_EXPORT(module_register);
KMOD_EXPORT(module_unregister);
KMOD_EXPORT(ticks);

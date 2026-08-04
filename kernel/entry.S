        # qemu -kernel 会把内核加载到 0x80000000，
        # 并让每个 hart（CPU）从该地址开始执行。
        # kernel.ld 保证下面的代码被放在 0x80000000。
        # 此时系统还处于机器模式(M-mode)，尚未建立 C 语言运行环境，
        # 所以先用汇编给每个 CPU 分配独立的内核栈。
.section .text
.global _entry
_entry:
        # 为 C 代码准备栈。
        # stack0 在 start.c 中声明，每个 CPU 使用 4096 字节。
        # sp = stack0 + ((hartid + 1) * 4096)
        la sp, stack0
        li a0, 1024*4
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
        # 跳转到 start.c 的 start()，由它完成特权级切换
        call start
spin:
        # 正常情况下不会执行到这里；若执行则说明内核出错
        j spin

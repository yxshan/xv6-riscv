        #
        # 用户态陷阱的低层跳板代码。
        # 它负责“用户态陷入内核”和“内核返回用户态”的寄存器切换。
        #
        # 内核把这段代码所在页面同时映射到用户和内核地址空间的
        # 同一个虚拟地址 TRAMPOLINE，因此在切换页表的过程中，
        # 当前正在执行的指令不会因为地址失效而崩溃。
        # kernel.ld 保证这段代码起始于页边界。
        #

#include "riscv.h"
#include "memlayout.h"

.section trampsec
.globl trampoline
.globl usertrap
trampoline:
.align 4
.globl uservec
uservec:    
	#
        # 用户态发生陷阱后，硬件切换到 S-mode 并跳转到 uservec。
        # 此时还在使用用户的页表，也没有切换内核栈。
        #

        # 先把用户 a0 暂存到 sscratch，腾出 a0 访问 TRAPFRAME。
        csrw sscratch, a0

        # 每个进程有自己的 trapframe 物理页，
        # 但都被映射到用户页表的同一虚拟地址 TRAPFRAME。
        li a0, TRAPFRAME
        
        # 把用户寄存器现场保存进 TRAPFRAME。
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# 把用户 a0 从 sscratch 取回并存入 trapframe。
        csrr t0, sscratch
        sd t0, 112(a0)

        # 从 trapframe 载入内核栈指针，切换到内核栈。
        ld sp, 8(a0)

        # tp 保存当前 hartid，供 cpuid() 使用。
        ld tp, 32(a0)

        # 载入 C 陷阱处理函数 usertrap() 的地址。
        ld t0, 16(a0)

        # 从 trapframe 取出内核页表基址。
        ld t1, 0(a0)

        # 确保之前的访存操作已经使用用户页表完成。
        sfence.vma zero, zero

        # 切换为内核页表。
        csrw satp, t1

        # 冲刷 TLB 中的用户页表残留项。
        sfence.vma zero, zero

        # 跳转到 usertrap() 开始 C 代码处理。
        jalr t0

.globl userret
userret:
        # usertrap() 返回到这里，a0 携带用户页表的 satp。
        # 这里执行“从内核返回用户态”的最后几步。

        # 切回用户页表并冲刷 TLB。
        sfence.vma zero, zero
        csrw satp, a0
        sfence.vma zero, zero

        li a0, TRAPFRAME

        # 从 TRAPFRAME 恢复除 a0 外的用户寄存器。
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# 最后恢复用户 a0（其中可能是系统调用返回值）。
        ld a0, 112(a0)
        
        # sret 返回用户态，并跳转到用户 PC。
        # sstatus/sepc 已由 usertrapret() 设置好。
        sret

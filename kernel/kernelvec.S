        #
        # S-mode（内核态）运行期间发生异常或中断时，
        # 硬件根据 stvec 跳转到这里的 kernelvec。
        #
        # 此时已经运行在内核栈上，所以只需把 caller-saved 寄存器压栈，
        # 调用 C 处理函数 kerneltrap()；返回后恢复寄存器并 sret，
        # 接着执行原来被打断的内核代码。
        #
.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
        # 在栈上留出 256 字节空间保存寄存器。
        addi sp, sp, -256

        # 保存 caller-saved 寄存器，防止 C 代码修改它们。
        sd ra, 0(sp)
        # sd sp, 8(sp)
        sd gp, 16(sp)
        sd tp, 24(sp)
        sd t0, 32(sp)
        sd t1, 40(sp)
        sd t2, 48(sp)
        sd a0, 72(sp)
        sd a1, 80(sp)
        sd a2, 88(sp)
        sd a3, 96(sp)
        sd a4, 104(sp)
        sd a5, 112(sp)
        sd a6, 120(sp)
        sd a7, 128(sp)
        sd t3, 216(sp)
        sd t4, 224(sp)
        sd t5, 232(sp)
        sd t6, 240(sp)

        # 调用 trap.c 中的 C 陷阱处理函数。
        call kerneltrap

        # 恢复寄存器。sp 不用恢复（栈本身没变），
        # tp 也不用恢复，因为它保存的是 hartid。
        ld ra, 0(sp)
        # ld sp, 8(sp)
        ld gp, 16(sp)
        # not tp (contains hartid), in case we moved CPUs
        ld t0, 32(sp)
        ld t1, 40(sp)
        ld t2, 48(sp)
        ld a0, 72(sp)
        ld a1, 80(sp)
        ld a2, 88(sp)
        ld a3, 96(sp)
        ld a4, 104(sp)
        ld a5, 112(sp)
        ld a6, 120(sp)
        ld a7, 128(sp)
        ld t3, 216(sp)
        ld t4, 224(sp)
        ld t5, 232(sp)
        ld t6, 240(sp)

        addi sp, sp, 256

        # 返回被中断的内核代码继续执行。
        sret

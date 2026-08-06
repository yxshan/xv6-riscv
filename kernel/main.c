// main() 是 S-mode 下内核的 C 入口。
// CPU 0 负责初始化所有内核子系统，然后启动第一个用户进程；
// 其他 CPU 等 CPU 0 完成初始化后，再各自配置页表和陷阱向量。
// 所有 CPU 最终都会进入 scheduler()，开始进程调度循环。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // 物理页分配器：管理内核末尾到 PHYSTOP 的物理内存
    kvminit();       // 创建内核页表，直接映射内核文本、数据和设备寄存器
    kvminithart();   // 把内核页表写入 satp，开启分页
    procinit();      // 初始化进程表与每个进程的内核栈
    trapinit();      // 设置用户态陷阱向量
    trapinithart();  // 安装当前 CPU 的内核陷阱向量
    plicinit();      // 配置 PLIC 中断控制器
    plicinithart();  // 允许当前 CPU 接收设备中断
    binit();         // 初始化块缓存，所有磁盘块读写先经过缓存
    iinit();         // 初始化 inode 缓存表
    mountinit();     // 初始化 VFS 挂载表锁
    fileinit();      // 初始化打开文件表
    virtio_disk_init(); // 初始化 QEMU 模拟的 virtio 磁盘
    userinit();      // 创建第一个用户进程 init
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // 非 CPU 0 也要开启分页并安装陷阱向量
    trapinithart();   // 安装内核陷阱向量
    plicinithart();   // 允许当前 CPU 接收设备中断
  }

  scheduler();        // 进入永不返回的调度器循环
}

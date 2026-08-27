#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"

// CPU 0 完成共享初始化后，将started设为1通知其他CPU
volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if (cpuid == 0)
    {
        // 只由CPU 0完成一次的内核共享初始化
        print_init();
        pmem_init();
        kvm_init();
        trap_kernel_init();

        printf("cpu %d is booting!\n", cpuid);

        // 先保证上述初始化对其他CPU可见，再发布started
        __sync_synchronize();
        started = 1;
    }
    else
    {
        // CPU 1等待CPU 0完成共享资源初始化
        while (started == 0)
            ;

        // 读取started后重新同步，确保看到CPU 0初始化后的内存状态
        __sync_synchronize();
    }

    // 每个hart都需要切换到内核页表
    kvm_inithart();

    // 每个hart都设置自己的PLIC、stvec并打开S-mode全局中断
    trap_kernel_inithart();

    // CPU 0已经输出过启动信息，CPU 1在完成自身初始化后再输出
    if (cpuid != 0)
        printf("cpu %d is booting!\n", cpuid);

    // 当前没有调度器和进程，内核保持运行以等待中断
    while (1)
        ;
}
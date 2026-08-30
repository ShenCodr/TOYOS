#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
#include "proc/mod.h"

// CPU 0 完成共享初始化后，将started设为1通知其他CPU
volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if (cpuid == 0) {
        // CPU 0 完成共享资源和首进程初始化
        print_init();
        printf("cpu %d is booting!\n", cpuid);

        pmem_init();
        kvm_init();
        kvm_inithart();

        mmap_init();
        proc_init();
        proc_make_first();

        trap_kernel_init();
        trap_kernel_inithart();

        // 首进程准备完成后再唤醒其他 CPU
        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0)
            ;

        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);

        kvm_inithart();
        trap_kernel_inithart();
    }

    // 每个 CPU 都进入自己的调度循环
    proc_scheduler();

    panic("main: never back");
    return 0;
}

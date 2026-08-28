#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if (cpuid == 0)
    {
        // CPU 0 初始化 UART 和打印锁
        print_init();

        // CPU 0 初始化物理页分配器
        pmem_init();

        // CPU 0 创建共享的内核页表
        kvm_init();

        // CPU 0 启用内核页表
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);

        // 发布初始化完成状态
        __sync_synchronize();
        started = 1;
    }
    else
    {
        // CPU 1 等待 CPU 0 完成共享资源初始化
        while (started == 0);

        __sync_synchronize();

        // CPU 1 启用 CPU 0 创建的内核页表
        kvm_inithart();

        printf("cpu %d is booting!\n", cpuid);
    }

    while (1);
}
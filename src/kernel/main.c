#include "arch/mod.h"
#include "lib/mod.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if (cpuid == 0)//cpu0初始化UART和打印锁
    {
        print_init();

        printf("cpu %d is booting!\n", cpuid);

        __sync_synchronize();
        started = 1;
    }
    else
    {
        while (started == 0);

        __sync_synchronize();

        printf("cpu %d is booting!\n", cpuid);
    }

    while (1);
}
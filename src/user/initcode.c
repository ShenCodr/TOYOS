#include "sys.h"

int main()
{
    int values[5];
    char *message = "hello, world";

    // 先从内核取得数组，再把数组和字符串传回内核检查
    syscall(SYS_copyout, values);
    syscall(SYS_copyin, values, 5);
    syscall(SYS_copyinstr, message);

    while (1)
        ;

    return 0;
}
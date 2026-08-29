#include "sys.h"

#define PGSIZE 4096

int main()
{
    char tmp[PGSIZE * 4];

    // 先访问数组高偏移位置，触发较近的栈缺页
    tmp[PGSIZE * 3] = 'h';
    tmp[PGSIZE * 3 + 1] = 'e';
    tmp[PGSIZE * 3 + 2] = 'l';
    tmp[PGSIZE * 3 + 3] = 'l';
    tmp[PGSIZE * 3 + 4] = 'o';
    tmp[PGSIZE * 3 + 5] = '\0';

    syscall(SYS_copyinstr, tmp + PGSIZE * 3);

    // 再访问数组最低地址，触发一次跨多页的栈扩展
    tmp[0] = 'w';
    tmp[1] = 'o';
    tmp[2] = 'r';
    tmp[3] = 'l';
    tmp[4] = 'd';
    tmp[5] = '\0';

    syscall(SYS_copyinstr, tmp);

    while (1)
        ;

    return 0;
}
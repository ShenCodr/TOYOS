#include "mod.h"

/*
    测试: 从用户空间传入一个int类型的数组
    uint64 addr 数组起始地址
    uint32 len  元素数量
    成功返回0
*/
uint64 sys_copyin()
{
    proc_t *p = myproc();
    uint64 addr;
    uint32 len;
    int value;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    // 逐个读取用户数组元素，避免在内核栈上创建不受控的数组
    for (uint32 i = 0; i < len; i++)
    {
        uvm_copyin(p->pgtbl,
                   (uint64)&value,
                   addr + i * sizeof(int),
                   sizeof(int));
        printf("get a number from user: %d\n", value);
    }

    return 0;
}
/*
    测试: 向用户空间传出一个int类型的数组
    uint64 addr 数组起始地址
    成功返回拷贝的元素数量
*/
uint64 sys_copyout()
{
    proc_t *p = myproc();
    uint64 addr;
    int values[5] = {1, 2, 3, 4, 5};

    arg_uint64(0, &addr);

    uvm_copyout(p->pgtbl,
                addr,
                (uint64)values,
                sizeof(values));

    return 5;
}
/*
    测试: 从用户空间传入一个字符串
    uint64 addr 字符串起始地址
    成功返回0
*/
uint64 sys_copyinstr()
{
    char buf[STR_MAXLEN];

    arg_str(0, buf, STR_MAXLEN);
    printf("get string for user: %s\n", buf);

    return 0;
}

/*
    用户堆空间伸缩
    uint64 new_heap_top (如果是0, 代表查询当前堆顶位置)
    成功返回new_heap_top, 失败返回-1
*/
uint64 sys_brk()
{
    proc_t *p = myproc();
    uint64 new_heap_top;
    uint64 ret_heap_top;
    uint64 change_len;

    arg_uint64(0, &new_heap_top);

    if (new_heap_top == 0)
    {
        // 参数为 0 时只查询当前堆顶
        ret_heap_top = p->heap_top;
        printf("look event: ret_heap_top = %p\n", ret_heap_top);
    }
    else if (new_heap_top > p->heap_top)
    {
        change_len = new_heap_top - p->heap_top;
        if (change_len > 0xfffffffful)
            return (uint64)-1;

        ret_heap_top = uvm_heap_grow(p->pgtbl,
                                     p->heap_top,
                                     (uint32)change_len);
        if (ret_heap_top == (uint64)-1)
            return (uint64)-1;

        p->heap_top = ret_heap_top;
        printf("grow event: ret_heap_top = %p\n", ret_heap_top);
    }
    else if (new_heap_top < p->heap_top)
    {
        change_len = p->heap_top - new_heap_top;
        if (change_len > 0xfffffffful)
            return (uint64)-1;

        ret_heap_top = uvm_heap_ungrow(p->pgtbl,
                                       p->heap_top,
                                       (uint32)change_len);
        if (ret_heap_top == (uint64)-1)
            return (uint64)-1;

        p->heap_top = ret_heap_top;
        printf("ungrow event: ret_heap_top = %p\n", ret_heap_top);
    }
    else
    {
        ret_heap_top = p->heap_top;
        printf("equal event: ret_heap_top = %p\n", ret_heap_top);
    }

    // 临时显示页表，用于观察堆页面的增加和释放
    vm_print(p->pgtbl);
    printf("\n");

    return ret_heap_top;
}
/*
    增加一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节,需检查是否是page-aligned)
    成功返回映射空间的起始地址, 失败返回-1
*/
uint64 sys_mmap()
{
    return 0;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    return 0;
}
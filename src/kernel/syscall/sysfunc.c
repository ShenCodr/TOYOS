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
    proc_t *p = myproc();
    mmap_region_t *tmp;
    uint64 start;
    uint64 mapped_begin;
    uint32 len;

    arg_uint64(0, &start);
    arg_uint32(1, &len);

    if (len == 0 || len % PGSIZE != 0)
        return (uint64)-1;

    if (start != 0)
    {
        if (start % PGSIZE != 0)
            return (uint64)-1;
        if (start < MMAP_BEGIN || start >= MMAP_END)
            return (uint64)-1;
        if ((uint64)len > MMAP_END - start)
            return (uint64)-1;

        mapped_begin = start;
    }
    else
    {
        // 与 uvm_mmap_find 保持相同的 first-fit 规则
        mapped_begin = MMAP_BEGIN;
        tmp = p->mmap;

        while (tmp != NULL)
        {
            if (mapped_begin <= tmp->begin &&
                (uint64)len <= tmp->begin - mapped_begin)
                break;

            mapped_begin = tmp->begin +
                           (uint64)tmp->npages * PGSIZE;
            tmp = tmp->next;
        }

        if (mapped_begin > MMAP_END ||
            (uint64)len > MMAP_END - mapped_begin)
            return (uint64)-1;
    }

    uvm_mmap(start,
             len / PGSIZE,
             PTE_R | PTE_W | PTE_U);

    // Task 4 提示性输出
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

    return mapped_begin;
}

/*
    解除一段内存映射
    uint64 start 起始地址
    uint32 len   范围 (字节, 需检查是否是page-aligned)
    成功返回0 失败返回-1
*/
uint64 sys_munmap()
{
    proc_t *p = myproc();
    uint64 start;
    uint32 len;

    arg_uint64(0, &start);
    arg_uint32(1, &len);

    if (len == 0 || len % PGSIZE != 0)
        return (uint64)-1;

    if (start % PGSIZE != 0)
        return (uint64)-1;
    if (start < MMAP_BEGIN || start >= MMAP_END)
        return (uint64)-1;
    if ((uint64)len > MMAP_END - start)
        return (uint64)-1;

    uvm_munmap(start, len / PGSIZE);

    // Task 4 提示性输出
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

    return 0;
}
// 打印用户态字符串
uint64 sys_print_str()
{
    char str[STR_MAXLEN];

    arg_str(0, str, STR_MAXLEN);
    printf("%s", str);

    return 0;
}

// 打印一个 32 位整数
uint64 sys_print_int()
{
    uint32 num;

    arg_uint32(0, &num);
    printf("%d\n", (int)num);

    return 0;
}

// 获取当前进程 PID
uint64 sys_getpid()
{
    proc_t *p = myproc();
    return p->pid;
}

// 复制当前进程，父进程获得子进程 PID。
uint64 sys_fork()
{
    return proc_fork();
}

#include "mod.h"

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

// 等待一个子进程退出，可选地传回退出状态
uint64 sys_wait()
{
    uint64 user_addr;

    arg_uint64(0, &user_addr);
    return proc_wait(user_addr);
}

// 退出当前进程，退出码由父进程 wait 获取
uint64 sys_exit()
{
    uint32 exit_code;

    arg_uint32(0, &exit_code);
    proc_exit((int)exit_code);

    panic("sys_exit: returned");
    return 0;
}

// 让当前进程睡眠指定数量的系统时钟周期
uint64 sys_sleep()
{
    uint32 ntick;

    arg_uint32(0, &ntick);
    timer_wait(ntick);

    return 0;
}

/* 从 data bitmap 申请一个块。 */
uint64 sys_alloc_block()
{
    return bitmap_alloc_block();
}

/* 释放指定 data block。 */
uint64 sys_free_block()
{
    uint32 block_num;

    arg_uint32(0, &block_num);
    bitmap_free_block(block_num);
    return 0;
}

/* 从 inode bitmap 申请一个 inode。 */
uint64 sys_alloc_inode()
{
    return bitmap_alloc_inode();
}

/* 释放指定 inode。 */
uint64 sys_free_inode()
{
    uint32 inode_num;

    arg_uint32(0, &inode_num);
    bitmap_free_inode(inode_num);
    return 0;
}

/* 用户接口约定：0=data bitmap，1=inode bitmap。 */
uint64 sys_show_bitmap()
{
    uint32 choose_bitmap;

    arg_uint32(0, &choose_bitmap);
    if (choose_bitmap > 1)
        return (uint64)-1;

    bitmap_print(choose_bitmap == 0);
    return 0;
}

/* 获取并保持指定块对应的 buffer 锁。 */
uint64 sys_get_block()
{
    uint32 block_num;

    arg_uint32(0, &block_num);
    return (uint64)buffer_get(block_num);
}

/* 将 buffer 的完整块复制到用户空间。 */
uint64 sys_read_block()
{
    proc_t *p = myproc();
    uint64 addr_buf, addr_data;

    arg_uint64(0, &addr_buf);
    arg_uint64(1, &addr_data);
    buffer_t *buf = (buffer_t *)addr_buf;

    assert(buf != NULL, "sys_read_block: NULL buffer");
    assert(sleeplock_holding(&buf->slk), "sys_read_block: buffer not locked");
    uvm_copyout(p->pgtbl, addr_data, (uint64)buf->data, BLOCK_SIZE);
    return 0;
}

/* 从用户空间复制完整块并写回磁盘。 */
uint64 sys_write_block()
{
    proc_t *p = myproc();
    uint64 addr_buf, addr_data;

    arg_uint64(0, &addr_buf);
    arg_uint64(1, &addr_data);
    buffer_t *buf = (buffer_t *)addr_buf;

    assert(buf != NULL, "sys_write_block: NULL buffer");
    assert(sleeplock_holding(&buf->slk), "sys_write_block: buffer not locked");
    uvm_copyin(p->pgtbl, (uint64)buf->data, addr_data, BLOCK_SIZE);
    buffer_write(buf);
    return 0;
}

/* 归还通过 sys_get_block 获得的 buffer。 */
uint64 sys_put_block()
{
    uint64 addr_buf;

    arg_uint64(0, &addr_buf);
    buffer_t *buf = (buffer_t *)addr_buf;

    assert(buf != NULL, "sys_put_block: NULL buffer");
    buffer_put(buf);
    return 0;
}

uint64 sys_show_buffer()
{
    buffer_print_info();
    return 0;
}

uint64 sys_flush_buffer()
{
    uint32 buffer_count;

    arg_uint32(0, &buffer_count);
    buffer_freemem(buffer_count);
    return 0;
}

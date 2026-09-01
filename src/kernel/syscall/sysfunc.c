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
                                     (uint32)change_len, PTE_R | PTE_W);
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

    if (start != 0)
    {
        uint64 requested_end = start + len;
        for (mmap_region_t *region = p->mmap;
             region != NULL;
             region = region->next)
        {
            uint64 region_end = region->begin +
                                (uint64)region->npages * PGSIZE;
            if (region_end < region->begin || region_end > MMAP_END)
                return (uint64)-1;
            if (start < region_end && region->begin < requested_end)
                return (uint64)-1;
        }
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

    uint64 requested_end = start + len;
    bool mapped = false;
    for (mmap_region_t *region = p->mmap;
         region != NULL;
         region = region->next)
    {
        uint64 region_end = region->begin +
                            (uint64)region->npages * PGSIZE;
        if (region_end < region->begin || region_end > MMAP_END)
            return (uint64)-1;
        if (region->begin <= start && requested_end <= region_end)
        {
            mapped = true;
            break;
        }
    }
    if (!mapped)
        return (uint64)-1;

    uvm_munmap(start, len / PGSIZE);

    // Task 4 提示性输出
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

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

/* 执行用户态 ELF，并在调用返回前复制好用户 argv。 */
uint64 sys_exec()
{
    proc_t *p = myproc();
    char path[STR_MAXLEN];
    uint64 argv_addr;
    char *argv[ELF_MAXARGS + 1];
    uint64 storage_page;
    uint32 argc;

    arg_str(0, path, STR_MAXLEN);
    arg_uint64(1, &argv_addr);
    if (argv_addr == 0)
        return (uint64)-1;

    storage_page = (uint64)pmem_alloc(true);
    argc = 0;
    for (; argc < ELF_MAXARGS; argc++) {
        uint64 user_arg;
        uint64 slot_addr = argv_addr + (uint64)argc * sizeof(uint64);
        if (slot_addr < argv_addr || slot_addr + sizeof(uint64) > VA_MAX) {
            pmem_free(storage_page, true);
            return (uint64)-1;
        }
        uvm_copyin(p->pgtbl, (uint64)&user_arg, slot_addr,
                   sizeof(user_arg));
        if (user_arg == 0)
            break;
        argv[argc] = (char *)(storage_page + (uint64)argc * ELF_MAXARG_LEN);
        uvm_copyin_str(p->pgtbl, (uint64)argv[argc], user_arg,
                       ELF_MAXARG_LEN);
    }
    if (argc == ELF_MAXARGS) {
        uint64 user_arg;
        uint64 slot_addr = argv_addr + (uint64)argc * sizeof(uint64);
        if (slot_addr < argv_addr || slot_addr + sizeof(uint64) > VA_MAX) {
            pmem_free(storage_page, true);
            return (uint64)-1;
        }
        uvm_copyin(p->pgtbl, (uint64)&user_arg, slot_addr,
                   sizeof(user_arg));
        if (user_arg != 0) {
            pmem_free(storage_page, true);
            return (uint64)-1;
        }
    }
    argv[argc] = NULL;

    uint64 ret = proc_exec(path, argv);
    pmem_free(storage_page, true);
    return ret;
}

static uint32 alloc_fd(file_t *file)
{
    proc_t *p = myproc();
    for (uint32 i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        if (p->open_file[i] == NULL) {
            p->open_file[i] = file;
            return i;
        }
    }
    return (uint32)-1;
}

uint64 sys_open()
{
    char path[STR_MAXLEN];
    uint32 open_mode;
    arg_str(0, path, STR_MAXLEN);
    arg_uint32(1, &open_mode);

    file_t *file = file_open(path, open_mode);
    if (file == NULL)
        return (uint64)-1;

    uint32 fd = alloc_fd(file);
    if (fd == (uint32)-1) {
        file_close(file);
        return (uint64)-1;
    }
    return fd;
}

uint64 sys_close()
{
    uint32 fd;
    file_t *file;
    if (arg_fd(0, &fd, &file) < 0)
        return (uint64)-1;
    myproc()->open_file[fd] = NULL;
    file_close(file);
    return 0;
}

uint64 sys_read()
{
    file_t *file;
    uint32 len;
    uint64 dst;
    if (arg_fd(0, NULL, &file) < 0)
        return 0;
    arg_uint32(1, &len);
    arg_uint64(2, &dst);
    return file_read(file, len, dst, true);
}

uint64 sys_write()
{
    file_t *file;
    uint32 len;
    uint64 src;
    if (arg_fd(0, NULL, &file) < 0)
        return 0;
    arg_uint32(1, &len);
    arg_uint64(2, &src);
    return file_write(file, len, src, true);
}

uint64 sys_lseek()
{
    file_t *file;
    uint32 offset, flag;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)-1;
    arg_uint32(1, &offset);
    arg_uint32(2, &flag);
    return file_lseek(file, offset, flag);
}

uint64 sys_dup()
{
    file_t *file;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)-1;

    file_t *dup = file_dup(file);
    if (dup == NULL)
        return (uint64)-1;
    uint32 fd = alloc_fd(dup);
    if (fd == (uint32)-1) {
        file_close(dup);
        return (uint64)-1;
    }
    return fd;
}

uint64 sys_fstat()
{
    file_t *file;
    uint64 user_dst;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)-1;
    arg_uint64(1, &user_dst);
    return file_get_stat(file, user_dst);
}

uint64 sys_get_dentries()
{
    file_t *file;
    uint32 len;
    uint64 dst;
    if (arg_fd(0, NULL, &file) < 0)
        return (uint64)-1;
    arg_uint64(1, &dst);
    arg_uint32(2, &len);

    inode_lock(file->ip);
    bool is_dir = file->ip->disk_info.type == INODE_TYPE_DIR;
    inode_unlock(file->ip);
    if (!is_dir || !file->readable)
        return (uint64)-1;
    return file_read(file, len, dst, true);
}

uint64 sys_mkdir()
{
    char path[STR_MAXLEN];
    arg_str(0, path, STR_MAXLEN);
    inode_t *ip = path_create_inode(path, INODE_TYPE_DIR,
                                    INODE_MAJOR_DEFAULT,
                                    INODE_MINOR_DEFAULT);
    if (ip == NULL)
        return (uint64)-1;
    inode_put(ip);
    return 0;
}

uint64 sys_chdir()
{
    char path[STR_MAXLEN];
    arg_str(0, path, STR_MAXLEN);
    inode_t *ip = path_to_inode(path);
    if (ip == NULL)
        return (uint64)-1;

    inode_lock(ip);
    bool is_dir = ip->disk_info.type == INODE_TYPE_DIR;
    inode_unlock(ip);
    if (!is_dir) {
        inode_put(ip);
        return (uint64)-1;
    }

    proc_t *p = myproc();
    inode_t *old = p->cwd;
    p->cwd = ip;
    if (old != NULL)
        inode_put(old);
    return 0;
}

uint64 sys_print_cwd()
{
    proc_t *p = myproc();
    char path[STR_MAXLEN];
    if (p->cwd == NULL)
        return (uint64)-1;
    uint32 offset = inode_to_path(p->cwd, path, sizeof(path));
    if (offset == (uint32)-1)
        return (uint64)-1;
    printf("%s\n", path + offset);
    return 0;
}

uint64 sys_link()
{
    char old_path[STR_MAXLEN];
    char new_path[STR_MAXLEN];
    arg_str(0, old_path, STR_MAXLEN);
    arg_str(1, new_path, STR_MAXLEN);
    return path_link(old_path, new_path);
}

uint64 sys_unlink()
{
    char path[STR_MAXLEN];
    arg_str(0, path, STR_MAXLEN);
    return path_unlink(path);
}

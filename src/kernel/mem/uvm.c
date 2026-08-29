#include "mod.h"

/*--------------------part-1: 关于内核空间<->用户空间的数据传递--------------------*/

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 page_va;
    uint64 page_pa;
    uint32 copy_len;
    pte_t *pte;

    // 用户地址区间必须位于合法的 Sv39 用户地址范围内
    assert(src + len >= src && src + len <= VA_MAX,
           "uvm_copyin: illegal user range");

    while (len > 0)
    {
        // 找到 src 当前所在用户页的页首地址
        page_va = src - src % PGSIZE;
        pte = vm_getpte(pgtbl, page_va, false);

        assert(pte != NULL, "uvm_copyin: pte not found");
        assert((*pte & PTE_V) && (*pte & PTE_U) && (*pte & PTE_R),
               "uvm_copyin: page cannot be read");

        // 用户虚拟页转换为内核可以访问的物理页
        page_pa = PTE_TO_PA(*pte);

        // 本轮最多复制到当前页末尾，下一轮继续处理下一页
        copy_len = PGSIZE - (src - page_va);
        if (copy_len > len)
            copy_len = len;

        memmove((void *)dst,
                (const void *)(page_pa + src - page_va),
                copy_len);

        src += copy_len;
        dst += copy_len;
        len -= copy_len;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 page_va;
    uint64 page_pa;
    uint32 copy_len;
    pte_t *pte;

    // 目标用户地址必须位于合法范围内
    assert(dst + len >= dst && dst + len <= VA_MAX,
           "uvm_copyout: illegal user range");

    while (len > 0)
    {
        // 找到 dst 当前所在用户页的页首地址
        page_va = dst - dst % PGSIZE;
        pte = vm_getpte(pgtbl, page_va, false);

        assert(pte != NULL, "uvm_copyout: pte not found");
        assert((*pte & PTE_V) && (*pte & PTE_U) && (*pte & PTE_W),
               "uvm_copyout: page cannot be written");

        page_pa = PTE_TO_PA(*pte);

        // 本轮只写到当前用户页末尾
        copy_len = PGSIZE - (dst - page_va);
        if (copy_len > len)
            copy_len = len;

        memmove((void *)(page_pa + dst - page_va),
                (const void *)src,
                copy_len);

        dst += copy_len;
        src += copy_len;
        len -= copy_len;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint64 page_va;
    uint64 page_pa;
    uint32 page_left;
    pte_t *pte;
    char ch;

    assert(maxlen > 0, "uvm_copyin_str: maxlen is zero");
    assert(src + maxlen >= src && src + maxlen <= VA_MAX,
           "uvm_copyin_str: illegal user range");

    while (maxlen > 0)
    {
        page_va = src - src % PGSIZE;
        pte = vm_getpte(pgtbl, page_va, false);

        assert(pte != NULL, "uvm_copyin_str: pte not found");
        assert((*pte & PTE_V) && (*pte & PTE_U) && (*pte & PTE_R),
               "uvm_copyin_str: page cannot be read");

        page_pa = PTE_TO_PA(*pte);
        page_left = PGSIZE - (src - page_va);

        // 在当前页中逐字节查找字符串结束符
        while (page_left > 0 && maxlen > 0)
        {
            ch = *(char *)(page_pa + src - page_va);
            *(char *)dst = ch;

            if (ch == '\0')
                return;

            src++;
            dst++;
            page_left--;
            maxlen--;
        }
    }

    panic("uvm_copyin_str: string is too long");
}

/*--------------------part-2: mmap_region相关--------------------*/

// 打印以mmap为首的mmap链
// for debug
void uvm_show_mmaplist(mmap_region_t *mmap)
{
    mmap_region_t *tmp = mmap;
    printf("\nalloced mmap_space:\n");
    if (tmp == NULL)
        printf("empty\n");
    while (tmp != NULL)
    {
        printf("alloced mmap_region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 两个 mmap_region 区域合并
// 注意: 保留一个 释放一个 不操作 next 指针
// 由uvm_mmap调用
static void mmap_merge(mmap_region_t *mmap_1, mmap_region_t *mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");

    // merge
    if (keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 寻找一块足够大的区域(len), 作为 mmap_region
// 由uvm_mmap调用(处理begin==0的情况)
// 成功返回begin, 失败返回0
static uint64 uvm_mmap_find(mmap_region_t *head_mmap, uint64 len, mmap_region_t **p_last_mmap, mmap_region_t **p_tmp_mmap)
{
    //TODO
    return 0;
}

// 在用户页表和进程mmap链里新增mmap区域 [begin, begin + npages * PGSIZE)
// 调用者保证begin是page-aligned的, 页面权限为perm
// 注意: 如果start==0, 意味着需要内核自主找一块足够大的空间
// 失败则panic卡死
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    // Task 4 实现前，仅保留辅助函数引用以通过分阶段编译
    (void)&mmap_merge;
    (void)&uvm_mmap_find;
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
// 失败则panic卡死
void uvm_munmap(uint64 begin, uint32 npages)
{

}

/*------------------part-3: 用户空间heap和stack管理相关------------------*/

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
    uint64 new_heap_top;
    uint64 map_begin;
    uint64 map_end;
    uint64 page;

    if (len == 0)
        return cur_heap_top;

    // 堆从 USER_BASE 向高地址增长，不能进入 mmap 区域
    if (cur_heap_top < USER_BASE || cur_heap_top > MMAP_BEGIN)
        return (uint64)-1;
    if ((uint64)len > MMAP_BEGIN - cur_heap_top)
        return (uint64)-1;

    new_heap_top = cur_heap_top + len;

    // 只为新覆盖到的完整虚拟页建立映射
    map_begin = cur_heap_top;
    if (map_begin % PGSIZE != 0)
        map_begin += PGSIZE - map_begin % PGSIZE;

    map_end = new_heap_top;
    if (map_end % PGSIZE != 0)
        map_end += PGSIZE - map_end % PGSIZE;

    for (uint64 va = map_begin; va < map_end; va += PGSIZE)
    {
        page = (uint64)pmem_alloc(false);
        vm_mappages(pgtbl, va, page, PGSIZE,
                    PTE_R | PTE_W | PTE_U);
    }

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 cur_heap_top, uint32 len)
{
    uint64 heap_bottom = USER_BASE + PGSIZE;
    uint64 new_heap_top;
    uint64 old_map_end;
    uint64 new_map_end;

    if (len == 0)
        return cur_heap_top;

    // initcode 所在页面不能作为堆空间释放
    if (cur_heap_top < heap_bottom || cur_heap_top > MMAP_BEGIN)
        return (uint64)-1;
    if ((uint64)len > cur_heap_top - heap_bottom)
        return (uint64)-1;

    new_heap_top = cur_heap_top - len;

    // 计算收缩前后仍需保留到哪个页面
    old_map_end = cur_heap_top;
    if (old_map_end % PGSIZE != 0)
        old_map_end += PGSIZE - old_map_end % PGSIZE;

    new_map_end = new_heap_top;
    if (new_map_end % PGSIZE != 0)
        new_map_end += PGSIZE - new_map_end % PGSIZE;

    // 解除已经完全落到新堆顶之外的页面，并归还物理页
    if (new_map_end < old_map_end)
    {
        vm_unmappages(pgtbl,
                      new_map_end,
                      old_map_end - new_map_end,
                      true);
    }

    return new_heap_top;
}

// 处理函数栈增长导致的page fault事件
// 成功返回new_ustack_npage，失败返回-1
uint64 uvm_ustack_grow(pgtbl_t pgtbl, uint64 old_ustack_npage,
                       uint64 fault_addr)
{
    uint64 old_stack_bottom;
    uint64 new_stack_bottom;
    uint64 page;
    uint64 max_stack_npage = (TRAPFRAME - MMAP_END) / PGSIZE;

    // 当前栈状态必须有效
    if (old_ustack_npage == 0 ||
        old_ustack_npage > max_stack_npage)
        return (uint64)-1;

    old_stack_bottom = TRAPFRAME - old_ustack_npage * PGSIZE;

    // 缺页地址必须位于当前栈下方，并且不能进入 mmap 区域
    if (fault_addr < MMAP_END || fault_addr >= old_stack_bottom)
        return (uint64)-1;

    new_stack_bottom = fault_addr - fault_addr % PGSIZE;

    // 一次缺页可能跨过多个尚未映射的栈页面
    for (uint64 va = new_stack_bottom;
         va < old_stack_bottom;
         va += PGSIZE)
    {
        page = (uint64)pmem_alloc(false);
        vm_mappages(pgtbl, va, page, PGSIZE,
                    PTE_R | PTE_W | PTE_U);
    }

    return (TRAPFRAME - new_stack_bottom) / PGSIZE;
}

/*----------------------part-4: 用户页表管理相关----------------------*/

// 递归释放页表占用的物理页和页表管理的用户物理页
// 顶级页表的 level 为 3
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    // TODO: Task 5 实现递归页表销毁
}

// 页表销毁
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    // trapframe 每个进程独有，可以释放
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, true);

    // trampoline 由所有进程共享，只解除映射
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false);

    destroy_pgtbl(pgtbl, 3);
}

// 连续虚拟空间的复制，由 uvm_copy_pgtbl 使用
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va;
    uint64 pa;
    uint64 page;
    int flags;
    pte_t *pte;

    for (va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");

        pa = PTE_TO_PA(*pte);
        flags = PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        memmove((void *)page, (const void *)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 复制用户页表，不包括 trapframe 和 trampoline
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top,
                    uint64 ustack_npage, mmap_region_t *mmap)
{
    // Task 5 实现前，保留复制辅助函数引用
    (void)&copy_range;
}
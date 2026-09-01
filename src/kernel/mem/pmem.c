#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    // 前 KERN_PAGES 个可分配页归内核区域管理。 
    kern_region.begin = (uint64)ALLOC_BEGIN;
    kern_region.end = kern_region.begin + KERN_PAGES * PGSIZE;
    kern_region.allocable = 0;
    kern_region.list_head.next = 0;
    spinlock_init(&kern_region.lk, "kern_region");

    // 剩余可分配页归用户区域管理。 
    user_region.begin = kern_region.end;
    user_region.end = (uint64)ALLOC_END;
    user_region.allocable = 0;
    user_region.list_head.next = 0;
    spinlock_init(&user_region.lk, "user_region");

    // 逐页放入两个区域各自的空闲链表。 
    for (uint64 page = kern_region.begin; page < kern_region.end; page += PGSIZE)
        pmem_free(page, true);

    for (uint64 page = user_region.begin; page < user_region.end; page += PGSIZE)
        pmem_free(page, false);
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void *pmem_alloc(bool in_kernel)
{
    alloc_region_t *region;
    page_node_t *page;

    region = in_kernel ? &kern_region : &user_region;

    spinlock_acquire(&region->lk);

    // 空闲链表为空时 panic 锁死。 
    if (region->allocable == 0) {
        spinlock_release(&region->lk);
        panic("pmem_alloc: out of memory");
    }

    // 从链表头取走一个空闲页
    page = region->list_head.next;
    region->list_head.next = page->next;
    region->allocable--;

    spinlock_release(&region->lk);

    // 返回清零后的物理页
    memset(page, 0, PGSIZE);
    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *region;
    page_node_t *node;

    region = in_kernel ? &kern_region : &user_region;

    // page 参数仍是 uint64 物理地址。
    // 只有空闲状态下，才把该页开头解释成链表节点以使用 next
    node = (page_node_t *)page;

    spinlock_acquire(&region->lk);

    // 头插法归还页面。 
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;

    spinlock_release(&region->lk);
}

// 获取两个物理页池当前可分配的页数
void pmem_stat(uint32 *free_pages_in_kernel, uint32 *free_pages_in_user)
{
    assert(free_pages_in_kernel != NULL && free_pages_in_user != NULL,
           "pmem_stat: NULL output");

    spinlock_acquire(&kern_region.lk);
    *free_pages_in_kernel = kern_region.allocable;
    spinlock_release(&kern_region.lk);

    spinlock_acquire(&user_region.lk);
    *free_pages_in_user = user_region.allocable;
    spinlock_release(&user_region.lk);
}

#include "mod.h"

// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;

// 初始化上述三个数据结构
void mmap_init()
{
    spinlock_init(&list_lk, "mmap_list");

    // list_head 是不可分配的哨兵节点
    list_head.next = &node_list[0];

    // 初始时按数组下标顺序连接全部空闲节点
    for (int i = 0; i < N_MMAP; i++)
    {
        node_list[i].mmap.begin = 0;
        node_list[i].mmap.npages = 0;
        node_list[i].mmap.next = NULL;

        if (i + 1 < N_MMAP)
            node_list[i].next = &node_list[i + 1];
        else
            node_list[i].next = NULL;
    }
}
// 从仓库申请一个 mmap_region_t
// 若仓库空了则 panic
mmap_region_t *mmap_region_alloc()
{
    mmap_region_node_t *node;

    spinlock_acquire(&list_lk);
    node = list_head.next;

    if (node == NULL)
    {
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: no free node");
        return NULL;
    }

    // 从空闲仓库链表头部取出一个节点
    list_head.next = node->next;
    node->next = NULL;

    // 清除上一次使用留下的进程区间信息
    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.next = NULL;

    spinlock_release(&list_lk);
    return &node->mmap;
}

// 向仓库归还一个 mmap_region_t
void mmap_region_free(mmap_region_t *mmap)
{
    mmap_region_node_t *node;
    mmap_region_node_t *tmp;
    uint64 node_addr;
    uint64 list_begin;
    uint64 list_end;

    if (mmap == NULL)
    {
        panic("mmap_region_free: mmap is NULL");
        return;
    }

    // mmap 是 mmap_region_node_t 的第一个字段，两者起始地址相同
    node = (mmap_region_node_t *)mmap;
    node_addr = (uint64)node;
    list_begin = (uint64)&node_list[0];
    list_end = (uint64)&node_list[N_MMAP];

    // 防止把仓库之外或未对齐的地址放入空闲链表
    if (node_addr < list_begin ||
        node_addr >= list_end ||
        (node_addr - list_begin) % sizeof(mmap_region_node_t) != 0)
    {
        panic("mmap_region_free: illegal node");
        return;
    }

    spinlock_acquire(&list_lk);

    // 检查重复归还，避免同一节点在空闲链表中出现两次
    for (tmp = list_head.next; tmp != NULL; tmp = tmp->next)
    {
        if (tmp == node)
        {
            spinlock_release(&list_lk);
            panic("mmap_region_free: double free");
            return;
        }
    }

    node->mmap.begin = 0;
    node->mmap.npages = 0;
    node->mmap.next = NULL;

    // 放回链表头部，因此并发释放后的索引会呈倒序交错
    node->next = list_head.next;
    list_head.next = node;

    spinlock_release(&list_lk);
}

// 输出可用的 mmap_region_node_t 链
// for debug
void mmap_show_nodelist()
{
    spinlock_acquire(&list_lk);

    mmap_region_node_t *tmp = list_head.next;
    int node = 0, index = 0;
    while (tmp)
    {
        index = tmp - &(node_list[0]);
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }

    spinlock_release(&list_lk);
}
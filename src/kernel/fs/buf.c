#include "mod.h"

static buffer_node_t buf_cache[N_BUFFER];
static buffer_node_t buf_head_active, buf_head_inactive;
static spinlock_t lk_buf_cache;

/* 将节点插入活跃/非活跃链表的头或尾。 */
static void insert_node(buffer_node_t *node, bool insert_active, bool insert_next)
{
    if (node->next != NULL && node->prev != NULL) {
        node->next->prev = node->prev;
        node->prev->next = node->next;
    }

    buffer_node_t *head = insert_active ? &buf_head_active : &buf_head_inactive;

    if (insert_next) {
        node->next = head->next;
        node->next->prev = node;
        node->prev = head;
        head->next = node;
    } else {
        node->prev = head->prev;
        node->prev->next = node;
        node->next = head;
        head->prev = node;
    }
}

/* 初始化两个循环链表和全部 buffer 控制块。 */
void buffer_init()
{
    spinlock_init(&lk_buf_cache, "buffer_cache");

    buf_head_active.next = &buf_head_active;
    buf_head_active.prev = &buf_head_active;
    buf_head_inactive.next = &buf_head_inactive;
    buf_head_inactive.prev = &buf_head_inactive;

    for (uint32 i = 0; i < N_BUFFER; i++) {
        buffer_node_t *node = &buf_cache[i];

        node->next = NULL;
        node->prev = NULL;
        node->buf.block_num = BLOCK_NUM_UNUSED;
        node->buf.ref = 0;
        node->buf.data = NULL;
        node->buf.disk = false;
        sleeplock_init(&node->buf.slk, "buffer");

        insert_node(node, false, true);
    }
}

/* 磁盘读取：block -> buffer。调用者必须持有 buffer 睡眠锁。 */
static void buffer_read(buffer_t *buf)
{
    assert(sleeplock_holding(&buf->slk), "buffer_read: lock not held");
    assert(buf->data != NULL, "buffer_read: data page is NULL");
    virtio_disk_rw(buf, false);
}

/* 磁盘写入：buffer -> block。调用者必须持有 buffer 睡眠锁。 */
void buffer_write(buffer_t *buf)
{
    assert(sleeplock_holding(&buf->slk), "buffer_write: lock not held");
    assert(buf->data != NULL, "buffer_write: data page is NULL");
    virtio_disk_rw(buf, true);
}

/* 从 buffer cache 获取指定 block 的已上锁 buffer。 */
buffer_t *buffer_get(uint32 block_num)
{
    buffer_node_t *node = NULL;
    bool need_read = false;

    spinlock_acquire(&lk_buf_cache);

    for (node = buf_head_active.next;
         node != &buf_head_active;
         node = node->next) {
        if (node->buf.block_num == block_num) {
            node->buf.ref++;
            insert_node(node, true, true);
            spinlock_release(&lk_buf_cache);

            sleeplock_acquire(&node->buf.slk);
            assert(node->buf.data != NULL,
                   "buffer_get: active buffer has no data page");
            return &node->buf;
        }
    }

    for (node = buf_head_inactive.next;
         node != &buf_head_inactive;
         node = node->next) {
        if (node->buf.block_num == block_num) {
            node->buf.ref++;
            insert_node(node, true, true);
            need_read = node->buf.data == NULL;
            spinlock_release(&lk_buf_cache);

            sleeplock_acquire(&node->buf.slk);
            if (need_read) {
                node->buf.data = pmem_alloc(true);
                assert(node->buf.data != NULL,
                       "buffer_get: alloc cached page failed");
                buffer_read(&node->buf);
            }
            return &node->buf;
        }
    }

    node = buf_head_inactive.prev;
    assert(node != &buf_head_inactive, "buffer_get: no free buffer");

    node->buf.block_num = block_num;
    node->buf.ref = 1;
    insert_node(node, true, false);
    spinlock_release(&lk_buf_cache);

    sleeplock_acquire(&node->buf.slk);
    if (node->buf.data == NULL) {
        node->buf.data = pmem_alloc(true);
        assert(node->buf.data != NULL, "buffer_get: alloc page failed");
    }
    buffer_read(&node->buf);

    return &node->buf;
}

/* 归还一个 buffer；调用者不再持有该 buffer 的睡眠锁。 */
void buffer_put(buffer_t *buf)
{
    assert(buf != NULL, "buffer_put: NULL buffer");
    assert(sleeplock_holding(&buf->slk), "buffer_put: lock not held");

    sleeplock_release(&buf->slk);

    buffer_node_t *node = (buffer_node_t *)buf;
    spinlock_acquire(&lk_buf_cache);
    assert(buf->ref > 0, "buffer_put: invalid reference count");

    buf->ref--;
    if (buf->ref == 0)
        insert_node(node, false, true);

    spinlock_release(&lk_buf_cache);
}

/* 释放 inactive 链表末端 buffer 持有的物理页。 */
uint32 buffer_freemem(uint32 buffer_count)
{
    uint32 freed = 0;

    spinlock_acquire(&lk_buf_cache);

    for (buffer_node_t *node = buf_head_inactive.prev;
         node != &buf_head_inactive && freed < buffer_count;) {
        buffer_node_t *prev = node->prev;

        if (node->buf.data != NULL) {
            pmem_free((uint64)node->buf.data, true);
            node->buf.data = NULL;
            node->buf.block_num = BLOCK_NUM_UNUSED;
            node->buf.disk = false;
            freed++;
        }

        node = prev;
    }

    spinlock_release(&lk_buf_cache);
    return freed;
}

/* 输出 buffer_cache 的信息（测试配置专用）。 */
void buffer_print_info()
{
    buffer_node_t *node;

    assert(N_BUFFER == N_BUFFER_TEST, "buffer_print_info: invalid N_BUFFER");

    spinlock_acquire(&lk_buf_cache);

    printf("buffer_cache information:\n");

    printf("1.active list:\n");
    for (node = buf_head_active.next; node != &buf_head_active; node = node->next) {
        printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
               (int)(node - buf_cache), node->buf.ref,
               (uint64)node->buf.data, node->buf.block_num);
    }
    printf("over!\n");

    printf("2.inactive list:\n");
    for (node = buf_head_inactive.next; node != &buf_head_inactive; node = node->next) {
        printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
               (int)(node - buf_cache), node->buf.ref,
               (uint64)node->buf.data, node->buf.block_num);
    }
    printf("over!\n");

    spinlock_release(&lk_buf_cache);
}

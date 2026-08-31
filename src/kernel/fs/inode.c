#include "mod.h"

extern super_block_t sb;

static inode_t inode_cache[N_INODE];
static spinlock_t lk_inode_cache;

static uint32 alloc_zero_block(void)
{
    uint32 block_num = bitmap_alloc_block();
    if (block_num == (uint32)-1)
        return (uint32)-1;

    buffer_t *buf = buffer_get(block_num);
    memset(buf->data, 0, BLOCK_SIZE);
    buffer_write(buf);
    buffer_put(buf);
    return block_num;
}

void inode_init()
{
    spinlock_init(&lk_inode_cache, "inode_cache");
    for (uint32 i = 0; i < N_INODE; i++) {
        inode_cache[i].valid_info = false;
        inode_cache[i].inode_num = INVALID_INODE_NUM;
        inode_cache[i].ref = 0;
        memset(&inode_cache[i].disk_info, 0, sizeof(inode_disk_t));
        sleeplock_init(&inode_cache[i].slk, "inode");
    }
}

static bool __free_data_blocks(uint32 block_num, uint32 level)
{
    if (block_num == 0)
        return true;

    if (level == 0) {
        bitmap_free_block(block_num);
        return false;
    }

    buffer_t *buf = buffer_get(block_num);
    uint32 *index = (uint32 *)buf->data;
    bool meet_empty = false;
    for (uint32 i = 0; i < BLOCK_SIZE / sizeof(uint32); i++) {
        if (index[i] == 0) {
            meet_empty = true;
            break;
        }
        if (__free_data_blocks(index[i], level - 1)) {
            meet_empty = true;
            break;
        }
    }
    buffer_put(buf);
    bitmap_free_block(block_num);
    return meet_empty;
}

static void free_data_blocks(uint32 *inode_index)
{
    unsigned int i;
    bool meet_empty = false;

    for (i = 0; i < INODE_INDEX_1; i++) {
        meet_empty = __free_data_blocks(inode_index[i], 0);
        if (meet_empty) return;
    }
    for (; i < INODE_INDEX_2; i++) {
        meet_empty = __free_data_blocks(inode_index[i], 1);
        if (meet_empty) return;
    }
    for (; i < INODE_INDEX_3; i++) {
        meet_empty = __free_data_blocks(inode_index[i], 2);
        if (meet_empty) return;
    }
    panic("free_data_blocks: impossible!");
}

static uint32 locate_or_add_block(uint32 *inode_index, uint32 logical_block_num)
{
    const uint32 index_per_block = BLOCK_SIZE / sizeof(uint32);
    if (logical_block_num >= INODE_BLOCK_INDEX_3)
        return (uint32)-1;

    if (logical_block_num < INODE_BLOCK_INDEX_1) {
        if (inode_index[logical_block_num] == 0) {
            uint32 block_num = alloc_zero_block();
            if (block_num == (uint32)-1)
                return (uint32)-1;
            inode_index[logical_block_num] = block_num;
        }
        return inode_index[logical_block_num];
    }

    if (logical_block_num < INODE_BLOCK_INDEX_2) {
        uint32 relative = logical_block_num - INODE_BLOCK_INDEX_1;
        uint32 outer = relative / index_per_block;
        uint32 inner = relative % index_per_block;
        uint32 index_slot = INODE_INDEX_1 + outer;
        bool new_index = false;

        if (inode_index[index_slot] == 0) {
            inode_index[index_slot] = alloc_zero_block();
            if (inode_index[index_slot] == (uint32)-1) {
                inode_index[index_slot] = 0;
                return (uint32)-1;
            }
            new_index = true;
        }

        buffer_t *buf = buffer_get(inode_index[index_slot]);
        uint32 *index = (uint32 *)buf->data;
        if (index[inner] == 0) {
            uint32 block_num = alloc_zero_block();
            if (block_num == (uint32)-1) {
                buffer_put(buf);
                if (new_index) {
                    bitmap_free_block(inode_index[index_slot]);
                    inode_index[index_slot] = 0;
                }
                return (uint32)-1;
            }
            index[inner] = block_num;
            buffer_write(buf);
        }
        uint32 result = index[inner];
        buffer_put(buf);
        return result;
    }

    uint32 relative = logical_block_num - INODE_BLOCK_INDEX_2;
    uint32 outer = relative / index_per_block;
    uint32 inner = relative % index_per_block;
    bool new_root = false;

    if (inode_index[INODE_INDEX_2] == 0) {
        inode_index[INODE_INDEX_2] = alloc_zero_block();
        if (inode_index[INODE_INDEX_2] == (uint32)-1) {
            inode_index[INODE_INDEX_2] = 0;
            return (uint32)-1;
        }
        new_root = true;
    }

    buffer_t *root_buf = buffer_get(inode_index[INODE_INDEX_2]);
    uint32 *root_index = (uint32 *)root_buf->data;
    uint32 first_block = root_index[outer];
    if (first_block == 0) {
        first_block = alloc_zero_block();
        if (first_block == (uint32)-1) {
            buffer_put(root_buf);
            if (new_root) {
                bitmap_free_block(inode_index[INODE_INDEX_2]);
                inode_index[INODE_INDEX_2] = 0;
            }
            return (uint32)-1;
        }
        root_index[outer] = first_block;
        buffer_write(root_buf);
    }
    buffer_put(root_buf);

    buffer_t *first_buf = buffer_get(first_block);
    uint32 *first_index = (uint32 *)first_buf->data;
    if (first_index[inner] == 0) {
        uint32 block_num = alloc_zero_block();
        if (block_num == (uint32)-1) {
            buffer_put(first_buf);
            return (uint32)-1;
        }
        first_index[inner] = block_num;
        buffer_write(first_buf);
    }
    uint32 result = first_index[inner];
    buffer_put(first_buf);
    return result;
}

void inode_rw(inode_t *ip, bool write)
{
    assert(ip != NULL, "inode_rw: NULL inode");
    assert(sleeplock_holding(&ip->slk), "inode_rw: slk");
    assert(ip->inode_num < sb.total_inodes, "inode_rw: invalid inode num");

    uint32 block_num = sb.inode_firstblock + ip->inode_num / INODE_PER_BLOCK;
    uint32 byte_offset = (ip->inode_num % INODE_PER_BLOCK) * sizeof(inode_disk_t);
    buffer_t *buf = buffer_get(block_num);
    if (write) {
        memmove(buf->data + byte_offset, &ip->disk_info, sizeof(inode_disk_t));
        buffer_write(buf);
    } else {
        memmove(&ip->disk_info, buf->data + byte_offset, sizeof(inode_disk_t));
        ip->valid_info = true;
    }
    buffer_put(buf);
}

inode_t *inode_get(uint32 inode_num)
{
    assert(inode_num < sb.total_inodes, "inode_get: invalid inode num");
    spinlock_acquire(&lk_inode_cache);

    for (uint32 i = 0; i < N_INODE; i++) {
        if (inode_cache[i].ref > 0 && inode_cache[i].inode_num == inode_num) {
            inode_cache[i].ref++;
            spinlock_release(&lk_inode_cache);
            return &inode_cache[i];
        }
    }

    for (uint32 i = 0; i < N_INODE; i++) {
        if (inode_cache[i].ref == 0) {
            inode_cache[i].inode_num = inode_num;
            inode_cache[i].ref = 1;
            inode_cache[i].valid_info = false;
            spinlock_release(&lk_inode_cache);
            return &inode_cache[i];
        }
    }

    spinlock_release(&lk_inode_cache);
    panic("inode_get: no free inode cache");
    return NULL;
}

inode_t *inode_create(uint16 type, uint16 major, uint16 minor)
{
    uint32 inode_num = bitmap_alloc_inode();
    if (inode_num == (uint32)-1)
        panic("inode_create: no free inode");

    inode_t *ip = inode_get(inode_num);
    inode_lock(ip);
    memset(&ip->disk_info, 0, sizeof(inode_disk_t));
    ip->disk_info.type = type;
    ip->disk_info.major = major;
    ip->disk_info.minor = minor;
    ip->disk_info.nlink = 1;
    ip->disk_info.size = 0;
    ip->valid_info = true;
    inode_rw(ip, true);
    inode_unlock(ip);
    return ip;
}

inode_t* inode_dup(inode_t* ip)
{
    assert(ip != NULL, "inode_dup: NULL inode");
    spinlock_acquire(&lk_inode_cache);
    assert(ip->ref > 0, "inode_dup: invalid reference");
    ip->ref++;
    spinlock_release(&lk_inode_cache);
    return ip;
}

void inode_lock(inode_t* ip)
{
    assert(ip != NULL, "inode_lock: NULL inode");
    spinlock_acquire(&lk_inode_cache);
    assert(ip->ref > 0, "inode_lock: invalid reference");
    spinlock_release(&lk_inode_cache);

    sleeplock_acquire(&ip->slk);
    if (!ip->valid_info)
        inode_rw(ip, false);
}

void inode_unlock(inode_t *ip)
{
    assert(ip != NULL, "inode_unlock: NULL inode");
    assert(sleeplock_holding(&ip->slk), "inode_unlock: lock not held");
    sleeplock_release(&ip->slk);
}

void inode_put(inode_t* ip)
{
    assert(ip != NULL, "inode_put: NULL inode");
    spinlock_acquire(&lk_inode_cache);
    assert(ip->ref > 0, "inode_put: invalid reference");

    if (ip->ref == 1 && ip->valid_info && ip->disk_info.nlink == 0) {
        spinlock_release(&lk_inode_cache);
        inode_lock(ip);
        inode_delete(ip);
        inode_unlock(ip);

        spinlock_acquire(&lk_inode_cache);
        assert(ip->ref == 1, "inode_put: reference changed during delete");
        ip->ref = 0;
        ip->inode_num = INVALID_INODE_NUM;
        ip->valid_info = false;
        spinlock_release(&lk_inode_cache);
        return;
    }

    ip->ref--;
    if (ip->ref == 0) {
        ip->inode_num = INVALID_INODE_NUM;
        ip->valid_info = false;
    }
    spinlock_release(&lk_inode_cache);
}

void inode_delete(inode_t *ip)
{
    assert(ip != NULL, "inode_delete: NULL inode");
    assert(sleeplock_holding(&ip->slk), "inode_delete: slk");
    free_data_blocks(ip->disk_info.index);
    memset(&ip->disk_info, 0, sizeof(inode_disk_t));
    inode_rw(ip, true);
    bitmap_free_inode(ip->inode_num);
    ip->valid_info = false;
}

uint32 inode_read_data(inode_t *ip, uint32 offset, uint32 len, void *dst, bool is_user_dst)
{
    assert(ip != NULL, "inode_read_data: NULL inode");
    assert(sleeplock_holding(&ip->slk), "inode_read_data: slk");
    if (len == 0 || offset >= ip->disk_info.size)
        return 0;
    if (len > ip->disk_info.size - offset)
        len = ip->disk_info.size - offset;

    uint32 total = 0;
    while (total < len) {
        uint32 position = offset + total;
        uint32 logical_block = position / BLOCK_SIZE;
        uint32 block_offset = position % BLOCK_SIZE;
        uint32 cut_len = MIN(BLOCK_SIZE - block_offset, len - total);
        uint32 block_num = locate_or_add_block(ip->disk_info.index, logical_block);
        if (block_num == (uint32)-1)
            break;

        buffer_t *buf = buffer_get(block_num);
        if (is_user_dst)
            uvm_copyout(myproc()->pgtbl, (uint64)dst + total,
                        (uint64)buf->data + block_offset, cut_len);
        else
            memmove((uint8 *)dst + total, buf->data + block_offset, cut_len);
        buffer_put(buf);
        total += cut_len;
    }
    return total;
}

uint32 inode_write_data(inode_t *ip, uint32 offset, uint32 len, void *src, bool is_user_src)
{
    assert(ip != NULL, "inode_write_data: NULL inode");
    assert(sleeplock_holding(&ip->slk), "inode_write_data: slk");
    if (len == 0 || offset > ip->disk_info.size)
        return 0;
    if (len > INODE_MAX_SIZE - offset)
        len = INODE_MAX_SIZE - offset;

    uint32 total = 0;
    while (total < len) {
        uint32 position = offset + total;
        uint32 logical_block = position / BLOCK_SIZE;
        uint32 block_offset = position % BLOCK_SIZE;
        uint32 cut_len = MIN(BLOCK_SIZE - block_offset, len - total);
        uint32 block_num = locate_or_add_block(ip->disk_info.index, logical_block);
        if (block_num == (uint32)-1)
            break;

        buffer_t *buf = buffer_get(block_num);
        if (is_user_src)
            uvm_copyin(myproc()->pgtbl, (uint64)buf->data + block_offset,
                       (uint64)src + total, cut_len);
        else
            memmove(buf->data + block_offset, (uint8 *)src + total, cut_len);
        buffer_write(buf);
        buffer_put(buf);
        total += cut_len;
    }

    if (total > 0) {
        uint32 end = offset + total;
        if (end > ip->disk_info.size)
            ip->disk_info.size = end;
        inode_rw(ip, true);
    }
    return total;
}

static char *inode_type_list[] = {"DATA", "DIR", "DEVICE"};

void inode_print(inode_t *ip, char* name)
{
    assert(sleeplock_holding(&ip->slk), "inode_print: slk");

    spinlock_acquire(&lk_inode_cache);

    printf("inode %s:\n", name);
    printf("ref = %d, inode_num = %d, valid_info = %d\n", ip->ref, ip->inode_num, ip->valid_info);
    printf("type = %s, major = %d, minor = %d, nlink = %d, size = %d\n", inode_type_list[ip->disk_info.type],
        ip->disk_info.major, ip->disk_info.minor, ip->disk_info.nlink, ip->disk_info.size);

    printf("index_list = [ ");
    for (int i = 0; i < INODE_INDEX_1; i++)
        printf("%d ", ip->disk_info.index[i]);
    printf("] [ ");
    for (int i = INODE_INDEX_1; i < INODE_INDEX_2; i++)
        printf("%d ", ip->disk_info.index[i]);
    printf("] [ ");
    for (int i = INODE_INDEX_2; i < INODE_INDEX_3; i++)
        printf("%d ", ip->disk_info.index[i]);
    printf("]\n\n");

    spinlock_release(&lk_inode_cache);
}

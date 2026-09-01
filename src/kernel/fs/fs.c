#include "mod.h"
#include "../lib/method.h"
#include "../proc/method.h"

super_block_t sb;

file_t file_table[N_FILE];
spinlock_t lk_file_table;

void file_init()
{
    spinlock_init(&lk_file_table, "file_table");
    for (uint32 i = 0; i < N_FILE; i++) {
        file_table[i].ip = NULL;
        file_table[i].readable = false;
        file_table[i].writbale = false;
        file_table[i].offset = 0;
        file_table[i].ref = 0;
    }
}

file_t *file_alloc()
{
    spinlock_acquire(&lk_file_table);
    for (uint32 i = 0; i < N_FILE; i++) {
        file_t *file = &file_table[i];
        if (file->ref != 0)
            continue;

        file->ip = NULL;
        file->readable = false;
        file->writbale = false;
        file->offset = 0;
        file->ref = 1;
        spinlock_release(&lk_file_table);
        return file;
    }
    spinlock_release(&lk_file_table);
    return NULL;
}

file_t *file_open(char *path, uint32 open_mode)
{
    if (path == NULL)
        return NULL;
    if ((open_mode & ~(FILE_OPEN_CREATE | FILE_OPEN_READ |
                       FILE_OPEN_WRITE)) != 0)
        return NULL;
    if ((open_mode & (FILE_OPEN_READ | FILE_OPEN_WRITE)) == 0)
        return NULL;

    inode_t *ip = path_to_inode(path);
    if (ip == NULL && (open_mode & FILE_OPEN_CREATE))
        ip = path_create_inode(path, INODE_TYPE_DATA,
                               INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
    if (ip == NULL)
        return NULL;

    inode_lock(ip);
    uint16 type = ip->disk_info.type;
    uint16 major = ip->disk_info.major;
    inode_unlock(ip);

    if (type == INODE_TYPE_DIVICE) {
        if ((open_mode & FILE_OPEN_CREATE) ||
            !device_open_check(major,
                               open_mode & (FILE_OPEN_READ | FILE_OPEN_WRITE))) {
            inode_put(ip);
            return NULL;
        }
    } else if (type == INODE_TYPE_DIR &&
               (open_mode & FILE_OPEN_WRITE)) {
        inode_put(ip);
        return NULL;
    } else if (type != INODE_TYPE_DATA && type != INODE_TYPE_DIR) {
        inode_put(ip);
        return NULL;
    }

    file_t *file = file_alloc();
    if (file == NULL) {
        inode_put(ip);
        return NULL;
    }

    file->ip = ip;
    file->readable = (open_mode & FILE_OPEN_READ) != 0;
    file->writbale = (open_mode & FILE_OPEN_WRITE) != 0;
    file->offset = 0;
    return file;
}

void file_close(file_t *file)
{
    if (file == NULL)
        return;

    inode_t *ip = NULL;
    spinlock_acquire(&lk_file_table);
    assert(file->ref > 0, "file_close: invalid file");
    file->ref--;
    if (file->ref == 0) {
        ip = file->ip;
        file->ip = NULL;
        file->readable = false;
        file->writbale = false;
        file->offset = 0;
    }
    spinlock_release(&lk_file_table);

    if (ip != NULL)
        inode_put(ip);
}

uint32 file_read(file_t *file, uint32 len, uint64 dst, bool is_user_dst)
{
    if (file == NULL || file->ref == 0 || file->ip == NULL ||
        !file->readable)
        return 0;

    inode_t *ip = file->ip;
    uint32 read_len = 0;
    uint16 type;
    uint16 major;

    inode_lock(ip);
    type = ip->disk_info.type;
    major = ip->disk_info.major;

    if (type == INODE_TYPE_DIVICE) {
        inode_unlock(ip);
        read_len = device_read_data(major, len, dst, is_user_dst);
    } else if (type == INODE_TYPE_DATA) {
        read_len = inode_read_data(ip, file->offset, len,
                                   (void *)dst, is_user_dst);
        file->offset += read_len;
        inode_unlock(ip);
    } else if (type == INODE_TYPE_DIR) {
        read_len = dentry_transmit(ip, dst, len, is_user_dst);
        file->offset += read_len;
        inode_unlock(ip);
    } else {
        inode_unlock(ip);
    }

    if (type == INODE_TYPE_DIVICE)
        file->offset += read_len;
    return read_len;
}

uint32 file_write(file_t *file, uint32 len, uint64 src, bool is_user_src)
{
    if (file == NULL || file->ref == 0 || file->ip == NULL ||
        !file->writbale)
        return 0;

    inode_t *ip = file->ip;
    uint32 write_len = 0;
    uint16 type;
    uint16 major;

    inode_lock(ip);
    type = ip->disk_info.type;
    major = ip->disk_info.major;

    if (type == INODE_TYPE_DIVICE) {
        inode_unlock(ip);
        write_len = device_write_data(major, len, src, is_user_src);
    } else if (type == INODE_TYPE_DATA) {
        write_len = inode_write_data(ip, file->offset, len,
                                     (void *)src, is_user_src);
        file->offset += write_len;
        inode_unlock(ip);
    } else {
        inode_unlock(ip);
    }

    if (type == INODE_TYPE_DIVICE)
        file->offset += write_len;
    return write_len;
}

uint32 file_lseek(file_t *file, uint32 lseek_offset, uint32 lseek_flag)
{
    if (file == NULL || file->ref == 0)
        return (uint32)-1;

    uint64 next;
    switch (lseek_flag) {
    case FILE_LSEEK_SET:
        next = lseek_offset;
        break;
    case FILE_LSEEK_ADD:
        next = (uint64)file->offset + lseek_offset;
        if (next > INODE_MAX_SIZE)
            next = INODE_MAX_SIZE;
        break;
    case FILE_LSEEK_SUB:
        next = file->offset > lseek_offset ?
               file->offset - lseek_offset : 0;
        break;
    default:
        return (uint32)-1;
    }

    file->offset = (uint32)next;
    return file->offset;
}

file_t *file_dup(file_t *file)
{
    if (file == NULL)
        return NULL;

    spinlock_acquire(&lk_file_table);
    if (file->ref == 0) {
        spinlock_release(&lk_file_table);
        return NULL;
    }
    file->ref++;
    spinlock_release(&lk_file_table);
    return file;
}

uint32 file_get_stat(file_t *file, uint64 user_dst)
{
    if (file == NULL || file->ref == 0 || file->ip == NULL ||
        user_dst == 0)
        return (uint32)-1;

    inode_t *ip = file->ip;
    file_stat_t stat;

    inode_lock(ip);
    stat.type = ip->disk_info.type;
    stat.nlink = ip->disk_info.nlink;
    stat.size = ip->disk_info.size;
    stat.inode_num = ip->inode_num;
    stat.offset = file->offset;
    uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&stat, sizeof(stat));
    inode_unlock(ip);
    return 0;
}

/* 基于 superblock 输出磁盘布局信息（for debug） */
static void sb_print()
{
    printf("\ndisk layout information:\n");
    printf("1. super block:  block[0]\n");
    printf("2. inode bitmap: block[%d - %d]\n", sb.inode_bitmap_firstblock,
           sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1);
    printf("3. inode region: block[%d - %d]\n", sb.inode_firstblock,
           sb.inode_firstblock + sb.inode_blocks - 1);
    printf("4. data bitmap:  block[%d - %d]\n", sb.data_bitmap_firstblock,
           sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1);
    printf("5. data region:  block[%d - %d]\n", sb.data_firstblock,
           sb.data_firstblock + sb.data_blocks - 1);
    printf("block size = %d Byte, total size = %d MB, total inode = %d\n\n",
           sb.block_size,
           (int)((unsigned long long)sb.total_blocks * sb.block_size /
                 1024 / 1024),
           sb.total_inodes);
}

void fs_init()
{
    buffer_init();

    buffer_t *buf = buffer_get(FS_SB_BLOCK);
    memmove(&sb, buf->data, sizeof(sb));
    buffer_put(buf);

    assert(sb.magic_num == FS_MAGIC, "fs_init: invalid filesystem magic");
    assert(sb.block_size == BLOCK_SIZE, "fs_init: invalid block size");
    assert(sb.inode_bitmap_firstblock == FS_SB_BLOCK + 1,
           "fs_init: invalid inode bitmap start");
    assert(sb.inode_firstblock ==
               sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks,
           "fs_init: invalid inode region start");
    assert(sb.data_bitmap_firstblock == sb.inode_firstblock + sb.inode_blocks,
           "fs_init: invalid data bitmap start");
    assert(sb.data_firstblock ==
               sb.data_bitmap_firstblock + sb.data_bitmap_blocks,
           "fs_init: invalid data region start");
    assert(sb.total_blocks == sb.data_firstblock + sb.data_blocks,
           "fs_init: invalid total blocks");

    inode_init();
    file_init();
    device_init();
    sb_print();
}

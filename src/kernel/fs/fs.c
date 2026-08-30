#include "mod.h"

super_block_t sb;

/* 基于 superblock 输出磁盘布局信息 (for debug) */
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
           (int)((unsigned long long)sb.total_blocks * sb.block_size / 1024 / 1024),
           sb.total_inodes);
}

/* 文件系统初始化：初始化 buffer cache 并读取超级块 */
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

    sb_print();
}

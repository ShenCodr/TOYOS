#include "mod.h"

super_block_t sb;

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
    sb_print();
    printf("============= test begin =============\n\n");

    inode_t *rooti, *ip_1, *ip_2, *ip_3, *ip_4, *ip_5;
    rooti = inode_get(ROOT_INODE);
    ip_1 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
    ip_2 = inode_create(INODE_TYPE_DIR, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
    ip_3 = inode_create(INODE_TYPE_DATA, INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);

    inode_lock(rooti);
    inode_lock(ip_1);
    inode_lock(ip_2);
    inode_lock(ip_3);

    assert(dentry_create(rooti, ip_1->inode_num, "AABBC") != INVALID_INODE_NUM,
           "dentry_create fail 1!");
    assert(dentry_create(ip_1, ip_2->inode_num, "aaabb") != INVALID_INODE_NUM,
           "dentry_create fail 2!");
    assert(dentry_create(ip_2, ip_3->inode_num, "file.txt") != INVALID_INODE_NUM,
           "dentry_create fail 3!");

    char tmp1[] = "This is file context!";
    char tmp2[32];
    inode_write_data(ip_3, 0, sizeof(tmp1), tmp1, false);

    inode_rw(rooti, true);
    inode_rw(ip_1, true);
    inode_rw(ip_2, true);

    inode_unlock(rooti);
    inode_unlock(ip_1);
    inode_unlock(ip_2);
    inode_unlock(ip_3);
    inode_put(rooti);
    inode_put(ip_1);
    inode_put(ip_2);
    inode_put(ip_3);

    char *path = "///AABBC///aaabb/file.txt";
    char name[MAXLEN_FILENAME];
    ip_4 = path_to_inode(path);
    if (ip_4 == NULL)
        panic("invalid ip_4");
    ip_5 = path_to_parent_inode(path, name);
    if (ip_5 == NULL)
        panic("invalid ip_5");
    printf("get a name = %s\n\n", name);

    inode_lock(ip_4);
    inode_lock(ip_5);
    inode_print(ip_4, "file.txt");
    inode_print(ip_5, "aaabb");
    inode_read_data(ip_4, 0, 32, tmp2, false);
    printf("read data: %s\n\n", tmp2);
    inode_unlock(ip_4);
    inode_unlock(ip_5);
    inode_put(ip_4);
    inode_put(ip_5);

    printf("============= test end =============\n");
    while (1)
        ;
}

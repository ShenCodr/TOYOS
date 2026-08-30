#include "mod.h"

extern super_block_t sb;

/* 在一个 bitmap block 中找到首个空闲 bit，置 1 后返回局部索引。 */
static uint32 bitmap_search_and_set(uint32 bitmap_block_num, uint32 valid_count)
{
    assert(valid_count <= BIT_PER_BLOCK,
           "bitmap_search_and_set: invalid bit count");

    buffer_t *buf = buffer_get(bitmap_block_num);

    for (uint32 index = 0; index < valid_count; index++) {
        uint32 byte = index / BIT_PER_BYTE;
        uint32 shift = index % BIT_PER_BYTE;
        uint8 mask = (uint8)(1U << shift);

        if ((buf->data[byte] & mask) == 0) {
            buf->data[byte] |= mask;
            buffer_write(buf);
            buffer_put(buf);
            return index;
        }
    }

    buffer_put(buf);
    return (uint32)-1;
}

/* 将 bitmap block 中第 index 个 bit 清零。 */
static void bitmap_clear(uint32 bitmap_block_num, uint32 index)
{
    assert(index < BIT_PER_BLOCK, "bitmap_clear: invalid bit index");

    buffer_t *buf = buffer_get(bitmap_block_num);
    uint32 byte = index / BIT_PER_BYTE;
    uint32 shift = index % BIT_PER_BYTE;

    buf->data[byte] &= (uint8)~(1U << shift);
    buffer_write(buf);
    buffer_put(buf);
}

/* 获取一个空闲 data block，并返回其全局磁盘块号。 */
uint32 bitmap_alloc_block()
{
    uint32 current_bit = 0;

    for (uint32 block = 0; block < sb.data_bitmap_blocks; block++) {
        uint32 valid_count = BIT_PER_BLOCK;
        if (current_bit + valid_count > sb.data_blocks)
            valid_count = sb.data_blocks - current_bit;

        uint32 index = bitmap_search_and_set(
            sb.data_bitmap_firstblock + block, valid_count);
        if (index != (uint32)-1)
            return sb.data_firstblock + current_bit + index;

        current_bit += valid_count;
    }

    return (uint32)-1;
}

/* 获取一个空闲 inode，并返回 inode 编号。 */
uint32 bitmap_alloc_inode()
{
    uint32 current_bit = 0;

    for (uint32 block = 0; block < sb.inode_bitmap_blocks; block++) {
        uint32 valid_count = BIT_PER_BLOCK;
        if (current_bit + valid_count > sb.total_inodes)
            valid_count = sb.total_inodes - current_bit;

        uint32 index = bitmap_search_and_set(
            sb.inode_bitmap_firstblock + block, valid_count);
        if (index != (uint32)-1)
            return current_bit + index;

        current_bit += valid_count;
    }

    return (uint32)-1;
}

/* 释放一个 data block。 */
void bitmap_free_block(uint32 block_num)
{
    assert(block_num >= sb.data_firstblock &&
               block_num < sb.data_firstblock + sb.data_blocks,
           "bitmap_free_block: block out of range");

    uint32 bit_num = block_num - sb.data_firstblock;
    uint32 bitmap_block = bit_num / BIT_PER_BLOCK;
    uint32 bit_in_block = bit_num % BIT_PER_BLOCK;

    bitmap_clear(sb.data_bitmap_firstblock + bitmap_block, bit_in_block);
}

/* 释放一个 inode。 */
void bitmap_free_inode(uint32 inode_num)
{
    assert(inode_num < sb.total_inodes, "bitmap_free_inode: inode out of range");

    uint32 bitmap_block = inode_num / BIT_PER_BLOCK;
    uint32 bit_in_block = inode_num % BIT_PER_BLOCK;

    bitmap_clear(sb.inode_bitmap_firstblock + bitmap_block, bit_in_block);
}

/* 打印某个 bitmap 中所有已分配 bit。 */
void bitmap_print(bool print_data_bitmap)
{
    uint32 first_block, bitmap_blocks, total_bits;
    uint32 global_base, current_bit = 0;

    if (print_data_bitmap) {
        printf("data bitmap alloced bits:\n");
        first_block = sb.data_bitmap_firstblock;
        bitmap_blocks = sb.data_bitmap_blocks;
        total_bits = sb.data_blocks;
        global_base = sb.data_firstblock;
    } else {
        printf("inode bitmap alloced bits:\n");
        first_block = sb.inode_bitmap_firstblock;
        bitmap_blocks = sb.inode_bitmap_blocks;
        total_bits = sb.total_inodes;
        global_base = 0;
    }

    for (uint32 block = 0; block < bitmap_blocks; block++) {
        uint32 bitmap_block_num = first_block + block;
        uint32 bits_in_this_block = BIT_PER_BLOCK;

        if (current_bit + bits_in_this_block > total_bits)
            bits_in_this_block = total_bits - current_bit;

        buffer_t *buf = buffer_get(bitmap_block_num);

        for (uint32 index = 0; index < bits_in_this_block; index++) {
            uint32 byte = index / BIT_PER_BYTE;
            uint32 shift = index % BIT_PER_BYTE;
            uint8 mask = (uint8)(1U << shift);

            if (buf->data[byte] & mask)
                printf("%d ", global_base + current_bit + index);
        }

        current_bit += bits_in_this_block;
        buffer_put(buf);
    }

    printf("over!\n\n");
}

#pragma once
#include "../arch/type.h"

/* Lab-6 正式系统调用号 */

#define SYS_brk 1
#define SYS_mmap 2
#define SYS_munmap 3
#define SYS_print_str 4
#define SYS_print_int 5
#define SYS_getpid 6
#define SYS_fork 7
#define SYS_wait 8
#define SYS_exit 9
#define SYS_sleep 10

/* Lab-7 文件系统系统调用 */
#define SYS_alloc_block 11
#define SYS_free_block 12
#define SYS_alloc_inode 13
#define SYS_free_inode 14
#define SYS_show_bitmap 15
#define SYS_get_block 16
#define SYS_read_block 17
#define SYS_write_block 18
#define SYS_put_block 19
#define SYS_show_buffer 20
#define SYS_flush_buffer 21

#define SYS_MAX_NUM 21

/* 可以传入的最大字符串长度 */
#define STR_MAXLEN 127
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

/* 保留 Lab-5 数据传输测试接口 */

#define SYS_copyin 11
#define SYS_copyout 12
#define SYS_copyinstr 13

#define SYS_MAX_NUM 13

/* 可以传入的最大字符串长度 */
#define STR_MAXLEN 127
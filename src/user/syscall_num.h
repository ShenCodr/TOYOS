#define SYS_copyin 1    // 用户地址空间的数据复制到内核
#define SYS_copyout 2   // 内核数据复制到用户地址空间
#define SYS_copyinstr 3 // 用户字符串复制到内核
#define SYS_brk 4       // 调整用户堆顶
#define SYS_mmap 5      // 创建匿名内存映射
#define SYS_munmap 6    // 解除匿名内存映射
## Lab-8：文件系统之上层抽象

### 1. 实验目标与实现

Lab-8 在 Lab-7 的 VirtIO、buffer cache、superblock 和 bitmap 之上，补齐 inode、目录项（dentry）和绝对路径解析，使磁盘块能够组织成普通文件与多层目录。实现包含 64 项 inode cache、inode 磁盘元数据同步、10 个直接索引、2 个一级间接索引和 1 个二级间接索引；目录项固定为 64B，支持名称查找、创建、删除以及 `.`、`..` 根目录初始化

inode 数据读写按逻辑块分段，经 buffer cache 访问磁盘；新建索引块先清零再写盘，删除 inode 时递归释放数据块和各级索引块。普通文件拒绝产生空洞的 `offset > size` 写入，并用无溢出方式检查 `INODE_MAX_SIZE`。路径解析从 inode 0 开始，支持连续 `/`；失败路径会释放已经取得的 inode 引用

复查阶段额外确认：新分配的数据块会清零，索引块分配失败不会把无效块号留在 inode 中，目录项创建会拒绝超出 superblock 范围的 inode 号；删除后重新分配同一数据块的隔离测试已读回零填充

mkfs 已预置根目录 inode 0、`ABCD.txt` inode 1 和 `abcd.txt` inode 2，并保留 64 位 `off_t` 块偏移计算，确保 5GB 镜像可以从干净构建中正确生成。Makefile 同时依赖 `mkfs.c` 与 `mkfs.h`，源码改变后会重新制盘。

### 2. 验证环境与结果

测试使用双 hart、QEMU 5.1.0、4096B block 和 `N_BUFFER = N_BUFFER_TEST = 8`。每组测试都先精确重建 `target/mkfs/disk.img`，避免持久化目录项互相污染。教师 Test-2 的大文件读取对象在测试前已释放，实际验证时改为仍持有的大文件 inode `ip_2`；大文件源缓冲区使用静态数组，避免环境中物理页不保证连续导致的无关失败。

| 测试 | 验证内容 | 实际 QEMU 结果 |
| --- | --- | --- |
| Test 1 | inode cache、创建、引用计数与最后释放 | 根 inode 为 0、DIR、size 256；创建 inode 3/4 后 bitmap 为 `0 1 2 3 4`，释放后恢复为 `0 1 2`，无 panic。 |
| Test 2 | 小文件、一级和二级索引、大文件读回 | 小文件 size 16000、直接块 1074--1077；大文件 size 174940000，直接块 1074--1083、一级索引 1084/2109、二级索引 3134，末尾读回 `GHABCDEF`。 |
| Test 3 | 目录项查找、创建、删除与空槽复用 | 根目录初始项位于 0/64/128/192，新目录写入偏移 256，删除后恢复四个初始项，并正确读回预置文件内容。 |
| Test 4 | 多层绝对路径与父目录解析 | `name=file.txt`；目标 inode 5、父目录 inode 4，文件 size 22、数据块 1076，读回 `This is file context!`。 |


Test 1：

![Lab-8 Test1](pictures/lab-8-test-1-inode.png)

Test 2：

![Lab-8 Test2](pictures/lab-8-test-2-index.png)

Test 3：

![Lab-8 Test3](pictures/lab-8-test-3-dentry.png)

Test 4：

![Lab-8 Test4](pictures/lab-8-test-4-path.png)

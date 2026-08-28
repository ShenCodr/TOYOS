# TOYOS

一个基于 RISC-V 的小型操作系统内核学习项目

> 本文记录 Lab 0 环境配置的实际过程、验证结果与踩坑原因

## 1. 项目目标

本次实验目标是准备 Linux、Git、RISC-V 工具链、QEMU 与 xv6，并以 **xv6-riscv-2020 能成功启动** 作为环境验收标准。

## 2. 环境搭建

实验采用 **Windows + WSL 2 Ubuntu 终端** 。后续实验的编译、运行和调试核心均在 Linux 终端完成。

| 项目              | 实际使用                                     |
| ----------------- | -------------------------------------------- |
| Linux 环境        | WSL 2 + Ubuntu 24.04.4 LTS                   |
| 项目目录          | `/home/shen/Work/TOYOS`                      |
| 编辑与终端        | Windows / WSL 终端，配合 VS Code             |
| RISC-V 编译器     | `riscv64-linux-gnu-gcc` 13.3.0               |
| RISC-V 二进制工具 | GNU Binutils 2.42                            |
| 调试器            | `gdb-multiarch` 15.1                         |
| QEMU              | 5.1.0，位于 `/home/shen/opt/qemu-5.1.0`      |
| xv6 参考内核      | `/home/shen/Work/xv6-labs-2020`，`util` 分支 |

系统软件源中同时存在 QEMU 8.2.2，但它与旧版 xv6 在本机发生了运行时兼容问题。因此显式指定 QEMU 5.1.0。

## 3. 目录布局

```text
/home/shen/Work/
├── TOYOS/                     # 本项目与 GitHub 仓库
├── qemu-5.1.0/                # QEMU 5.1.0 源码及独立构建目录
└── xv6-labs-2020/             # xv6-riscv-2020，使用 util 分支

/home/shen/opt/qemu-5.1.0/
└── bin/qemu-system-riscv64    # 课程实际使用的 QEMU 可执行文件
```

## 4. Lab 0 配置过程

### 4.1 安装工具链

首先更新软件包索引并安装交叉编译、模拟和调试工具：

```bash
sudo apt-get update
sudo apt-get install -y \
  gdb-multiarch qemu-system-misc \
  gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
```

其中，`gcc-riscv64-linux-gnu` 负责生成 RISC-V 代码，`binutils-riscv64-linux-gnu` 提供链接器和 ELF 工具，`qemu-system-misc` 提供系统级 RISC-V 模拟器，`gdb-multiarch` 用于后续调试。本机原本已有 GCC、Make、Git 与 wget；

### 4.2 验证交叉编译器

使用一个不依赖 C 标准库的最小 C 文件进行交叉编译：

```bash
riscv64-linux-gnu-gcc -ffreestanding -nostdlib -c minimal.c -o minimal.o
file minimal.o
riscv64-linux-gnu-readelf -h minimal.o
```

`-ffreestanding` 和 `-nostdlib` 表明这是没有完整操作系统和标准库的裸机式程序。实际生成的 `minimal.o` 被识别为 64 位 RISC-V ELF 文件，说明交叉编译链可用。

### 4.3 构建课程版本的 QEMU 5.1.0

系统 QEMU 能正常安装，但启动旧版 xv6 时在 `mret` 附近反复陷入 M-mode，未进入 xv6 shell。因此从源码构建 QEMU 5.1.0。

先安装构建依赖：

```bash
sudo apt-get install -y \
  ninja-build pkg-config libglib2.0-dev libpixman-1-dev zlib1g-dev
```

下载并解压源码后，在独立目录配置：

```bash
mkdir -p /home/shen/Work/qemu-5.1.0/build-riscv64
cd /home/shen/Work/qemu-5.1.0/build-riscv64
../configure \
  --disable-kvm \
  --disable-werror \
  --disable-gtk \
  --disable-sdl \
  --prefix=/home/shen/opt/qemu-5.1.0 \
  --target-list=riscv64-softmmu
make -j4
make install
```

`--target-list=riscv64-softmmu` 只构建本项目需要的 RISC-V 系统模拟器；`--prefix` 将其安装到用户目录，避免覆盖系统 QEMU；关闭 GTK 和 SDL 后，纯终端环境也能运行

### 4.4 获取与运行 xv6-riscv-2020

使用 xv6-riscv-2020 的 `util` 分支验证环境：

```bash
mkdir -p /home/shen/Work
git clone git://g.csail.mit.edu/xv6-labs-2020 /home/shen/Work/xv6-labs-2020
cd /home/shen/Work/xv6-labs-2020
git checkout util
```

运行时增加兼容参数并强制使用 QEMU：

```bash
env "CFLAGS=-Wall -Werror -O -fno-omit-frame-pointer -ggdb -DSOL_UTIL -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie -Wno-infinite-recursion" \
make -e qemu QEMU=/home/shen/opt/qemu-5.1.0/bin/qemu-system-riscv64
```

### 4.5 环境验收结果

出现以下输出，表示 xv6 已完成编译、被 QEMU 正常启动并进入 shell：

```text
xv6 kernel is booting
hart 2 starting
hart 1 starting
init: starting sh
$
```

随后在 xv6 shell 中执行：

```sh
ls
echo LAB0_OK
```

实际结果：`ls` 能列出 xv6 文件系统内容，`echo LAB0_OK` 输出 `LAB0_OK`

## 5. WSL 网络代理说明

本机 Windows 代理端口为 `7897`。WSL 的 NAT 网络不能直接使用 Windows 的 `127.0.0.1:7897`，动态获取 Windows 主机网关后配置代理：

```bash
lab_proxy_host=$(ip route show | awk '/default/ {print $3}')
export http_proxy="http://${lab_proxy_host}:7897"
export https_proxy="http://${lab_proxy_host}:7897"
export all_proxy="http://${lab_proxy_host}:7897"
```

## 6. Git 分支与提交约定

本实验具有连续性。本项目按 `master → lab-1 → lab-2 → ...` 的顺序保留个人实现历史，后续 Lab 从上一个个人分支继续开发。

本 README 在 `master` 分支维护。开始前，仓库原本处于 `lab-1` 分支；为写入基线环境文档，先确认工作区状态后切回 `master`：

```bash
git status --short --branch
git switch master
```

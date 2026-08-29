#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();

// 第一个用户进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成trapframe和trampoline的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    // 用户页表自身和中间页表都从内核物理页区域分配
    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(true);
    assert(pgtbl != NULL, "proc_pgtbl_init: alloc pgtbl failed");

    // trampoline 在内核页表和用户页表中使用相同虚拟地址
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline,
                PGSIZE, PTE_R | PTE_X);

    // trapframe 供 S-mode 保存和恢复用户寄存器，U-mode 无权访问
    vm_mappages(pgtbl, TRAPFRAME, trapframe,
                PGSIZE, PTE_R | PTE_W);

    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问

	注意: 用用户空间的地址映射需要标记 PTE_U
*/
void proc_make_first()
{
    // 设置第一个用户进程的身份，并分配保存用户寄存器的 trapframe
    proczero.pid = 0;
    proczero.mmap = NULL; // 新进程尚未分配任何 mmap 区域
    proczero.tf = (trapframe_t *)pmem_alloc(false);
    assert(proczero.tf != NULL, "proc_make_first: alloc trapframe failed");

    // 创建用户页表，并建立 trampoline 和 trapframe 的高地址映射
    proczero.pgtbl = proc_pgtbl_init((uint64)proczero.tf);

    // 映射一页用户栈，栈顶紧邻 trapframe 的下方
    void *ustack_page = pmem_alloc(false);
    assert(ustack_page != NULL, "proc_make_first: alloc user stack failed");
    vm_mappages(proczero.pgtbl, TRAPFRAME - PGSIZE, (uint64)ustack_page,
                PGSIZE, PTE_R | PTE_W | PTE_U);
    proczero.ustack_npage = 1;

    // initcode 的代码和数据共用一页，用户堆从该页之后开始增长
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too large");
    void *initcode_page = pmem_alloc(false);
    assert(initcode_page != NULL, "proc_make_first: alloc initcode failed");
    memmove(initcode_page, initcode, initcode_len);
    vm_mappages(proczero.pgtbl, USER_BASE, (uint64)initcode_page,
                PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    proczero.heap_top = USER_BASE + PGSIZE;

    // 设置首次进入用户态时的 PC 和用户栈顶
    proczero.tf->user_to_kern_epc = USER_BASE;
    proczero.tf->sp = TRAPFRAME;

    // KSTACK(0) 已在 kvm_init() 中建立映射，ctx 指向用户态返回准备函数
    proczero.kstack = KSTACK(0);
    proczero.ctx.ra = (uint64)trap_user_return;
    proczero.ctx.sp = proczero.kstack + PGSIZE;

    // 记录 CPU0 当前运行的进程，并切换到 proczero 的内核上下文
    mycpu()->proc = &proczero;
    swtch(&mycpu()->ctx, &proczero.ctx);
}
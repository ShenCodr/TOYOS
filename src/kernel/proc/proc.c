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
static proc_t *proczero;

// 进程结构体仓库
static proc_t proc_list[N_PROC];

// 全局 PID 及其保护锁
static int global_pid;
static spinlock_t pid_lk;

// 分配一个全局唯一的 PID
static int alloc_pid()
{
    int pid;

    spinlock_acquire(&pid_lk);
    assert(global_pid > 0, "alloc_pid: overflow");
    pid = global_pid++;
    spinlock_release(&pid_lk);

    return pid;
}

// 首次被调度时释放进程锁并返回用户态
static void proc_return()
{
    proc_t *p = myproc();

    spinlock_release(&p->lk);
    trap_user_return();
}

// 初始化进程仓库
void proc_init()
{
    spinlock_init(&pid_lk, "pid");
    global_pid = 1;

    for (int i = 0; i < N_PROC; i++) {
        spinlock_init(&proc_list[i].lk, "proc");
        proc_list[i].state = UNUSED;
        proc_list[i].kstack = KSTACK(i);
    }
}

/*
    从进程仓库申请一个 UNUSED 槽位。
    成功时返回持有自身锁的进程，仓库耗尽时返回 NULL。
*/
proc_t *proc_alloc()
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];

        spinlock_acquire(&p->lk);
        if (p->state != UNUSED) {
            spinlock_release(&p->lk);
            continue;
        }

        p->pid = alloc_pid();
        memset(p->name, 0, sizeof(p->name));

        p->parent = NULL;
        p->exit_code = 0;
        p->sleep_space = NULL;

        p->heap_top = 0;
        p->ustack_npage = 0;
        p->mmap = NULL;

        p->tf = (trapframe_t *)pmem_alloc(false);
        assert(p->tf != NULL, "proc_alloc: alloc trapframe failed");
        p->pgtbl = proc_pgtbl_init((uint64)p->tf);

        memset(&p->ctx, 0, sizeof(p->ctx));
        p->ctx.ra = (uint64)proc_return;
        p->ctx.sp = p->kstack + PGSIZE;

        return p;
    }

    return NULL;
}

/*
    回收进程包含的资源并将槽位恢复为 UNUSED。
    调用者需要持有 p->lk，本函数不释放该锁。
*/
void proc_free(proc_t *p)
{
    assert(spinlock_holding(&p->lk), "proc_free: lock not held");

    if (p->pgtbl != NULL) {
        uvm_destroy_pgtbl(p->pgtbl);
        p->pgtbl = NULL;
        p->tf = NULL;
    }

    while (p->mmap != NULL) {
        mmap_region_t *region = p->mmap;
        p->mmap = region->next;
        mmap_region_free(region);
    }

    p->pid = 0;
    memset(p->name, 0, sizeof(p->name));
    p->parent = NULL;
    p->exit_code = 0;
    p->sleep_space = NULL;
    p->heap_top = 0;
    p->ustack_npage = 0;
    memset(&p->ctx, 0, sizeof(p->ctx));

    p->state = UNUSED;
}

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
    // 从进程仓库申请第一个槽位，返回时持有进程锁
    proczero = proc_alloc();
    assert(proczero != NULL, "proc_make_first: alloc proc failed");
    assert(proczero->pid == 1, "proc_make_first: unexpected pid");
    memmove(proczero->name, "proczero", 9);

    // 映射一页用户栈，栈顶紧邻 trapframe 的下方
    void *ustack_page = pmem_alloc(false);
    assert(ustack_page != NULL, "proc_make_first: alloc user stack failed");
    vm_mappages(proczero->pgtbl, TRAPFRAME - PGSIZE,
                (uint64)ustack_page, PGSIZE,
                PTE_R | PTE_W | PTE_U);
    proczero->ustack_npage = 1;

    // initcode 的代码和数据共用一页，用户堆从该页之后开始
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too large");
    void *initcode_page = pmem_alloc(false);
    assert(initcode_page != NULL, "proc_make_first: alloc initcode failed");
    memmove(initcode_page, initcode, initcode_len);
    vm_mappages(proczero->pgtbl, USER_BASE,
                (uint64)initcode_page, PGSIZE,
                PTE_R | PTE_W | PTE_X | PTE_U);
    proczero->heap_top = USER_BASE + PGSIZE;

    // 设置首次进入用户态时的 PC 和用户栈顶
    proczero->tf->user_to_kern_epc = USER_BASE;
    proczero->tf->sp = TRAPFRAME;

    // 创建完成，等待调度器选择
    proczero->state = RUNNABLE;
    spinlock_release(&proczero->lk);
}

/*
    复制当前进程，创建一个可调度的子进程。
    父进程获得子进程 PID，子进程从 fork 返回 0。
*/
int proc_fork()
{
    proc_t *parent = myproc();
    assert(parent != NULL, "proc_fork: no current proc");

    proc_t *child = proc_alloc();
    if (child == NULL)
        return -1;

    // 深复制代码、堆、栈和 mmap 页面。
    child->heap_top = parent->heap_top;
    child->ustack_npage = parent->ustack_npage;
    uvm_copy_pgtbl(parent->pgtbl, child->pgtbl,
                   parent->heap_top, parent->ustack_npage,
                   parent->mmap);

    // mmap 区域描述节点也由父子进程分别持有。
    mmap_region_t **tail = &child->mmap;
    for (mmap_region_t *src = parent->mmap;
         src != NULL;
         src = src->next) {
        mmap_region_t *dst = mmap_region_alloc();
        dst->begin = src->begin;
        dst->npages = src->npages;
        dst->next = NULL;

        *tail = dst;
        tail = &dst->next;
    }

    // 复制用户寄存器现场，并修改子进程的 fork 返回值。
    memmove(child->tf, parent->tf, sizeof(trapframe_t));
    child->tf->a0 = 0;

    memmove(child->name, parent->name, sizeof(child->name));
    child->parent = parent;

    int child_pid = child->pid;
    child->state = RUNNABLE;
    spinlock_release(&child->lk);

    return child_pid;
}

/*
    当前进程主动让出 CPU。
    RUNNING -> RUNNABLE
*/
void proc_yield()
{
    proc_t *p = myproc();
    assert(p != NULL, "proc_yield: no current proc");

    spinlock_acquire(&p->lk);
    assert(p->state == RUNNING, "proc_yield: proc not running");

    p->state = RUNNABLE;
    proc_sched();

    spinlock_release(&p->lk);
}

/*
    当前进程切换回本 CPU 的调度器。
    调用者必须只持有当前进程锁。
*/
void proc_sched()
{
    proc_t *p = myproc();
    cpu_t *cpu = mycpu();

    assert(p != NULL, "proc_sched: no current proc");
    assert(spinlock_holding(&p->lk), "proc_sched: lock not held");
    assert(cpu->noff == 1, "proc_sched: unexpected lock depth");
    assert(p->state != RUNNING, "proc_sched: proc still running");
    assert(intr_get() == 0, "proc_sched: interrupt enabled");

    int origin = cpu->origin;
    swtch(&p->ctx, &cpu->ctx);
    cpu->origin = origin;
}

/*
    循环扫描进程仓库，只调度 RUNNABLE 进程。
*/
void proc_scheduler()
{
    cpu_t *cpu = mycpu();
    cpu->proc = NULL;

    while (1) {
        intr_on();

        for (int i = 0; i < N_PROC; i++) {
            proc_t *p = &proc_list[i];

            spinlock_acquire(&p->lk);
            if (p->state == RUNNABLE) {
                p->state = RUNNING;
                cpu->proc = p;

                swtch(&cpu->ctx, &p->ctx);

                cpu->proc = NULL;
            }
            spinlock_release(&p->lk);
        }
    }
}

#include "mod.h"
#include "../../user/syscall_num.h"

// in trampoline.S
extern char trampoline[];  // 内核和用户切换的代码
extern char user_vector[]; // 用户触发陷阱进入内核
extern char user_return[]; // 内核处理完毕返回用户

// in trap.S
extern char kernel_vector[]; // 内核态trap处理流程, 进入内核后应当切换中断处理入口

// in trap_kernel.c
extern char *interrupt_info[16]; // 中断错误信息
extern char *exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();
    uint64 stval = r_stval();

    // 用户态 trap 到达内核后，后续内核 trap 应改由 kernel_vector 接管
    w_stvec((uint64)kernel_vector);

    // 此处理器应来自 U-mode，且硬件进入 trap 时已关闭中断。
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from user mode");
    assert(intr_get() == 0, "trap_user_handler: interrupt enabled");

    proc_t *proc = myproc();
    proc->tf->user_to_kern_epc = sepc;

    int trap_id = scause & 0xf;
    if (scause & 0x8000000000000000ul)
    {
        // 中断：复用 Lab-3 已验证的时钟和 UART 外设处理路径
        switch (trap_id)
        {
        case 1:
            timer_interrupt_handler();
            break;

        case 9:
            external_interrupt_handler();
            break;

        default:
            printf("\nunexpected user interrupt: %s\n", interrupt_info[trap_id]);
            printf("trap_id = %d, sepc = %p, stval = %p\n",
                   trap_id, sepc, stval);
            panic("trap_user_handler");
        }
    }
    else
    {
        // 异常：Lab-4 只处理 U-mode 发出的 ecall
        switch (trap_id)
        {
        case 8:
            // ecall 长度为 4 字节，返回时跳过它以避免重复陷入
            proc->tf->user_to_kern_epc += 4;

            if (proc->tf->a7 == SYS_helloworld)
                printf("proczero: hello world!\n");
            else
                panic("trap_user_handler: unknown syscall");
            break;

        default:
            printf("\nunexpected user exception: %s\n", exception_info[trap_id]);
            printf("trap_id = %d, sepc = %p, stval = %p\n",
                   trap_id, sepc, stval);
            panic("trap_user_handler");
        }
    }

    // 恢复用户页表和用户寄存器，最终由 sret 回到用户态
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    // 设置用户 trap 向量期间关闭中断，避免中间状态被打断
    intr_off();

    proc_t *proc = myproc();
    trapframe_t *tf = proc->tf;

    // 下次从 U-mode 陷入时，先执行用户页表中的 user_vector
    uint64 user_vector_va = TRAMPOLINE + (uint64)(user_vector - trampoline);
    w_stvec(user_vector_va);

    // 保存下次从用户态进入内核时所需的内核执行环境
    tf->user_to_kern_satp = r_satp();
    tf->user_to_kern_sp = proc->kstack + PGSIZE;
    tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    tf->user_to_kern_hartid = r_tp();

    // 恢复用户程序 PC，并让 sret 返回到 U-mode 且恢复后允许中断
    w_sepc(tf->user_to_kern_epc);
    uint64 sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP;
    sstatus |= SSTATUS_SPIE;
    w_sstatus(sstatus);

    // trampoline 切换用户页表、恢复寄存器并以 sret 进入用户态
    uint64 user_return_va = TRAMPOLINE + (uint64)(user_return - trampoline);
    void (*user_return_fn)(uint64, uint64) =
        (void (*)(uint64, uint64))user_return_va;
    user_return_fn(TRAPFRAME, MAKE_SATP(proc->pgtbl));
}
#include "mod.h"
#include "../proc/method.h"

// 初始化睡眠锁
void sleeplock_init(sleeplock_t *lk, char *name)
{
    spinlock_init(&lk->lock, "sleeplock");
    lk->locked = 0;
    lk->name = name;
    lk->pid = 0;
}

// 检查当前进程是否持有睡眠锁
bool sleeplock_holding(sleeplock_t *lk)
{
    proc_t *p = myproc();
    bool holding;

    assert(p != NULL, "sleeplock_holding: no current proc");

    spinlock_acquire(&lk->lock);
    holding = lk->locked && lk->pid == p->pid;
    spinlock_release(&lk->lock);

    return holding;
}

// 获取失败时睡眠，直到持有者释放锁
void sleeplock_acquire(sleeplock_t *lk)
{
    proc_t *p = myproc();

    assert(p != NULL, "sleeplock_acquire: no current proc");

    spinlock_acquire(&lk->lock);
    while (lk->locked)
        proc_sleep(lk, &lk->lock);

    lk->locked = 1;
    lk->pid = p->pid;
    spinlock_release(&lk->lock);
}

// 释放锁并唤醒所有等待者
void sleeplock_release(sleeplock_t *lk)
{
    proc_t *p = myproc();

    assert(p != NULL, "sleeplock_release: no current proc");

    spinlock_acquire(&lk->lock);
    assert(lk->locked && lk->pid == p->pid,
           "sleeplock_release: not holding");

    lk->locked = 0;
    lk->pid = 0;
    proc_wakeup(lk);
    spinlock_release(&lk->lock);
}

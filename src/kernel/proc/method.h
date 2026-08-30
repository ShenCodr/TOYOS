#pragma once

// proc.c: 进程管理相关

void proc_init();
proc_t *proc_alloc();
void proc_free(proc_t *p);

pgtbl_t proc_pgtbl_init(uint64 trapframe);
void proc_make_first();
int proc_fork();
int proc_wait(uint64 user_addr);
void proc_exit(int exit_code);
void proc_yield();
void proc_sleep(void *sleep_space, spinlock_t *lk);
void proc_wakeup(void *sleep_space);
void proc_sched();
void proc_scheduler();

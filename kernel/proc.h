#ifndef PROC_H
#define PROC_H

#include "kernel.h"

#define NPROC      3
#define PROC_READY 0
#define PROC_RUN   1
#define PROC_EXIT  2

typedef struct {
    u32 pid;
    u32 pc;              /* 用户 sepc */
    u32 sp;              /* 用户 sp */
    u32 state;
    u32 stack_pa;        /* 用户栈物理页 */
    u32 nrun;
    u32 brk;             /* 用户堆顶（sbrk 用） */
    u32 regs[32];        /* 完整用户上下文 */
} proc_t;

extern proc_t  procs[NPROC];
extern int     cur_proc;

void proc_init(void);
void proc_create(u32 pid, u32 entry, u32 stack_pa);
/* 时间片轮转：返回下一个可运行进程 */
proc_t *proc_schedule(void);
/* 上下文切换（汇编） */
void proc_switch(u32 *old_pc, u32 *old_sp, u32 new_pc, u32 new_sp);
/* 设置下一次定时器中断 */
void proc_set_timer(u32 delta);

#endif
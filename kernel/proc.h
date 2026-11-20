#ifndef PROC_H
#define PROC_H

#include "kernel.h"

#define NPROC      5
#define NPROC_INIT 3
#define PROC_READY 0
#define PROC_RUN   1
#define PROC_EXIT  2
#define PROC_UNUSED 3   /* 槽位空闲：被 kill 回收过，或开机就未使用 */
#define PROC_SUSPEND 4  /* 被宿主挂起：不参与调度 */

typedef struct {
    u32 pid;
    u32 pc;              /* 用户 sepc */
    u32 sp;              /* 用户 sp */
    u32 state;
    u32 stack_pa;        /* 用户栈物理页 */
    u32 nrun;
    u32 brk;             /* 用户堆顶（sbrk 用） */
    u32 *pgtbl;          /* 独立页表根 */
    u32 code_pa;         /* 本进程独占的代码/数据物理页起始 */
    u32 code_pages;      /* 占几页 */
    u32 regs[32];        /* 完整用户上下文 */
    /* 文件描述符表：fd 0..3 每个进程独立。
       存文件槽下标，-1 表示空闲。 */
    int fd_slot[4];
    u32 fd_pos[4];
} proc_t;

extern proc_t  procs[NPROC];
extern int     cur_proc;

void proc_init(void);
/* 结束一个进程并回收它的代码页/堆页/页表。
   返回 0 成功，-1 槽位本来就空，-2 进程正在运行（只能由定时器中断结束）。 */
int  proc_kill(u32 pid);
/* 回收已经死掉的进程（调用者必须已经切走 satp）。 */
void proc_reap(u32 pid);
int  proc_suspend(u32 pid);
int  proc_resume(u32 pid);
void proc_create(u32 pid, u32 entry, u32 stack_pa);
/* 时间片轮转：返回下一个可运行进程 */
proc_t *proc_schedule(void);
/* 上下文切换（汇编） */
void proc_switch(u32 *old_pc, u32 *old_sp, u32 new_pc, u32 new_sp);
/* 设置下一次定时器中断 */
void proc_set_timer(u32 delta);
/* 把进程表同步到 RAM 顶页，供 Web 前端读取 */
void front_sync(void);

#endif
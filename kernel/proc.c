#include "kernel.h"
#include "proc.h"

proc_t procs[NPROC];
int    cur_proc = -1;

void proc_init(void)
{
    memset(procs, 0, sizeof(procs));
    cur_proc = -1;
}

void proc_create(u32 pid, u32 entry, u32 stack_pa)
{
    procs[pid].pid      = pid;
    procs[pid].pc       = entry;
    procs[pid].sp       = stack_pa + 4096u - 16u;
    procs[pid].state    = PROC_READY;
    procs[pid].stack_pa = stack_pa;
    procs[pid].nrun     = 0;
    procs[pid].brk      = 0;
    memset(procs[pid].regs, 0, sizeof(procs[pid].regs));
    procs[pid].regs[1] = procs[pid].sp;   /* x2(sp) 存在偏移 4，即 regs[1] */
    procs[pid].regs[10] = pid;            /* x10 = a0 = pid */
}

proc_t *proc_schedule(void)
{
    if (cur_proc >= 0 && procs[cur_proc].state == PROC_RUN)
        procs[cur_proc].state = PROC_READY;

    for (int i = 1; i <= NPROC; i++) {
        int idx = (cur_proc + i) % NPROC;
        if (procs[idx].state == PROC_READY) {
            procs[idx].state = PROC_RUN;
            cur_proc = idx;
            procs[idx].nrun++;
            return &procs[idx];
        }
    }
    return NULL;
}

/* 设置 CLINT mtimecmp，delta 为相对当前 mtime 的周期数 */
void proc_set_timer(u32 delta)
{
    volatile u64 *mtime    = (volatile u64 *)(CLINT_BASE + 0xBFF8u);
    volatile u64 *mtimecmp = (volatile u64 *)(CLINT_BASE + 0x4000u);
    *mtimecmp = *mtime + delta;
}
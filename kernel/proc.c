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

/* 前端进程视图：把进程表写进 RAM 最后一页。
   page_alloc 从 _kernel_end(0x80007000) 往上分配，永远碰不到这一页，
   所以不会和任何内核数据结构冲突。Web 端用 rv_read32 逐字读。 */
#define FRONT_PAGE 0x83FFF000u

void front_sync(void)
{
    volatile u32 *t = (volatile u32 *)FRONT_PAGE;
    t[0] = 0x52565350u;          /* 'RVSP' 魔数，前端用来确认表有效 */
    t[1] = NPROC;
    for (u32 i = 0; i < NPROC; i++) {
        t[2 + i * 4 + 0] = procs[i].pid;
        t[2 + i * 4 + 1] = procs[i].state;
        t[2 + i * 4 + 2] = procs[i].pc;
        t[2 + i * 4 + 3] = procs[i].nrun;
    }
}

proc_t *proc_schedule(void)
{
    proc_t *ret = NULL;

    if (cur_proc >= 0 && procs[cur_proc].state == PROC_RUN)
        procs[cur_proc].state = PROC_READY;

    for (int i = 1; i <= NPROC; i++) {
        int idx = (cur_proc + i) % NPROC;
        if (procs[idx].state == PROC_READY) {
            procs[idx].state = PROC_RUN;
            cur_proc = idx;
            procs[idx].nrun++;
            ret = &procs[idx];
            break;
        }
    }
    front_sync();
    return ret;
}

/* 设置 CLINT mtimecmp，delta 为相对当前 mtime 的周期数 */
void proc_set_timer(u32 delta)
{
    volatile u64 *mtime    = (volatile u64 *)(CLINT_BASE + 0xBFF8u);
    volatile u64 *mtimecmp = (volatile u64 *)(CLINT_BASE + 0x4000u);
    *mtimecmp = *mtime + delta;
}
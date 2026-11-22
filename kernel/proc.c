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
    /* fd 表必须初始化成「全空」。proc_init 把整个 procs 清零，fd_slot 全是 0，
       而 0 是合法的文件槽——开机进程的 4 个 fd 会全部指向文件槽 0，
       任何 open() 都拿不到 fd，read(0) 还会读到那个文件。 */
    for (int fd = 0; fd < 4; fd++) { procs[pid].fd_slot[fd] = -1; procs[pid].fd_pos[fd] = 0; }
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

/* 回收一个进程占用的全部物理页。
   用户 L1 里的每个有效 PTE 都指向本进程独占的页（代码页、缺页分配的堆页），
   逐个还给 page_free。内核映射那一层是从 g_kroot 复制来的共享页，绝不能碰。
   栈页不在这里回收：g_ustack[] 是开机一次性分配的固定数组，krestart 会直接复用，
   还回去反而会被分给别的用途。 */
void proc_reap(u32 pid)
{
    if (pid >= NPROC) return;
    proc_t *p = &procs[pid];
    if (p->pgtbl) {
        u32 uvpn2 = (0x80400000u >> 22) & 0x3FFu;
        u32 e = p->pgtbl[uvpn2];
        if (e & 1u) {
            u32 *ul1 = (u32 *)((e >> 10) << 12);
            for (u32 i = 0; i < 1024; i++)
                if (ul1[i] & 1u) page_free((void *)((ul1[i] >> 10) << 12));
            page_free(ul1);
        }
        page_free(p->pgtbl);
        p->pgtbl = 0;
    }
    p->code_pa = 0;
    p->code_pages = 0;
    p->stack_pa = 0;
    memset(p, 0, sizeof(*p));
    p->pid = pid;
    p->state = PROC_UNUSED;
    if (cur_proc == (int)pid) cur_proc = -1;
    front_sync();
    panel_sync_procs();
}

int proc_kill(u32 pid)
{
    if (pid >= NPROC) return -1;
    if (procs[pid].state == PROC_RUN) return -2;
    if (procs[pid].state == PROC_UNUSED) return -1;
    proc_reap(pid);
    return 0;
}

int proc_suspend(u32 pid)
{
    if (pid >= NPROC) return -1;
    if (procs[pid].state != PROC_READY) return -1;
    procs[pid].state = PROC_SUSPEND;
    front_sync();
    return 0;
}

int proc_resume(u32 pid)
{
    if (pid >= NPROC) return -1;
    if (procs[pid].state != PROC_SUSPEND) return -1;
    procs[pid].state = PROC_READY;
    front_sync();
    return 0;
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
    panel_sync_procs();   /* 用户进程在跑时 shell 主循环是挂起的，只能在这里刷 */
    return ret;
}

/* 设置 CLINT mtimecmp，delta 为相对当前 mtime 的周期数 */
void proc_set_timer(u32 delta)
{
    volatile u64 *mtime    = (volatile u64 *)(CLINT_BASE + 0xBFF8u);
    volatile u64 *mtimecmp = (volatile u64 *)(CLINT_BASE + 0x4000u);
    *mtimecmp = *mtime + delta;
}
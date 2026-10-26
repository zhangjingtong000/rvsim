#include "include/mmu.h"
#include "include/csr.h"
#include "include/bus.h"

/* Sv32 PTE 位 */
#define PTE_V 0x001u
#define PTE_R 0x002u
#define PTE_W 0x004u
#define PTE_X 0x008u
#define PTE_U 0x010u
#define PTE_G 0x020u
#define PTE_A 0x040u
#define PTE_D 0x080u

static bool read_phys32(u32 pa, u32 *v) { return bus_read(pa, 4, v); }
static bool write_phys32(u32 pa, u32 v) { return bus_write(pa, 4, v); }

bool mmu_translate(u32 satp, u8 priv, u32 mstatus, u32 va, acc_t acc,
                   u32 *pa_out, u32 *cause_out)
{
    u32 page_fault_cause = (acc == ACC_FETCH) ? CAUSE_FETCH_PAGE_FAULT
                          : (acc == ACC_LOAD)  ? CAUSE_LOAD_PAGE_FAULT
                                               : CAUSE_STORE_PAGE_FAULT;

    /* 有效特权级：M 模式且 MPRV=1 时按 MPP 翻译 */
    u8 eff = priv;
    if (priv == PRV_M && (mstatus & MSTATUS_MPRV))
        eff = (u8)((mstatus & MSTATUS_MPP) >> 11);

    /* satp.MODE == 0 (Bare) 或 M 模式取指（MPRV 不影响取指） -> 直接映射 */
    bool bare = ((satp >> 31) == 0);
    if (acc == ACC_FETCH && priv == PRV_M) bare = true;
    if (bare || eff == PRV_M) {
        *pa_out = va;
        return true;
    }

    u32 root = (satp & 0x003FFFFFu) << 12;
    u32 a = root;
    u32 pte = 0;

    for (int i = 1; i >= 0; i--) {
        u32 vpn = (va >> (12 + 10 * i)) & 0x3FFu;
        u32 pte_addr = a + vpn * 4u;
        if (!read_phys32(pte_addr, &pte)) { *cause_out = page_fault_cause; return false; }
        if (!(pte & PTE_V)) { *cause_out = page_fault_cause; return false; }
        if (!(pte & PTE_R) && (pte & PTE_W)) { *cause_out = page_fault_cause; return false; }

        bool leaf = (pte & PTE_R) || (pte & PTE_X);
        if (!leaf) {
            a = (pte >> 10) << 12;
            if (i == 0) { *cause_out = page_fault_cause; return false; }
            continue;
        }

        /* 超页检查：4 MiB 超页要求 ppn[0] == 0 */
        if (i > 0) {
            u32 mask = (1u << (10 * i)) - 1u;
            if ((pte >> 10) & mask) { *cause_out = page_fault_cause; return false; }
        }

        /* 权限判定 */
        bool u_page = (pte & PTE_U) != 0;
        bool sum = (mstatus & MSTATUS_SUM) != 0;
        bool mxr = (mstatus & MSTATUS_MXR) != 0;

        /* U 位检查 */
        if (eff == PRV_U) {
            if (!u_page) { *cause_out = page_fault_cause; return false; }
        } else {
            /* S 模式：非 U 页可直接访；U 页仅当 SUM=1 且是数据访问 */
            if (u_page) {
                if (acc == ACC_FETCH) { *cause_out = page_fault_cause; return false; }
                if (!sum) { *cause_out = page_fault_cause; return false; }
            }
        }

        /* R/W/X 检查 */
        if (acc == ACC_FETCH) {
            if (!(pte & PTE_X)) { *cause_out = page_fault_cause; return false; }
        } else if (acc == ACC_LOAD) {
            bool readable = (pte & PTE_R) || (mxr && (pte & PTE_X));
            if (!readable) { *cause_out = page_fault_cause; return false; }
        } else {
            if (!(pte & PTE_W)) { *cause_out = page_fault_cause; return false; }
        }

        /* A/D：硬件置位（不走 Svade） */
        u32 npte = pte;
        if (!(pte & PTE_A)) npte |= PTE_A;
        if (acc == ACC_STORE && !(pte & PTE_D)) npte |= PTE_D;
        if (npte != pte) {
            if (!write_phys32(pte_addr, npte)) { *cause_out = page_fault_cause; return false; }
        }

        /* 物理地址拼接（4 KiB 页与 4 MiB 超页分别处理） */
        u32 pa;
        if (i == 0) {
            pa = ((pte >> 10) << 12) | (va & 0xFFFu);
        } else {
            u32 ppn1 = (pte >> 20) & 0xFFFu;
            pa = (ppn1 << 22) | (va & 0x003FFFFFu);
        }
        *pa_out = pa;
        return true;
    }
    *cause_out = page_fault_cause;
    return false;
}
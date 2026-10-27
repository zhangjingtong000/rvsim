#include "include/fdt.h"
#include "include/bus.h"
#include <stdio.h>
#include <string.h>

devmap_t g_devmap;

/* 编译期默认值（QEMU virt 布局）。没有 dtb 时就是这一套。 */
void devmap_defaults(void)
{
    memset(&g_devmap, 0, sizeof(g_devmap));
    g_devmap.ram_base    = RAM_BASE;
    g_devmap.ram_size    = RAM_SIZE;
    g_devmap.clint_base  = CLINT_BASE;
    g_devmap.plic_base   = PLIC_BASE;
    g_devmap.uart_base   = UART_BASE;
    g_devmap.syscon_base = SYSCON_BASE;
}

/* ── FDT 基本读取（全部大端） ── */
#define FDT_MAGIC       0xD00DFEEDu
#define FDT_BEGIN_NODE  1u
#define FDT_END_NODE    2u
#define FDT_PROP        3u
#define FDT_NOP         4u
#define FDT_END         9u

static u32 rd32(u32 pa)
{
    u32 v = 0;
    if (!bus_read(pa, 4, &v)) return 0;
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}

static u32 hdr_off_struct(u32 base) { return rd32(base + 8);  }
static u32 hdr_off_strings(u32 base) { return rd32(base + 12); }
static u32 hdr_totalsize(u32 base)   { return rd32(base + 4);  }

static const char *str_at(u32 strblock, u32 off)
{
    static char buf[64];
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 63; i++) {
        u32 v = 0;
        if (!bus_read(strblock + off + (u32)i, 1, &v)) break;
        buf[i] = (char)(v & 0xFF);
        if (buf[i] == 0) break;
    }
    return buf;
}

/* compatible 匹配：在 len 字节的属性值里找逗号分隔的字符串列表 */
static int compat_has(u32 data_pa, u32 len, const char *want)
{
    char buf[256];
    u32 n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    for (u32 i = 0; i < n; i++) {
        u32 v = 0;
        if (!bus_read(data_pa + i, 1, &v)) { n = i; break; }
        buf[i] = (char)(v & 0xFF);
    }
    buf[n] = 0;
    /* buf 里是若干 NUL 分隔的字符串 */
    u32 i = 0;
    while (i < n) {
        const char *s = buf + i;
        size_t l = strlen(s);
        if (l > 0 && strcmp(s, want) == 0) return 1;
        if (l > 0 && strstr(s, want) != NULL) return 1;
        i += (u32)l + 1;
    }
    return 0;
}

int fdt_check(u32 dtb_phys)
{
    if (dtb_phys == 0) return 0;
    u32 magic = rd32(dtb_phys);
    u32 total = hdr_totalsize(dtb_phys);
    if (magic != FDT_MAGIC) return 0;
    if (total < 40 || total > 4u * 1024u * 1024u) return 0;
    return 1;
}

/* 当前节点的分类（由 compatible 决定） */
enum { NODE_OTHER = 0, NODE_MEMORY, NODE_PLIC, NODE_CLINT, NODE_UART, NODE_SYSCON };

int fdt_apply(u32 dtb_phys)
{
    if (!fdt_check(dtb_phys)) {
        printf("[fdt] 无可用 dtb（magic=%08x），设备地址用编译期默认值\n", rd32(dtb_phys));
        return 0;
    }

    u32 off_struct  = hdr_off_struct(dtb_phys);
    u32 off_strings = hdr_off_strings(dtb_phys);
    u32 pos         = dtb_phys + off_struct;
    u32 strblock    = dtb_phys + off_strings;
    u32 total       = hdr_totalsize(dtb_phys);
    u32 end         = dtb_phys + total;

    int  kind = NODE_OTHER;
    u32  reg_base = 0, reg_size = 0;
    int  have_reg = 0;
    int  applied = 0;

    printf("[fdt] 解析 dtb @ 0x%08x（totalsize=%u）\n", dtb_phys, total);

    while (pos + 4 <= end) {
        u32 token = rd32(pos);
        pos += 4;

        if (token == FDT_BEGIN_NODE) {
            /* 跳过节点名 */
            int i = 0;
            for (; i < 255; i++) {
                u32 v = 0;
                if (!bus_read(pos + (u32)i, 1, &v)) break;
                if ((v & 0xFF) == 0) break;
            }
            pos += ((u32)i + 1u + 3u) & ~3u;
            kind = NODE_OTHER;
            have_reg = 0;
            reg_base = reg_size = 0;
        }
        else if (token == FDT_END_NODE) {
            /* 节点结束：如果这个节点的 compatible 认出来了、又有 reg，就应用 */
            if (have_reg) {
                switch (kind) {
                case NODE_MEMORY:
                    /* RAM 的物理布局由编译期静态数组 g_ram[RAM_SIZE] 决定，运行时不能改基址。
                       所以这里不做"采用"，只做一致性校验：dtb 说的 RAM 与编进模拟器的
                       不一致时明确警告，避免出现"dtb 改了但程序照旧"的静默不一致。 */
                    applied++;
                    printf("[fdt]   memory: base=0x%08x size=0x%08x", reg_base, reg_size);
                    if (reg_base != RAM_BASE || reg_size != RAM_SIZE) {
                        printf("  [警告] 与编译期 (%08x/%08x) 不一致；"
                               "RAM 基址与容量是编译期常量，不会被 dtb 覆盖\n",
                               RAM_BASE, RAM_SIZE);
                    } else {
                        printf("（与编译期一致）\n");
                    }
                    break;
                case NODE_PLIC:
                    g_devmap.plic_base = reg_base; g_devmap.plic_from_dtb = 1; applied++;
                    printf("[fdt]   plic  : base=0x%08x size=0x%08x\n", reg_base, reg_size);
                    break;
                case NODE_CLINT:
                    g_devmap.clint_base = reg_base; g_devmap.clint_from_dtb = 1; applied++;
                    printf("[fdt]   clint : base=0x%08x size=0x%08x\n", reg_base, reg_size);
                    break;
                case NODE_UART:
                    g_devmap.uart_base = reg_base; g_devmap.uart_from_dtb = 1; applied++;
                    printf("[fdt]   uart  : base=0x%08x size=0x%08x\n", reg_base, reg_size);
                    break;
                case NODE_SYSCON:
                    g_devmap.syscon_base = reg_base; applied++;
                    printf("[fdt]   syscon: base=0x%08x size=0x%08x\n", reg_base, reg_size);
                    break;
                default: break;
                }
            }
            kind = NODE_OTHER;
            have_reg = 0;
        }
        else if (token == FDT_PROP) {
            u32 len     = rd32(pos);
            u32 nameoff = rd32(pos + 4);
            u32 data    = pos + 8;
            pos += 8 + ((len + 3u) & ~3u);

            const char *pname = str_at(strblock, nameoff);

            if (strcmp(pname, "device_type") == 0 && len >= 6) {
                char t[16] = {0};
                for (u32 i = 0; i < len && i < 15; i++) {
                    u32 v = 0;
                    if (!bus_read(data + i, 1, &v)) break;
                    t[i] = (char)(v & 0xFF);
                }
                if (strncmp(t, "memory", 6) == 0) kind = NODE_MEMORY;
            }
            else if (strcmp(pname, "compatible") == 0) {
                if (compat_has(data, len, "riscv,plic0") ||
                    compat_has(data, len, "sifive,plic-1.0.0") ||
                    compat_has(data, len, "riscv,plic"))
                    kind = NODE_PLIC;
                else if (compat_has(data, len, "riscv,clint0") ||
                         compat_has(data, len, "sifive,clint0"))
                    kind = NODE_CLINT;
                else if (compat_has(data, len, "ns16550a") ||
                         compat_has(data, len, "ns16550") ||
                         compat_has(data, len, "sifive,uart0"))
                    kind = NODE_UART;
                else if (compat_has(data, len, "sifive,test0") ||
                         compat_has(data, len, "syscon"))
                    kind = NODE_SYSCON;
            }
            else if (strcmp(pname, "reg") == 0 && len >= 8) {
                reg_base = rd32(data);
                reg_size = rd32(data + 4);
                have_reg = 1;
            }
        }
        else if (token == FDT_NOP) {
            continue;
        }
        else if (token == FDT_END) {
            break;
        }
        else {
            printf("[fdt] 未知 token 0x%x @ 0x%08x，停止解析\n", token, pos - 4);
            break;
        }
    }

    printf("[fdt] 从 dtb 应用了 %d 个设备项\n", applied);
    return applied;
}
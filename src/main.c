#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "include/cpu.h"
#include "include/bus.h"
#include "include/loader.h"

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int s) { (void)s; g_stop = 1; }

static void usage(const char *p)
{
    fprintf(stderr,
        "用法: %s [选项]\n"
        "  --elf <path>         加载 ELF32（riscv-tests）\n"
        "  --image <path>       加载裸 Image 到 0x80000000\n"
        "  --dtb <path>         加载 dtb 到指定地址（默认 0x87e00000）\n"
        "  --dtb-addr <hex>     dtb 物理地址\n"
        "  --max-cycles <n>     指令数上限（默认 50000000）\n"
        "  --tohost-addr <hex>  手动指定 tohost 地址\n"
        "  --trace              打印每条指令（慢）\n",
        p);
}

/* 检查 tohost：1=PASS，奇数>1=FAIL，否则 = 0（未结束） */
static int check_tohost(cpu_t *c, u32 tohost)
{
    if (!tohost) return 0;
    if (tohost < RAM_BASE || tohost >= RAM_BASE + RAM_SIZE) return 0;
    u32 v = 0;
    bus_read(tohost, 4, &v);
    if (v == 0) return 0;
    if (v == 1) return 1;
    if (v & 1) {
        fprintf(stderr, "  FAIL (test #%u)\n", (v - 1) / 2);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *elf = NULL, *image = NULL, *dtb = NULL;
    u32 dtb_addr = 0x87E00000u;
    u32 tohost_override = 0;
    bool trace = false;
    u64 max_cycles = 50000000ull;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--elf") && i + 1 < argc)       elf = argv[++i];
        else if (!strcmp(argv[i], "--image") && i + 1 < argc) image = argv[++i];
        else if (!strcmp(argv[i], "--dtb") && i + 1 < argc)   dtb = argv[++i];
        else if (!strcmp(argv[i], "--dtb-addr") && i + 1 < argc) dtb_addr = (u32)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--max-cycles") && i + 1 < argc) max_cycles = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--tohost-addr") && i + 1 < argc) tohost_override = (u32)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--trace")) trace = true;
        else { usage(argv[0]); return 2; }
    }
    if (!elf && !image) { usage(argv[0]); return 2; }

    signal(SIGINT, on_sigint);
    bus_init();

    cpu_t cpu;
    cpu_reset(&cpu);
    cpu.max_cycles = max_cycles;

    u32 tohost = tohost_override;
    if (elf) {
        if (!loader_load_elf(elf, &cpu.img)) return 1;
        cpu.pc = cpu.img.entry;
        if (!tohost && cpu.img.has_tohost) tohost = cpu.img.tohost_addr;
        printf("[rvsim] ELF=%s entry=0x%08x 装载=%u 字节 tohost=0x%08x%s\n",
               elf, cpu.pc, cpu.img.loaded_bytes, tohost,
               cpu.img.has_tohost ? "" : " (未找到符号)");
    } else {
        u32 entry = 0, sz = 0;
        if (!loader_load_raw(image, RAM_BASE, &entry, &sz)) return 1;
        cpu.pc = entry;
        printf("[rvsim] Image=%s entry=0x%08x 大小=%u 字节\n", image, entry, sz);
        /* Linux M 模式启动约定：a0=hartid, a1=dtb 物理地址 */
        cpu.x[10] = 0;
        if (dtb) {
            u32 dsz = 0, dent = 0;
            if (!loader_load_raw(dtb, dtb_addr, &dent, &dsz)) return 1;
            cpu.x[11] = dtb_addr;
            printf("[rvsim] dtb 已装入 0x%08x (%u 字节)\n", dtb_addr, dsz);
        }
        cpu.x[2] = RAM_BASE + RAM_SIZE - 16;   /* sp */
    }

    printf("[rvsim] 开始执行，max_cycles=%llu\n", (unsigned long long)max_cycles);

    int result = 0;
    u64 steps = 0;
    while (cpu.running && !cpu.halted && !g_stop) {
        if (!cpu_step(&cpu)) break;
        steps++;
        if (trace) {
            printf("  pc=%08x insn=%08x priv=%d\n", cpu.pc, cpu.cur_instr, cpu.csr.priv);
        }
        /* tohost 检查：每 64 步看一次，避免拖慢 */
        if ((steps & 63) == 0) {
            int r = check_tohost(&cpu, tohost);
            if (r != 0) { result = r; cpu.halted = true; break; }
        }
        /* syscon 关机 */
        if (g_syscon.poweroff || g_syscon.reboot) {
            printf("\n[rvsim] syscon %s\n", g_syscon.poweroff ? "poweroff" : "reboot");
            cpu.halted = true;
            break;
        }
        bus_tick(&cpu.csr, cpu.cycles);
    }

    /* 循环结束后再检查一次 tohost */
    if (result == 0) {
        int r = check_tohost(&cpu, tohost);
        if (r != 0) result = r;
    }

    printf("\n[rvsim] 停止：执行 %llu 条指令，cycles=%llu，priv=%d，pc=0x%08x\n",
           (unsigned long long)steps, (unsigned long long)cpu.cycles, cpu.csr.priv, cpu.pc);

    if (result == 1) printf("[rvsim] 结果：PASS\n");
    else if (result == -1) printf("[rvsim] 结果：FAIL\n");
    else printf("[rvsim] 结果：未判定（tohost 未写入终值）\n");

    return (result == 1) ? 0 : 1;
}
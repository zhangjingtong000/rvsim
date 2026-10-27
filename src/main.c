#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include "include/cpu.h"
#include "include/bus.h"
#include "include/fdt.h"
#include "include/loader.h"
#include "include/disasm.h"
#include "include/bus.h"

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
    (void)c;
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
    u32 dtb_addr = 0x83E00000u;   /* 60 MB 处，避开内核且不超 64 MB RAM */
    u32 tohost_override = 0;
    bool trace = false;
    u64 max_cycles = 50000000ull;

    u32 dump_addr = 0, dump_len = 0;
    u32 dis_addr = 0, dis_count = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disasm") && i + 2 < argc) {
            dis_addr  = (u32)strtoul(argv[++i], NULL, 0);
            dis_count = (u32)strtoul(argv[++i], NULL, 0);
            continue;
        }
        if (!strcmp(argv[i], "--dump-mem") && i + 2 < argc) {
            dump_addr = (u32)strtoul(argv[++i], NULL, 0);
            dump_len  = (u32)strtoul(argv[++i], NULL, 0);
            continue;
        }
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

    /* 先填编译期默认值；若随后成功解析 dtb，会被覆盖（FR-2.8） */
    devmap_defaults();

    signal(SIGINT, on_sigint);
    /* stdin 非阻塞，供 UART 输入 */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
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
        cpu.x[2] = RAM_BASE + RAM_SIZE - 16;   /* sp */
    }

    /* FR-2.8：设备地址从 dtb 读取。无论加载 ELF 还是 Image，只要给了 --dtb 就解析。
       解析结果覆盖 devmap_defaults() 填的编译期默认值。 */
    if (dtb) {
        u32 dsz = 0, dent = 0;
        if (!loader_load_raw(dtb, dtb_addr, &dent, &dsz)) return 1;
        cpu.x[11] = dtb_addr;
        printf("[rvsim] dtb 已装入 0x%08x (%u 字节)\n", dtb_addr, dsz);
        fdt_apply(dtb_addr);
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
        /* 检测到 shell 提示符后，依次执行几条命令 */
        if (g_shell_prompt_seen) {
            g_shell_prompt_seen = false;
            static int idx = 0;
            static const char *cmds[] = {
                "uname -a\n", "ls /\n", "cat /proc/cpuinfo\n", "free\n", "ps\n"
            };
            if (idx < (int)(sizeof(cmds)/sizeof(cmds[0]))) {
                for (const char *q = cmds[idx]; *q; q++) uart_push_rx((u8)*q);
                idx++;
            }
        }
        /* 检测到登录提示符后，自动输入用户名与密码 */
        {
            static bool fed_user = false, fed_pass = false;
            if (g_login_seen && !fed_user) {
                fed_user = true;
                const char *u2 = "root\n";
                for (const char *q = u2; *q; q++) uart_push_rx((u8)*q);
            }
            if (g_password_seen && !fed_pass) {
                fed_pass = true;
                const char *p2 = "root\n";
                for (const char *q = p2; *q; q++) uart_push_rx((u8)*q);
            }
        }
        /* 每 4096 步从 stdin 读一次，喂给 UART */
        if ((steps & 4095) == 0) {
            u8 buf[64];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            for (ssize_t k = 0; k < n; k++) uart_push_rx(buf[k]);
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
    else if (g_syscon.poweroff)
        printf("[rvsim] 结果：程序主动关机（syscon poweroff，视为正常结束）\n");
    else
        printf("[rvsim] 结果：未判定（tohost 未写入终值）\n");

    if (dis_count) {
        printf("\n[rvsim] 反汇编 @ 0x%08x（%u 条）\n", dis_addr, dis_count);
        for (u32 k = 0; k < dis_count; k++) {
            u32 a = dis_addr + k * 4, insn = 0;
            bus_read(a, 4, &insn);
            char txt[96];
            rv_disasm(a, insn, txt, sizeof txt);
            printf("  %08x: %08x  %s\n", a, insn, txt);
        }
    }

    if (dump_len) {
        printf("\n[rvsim] 内存 dump @ 0x%08x (%u 字节)\n", dump_addr, dump_len);
        for (u32 off = 0; off < dump_len; off += 16) {
            printf("  %08x: ", dump_addr + off);
            for (u32 k = 0; k < 16 && off + k < dump_len; k++) {
                u32 v = 0;
                bus_read(dump_addr + off + k, 1, &v);
                printf("%02x ", v & 0xFF);
            }
            printf("\n");
        }
    }

    /* 退出码约定：0 = 成功。
       - tohost==1（riscv-tests PASS）
       - 或程序主动 syscon poweroff（CoreMark 等以关机表示跑完）
       其余（失败、超 max_cycles 仍未结束）返回 1。 */
    if (result == 1) return 0;
    if (g_syscon.poweroff) return 0;
    return 1;
}
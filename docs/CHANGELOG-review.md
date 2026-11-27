# rvsim 开发记录：外部评审修复、踩坑与补充成果

本文件收录从 README 移出的开发过程材料：2026-10-27 外部评审后的修复记录、踩过的坑、FR-2.8 的完整验证方法，以及同期的补充成果。面向读者的项目介绍、构建方式与测试口径在 `README.md`；AI 使用说明在 `docs/ai-guide.md`。

时间线：2026-10-27 外部评审（76/100）→ 修复；2026-11 桌面外壳与界面 e2e。

## 一、2026-10-27 外部评审修复记录

评审指出以下问题，均已修复：

| 问题 | 修复 | 验证 |
|---|---|---|
| 上下文切换没真正切 `sp`（`lw t0,4(sp)` 读了却没用） | `s_entry.S` 加 `csrw sscratch, t0` | 新增栈校验测试：A/B/C 各 20 次、无破坏标记 |
| `proc_create` 把 sp 写进 `regs[2]`（x3/gp），应为 `regs[1]`（x2/sp） | 改索引 | 同上；此前 sp 一直是 0，只因演示程序不用栈才没暴露 |
| `proc_schedule()` 在 `s_main` 里被调两次，实际从 proc 1 起跑 | 删掉多余那次 | `[S] 启动 proc 0`，A/B/C 各 25 次完全均匀 |
| `mcounteren` 只存不判 | 新增 `counter_ok()` | `tests/directed/t_mcounteren` → T |
| `sstatus.FS` 可写（无 F 扩展应 read-only 0） | `SSTATUS_MASK` 去掉 FS/XS | `tests/directed/t_sstatus_fs` → N |
| `mcycle` 写入被执行循环覆盖 | 写后同步 `c->cycles` | `tests/directed/t_mcycle` → Y |
| commit 声称 FR-7.5 完成但 shell 没有 `run` | 真正实现 `run`（重置+调度+进 U） | 敲 `run` 后第二轮 A/B/C 各 20 次 |
| trap 返回把 x5 覆盖成 sp（`lw t0,4(sp)` 覆写刚恢复的 t0） | 读 sp 后再 `lw t0,16(sp)` 补回；M 模式删掉多余的那行 | 用户程序埋 `t0=0x0BADF00D` 哨兵跨 ecall 校验 → 20/20/20 无 `X` |
| `run_tests.sh` 硬编码 `/home/rvsim`，且 0 个用例也打印"全部通过" | 路径按脚本位置推导；`total==0` 直接报错退出 1 | `ISA_DIR=/tmp/nonexistent` → 报错 exit 1 |
| CoreMark 跑完 `j .` 空转到 max_cycles，"未判定" + exit 1 | `start_cm.S` 改为写 syscon 关机；主程序把 syscon 关机视为正常结束 | CoreMark 32 秒退出、exit 0、"程序主动关机" |

## 二、踩过的坑

- `csrr a0, mhartid` 是 CSRRS + `rs1=x0`，**不该发起写**，否则误判 illegal
- AMO 的 `funct7` 含 `aq`/`rl` 两位，必须用 `funct5`
- `JAL`/`JALR` 目标 misaligned 时 **`rd` 不得写回**（先检查再写回）
- `wfi` 若 `return` 而不推进 `pc` 会原地死循环
- `mie` 的可写掩码漏了 bit1（SSIP）会导致中断永远开不起来
- PLIC 的 claim 在 context 块 `+0x4`，不是 `+0x0`
- UART 的 IIR 必须基于自身 THR 状态，**不能依赖 PLIC pending**（claim 会先清 pending）
- `linker.ld` 必须收 `*(.sbss)`，否则小 bss 变量落在 `_kernel_end` 之外，被 `page_alloc` 自己清零
- `s_trap_entry` 必须用 `sscratch` 换到内核栈，否则 S 模式写用户栈触发 page fault 无限循环

## 三、FR-2.8：设备地址从 dtb 读取

`src/fdt.c`（219 行）解析 FDT：校验 header magic，遍历 structure block，按 `compatible` 属性识别节点（不是按节点名——plic 的节点名是 `interrupt-controller@c000000`，按名字匹配会漏），从 `reg` 读 <base,size>，写进运行时表 `g_devmap`。所有外设访问都走这张表。无 `--dtb` 时回退到编译期默认值（QEMU virt 布局），所以 riscv-tests、CoreMark、Linux 都不受影响。

**验证方法（可复现）**：

```bash
# 正常 dtb：解析出 5 个设备
./build/rvsim --elf tests/riscv-tests/isa/rv32ui-p-add --dtb dts/minimal.dtb --max-cycles 200
# [fdt] memory/clint/plic/uart/syscon … 从 dtb 应用了 5 个设备项

# 改一份 dtb 把 plic 挪到 0x0D000000、uart 挪到 0x11000000
sed -e 's/0x0C000000 0x400000/0x0D000000 0x400000/' \
    -e 's/serial@10000000/serial@11000000/' \
    -e 's/0x10000000 0x100/0x11000000 0x100/' dts/minimal.dts > /tmp/alt.dts
dtc -I dts -O dtb -o /tmp/alt.dtb /tmp/alt.dts

# 解析结果跟着变
./build/rvsim --elf ... --dtb /tmp/alt.dtb --max-cycles 200
# [fdt] plic : base=0x0d000000 / uart : base=0x11000000

# 实际访问也变：内核硬编码访问 0x10000000，UART 被挪走后应完全无输出
./build/rvsim --elf build/kernel.elf --dtb dts/minimal.dtb --max-cycles 500000   # 有输出
./build/rvsim --elf build/kernel.elf --dtb /tmp/alt.dtb    --max-cycles 500000   # 无输出
```

**边界**：`g_ram[]` 是编译期静态数组，所以 dtb 里 `memory` 节点的 **base 和 size 都不会被采用**——解析出来只做**一致性校验**，与编译期值不符时打印警告。改 RAM 基址或容量必须重新编译。设备大小（`UART_SIZE`/`PLIC_SIZE`/`CLINT_SIZE`）同理是编译期常量，dtb 只覆盖基址。理由：RISC-V 的 RAM 布局决定了 `g_ram[]` 的数组下标计算，运行时改不了；MMIO 窗口大小在外设实现里也是固定的。

## 四、补充成果（2026-10-27）

### 4.1 定时器中断驱动的抢占式调度

不再依赖用户程序主动 `yield`，改由 CLINT 定时器中断强制切换。

**架构**：M 模式收到 `MTIP` 只做两件事——把 `mtimecmp` 推到远未来避免重复触发、置 `mip.STIP`；S 模式收到 `STI` 时保存当前进程全部 32 个寄存器、调度下一个、再用 `ecall(SBI='SSIP')` 请 M 模式清 `STIP` 并重装定时器。

**关键修复**（每一个都是"不修就永远不工作"）：

- `mideleg` 必须委派 STIP，否则中断进不了 S 模式
- `enter_s_mode` 必须置 `MPIE`，否则 `mret` 后 `MIE` 被清、定时器永不触发
- `enter_u_mode` 必须置 `SPIE`，否则 `sret` 后 `SIE` 被清、S 模式中断永久关闭
- M/S 两套栈帧布局必须统一为 `xN 在 (N-1)*4`，否则 handler 写参数会串到别的寄存器
- SBI `ecall` 会穿越 M 模式并恢复寄存器，**局部变量必须放静态区**，否则返回值被覆盖

**实测**（`--max-cycles 2000000`）：

```
BBBBBBBBBBCCCCCCCCCCCCCAAAAAAAAAAAAABBBBBBBBBBBBCCCCCCCCCCCCAAAAAAAAAAAABBBBBBBBBBBBCCCCCCCCCCCCCAAAAAAAAAAAAA
37 A / 38 B / 38 C
```

### 4.2 缺页异常处理（按需分配）

用户页表初始化时只映射代码页，数据页与栈页不预先映射。S 模式 handler 收到 `cause 13/15` 后分配物理页、填 PTE（`U|R|W|A|D`）、返回原 `epc` 重试指令；非法地址则终止进程。

**实测**：100 万条内触发 **4 次 store page fault**，进程仍正常轮转。

### 4.3 CoreMark

```
CoreMark Size    : 666
Total ticks      : 125632192
Total time (secs): 12
Iterations/Sec   : 333
Iterations       : 4000
Compiler flags   : -O2 -march=rv32ima_zicsr_zifencei
seedcrc          : 0x18f2
[0]crclist       : 0xe3c1
[0]crcmatrix     : 0x0747
[0]crcstate      : 0x8d84
[0]crcfinal      : 0x5249
Correct operation validated.
```

**分数 333 iters/sec** 是按 guest 时间算的（`MTIME_DIV=10`，guest 1 秒约为墙钟 4 秒），折算墙钟约 80 iters/sec。可信的是四个 CRC 值。

**折磨了很久的根因**：`core_portme.h` 里 `MULTITHREAD` 误写为 `0`。CoreMark 用 `for (i = 0; i < MULTITHREAD; i++)` 做所有初始化，为 0 时一条都不执行——`core_list_init` 没被调用、`list` 保持 NULL，程序在 `core_bench_list` 解引用空指针。表现为"跑几十亿条指令也不出结果"，极难从现象反推。

### 4.4 可视化增强

`http://127.0.0.1:8777`（`cd web && python3 -m http.server 8777`）

- **反汇编**：`src/disasm.c`（180 行），覆盖 RV32IMA 全部指令 + CSR 名，显示 PC 附近 13 条并高亮当前指令
- **指令阶段视图**：IF/ID/EX/MEM/WB 五个阶段按指令类别动态点亮——`STORE` 亮 MEM、`LOAD` 亮 MEM+WB、分支只亮 EX、ALU 亮 EX+WB
- **内存热图**：4 KB 页粒度，512 格，读绿写橙、亮度代表访问频次，悬停显示页号与读写次数
- **寄存器双列布局** + CSR 表 + PC 附近内存

### 4.5 与 QEMU 的语义对照

`./tests/qemu_crosscheck.sh` —— 同一份 CoreMark ELF 分别喂给 rvsim 和 QEMU，比对四个 CRC 值。

| 项 | rvsim | QEMU |
|---|---|---|
| CoreMark Size | 666 | 666 |
| seedcrc | 0x18f2 | 0x18f2 |
| crclist | 0xe3c1 | 0xe3c1 |
| crcmatrix | 0x0747 | 0x0747 |
| crcstate | 0x8d84 | 0x8d84 |
| crcfinal | 0x5249 | 0x5249 |
| Total ticks | 125632192 | 10020861 |

脚本只比 CRC，不比时间：QEMU 侧会打印 `ERROR! Must execute for at least 10 secs for a valid result!` 和 `Errors detected`（它跑得比 10 秒门槛快），脚本靠 `timeout 280` 结束。

**计算结果完全一致，只有性能不同**。这比"能启动 Linux"更强的证据：CRC 是对链表操作、矩阵乘法、状态机转换全部结果的压缩摘要，全都对得上说明指令语义、异常处理、内存行为一致。

### 4.6 新增调试工具

`--dump-mem <addr> <len>`：运行结束后 dump 指定物理内存。定位 CoreMark 问题时靠它读出 `core_list_init` 的真实入参，比反复加 printf 可靠得多。

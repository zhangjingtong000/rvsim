# rvsim · RISC-V 模拟器与迷你内核

从零实现的 RV32IMA 模拟器 + 迷你内核 + 可视化调试界面。本科项目项目。

## 当前状态（2026-10-26）

| 验收门 | 目标 | 状态 | 证据 |
|---|---|---|---|
| Gate 0 环境 | 工具链可用、82 个测试可编译 | ✅ | `build/` 产物 |
| Gate 1 最小可执行环境 | ELF 加载 + trap + U 模式 + tohost | ✅ | `rv32ui-p-add` PASS |
| Gate 2 RV32I | 42/42 | ✅ | `./tests/run_tests.sh rv32ui` |
| Gate 3 RV32M + RV32A | 8/8 + 10/10 | ✅ | `./tests/run_tests.sh rv32um` / `rv32ua` |
| Gate 4 特权级与计数器 | 16/16 | ✅ | `./tests/run_tests.sh rv32mi` |
| Gate 5 S 模式 + Sv32 | 6/6 | ✅ | `./tests/run_tests.sh rv32si` |
| Gate 6 启动 Linux | 进 shell + 执行命令 | ✅ | `uname -a` / `ls /` / `cat /proc/cpuinfo` / `free` / `ps` |
| Gate 7a 迷你内核 | M→S→U + Sv32 + 多进程 + 系统调用 | ✅ | 3 进程 `ABCABC...` 轮转 |
| **抢占式调度** | 定时器中断驱动，非 yield | ✅ | `B→C→A` 轮转，37/38/38 均衡 |
| **缺页处理** | 按需分配物理页 | ✅ | 实测 4 次 store page fault 后正常继续 |
| **CoreMark** | 跑通并给出分数 | ✅ | **333 iters/sec**，`Correct operation validated` |
| **可视化增强** | 反汇编 + 阶段视图 + 热图 | ✅ | 13 行反汇编、5 阶段动态点亮、512 格热图 |
| **QEMU 语义对照** | 与 QEMU 比对计算结果 | ✅ | 四个 CRC 值完全一致 |
| Gate 7b WASM 可视化 | 浏览器调试界面 | ✅ | 29 KB wasm，Node + 浏览器双验证 |

**riscv-tests：82/82 全通过。**

## 数字

| 项 | 值 |
|---|---|
| 模拟器源码 | 1,719 行（`src/`） |
| 迷你内核 | 516 行（`kernel/`） |
| 解释速度 | **32 MIPS**（1000 万条 0.31 秒） |
| WASM 体积 | 29 KB |
| Linux 启动 | 6.1.14，约 3 亿条指令到 shell |

## 目录

```
src/          模拟器：CPU/CSR/trap/MMU/总线/外设/加载器
kernel/       迷你内核：M 模式启动、Sv32、S/U 切换、系统调用
web/          WASM 可视化前端
dts/          自写设备树
tests/        riscv-tests + 批量跑分器
docs/         需求文档、设计文档、调研
```

## 运行

```bash
# 回归（82 个测试）
./tests/run_tests.sh all

# 跑单个测试
./build/rvsim --elf tests/riscv-tests/isa/rv32ui-p-add

# 启动 Linux
./build/rvsim --image third_party/linux-image/Image --dtb dts/minimal.dtb \
  --max-cycles 4000000000 < /dev/null

# 迷你内核
./build/rvsim --elf build/kernel.elf

# 可视化（先编译 WASM）
emcc -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=createRvsim \
  -s EXPORTED_FUNCTIONS='["_rv_reset","_rv_load","_rv_step","_rv_pc","_rv_reg","_rv_priv","_rv_cycles","_rv_result","_rv_insn","_rv_csr","_rv_mem","_rv_tohost","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' \
  -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=134217728 \
  -I src/include -o web/rvsim.js \
  web/wasm_entry.c src/exec.c src/csr.c src/bus.c src/mmu.c src/loader_mem.c
cd web && python3 -m http.server 8777
```

## Gate 7a 的完成度（如实说明）

**已做到**：
- M 模式启动 → 自建 Sv32 恒等页表 → `mret` 进 S 模式 → `sret` 进 U 模式
- **3 个进程轮转**（`ABCABC...`），`proc_schedule` 时间片轮转
- **完整上下文切换**：`s_trap_entry` 保存 x1-x31 + 用户 `sp`，切换时整组恢复
- **4 个系统调用**：`write` / `exit` / `getpid` / `yield`
- `sscratch` 换栈 + `SUM=1` 读用户内存
- 优雅关机（syscon）

**尚未做到**（设计文档 FR-7 的完整要求）：
- 缺页异常处理（当前页表静态建好，未做按需分配）
- 定时器中断驱动的抢占式调度（`m_trap_handler` 的 MTIP 逻辑已写，但为避免与 yield 抢 pc 保存而暂未启用）
- 内核内 shell**

## 代码结构要点

- **CSR 只有一份物理存储**：`sstatus`/`sie`/`sip` 是 `mstatus`/`mie`/`mip` 的掩码视图
- **`mepc` = 触发 trap 的指令地址**，`+4` 由 handler 自己做
- **`mret`/`sret`**：`xIE←xPIE`、`xPIE←1`、`xPP←U`、`MPRV←0`，`sret` 读 `SPP`
- **总线**：先判 RAM 再逐设备匹配区间（不用高位折叠，避免别名）
- **misaligned load/store 由硬件分解**，取指 misaligned 抛 cause 0 且 `rd` 不写回
- **Sv32**：A/D 硬件置位、超页 `ppn` 检查、`SUM`/`MXR`/`MPRV`

## 已知限制

1. 不做 C/F/D/B/V 扩展
2. 不做 JIT（32 MIPS 解释执行）
3. 单 hart
4. PMP 不做强制（`rv32mi-p-pmpaddr` 在 G=0 时直接 pass，验证强度为零）
5. 不做 `-v` 测试变体（需 newlib）
6. QEMU 对照只做语义一致，不做逐字节

## 踩过的坑（值得记录）

- `csrr a0, mhartid` 是 CSRRS + `rs1=x0`，**不该发起写**，否则误判 illegal
- AMO 的 `funct7` 含 `aq`/`rl` 两位，必须用 `funct5`
- `JAL`/`JALR` 目标 misaligned 时 **`rd` 不得写回**（先检查再写回）
- `wfi` 若 `return` 而不推进 `pc` 会原地死循环
- `mie` 的可写掩码漏了 bit1（SSIP）会导致中断永远开不起来
- PLIC 的 claim 在 context 块 `+0x4`，不是 `+0x0`
- UART 的 IIR 必须基于自身 THR 状态，**不能依赖 PLIC pending**（claim 会先清 pending）
- `linker.ld` 必须收 `*(.sbss)`，否则小 bss 变量落在 `_kernel_end` 之外，被 `page_alloc` 自己清零
- `s_trap_entry` 必须用 `sscratch` 换到内核栈，否则 S 模式写用户栈触发 page fault 无限循环


---

## 补充成果（2026-10-27）

### 一、定时器中断驱动的抢占式调度

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

### 二、缺页异常处理（按需分配）

用户页表初始化时只映射代码页，数据页与栈页不预先映射。S 模式 handler 收到 `cause 13/15` 后分配物理页、填 PTE（`U|R|W|A|D`）、返回原 `epc` 重试指令；非法地址则终止进程。

**实测**：100 万条内触发 **4 次 store page fault**，进程仍正常轮转。

### 三、CoreMark

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

**分数 333 iters/sec**（对应约 3.33 CoreMark/MHz，假设 100 MHz 核心）。

**折磨了很久的根因**：`core_portme.h` 里 `MULTITHREAD` 误写为 `0`。CoreMark 用 `for (i = 0; i < MULTITHREAD; i++)` 做所有初始化，为 0 时一条都不执行——`core_list_init` 没被调用、`list` 保持 NULL，程序在 `core_bench_list` 解引用空指针。表现为"跑几十亿条指令也不出结果"，极难从现象反推。

### 四、可视化增强

`http://127.0.0.1:8777`（`cd web && python3 -m http.server 8777`）

- **反汇编**：新增 `src/disasm.c`（180 行），覆盖 RV32IMA 全部指令 + CSR 名，显示 PC 附近 13 条并高亮当前指令
- **指令阶段视图**：IF/ID/EX/MEM/WB 五个阶段按指令类别动态点亮——`STORE` 亮 MEM、`LOAD` 亮 MEM+WB、分支只亮 EX、ALU 亮 EX+WB，能直观看出"这条指令在做什么"
- **内存热图**：4 KB 页粒度，512 格，读绿写橙、亮度代表访问频次，悬停显示页号与读写次数
- **寄存器双列布局** + CSR 表 + PC 附近内存

### 五、与 QEMU 的语义对照

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

**计算结果完全一致，只有性能不同**。这比"能启动 Linux"更强的证据：CRC 是对链表操作、矩阵乘法、状态机转换全部结果的压缩摘要，全都对得上说明指令语义、异常处理、内存行为一致。

### 新增调试工具

`--dump-mem <addr> <len>`：运行结束后 dump 指定物理内存。定位 CoreMark 问题时靠它读出 `core_list_init` 的真实入参，比反复加 printf 可靠得多。

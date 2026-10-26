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
| Gate 7a 迷你内核 | M→S→U + Sv32 + 系统调用 | 🟡 **核心打通，未达 FR-7 全部要求** | 见下 |
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

**已做到**：M 模式启动 → 自建 Sv32 恒等页表 → `mret` 进 S 模式 → `sret` 进 U 模式 → 用户程序经 `ecall` 发系统调用 → S 模式 `s_trap_handler` 处理 `write`/`exit`（用 `sscratch` 换栈、`SUM=1` 读用户内存）→ 用户态输出正确 → 优雅关机。

**尚未做到**（设计文档 FR-7 的要求）：
- 多进程（≥3 个）与上下文切换
- 缺页异常处理（当前页表是静态建好的）
- 系统调用数量（当前 2 个，要求 ≥4 个）
- 定时器中断驱动的抢占式调度
- 内核内 shell

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

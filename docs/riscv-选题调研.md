# 从零实现 RISC-V 模拟器：项目选题技术调研

调研日期：2026-10-26（所有 star / 提交时间均为当日快照）。
方法说明见文末「口径与可复现性」。

---

## 0. 一句话结论

- 一个能跑真实程序（ELF 加载 + UART + 基础 CSR/异常）的 RV32I+M 模拟器，**实测锚点显示在 1,200–2,600 行**，合理预估 1,500–2,500 行。
- 最适合抄「组织方式」而不是抄代码的项目是 **rv32emu**（活跃、C99、模块边界清楚、有 ACT4 CI）；想看「最小闭环怎么收敛」看 **mini-rv32ima**（核心约 400–550 行）。
- 判定「我做对了」的权威测试：**riscv-tests**（事实标准，exit code 判定）+ **riscv-arch-test**（RISC-V 官方认证，自检查 ELF，打印 `RVCP-SUMMARY`）。

---

## A. 主流 RISC-V 模拟器 / 仿真器

### A.1 总表

| # | 项目 | 仓库 | Stars | 默认分支最后提交 | 语言 | 核心代码实测行数 | Windows |
|---|------|------|-------|------------------|------|------------------|---------|
| 1 | Spike | [riscv-software-src/riscv-isa-sim](https://github.com/riscv-software-src/riscv-isa-sim) | 3,264 | 2026-10-26 | C++ | `riscv/*.cc` 29 文件 **12,419** 行；`riscv/*.h` 1,640 文件 33,220 行 | ✗ 官方不支持 |
| 2 | QEMU | [qemu/qemu](https://github.com/qemu/qemu) | 13,927 | 2026-10-24 | C | `target/riscv/` 93 文件 **54,117** 行 | ✓ 有官方二进制 |
| 3 | mini-rv32ima | [cnlohr/mini-rv32ima](https://github.com/cnlohr/mini-rv32ima) | 2,191 | 2025-12-21 | C | `mini-rv32ima.h` **548** + `mini-rv32ima.c` **520** 行 | ✓ |
| 4 | rvemu | [d0iasm/rvemu](https://github.com/d0iasm/rvemu) | 963 | 2023-10-01（推送 2024-05-29） | Rust | `src/` 15 文件 **6,305** 行 | ✓（推测） |
| 5 | riscv-rust | [takahirox/riscv-rust](https://github.com/takahirox/riscv-rust) | 698 | 2021-08-22（推送 2023-04-06） | Rust | `src/` 12 文件 **7,390** 行 | ✓（推测） |
| 6 | rv32emu | [sysprog21/rv32emu](https://github.com/sysprog21/rv32emu) | 594 | 2026-10-25 | C99 | `src/` 70 文件 **62,871** 行（含生成代码） | △ 未找到官方说明 |
| 7 | venus | [kvakil/venus](https://github.com/kvakil/venus) | 240 | 2018-10-11（推送 2022-04-02） | Kotlin/JS | `src/` 176 文件 **4,748** 行 | ✓ 浏览器 |
| 8 | Ripes | [mortbopet/Ripes](https://github.com/mortbopet/Ripes) | 3,460 | 2026-10-22 | C++/Qt | `src/` 255 文件 **38,779** 行 | ✓ 官方二进制 |
| 9 | rv8（补充） | [larkmjc/rv8](https://github.com/larkmjc/rv8) | 727 | 2022-02-05 | C++ | 未统计 | ✗ x86-64 host |
| 10 | RVVM（补充） | [LekKit/RVVM](https://github.com/LekKit/RVVM) | 1,313 | 2026-10-23 | C | 未统计 | 未验证 |
| 11 | WerWolv/riscv-emulator（补充，体量锚点） | [WerWolv/riscv-emulator](https://github.com/WerWolv/riscv-emulator) | 61 | 2026-04-18 | C++ | **2,635** 行 | 未验证 |
| 12 | fmash16/riscv_emulator（补充，体量锚点） | [fmash16/riscv_emulator](https://github.com/fmash16/riscv_emulator) | 82 | 2024-02-04 | C | **1,242** 行 | 未验证 |

> Stars / 提交时间来源：GitHub REST API `https://api.github.com/repos/<owner>/<repo>`，2026-10-26 抓取（例：https://api.github.com/repos/sysprog21/rv32emu ）。「默认分支最后提交」取 `commits/HEAD` 的 committer date；「推送时间」取 `pushed_at`（会包含非默认分支）。

### A.2 逐项

#### 1. Spike（官方 ISA 参考模拟器）

- 仓库：https://github.com/riscv-software-src/riscv-isa-sim ｜ 3,264 stars ｜ HEAD `c597c6514c01`（2026-10-26）｜仓库 size 9,411 KB（API）。
- **规模（实测）**：`riscv/*.cc` 29 个文件 12,419 行；`riscv/*.h` 1,640 个文件 33,220 行——其中 `riscv/insns/` 有 **1,588 个文件、290 KB**，每条指令/每个扩展一个 header。这是「按指令拆文件」的极端组织方式，本科项目不必模仿。
- **ISA**：README 列出的清单极长，包括 RV32I/RV64I（v2.1）、RV32E/64E、M/A/B/F/D/Q/C/V/P、Zicsr、Zifencei、Zicntr、Zba/Zbb/Zbc/Zbs、Zfh、以及 M/S/U 特权 1.11 和 Hypervisor。来源：https://github.com/riscv-software-src/riscv-isa-sim/blob/master/README.md
- **能跑**：`riscv-tests` 的官方运行载体（`isa/Makefile` 里 `RISCV_SIM ?= spike`）；`spike pk hello` 跑用户程序；配合 bbl/pk 跑 Linux。
- **跨平台**：README 只给 Linux（apt）与 OpenBSD 构建步骤，依赖 `device-tree-compiler`、boost。**官方没有 Windows 支持**；社区在 SiFive 论坛讨论过 Windows 构建（https://forums.sifive.com/t/building-riscv-spike-simulator-for-windows/4029 ），实践上走 WSL/MSYS2。
- **调试**：`-d` 交互式调试（单步、`until`、看 CSR/内存），以及通过 OpenOCD + GDB 远程调试（README「Interactive Debug Mode」「Debugging With Gdb」两节）。

#### 2. QEMU 的 RISC-V 支持

- 仓库：https://github.com/qemu/qemu ｜ 13,927 stars ｜ HEAD `d73149283205`（2026-10-24）｜仓库 size 716,838 KB（API，即约 700 MB）。
- **规模（实测）**：`target/riscv/` 93 个文件 54,117 行；最大的几个是 `tcg/csr.c` 6,983 行、`tcg/vector_helper.c` 5,913 行、`tcg/insn_trans/trans_rvv.c.inc` 4,093 行、`cpu.c` 3,891 行。
- **ISA**：32/64 位都可模拟（`qemu-system-riscv32` / `qemu-system-riscv64`），支持多块开发板与 `virt` 通用板，可跑 Linux；扩展通过 `-cpu` 选择。文档：https://github.com/qemu/qemu/blob/master/docs/system/target-riscv.rst
- **能跑**：Linux、U-Boot、OpenSBI 等整机镜像；`qemu-riscv32` 用户态可直接跑 RISC-V Linux 二进制。
- **Windows**：官方下载页明确给出 Windows 二进制（Stefan Weil 构建）与 MSYS2 包：https://www.qemu.org/download/ 、https://qemu.weilnetz.de/w64/
- **调试**：GDB stub、`-d` 系列 trace、多种 monitor。

#### 3. mini-rv32ima

- 仓库：https://github.com/cnlohr/mini-rv32ima ｜ 2,191 stars ｜ HEAD `84858f58cb41`（2025-12-21）。
- **规模（实测）**：`mini-rv32ima/mini-rv32ima.h` 548 行，`mini-rv32ima/mini-rv32ima.c`（demo wrapper）520 行。README 自述「核心约 400 行、wrapper 约 250 行」，见 https://github.com/cnlohr/mini-rv32ima/blob/master/README.md
- **ISA**：RV32IMA + Zicsr + Zifencei，**partial S-mode，无 MMU**（RV32 NOMMU）。
- **能跑**：RV32 NOMMU Linux（busybox 用户态）、Doom（`experiments/emdoom`，`winrundoom.ps1`）、CoreMark（README 自述约 450 分）。
- **不能跑**：README 明确写 riscv-tests 构建「does not work, now」。
- **Windows**：✓ 自带 `windows/winrun.ps`、`windows/winrundoom.ps1`。
- **调试**：无调试器，靠宏（`MINIRV32WARN`、`MINIRV32_POSTEXEC` 等）注入。

#### 4. rvemu（Rust，跑 Linux/xv6）

- 仓库：https://github.com/d0iasm/rvemu ｜ 963 stars ｜ HEAD `f55eb5b376f2`（2023-10-01；`pushed_at` 2024-05-29，说明有其他分支活动）。
- **规模（实测）**：`src/` 15 个 `.rs` 共 6,305 行；`cpu.rs` 3,571 行、`devices/virtio_blk.rs` 617、`csr.rs` 397、`exception.rs` 234、`devices/plic.rs` 221。
- **ISA**：RV64GC（RV64IMAFD + Zicsr + Zifencei + RV64C）、特权 ISA、Sv39、CSR（M/S 级）、UART/CLINT/PLIC/Virtio、设备树。
- **能跑**：Linux（bbl + busybear）、xv6、riscv-tests（`make test`）、浏览器（rvemu.app）。
- **Windows**：Rust CLI，理论上 `cargo build` 可跨平台（**推测**：README 未给 Windows 说明）。
- **调试**：Web 前端可看执行；CLI 无调试器。

#### 5. riscv-rust（Rust + WASM）

- 仓库：https://github.com/takahirox/riscv-rust ｜ 698 stars ｜ HEAD `8ee69d7a5dc7`（2021-08-22；`pushed_at` 2023-04-06）。
- **规模（实测）**：`src/` 12 个 `.rs` 共 7,390 行；`cpu.rs` 3,963 行、`mmu.rs` 935、`elf_analyzer.rs` 720、`device/virtio_block_disk.rs` 522。
- **ISA**：RV32/64 I、M、F(almost)、D(almost)、A(almost)、C(almost)、Zicsr/Zifencei(almost)、CSR(almost)、SV32/SV39；未做 Q、SV48、PMP。
- **能跑**：Linux（OpenSBI / 旧 BBL）、xv6、riscv-tests（README 给出 `rv32ui-p-add` 的跑法）。
- **Windows**：Rust/WASM，可跨平台（**推测**）。
- **调试**：README 截图含 debugger（单步、断点）。

#### 6. rv32emu（跑 Doom 的那个）

- 仓库：https://github.com/sysprog21/rv32emu ｜ 594 stars ｜ HEAD `cab9a112422c`（2026-10-25）。
- **规模（实测）**：`src/` 70 个文件 62,871 行，但**包含生成代码与 JIT**：
  - `decode.c` 8,809 行（文件头写明 `AUTO-GENERATED by gen-decoder.py`）
  - `rv32_v_template.c` 7,698 行（V 扩展模板）
  - `jit.c` 6,470 行
  - `emulate.c` 5,041 + `riscv.c` 2,430 = **7,471 行**（纯解释器核心）
- **ISA**：RV32I/RV32E + M + A + F + C + Zba/Zbb/Zbc/Zbs，V 扩展部分支持（VLEN=128，`make config` 开启）。
- **能跑**：Doom（`make doom`）、Quake（`make quake`）、RV32 Linux（系统模式，virtio 块设备，`make ENABLE_SYSTEM=1 system`）、riscv-tests（`make check`）、ACT4 arch-test（`make arch-test`）。
- **Windows**：**未找到**官方 Windows 构建说明；GitHub Actions 里没有 windows 任务（实测：`grep windows .github/workflows` 无匹配）。依赖 SDL2，理论上可编译（**推测**）。
- **调试**：GDB Remote Serial Protocol（`docs/gdbstub.md`）、寄存器 JSON dump、rv_histogram/rv_profiler 工具。

#### 7. venus（UC Berkeley CS61C 教学模拟器）

- 仓库：https://github.com/kvakil/venus ｜ 240 stars ｜ HEAD `60b0d07827e1`（2018-10-11；`pushed_at` 2022-04-02）。
- **规模（实测）**：`src/` 176 个 `.kt` 共 4,748 行（含测试）；其中汇编器 `assembler/Assembler.kt` 244 行、模拟器 `simulator/Simulator.kt` 162 行。
- **ISA**：RV32IM。
- **能跑**：教学小程序（`ecall` 的 print/sbrk），不跑 Linux / riscv-tests。
- **Windows**：浏览器版本（https://www.kvakil.me/venus/ ），Windows 直接可用。
- **调试**：单步 + 撤销、断点、机器码/汇编并排、内存可视化——**这是它最值得抄的部分**。

#### 8. Ripes（可视化模拟器）

- 仓库：https://github.com/mortbopet/Ripes ｜ 3,460 stars ｜ HEAD `7827f5f96a6b`（2026-10-22）。
- **规模（实测）**：`src/` 255 个 `.cpp/.h` 共 38,779 行；`isa/rv_i_ext.h` 1,131 行、`isa/instruction.h` 961 行、`processors/RISC-V/rv6s_dual/rv6s_dual.h` 913 行。
- **ISA**：README 写 RV32IMC / RV64IMC。
- **能跑**：汇编与 C 小程序；5 级流水线与双发射处理器的可视化、cache 模拟。
- **Windows**：✓ 官方提供 Linux/Windows/macOS 二进制；也有 WebAssembly 版（https://ripes.dk/ ）。
- **调试**：完整 GUI（流水线时序图、cache、寄存器、内存）。

#### 9–10. 补充：rv8、RVVM

- **rv8**：https://github.com/larkmjc/rv8 （原 `michaeljclark/rv8`，API 301 跳转）。727 stars，2022-02-05 最后推送。C++，x86-64 专用 JIT（IM 翻译、AFD 解释），RV32/RV64 IMAFD + 特权 1.9.1，有用户态模拟器与全系统模拟器，内置简单调试器（断点/单步/反汇编）。**不是跨平台**，Windows 不可用。
- **RVVM**：https://github.com/LekKit/RVVM 。1,313 stars，2026-10-23。C，RV64IMAFDCB + Zkr/Zicbom/Zicboz/Sstc，跑 Linux/FreeBSD/OpenBSD/Haiku，有 JIT 与内置 VM debugger，`-rv32` 可切 32 位。活跃度高，但代码服务整机虚拟化，对本科项目偏重。

#### 11–12. 补充：两个「体量锚点」项目（不是让你参考实现，是用它们校准工作量）

- **fmash16/riscv_emulator**（https://github.com/fmash16/riscv_emulator ）：82 stars，2024-02-04。C，**1,242 行**（`cpu.c` 743、`csr.h` 133、`opcodes.h` 117）。RV32I/RV64I + Zicsr + M。README 的 TODO 明确写着 trap handling、UART、VirtIO 都还没做，输入是裸 `.bin` 而非 ELF。
- **WerWolv/riscv-emulator**（https://github.com/WerWolv/riscv-emulator ）：61 stars，2026-04-18。C++，**2,635 行**（`core.cpp` 801、`core.hpp` 376、`mmu.hpp` 167）。RV32IMA，**能启动 Linux**（含 MMU 与若干设备）。

---

## B. 关键可行性问题

### B1. 本科级「能跑真实程序的 RV32I 模拟器」大概多少行？

**结论（推测，但有实测锚点）：1,500–2,500 行。**

推算拆解（每一步都有实测参照）：

| 模块 | 参照实测值 | 说明 |
|------|-----------|------|
| RV32I 取指-译码-执行 + 内存 | fmash16 `cpu.c` **743 行** | 已含 RV32M 与 Zicsr 的 CSR 框架 |
| ELF 加载器 | rv32emu `src/elf.c` **467 行** | 含大量边界校验；简化版约 250–400 行 |
| CSR + 异常/中断 | rvemu `csr.rs` 397 + `exception.rs` 234 ≈ **630 行** | 若要过 rv32mi 测试，基本省不掉 |
| UART + CLINT 计时器 | mini-rv32ima 的 demo wrapper 里约 **200 行** | 只做 MMIO 轮询的极简实现 |
| 指令展开（switch 大表） | mini-rv32ima 核心 **~400 行** | 作者自述，覆盖 RV32IMA |

- 下限证据：fmash16 用 **1,242 行**做到 RV32I+M+Zicsr 可跑裸机 C（但无 ELF、无 trap、无 UART）。
- 上限证据：WerWolv 用 **2,635 行**做到 RV32IMA + MMU + 设备，能启动 Linux。
- 对照：rvemu 的 `cpu.rs` 3,571 行是 RV64GC 全指令 + 外设；rv32emu 的纯解释器核心 7,471 行是 RV32IMAFC+bitmanip。

所以「RV32I+M + ELF + UART + 基本异常」落在 1,500–2,500 行是合理的；如果还要 C 扩展和 Merkle 式完整 CSR，再加 500–1,000 行。

### B2. riscv-tests 的规模与判定方式

- 仓库：https://github.com/riscv-software-src/riscv-tests ｜ 1,257 stars ｜ HEAD `3df703cd52d5`（2026-10-19）。
- **规模（实测，API tree 统计）**：`isa/` 目录下共 **1,180 个 `.S`** 测试文件。分项：
  - RV32 用户级：`rv32ui` **42**、`rv32um` **8**、`rv32ua` **10**、`rv32uf` **11**、`rv32ud` **11**、`rv32uc` **1**（`rvc.S` 仅 142 字节）
  - RV32 特权：`rv32mi` **16**、`rv32si` **6**
  - RV64 用户级：`rv64ui` **54**
  - `rv32up` 447 / `rv64up` 313（P 扩展，本科可忽略）
- 还有 `benchmarks/`（dhrystone、median、qsort、mm、multiply、towers、vvadd 等），以及 `debug/` 调试测试。
- **判定方式：是 exit code，不是 TAP。** 证据链：
  1. `RVTEST_PASS` 定义为 `fence; li TESTNUM,1; li a7,93; li a0,0; ecall`；`RVTEST_FAIL` 把 `TESTNUM` 移位后塞进 `a0` 再 `ecall`。源码：https://github.com/riscv/riscv-test-env/blob/master/p/riscv_test.h （`riscv-tests` 通过 submodule 引入，`.gitmodules` 指向 `riscv-test-env`）。
  2. Spike 的 fesvr 把 `a7=93` 映射到 `sys_exit`，执行 `htif_exit(code << 1 | 1)`：https://github.com/riscv-software-src/riscv-isa-sim/blob/master/fesvr/syscall.cc （`table[93] = &syscall_t::sys_exit;`，第 224–226 行）。
  3. 测试还有一个 `tohost`/`fromhost` 通道（`write_tohost: sw TESTNUM, tohost`），失败时会往 `tohost` 写非零值并死循环。
  4. `isa/Makefile` 只是 `$(RISCV_SIM) --isa=... $< 2> $@`，没有解析输出，所以官方跑法依赖模拟器进程返回码。
- 对你的模拟器意味着：**必须实现 `ecall`（a7=93）→ 带 a0 退出**，最好再实现 tohost MMIO，否则 riscv-tests 全挂。C 扩展测试几乎没有（`rvc.S` 只有 142 字节），别指望它验证 C。

### B3. riscv-arch-test 与 riscv-tests 的区别

| 维度 | riscv-tests | riscv-arch-test |
|------|-------------|-----------------|
| 仓库 | https://github.com/riscv-software-src/riscv-tests | https://github.com/riscv/riscv-arch-test |
| Stars / 最后提交 | 1,257 / 2026-10-19 | 803 / 2026-10-26 |
| 形态 | 手写汇编，TVM（test virtual machine）宏 | **生成式**：ACT4 框架 + UDB 配置 + Sail 参考模型 |
| 判定 | `ecall(a7=93)` 退出码（0=pass） | 自检查 ELF，运行时打印 `RVCP-SUMMARY: TEST PASSED / FAILED` |
| 规模（实测） | `isa/` **1,180 个 `.S`** | `tests/` **2,236 个 `.S`**（含所有扩展）；`tests/rv32i` **739**、其中 `tests/rv32i/I` 仅 **39**；`tests/rv64i` 836；`tests/priv` 390。仓库共 4,833 个 blob，其中 `generators/` 900+、`coverpoints/` 400+、`config/cores/` 185 |
| 定位 | 单元测试 / bring-up，事实标准 | RISC-V 官方**认证**测试（ACT），面向 conformant 声明 |

关键差别（README 原文）：ACT4「requires a UDB configuration file ... and an rvmodel_macros.h file defining DUT-specific operations such as printing to a console, terminating a test with a success/failure code, and generating interrupts」，并用 Sail 参考模型算出期望签名再编译进 ELF。也就是说 arch-test 需要你先写一套 port 文件（`rvmodel_macros.h` + 链接脚本），比 riscv-tests 重得多。来源：https://github.com/riscv/riscv-arch-test/blob/main/README.md

### B4. CoreMark 的 RISC-V 移植

- **现成可得**：官方 [eembc/coremark](https://github.com/eembc/coremark)（1,271 stars，2026-10-17）+ RISC-V 移植 [riscv-boom/riscv-coremark](https://github.com/riscv-boom/riscv-coremark)（75 stars，2023-12-12）。后者 16 个文件，分 `riscv64/`（Linux）与 `riscv64-baremetal/`（裸机，含 `crt.S`、`link.ld`、`syscalls.c`）。
- **跑它需要模拟器支持什么**（来自源码实测）：
  1. **cycle CSR**：`riscv64-baremetal/core_portme.c` 第 114–117 行用 `read_csr(cycle)` 取时间基准（`GETMYTIME`）。模拟器必须实现 `cycle`（0xC00）并让它递增，否则直接非法指令。链接：https://github.com/riscv-boom/riscv-coremark/blob/master/riscv64-baremetal/core_portme.c
  2. **输出通道**：`syscalls.c` 用 `tohost`/`fromhost` 实现 `SYS_write`（64），`tohost_exit` 用 `tohost = (code<<1)|1` 退出。所以要么实现 HTIF 的 tohost MMIO，要么实现 UART 并把 `printf` 重定向过去。
  3. **计时器/中断**：裸机 port 的 `crt.S` 会设 trap handler；若只跑单次迭代不依赖中断，可以不给 CLINT，但 `mcycle` 必须有。
  4. 链接脚本与起始地址（`link.ld` 指定）。
- 参考实测值：mini-rv32ima README 自述「~450 CoreMark on my laptop，约 QEMU 的一半」（https://github.com/cnlohr/mini-rv32ima/blob/master/README.md ）。

### B5. 跑 Doom 需要什么额外工作

rv32emu 的做法（实测读代码）：Doom 不是跑在 Linux framebuffer 上，而是跑在 **user-mode + 自定义 syscall 层**：

- `src/syscall_sdl.c` 31,740 字节，定义了一整套非标准 syscall 编号（`0x00`–`0x70` 分页、`INIT_AUDIO`/`PLAY_MUSIC`/`PLAY_SFX`/`SET_MUSIC_VOLUME`/`STOP_MUSIC`，以及 `SDL_KEYDOWN`/`SDL_MOUSEMOTION` 等事件回传）。
- 也就是说需要：
  1. **帧缓冲**：在 guest 内存里划一块 RGB 缓冲区，由宿主 SDL 每帧 blit 到窗口；
  2. **输入**：键盘/鼠标事件从 SDL 队列转成 guest 能读的结构；
  3. **音频**：SDL_mixer 的 music/sfx 通道（可选）；
  4. **定时器**：`gettimeofday`/`clock` 类 syscall，Doom 靠它控制帧率；
  5. **文件 IO**：读 WAD 文件（`open/read/lseek`）；
  6. **堆**：`sbrk`/`malloc`。
- Doom 本体来自 https://github.com/sysprog21/doom_riscv （11 stars，C++ port，"Doom classic port to lightweight RISC-V"）。
- 结论：让 RV32I 模拟器跑 Doom，工作量**不小于**模拟器本体——一套 user-mode syscall ABI + SDL 胶水，rv32emu 用了约 31 KB 源码。项目若做这个，建议降级为「跑 CoreMark + 跑 riscv-tests + 跑一个 hello world」，Doom 作为可选加分。

### B6. ELF 加载的复杂度

- **最小可用（静态链接 ELF）**：只需读 ELF32 header → 遍历 program header → 对每个 `PT_LOAD` 把 `p_filesz` 字节写到 `p_vaddr`，再把 `[p_filesz, p_memsz)` 清零；入口用 `e_entry`。**不需要重定位。**
  - 实测参照：rv32emu 的 `src/elf.c` 共 467 行（含安全校验：magic、`ELFCLASS32`、`EM_RISCV`(243)、表越界、`p_filesz <= p_memsz`），加载逻辑本身只有 20 多行（`elf_load()`）。
- **什么时候才需要重定位**：PIE、共享库、动态链接、或「加载地址 ≠ 链接地址」。riscv-tests 的 `p` 环境用固定链接脚本（`env/p/link.ld`），所以不需要。
- 若要做，psABI 定义的类型主要是：`R_RISCV_32/64`、`R_RISCV_HI20`、`R_RISCV_LO12_I/LO12_S`、`R_RISCV_BRANCH`、`R_RISCV_JAL`、`R_RISCV_CALL/CALL_PLT`、`R_RISCV_PCREL_HI20/PCREL_LO12_I/LO12_S`、`R_RISCV_RELAX`、`R_RISCV_ALIGN`。来源：https://github.com/riscv-non-isa/riscv-elf-psabi-doc
- 结论：**本科项目只做静态 ELF + PT_LOAD 是正当的**，把省下的时间放到 CSR/异常正确性上。

---

## C. 判断

### C1. 从零写 RV32I + RV32M，最难的三个技术点

1. **CSR 与异常/中断的交互语义**（最容易「看起来对、测试全红」）
   具体包括：`mstatus` 的 MIE/MPIE/MPP 在 trap 与 `mret` 时的压栈/恢复；`mtvec` 的 direct/vectored 两种模式（低两位）；`mepc` 的取值——`ecall` 要 +4、非法指令要指向指令自身、压缩指令要指向 2 字节边界；`mcause` 的高位（interrupt）与异常码；`mtval` 在非法指令时放指令编码、在 misaligned 时放地址；`medeleg/mideleg` 的委托顺序；以及异常优先级（取指 > 读 > 写）。riscv-tests 的 `rv32mi` 16 个测试几乎全部打在这里。

2. **取指、PC 与指令长度的边界**
   没有 C 扩展时是「4 字节对齐 + 跨页取指 + `jalr` 清 bit0 + 分支目标 misaligned 异常」；一旦加 C，就变成「按 `inst[1:0]` 判 2/4 字节、PC 只保证 2 字节对齐、`mepc` 低位、跨 4KB 页的指令不能拆读」。这类 bug 只在特定测试上暴露，调试成本极高。

3. **系统边界的闭环：ELF → 内存映射 → 退出/输出**
   即 `PT_LOAD` 加载与 bss 清零、起始栈指针、`tohost`/`fromhost` 或 `ecall` ABI、UART 的 MMIO 地址与 TX 就绪位、`mtime`/`mcycle` 递增、退出码传递。少了这一环，模拟器只能跑你自己编的裸机 bin，跑不了任何现成程序——而这恰恰是「能不能交付」的分水岭。

（可选第 4：立即数解码与符号扩展，错得很隐蔽但不算「难」。）

### C2. 最适合作为「实现质量参照物」的项目

**首选 rv32emu**（https://github.com/sysprog21/rv32emu ）：
- 活跃（2026-10-25 还有提交），C99，无重型框架依赖；
- 模块边界清楚：`riscv.c`（CPU/CSR/内存）+ `emulate.c`（指令语义）+ `decode.c`（生成的分发表）+ `elf.c` + `io.c`，正好对应你项目要拆的几个模块；
- 有 ACT4 arch-test 的 CI 接入（`docs/arch-test.md`），说明它的「正确性判定」是可复现的；
- 有文档（`docs/*.md` 十几篇），能看「别人怎么把一件事写清楚」；
- 规模适中：纯解释器核心 7,471 行，不会像 Spike/QEMU 那样一眼望不到边。

**次选 mini-rv32ima**（https://github.com/cnlohr/mini-rv32ima ）：看「如何把一件事压到 400 行」的取舍——它明确不实现 MMU、不实现多核、不管 ELF 重定位，这种「明确放弃什么」的判断比代码本身更值得学。

**不建议作为参照**：venus（2018 后基本停更、4.7k 行里一半是 UI 胶水）、rvemu / riscv-rust（2021–2023 后停更，代码结构停在旧版特权规范）。它们可以看「调试器/可视化怎么做」，不适合看「现代工程组织」。

### C3. 有没有权威的一致性测试

有，两层：

1. **riscv-tests**（https://github.com/riscv-software-src/riscv-tests ）——事实标准、生态默认。对本科项目，做到 `rv32ui` 42 + `rv32um` 8 + `rv32mi` 16 全绿，就足以支撑「正确实现了 RV32I+M 与机器模式异常」的结论。判定简单（exit code 0），不需要写 port 文件。
2. **riscv-arch-test / ACT4**（https://github.com/riscv/riscv-arch-test ）——RISC-V 官方认证测试，权威性最高。代价是要写 `rvmodel_macros.h`、UDB 配置、链接脚本，并用 Sail 参考模型生成自检查 ELF；对本科项目偏重，但可以作为「扩展目标」写进论文，或只挑 `tests/rv32i/I` 的 39 个跑。

建议的验收口径：**riscv-tests 全绿 + CoreMark 跑通并给出分数 + 自写 10 个定向测试（覆盖 CSR/mret/ecall）**，这套组合比单跑 arch-test 更能讲清楚「我验证了什么」。

---

## 口径与可复现性

- **Stars / 提交时间**：GitHub REST API，2026-10-26 抓取，端点 `https://api.github.com/repos/<owner>/<repo>` 与 `/commits/HEAD`。
- **代码行数（实测）**：下载各仓库默认分支 zip（`codeload.github.com/.../zip/refs/heads/<branch>`），解压后对指定目录的 `.c/.h/.cc/.cpp/.rs/.kt` 逐个统计换行符数量（含空行与注释，等价于 `wc -l`）。对应 commit：Spike `c597c6514c01`、QEMU `d73149283205`、mini-rv32ima `84858f58cb41`、rvemu `f55eb5b376f2`、riscv-rust `8ee69d7a5dc7`、rv32emu `cab9a112422c`、venus `60b0d07827e1`、Ripes `7827f5f96a6b`、WerWolv `HEAD`、fmash16 `HEAD`。
- **QEMU `target/riscv/` 行数**：因仓库 700 MB，改用 GitHub contents API 逐文件取 base64 解码后统计（93/93 成功）。
- **标注约定**：「实测」= 本次统计/运行；「README 自述」= 项目文档声明；「推测」= 基于锚点的估计，未经运行验证。
- **未找到**：Whisper 的官方仓库（`westerndigitalcorporation/whisper` 已 404）；rv32emu 的 Windows 官方构建说明；rv8 / RVVM 的行数统计（未下载）。

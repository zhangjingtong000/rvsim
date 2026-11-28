# rvsim · RISC-V 模拟器与迷你内核

从零实现的 RV32IMA 模拟器 + 迷你内核 + 可视化界面。

**当前目标：让 AI agent 能完整地使用这个操作系统。**

具体做法是把内核状态建模成**可审计、可断言、可回滚**的对象：

- **事件是真相**：内核维护事件日志，`log` 按发生顺序输出
- **能力清单由内核生成**：`manifest` 输出全部能力的结构化清单
- **一次调用自证**：`assert key=value` 让内核直接回答是否成立，AI 不必自己数
- **预演与提交分离**：`plan` 登记，`apply` 执行
- **可回滚**：`snapshot` 存档，`restore` 回滚，`resume` 续跑

面向 AI 的使用说明见 `docs/ai-guide.md`；终端内可执行 `guide` 与 `manifest`。

## 30 秒验证

只想确认「这套东西能不能跑」，下面三条命令就够（干净克隆，Linux/WSL；第一条会联网拉
capstone 与 riscv-tests，约 10 MB，离线做法见「离线 / 网络受限时怎么构建」）：

```bash
make deps && make -j4
./build/rvsim --elf tests/riscv-tests/isa/rv32ui-p-add
# 期望输出末尾： [rvsim] 结果：PASS
```

本机实测输出（gcc 13 + riscv64-unknown-elf-gcc）：

```
[rvsim] ELF=tests/riscv-tests/isa/rv32ui-p-add entry=0x80000000 装载=9492 字节 tohost=0x80001000
[rvsim] 开始执行，max_cycles=50000000
[rvsim] 停止：执行 512 条指令，cycles=512，priv=3，pc=0x8000004c
[rvsim] 结果：PASS
```

这三条只证明「依赖拉齐了 + 模拟器编出来了 + 能跑完一条 RV32I 自检」。完整的 91 项回归、19 项能力契约、153 项界面 e2e 见「测试覆盖」；网络受限时先看「离线 / 网络受限时怎么构建」。

## 当前状态（2026-11-21）

> **这些状态数字的来源与可复核性**：下表与后文的实测值都来自**开发机上的完整构建**
> （2026-11-21 那一轮，Linux/WSL + gcc 13 + riscv64-unknown-elf-gcc）。
> 仓库**有意不包含**构建产物与测试依赖：`build/`、`third_party/`、
> `tests/riscv-tests/` 都在 `.gitignore` 里，`web/images/kernel.elf` 也由 `make`
> 生成而不入库。所以「干净克隆后直接看到这一片绿灯」不成立——必须先
> `make deps && make`。今天在一台新机器上按「运行 → 首次使用」的步骤能复现到哪一步，
> 取决于能否联网拿到 capstone / riscv-tests，以及是否自备 Linux `Image` 与 QEMU。
> 有三类东西本仓库无法自带，下文各自标注：**Linux 镜像**（`third_party/linux-image/`
> 不入库）、**QEMU 交叉验证**（需 `qemu-system-riscv32`）、**浏览器 e2e**（需
> playwright + chromium）。

| 验收门 | 目标 | 状态 | 证据 |
|---|---|---|---|
| Gate 0 环境 | 工具链可用、91 个测试可编译 | ✅ | `build/` 产物 |
| Gate 1 最小可执行环境 | ELF 加载 + trap + U 模式 + tohost | ✅ | `rv32ui-p-add` PASS |
| Gate 2 RV32I | 42/42 | ✅ | `./tests/run_tests.sh rv32ui` |
| Gate 3 RV32M + RV32A | 8/8 + 10/10 | ✅ | `./tests/run_tests.sh rv32um` / `rv32ua` |
| Gate 4 特权级与计数器 | 16/16 | ✅ | `./tests/run_tests.sh rv32mi` |
| Gate 5 S 模式 + Sv32 | 6/6 | ✅ | `./tests/run_tests.sh rv32si` |
| Gate 6 启动 Linux | 进 shell + 执行命令 | ✅（开发机） | `uname -a` / `ls /` / `cat /proc/cpuinfo` / `free` / `ps`；Linux `Image` 不入库，需自备到 `third_party/linux-image/` |
| Gate 7a 迷你内核 | M→S→U + Sv32 + 多进程 + 系统调用 | ✅ | 3 进程 `ABCABC...` 轮转 |
| **抢占式调度** | 定时器中断驱动，非 yield | ✅ | `B→C→A` 轮转，37/38/38 均衡 |
| **缺页处理** | 按需分配物理页 | ✅ | 实测 4 次 store page fault 后正常继续 |
| **CoreMark** | 跑通并给出分数 | ✅ | 333 iters/sec（guest 时间，读法见「性能特征」） |
| **可视化增强** | 反汇编 + 阶段视图 + 热图 | ✅ | 13 行反汇编、5 阶段动态点亮、512 格热图 |
| **QEMU 语义对照** | 与 QEMU 比对计算结果 | ✅ | CoreMark 四个 CRC 一致（`tests/qemu_crosscheck.sh`）+ **Linux 串口行集合一致**（`tests/qemu_linux_crosscheck.sh`，`uname -a` 逐字节相同，`ls /`、`free` 归一化后一致） |
| Gate 7b WASM 可视化 | 浏览器调试界面 | ✅ | 94 KB wasm，Node + 浏览器双验证 |

**riscv-tests：91/91（82 个官方用例 + 9 个自写定向）；能力契约：19/19；界面 e2e：153/153。**

**CI**：`.github/workflows/ci.yml` 已启用，分两个 job（内核 / 界面），跑的就是「运行」一节的命令。本地等价物是 `./scripts/ci.sh`；缺 playwright 时想跳过必须显式加 `--allow-skip`，默认跳过即失败。

**CI 实际结论（2026-11-28）**：两个 job 都在真实 runner 上跑过，一红一绿都记在这里：

- **修复前** run [46105194216](https://github.com/zhangjingtong000/rvsim/actions/runs/46105194216)：内核 job success；界面 job **failure**，e2e **137/153**，从 `13.15 终止正在运行的进程后回到 shell` 起连续 16 条报「内核没有响应」。不是抖动：本机复现到的现场是进程表停在 `2:RUN`、终端停在 `[U] proc 0 exit(0)` 之后不再输出、cycles 还在涨，根因是内核两处问题——
  1. `kernel/s_entry.S` 的 trap 入口无条件用 `sscratch` 换栈。**内核态里再进 trap**（嵌套）时 `sscratch` 里是用户 sp，入口会把它当内核栈，紧接着 `sw ra,0(sp)` 就 store 缺页，再触发一次 trap → 无限递归。表现就是内核彻底静默、宿主所有下行命令 4 秒超时「内核没有响应」。
  2. `kernel/main.c` 的 `exit` 路径切进程时漏了 `proc_switch_satp(next->pgtbl)`（定时器抢占 / `yield` / `kill` 三处都切了，只有这里漏），下一个进程拿着上一个进程的页表在跑。
- **修复后** run [46118946829](https://github.com/zhangjingtong000/rvsim/actions/runs/46118946829)（head `910ad52`）：内核 job **success**（91/91 + 能力契约 19/19），界面 job **success**，e2e **153/153**（`13.15` PASS）。同一份代码在本机也是 91/91 + 153/153。

本页 badge 显示的就是仓库最近一次运行的真实结论，不再声称「全部 success」。

[![CI](https://github.com/zhangjingtong000/rvsim/actions/workflows/ci.yml/badge.svg)](https://github.com/zhangjingtong000/rvsim/actions/workflows/ci.yml)

## 桌面外壳（2026-11 新增）

内核不画像素，只发布结构化数据；宿主 HTML/CSS 用真字体渲染。固定地址协议（窗口表 / 终端网格 / 进程表 / 文件表 / 系统状态 / 宿主下行命令）写在 `docs/ui-spec.md` 第 0 章。

| 块 | 能力 |
|---|---|
| 窗口 | 八向缩放、拖动阈值 4px、最大化圆角归零、失焦态、最小化动画、半屏/四角/顶部吸附、Snap 布局面板、``Alt+` `` 窗口切换器 |
| 终端 | 61×22 网格、2000 行 scrollback、内核语义属性上色、折行续行标记、Ctrl+A/F/Shift+C/V、右键菜单、多行粘贴确认、Ctrl+点击链接 |
| 文件 | 工具栏（新建/文件夹/重命名/删除/导入）、目录导航、路径栏、右键菜单、文本编辑器（保存前检查文件是否还在） |
| 进程 | Monitor 行右键终止/挂起/恢复，跑飞的程序能在定时器中断里终止 |
| 视觉 | 动效 6 档阶梯（100/167/250/333/500 + 700 启动）、Fluent 三条缓动、字号阶梯、状态色、WCAG 对比度实测 |

**对标表**：`docs/ui-benchmark.md`，拿 Win11 / macOS 14 / GNOME 45 的公开规格逐项对照，每项标「对齐 / 偏差 / 缺失 / 超出」。**主题**：深色默认，浅色在「设置 → 外观」切换并存 localStorage；两套主题对比度实测（深色正文 18.1:1、浅色 16.1:1，WCAG AA 要求 4.5:1）。

## 数字

> 下面的行数由 `./scripts/count_lines.sh` 生成，改代码后重跑即可，不要手改。

| 项 | 值 |
|---|---|
| 模拟器源码 | **2,528 行**（`src/`，16 个文件，含 FDT 解析、反汇编适配） |
| 迷你内核 | **6,631 行**（`kernel/`，20 个文件，含事件日志、能力清单、快照、计划、图形界面下行通道） |
| 桌面外壳 | **3,946 行**（`web/index.html`，单文件 HTML/CSS/JS） |
| 规格文档 | `docs/ui-spec.md` 549 行 / **154 项功能**；`docs/ui-benchmark.md` 386 行 |
| 测试 | riscv-tests **91**（82 官方 + 9 定向）+ 能力契约 **19** + 界面 e2e **153** |
| 解释速度 | **约 25.8 MIPS**（CoreMark 12.58 亿条 / 48.7 秒墙钟；轻负载微基准可达 32） |
| WASM 体积 | 96,112 字节（约 94 KB） |
| 内核镜像 | 115,480 字节（`make` 编出 `build/kernel.elf` 并复制到 `web/images/kernel.elf`；`.gitignore` 忽略 `*.elf`，仓库里没有这个文件，需先构建） |
| Linux 启动 | 6.1.14，约 3 亿条指令到 shell |

## 目录

```
src/            模拟器：CPU/CSR/trap/MMU/总线/外设/加载器
kernel/         迷你内核：M 模式启动、Sv32、S/U 切换、系统调用、
                图形界面下行通道（文件/进程操作）
web/            桌面外壳：index.html（结构+样式+逻辑）、WASM 运行时
                （rvsim.<hash>.js/.wasm，脚本生成）、images/kernel.elf（make 生成）
dts/            自写设备树
tests/          riscv-tests + 能力契约 + 批量跑分器
scripts/ui/     界面 e2e（153 用例）与性能/尺寸测量脚本
docs/           ui-spec.md（功能与接口契约）、ui-benchmark.md（对标表）、
                需求/设计/调研、开发记录（CHANGELOG-review.md）
bench/          CoreMark 上游整棵源码树（114 个文件，含 doxygen 生成的 HTML），
                原样引入只读：`make coremark` 用它跑分。不要在这里改东西，
                要更新就整棵替换成上游新版本。
```

`web/images/` 里有两样东西值得说明：`kernel.elf` 是 `make` 生成的产物（`.gitignore` 忽略 `*.elf`，仓库里没有它）；`rv32ui-p-add` 是**唯一入库的 ELF**（15,480 字节，取自 riscv-tests 的 `isa/rv32ui-p-add`，与上游编译产物逐字节相同），供调试面板的「rv32ui-p-add」按钮在干净克隆里直接点开跑 ISA 自检。它没有 `.elf` 后缀，所以不受 `*.elf` 忽略规则约束——入库是有意的，不是漏网。

## AI-OS 能力

| 能力 | 用途 |
|---|---|
| `manifest` | 全部能力的结构化清单（内核生成，非手写） |
| `guide` | 面向 AI 的使用指南（事件语义、序号体系、易错点） |
| `log` | 事件流：全部 / count / last / find，末尾自带摘要 |
| `assert` | 一次调用自证，三种写法等价 |
| `plan` / `apply` | 预演与提交分离，执行后计划被消耗 |
| `snapshot` / `restore` / `resume` | 存档、回滚、续跑 |
| `status` | 一次拿到进程数、事件数、计划数、快照数、是否空闲 |

**37 项能力**全部可调用：`replay` 从事件流重建存活进程并与 `ps` 交叉验证，`verify` 在 trap 侧独立计数（`unaccounted_ecall` 为 0），`regs`/`setreg`/`setpc` 让 AI 与前端在寄存器读写上对等。

这个数字不是手写的：**以 `manifest` 输出的 `caps` 字段为准**，它就是 `kernel/main.c` 里 `g_caps[]` 的条数。清单里每条能力的 `cap` 都唯一，agent 可以放心按 `cap` 建索引：无参数的 `resume` 是「让就绪进程开跑」，带 pid 的那条叫 `resume-pid <pid>`（恢复被挂起的进程），两条语义不同，因此不复用同一个 key。

**内核已具备的操作系统要素**：真 ELF 加载器（PT_LOAD 段、BSS 清零、每进程独立物理代码页）、内存文件系统（文件 + 目录 + 路径）、文件描述符（open/read/close，用户程序可按 fd 顺序读文件）、shell 输出重定向（`cmd > file`）、多用户与权限（login / whoami / chmod，uid 0 为 root）、每进程独立地址空间（fork 时复制代码页，父子隔离）。

**事件流是 16384 条环形缓冲**（有界，会覆盖），`replay` 的 `complete` 字段标记是否被覆盖过。

## 内核完成度

- M 模式启动 → 自建 Sv32 恒等页表 → `mret` 进 S 模式 → `sret` 进 U 模式
- 3 个进程轮转（`ABCABC...`），`proc_schedule` 时间片轮转；抢占由定时器中断驱动（M 模式 MTIP → 置 `mip.STIP` → S 模式 STI 切换）
- 完整上下文切换：`s_trap_entry` 保存 x1-x31 + 用户 `sp`，切换时整组恢复并切 `satp`
- 缺页异常按需分配（`cause 13/15`），非法地址终止进程
- 系统调用：`write` / `exit` / `getpid` / `yield` / `fork` / `sbrk`
- `sscratch` 换栈（按 `sstatus.SPP` 区分来源：来自 U 才换栈，内核里再进 trap 复用内核栈，不会把用户 sp 当内核栈）+ `SUM=1` 读用户内存；优雅关机（syscon）
- 内核内 shell：37 条命令（`help` / `ps` / `run` / `reset` / `mem` / `uptime` 是最早的一批；完整清单以 `manifest` 为准）

## 代码结构要点

- **CSR 只有一份物理存储**：`sstatus`/`sie`/`sip` 是 `mstatus`/`mie`/`mip` 的掩码视图
- **`mepc` = 触发 trap 的指令地址**，`+4` 由 handler 自己做
- **`mret`/`sret`**：`xIE←xPIE`、`xPIE←1`、`xPP←U`、`MPRV←0`，`sret` 读 `SPP`
- **总线**：先判 RAM 再逐设备匹配区间（不用高位折叠，避免别名）
- **misaligned load/store 由硬件分解**，取指 misaligned 抛 cause 0 且 `rd` 不写回
- **Sv32**：A/D 硬件置位、超页 `ppn` 检查、`SUM`/`MXR`/`MPRV`
- **设备地址来自 dtb**：`src/fdt.c` 按 `compatible` 匹配节点、读 `reg` 写入运行时表，无 `--dtb` 时回退 QEMU virt 布局

## 性能特征（实测，不是估算）

| 场景 | 帧率 | p99 帧间隔 |
|---|---|---|
| 空闲 | 135 fps | 8.6 ms |
| 拖动窗口 | 125 fps | 9.3 ms |
| 缩放窗口 | 114 fps | 10.3 ms |
| 终端滚动历史 800 行 | 128 fps | 10.4 ms |
| 100 轮「跑飞→全终止」压测后 | 132 fps | 9.3 ms |

RAIL 帧预算 16.66ms、应用实际可用 10ms，上面 p99 都在预算内。上表是机器空闲时测的：同一台机器上跑重负载时，模拟器速率会从 14M 掉到 6M、帧率从 132 掉到 88——那是宿主资源竞争。测性能前先确认机器空闲。

**页池压测**：100 轮「`run spin.elf` → 逐个终止」，累计缺页 164 次（300 次进程启动，平均每次 0.55 页，来自栈页和数据页的按需分配）；进程零残留、文件数不变、无 pageerror，压完还能正常 `run hello.elf`。缺页数由内核在系统状态页发布（`sys_sync_t.pf_count`），宿主从 `rv.state.mem.faults` 读。

**终端 scrollback** 内核侧 2000 行，DOM 里最多挂 800 行。实测 584 行时 DOM 有 3817 个节点、帧率 117fps；清空历史行回到 136fps——瓶颈是节点数而不是内核，所以超过 800 行丢最老的，帧率稳定在 128fps 左右。（试过 `content-visibility:auto`，反而掉到 96fps，已撤掉。）

### 数字怎么读

CoreMark 的 **333 iters/sec 按 guest 时间算**：`MTIME_DIV=10` 让每 10 条指令 `mtime+1`，guest 的 1 秒约等于墙钟 4 秒，所以这个数字虚高约 4 倍（按墙钟折算约 80 iters/sec），由它推算的「3.33 CoreMark/MHz（假设 100MHz）」没有真实硬件时钟依据。**可信的是四个 CRC 值**（与官方校验值、与 QEMU 完全一致），它证明功能正确，不是性能。对外给性能数字时，标明时间基准来自 `mtime` 而非墙钟。

## 运行

```bash
# 回归：91 个指令测试 + 19 项能力契约
./tests/run_tests.sh all

# 界面 e2e（需要先起前端：cd web && python3 -m http.server 8799）
node scripts/ui/e2e.js

# 全部三套一起跑（CI 用的就是这条）
./scripts/ci.sh

# 跑单个测试
./build/rvsim --elf tests/riscv-tests/isa/rv32ui-p-add

# 启动 Linux
./build/rvsim --image third_party/linux-image/Image --dtb dts/minimal.dtb \
  --max-cycles 4000000000 < /dev/null

# 迷你内核
./build/rvsim --elf build/kernel.elf

# 可视化（先编译 WASM）
./scripts/build_wasm.sh    # 重新生成 web/rvsim.<hash>.js + .wasm（文件名按内容哈希，含 capstone，必须一起编）
cd web && python3 -m http.server 8777
```

**首次使用（干净克隆）必须先拉依赖**：

```bash
./scripts/fetch_deps.sh all   # 等价于 make deps：下载 capstone + riscv-tests 并编译 p 测试
make                          # 构建 build/rvsim 与 build/kernel.elf（并同步到 web/images）
make directed                 # 构建 9 个定向测试 ELF
make dts                      # 构建 dts/minimal.dtb（需要 dtc，仅手动验证 dtb 时才需要）
./tests/run_tests.sh all      # 回归（应 91/91 + 契约 19/19）
```

只想先确认能跑，用开头的「30 秒验证」；拉依赖失败看下一节。

环境要求（缺哪个 `make` 都会报出来）：

| 依赖 | 用途 | 缺了会怎样 |
|---|---|---|
| `gcc` / `make` | 编译模拟器 | 直接编不了 |
| `riscv64-unknown-elf-gcc` | 编译迷你内核与定向测试 | `make` 在 kernel 阶段失败 |
| `python3` / `node` | `count_lines.py`、能力契约、界面 e2e | 对应步骤跳过或失败 |
| `dtc` | 编译 dts | 只有 `make dts` 需要 |
| 网络 | `make deps` 拉 capstone / riscv-tests | 需要一次性联网 |

缺 capstone 时 `make` 会直接提示 `先执行：make deps`，不会只丢一个 `capstone.h not found`。

### 离线 / 网络受限时怎么构建

`make deps`（即 `./scripts/fetch_deps.sh all`）实际做两件联网的事：

| 目标 | 拉什么 | 落到哪 |
|---|---|---|
| capstone | GitHub 上 `capstone-engine/capstone` 的 **5.0.7 源码 tarball**，只取源码 + 头文件，不需要 cmake——本项目手工编译其中 11 个 `.c` | `third_party/capstone/` |
| riscv-tests | `riscv-software-src/riscv-tests` 的 **`git clone --recursive --depth 1`**（必须带 `env` 子模块，链接脚本在 `env/p/link.ld`），拉完当场编出 82 个 `rv32*-p-*` ELF | `tests/riscv-tests/` |

两个目录都被 `.gitignore` 忽略：它们不进仓库，也正是离线构建唯一需要补齐的东西。GitHub 直连失败时走代理（脚本读**大写** `HTTPS_PROXY`，会显式传给 `curl -x` 和 `git -c http.proxy`）：

```bash
HTTPS_PROXY=http://127.0.0.1:7897 make deps
```

完全离线时从已构建好的机器拷：

```bash
tar czf rvsim-deps.tgz third_party/capstone tests/riscv-tests   # 源机器
tar xzf rvsim-deps.tgz && make -j4                              # 目标机器
```

要点：两个目录约 1.5 MB + 8.7 MB，U 盘即可；`tests/riscv-tests` 里**已编译好的 ELF 要一起拷**，否则目标机器仍需 `riscv64-unknown-elf-gcc`；判断依赖是否齐看 `third_party/capstone/include/capstone/capstone.h` 与 `tests/riscv-tests/isa/rv32ui-p-add`；只用 tarball 回退路径拿到的 riscv-tests 不含 `env` 子模块，官方用例编不出来，这种情况从别处拷一份完整的更省事。

## 测试覆盖

三套测试，各自覆盖不同层面：

| 套件 | 规模 | 覆盖 |
|---|---|---|
| riscv-tests | 91（82 官方 + 9 定向） | RV32I/M/A、特权级与 CSR、S 模式与 Sv32 |
| 能力契约 `tests/contract.js` | 19 | manifest / assert / plan / snapshot 等能力层 |
| 界面 e2e `scripts/ui/e2e.js` | 153 | 桌面外壳：窗口 / 终端 / 文件 / 进程 / 菜单 / 键盘 |

前两套在 `./tests/run_tests.sh all` 里；界面 e2e 单独跑（需要 node + playwright + 浏览器），由 `./scripts/ci.sh` 串起来。**`run_tests.sh` 绿了不等于全绿。**

三条口径说明：

- **PMP 未经检验**：`rv32mi-p-*` 中涉及 PMP 的用例在 `G=0` 配置下不校验权限，属于「通过但没验到」，因此不能声称 PMP 已验证。要补这块，需让用例在 `G=1` 下运行，并确认越权访问会触发异常。
- **QEMU 对照只比 CRC**：`tests/qemu_crosscheck.sh` 比对四个 CRC 与 CoreMark Size，不比时间、不比 cycle 数。QEMU 侧因跑得比 10 秒门槛快，会打印 `ERROR! Must execute for at least 10 secs for a valid result!` 与 `Errors detected`，脚本靠 `timeout` 结束——不影响 CRC 一致性结论。
- **Gate 6 的「串口行集合一致」已实现**：`tests/qemu_linux_crosscheck.sh` 用同一份 Linux `Image` 在 rvsim 与 `qemu-system-riscv32 -M virt` 上各跑一遍，执行 `uname -a` / `ls /` / `free`，两级比对：
  `uname -a` **逐字节一致**（`Linux buildroot 6.1.14 ... riscv32 GNU/Linux`）；
  `ls /` 与 `free` 去 ANSI、去时间戳、数字归一化后**行集合一致**。
  为什么不是整段启动日志逐字节比：启动日志带时间戳、两套设备树的探测顺序不同、内核自身占用内存也有约 10 KB 差异——这些不是语义差异。脚本缺 `qemu-system-riscv32` 或 `Image`/`dtb` 时跳过，不产生红灯。

### 想自己复核这些数字

站在一台新机器上，从干净克隆开始：

```bash
# 1) 拉依赖（联网一次，约 10 MB）：capstone 源码 + riscv-tests
./scripts/fetch_deps.sh all          # 等价于 make deps

# 2) 构建：模拟器 + 内核镜像（并复制到 web/images/kernel.elf）+ 9 个定向测试
make -j4 && make directed

# 3) 回归：82 个官方用例 + 9 个定向 + 19 项能力契约
./tests/run_tests.sh all             # 期望 91/91 与契约 19/19
                                     # total==0 时脚本会 exit 1，不会把「一个都没跑」判成通过

# 4) 界面 e2e（另开一个终端：cd web && python3 -m http.server 8799）
node scripts/ui/e2e.js               # 期望 153/153，需要 playwright + chromium

# 5) Linux：自备 third_party/linux-image/Image（仓库不含）
./build/rvsim --image third_party/linux-image/Image --dtb dts/minimal.dtb \
  --max-cycles 4000000000 < /dev/null

# 6) QEMU 交叉验证：需要 qemu-system-riscv32 + 同一份 Image/dtb
./tests/qemu_crosscheck.sh
```

第 3 步是分水岭：它只依赖 `make deps` 能拉到的两样东西，不依赖 Linux 镜像、QEMU 或浏览器。第 4 步需要 playwright，CI 里用 `./scripts/ci.sh` 串起来（缺 playwright 时想跳过必须显式加 `--allow-skip`，默认跳过即失败）。第 5、6 步依赖本仓库不提供的 Linux 镜像，能跑到哪一步取决于你自备的镜像与工具链。

## 设计取舍

单机演示范围内有意不做的部分：

| 不做 | 原因 | 代价 |
|---|---|---|
| W^X（页级可写/可执行分离） | 内核与用户页都按演示需要给 `R`+`W`+`X` 三个权限位，构建时 ld 会警告 `has a LOAD segment with RWX permissions` | 攻击面变大；真实系统须按页拆 R/W/X |
| C/F/D/B/V 扩展 | RV32IMA + Zicsr/Zifencei 覆盖 riscv-tests 主体与内核所需 | 浮点、压缩指令程序跑不了 |
| `-v` 测试变体 | 需要 newlib 构建用户态测试 | 覆盖只到 `-p` 系列 |
| JIT / 二进制翻译 | 解释执行已约 25 MIPS，够跑 CoreMark 与 Linux；JIT 的调试成本远大于收益 | 比 JIT 慢一个数量级 |
| 多 hart | 单核足以验证 M/S/U 与抢占调度；SMP 会牵动 IPI、原子指令与锁 | 无法验证多核同步 |
| PMP 强制 | 寄存器只实现读写，不参与地址检查 | 无法验证 PMP 权限位（见「测试覆盖」） |
| 逐字节 QEMU 对照 | CRC 语义一致已能定位大部分错误 | 时序与 cycle 数不比对 |
| Alt 块选（矩形选区） | 基于 DOM 选区，DOM 不支持矩形选区；WT 是自绘的 | 终端只能按行选 |
| ANSI 16 色转义解析 | 改用内核语义属性上色，避免在宿主再实现一遍 ANSI | 输出原始 ANSI 的程序显示不了颜色 |
| dtb 决定 RAM base/size | `g_ram[]` 是编译期静态数组，下标计算依赖基址 | 改 RAM 布局要重新编译，dtb 只做一致性校验 |

## 开发记录

项目经历过一次外部评审（76/100）。评审发现的问题、修复项与验证方法，以及踩过的坑、FR-2.8 设备树验证步骤、CoreMark 与调度等补充成果，都在 [`docs/CHANGELOG-review.md`](docs/CHANGELOG-review.md)。

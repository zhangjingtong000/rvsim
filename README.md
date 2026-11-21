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

---

## 当前状态（2026-11-21）

| 验收门 | 目标 | 状态 | 证据 |
|---|---|---|---|
| Gate 0 环境 | 工具链可用、91 个测试可编译 | ✅ | `build/` 产物 |
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
| Gate 7b WASM 可视化 | 浏览器调试界面 | ✅ | 90 KB wasm，Node + 浏览器双验证 |

**riscv-tests：91/91（82 个官方用例 + 9 个自写定向）；能力契约：18/18；界面 e2e：153/153。**

**CI**：`.github/workflows/ci.yml` 分两个 job（内核 / 界面），跑的就是上面这些命令；
本地等价物是 `./scripts/ci.sh

# 缺环境（没有 playwright）时想跳过，必须显式说明；默认跳过即失败
./scripts/ci.sh --allow-skip`。这份工作流**没有在真实 GitHub runner 上跑过**
（本机没有 runner），命令序列和本地一致、YAML 语法已校验——第一次推送后请看 Actions 页面。

91 项只覆盖模拟器指令。能力层（manifest/assert/plan/snapshot）由
`tests/contract.js` 覆盖；桌面外壳（窗口 / 终端 / 文件 / 进程 / 菜单 /
键盘）由 `scripts/ui/e2e.js` 的 153 个用例覆盖。

前两套（riscv-tests + 能力契约）在 `./tests/run_tests.sh all` 里；
界面 e2e 单独跑（需要 node + playwright + 浏览器），由 `./scripts/ci.sh`
串起来。**不要以为 `run_tests.sh` 绿了就全绿了**。

### 桌面外壳（2026-11 新增）

内核不画像素，只发布结构化数据；宿主 HTML/CSS 用真字体渲染。
固定地址协议（窗口表 / 终端网格 / 进程表 / 文件表 / 系统状态 / 宿主下行命令）
写在 `docs/ui-spec.md` 第 0 章。

| 块 | 能力 |
|---|---|
| 窗口 | 八向缩放、拖动阈值 4px、最大化圆角归零、失焦态、最小化动画、半屏/四角/顶部吸附、Snap 布局面板、`Alt+`` 切换器 |
| 终端 | 61×22 网格、2000 行 scrollback、内核语义属性上色、折行续行标记、Ctrl+A/F/Shift+C/V、右键菜单、多行粘贴确认、Ctrl+点击链接 |
| 文件 | 工具栏（新建/文件夹/重命名/删除/导入）、目录导航、路径栏、右键菜单、文本编辑器（保存前检查文件是否还在） |
| 进程 | Monitor 行右键终止/挂起/恢复，跑飞的程序能在定时器中断里终止 |
| 视觉 | 动效 6 档阶梯（100/167/250/333/500 + 700 启动）、Fluent 三条缓动、字号阶梯、状态色、WCAG 对比度实测 |

**对标表**：`docs/ui-benchmark.md`。拿 Win11 / macOS 14 / GNOME 45 的公开规格
逐项对照，每项标「对齐 / 偏差 / 缺失 / 超出」。

**主题**：深色默认，浅色在「设置 → 外观」里切换，存 localStorage。
两套主题的对比度都实测过（深色正文 18.1:1、浅色 16.1:1，均远超 WCAG AA 的 4.5:1）。

**仍有 2 项没做**：Alt 块选（DOM 选区不支持矩形，WT 是自绘的）、
ANSI 16 色（我们不解析转义序列，改成了内核语义属性上色）。

## 数字

> 下面的行数由 `./scripts/count_lines.sh` 生成，改代码后重跑即可，不要手改。


| 项 | 值 |
|---|---|
| 模拟器源码 | **2,581 行**（`src/`，含 FDT 解析、反汇编适配） |
| 迷你内核 | **6,375 行**（`kernel/`，含事件日志、能力清单、快照、计划、图形界面下行通道） |
| 桌面外壳 | **3,827 行**（`web/index.html`，单文件 HTML/CSS/JS） |
| 规格文档 | `docs/ui-spec.md` 521 行 / **153 项功能**；`docs/ui-benchmark.md` 379 行 |
| 测试 | riscv-tests **91**（82 官方 + 9 定向）+ 能力契约 **18** + 界面 e2e **153** |
| 解释速度 | **约 25.8 MIPS**（CoreMark 12.58 亿条 / 48.7 秒；轻负载微基准可达 32） |
| WASM 体积 | 96,112 字节（约 94 KB） |
| 内核镜像 | 113,036 字节（`web/images/kernel.elf`，`make` 自动同步） |
| Linux 启动 | 6.1.14，约 3 亿条指令到 shell |

> 行数会随代码变化，重跑 `python3 scripts/count_lines.py` 即可，不要手改。

## 目录

```
src/            模拟器：CPU/CSR/trap/MMU/总线/外设/加载器
kernel/         迷你内核：M 模式启动、Sv32、S/U 切换、系统调用、
                图形界面下行通道（文件/进程操作）
web/            桌面外壳：index.html（结构+样式+逻辑）、WASM 运行时、
                images/kernel.elf
dts/            自写设备树
tests/          riscv-tests + 能力契约 + 批量跑分器
scripts/ui/     界面 e2e（153 用例）与性能/尺寸测量脚本
docs/           ui-spec.md（功能与接口契约）、ui-benchmark.md（对标表）、
                需求/设计/调研
```

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

**能力状态**：32 项能力全部可调用，`replay` 从事件流重建存活进程并与 `ps` 交叉验证，
`verify` 在 trap 侧独立计数（`unaccounted_ecall` 为 0），`regs`/`setreg`/`setpc`
让 AI 与前端在寄存器读写上对等。

**内核已具备的操作系统要素**：
- 真 ELF 加载器（PT_LOAD 段、BSS 清零、每进程独立物理代码页）
- 内存文件系统（文件 + 目录 + 路径，32 项能力里的 ls/cat/write/rm/mkdir）
- 文件描述符（open/read/close 系统调用，用户程序可按 fd 顺序读文件）
- shell 输出重定向（`cmd > file`）
- 多用户与权限（login / whoami / chmod，uid 0 为 root）
- 每进程独立地址空间（fork 时复制代码页，父子隔离）

**事件流是 16384 条环形缓冲**（有界，会覆盖），`replay` 的 `complete` 字段标记是否被覆盖过。

## 性能特征（实测，不是估算）

| 场景 | 帧率 | p99 帧间隔 |
|---|---|---|
| 空闲 | 135 fps | 8.6 ms |
| 拖动窗口 | 125 fps | 9.3 ms |
| 缩放窗口 | 114 fps | 10.3 ms |
| 终端滚动历史 800 行 | 128 fps | 10.4 ms |
| 100 轮「跑飞→全终止」压测后 | 132 fps | 9.3 ms |

RAIL 的帧预算是 16.66ms、应用实际可用 10ms，上面的 p99 都在预算内。

> 上表是机器空闲时测的。同一台机器上跑着别的重负载时，模拟器速率会从 14M
> 掉到 6M、帧率从 132 掉到 88——那是宿主资源竞争，不是代码退化。
> 测性能前先确认机器空闲。

**页池压测**：100 轮「`run spin.elf` → 逐个终止」，累计缺页 **164** 次
（300 次进程启动，平均每次 0.55 页，来自栈页和数据页的按需分配）；
进程零残留、文件数不变、无 pageerror，压完还能正常 `run hello.elf`。
缺页数由内核在系统状态页发布（`sys_sync_t.pf_count`），宿主从
`rv.state.mem.faults` 读——不再靠抓终端文本。

**终端的 scrollback 是 2000 行（内核），DOM 里最多挂 800 行**。
实测 584 行时 DOM 有 3817 个节点、帧率 117fps；清空历史行回到 136fps——
瓶颈是节点数而不是内核。所以超过 800 行就丢最老的，帧率稳定在 128fps 左右。
（试过 `content-visibility:auto`，反而掉到 96fps，已撤掉。）

## 运行

```bash
# 回归：91 个指令测试 + 18 项能力契约
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
./scripts/build_wasm.sh    # 重新生成 web/rvsim.js + .wasm（含 capstone，必须一起编）
cd web && python3 -m http.server 8777
```

**首次使用（干净克隆）必须先拉依赖**：

```bash
./scripts/fetch_deps.sh all   # 等价于 make deps：capstone + riscv-tests + dtb 工具
make                          # 构建 build/rvsim
make directed                 # 构建 4 个定向测试 ELF
make dts                      # 构建 dts/minimal.dtb
./tests/run_tests.sh all      # 回归（应 91/91 + 契约 18/18）
```

缺 capstone 时 `make` 会直接提示 `先执行：make deps`，不会只丢一个 `capstone.h not found`。

## Gate 7a 的完成度（如实说明）

**已做到**：
- M 模式启动 → 自建 Sv32 恒等页表 → `mret` 进 S 模式 → `sret` 进 U 模式
- **3 个进程轮转**（`ABCABC...`），`proc_schedule` 时间片轮转
- **完整上下文切换**：`s_trap_entry` 保存 x1-x31 + 用户 `sp`，切换时整组恢复
- **4 个系统调用**：`write` / `exit` / `getpid` / `yield`
- `sscratch` 换栈 + `SUM=1` 读用户内存
- 优雅关机（syscon）

**已实现**（本节此前写"尚未做到"，属于文档滞后；下列均已在 2026-10-27 完成并实测）：
- 缺页异常处理：按需分配，`mem` 命令显示实测缺页次数
- 定时器中断驱动的抢占式调度：M 模式 MTIP → 置 `mip.STIP` → S 模式 STI 切换进程
- 每进程独立页表 + 切换 `satp`
- 内核内 shell：`help` / `ps` / `run` / `reset` / `mem` / `uptime`
- `fork` / `sbrk` 系统调用
- **上下文切换的 `sp` 真正切到新进程**（2026-10-27 修复，见下）

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

### FR-2.8：设备地址从 dtb 读取（2026-10-27 实现）

`src/fdt.c`（219 行）解析 FDT：校验 header magic，遍历 structure block，按 `compatible` 属性识别节点（不是按节点名——plic 的节点名是 `interrupt-controller@c000000`，按名字匹配会漏），从 `reg` 读 <base,size>，写进运行时表 `g_devmap`。所有外设访问都走这张表。

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

无 `--dtb` 时回退到编译期默认值（QEMU virt 布局），所以 riscv-tests、CoreMark、Linux 都不受影响。

**边界（如实说明）**：`g_ram[]` 是编译期静态数组，所以 dtb 里 `memory` 节点的 **base 和 size 都不会被采用**——解析出来只做**一致性校验**，与编译期值不符时打印警告。改 RAM 基址或容量必须重新编译。设备大小（`UART_SIZE`/`PLIC_SIZE`/`CLINT_SIZE`）同理是编译期常量，dtb 只覆盖基址。这样做的理由：RISC-V 的 RAM 布局决定了 `g_ram[]` 的数组下标计算，运行时改不了；MMIO 窗口大小在外设实现里也是固定的。

### 必须在论文/演示里如实说明的三条

**一、`pmpaddr` 是假 PASS。** `rv32mi-p-*` 里涉及 PMP 的用例在 `G=0` 配置下不真正检查权限，属于"通过但没验证到"。**不要说"PMP 已验证"。**

**二、CoreMark 的分数不可作为性能结论。** `MTIME_DIV=10` 意味着每 10 条指令 `mtime+1`，所以 **guest 认为的 1 秒 ≠ 真实 1 秒**。实测：CoreMark 12.57 亿条指令，guest 报 `Total time : 12 secs`，而墙钟跑了约 **49.5 秒**——guest 时钟比真实时间**慢约 4 倍**（不是快）。因此：

- `333 iters/sec` 是按 guest 时间算的，**虚高约 4 倍**（按墙钟折算约 80 iters/sec）
- 由它推算的 `3.33 CoreMark/MHz（假设 100MHz）` 没有真实硬件时钟依据

**可信的只有四个 CRC 值**（与官方校验值、与 QEMU 完全一致），它证明的是**功能正确**，不是性能。论文里如果要给性能数字，必须说明时间基准来自 `mtime` 而非墙钟。

**三、QEMU 对照脚本只比 CRC，不比时间。** QEMU 侧会打印 `ERROR! Must execute for at least 10 secs for a valid result!` 和 `Errors detected`（因为它跑得比 10 秒门槛快），脚本靠 `timeout 280` 结束。这不影响 CRC 一致性结论，但要在文档里说清楚。

### 2026-10-27 修复记录（评审发现）

外部评审（76/100）指出以下问题，均已修复：

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

# RVSIM 面向 AI 的使用指南

> **这份文档需要长期维护。** 新增能力、改变字段语义、调整输出格式时，必须同步更新。
> 终端内的 `guide` 命令输出的是精简版；本文是完整版。

---

## 1. 这是什么

一台 RISC-V（RV32IMA）模拟器上运行的迷你操作系统。通过**串口**交互：
你发送文本命令，它以文本/JSON 回应。

**它不是 POSIX shell。** 没有管道、通配符、`~`、环境变量。**但有输出重定向**：`cmd > file` 会把命令输出写进文件。
写 `ps -ef` 或 `log | tail` 一定会失败。

---

## 2. 快速开始

```
rvsh> manifest     # 列出全部能力（JSON）
rvsh> guide        # 精简版使用指南（就是你正在读的这份的摘要）
rvsh> help         # 只有命令名
```

**先读 `manifest`**，再决定调用哪个能力。

---

## 3. 能力清单

**37 条全部可调用**，与内核 `manifest` 输出的 `caps` 一致。下表由
`scripts/gen_cap_table.py` 从 `kernel/main.c` 的 `g_caps[]` 生成，不要手改；
改了能力表就重跑 `python3 scripts/gen_cap_table.py --write`（`--check` 只校验是否过期）。

<!-- CAP-TABLE:BEGIN（由 scripts/gen_cap_table.py 生成，勿手改） -->
| 能力 | 用法 | 说明 |
|---|---|---|
| `help` | `help` | 列出全部命令 |
| `manifest` | `manifest` | 输出本能力清单（机器可解析） |
| `status` | `status` | 统一状态入口（JSON）：at_prompt 表示发命令时系统是否停在提示符 |
| `ps` | `ps` | 进程表（JSON）：pc=用户态入口，nrun=被调度次数 |
| `run` | `run <file>` | 装载指定程序并启动 3 个初始进程；fork 演示会额外创建子进程（共 4 个） |
| `reset` | `reset` | 把全部进程重置到就绪 |
| `kill` | `kill <pid>` | 结束一个用户进程并回收它的代码页/堆页/页表；正在运行的进程要在 Monitor 里终止 |
| `suspend` | `suspend <pid>` | 让一个就绪进程退出调度 |
| `resume-pid` | `resume-pid <pid>` | 恢复被挂起的进程并立刻切进用户态（无参数的 resume 是「让就绪进程开跑」，两者语义不同） |
| `mem` | `mem` | 物理内存大小与累计缺页次数（自启动起） |
| `uptime` | `uptime` | 读取虚拟时钟 mtime |
| `log` | `log [all] \| log count [type] \| log last <n> \| log find <pid>` | 事件流：全部（带摘要）/计数/最近 n 条/按进程过滤 |
| `tail` | `tail` | 输出最近一段串口内容，换行显示为 \| |
| `assert` | `assert <key>=<value> \| <key>: <value> \| <key> <value>` | 断言：内核直接回答是否成立，返回 want/got/ok |
| `plan` | `plan run <prog> \| plan reset \| plan restore <snap_id>` | 登记一个计划但不执行；用 apply <id> 执行 |
| `plans` | `plans` | 列出待执行的计划 |
| `apply` | `apply <plan_id>` | 执行指定计划 |
| `snapshot` | `snapshot` | 保存当前进程状态快照，返回快照编号 |
| `snaps` | `snaps` | 列出快照：id 用于 restore，evt_seq 是该快照时的事件序号 |
| `restore` | `restore <snap_id>` | 恢复到指定快照（回滚，改变当前进程状态） |
| `guide` | `guide` | 使用指南：事件字段语义、序号体系、易错点 |
| `resume` | `resume` | 让就绪进程开始运行（restore/reset 之后用） |
| `replay` | `replay` | 从事件流重建存活进程，与 ps 交叉验证 |
| `regs` | `regs [pid]` | 读某进程的 32 个寄存器（默认 pid 0） |
| `verify` | `verify` | 交叉验证：trap 侧独立计数 vs 事件流 |
| `setreg` | `setreg <pid> <reg> <val>` | 改写进程寄存器（调试用，不可逆） |
| `setpc` | `setpc <pid> <addr>` | 改写进程执行点（调试用，不可逆） |
| `ls` | `ls [path]` | 列出内存文件；给路径则只列该前缀之下 |
| `cat` | `cat <name>` | 读取文件内容 |
| `write` | `write <name> <text>` | 写入文件（覆盖），返回字节数 |
| `rm` | `rm <name>` | 删除文件 |
| `mkdir` | `mkdir <path>` | 创建目录（名字里可以带 / 表示层级） |
| `login` | `login <user>` | 切换当前用户（uid 0 是 root） |
| `whoami` | `whoami` | 当前用户与 uid |
| `clear` | `clear` | 清空窗口里的终端内容（不动 UART 历史） |
| `echo` | `echo <text>` | 原样输出一段文本（不包 JSON，给欢迎语用） |
| `chmod` | `chmod <name> <0\|1>` | 改文件权限（仅 root） |
<!-- CAP-TABLE:END -->

`run` 的 `<file>` 是 **RAMFS 里的任意程序**，不是固定几个名字：`run hello.elf`、
`run fork.elf`、`run badptr.elf`、`run spin.elf` 都能跑，裸名自动补 `.bin`。
`ls` 能看到当前装载了哪些。`manifest` 的 `run` 行参数列写的就是这一点。

**有文件系统和多用户**。别绕开它们——AI 操控一个系统时，「能读写文件」和
「知道当前是谁」是最基本的两件事。

## 4. `log` 的四种形态

```
log               默认：最近 30 条 + 摘要
log all           全量
log count         按类型汇总
log count <type>  单一类型计数，type ∈ spawn/exit/ecall/restore/oob
log last <n>      最近 n 条
log find <pid>    某个进程的全部事件
```

**每次倾倒末尾都有一行摘要：**

```
[summary] total=39 spawn=3 exit=9 ecall=27 restore=0 denied=18
```

**要计数就直接读这一行，不要自己数。**

### 事件字段语义

| 类型 | arg0 | arg1 | result | code |
|---|---|---|---|---|
| `spawn` | 入口地址 | 用户栈地址 | 0 | 0 |
| `exit` | 退出码 | 0 | 0 | 0 |
| `ecall` | 缓冲区地址 | 长度 | 写出字节数（-1 表示全被拒） | 0 成功 / 1 地址非法 / 2 超长 |
| `restore` | 快照 id | 该快照时的事件序号 | 1 | 0 |

### 两套序号，别混淆

- **`log` 的 `seq`**：全局单调递增，包含已被环形缓冲覆盖的旧事件
- **`log count` 的 `total`**：当前缓冲区里还剩多少条

两者**不相等**，`snaps` 返回的 `evt_seq` 用的是前者。

---

## 5. `assert` 的三种等价写法

```
assert nproc=3
assert nproc: 3
assert nproc 3
```

**支持的 key：**

| key | 含义 |
|---|---|
| `spawn` / `exit` / `ecall` / `restore` | 该类型的事件条数 |
| `events` | 事件总条数 |
| `nproc` | 活跃进程数（就绪 + 运行） |
| `oob` | 被拒绝的写入次数（`code != 0`） |
| `uart` | 最近 4KB 历史输出里是否包含指定子串 |
| `reset` / `snapshot` / `plan` / `apply` / `sbrk` / `fork` / `setreg` / `getpid` / `yield` | 各类事件条数 |
| `plans` / `snaps` | 待执行计划数 / 快照数 |
| `at_prompt` | 发命令时系统是否停在提示符（1/0） |

`uart` 是字符串比较，其余都是整数比较。

**返回：** `{"assert":"nproc","want":3,"got":3,"ok":true}`

---

## 6. `plan` / `apply` 两段式

```
plan run <prog>        生成"运行某程序"的计划
plan reset             生成"重置进程"的计划
plan restore <id>      生成"回滚"的计划（标 irreversible）
plans                  看有哪些待执行
apply <id>             执行
```

**`apply` 之后计划即被消耗**，`plans` 会变空。

**为什么分两段：** 破坏性操作可以先看后果再决定执行。

---

## 7. 输出格式说明

**`run` 之后的 `AAABBBCCC` / `PPPPCCCC` 是用户程序打印的，不是内核日志。**
内核日志带前缀，如 `[S] 进入 S 模式`、`[U] proc 0 exit(0)`。

**`tail` 把换行显示为 `|`**，且首行可能被滑窗截断（看到孤立的 `0|` 是正常的）。

---

## 8. 常见工作流

### 跑演示并审计

```
run badptr
log                       # 读摘要：spawn=3 exit=3 denied=6
assert exit=3             # 直接断言
```

### 改之前先存档（试错-回滚）

```
snapshot                  # → {"snapshot":1}
plan run badptr
apply 1
assert nproc=0            # 进程都退出了
restore 1                 # 回滚
assert nproc=3            # 进程回到就绪
```

### 预演破坏性操作

```
plan restore 1            # → {"plan":2,"intent":"restore","irreversible":true}
apply 2                   # 真的执行
```

---

## 9. 易错点

| 易错 | 正确做法 |
|---|---|
| `ps -ef` / `grep` / 管道 | 直接用 `ps`，它已经是 JSON |
| `run prog=badptr` | `run badptr`（参数直接跟在后面，不是键值对） |
| 自己数 `log` 的行 | 读末尾的 `[summary]` 或 `assert` |
| 以为 `snapshot` 会存事件流 | 只存进程状态；事件流有界（16384 条），会被覆盖 |
| 以为 `restore` 可撤销 | 标了 `irreversible: true`，执行前先 `snapshot` |
| 以为 `run fork` 是 3 个进程 | fork 演示会多一个子进程，共 4 个 |
| 用 `log` 被刷屏 | 默认只给 30 条，要全量用 `log all` |
| `restore` 之后直接 `ps` 就以为完事 | 进程只是 READY，要 `resume` 才真的继续跑 |
| 以为 `restore` 是「暂停恢复」 | 它是「回到快照时的进程表与代码，然后从头重跑」 |
| 以为 `log` 是完整历史 | 它是有界缓冲（16384 条）；`replay` 的 `complete` 说明是否被覆盖 |
| 用 `ps` 判断事件流对不对 | 用 `replay` 交叉验证，它只看事件不读 `procs` |

---

## 10. 维护约定

改动以下任一项时，必须同步更新**本文档**与内核里的 `guide` 命令：

1. 新增或删除能力（改 `g_caps` 表）
2. 改变事件字段语义（`evt_t` 的 arg0/arg1/code）
3. 改变输出格式（JSON 结构、表头列数）
4. 改变默认行为（如 `log` 的默认条数）

**判断标准：如果 AI 因为不知道这条信息而会用错，它就该出现在这里，而不是 `manifest` 的 desc 里。**

`manifest` 的 `desc` 合理长度是**一句话**，只回答「这是什么、怎么调」。

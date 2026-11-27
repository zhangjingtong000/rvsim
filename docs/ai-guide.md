# RVSIM 面向 AI 的使用指南

> **这份文档需要长期维护。** 新增能力、改变字段语义、调整输出格式时，必须同步更新。
> 终端内的 `guide` 命令输出的是精简版；本文是完整版。

---

## 1. 这是什么

一台 RISC-V（RV32IMA）模拟器上运行的迷你操作系统。通过**串口**交互：
你发送文本命令，它以文本/JSON 回应。

**它不是 POSIX shell。** 没有管道、通配符、`~`、环境变量。**但有输出重定向**：`cmd > file` 会把命令输出写进文件。。
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

| 能力 | 用法 | 说明 |
|---|---|---|
| `help` | `help` | 命令名列表 |
| `manifest` | `manifest` | 全部能力的结构化清单 |
| `guide` | `guide` | 精简版使用指南 |
| `status` | `status` | 一次拿到 nproc/events/plans/snaps/uart_len/at_prompt |
| `ps` | `ps` | 进程表 JSON |
| `run` | `run <prog>` | 装载并运行，prog ∈ hello / fork / badptr |
| `reset` | `reset` | 把进程重置到就绪 |
| `mem` | `mem` | 物理内存与累计缺页次数 |
| `uptime` | `uptime` | 虚拟时钟 mtime |
| `log` | 见 §4 | 事件流 |
| `tail` | `tail` | 最近一段串口输出 |
| `assert` | 见 §5 | 断言 |
| `plan` | 见 §6 | 生成计划但不执行 |
| `plans` | `plans` | 列出待执行计划 |
| `apply` | `apply <id>` | 执行计划（执行后计划即被消耗） |
| `snapshot` | `snapshot` | 保存进程状态快照 |
| `snaps` | `snaps` | 列出快照 |
| `restore` | `restore <id>` | 回滚（不可逆） |
| `resume` | `resume` | 让就绪进程开跑（restore/reset 之后必须用它） |
| `replay` | `replay` | 从事件流重建存活进程，与 `ps` 交叉验证 |
| `regs` | `regs [pid]` | 读某进程的 32 个寄存器 |
| `setreg` | `setreg <pid> <reg> <val>` | 改写寄存器（不可逆，调试用） |
| `setpc` | `setpc <pid> <addr>` | 改写执行点（不可逆，调试用） |
| `verify` | `verify` | 交叉验证：trap 侧计数 vs 事件流 |

---


### 上面这张表不全，以 `manifest` 为准

上表只列了核心能力，实际有 **37 条**。用 `manifest` 拿完整清单（机器可解析）。
下表是上表**漏掉**的部分，都是真实可用的：

| 能力 | 用途 |
|---|---|
| `kill <pid>` | 结束进程并回收代码页/堆页/页表 |
| `suspend <pid>` / `resume-pid <pid>` | 挂起 / 恢复（无参数的 `resume` 是让就绪进程开跑，见上表） |
| `ls [path]` | 列文件（可只看某个前缀） |
| `cat <name>` | 读文件内容 |
| `write <name> <text>` | 写文件（覆盖） |
| `rm <name>` | 删文件 |
| `mkdir <name>` | 建目录 |
| `login` / `whoami` / `chmod` | 多用户与权限（uid 0 是 root） |
| `clear` / `echo` | 清屏 / 原样输出 |
| `cmd > file` | 输出重定向 |

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

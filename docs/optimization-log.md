# 优化记录：从实验暴露的问题到接口改进

**原则**：实验失败先问「是不是我们这边的问题」，能改的就改，不写进文档当困境。

---

## 第一轮实验暴露的问题（全部是我们自己的）

| 现象 | 根因 | 归属 |
|---|---|---|
| `run prog=badptr` 失败 | manifest 把参数写成 `"args":"prog=hello\|fork\|badptr"`，看起来像键值对 | **我们的表述问题** |
| `run badptr 3` | 没说明不能带额外参数 | **我们的表述问题** |
| `ps -ef \| grep` 反复失败 | 我们的 shell 不是 POSIX，但没声明 | **我们的缺失** |
| `log \| tail -n 20` 失败 | 同上 | **我们的缺失** |
| `assert oob 18` 报格式错 | key 解析没在空格处停 | **我们的 bug** |
| `log count oob` 未知命令 | manifest 写了 `log count [type]` 但没实现 | **我们的 bug** |
| `ps` 表格难解析 | 空格分隔，列边界靠猜 | **我们的设计问题** |
| `log` 39 条太长，模型数错 | 没有查询原语，只能整段倾倒 | **我们的设计问题** |

---

## 六项修复

### 1. manifest 参数格式

```diff
- {"cap":"run","args":"prog=hello|fork|badptr",...}
+ {"cap":"run","usage":"run <prog>","values":"hello|fork|badptr",...}
```

`usage` 给完整命令形式，`values` 给枚举取值。**避免被理解成键值对。**

### 2. `ps` 改 JSON

```diff
- PID  STATE  NRUN  PC
-  0   READY  0     0x80400000
+ {"procs":[{"pid":0,"state":"READY","pc":"0x80400000","nrun":0},...]}
```

未使用的槽标 `UNUSED`（按 `stack_pa == 0` 判断）。

### 3. `log` 自带摘要

倾倒完自动附一行：

```
[summary] total=39 spawn=3 exit=9 ecall=27 restore=0 denied=18
```

**理由**：实测模型会读完整 log 然后自己数，且在窗口边界数错。
**把答案放在它一定会读到的地方，比指望它数对可靠。**

### 4. `log` 增加查询原语

```
log count          → {"log":{"total":39,"spawn":3,"exit":9,"denied":18}}
log count exit     → {"count":{"exit":9}}
log count oob      → {"count":{"oob":18}}
log last 12        → 最近 12 条 + 摘要
```

**把 O(n) 的整段倾倒变成 O(1) 查询。**

### 5. 未知命令 → 结构化失败

```diff
- 未知命令，试 help
+ {"ok":false,"why":"unknown command","echo":"help run","hint":"输入 help 或 manifest 查看可用能力"}
```

带 `echo` 和 `hint`，让模型能自己纠正，而不是在同一个错误上反复试。

### 6. 启动声明非 POSIX

```
简易 shell（非 POSIX，不支持管道/重定向），输入 manifest 查看能力
```

**模型会本能地写 `ps -ef`、`log | tail`。声明一次比纠正五次便宜。**

---

## 效果（同一任务、同一模型 deepseek-flash）

| 指标 | 改进前 | 改进后 |
|---|---|---|
| B 组正确率 | 3/3 | 2/2 |
| B 组平均步数 | 10.0 | **7.0** |
| B 组平均 token | 34,311 | **18,049** |
| B 组最好一次 | — | **5 步 / 6,533 token** |
| A 组 | 0/3 | 0/1（仍答 `oob=0`） |

**token 降到约一半，而且最好的一次只用了 5 步。**

A 组的失败**不是待修的问题**——它没有事件流，答不出历史问题，这正是对照组的设计意图。

---

## 第二轮：待办全部完成

| 待办 | 状态 | 实现要点 |
|---|---|---|
| `assert` 支持 `key: value` | ✅ | key 解析在 `=`、`:`、空格三处终止 |
| `log find <pid>` | ✅ | 按 actor 过滤，回答「某进程经历过什么」 |
| `status` 含 UART 状态 | ✅ | `uart_len` + `at_prompt` |
| `plan` 扩展到 reset / restore | ✅ | 计划带 kind，apply 按 kind 分派 |

### 一个值得记下的实现陷阱

`status` 的 `at_prompt` 最初恒为 false。

原因：UART 尾部缓冲是**滑动窗口**（满了丢最旧），
而 `g_cmd_mark` 是**绝对位置**。缓冲满之后 mark 指向的内容早就移位了。

**修法**：不在命令执行时回看，而是在**命令第一个字符到达的那一刻**
立刻判定并保存结果。

> 滑动窗口 + 绝对索引 不兼容，这是缓冲类设计的老坑。

### 现在的完整能力

```
help     manifest  status   ps       run      reset    mem
uptime   log       tail     assert   plan     plans    apply
snapshot snaps     restore
```

`log` 的四种形态：`log`（全部+摘要）、`log count`（计数）、
`log count <type>`（分类计数）、`log last <n>`（尾部）、`log find <pid>`（按进程）。

`plan` 的三种意图：`plan run <prog>`、`plan reset`、`plan restore <snap_id>`。

---

## 下一轮实验该测什么

**破坏性操作**比「计数」更能拉开 A/B 差距：

> 任务：先存档，运行演示，然后回滚到存档前，并证明恢复成功。

- A 组：**根本没有 snapshot/restore**，做不到
- B 组：`snapshot` → `run` → `restore` → `assert nproc=3`

这个差异是**能力有无**，不是**效率高低**，比计数任务更绝对。

# 实验记录：事件流与断言的价值（A/B 对照）

**日期**：2026-11-05
**模型**：deepseek-flash（temperature=0，max_tokens=4000）
**载体**：rvsim RV32 模拟器 + mini-kernel，wasm 内嵌执行

---

## 1. 实验设计

### 为什么换任务

第一轮用的是「跑一次 badptr 演示并确认」——**太简单**，一条 `run badptr` 就够。
实测完整接口组反而更慢（8 步 / 15,457 token vs 4.5 步 / 2,706 token），
因为额外接口只增加了选择与格式负担。

**这类任务测不出设计意义。** 于是换成「**需要历史**」的任务。

### 任务

> 连续运行 badptr 演示 3 次。然后回答：
> 1. 三次演示总共产生了多少条 exit 事件？
> 2. 三次演示总共有多少次地址越界写入被拒绝？
> 3. 最后一次演示中，哪个进程（PID）最先退出？

**最终答案格式**：`ANSWER: exit=<数字> oob=<数字> first=<PID>`

### 真值（由实验者从事件流独立核对，不采信模型自述）

```
exit = 9     (3 次 × 3 进程)
oob  = 18    (3 次 × 3 进程 × 2 次越界写入)
first = 0    (最后一次演示中 PID 0 最先退出)
```

### 两组

| 组 | 内核 | 能力 |
|---|---|---|
| **A** | `kernel-min.elf` | `help` `ps` `run` `reset` `mem` `uptime` |
| **B** | `kernel.elf` | A 组全部 + `manifest` `log` `assert` `plan` `apply` `plans` `snapshot` `snaps` `restore` `status` `tail` |

**关键差异：A 组没有任何历史记录，只有 `ps` 给出的当前状态。**

---

## 2. 结果

| 组 | 正确 | 平均步数 | 平均 token |
|---|---|---|---|
| **A** | **0 / 3** | 5.0 | 4,753 |
| **B** | **3 / 3** | 10.0 | 34,311 |

### A 组的回答（全部错误）

```
trial 1: answer={"exit":9,"oob":0,"first":0}  错
trial 2: answer={"exit":9,"oob":0,"first":0}  错
trial 3: answer={"exit":9,"oob":0,"first":0}  错
```

**三次都猜 `oob=0`。**

典型轨迹：`run badptr` ×3 → `mem` → `ps` → 尝试 `manifest`（不存在）→ `uptime` → 猜答案。

**它不是拒绝回答，是「没有信息可依」**——`ps` 只显示进程当前状态，
而「被拒绝过几次」从未出现在任何它能读到的输出里。

### B 组的回答（全部正确）

```
trial 1: steps=13 tokens=52721  answer={"exit":9,"oob":18,"first":0}  对
trial 2: steps=8  tokens=18951  answer={"exit":9,"oob":18,"first":0}  对
trial 3: steps=9  tokens=31260  answer={"exit":9,"oob":18,"first":0}  对
```

典型轨迹：`manifest` → `run badptr` ×3 → `log`（读事件流）→ `assert oob=18` → `assert exit=9` → 答案。

---

## 3. 分析

### 3.1 事件流把「不可回答」变成「可回答」

A 组的三次错误不是随机的——**它稳定地猜 0**，因为
「越界被拒」这个事实在只读当前状态的接口下完全不可见。

这正是「事件是真相，状态是投影」的实证：
**投影丢掉了历史，而审计问题恰恰需要历史。**

### 3.2 代价是真实的

B 组多花 **7.2 倍 token**、**2 倍步数**。
主因是 `log` 的输出很长（39 条事件），每次都要重新进入上下文。

**这不是免费的。** 论文里必须写明。

### 3.3 这个结论有边界

同一套接口在**简单任务**上（第一轮实验）表现更差：

| 任务类型 | A 组 | B 组 |
|---|---|---|
| 跑一次演示并确认 | 2/2，2,706 token | 0/2（被格式绊住），15,457 token |
| 需要历史的三问 | **0/3** | **3/3**，34,311 token |

**所以正确的表述不是「事件流更好」，而是：**

> 事件流对**需要历史的问题**是必要的，对**即时操作**是负担。

---

## 4. 局限

1. **单模型**（deepseek-flash）、单任务族、n=3 —— 不能外推。
2. **任务刻意选择「需要历史」** —— 这是设计使然，用于验证「没有它就是不行」，不代表普遍收益。
3. **`assert` 语法仍有摩擦**：轨迹里出现 `assert key=oob value=18`、`assert oob 18`（后者已修）。
4. **A 组并非无能力**：它能跑演示、能看 `ps`，只是没有历史。
5. 实验者对真值的核对使用了 `log`，这是**观察者能力**，不属于被测对象。

---

## 5. 复现

```bash
# 构建两版内核
make build/kernel.elf build/kernel-min.elf

# 跑实验（需 DEEPSEEK_API_KEY）
cd web
DS_KEY=<key> ONLY=A TRIALS=3 MAX_STEPS=10 node exp2.js
DS_KEY=<key> ONLY=B TRIALS=3 MAX_STEPS=14 node exp2.js
```

脚本：`web/exp2.js`（历史审计任务）、`web/exp.js`（简单任务，第一轮）

---

## 6. 一句话结论

> **没有事件流时，agent 对「发生过什么」只能猜，且稳定地猜错；
> 有事件流时它能准确回答，代价是 7 倍的 token。**

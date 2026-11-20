# rvsim 桌面功能规格与验收标准

这份文档是**测试的唯一依据**，也是**设计时的接口登记表**。

规矩只有一条，但它必须被当真的：

> **一个功能在写实现之前，先在这里登记它的观测接口。**
> 登记不出来，说明这个功能做出来也没法验证，那就先别做。
> 事后补登记不算——事后补的接口必然是被实现细节绑架的，测不到真正该测的东西。

---

## 0. 测试接口约定

桌面一共暴露四类接口。四类都是**产品的一部分**，不是测试脚手架：
宿主自己读状态、AI 操控桌面、自动化回归，走的都是这四类。

### 0.1 观测接口：`window.rv.state`（内核发布的真实状态）

内核不画像素，只往固定物理地址写结构化数据；`rv.state` 把它翻译成对象。
这是唯一可信的「系统真实状态」——DOM 是它的渲染结果，不是它的来源。

| 字段 | 类型 | 含义 |
|---|---|---|
| `wins[3]` | `{id,title,focused,visible,x,y,z,w,h}` | 窗口。坐标是 800×600 逻辑桌面，不是像素 |
| `procs[]` | `{pid,state,pc}` | 进程表，`state` ∈ READY/RUN/EXIT |
| `files[]` | `{name,size}` | RAMFS 文件 |
| `term[22]` | `string[22]` | 终端字符网格，61 列 × 22 行 |
| `rate` | `number` | 指令吞吐 M/s |
| `cycles` / `pc` | `number` / `string` | 已执行周期、当前 PC |

### 0.2 观测接口：`window.rv.ui`（宿主侧的可见状态）

内核不知道的东西（菜单开没开、焦点环在哪、壁纸是哪一档）全部集中在这里。
**宿主新增任何可见状态，必须同时加进这个对象**，否则它就只存在于像素里。

`booted` `drawer` `startMenu` `trayPanel` `snapPicker` `snapGhost` `ctxMenu`
`tip` `hostWins` `dockActive` `iconCount` `selectedIcons` `focusedWindow`
`snapClasses` `termFontSize` `wallStyle` `iconsVisible` `clock` `rate`

### 0.3 驱动接口：`window.rv.*`（模拟用户动作）

原则：**幂等**。`ui_startMenu(true)` 是「打开」，不是「切换」——
参数写成布尔就必须是幂等语义，否则测试和 AI 都会踩同一个坑。

| 接口 | 语义 |
|---|---|
| `launch(id)` / `ui_open(id)` | 启动应用（等价双击图标） |
| `closeWin(i)` / `focusWin(i)` | 关闭 / 聚焦窗口 i |
| `ui_winBtn(i,"min"\|"max"\|"close")` | 点窗控按钮 |
| `ui_snapTo(i, zone)` | 吸附到 `left/right/top/tl/tr/bl/br`，或 `"restore"` 还原默认尺寸 |
| `ui_pickLayout(i, layoutIdx, cellIdx)` | 布局选择器第 `layoutIdx` 种布局的第 `cellIdx` 格 |
| `ui_startMenu(bool)` / `ui_drawer(bool)` / `ui_tray(bool)` | 开合三个浮层 |
| `ui_snapPicker(i)` / `ui_ctx(items,x,y)` | 弹吸附选择器 / 弹右键菜单 |
| `ui_clickIcon(app)` / `ui_dblIcon(app)` | 单击 / 双击桌面图标 |
| `ui_dblFile(name)` | 双击文件行 |
| `type(s)` / `cmd(s)` | 向 UART 写原文 / 写一条命令并回车 |
| `click(x,y)` | 往内核送一次点击（逻辑坐标） |

### 0.4 DOM 契约（选择器、类名、`data-*`）

这些不是「测试顺手抓的元素」，是**样式与脚本的公共约定**，改名等于改接口。

- 桌面：`#os` `#wall` `#icons` `#dock` `#clock` `#boot`
- 图标：`.dicon[data-app]`，选中态 `.dicon.sel`
- Dock：`.dock-app[data-app]`，运行指示 `.dock-app.active`，`#startBtn` `#dbgBtn`
- 窗口：`.win`（`.focused` `.dragging` `.closing` `.minimizing` `.snap-l/r/t`）、
  `.win-bar`、`.win-ctl .min/.max/.close`、`.win-body`、`.term-dom`、`.tcur`、`.panel-dom`
- 浮层：`#ctxMenu.on .ctx-item`、`#tip.on`、`#trayPanel.on`、`#snapPicker .sl`/`.sl-cell`、
  `#snapGhost.on`、`#startMenu`、`#smTiles .sm-tile`、`#smRecent .sm-ri`、`#smSearch`、`.sm-power`
- 宿主应用窗口：`.host-win`、`.host-win .win-title`
- 表格：`.ftab tr[data-name][data-dir]`、`.about-grid div`、`.kv`
- 调试抽屉：`#drawer` `#procs .prow` `#reg .rg` `#caps .cap` `#files .frow` `#events .ev`
- 开机：`#boot`、`.boot-bar`、`#bootBar`、`.boot-status`
- 设置项：`#setWall button[data-w]`、`#setIcons`、`#setFs button[data-f]`、`#setBoot`

### 0.5 内存契约（内核 ↔ 宿主的固定地址）

| 地址 | 结构 | 方向 |
|---|---|---|
| `0x83FFE000` | `win_info_t[3]`，每项 11×u32 `{buf,w,h,titlePtr,focused,visible,x,y,z,win_w,win_h}` | 内核 → 宿主 |
| `0x83FFE200` | `win_cmd`，`v = idx*10+act+1`，`0` 表示无命令 | 宿主 → 内核 |
| `0x83FFE210` | `win_pos_t {idx,x,y,w,h,seq}` | 宿主 → 内核 |
| `0x83FE0000` | `term_sync_t {magic"TRM1",cols,rows,cells_addr,rowseq_addr,cursor_x,cursor_y,cursor_visible,seq}` | 内核 → 宿主 |
| `0x83FD0000` | `proc_row_t[5]`，20 B/行 | 内核 → 宿主 |
| `0x83FD1000` | `file_row_t[16]`，40 B/行 | 内核 → 宿主 |
| `0x83FD2000` | `sys_sync_t {magic"SYS1",…,nproc,nfile,nrun}` | 内核 → 宿主 |

命令字 `+1` 偏移是硬约定：`idx=0, act=0`（关闭第一个窗口）打包出来是 `0`，
不加偏移就会被内核当成「没有命令」，关闭第一个窗口会静默失败。

### 0.6 持久化契约（`localStorage`）

`rvsim.wall`（壁纸档位 0/1/2）、`rvsim.termScale`（字号 0.9/1/1.15）、`rvsim.skipBoot`（"1" 跳过开机动画）。

---

## 1. 启动

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 1.1 | 开机画面 | 有 logo、进度条、状态文字 | `#boot`、`.boot-bar`、`.boot-status` | 三个元素都存在 | ✓ |
| 1.2 | 进度推进 | 进度值随阶段上升 | `#bootBar.style.width` | 采样序列单调不减且最终到 100% | ✓ |
| 1.3 | 就绪后淡出 | shell 出现后 `#boot` 被移除 | `rv.ui.booted` | 为 `true` | ✓ |
| 1.4 | 跳过开机动画 | 设置里关掉后下次加载无 `#boot` | `localStorage["rvsim.skipBoot"]` | 置 `"1"` 后刷新，`#boot` 不存在 | ✓ |

## 2. 桌面

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 2.1 | 壁纸 | 默认极光渐变 | `rv.ui.wallStyle`、`#wall.style.background` | 非空且以 `radial-gradient` 开头 | ✓ |
| 2.2 | 壁纸可选 | 3 档且立即生效 | `#setWall button[data-w]` → `rv.ui.wallStyle` | 三档取值两两不同 | ✓ |
| 2.3 | 桌面图标 | 6 个，有图标和标签 | `.dicon[data-app]`、`rv.ui.iconCount` | 数量 6，每个有 `<svg>` 和 `<span>` | ✓ |
| 2.4 | 单击选中 | 只有一个高亮 | `rv.ui.selectedIcons` | 长度为 1 | ✓ |
| 2.5 | 双击打开 | 对应应用可见 | `rv.ui_dblIcon(app)` → `rv.state.wins[i].visible` | 为 `true` | ✓ |
| 2.6 | 图标键盘可达 | Tab 能到，Enter 能开 | `document.activeElement.className`、`KeyboardEvent` | Tab 序列含 `.dicon`；Enter 后窗口可见 | ✓ |
| 2.7 | 右键菜单 | 刷新 / 显示设置 / 关于本机 | `rv.ui_ctx(...)` → `rv.ui.ctxMenu` | 三项且文案匹配 | ✓ |
| 2.8 | 刷新 | 点「刷新」图标重建 | `rv.ui_ctx` 的 `刷新` 项 → `iconCount` | 数量不变且无 `pageerror` | ✓ |

## 3. 窗口管理

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 3.1 | 打开 | Dock / 图标 / 磁贴三种入口都行 | `launch` / `ui_dblIcon` / `#smTiles .sm-tile` | 三种方式后 `visible === true` | ✓ |
| 3.2 | 关闭 | 窗控 ✕ / Ctrl+W / 右键菜单 | `ui_winBtn(i,"close")`、`Control+w` | `visible === false` | ✓ |
| 3.3 | 最小化 | 窗控 — / Ctrl+M | `ui_winBtn(i,"min")`、`Control+m` | `visible === false` | ✓ |
| 3.4 | 最大化 | 800×600 | `ui_winBtn(i,"max")` → `rv.state.wins[i]` | `[x,y,w,h] === [0,0,800,600]` | ✓ |
| 3.5 | 还原 | 回到最大化前尺寸 | 再次 `ui_winBtn(i,"max")` | `[w,h]` 回到原值 | ✓ |
| 3.6 | 拖动 | 窗口跟随鼠标，位移一比一 | `mouse.down/move/up` → `wins[i].x/y` | 逻辑位移与像素位移在 1px 内一致 | ✓ |
| 3.7 | 焦点 | 点标题栏后该窗口 `focused` | `rv.ui.focusedWindow` | 等于该窗口下标 | ✓ |
| 3.8 | z 序 | 聚焦窗口 z 最大 | `wins[i].z` | 等于 `max(z)` | ✓ |
| 3.9 | 吸附左/右 | 400×600 | `ui_snapTo(i,"left"/"right")` | `[x,w] === [0,400]` / `[400,400]` | ✓ |
| 3.10 | 吸附顶部 | 800×600 | `ui_snapTo(i,"top")` | `[w,h] === [800,600]` | ✓ |
| 3.11 | 吸附四角 | 400×300 | `ui_snapTo(i,"tl"/"tr"/"bl"/"br")` | `[x,y,w,h]` 各自匹配 | ✓ |
| 3.12 | 并排吸附 | 拖到另一窗口边缘吸到对应半边 | 拖动到邻居边缘 → `wins[i]` | `[x,w] === [0,400]` | ✓ |
| 3.13 | 贴边圆角 | 吸附后贴边侧 radius 为 0 | `getComputedStyle(.win).border*Radius` | 左侧 `0px`、另一侧 `8px` | ✓ |
| 3.14 | 布局选择器 | 4 种布局 11 个区域 | `ui_snapPicker(i)` → `#snapPicker .sl`/`.sl-cell` | 4 个布局、11 个格子 | ✓ |
| 3.15 | 选择器应用 | 点格子后窗口变到该区域 | `ui_pickLayout(i,l,c)` → `wins[i]` | 等于格子比例换算值 | ✓ |
| 3.16 | 双击标题栏 | 最大化 ↔ 还原 | `mouse.dblclick(.win-bar)` | 两次双击后 `[800,600]` → `[480,360]` | ✓ |
| 3.17 | Alt+方向 | 左/右/上/下四种 | `Alt+Arrow*` → `wins[i]` | 四种结果各自匹配 | ✓ |
| 3.18 | Ctrl+W / Ctrl+M | 关闭 / 最小化 | `Control+w` / `Control+m` | `visible === false` | ✓ |
| 3.19 | 右键菜单 | 还原/最小化/最大化/关闭 | `#ctxMenu.on .ctx-item` | 四项且文案匹配 | ✓ |
| 3.20 | 不能被拖出桌面 | 拖到边界外被夹住 | 拖到负坐标 → `wins[i]` | `x >= -w+120` 且 `y >= 0` | ✓ |
| 3.21 | 关闭动画 | 关闭前有 `closing` 类 | `.win.closing` | 点击后 60ms 内存在 | ✓ |
| 3.22 | 最小化动画 | 最小化前有 `minimizing` 类 | `.win.minimizing` | 同上 | ✓ |

## 4. Dock

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 4.1 | 应用图标 | 6 个 | `.dock-app` | 数量 6 | ✓ |
| 4.2 | 运行指示条 | 可见窗口对应图标 `active` | `rv.ui.dockActive` | 对应位为 `true` | ✓ |
| 4.3 | 全关后指示条灭 | 全部关闭后 6 个都 false | `closeWin(0/1/2)` → `dockActive` | 全 `false` | ✓ |
| 4.4 | 悬停放大 | `scale(1.08)` | `pointerenter` → `getComputedStyle(.dock-app).transform` | 矩阵首项 > 1 | ✓ |
| 4.5 | 悬停 tooltip | 显示应用名 | `rv.ui.tip` | 等于应用名 | ✓ |
| 4.6 | 点击启动 | 点击后窗口可见 | `.dock-app` click → `wins[i].visible` | 为 `true` | ✓ |
| 4.7 | 开始按钮 | 点击开关开始菜单 | `#startBtn` click → `rv.ui.startMenu` | 开→关→开 | ✓ |
| 4.8 | 调试按钮 | 点击开关抽屉 | `#dbgBtn` click → `rv.ui.drawer` | 开→关 | ✓ |
| 4.9 | 托盘时钟 | `HH:MM` | `rv.ui.clock` | 正则 `^\d{2}:\d{2}$` | ✓ |
| 4.10 | 托盘日期 | `M/D` | 托盘日期文本 | 非空且含 `/` | ✓ |
| 4.11 | 速率显示 | `x.x M/s` | `rv.ui.rate`、托盘文本 | 非 `—` | ✓ |

## 5. 开始菜单

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 5.1 | 打开/关闭 | 点开始按钮切换 | `#startBtn` click → `rv.ui.startMenu` | 两次点击后开→关 | ✓ |
| 5.2 | 点外部关闭 | 点桌面后关闭 | 点 `#os` → `startMenu` | 为 `false` | ✓ |
| 5.3 | Escape 关闭 | 按键后关闭 | `Escape` → `startMenu` | 为 `false` | ✓ |
| 5.4 | 搜索框 | 存在且可输入 | `#smSearch` | 存在且可 `fill` | ✓ |
| 5.5 | 搜索过滤 | 输入 `mon` 只剩 Monitor | `#smTiles .sm-tile` 可见集合 | 长度 1 且含 Monitor | ✓ |
| 5.6 | 6 个磁贴 | 数量 6 | `.sm-tile` | 数量 6 | ✓ |
| 5.7 | 6 条推荐 | 数量 6 | `.sm-ri` | 数量 6 | ✓ |
| 5.8 | 磁贴启动 | 点 Settings 打开设置 | 磁贴 click → `rv.ui.hostWins` | 含 Settings 且可见 | ✓ |
| 5.9 | 推荐项发命令 | 点 `ps` 后终端有输出 | `.sm-ri` click → `rv.state.term` | 终端出现进程表内容 | ✓ |
| 5.10 | 终端关着时推荐项 | 自动拉起终端 | 先 `closeWin(0)` 再点 | `wins[0].visible === true` | ✓ |
| 5.11 | 用户条 | 显示 root | `.sm-user` 文本 | 含 `root` | ✓ |
| 5.12 | 电源按钮 | 重启内核 | `.sm-power` click → `rv.ready` + `state.term` | 仍 ready，且终端重新出现开机横幅 | ✓ |

## 6. 托盘面板

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 6.1 | 点击打开 | `#trayPanel` 显示 | `#dockTray` click → `rv.ui.trayPanel` | 为 `true` | ✓ |
| 6.2 | 时间格式 | `HH:MM:SS` | 面板时间文本 | 正则 | ✓ |
| 6.3 | 日期行 | 含年月日星期 | `.tp-date` 文本 | 含「星期」或 `/` | ✓ |
| 6.4 | 时区 | 非空 | `.tp-dow`/日期行 | 文本非空 | ✓ |
| 6.5 | 日历 | 28–31 天，今天高亮 | `.tp-day`、`.tp-day.today` | 数量在 28–31，`today` 恰好 1 个 | ✓ |
| 6.6 | 不溢出 | 面板在视口内 | `getBoundingClientRect` | `left>=0 && right<=innerWidth && bottom<=innerHeight` | ✓ |
| 6.7 | 点外部关闭 | 点桌面后关闭 | 点 `#os` → `trayPanel` | 为 `false` | ✓ |
| 6.8 | 右键菜单 | 两项 | `rv.ui.ctxMenu` | 长度 2 | ✓ |

## 7. 应用

### 7.1 Terminal

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 7.1.1 | 字符网格 | 22 行 × 61 列 | `rv.state.term` | `length === 22`，每行 ≤ 61 显示列 | ✓ |
| 7.1.2 | 提示符 | 含 `rvsh>` | `rv.state.term` | 任一行含 `rvsh>` | ✓ |
| 7.1.3 | 命令执行 | `whoami` 有 JSON 输出 | `rv.type("whoami\r")` | 终端含 `"user"` | ✓ |
| 7.1.4 | 中文 | `echo 中文` 能显示 | `rv.type(...)` | 终端含「中文」 | ✓ |
| 7.1.5 | 光标 | 有光标元素且位置跟随 | `.tcur` 的 `left/top` | 随光标列/行变化 | ✓ |
| 7.1.6 | 字号自适应 | 窗口变宽字号变大 | `rv.ui.termFontSize` | 最大化后变大 | ✓ |
| 7.1.7 | 不折行 | 无横向溢出 | `scrollWidth` vs `clientWidth` | 相等 | ✓ |
| 7.1.8 | Ctrl+C | 发中断 | `Control+c` | 终端出现 `^C` 或新提示符 | ✓ |
| 7.1.9 | 粘贴 | 粘贴文本进终端 | `insertText` / `rv.type` | 终端出现粘贴内容 | ✓ |

### 7.2 Monitor

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 7.2.1 | 进程表 | 3 行 | `.panel-dom tbody tr` | 数量 3 | ✓ |
| 7.2.2 | 列齐全 | PID / STATE / PC | 表头文本 | 三列都在 | ✓ |
| 7.2.3 | 数据真实 | 与 `rv.state.procs` 一致 | 逐行比对 | 每行 PID 与状态匹配 | ✓ |

### 7.3 Files

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 7.3.1 | 文件列表 | 5 行 | `.ftab tr[data-name]` | 数量 5 | ✓ |
| 7.3.2 | 列齐全 | NAME / SIZE | 表头文本 | 两列都在 | ✓ |
| 7.3.3 | 双击 .elf | 终端出现 `run hello.elf` 并真的跑起来 | `ui_dblFile("hello.elf")` → `rv.state.term` | 含 `run hello.elf`，且随后含 `exit(0)` | ✓ |
| 7.3.4 | 双击 .txt | 终端出现 `cat readme.txt` 与文件内容 | `ui_dblFile("readme.txt")` | 含 `cat readme.txt` 且含文件首行 | ✓ |

### 7.4 Settings

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 7.4.1 | 壁纸切换 | 3 档立即生效 | `#setWall button[data-w]` → `rv.ui.wallStyle` | 三档互不相同 | ✓ |
| 7.4.2 | 图标开关 | 关闭后 `#icons` 隐藏 | `#setIcons` → `rv.ui.iconsVisible` | 由 `true` 变 `false` | ✓ |
| 7.4.3 | 字号 3 档 | 「大」真的更大 | `#setFs button[data-f]` → `rv.ui.termFontSize` | 单调递增 | ✓ |
| 7.4.4 | 开机动画 | 开关写入 localStorage | `#setBoot` → `localStorage["rvsim.skipBoot"]` | 值为 `"1"` / 被移除 | ✓ |
| 7.4.5 | 硬件信息 | 4 行 | `.kv` | 数量 4 | ✓ |

### 7.5 Guide / 7.6 About

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 7.5.1 | Guide 打开 | 窗口出现 | `rv.ui.hostWins` | 含 Guide 且可见 | ✓ |
| 7.5.2 | Guide 可滚动 | 内容超出时 `overflow:auto` | `getComputedStyle` | `overflow-y` 为 `auto`/`scroll` | ✓ |
| 7.6.1 | About 打开 | 窗口出现 | `rv.ui.hostWins` | 含 About 且可见 | ✓ |
| 7.6.2 | About 规格 | 6 格信息 | `.about-grid div` | 数量 6 | ✓ |
| 7.6.3 | 关闭 | 点 ✕ 后消失 | `.host-win .close` click | `hostWins` 不含该项 | ✓ |

## 8. 调试抽屉

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 8.1 | 打开 | ⚙ 或 F12 | `rv.ui_drawer(true)` / `F12` → `rv.ui.drawer` | 为 `true` | ✓ |
| 8.2 | 进程表 | ≥4 行 | `#procs .prow` | 数量 ≥ 4 | ✓ |
| 8.3 | 寄存器 | 32 个 | `#reg .rg` | 数量 32 | ✓ |
| 8.4 | 能力清单 | >20 条 | `#caps .cap` | 数量 > 20 | ✓ |
| 8.5 | 文件 | 5 条 | `#files .frow` | 数量 5 | ✓ |
| 8.6 | 事件流 | ≥1 条 | `#events .ev` | 数量 ≥ 1 | ✓ |
| 8.7 | 运行时 | 周期/速率/PC/特权级有值 | `#vCy` `#vIn` `#vPc` `#vPv` | 均非 `—` 且周期不为 `0` | ✓ |
| 8.8 | 关闭 | 再点一次关闭 | `rv.ui_drawer(false)` | `drawer === false` | ✓ |

## 9. 无障碍

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 9.1 | Tab 可达 | 能到图标 / 窗控 / Dock | `document.activeElement` 序列 | 24 次 Tab 内出现 `.dicon` 与 `.dock-app` | ✓ |
| 9.2 | 焦点环 | 2px 白色描边 | `getComputedStyle(:focus-visible)` | `outline-width === 2px`、色为白 | ✓ |
| 9.3 | Enter 激活 | 图标 Enter 能开 | `.dicon` focus + `Enter` | 窗口可见 | ✓ |
| 9.4 | 对比度 | 正文 ≥ 4.5:1 | `--t1`/`--t2` 与 `--s0` 计算 | 相对亮度比 ≥ 4.5 | ✓ |
| 9.5 | 减少动效 | `prefers-reduced-motion` 生效 | `matchMedia` + 规则存在 | 命中后 `animation` 为 none | ✓ |

## 10. 性能

| # | 指标 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 10.1 | 帧率 | ≥ 60fps | rAF 计数 | 采样 1s 计数 | ✓ 178 |
| 10.2 | 输入延迟 | < 100ms | keydown 时间戳 → 网格更新 | 差值 < 100ms | ✓ 9–18ms |
| 10.3 | 启动 | < 3s | `goto` → `rv.ready` | 计时 | ✓ 1.5s |
| 10.4 | 拖动跟手 | 位移一比一 | 采样鼠标与窗口坐标 | 每段偏差 < 2px | ✓ |

## 11. 状态持久化

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 11.1 | 壁纸 | 刷新后保持 | `localStorage["rvsim.wall"]` | 重载后 `wallStyle` 与设置前一致 | ✓ |
| 11.2 | 字号 | 刷新后保持 | `localStorage["rvsim.termScale"]` | 重载后 `termFontSize` 一致 | ✓ |
| 11.3 | 开机动画 | 刷新后保持 | `localStorage["rvsim.skipBoot"]` | 重载后 `#boot` 不存在 | ✓ |

## 12. 边界情况

| # | 场景 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 12.1 | 全部关闭后恢复 | 双击图标能重新打开 | `closeWin(0..2)` → `ui_dblIcon` | 三条关闭全部生效，再开 `visible === true` | ✓ |
| 12.2 | 右键菜单在角落 | 不溢出视口 | `ui_ctx(items, innerWidth-4, innerHeight-4)` | `right<=innerWidth && bottom<=innerHeight` | ✓ |
| 12.3 | tooltip 在边缘 | 不溢出视口 | 悬停最后一个 Dock 图标 | 同上 | ✓ |
| 12.4 | 窗口重叠 z 序 | 聚焦的在上 | `focusWin(i)` → `wins[i].z` | 等于 `max(z)` | ✓ |
| 12.5 | 反复开关 20 次 | 状态一致无泄漏 | 循环 `launch`/`closeWin` | 每次 `visible` 与预期一致 | ✓ |

---

## 13. 这轮从「测不出来」到「测得出来」的清单

这几条是清单先行的直接产物：先登记接口，才发现问题不在实现细节而在设计。

| 现象 | 根因 | 修法 |
|---|---|---|
| 连续关闭三个窗口只关掉一个 | 命令字只有一个槽位，宿主连发会互相覆盖 | 宿主侧串行队列，等内核清零再发下一条 |
| 右键菜单在右下角溢出 1.4px | 复用了居中菜单的 `smIn`，带 `translateX(-50%)` | 按 px 定位的浮层改用 `popIn` |
| 键盘永远走不出第一个元素 | Tab 被无条件拦截送进 UART，而内核没有补全 | 取消拦截，Tab 交还浏览器 |
| 桌面图标读不出壁纸档位 | 默认壁纸只写在 CSS 里 | 开机把默认值落成 inline style |
| `ui_startMenu(true)` 有时把菜单关掉 | 打开函数里带了 toggle | 打开幂等，开合归按钮 |
| 布局选择器选完，窗口被别的窗口盖住 | 应用布局没有聚焦 | 应用布局后下发聚焦命令 |
| 执行 `run` 后开机横幅重印，刚敲的命令被顶出屏幕 | 进程退出重入 shell 时又打了一遍 MOTD | 横幅只在开机打一次 |
| 双击标题栏最大化偶尔无效 | 窗口未置顶时点击落在别的窗口上 | 由「应用布局/吸附即聚焦」保证 |

---

## 统计

| 状态 | 数量 |
|---|---|
| ✓ 已实现且测过 | 88 |
| ? 待验证 | 0 |
| ✗ 未实现 | 0 |

**测试文件**：`scripts/ui/e2e.js`，116 个用例，用例编号与本文档编号一一对应。
最近一次执行：**116/116 通过**。

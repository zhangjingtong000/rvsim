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
| `procs[]` | `{pid,state,pc}` | 进程表，`state` ∈ READY/RUN/EXIT/UNUSED/SUSPEND |
| `files[]` | `{name,size}` | RAMFS 文件。开机种子：`hello.elf` `fork.elf` `fdtest.elf` `badptr.elf` `spin.elf` `readme.txt` |
| `term[22]` | `string[22]` | 终端字符网格，61 列 × 22 行 |
| `rate` | `number` | 指令吞吐 M/s |
| `cycles` / `pc` | `number` / `string` | 已执行周期、当前 PC |

### 0.2 观测接口：`window.rv.ui`（宿主侧的可见状态）

内核不知道的东西（菜单开没开、焦点环在哪、壁纸是哪一档）全部集中在这里。
**宿主新增任何可见状态，必须同时加进这个对象**，否则它就只存在于像素里。

`booted` `drawer` `startMenu` `trayPanel` `snapPicker` `snapGhost` `ctxMenu`
`tip` `hostWins` `dockActive` `iconCount` `selectedIcons` `focusedWindow`
`snapClasses` `termFontSize` `wallStyle` `iconsVisible` `clock` `rate`

另有一个不属于「可见状态」但必须可读的字段：`rv.fsCwd` —— 文件浏览器的当前目录。
它决定列表里出现哪些行，测不出来就等于这一层交互无法验证。

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
| `fsWrite(name,text)` / `fsRead(name)` | 整文件写入 / 读出。返回 Promise<rc> / Promise<string\|null> |
| `fsRemove(name)` / `fsRename(from,to)` / `fsMkdir(name)` | 删除 / 重命名 / 建目录 |
| `fsChdir(path)` / `fsCwd` | 切换文件浏览器目录 / 读当前目录 |
| `openFile(name)` | 按文件类型打开：目录进入、`.elf` 运行、文本进编辑器 |
| `killProc(pid)` / `suspendProc(pid)` / `resumeProc(pid)` | 结束 / 挂起 / 恢复进程 |

文件与进程那几组接口和图形界面的按钮是**同一条通路**：宿主把命令写进内核的控制块，
内核执行完把结果写回。AI 操控桌面走的就是这条通路，不是模拟鼠标。

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
- 表格：`.ftab tr[data-name][data-dir]`、`.ptab tr[data-pid][data-st]`、`.about-grid div`、`.kv`
- 文件浏览器：`.fbar button[data-f=up|new|mkdir|rename|del|import]`、`.fpath`、
  `.fscroll`、`tr.sel`（选中行）、`.fdlg`/`.fdlg-box`（窗口内输入框）
- 文本编辑器：`.ed-wrap`、`.ed-bar`、`.ed-path`、`.ed-area`、`.ed-save`、`.ed-reload`、`.ed-st`
- 调试抽屉：`#drawer` `#procs .prow` `#reg .rg` `#caps .cap` `#files .frow` `#events .ev`
- 开机：`#boot`、`.boot-bar`、`#bootBar`、`.boot-status`
- 设置项：`#setWall button[data-w]`、`#setIcons`、`#setFs button[data-f]`、`#setBoot`

### 0.5 内存契约（内核 ↔ 宿主的固定地址）

| 地址 | 结构 | 方向 |
|---|---|---|
| `0x83FFE000` | `win_info_t[3]`，每项 11×u32 `{buf,w,h,titlePtr,focused,visible,x,y,z,win_w,win_h}` | 内核 → 宿主 |
| `0x83FFE200` | `win_cmd`，`v = idx*10+act+1`，`0` 表示无命令 | 宿主 → 内核 |
| `0x83FFE210` | `win_pos_t {idx,x,y,w,h,seq}` | 宿主 → 内核 |
| `0x83FE0000` | `term_sync_t`，17 个 u32，见 0.5 | 内核 → 宿主 |
| `0x83FD0000` | `proc_row_t[5]`，20 B/行 | 内核 → 宿主 |
| `0x83FD1000` | `file_row_t[16]`，40 B/行 | 内核 → 宿主 |
| `0x83FD2000` | `sys_sync_t {magic"SYS1",…,nproc,nfile,nrun}` | 内核 → 宿主 |
| `0x83FFC000` | `host_cmd_t {magic"HCMD",seq,op,a0,len,rc,p0[64],p1[64]}` | 宿主 → 内核 |
| `0x83FFD000` | 命令数据页（≤ 4096 B），写文件的内容、读文件的结果都走这里 | 双向 |

失败码是分开的，界面据此给不同提示，不会把「名字太长」和「槽位满」混成一句话：

| rc | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 名字不合法 / 太长（上限 23 字节） |
| 2 | 文件槽用尽（最多 16 个） |
| 3 | 已经有同名条目 |

`host_cmd_t` 是图形界面的**系统调用入口**：`op` 1 写文件 / 2 删除 / 3 重命名 /
4 建目录 / 5 结束进程 / 6 挂起 / 7 恢复 / 8 读文件。宿主写完把 `magic` 置成
`'HCMD'`，内核每轮读一次、执行、把 `rc` 写回并清 `magic`。
没有这条通路，图形界面就只能看不能改——之前正是如此。

命令字 `+1` 偏移是硬约定：`idx=0, act=0`（关闭第一个窗口）打包出来是 `0`，
不加偏移就会被内核当成「没有命令」，关闭第一个窗口会静默失败。

### 0.6 文件命名空间约定

RAMFS 是**平铺**命名空间，没有真正的目录树。路径一律规范成「不带前导 `/`」的形式：

- `fs_write("/docs/a.txt")` 与 `fs_write("docs/a.txt")` 是同一个文件，内核存 `docs/a.txt`
- `fs_mkdir("/docs")` 与 `fs_mkdir("docs")` 是同一个目录
- `rv.fsCwd` 存的是规范名（`docs`），路径栏显示成 `/docs`
- `.` 段被丢弃（`./z.txt` → `z.txt`），`..` 段直接拒绝——平铺命名空间没有上级，
  允许它只会造出一个进不去的文件；`.` 和 `..` 本身也不能当文件名

不统一的话，shell 里 `mkdir /docs` 和图形界面里新建 `docs` 会变成两个目录，
而 `fs_under` 两种写法都接受——查得到、列不出，是最难查的那种不一致。

### 0.7 持久化契约（`localStorage`）

`rvsim.wall`（壁纸档位 0/1/2）、`rvsim.termScale`（字号 0.9/1/1.15）、`rvsim.skipBoot`（"1" 跳过开机动画）。

---

### 0.5 `term_sync_t` 完整字段（17 个 u32，**只能追加**）

宿主按固定偏移读。新字段一律加在末尾，**不能插在中间**，否则宿主读到的
每个字段都会错位。

| 偏移 | 字段 | 含义 |
|---|---|---|
| 0 | `magic` | `"TRM1"` |
| 1 | `cols` | 列数（61） |
| 2 | `rows` | 行数（22） |
| 3 | `cells_addr` | 当前屏字符单元数组的物理地址 |
| 4 | `rowseq_addr` | 每行序号数组地址 |
| 5 | `cursor_x` | 光标列 |
| 6 | `cursor_y` | 光标行 |
| 7 | `cursor_visible` | 光标是否可见 |
| 8 | `seq` | 内容序号，变了才重渲染 |
| 9 | `scroll_addr` | 回滚历史字符单元数组地址 |
| 10 | `scroll_head` | 历史环形缓冲的写头 |
| 11 | `scroll_count` | 历史里有效行数 |
| 12 | `scroll_lines` | 历史容量（2000） |
| 13 | `attr_addr` | **当前屏属性字节数组的地址**（不是值） |
| 14 | `scroll_attr_addr` | **历史属性字节数组的地址**（不是值） |
| 15 | `wrap_addr` | 当前屏换行标记数组地址 |
| 16 | `scroll_wrap_addr` | 历史换行标记数组地址 |

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
| 7.1.8 | Ctrl+C 放弃当前行 | 回显 `^C` 且行缓冲清空 | `Control+c` → `rv.state.term`，随后发一条命令 | 含 `^C`，且随后的命令**能执行**（行缓冲没残留） | ✓ |
| 7.1.9 | 粘贴 | 粘贴后回车能执行 | `ClipboardEvent` + `Enter` | 终端出现粘贴内容，且回到提示符 | ✓ |

### 7.2 Monitor

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 7.2.1 | 进程表 | 3 行 | `.panel-dom tbody tr` | 数量 3 | ✓ |
| 7.2.2 | 列齐全 | PID / STATE / PC | 表头文本 | 三列都在 | ✓ |
| 7.2.3 | 数据真实 | 与 `rv.state.procs` 一致 | 逐行比对 | 每行 PID 与状态匹配 | ✓ |

### 7.3 Files

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 7.3.1 | 文件列表 | 6 行（RAMFS 种子 6 个文件） | `.ftab tr[data-name]` | 数量 6 | ✓ |
| 7.3.2 | 列齐全 | NAME / SIZE | 表头文本 | 两列都在 | ✓ |
| 7.3.3 | 双击 .elf | 终端出现 `run hello.elf` 并真的跑起来 | `ui_dblFile("hello.elf")` → `rv.state.term` | 含 `run hello.elf`，且随后含 `exit(0)` | ✓ |
| 7.3.4 | 双击 .txt | 打开文本编辑器并载入内容 | `rv.openFile("readme.txt")` → `.ed-area` | 窗口标题含文件名，正文非空 | ✓ |

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
| 7.6.2 | About 规格 | 6 格信息且默认全部可见 | `.about-grid > div` 与 `.win-body` 的 `getBoundingClientRect` | 数量 6，且每格都在可视区内 | ✓ |
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
| 10.1 | 帧率 | ≥ 60fps | rAF 计数 | 采样 1.2s 计数 | ✓ 144（1440×900） |
| 10.2 | 输入延迟 | < 100ms | keydown 时间戳 → 网格更新 | 差值 < 100ms | ✓ 9–18ms |
| 10.3 | 启动 | < 3s | `goto` → `rv.ready` | 计时 | ✓ 1.5s |
| 10.4 | 拖动跟手 | 位移一比一 | 采样鼠标与窗口坐标 | 每段偏差 < 2px | ✓ |

## 11. 状态持久化

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 11.1 | 壁纸 | 刷新后保持 | `localStorage["rvsim.wall"]` | 重载后 `wallStyle` 与设置前一致 | ✓ |
| 11.2 | 字号 | 刷新后保持 | `localStorage["rvsim.termScale"]` | 重载后 `termFontSize` 一致 | ✓ |
| 11.3 | 开机动画 | 刷新后保持 | `localStorage["rvsim.skipBoot"]` | 重载后 `#boot` 不存在 | ✓ |
| 11.4 | 主题 | 刷新后保持 | `localStorage["rvsim.theme"]` | 重载后 `documentElement.dataset.theme` 与设置前一致 | ✓ |

## 12. 边界情况

| # | 场景 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 12.1 | 全部关闭后恢复 | 双击图标能重新打开 | `closeWin(0..2)` → `ui_dblIcon` | 三条关闭全部生效，再开 `visible === true` | ✓ |
| 12.2 | 右键菜单在角落 | 不溢出视口 | `ui_ctx(items, innerWidth-4, innerHeight-4)` | `right<=innerWidth && bottom<=innerHeight` | ✓ |
| 12.3 | tooltip 在边缘 | 不溢出视口 | 悬停最后一个 Dock 图标 | 同上 | ✓ |
| 12.4 | 窗口重叠 z 序 | 聚焦的在上 | `focusWin(i)` → `wins[i].z` | 等于 `max(z)` | ✓ |
| 12.5 | 反复开关 20 次 | 状态一致无泄漏 | 循环 `launch`/`closeWin` | 每次 `visible` 与预期一致 | ✓ |

---

## 13. 文件与进程操作（图形界面）

这一节回答的是「这到底是个能用的系统，还是个只读仪表盘」。
在补上之前，图形界面只能看：文件能打开不能改，进程能看不能停。

| # | 功能 | 验收标准 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 13.1 | Files 工具栏 | 6 个图标按钮 | `.fbar button[data-f]` | `["up","new","mkdir","rename","del","import"]` | ✓ |
| 13.2 | 新建文件 | 写进 RAMFS 且列表里能看到 | `rv.fsWrite` → `rv.state.files` + `tr[data-name]` | 内核与列表两处都出现 | ✓ |
| 13.3 | 新建文件夹 | 出现目录行 | `rv.fsMkdir` → `tr[data-dir]` | 该行 `data-dir="1"` | ✓ |
| 13.4 | 重命名 | 旧名消失、新名出现 | `rv.fsRename` → `rv.state.files` | 两处一致 | ✓ |
| 13.5 | 删除 | 从内核与列表同时消失 | `rv.fsRemove` → `rv.state.files` | 两处都没有 | ✓ |
| 13.6 | 目录导航 | 双击进入，路径栏与列表同步 | `rv.fsCwd`、`.fpath`、`tr[data-name]` | 路径等于该目录，列表只含直接子项 | ✓ |
| 13.7 | 返回上一级 | ↑ 回到根 | `.fbar button[data-f="up"]` | `fsCwd === ""` 且能看见刚才的子目录 | ✓ |
| 13.8 | 文件行右键 | 打开 / 重命名 / 删除 | `rv.ui.ctxMenu` | 三项且文案匹配 | ✓ |
| 13.9 | 文本编辑器 | 双击 `.txt` 打开，保存写回 RAMFS | `.ed-area` + `.ed-save` → `rv.fsRead` | 读回内容等于写入内容 | ✓ |
| 13.10 | 编辑器底色 | 背后文字不透上来 | `getComputedStyle(.ed-wrap).backgroundColor` | 无 alpha 通道（不透明） | ✓ |
| 13.11 | 终止进程 | 行右键「终止进程」后进程消失 | `tr[data-pid]` 右键 → `rv.state.procs` | 该 pid 不在表里 | ✓ |
| 13.12 | 挂起 / 恢复 | 挂起后状态 SUSPEND，恢复后能跑完 | `rv.suspendProc` / `rv.resumeProc` | 状态为 `SUSPEND`；恢复后进程全部 EXIT | ✓ |
| 13.13 | 桌面右键新建 | 含「新建文本文件…」 | `rv.ui.ctxMenu` | 含该项 | ✓ |
| 13.14 | 导入程序入口 | 能选本地 `.elf` 写进 RAMFS | `.fbar button[data-f="import"]` | 存在（点击打开文件选择器） | ✓ |
| 13.15 | 终止运行中的进程 | 程序跑飞时能停下来并回到 shell | `rv.killProc` → `rv.state.term` | 终端回到 `rvsh>` | ✓ |
| 13.16 | 中文文件名 | 内核、列表、打开三处都对 | `fsWrite("中文名测试.txt")` → `rv.state.files` + `tr[data-name]` + `fsRead` | 三处都是同一个中文名 | ✓ |
| 13.17 | 文件名不注入 HTML | 名字里的 `<b>` / `<img>` / 引号不当标签 | `.ftab tbody` 里是否有 `b`/`img` 元素；`tr.dataset.name` | 没有元素，`data-name` 完整 | ✓ |
| 13.18 | 删目录级联 | 子项一起删掉 | `fsRemove(dir)` → `fsRead(dir/kid)` | 返回 `null` | ✓ |
| 13.19 | 目录改名迁子项 | 子项跟着到新路径 | `fsRename(dir, dir2)` → 两个路径各读一次 | 新路径有、旧路径没有 | ✓ |
| 13.20 | 文件与目录不互相顶替 | 类型冲突被拒且原因可区分 | `fsMkdir(已存在的文件)` / `fsWrite(目录)` | 分别返回 3（同名）/ 1（类型冲突） | ✓ |
| 13.21 | 超长名字被拒 | 不静默截断 | `fsWrite("L"*30)` → `rv.state.files` | rc≠0，且没有截断后的条目 | ✓ |
| 13.22 | 超量内容被拒 | 不静默丢尾部 | `fsWrite(name, "A"*5000)` | Promise reject，且没建出文件 | ✓ |
| 13.23 | 不存在的进程 | 操作报失败 | `killProc(99)` / `killProc(-1)` | 都返回非 0 | ✓ |
| 13.24 | 不能进文件或不存在路径 | `fsChdir` 校验 | `fsChdir("readme.txt")` / `fsChdir("no/such")` | `fsCwd` 不变 | ✓ |
| 13.25 | 失败有可见提示 | 路径栏出现红色错误文字并自动恢复 | 走 UI 重命名到已存在 → `.fpath` 文本与颜色 | 含「失败」，色值 `248,113,113`，3s 后恢复 `/` | ✓ |
| 13.26 | 跑飞的程序能被单独终止 | 进程在跑时表里能看到 RUN，逐个终止后回到 shell | `run spin.elf` → `rv.state.procs` → `killProc` | 出现过 `RUN`；全部终止后表为空且终端回到 `rvsh>` | ✓ |

## 附录 A. 这轮从「测不出来」到「测得出来」的清单

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
| 按一次 Ctrl+C，之后所有命令都失效 | 0x03 被当成普通字符塞进行缓冲，之后每条命令都拼在它后面 | 内核把 Ctrl+C 当「放弃当前行」处理 |
| About 只显示 6 格里的 2 格 | 窗口固定 344px，内容 498px | 宿主窗口按内容定高 |
| 文件能打开不能改，进程能看不能停 | 图形界面没有下行命令通道，只有 UART 一个入口 | 新增 `host_cmd_t` 控制块，图形界面与 AI 共用 |
| 根目录下重命名必然失败 | 父路径用 `slice(0, lastIndexOf("/"))`，没有 `/` 时返回 -1，把文件名砍掉一个字当目录 | 判断 `slash > 0` 才算父路径 |
| `/docs` 与 `docs` 是两个目录 | 内核只按原样存名字，两种写法互不相认 | 内核统一规范化，宿主只保留一份规范名 |
| 文件操作失败时界面毫无反应 | 失败分支只有 `.catch(refreshFiles)`，静默吞掉 | 失败在路径栏留一句话 |
| 中文文件名在界面上是乱码，双击打不开 | 逐字节 `String.fromCharCode`，把 UTF-8 当 Latin-1 | 统一用 `TextDecoder("utf-8")` 解码 |
| 文件名里的 `<b>` / `<img>` 真的变成元素，带引号的名字把 `data-name` 截断 | 行 HTML 用字符串拼接，没有转义 | 所有拼进 HTML 的文本过 `esc()` |
| 错误提示一闪就没了，一次文字都没出现 | `filesToast` 写完立刻被 `panelsRender` 覆盖；且路径栏只在数据变化时才重写 | 提示做成状态，路径栏每帧校正 |
| 删掉目录后子文件成孤儿，重建同名目录它们又"复活" | `fs_remove` 只清目录项 | 删目录连带删子项 |
| 目录改名后子文件全留在旧路径下，看不见也删不掉 | `fs_rename` 只改目录项名字 | 改名时迁移子项前缀 |
| 同名文件与目录可以互相顶替 | `fs_write` / `fs_mkdir` 不检查 `is_dir` 冲突，`fs_mkdir` 遇到已存在直接返回 0 | 类型冲突明确报错 |
| `killProc(99)` 也返回成功 | `HCMD_KILL` 分支丢弃了 `proc_kill` 的返回值 | 按返回值回填 rc |
| `fsChdir` 能进入文件和根本不存在的路径 | 直接赋值，没有校验 | 不是目录就不进 |
| 超 4 KB 的内容保存时静默丢字节，却提示"已保存 N 字节" | 宿主按数据页上限 `Math.min` 截断，编辑器又用字符串长度报告字节数 | 超限直接 reject 并报出实际字节数 |
| 用户程序运行期间 Monitor 一直是"全部 READY"，不知道该终止哪一行 | 进程表只在 shell 主循环里发布，而那时 shell 正挂起 | 每次调度都刷进程表（`panel_sync_procs`） |
| 终止非当前进程的请求被取走后丢掉，还回了成功 | 定时器中断只处理「当前进程」，其余分支直接丢弃 | 两种 pid 都处理，并按结果回填 rc |

---


## 14. 外壳基础交互（14.1–14.8）

这一章是「桌面外壳本身」而不是「应用」的契约：滚动条、终端历史、配色、选区、缩放、切换器。判定方式都是实测数值或真实鼠标事件，不是数 DOM 节点。

| 编号 | 能力 | 预期行为 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 14.1 | 滚动条自绘 | 全站深色细滚动条，不再是浏览器默认浅色组件 | `.panel-dom` 的 `scrollbar-width/color` | `thin` + `rgba(255,255,255,…)` | ✓ |
| 14.2 | 终端历史回看 | `manifest` 这类长输出能滚到最顶上看到开头 | `.term-line.hist` 数量、`scrollTop`、首行文本 | 历史行 > 0 且总行数 > 22 且滚顶能看到 manifest 开头 | ✓ |
| 14.3 | 语义分层上色 | 提示符/命令/JSON/日志/错误各一色 | `.ta1`/`.ta2`/`.tc-json` 的 `color` | 四者颜色互不相同 | ✓ |
| 14.4 | 终端可选中 | 拖选能选中文字 | `getSelection().length` | > 0（允许重试两次，偶发失败不掩盖） | ✓ |
| 14.5 | Ctrl+C 语义 | 有选区时复制，无选区才中断 | `^C` 出现次数、选区长度 | 有选区时不增加 `^C` 且选区保留 | ✓ |
| 14.6 | 连续退格 | 退格能一直删，不是只删一个 | `rv.state.term` 里提示符行的内容 | 输入 8 个删 5 个剩 3 个，再删 5 个清空 | ✓ |
| 14.7 | 八向缩放 | 四角四边都能拖大，逻辑坐标不越界 | `rv.state.wins[0]` 的 `x/y/w/h` | 每次拖动后尺寸变化且 0 ≤ x ≤ 800、0 ≤ y ≤ 600 | ✓ |
| 14.8 | 字号随宽度变 | 窗口变窄字号变小 | `rv.ui.termFontSize` | 吸附到 `tl`（400×300）后字号小于默认 | ✓ |

### 14.x 补充契约

**窗口切换器**：`Alt+`` 打开/循环，松开 `Alt` 应用。`rv.focusWin(idx)` 是驱动接口。不用 `Alt+Tab` 是因为那是操作系统级快捷键，浏览器拿不到。

**终端快捷键**（Chrome 占用了 `Ctrl+Shift+A`/`Ctrl+Shift+V`，所以改键）：

| 键 | 行为 |
|---|---|
| `Ctrl+C` | 有选区复制，无选区发 0x03 中断 |
| `Ctrl+Shift+C` | 复制选区 |
| `Ctrl+Shift+V` | 粘贴（多行会确认） |
| `Ctrl+A` | 全选终端 |
| `Ctrl+F` | 终端查找 |
| `Alt+`` | 窗口切换器 |

**终端右键菜单**：复制 / 粘贴 / 全选 / 查找 / 清屏。

**动效阶梯**（取代原来 18 个散值）：

| 变量 | 值 | 用途 |
|---|---|---|
| `--dur-1` | 100ms | hover、按下、焦点环 |
| `--dur-2` | 167ms | 小元素进出、窗口吸附 |
| `--dur-3` | 250ms | 菜单、浮层、提示 |
| `--dur-4` | 333ms | 较大位移、抽屉 |
| `--dur-5` | 500ms | 页面级 |
| `--dur-boot` | 700ms | 启动动画（不参与交互阶梯） |

缓动三条取 Fluent：进入 `(0,0,0,1)`、退出 `(1,0,1,1)`、点对点 `(0.55,0.55,0,1)`。

**语义属性**（内核 → 宿主，由 `term_sync_t` 偏移 13 的 `attr_addr`
指向的字节数组给出；偏移 14 的 `scroll_attr_addr` 是同一套值的历史版本）：

| 值 | 含义 | 宿主类名 |
|---|---|---|
| 0 | 普通输出 | 无（走行首启发式认 JSON/日志） |
| 1 | 提示符 | `.ta1` |
| 2 | 用户输入回显 | `.ta2` |
| 5 | 错误 | `.ta5` |

---

## 15. 主题（15.1–15.3）

深色是默认；浅色只覆盖 token，不改任何组件规则。切换入口在「设置 → 外观」，
存 `localStorage["rvsim.theme"]`。

| 编号 | 能力 | 预期行为 | 测试接口 | 判定方式 | 状态 |
|---|---|---|---|---|---|
| 15.1 | 浅色切换生效 | `--s0` 变化、`data-theme="light"`、终端底色变浅 | 根元素 CSS 变量、`dataset.theme`、`.term-dom` 的 `backgroundColor` | 变量值变化且终端底色 RGB 均 > 200 | ✓ |
| 15.2 | 浅色对比度达标 | 正文 / 卡片正文 / 次要文字都 ≥4.5:1 | 读 `--t1`/`--t2`/`--s0`/`--s1` 用 WCAG 公式算 | 三项都 ≥4.5 | ✓ |
| 15.3 | 主题持久化 | 选择存进 localStorage，设置面板的选中态跟着走 | `localStorage["rvsim.theme"]`、`#setTheme button.on` | 存的是 `light`，选中按钮的 `data-t` 也是 `light` | ✓ |

### 主题契约

- 颜色一律走 token（`--s0`~`--s4`、`--t1`~`--t3`、`--acc`、`--stroke*`、`--hover`/`--active`、
  `--overlay*`、`--sh-*`、`--ok`/`--warn`/`--err`）。组件规则里**不许写死颜色**，
  否则换主题时那一处会漏掉。
- 新增 token 必须在 `[data-theme="light"]` 里有对应值，否则浅色下会用深色值。
- 宿主窗口（设置/帮助/关于/编辑器）不透明；只有 guest 窗口用半透明 + 模糊。

### 实测对比度

| 组合 | 深色 | 浅色 | 要求 |
|---|---|---|---|
| 正文 / 页面底 | 18.13 | 16.12 | ≥4.5 |
| 次要 / 页面底 | 10.94 | 6.71 | ≥4.5 |
| 三级 / 页面底 | 5.76 | 3.13 | ≥3 |
| 强调 / 页面底 | 9.74 | 3.40 | ≥3 |
| 终端正文 / 终端底 | 13.15 | 15.80 | ≥4.5 |

---

**测试文件**：`scripts/ui/e2e.js`，153 个用例，用例编号与本文档编号一一对应。
最近一次执行：**153/153 通过**；内核侧 riscv-tests 91/91、契约测试 18/18。

`spin.elf` 是专为「跑飞」这条路径加进种子集的：它死循环、每 20 万条指令打一个
字符、永不退出。没有它，「终止运行中的进程」只能靠读代码相信，测不出来。

其中 13.16–13.25 这 10 条是**对抗性验收**补进来的：不是按设计路径走，
而是拿空名、超长名、中文名、注入名、类型冲突、不存在的 pid 去撞，
撞出来的每一个问题都修完才写进清单。

帧率 144 就是这块显示器 144 Hz 的刷新上限：关掉 monitor/files、把壁纸换成纯色，
数字都不变，说明帧循环没有卡在 CPU 上，测到的是 vsync 而不是算力。
早先记录的 178 是计数口径不同得出的，已按实测改写。

**回归基线之外的纪律**：任何一条用例只要「只敲字不回车」，
就会把 shell 的行缓冲留脏，后面所有命令都被拼在残留内容后面变成垃圾。
所以发命令统一走 `shellCmd(p, cmd)`（先 Ctrl+C 再发），
这也是真实用户遇到脏行时的做法。

## 统计

| 状态 | 数量 |
|---|---|
| ✓ 已实现且测过 | 125 |
| ? 待验证 | 0 |
| ✗ 未实现 | 0 |

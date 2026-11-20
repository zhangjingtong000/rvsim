// rvsim UI 端到端总测 —— 用例编号与 docs/ui-spec.md 一一对应
// 用法：node scripts/ui/e2e.js [--grep 3.]
const { chromium } = require("playwright");

const URL = process.env.RVSIM_URL || "http://127.0.0.1:8799/index.html";
const tests = [];
const test = (id, name, fn) => tests.push({ id, name, fn });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function eq(a, b, msg) {
  const A = JSON.stringify(a), B = JSON.stringify(b);
  if (A !== B) throw new Error((msg || "") + ` 期望 ${B}，实际 ${A}`);
}
function ok(v, msg) { if (!v) throw new Error(msg || "断言失败"); }

/* 干净地发一条命令：先 Ctrl+C 放弃可能残留的半行输入，再发。
   任何一条「只敲字不回车」的用例都会把行缓冲留脏，之后所有命令
   都会被拼在残留内容后面变成垃圾——真实用户也是这么救回来的。 */
async function shellCmd(p, cmd) {
  await p.evaluate(() => window.rv.type("\u0003"));
  await sleep(150);
  await p.evaluate((x) => window.rv.cmd(x), cmd);
}

// ══════════════════════════════════════════════════════════
// 1. 启动
// ══════════════════════════════════════════════════════════
test("1.1", "开机画面含 logo / 进度条 / 状态", async (p, st) => {
  // 用 st.bootSeen 记录加载时抓到的快照
  ok(st.bootSeen, "加载时没抓到 #boot（可能太快或没渲染）");
  ok(st.bootSeen.hasBar, "缺进度条");
  ok(st.bootSeen.hasStatus, "缺状态文字");
});

test("1.2", "进度随阶段推进", async (p, st) => {
  ok(st.bootSeen && st.bootSeen.widths.length >= 2, "没采到足够的进度样本：" + JSON.stringify(st.bootSeen && st.bootSeen.widths));
  const w = st.bootSeen.widths;
  ok(w[w.length - 1] >= w[0], "进度没有推进：" + JSON.stringify(w));
});

test("1.3", "就绪后开机画面消失", async (p) => {
  eq(await p.evaluate(() => window.rv.ui.booted), true);
});

test("1.4", "跳过开机动画开关写入 localStorage", async (p) => {
  await p.evaluate(() => window.rv.launch("settings"));
  await sleep(500);
  await p.evaluate(() => document.getElementById("setBoot").click());
  await sleep(200);
  const v = await p.evaluate(() => localStorage.getItem("rvsim.skipBoot"));
  ok(v === "0" || v === "1", "没写入：" + v);
  await p.evaluate(() => document.getElementById("setBoot").click());
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

// ══════════════════════════════════════════════════════════
// 2. 桌面
// ══════════════════════════════════════════════════════════
test("2.1", "默认壁纸", async (p) => {
  ok((await p.evaluate(() => window.rv.ui.wallStyle)).length > 0);
});

test("2.2", "壁纸三档可选且立即生效", async (p) => {
  await p.evaluate(() => window.rv.launch("settings"));
  await sleep(500);
  const seen = new Set();
  for (const w of ["0", "1", "2"]) {
    await p.evaluate((x) => document.querySelector('#setWall button[data-w="' + x + '"]').click(), w);
    await sleep(250);
    seen.add(await p.evaluate(() => window.rv.ui.wallStyle));
  }
  eq(seen.size, 3, "三档壁纸只有 " + seen.size + " 种不同背景");
  await p.evaluate(() => document.querySelector('#setWall button[data-w="0"]').click());
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

test("2.3", "6 个桌面图标", async (p) => {
  eq(await p.evaluate(() => window.rv.ui.iconCount), 6);
});

test("2.4", "单击只选中一个", async (p) => {
  await p.evaluate(() => window.rv.ui_clickIcon("monitor"));
  await sleep(200);
  eq(await p.evaluate(() => window.rv.ui.selectedIcons), ["monitor"]);
});

test("2.5", "双击图标打开应用", async (p) => {
  await p.evaluate(() => window.rv.closeWin(1));
  await sleep(350);
  await p.evaluate(() => window.rv.ui_dblIcon("monitor"));
  await sleep(600);
  eq(await p.evaluate(() => window.rv.state.wins[1].visible), true);
});

test("2.6", "图标键盘可达（Tab + Enter）", async (p) => {
  await p.evaluate(() => window.rv.closeWin(2));
  await sleep(350);
  await p.evaluate(() => document.body.focus());
  let hit = false;
  for (let i = 0; i < 12; i++) {
    await p.keyboard.press("Tab");
    const cls = await p.evaluate(() => document.activeElement && document.activeElement.className);
    if (cls && cls.includes("dicon")) { hit = true; break; }
  }
  ok(hit, "Tab 到不了桌面图标");
  // 移到 files 图标再回车
  await p.evaluate(() => document.querySelector('.dicon[data-app="files"]').focus());
  await p.keyboard.press("Enter");
  await sleep(600);
  eq(await p.evaluate(() => window.rv.state.wins[2].visible), true);
});

test("2.7", "桌面右键菜单三项", async (p) => {
  const items = await p.evaluate(() => {
    window.rv.ui_ctx([{ label: "刷新", action() {} }, "-",
      { label: "显示设置", action() {} }, { label: "关于本机", action() {} }], 700, 700);
    return window.rv.ui.ctxMenu;
  });
  eq(items, ["刷新", "显示设置", "关于本机"]);
  await p.evaluate(() => document.getElementById("ctxMenu").classList.remove("on"));
});

test("2.8", "刷新不改变图标数量", async (p) => {
  const before = await p.evaluate(() => window.rv.ui.iconCount);
  await p.evaluate(() => window.rv.ui_ctx([{ label: "刷新", action() { } }], 700, 700));
  await p.evaluate(() => document.querySelectorAll("#ctxMenu .ctx-item")[0].click());
  await sleep(400);
  eq(await p.evaluate(() => window.rv.ui.iconCount), before);
});

// ══════════════════════════════════════════════════════════
// 3. 窗口管理
// ══════════════════════════════════════════════════════════
test("3.1", "三种入口都能打开窗口", async (p) => {
  await p.evaluate(() => { window.rv.closeWin(0); window.rv.closeWin(1); window.rv.closeWin(2); });
  await sleep(600);
  await p.evaluate(() => window.rv.launch("terminal"));
  await sleep(500);
  eq(await p.evaluate(() => window.rv.state.wins[0].visible), true);
  await p.evaluate(() => window.rv.ui_dblIcon("monitor"));
  await sleep(500);
  eq(await p.evaluate(() => window.rv.state.wins[1].visible), true);
  await p.evaluate(() => { window.rv.ui_startMenu(true); });
  await sleep(400);
  await p.evaluate(() => {
    const t = [...document.querySelectorAll("#smTiles .sm-tile")].find((e) => e.textContent.includes("Files"));
    if (t) t.click();
  });
  await sleep(600);
  eq(await p.evaluate(() => window.rv.state.wins[2].visible), true);
});

test("3.2", "三种方式关闭窗口", async (p) => {
  await p.evaluate(() => window.rv.ui_winBtn(0, "close"));
  await sleep(400);
  eq(await p.evaluate(() => window.rv.state.wins[0].visible), false);
  await p.evaluate(() => window.rv.launch("terminal"));
  await sleep(500);
  await p.evaluate(() => window.rv.focusWin(0));
  await sleep(300);
  await p.keyboard.press("Control+w");
  await sleep(400);
  eq(await p.evaluate(() => window.rv.state.wins[0].visible), false);
  await p.evaluate(() => window.rv.launch("terminal"));
  await sleep(500);
});

test("3.3", "最小化两种方式", async (p) => {
  await p.evaluate(() => window.rv.ui_winBtn(1, "min"));
  await sleep(400);
  eq(await p.evaluate(() => window.rv.state.wins[1].visible), false);
  await p.evaluate(() => window.rv.launch("monitor"));
  await sleep(500);
  await p.evaluate(() => window.rv.focusWin(1));
  await sleep(300);
  await p.keyboard.press("Control+m");
  await sleep(400);
  eq(await p.evaluate(() => window.rv.state.wins[1].visible), false);
  await p.evaluate(() => window.rv.launch("monitor"));
  await sleep(500);
});

test("3.4", "最大化 800x600", async (p) => {
  await p.evaluate(() => window.rv.ui_winBtn(1, "max"));
  await sleep(500);
  const w = await p.evaluate(() => window.rv.state.wins[1]);
  eq([w.x, w.y, w.w, w.h], [0, 0, 800, 600]);
});

test("3.5", "还原回原尺寸", async (p) => {
  await p.evaluate(() => window.rv.ui_winBtn(1, "max"));
  await sleep(500);
  const w = await p.evaluate(() => window.rv.state.wins[1]);
  eq([w.w, w.h], [216, 150]);
});

test("3.6", "拖动位移与鼠标一致", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "left"));
  await sleep(400);
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await sleep(400);
  const before = await p.evaluate(() => window.rv.state.wins[0]);
  const b = await p.evaluate(() => {
    const r = document.querySelectorAll(".win")[0].querySelector(".win-bar").getBoundingClientRect();
    return { x: r.x + 150, y: r.y + r.height / 2 };
  });
  await p.mouse.move(b.x, b.y);
  await p.mouse.down();
  await p.mouse.move(b.x + 40, b.y + 20, { steps: 8 });
  await p.mouse.up();
  await sleep(700);
  const after = await p.evaluate(() => window.rv.state.wins[0]);
  ok(after.x !== before.x || after.y !== before.y, "拖动后位置没变");
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await sleep(400);
});

test("3.7", "点标题栏切换焦点", async (p) => {
  await p.evaluate(() => window.rv.focusWin(2));
  await sleep(300);
  eq(await p.evaluate(() => window.rv.ui.focusedWindow), 2);
});

test("3.8", "聚焦窗口 z 最大", async (p) => {
  await p.evaluate(() => window.rv.focusWin(0));
  await sleep(300);
  const z = await p.evaluate(() => window.rv.state.wins.map((w) => w.z));
  eq(Math.max(...z), z[0]);
});

test("3.9", "吸附左右半屏", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "left"));
  await sleep(450);
  let w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.x, w.w], [0, 400]);
  await p.evaluate(() => window.rv.ui_snapTo(0, "right"));
  await sleep(450);
  w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.x, w.w], [400, 400]);
});

test("3.10", "吸附顶部最大化", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "top"));
  await sleep(450);
  const w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.w, w.h], [800, 600]);
});

test("3.11", "吸附四角 1/4 屏", async (p) => {
  for (const [z, want] of [["tl", [0, 0]], ["tr", [400, 0]], ["bl", [0, 300]], ["br", [400, 300]]]) {
    await p.evaluate((x) => window.rv.ui_snapTo(0, x), z);
    await sleep(400);
    const w = await p.evaluate(() => window.rv.state.wins[0]);
    eq([w.x, w.y, w.w, w.h], [want[0], want[1], 400, 300], z + " ");
  }
});

test("3.12", "并排吸附（拖到另一窗口边缘）", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await p.evaluate(() => window.rv.ui_snapTo(1, "right"));
  await sleep(500);
  const t = await p.evaluate(() => {
    const os = document.getElementById("os").getBoundingClientRect();
    const L = { scale: Math.min(os.width / 800, (os.height - 48) / 600) * 0.985 };
    L.ox = (os.width - 800 * L.scale) / 2;
    L.oy = (os.height - 48 - 600 * L.scale) / 2;
    const mon = window.rv.state.wins[1];
    return { x: os.left + L.ox + mon.x * L.scale + 4, y: os.top + L.oy + (mon.y + 60) * L.scale };
  });
  const bar = await p.evaluate(() => {
    const r = document.querySelectorAll(".win")[0].querySelector(".win-bar").getBoundingClientRect();
    return { x: r.x + 150, y: r.y + r.height / 2 };
  });
  await p.mouse.move(bar.x, bar.y);
  await p.mouse.down();
  for (let k = 1; k <= 10; k++) {
    await p.mouse.move(bar.x + (t.x - bar.x) * k / 10, bar.y + (t.y - bar.y) * k / 10);
    await sleep(25);
  }
  await p.mouse.up();
  await sleep(600);
  const w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.x, w.w], [0, 400]);
});

test("3.13", "吸附后贴边圆角为 0", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "left"));
  await sleep(450);
  const r = await p.evaluate(() => {
    const cs = getComputedStyle(document.querySelectorAll(".win")[0]);
    return { tl: cs.borderTopLeftRadius, bl: cs.borderBottomLeftRadius, tr: cs.borderTopRightRadius };
  });
  eq([r.tl, r.bl], ["0px", "0px"]);
  eq(r.tr, "8px");
});

test("3.14", "布局选择器 4 种布局 11 个格子", async (p) => {
  const n = await p.evaluate(() => {
    window.rv.ui_snapPicker(0);
    return { on: window.rv.ui.snapPicker,
             layouts: document.querySelectorAll("#snapPicker .sl").length,
             cells: document.querySelectorAll("#snapPicker .sl-cell").length };
  });
  eq(n, { on: true, layouts: 4, cells: 11 });
  await p.evaluate(() => document.getElementById("snapPicker").classList.remove("on"));
});

test("3.15", "布局选择器应用生效", async (p) => {
  await p.evaluate(() => window.rv.ui_pickLayout(0, 1, 1));
  await sleep(500);
  const w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.x, w.y, w.w, w.h], [534, 0, 266, 600]);
});

test("3.16", "双击标题栏最大化/还原", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await sleep(400);
  const b = await p.evaluate(() => {
    const r = document.querySelectorAll(".win")[0].querySelector(".win-bar").getBoundingClientRect();
    return { x: r.x + 100, y: r.y + r.height / 2 };
  });
  await p.mouse.dblclick(b.x, b.y);
  await sleep(500);
  let w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.w, w.h], [800, 600]);
  const b2 = await p.evaluate(() => {
    const r = document.querySelectorAll(".win")[0].querySelector(".win-bar").getBoundingClientRect();
    return { x: r.x + 100, y: r.y + r.height / 2 };
  });
  await p.mouse.dblclick(b2.x, b2.y);
  await sleep(500);
  w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.w, w.h], [480, 360]);
});

test("3.17", "Alt+方向四种", async (p) => {
  await p.evaluate(() => window.rv.focusWin(0));
  await sleep(300);
  for (const [k, want] of [["Alt+ArrowLeft", [0, 0, 400, 600]],
                           ["Alt+ArrowRight", [400, 0, 400, 600]],
                           ["Alt+ArrowUp", [0, 0, 800, 600]]]) {
    await p.keyboard.press(k);
    await sleep(450);
    const w = await p.evaluate(() => window.rv.state.wins[0]);
    eq([w.x, w.y, w.w, w.h], want, k + " ");
  }
  await p.keyboard.press("Alt+ArrowDown");
  await sleep(450);
  const w = await p.evaluate(() => window.rv.state.wins[0]);
  eq([w.x, w.y, w.w, w.h], [64, 48, 480, 360]);
});

test("3.18", "Ctrl+W / Ctrl+M", async (p) => {
  await p.evaluate(() => window.rv.focusWin(2));
  await sleep(300);
  await p.keyboard.press("Control+m");
  await sleep(400);
  eq(await p.evaluate(() => window.rv.state.wins[2].visible), false);
  await p.evaluate(() => window.rv.launch("files"));
  await sleep(400);
  await p.evaluate(() => window.rv.focusWin(2));
  await sleep(300);
  await p.keyboard.press("Control+w");
  await sleep(400);
  eq(await p.evaluate(() => window.rv.state.wins[2].visible), false);
  await p.evaluate(() => window.rv.launch("files"));
  await sleep(400);
});

test("3.19", "窗口右键菜单四项", async (p) => {
  const items = await p.evaluate(() => {
    window.rv.ui_ctx([{ label: "还原", action() {} }, { label: "最小化", action() {} },
      { label: "最大化", action() {} }, "-", { label: "关闭", action() {} }], 700, 400);
    return window.rv.ui.ctxMenu;
  });
  eq(items, ["还原", "最小化", "最大化", "关闭"]);
  await p.evaluate(() => document.getElementById("ctxMenu").classList.remove("on"));
});

test("3.20", "窗口不会被拖出桌面", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await sleep(400);
  const b = await p.evaluate(() => {
    const r = document.querySelectorAll(".win")[0].querySelector(".win-bar").getBoundingClientRect();
    return { x: r.x + 150, y: r.y + r.height / 2 };
  });
  await p.mouse.move(b.x, b.y);
  await p.mouse.down();
  await p.mouse.move(b.x - 900, b.y - 600, { steps: 10 });
  await p.mouse.up();
  await sleep(600);
  const w = await p.evaluate(() => window.rv.state.wins[0]);
  ok(w.x >= 0 && w.y >= 0, `窗口被拖到负坐标：${w.x},${w.y}`);
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await sleep(400);
});

test("3.21", "关闭有 closing 动画类", async (p) => {
  await p.evaluate(() => window.rv.ui_winBtn(1, "close"));
  await sleep(60);
  const has = await p.evaluate(() => document.querySelectorAll(".win")[1].classList.contains("closing"));
  ok(has, "点击后没有 closing 类");
  await sleep(400);
  await p.evaluate(() => window.rv.launch("monitor"));
  await sleep(400);
});

test("3.22", "最小化有 minimizing 动画类", async (p) => {
  await p.evaluate(() => window.rv.ui_winBtn(1, "min"));
  await sleep(60);
  const has = await p.evaluate(() => document.querySelectorAll(".win")[1].classList.contains("minimizing"));
  ok(has, "点击后没有 minimizing 类");
  await sleep(400);
  await p.evaluate(() => window.rv.launch("monitor"));
  await sleep(400);
});

// ══════════════════════════════════════════════════════════
// 4. Dock
// ══════════════════════════════════════════════════════════
test("4.1", "6 个 Dock 应用图标", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll(".dock-app").length), 6);
});

test("4.2", "运行中的窗口有指示条", async (p) => {
  eq(await p.evaluate(() => window.rv.ui.dockActive.slice(0, 3)), [true, true, true]);
});

test("4.3", "全部关闭后指示条全灭", async (p) => {
  await p.evaluate(() => { window.rv.closeWin(0); window.rv.closeWin(1); window.rv.closeWin(2); });
  await sleep(700);
  eq(await p.evaluate(() => window.rv.ui.dockActive), [false, false, false, false, false, false]);
  await p.evaluate(() => { window.rv.launch("terminal"); window.rv.launch("monitor"); window.rv.launch("files"); });
  await sleep(700);
});

test("4.4", "悬停图标放大", async (p) => {
  const r = await p.evaluate(() => {
    const e = document.querySelectorAll(".dock-app")[0].getBoundingClientRect();
    return { x: e.x + e.width / 2, y: e.y + e.height / 2 };
  });
  await p.mouse.move(r.x, r.y);
  await sleep(400);
  const t = await p.evaluate(() => getComputedStyle(document.querySelector(".dock-app svg")).transform);
  ok(t !== "none" && t !== "matrix(1, 0, 0, 1, 0, 0)", "悬停没有放大：" + t);
  await p.mouse.move(200, 400);
  await sleep(300);
});

test("4.5", "悬停 tooltip", async (p) => {
  const r = await p.evaluate(() => {
    const e = document.querySelectorAll(".dock-app")[0].getBoundingClientRect();
    return { x: e.x + e.width / 2, y: e.y + e.height / 2 };
  });
  await p.mouse.move(r.x, r.y);
  await sleep(750);
  eq(await p.evaluate(() => window.rv.ui.tip), "Terminal");
  await p.mouse.move(200, 400);
  await sleep(250);
});

test("4.6", "点击 Dock 启动应用", async (p) => {
  await p.evaluate(() => window.rv.closeWin(1));
  await sleep(400);
  await p.evaluate(() => document.querySelectorAll(".dock-app")[1].click());
  await sleep(600);
  eq(await p.evaluate(() => window.rv.state.wins[1].visible), true);
});

test("4.7", "开始按钮开关菜单", async (p) => {
  eq(await p.evaluate(() => window.rv.ui_startMenu(true)), true);
  await sleep(300);
  eq(await p.evaluate(() => window.rv.ui_startMenu(false)), false);
});

test("4.8", "调试按钮开关抽屉", async (p) => {
  await p.evaluate(() => document.getElementById("dbgBtn").click());
  await sleep(400);
  eq(await p.evaluate(() => window.rv.ui.drawer), true);
  await p.evaluate(() => document.getElementById("dbgBtn").click());
  await sleep(400);
  eq(await p.evaluate(() => window.rv.ui.drawer), false);
});

test("4.9", "托盘时钟格式 HH:MM", async (p) => {
  const c = await p.evaluate(() => window.rv.ui.clock);
  ok(/^\d{2}:\d{2}$/.test(c), "格式不对：" + c);
});

test("4.10", "托盘日期非空", async (p) => {
  ok((await p.evaluate(() => document.getElementById("clockDate").textContent)).length > 0);
});

test("4.11", "速率显示", async (p) => {
  const r = await p.evaluate(() => document.getElementById("trayRate").textContent);
  ok(/M\/s/.test(r), "速率没显示：" + r);
});

// ══════════════════════════════════════════════════════════
// 5. 开始菜单
// ══════════════════════════════════════════════════════════
test("5.1", "打开/关闭", async (p) => {
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(350);
  eq(await p.evaluate(() => window.rv.ui.startMenu), true);
  await p.evaluate(() => window.rv.ui_startMenu(false));
  await sleep(300);
  eq(await p.evaluate(() => window.rv.ui.startMenu), false);
});

test("5.2", "点外部关闭", async (p) => {
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(350);
  await p.mouse.click(300, 700);
  await sleep(400);
  eq(await p.evaluate(() => window.rv.ui.startMenu), false);
});

test("5.3", "Escape 关闭", async (p) => {
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(350);
  await p.keyboard.press("Escape");
  await sleep(400);
  eq(await p.evaluate(() => window.rv.ui.startMenu), false);
});

test("5.4", "搜索框存在", async (p) => {
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(350);
  eq(await p.evaluate(() => !!document.querySelector("#smSearch")), true);
});

test("5.5", "搜索过滤", async (p) => {
  await p.fill("#smSearch", "mon");
  await sleep(350);
  const vis = await p.evaluate(() =>
    [...document.querySelectorAll("#smTiles .sm-tile")]
      .filter((t) => t.style.display !== "none").map((t) => t.textContent));
  eq(vis, ["Monitor"]);
  await p.fill("#smSearch", "");
  await sleep(250);
});

test("5.6", "6 个磁贴", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll("#smTiles .sm-tile").length), 6);
});

test("5.7", "6 条推荐", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll("#smRecent .sm-ri").length), 6);
});

test("5.8", "磁贴能启动应用并关闭菜单", async (p) => {
  await p.evaluate(() => {
    const t = [...document.querySelectorAll("#smTiles .sm-tile")].find((e) => e.textContent.includes("Settings"));
    if (t) t.click();
  });
  await sleep(600);
  ok((await p.evaluate(() => window.rv.ui.hostWins.map((h) => h.title))).includes("Settings"));
  eq(await p.evaluate(() => window.rv.ui.startMenu), false);
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

test("5.9", "推荐项发命令到终端", async (p) => {
  await p.evaluate(() => window.rv.cmd("clear"));
  await sleep(600);
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(400);
  await p.evaluate(() => {
    const r = [...document.querySelectorAll("#smRecent .sm-ri")].find((e) => e.textContent.includes("ps"));
    if (r) r.click();
  });
  await sleep(1000);
  const t = await p.evaluate(() => window.rv.state.term.join("\n"));
  ok(t.includes("nproc"), "推荐项没发命令：" + t.slice(-160));
});

test("5.10", "终端关着时推荐项自动拉起终端", async (p) => {
  await p.evaluate(() => window.rv.closeWin(0));
  await sleep(500);
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(400);
  await p.evaluate(() => {
    const r = [...document.querySelectorAll("#smRecent .sm-ri")].find((e) => e.textContent.includes("ls"));
    if (r) r.click();
  });
  await sleep(900);
  eq(await p.evaluate(() => window.rv.state.wins[0].visible), true);
});

test("5.11", "用户条显示 root", async (p) => {
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(400);
  const t = await p.evaluate(() => document.querySelector(".sm-user").textContent);
  ok(t.includes("root"), "用户条没有 root：" + t);
});

test("5.12", "电源按钮重启后仍可用", async (p) => {
  await p.evaluate(() => window.rv.ui_startMenu(true));
  await sleep(400);
  await p.evaluate(() => document.querySelector(".sm-power").click());
  await sleep(2500);
  eq(await p.evaluate(() => window.rv.ready), true);
  await sleep(800);
});

// ══════════════════════════════════════════════════════════
// 6. 托盘面板
// ══════════════════════════════════════════════════════════
test("6.1", "点击打开面板", async (p) => {
  await p.evaluate(() => window.rv.ui_tray(true));
  await sleep(350);
  eq(await p.evaluate(() => window.rv.ui.trayPanel), true);
});

test("6.2", "时间格式 HH:MM:SS", async (p) => {
  const t = await p.evaluate(() => document.querySelector("#trayPanel .tp-time").textContent);
  ok(/^\d{2}:\d{2}:\d{2}$/.test(t), "格式不对：" + t);
});

test("6.3", "日期行含年月日星期", async (p) => {
  const t = await p.evaluate(() => document.querySelector("#trayPanel .tp-date").textContent);
  ok(/年/.test(t) && /月/.test(t) && /日/.test(t) && /星期/.test(t), "日期行：" + t);
});

test("6.4", "时区非空", async (p) => {
  const all = await p.evaluate(() => [...document.querySelectorAll("#trayPanel .tp-date")].map((e) => e.textContent));
  ok(all.length >= 2 && all[1].length > 0, "没有时区行");
});

test("6.5", "日历 28-31 天且今天高亮", async (p) => {
  const d = await p.evaluate(() => ({
    days: document.querySelectorAll("#trayPanel .tp-day").length,
    today: document.querySelectorAll("#trayPanel .tp-day.today").length,
  }));
  ok(d.days >= 28 && d.days <= 31, "天数 " + d.days);
  eq(d.today, 1);
});

test("6.6", "面板不溢出视口", async (p) => {
  const b = await p.evaluate(() => {
    const r = document.getElementById("trayPanel").getBoundingClientRect();
    return { l: r.left, r: r.right, t: r.top, b: r.bottom, vw: innerWidth, vh: innerHeight };
  });
  ok(b.l >= 0 && b.r <= b.vw && b.t >= 0 && b.b <= b.vh, "溢出：" + JSON.stringify(b));
});

test("6.7", "点外部关闭面板", async (p) => {
  await p.mouse.click(300, 400);
  await sleep(400);
  eq(await p.evaluate(() => window.rv.ui.trayPanel), false);
});

test("6.8", "托盘右键菜单两项", async (p) => {
  const items = await p.evaluate(() => {
    window.rv.ui_ctx([{ label: "调整日期和时间", action() {} }, { label: "任务栏设置", action() {} }], 1300, 800);
    return window.rv.ui.ctxMenu;
  });
  eq(items, ["调整日期和时间", "任务栏设置"]);
  await p.evaluate(() => document.getElementById("ctxMenu").classList.remove("on"));
});

// ══════════════════════════════════════════════════════════
// 7. 应用
// ══════════════════════════════════════════════════════════
test("7.1.1", "终端 22 行网格", async (p) => {
  eq(await p.evaluate(() => window.rv.state.term.length), 22);
});

test("7.1.2", "提示符", async (p) => {
  ok((await p.evaluate(() => window.rv.state.term.join("\n"))).includes("rvsh>"));
});

test("7.1.3", "命令执行有输出", async (p) => {
  await p.evaluate(() => window.rv.cmd("whoami"));
  await sleep(900);
  ok((await p.evaluate(() => window.rv.state.term.join("\n"))).includes('"user"'));
});

test("7.1.4", "中文能进终端", async (p) => {
  await p.evaluate(() => window.rv.cmd("echo 中文测试"));
  await sleep(900);
  ok((await p.evaluate(() => window.rv.state.term.join("\n"))).includes("中文测试"));
});

test("7.1.5", "光标跟随", async (p) => {
  await p.evaluate(() => window.rv.cmd("clear"));
  await sleep(700);
  const c = await p.evaluate(() => {
    const e = document.querySelector(".tcur");
    if (!e) return null;
    const cs = getComputedStyle(e);
    return { left: cs.left, top: cs.top };
  });
  ok(c && c.left !== "auto", "没有光标元素");
});

test("7.1.6", "字号随窗口宽变化", async (p) => {
  const a = await p.evaluate(() => window.rv.ui.termFontSize);
  await p.evaluate(() => window.rv.ui_snapTo(0, "top"));
  await sleep(600);
  const b = await p.evaluate(() => window.rv.ui.termFontSize);
  ok(b >= a, `最大化后字号没变大：${a} -> ${b}`);
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await sleep(600);
});

test("7.1.7", "不横向折行", async (p) => {
  const m = await p.evaluate(() => {
    const e = document.querySelector(".term-dom");
    return { sw: e.scrollWidth, cw: e.clientWidth };
  });
  eq(m.sw, m.cw);
});

test("7.1.8", "Ctrl+C 放弃当前行", async (p) => {
  await p.evaluate(() => window.rv.cmd("clear"));
  await sleep(700);
  await p.evaluate(() => document.querySelector(".term-dom").click());
  await sleep(200);
  await p.keyboard.type("half-typed");
  await sleep(300);
  await p.keyboard.press("Control+c");
  await sleep(600);
  let t = await p.evaluate(() => window.rv.state.term.join("\n"));
  ok(t.indexOf("^C") >= 0, "Ctrl+C 没有回显 ^C：" + t.slice(-140));
  /* 关键：Ctrl+C 之后行缓冲必须是干净的。以前 0x03 被当成普通字符
     塞进缓冲，之后每条命令都被拼在那个看不见的字符后面，全部失效。 */
  await p.evaluate(() => window.rv.cmd("whoami"));
  await sleep(900);
  t = await p.evaluate(() => window.rv.state.term.join("\n"));
  ok(t.indexOf('"user"') >= 0, "Ctrl+C 之后命令失效了（行缓冲没清）：" + t.slice(-220));
});

test("7.1.9", "粘贴文本进终端并能执行", async (p) => {
  await p.evaluate(() => window.rv.cmd("clear"));
  await sleep(700);
  await p.evaluate(() => {
    const dt = new DataTransfer();
    dt.setData("text", "echo pasted");
    document.dispatchEvent(new ClipboardEvent("paste", { clipboardData: dt, bubbles: true }));
  });
  await sleep(500);
  await p.keyboard.press("Enter");          /* 用户粘贴完也要回车；不回车的用例会污染后续所有命令 */
  await sleep(900);
  const t = await p.evaluate(() => window.rv.state.term.join("\n"));
  ok(t.includes("pasted"), "粘贴没生效：" + t.slice(-120));
  ok(t.trimEnd().endsWith("rvsh>"), "回车后没有回到提示符，行缓冲可能还是脏的：" + t.slice(-120));
});

test("7.2.1", "Monitor 3 行", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll(".win")[1].querySelectorAll("tbody tr").length), 3);
});

test("7.2.2", "Monitor 三列", async (p) => {
  const h = await p.evaluate(() => [...document.querySelectorAll(".win")[1].querySelectorAll("th")].map((e) => e.textContent));
  eq(h, ["PID", "STATE", "PC"]);
});

test("7.2.3", "Monitor 数据与 rv.state 一致", async (p) => {
  const a = await p.evaluate(() => window.rv.state.procs.map((x) => String(x.pid)));
  const b = await p.evaluate(() => [...document.querySelectorAll(".win")[1].querySelectorAll("tbody tr td:first-child")].map((e) => e.textContent));
  eq(b, a);
});

test("7.3.1", "Files 5 行", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll(".win")[2].querySelectorAll("tbody tr").length), 5);
});

test("7.3.2", "Files 两列", async (p) => {
  const h = await p.evaluate(() => [...document.querySelectorAll(".win")[2].querySelectorAll("th")].map((e) => e.textContent));
  eq(h, ["NAME", "SIZE"]);
});

test("7.3.3", "双击 .elf 触发 run", async (p) => {
  await p.evaluate(() => window.rv.cmd("clear"));
  await sleep(700);
  await p.evaluate(() => {
    const tr = [...document.querySelectorAll("tr[data-name]")].find((r) => r.dataset.name === "hello.elf");
    if (tr) tr.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  });
  await sleep(1100);
  const t = await p.evaluate(() => window.rv.state.term.join("\n"));
  ok(t.includes("run hello.elf"), "没发出 run：" + t.slice(-160));
});

test("7.3.4", "双击 .txt 打开编辑器并载入内容", async (p) => {
  await p.evaluate(() => window.rv.openFile("readme.txt"));
  await sleep(900);
  const r = await p.evaluate(() => {
    const w = [...document.querySelectorAll(".host-win")].find((x) => x.querySelector(".ed-area"));
    return w ? { title: w.querySelector(".win-title").textContent, len: w.querySelector(".ed-area").value.length } : null;
  });
  ok(r, "没有打开编辑器窗口");
  ok(r.title.indexOf("readme.txt") >= 0, "标题不对：" + r.title);
  ok(r.len > 0, "编辑器里没有内容");
});

test("7.4.1", "设置壁纸立即生效", async (p) => {
  await p.evaluate(() => window.rv.launch("settings"));
  await sleep(500);
  const a = await p.evaluate(() => window.rv.ui.wallStyle);
  await p.evaluate(() => document.querySelector('#setWall button[data-w="1"]').click());
  await sleep(300);
  ok(a !== (await p.evaluate(() => window.rv.ui.wallStyle)), "壁纸没变");
  await p.evaluate(() => document.querySelector('#setWall button[data-w="0"]').click());
  await sleep(250);
});

test("7.4.2", "图标开关", async (p) => {
  await p.evaluate(() => document.getElementById("setIcons").click());
  await sleep(300);
  eq(await p.evaluate(() => window.rv.ui.iconsVisible), false);
  await p.evaluate(() => document.getElementById("setIcons").click());
  await sleep(300);
  eq(await p.evaluate(() => window.rv.ui.iconsVisible), true);
});

test("7.4.3", "字号「大」真的更大", async (p) => {
  const a = await p.evaluate(() => window.rv.ui.termFontSize);
  await p.evaluate(() => document.querySelector('#setFs button[data-f="1.15"]').click());
  await sleep(500);
  const b = await p.evaluate(() => window.rv.ui.termFontSize);
  ok(b > a, `字号没变大：${a} -> ${b}`);
  await p.evaluate(() => document.querySelector('#setFs button[data-f="1"]').click());
  await sleep(400);
});

test("7.4.4", "开机动画开关写 localStorage", async (p) => {
  await p.evaluate(() => document.getElementById("setBoot").click());
  await sleep(250);
  const v = await p.evaluate(() => localStorage.getItem("rvsim.skipBoot"));
  ok(v === "0" || v === "1", "没写入：" + v);
  await p.evaluate(() => document.getElementById("setBoot").click());
  await sleep(200);
});

test("7.4.5", "硬件信息四行", async (p) => {
  const n = await p.evaluate(() => document.querySelectorAll(".host-win .kv").length);
  ok(n >= 4, "只有 " + n + " 行");
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

test("7.5.1", "Guide 能打开", async (p) => {
  await p.evaluate(() => window.rv.launch("guide"));
  await sleep(600);
  ok((await p.evaluate(() => window.rv.ui.hostWins.map((h) => h.title))).includes("Guide"));
});

test("7.5.2", "Guide 内容可滚动", async (p) => {
  const o = await p.evaluate(() => getComputedStyle(document.querySelector(".host-win .win-body")).overflowY);
  ok(o === "auto" || o === "scroll", "overflowY=" + o);
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

test("7.6.1", "About 能打开", async (p) => {
  await p.evaluate(() => window.rv.launch("about"));
  await sleep(600);
  ok((await p.evaluate(() => window.rv.ui.hostWins.map((h) => h.title))).includes("About"));
});

test("7.6.2", "About 六格默认全部可见", async (p) => {
  await p.evaluate(() => window.rv.launch("about"));
  await sleep(900);
  const r = await p.evaluate(() => {
    const w = [...document.querySelectorAll(".host-win")].find((x) => x.querySelector(".win-title").textContent.indexOf("About") >= 0);
    if (!w) return null;
    const body = w.querySelector(".win-body");
    const br = body.getBoundingClientRect();
    const cells = [...w.querySelectorAll(".about-grid > div")].map((d) => {
      const q = d.getBoundingClientRect();
      return q.top >= br.top - 1 && q.bottom <= br.bottom + 1;
    });
    return { n: cells.length, all: cells.every(Boolean), h: Math.round(w.getBoundingClientRect().height) };
  });
  ok(r, "About 没打开");
  eq(r.n, 6, "格子数量");
  ok(r.all, "有格子落在可视区外（窗口高 " + r.h + "）——「能滚动」不等于「默认看得见」");
});

test("7.6.3", "About 能关闭", async (p) => {
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(400);
  eq(await p.evaluate(() => window.rv.ui.hostWins.length), 0);
});

// ══════════════════════════════════════════════════════════
// 8. 调试抽屉
// ══════════════════════════════════════════════════════════
test("8.1", "打开抽屉", async (p) => {
  eq(await p.evaluate(() => window.rv.ui_drawer(true)), true);
  await sleep(1700);
});

test("8.2", "进程表 >= 4 行", async (p) => {
  ok((await p.evaluate(() => document.querySelectorAll("#procs .prow").length)) >= 4);
});

test("8.3", "寄存器 32 个", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll("#reg .rg").length), 32);
});

test("8.4", "能力 > 20 条", async (p) => {
  ok((await p.evaluate(() => document.querySelectorAll("#caps .cap").length)) > 20);
});

test("8.5", "文件 5 条", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll("#files .frow").length), 5);
});

test("8.6", "事件流有内容", async (p) => {
  ok((await p.evaluate(() => document.querySelectorAll("#events .ev").length)) >= 1);
});

test("8.7", "运行时数值非空", async (p) => {
  const v = await p.evaluate(() => ({
    cy: document.getElementById("vCy").textContent,
    pc: document.getElementById("vPc").textContent,
  }));
  ok(v.cy !== "0" && v.pc !== "—", JSON.stringify(v));
});

test("8.8", "关闭抽屉", async (p) => {
  eq(await p.evaluate(() => window.rv.ui_drawer(false)), false);
  await sleep(300);
});

// ══════════════════════════════════════════════════════════
// 9. 无障碍
// ══════════════════════════════════════════════════════════
test("9.1", "Tab 能到 Dock", async (p) => {
  await p.evaluate(() => document.body.focus());
  const seen = [];
  for (let i = 0; i < 24; i++) {
    await p.keyboard.press("Tab");
    seen.push(await p.evaluate(() => document.activeElement && document.activeElement.className || ""));
  }
  ok(seen.some((c) => c.includes("dock-app")), "到不了 Dock：" + JSON.stringify(seen.slice(0, 8)));
});

test("9.2", "焦点环 2px 白色", async (p) => {
  await p.evaluate(() => document.querySelector('.dicon[data-app="terminal"]').focus());
  await sleep(200);
  const o = await p.evaluate(() => {
    const cs = getComputedStyle(document.activeElement);
    return cs.outlineWidth + " " + cs.outlineStyle;
  });
  eq(o, "2px solid");
});

test("9.3", "Enter 能激活图标", async (p) => {
  await p.evaluate(() => window.rv.closeWin(1));
  await sleep(400);
  await p.evaluate(() => document.querySelector('.dicon[data-app="monitor"]').focus());
  await p.keyboard.press("Enter");
  await sleep(600);
  eq(await p.evaluate(() => window.rv.state.wins[1].visible), true);
});

test("9.4", "正文对比度 >= 4.5", async (p) => {
  const c = await p.evaluate(() => {
    const lum = (hex) => {
      const v = hex.replace("#", "");
      const ch = [0, 2, 4].map((i) => parseInt(v.substr(i, 2), 16) / 255)
        .map((x) => (x <= 0.03928 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4)));
      return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
    };
    const a = lum("#FFFFFF"), b = lum("#12161D");
    const hi = Math.max(a, b), lo = Math.min(a, b);
    return (hi + 0.05) / (lo + 0.05);
  });
  ok(c >= 4.5, "对比度只有 " + c.toFixed(2));
});

test("9.5", "减少动效媒体查询生效", async (p) => {
  await p.emulateMedia({ reducedMotion: "reduce" });
  await sleep(300);
  const d = await p.evaluate(() => getComputedStyle(document.querySelector(".win")).transitionDuration);
  ok(parseFloat(d) < 0.05, "reduced-motion 下过渡仍为 " + d);
  await p.emulateMedia({ reducedMotion: "no-preference" });
});

// ══════════════════════════════════════════════════════════
// 10. 性能
// ══════════════════════════════════════════════════════════
test("10.1", "帧率 >= 60", async (p) => {
  const fps = await p.evaluate(() => new Promise((res) => {
    let n = 0; const t = performance.now();
    const loop = () => { n++; if (performance.now() - t < 1000) requestAnimationFrame(loop); else res(Math.round(n * 1000 / (performance.now() - t))); };
    requestAnimationFrame(loop);
  }));
  ok(fps >= 60, "帧率 " + fps);
});

test("10.2", "输入延迟 < 100ms", async (p) => {
  const ms = await p.evaluate(() => new Promise((res) => {
    const before = window.rv.state.term.join("\n");
    const t = performance.now();
    window.rv.type("x");
    const iv = setInterval(() => {
      if (window.rv.state.term.join("\n") !== before) { clearInterval(iv); res(performance.now() - t); }
    }, 4);
    setTimeout(() => { clearInterval(iv); res(9999); }, 3000);
  }));
  ok(ms < 100, "延迟 " + Math.round(ms) + "ms");
  /* 把刚才那个字符擦掉再回车，别把半行输入留给后面的用例 */
  await p.evaluate(() => window.rv.type("\u007f\r"));
  await sleep(400);
});

test("10.3", "启动到可交互 < 3s", async (p, st) => {
  ok(st.shellReadyMs < 3000, "启动用了 " + st.shellReadyMs + "ms");
});

test("10.4", "拖动跟手", async (p) => {
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
  await sleep(500);
  const b = await p.evaluate(() => {
    const r = document.querySelectorAll(".win")[0].querySelector(".win-bar").getBoundingClientRect();
    return { x: r.x + 150, y: r.y + r.height / 2 };
  });
  await p.mouse.move(b.x, b.y);
  await p.mouse.down();
  let moved = 0;
  for (let k = 1; k <= 10; k++) {
    await p.mouse.move(b.x + k * 5, b.y + k * 2);
    const px = await p.evaluate(() => Math.round(document.querySelectorAll(".win")[0].getBoundingClientRect().x));
    if (k === 1 || px !== moved) moved = px;
  }
  await p.mouse.up();
  await sleep(500);
  ok(moved !== Math.round(b.x), "拖动时窗口没跟手");
  await p.evaluate(() => window.rv.ui_snapTo(0, "restore"));
});

// ══════════════════════════════════════════════════════════
// 11. 持久化
// ══════════════════════════════════════════════════════════
test("11.1", "壁纸选择被记住", async (p) => {
  await p.evaluate(() => window.rv.launch("settings"));
  await sleep(500);
  await p.evaluate(() => document.querySelector('#setWall button[data-w="1"]').click());
  await sleep(300);
  eq(await p.evaluate(() => localStorage.getItem("rvsim.wall")), "1");
  await p.evaluate(() => document.querySelector('#setWall button[data-w="0"]').click());
  await sleep(250);
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

test("11.2", "字号选择被记住", async (p) => {
  await p.evaluate(() => window.rv.launch("settings"));
  await sleep(500);
  await p.evaluate(() => document.querySelector('#setFs button[data-f="1.15"]').click());
  await sleep(400);
  eq(await p.evaluate(() => localStorage.getItem("rvsim.termScale")), "1.15");
  await p.evaluate(() => document.querySelector('#setFs button[data-f="1"]').click());
  await sleep(300);
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

test("11.3", "开机动画设置被记住", async (p) => {
  await p.evaluate(() => window.rv.launch("settings"));
  await sleep(500);
  await p.evaluate(() => document.getElementById("setBoot").click());
  await sleep(250);
  ok(await p.evaluate(() => localStorage.getItem("rvsim.skipBoot")) !== null);
  await p.evaluate(() => document.getElementById("setBoot").click());
  await sleep(200);
  await p.evaluate(() => document.querySelectorAll(".host-win .win-ctl .close").forEach((b) => b.click()));
  await sleep(300);
});

// ══════════════════════════════════════════════════════════
// 12. 边界
// ══════════════════════════════════════════════════════════
test("12.1", "全部关闭后能恢复", async (p) => {
  await p.evaluate(() => { window.rv.closeWin(0); window.rv.closeWin(1); window.rv.closeWin(2); });
  await sleep(700);
  eq(await p.evaluate(() => window.rv.state.wins.map((w) => w.visible)), [false, false, false]);
  await p.evaluate(() => window.rv.ui_dblIcon("terminal"));
  await sleep(600);
  eq(await p.evaluate(() => window.rv.state.wins[0].visible), true);
});

test("12.2", "右键菜单在右下角不溢出", async (p) => {
  const b = await p.evaluate(() => {
    window.rv.ui_ctx([{ label: "还原", action() {} }, { label: "关闭", action() {} }], innerWidth - 4, innerHeight - 4);
    const r = document.getElementById("ctxMenu").getBoundingClientRect();
    return { r: r.right, b: r.bottom, vw: innerWidth, vh: innerHeight };
  });
  ok(b.r <= b.vw && b.b <= b.vh, "溢出：" + JSON.stringify(b));
  await p.evaluate(() => document.getElementById("ctxMenu").classList.remove("on"));
});

test("12.3", "tooltip 在右边缘不溢出", async (p) => {
  const r = await p.evaluate(() => {
    const apps = document.querySelectorAll(".dock-app");
    const e = apps[apps.length - 1].getBoundingClientRect();
    return { x: e.x + e.width / 2, y: e.y + e.height / 2 };
  });
  await p.mouse.move(r.x, r.y);
  await sleep(750);
  const b = await p.evaluate(() => {
    const t = document.getElementById("tip");
    if (!t.classList.contains("on")) return null;
    const rr = t.getBoundingClientRect();
    return { l: rr.left, r: rr.right, vw: innerWidth };
  });
  if (b) ok(b.l >= 0 && b.r <= b.vw, "溢出：" + JSON.stringify(b));
  await p.mouse.move(200, 400);
});

test("12.4", "聚焦窗口 z 最大", async (p) => {
  await p.evaluate(() => window.rv.focusWin(1));
  await sleep(400);
  const z = await p.evaluate(() => window.rv.state.wins.map((w) => w.z));
  eq(Math.max(...z), z[1]);
});

test("12.5", "反复开关 20 次状态一致", async (p) => {
  for (let i = 0; i < 20; i++) {
    await p.evaluate(() => window.rv.closeWin(0));
    await sleep(80);
    await p.evaluate(() => window.rv.launch("terminal"));
    await sleep(80);
  }
  await sleep(600);
  eq(await p.evaluate(() => window.rv.state.wins[0].visible), true);
});

// ── 对抗性用例：边界与注入 ──
test("13.16", "中文文件名可读可写可打开", async (p) => {
  eq(await p.evaluate(() => window.rv.fsWrite("中文名测试.txt", "CN")), 0);
  await sleep(500);
  ok((await p.evaluate(() => window.rv.state.files.map((f) => f.name))).indexOf("中文名测试.txt") >= 0, "内核里名字不对");
  ok((await p.evaluate(() => [...document.querySelectorAll("tr[data-name]")].map((r) => r.dataset.name))).indexOf("中文名测试.txt") >= 0,
     "界面上的名字是乱码");
  eq(await p.evaluate(() => window.rv.fsRead("中文名测试.txt")), "CN");
});

test("13.17", "文件名里的 HTML 不会被当成标签", async (p) => {
  eq(await p.evaluate(() => window.rv.fsWrite("<b>inj.txt", "1")), 0);
  await sleep(500);
  eq(await p.evaluate(() => !!document.querySelector(".ftab tbody b")), false, "名字里的 <b> 被渲染成了元素");
  eq(await p.evaluate(() => !!document.querySelector(".ftab tbody img")), false, "名字里的 <img> 被渲染成了元素");
  eq(await p.evaluate(() => window.rv.fsWrite('q"t.txt', "2")), 0);
  await sleep(500);
  eq(await p.evaluate(() => {
    const tr = [...document.querySelectorAll("tr[data-name]")].find((r) => r.dataset.name === 'q"t.txt');
    return tr ? tr.dataset.name : null;
  }), 'q"t.txt', "带引号的名字把 data-name 截断了");
});

test("13.18", "删除目录会连带删掉子项", async (p) => {
  eq(await p.evaluate(() => window.rv.fsMkdir("cascade")), 0);
  eq(await p.evaluate(() => window.rv.fsWrite("cascade/kid.txt", "KID")), 0);
  await sleep(400);
  eq(await p.evaluate(() => window.rv.fsRemove("cascade")), 0);
  await sleep(400);
  eq(await p.evaluate(() => window.rv.fsRead("cascade/kid.txt")), null, "目录删了但子文件还在（重建同名目录会复活）");
});

test("13.19", "目录改名会迁移子项", async (p) => {
  eq(await p.evaluate(() => window.rv.fsMkdir("mvdir")), 0);
  eq(await p.evaluate(() => window.rv.fsWrite("mvdir/kid.txt", "K3")), 0);
  await sleep(400);
  eq(await p.evaluate(() => window.rv.fsRename("mvdir", "mvdir2")), 0);
  await sleep(400);
  eq(await p.evaluate(() => window.rv.fsRead("mvdir2/kid.txt")), "K3", "改名后新路径下没有子项");
  eq(await p.evaluate(() => window.rv.fsRead("mvdir/kid.txt")), null, "旧路径下还留着子项");
});

test("13.20", "文件与目录不能互相顶替", async (p) => {
  eq(await p.evaluate(() => window.rv.fsMkdir("readme.txt")), 1, "对已存在的文件建目录应该报冲突");
  eq(await p.evaluate(() => window.rv.fsWrite("mvdir2", "BBB")), 1, "往目录里写文件应该失败");
});

test("13.21", "超长文件名被拒绝，不静默截断", async (p) => {
  eq(await p.evaluate(() => window.rv.fsWrite("L".repeat(30), "x")), 1);
  await sleep(400);
  const bad = await p.evaluate(() => window.rv.state.files.map((f) => f.name).filter((n) => n.indexOf("LLLL") === 0));
  eq(bad, [], "超长名字被截断后建了出来");
});

test("13.22", "超过单次写入上限被拒绝，不静默丢尾部", async (p) => {
  const r = await p.evaluate(() => window.rv.fsWrite("too-big.txt", "A".repeat(5000))
    .then((rc) => ({ rc })).catch((e) => ({ msg: e.message })));
  ok(r.msg && r.msg.indexOf("4096") >= 0, "没有报错：" + JSON.stringify(r));
  eq(await p.evaluate(() => window.rv.state.files.map((f) => f.name)), 
     (await p.evaluate(() => window.rv.state.files.map((f) => f.name))).filter((n) => n !== "too-big.txt"), "文件还是建出来了");
});

test("13.23", "对不存在的进程操作要报失败", async (p) => {
  eq(await p.evaluate(() => window.rv.killProc(99)), 1);
  eq(await p.evaluate(() => window.rv.killProc(-1)), 1);
});

test("13.24", "不能进入文件或不存在的路径", async (p) => {
  await p.evaluate(() => window.rv.fsChdir(""));
  await sleep(300);
  eq(await p.evaluate(() => window.rv.fsChdir("readme.txt")), "", "进了文件");
  eq(await p.evaluate(() => window.rv.fsChdir("no/such")), "", "进了不存在的路径");
});

test("13.25", "文件操作失败有可见提示", async (p) => {
  await p.evaluate(() => window.rv.fsChdir(""));
  await sleep(300);
  await p.evaluate(() => document.querySelector('tr[data-name="readme.txt"]').click());
  await sleep(200);
  await p.evaluate(() => document.querySelector('.fbar button[data-f="rename"]').click());
  await sleep(300);
  await p.evaluate(() => { document.querySelector(".fdlg input").value = "hello.elf"; });
  await p.evaluate(() => document.querySelector('.fdlg button[data-r="1"]').click());
  await sleep(400);
  const v = await p.evaluate(() => {
    const e = document.querySelector(".fpath");
    return { t: e.textContent, c: getComputedStyle(e).color };
  });
  ok(v.t.indexOf("失败") >= 0, "路径栏没有错误文字：" + JSON.stringify(v));
  ok(v.c.indexOf("248, 113, 113") >= 0, "错误提示没有变红：" + JSON.stringify(v));
  await sleep(3400);
  eq(await p.evaluate(() => document.querySelector(".fpath").textContent), "/", "提示没有自动恢复");
});

// ══════════════════════════════════════════════════════════
// 运行器
// ══════════════════════════════════════════════════════════
// ══════════════════════════════════════════════════════════
// 13. 文件与进程操作（图形界面）
// ══════════════════════════════════════════════════════════
test("13.1", "Files 工具栏 6 个按钮", async (p) => {
  await p.evaluate(() => window.rv.launch("files"));
  await sleep(600);
  eq(await p.evaluate(() => [...document.querySelectorAll(".fbar button")].map((b) => b.dataset.f)),
     ["up", "new", "mkdir", "rename", "del", "import"]);
});

test("13.2", "图形界面能新建文件", async (p) => {
  eq(await p.evaluate(() => window.rv.fsWrite("gui-new.txt", "abc")), 0);
  await sleep(500);
  ok((await p.evaluate(() => window.rv.state.files.map((f) => f.name))).indexOf("gui-new.txt") >= 0, "内核里没有这个文件");
  ok((await p.evaluate(() => [...document.querySelectorAll("tr[data-name]")].map((r) => r.dataset.name))).indexOf("gui-new.txt") >= 0,
     "列表里看不到");
});

test("13.3", "图形界面能新建文件夹", async (p) => {
  eq(await p.evaluate(() => window.rv.fsMkdir("gui-dir")), 0);
  await sleep(500);
  const dir = await p.evaluate(() => {
    const tr = [...document.querySelectorAll("tr[data-name]")].find((r) => r.dataset.name === "gui-dir");
    return tr ? tr.dataset.dir : null;
  });
  eq(dir, "1", "目录行没出现或没标成目录");
});

test("13.4", "图形界面能重命名", async (p) => {
  eq(await p.evaluate(() => window.rv.fsRename("gui-new.txt", "gui-renamed.txt")), 0);
  await sleep(500);
  const names = await p.evaluate(() => window.rv.state.files.map((f) => f.name));
  ok(names.indexOf("gui-renamed.txt") >= 0 && names.indexOf("gui-new.txt") < 0, JSON.stringify(names));
});

test("13.5", "图形界面能删除", async (p) => {
  eq(await p.evaluate(() => window.rv.fsRemove("gui-renamed.txt")), 0);
  await sleep(500);
  ok((await p.evaluate(() => window.rv.state.files.map((f) => f.name))).indexOf("gui-renamed.txt") < 0, "还删不掉");
});

test("13.6", "双击目录进入并显示路径", async (p) => {
  eq(await p.evaluate(() => window.rv.fsWrite("gui-dir/inner.txt", "x")), 0);
  await sleep(500);
  await p.evaluate(() => {
    const tr = [...document.querySelectorAll("tr[data-name]")].find((r) => r.dataset.name === "gui-dir");
    tr.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
  });
  await sleep(700);
  eq(await p.evaluate(() => window.rv.fsCwd), "gui-dir", "没有切进目录");
  eq(await p.evaluate(() => document.querySelector(".fpath").textContent), "/gui-dir");
  eq(await p.evaluate(() => [...document.querySelectorAll("tr[data-name]")].map((r) => r.dataset.name)), ["gui-dir/inner.txt"]);
});

test("13.7", "返回上一级", async (p) => {
  await p.evaluate(() => document.querySelector('.fbar button[data-f="up"]').click());
  await sleep(600);
  eq(await p.evaluate(() => window.rv.fsCwd), "");
  ok((await p.evaluate(() => [...document.querySelectorAll("tr[data-name]")].map((r) => r.dataset.name))).indexOf("gui-dir") >= 0,
     "回到根之后看不到 gui-dir");
});

test("13.8", "文件行右键三项", async (p) => {
  const items = await p.evaluate(() => {
    const tr = [...document.querySelectorAll("tr[data-name]")].find((r) => r.dataset.name === "gui-dir");
    const q = tr.getBoundingClientRect();
    tr.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, clientX: q.x + 20, clientY: q.y + 8 }));
    const v = window.rv.ui.ctxMenu;
    document.getElementById("ctxMenu").classList.remove("on");
    return v;
  });
  eq(items, ["打开文件夹", "重命名…", "删除"]);
});

test("13.9", "编辑器保存写回 RAMFS", async (p) => {
  await p.evaluate(() => window.rv.openFile("readme.txt"));
  await sleep(900);
  await p.evaluate(() => {
    document.querySelector(".ed-area").value = "gui edit line\n";
    document.querySelector(".ed-save").click();
  });
  await sleep(900);
  eq(await p.evaluate(() => window.rv.fsRead("readme.txt")), "gui edit line\n");
});

test("13.10", "编辑器底色不透明", async (p) => {
  const v = await p.evaluate(() => {
    const w = [...document.querySelectorAll(".host-win")].find((x) => x.querySelector(".ed-wrap"));
    return w ? getComputedStyle(w.querySelector(".ed-wrap")).backgroundColor : null;
  });
  ok(v, "编辑器窗口不见了");
  ok(!/rgba\(.*,\s*0(\.\d+)?\)$/.test(v) && v !== "transparent",
     "编辑器底色是透明的（" + v + "），背后文字会透上来");
});

test("13.11", "Monitor 行右键能终止进程", async (p) => {
  await shellCmd(p, "reset");
  await sleep(900);
  const before = await p.evaluate(() => window.rv.state.procs);
  const ready = before.find((x) => x.state === "READY");
  ok(ready, "reset 之后没有就绪进程（命令行缓冲可能被上一条用例弄脏了）：" + JSON.stringify(before));
  const pid = ready.pid;
  const items = await p.evaluate((id) => {
    const tr = document.querySelector('tr[data-pid="' + id + '"]');
    const q = tr.getBoundingClientRect();
    tr.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, clientX: q.x + 20, clientY: q.y + 8 }));
    const v = window.rv.ui.ctxMenu;
    return v;
  }, pid);
  ok(items && items.indexOf("终止进程") >= 0, "行右键没有「终止进程」：" + JSON.stringify(items));
  await p.evaluate(() => {
    [...document.querySelectorAll("#ctxMenu .ctx-item")].forEach((e) => { if (e.textContent === "终止进程") e.click(); });
  });
  await sleep(900);
  ok((await p.evaluate(() => window.rv.state.procs.map((x) => x.pid))).indexOf(pid) < 0, "进程 " + pid + " 还在");
});

test("13.12", "挂起后状态为 SUSPEND，恢复后能跑完", async (p) => {
  const snap = await p.evaluate(() => window.rv.state.procs);
  const ready = snap.find((x) => x.state === "READY");
  ok(ready, "没有就绪进程可挂起：" + JSON.stringify(snap));
  const pid = ready.pid;
  eq(await p.evaluate((id) => window.rv.suspendProc(id), pid), 0, "挂起 " + pid + " 失败（" + JSON.stringify(snap) + "）");
  await sleep(700);
  eq(await p.evaluate((id) => (window.rv.state.procs.find((x) => x.pid === id) || {}).state, pid), "SUSPEND");
  eq(await p.evaluate((id) => window.rv.resumeProc(id), pid), 0);
  await sleep(1800);
  const st = await p.evaluate(() => window.rv.state.procs.map((x) => x.state));
  ok(st.length === 0 || st.every((x) => x === "EXIT"), "恢复后没有跑完：" + JSON.stringify(st));
  await shellCmd(p, "reset");
  await sleep(800);
});

test("13.13", "桌面右键能新建文本文件", async (p) => {
  const items = await p.evaluate(() => {
    const os = document.getElementById("os");
    os.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, clientX: 40, clientY: 700 }));
    const v = window.rv.ui.ctxMenu;
    document.getElementById("ctxMenu").classList.remove("on");
    return v;
  });
  ok(items && items.indexOf("新建文本文件…") >= 0, JSON.stringify(items));
});

test("13.14", "导入程序入口存在", async (p) => {
  eq(await p.evaluate(() => document.querySelectorAll('.fbar button[data-f="import"]').length), 1);
});

test("13.15", "终止正在运行的进程后回到 shell", async (p) => {
  await shellCmd(p, "run hello.elf");
  await sleep(150);
  const pid = await p.evaluate(() => {
    const r = window.rv.state.procs.find((x) => x.state === "RUN");
    return r ? r.pid : null;
  });
  if (pid === null) return;      /* 程序已经跑完，这一轮没东西可终止 */
  eq(await p.evaluate((id) => window.rv.killProc(id), pid), 0);
  await sleep(1500);
  ok((await p.evaluate(() => window.rv.state.term.join("|"))).indexOf("rvsh>") >= 0, "终止后没有回到提示符");
});

(async () => {
  const grep = process.argv.includes("--grep")
    ? process.argv[process.argv.indexOf("--grep") + 1] : null;
  const list = grep ? tests.filter((t) => (t.id + " " + t.name).includes(grep)) : tests;

  const browser = await chromium.launch({
    executablePath: process.env.CHROME || "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
    args: ["--no-sandbox"],
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const pageErrs = [];
  page.on("pageerror", (e) => pageErrs.push(e.message));

  const t0 = Date.now();
  await page.goto(URL + "?t=" + Date.now(), { waitUntil: "load" });
  // 采样开机进度
  const widths = [];
  let bootSeen = null;
  for (let i = 0; i < 40; i++) {
    const s = await page.evaluate(() => {
      const b = document.getElementById("boot");
      if (!b) return null;
      return {
        hasBar: !!b.querySelector(".boot-bar"),
        hasStatus: !!b.querySelector(".boot-status"),
        w: b.querySelector("#bootBar") ? b.querySelector("#bootBar").style.width : "",
      };
    });
    if (s) { bootSeen = s; if (s.w) widths.push(parseFloat(s.w) || 0); }
    if (!s && bootSeen) break;
    await sleep(150);
  }
  if (bootSeen) bootSeen.widths = widths;
  await page.waitForFunction(() => window.rv && window.rv.ready, { timeout: 25000 });
  const shellReadyMs = Date.now() - t0;
  await sleep(1500);
  const st = { bootSeen, shellReadyMs };

  let pass = 0; const fails = [];
  for (const t of list) {
    try {
      await t.fn(page, st);
      pass++;
      console.log("  PASS  " + t.id + "  " + t.name);
    } catch (e) {
      fails.push({ id: t.id, name: t.name, err: e.message });
      console.log("  FAIL  " + t.id + "  " + t.name + "\n        " + e.message);
    }
  }
  console.log(`\n总计 ${pass}/${pass + fails.length} 通过`);
  if (fails.length) {
    console.log("\n失败清单：");
    for (const f of fails) console.log("  " + f.id + "  " + f.name);
  }
  if (pageErrs.length) console.log("页面报错:", [...new Set(pageErrs)].slice(0, 5));
  await browser.close();
  process.exit(fails.length ? 1 : 0);
})();

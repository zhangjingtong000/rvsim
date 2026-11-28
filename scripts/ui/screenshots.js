// 生成 docs/screenshots/ 的界面截图（桌面 / 终端 / Monitor）。
//
// 用法（Windows 侧，需 playwright + Chrome）：
//   NODE_PATH=... node scripts/ui/screenshots.js
//
// 前置：先起一个静态服务，例如 `python3 -m http.server 8799 --directory web`。
// 输出固定三张：desktop.png / terminal.png / monitor.png。
const fs = require("fs");
const path = require("path");

function CHROME_PATH() {
  if (process.env.CHROME) return process.env.CHROME;
  const cands = [
    "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
    "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
    "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
  ];
  for (const c of cands) {
    try { if (fs.existsSync(c)) return c; } catch (_) {}
  }
  return undefined;
}

const { chromium } = require("playwright");

const URL = process.env.RVSIM_URL || "http://127.0.0.1:8799/index.html";
const OUT = path.join(__dirname, "..", "..", "docs", "screenshots");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function shot(page, file, label) {
  await page.screenshot({ path: path.join(OUT, file), fullPage: false });
  const st = fs.statSync(path.join(OUT, file));
  console.log(`  ${file}  ${st.size} 字节  (${label})`);
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await chromium.launch({
    executablePath: CHROME_PATH(),
    args: ["--no-sandbox", "--force-device-scale-factor=2"],
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const errs = [];
  page.on("pageerror", (e) => errs.push(e.message));

  await page.goto(URL + "?shot=" + Date.now(), { waitUntil: "load" });
  await page.waitForFunction(() => window.rv && window.rv.ready, { timeout: 30000 });
  await sleep(3500); // 开机动画 + 内核启动完成

  const wins = await page.evaluate(() =>
    window.rv.state.wins.map((w) => ({ id: w.id, visible: w.visible, x: w.x, y: w.y, w: w.w, h: w.h })));
  console.log("窗口:", JSON.stringify(wins));

  // 终端里留一段读得出东西的输出（内核已经在跑，补一条 ps）
  await page.evaluate(() => window.rv.cmd("ps"));
  await sleep(1200);

  // 1) 桌面：三个窗口都可见，终端在前
  await page.evaluate(() => window.rv.launch("terminal"));
  await sleep(700);
  await shot(page, "desktop.png", "桌面：终端 / Monitor / Files");

  // 2) 终端特写
  await page.evaluate(() => window.rv.cmd("status"));
  await sleep(900);
  const term = page.locator(".win").nth(0);
  await term.screenshot({ path: path.join(OUT, "terminal.png") });
  console.log(`  terminal.png  ${fs.statSync(path.join(OUT, "terminal.png")).size} 字节`);

  // 3) Monitor 特写：先跑一个程序留下事件，再 reset 让进程回到 READY
  //    （resume 之后 badptr 又会崩，所以截 reset 的那一刻）
  await page.evaluate(() => {
    window.rv.launch("monitor");
    window.rv.cmd("run badptr");
  });
  await sleep(1500);
  await page.evaluate(() => window.rv.cmd("reset"));
  await sleep(900);
  const mon = page.locator(".win").nth(1);
  await mon.screenshot({ path: path.join(OUT, "monitor.png") });
  console.log(`  monitor.png  ${fs.statSync(path.join(OUT, "monitor.png")).size} 字节`);

  if (errs.length) console.log("页面报错:", [...new Set(errs)].slice(0, 5));
  await browser.close();
})();

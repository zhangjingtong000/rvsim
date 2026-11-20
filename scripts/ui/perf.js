// 性能实测：启动、帧率、输入延迟、吞吐、内存
const { chromium } = require("playwright");

(async () => {
  const browser = await chromium.launch({
    executablePath: "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
    args: ["--no-sandbox"],
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  const t0 = Date.now();
  await page.goto("http://127.0.0.1:8799/index.html?t=" + Date.now(), { waitUntil: "load" });
  const tLoad = Date.now() - t0;

  // 等 shell 就绪（终端里出现 rvsh）
  await page.waitForFunction(() => {
    const h = document.querySelector(".term-dom");
    return h && h.textContent.indexOf("rvsh") >= 0;
  }, { timeout: 30000 });
  const tShell = Date.now() - t0;

  await page.waitForTimeout(3000);

  const R = {};

  // 1) 帧率：1 秒内 requestAnimationFrame 次数
  R.fps = await page.evaluate(() => new Promise((res) => {
    let n = 0; const t = performance.now();
    const loop = () => {
      n++;
      if (performance.now() - t < 1000) requestAnimationFrame(loop);
      else res(Math.round(n * 1000 / (performance.now() - t)));
    };
    requestAnimationFrame(loop);
  }));

  // 2) 吞吐：读托盘速率
  R.rate = await page.evaluate(() => window.rv.state.rate);

  // 3) 输入延迟：发一个字符，量它出现在终端网格的时间
  const lat = [];
  for (let k = 0; k < 5; k++) {
    const ms = await page.evaluate(() => new Promise((res) => {
      const before = window.rv.state.term.join("\n");
      const t = performance.now();
      window.rv.type("x");
      const iv = setInterval(() => {
        const now = window.rv.state.term.join("\n");
        if (now !== before) { clearInterval(iv); res(performance.now() - t); }
      }, 4);
      setTimeout(() => { clearInterval(iv); res(-1); }, 2000);
    }));
    lat.push(Math.round(ms));
  }
  R.inputLatencyMs = lat;

  // 4) 内存
  R.mem = await page.evaluate(() => {
    const m = window.__rvsim;
    return {
      wasmHeapMB: m ? Math.round(m.HEAPU8.length / 1048576) : null,
      jsHeapMB: performance.memory ? Math.round(performance.memory.usedJSHeapSize / 1048576) : null,
    };
  });

  // 5) 窗口拖动时的帧率（拖动 1 秒）
  const bar = await page.evaluate(() => {
    const b = document.querySelectorAll(".win")[0].querySelector(".win-bar").getBoundingClientRect();
    return { x: b.x + 200, y: b.y + b.height / 2 };
  });
  await page.mouse.move(bar.x, bar.y);
  await page.mouse.down();
  const dragFps = await page.evaluate(() => new Promise((res) => {
    let n = 0; const t = performance.now();
    const loop = () => { n++; if (performance.now() - t < 1000) requestAnimationFrame(loop); else res(Math.round(n * 1000 / (performance.now() - t))); };
    requestAnimationFrame(loop);
  }));
  for (let k = 0; k < 20; k++) { await page.mouse.move(bar.x + k * 6, bar.y + k * 2); await page.waitForTimeout(40); }
  await page.mouse.up();
  R.dragFps = dragFps;

  R.startup = { pageLoadMs: tLoad, shellReadyMs: tShell };

  console.log(JSON.stringify(R, null, 1));
  await browser.close();
})();

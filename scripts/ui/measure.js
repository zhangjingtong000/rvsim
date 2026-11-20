// 量取当前界面的所有关键尺寸/颜色，输出成可对比的 JSON
const { chromium } = require("playwright");

(async () => {
  const browser = await chromium.launch({
    executablePath: "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
    args: ["--no-sandbox"],
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  await page.goto("http://127.0.0.1:8799/index.html?t=" + Date.now(), { waitUntil: "load" });
  await page.waitForTimeout(8500);

  // 打开开始菜单，才能量它
  await page.click("#startBtn");
  await page.waitForTimeout(400);

  const m = await page.evaluate(() => {
    const q = (s) => document.querySelector(s);
    const cs = (s, p) => { const e = q(s); return e ? getComputedStyle(e)[p] : null; };
    const box = (s) => { const e = q(s); if (!e) return null; const b = e.getBoundingClientRect(); return [Math.round(b.width), Math.round(b.height)]; };
    const px = (v) => parseFloat(v) || 0;
    const rgb = (s, p) => { const e = q(s); return e ? getComputedStyle(e)[p] : null; };

    return {
      dock: {
        h: px(cs("#dock", "height")),
        bg: rgb("#dock", "backgroundColor"),
        borderTop: cs("#dock", "borderTopWidth") + " " + cs("#dock", "borderTopColor"),
        appSize: box(".dock-app"),
        appGap: cs(".dock-apps", "gap"),
        svgSize: box(".dock-app svg"),
        radius: cs(".dock-app", "borderRadius"),
      },
      win: {
        radius: cs(".win", "borderRadius"),
        border: cs(".win", "borderWidth") + " " + cs(".win", "borderColor"),
        barH: px(cs(".win-bar", "height")),
        barBg: cs(".win-bar", "backgroundImage"),
        titleSize: cs(".win-title", "fontSize"),
        titleWeight: cs(".win-title", "fontWeight"),
        titleColor: rgb(".win-title", "color"),
        ctlBtnW: box(".win-ctl button"),
        bodyBg: rgb(".win-body", "backgroundColor"),
        shadow: cs(".win", "boxShadow"),
      },
      icon: {
        cell: box(".dicon"),
        svg: box(".dicon svg"),
        labelSize: cs(".dicon span", "fontSize"),
        labelColor: rgb(".dicon span", "color"),
        gap: cs("#icons", "gap"),
      },
      start: {
        box: box("#startMenu"),
        padding: cs("#startMenu", "padding"),
        radius: cs("#startMenu", "borderRadius"),
        bg: rgb("#startMenu", "backgroundColor"),
        searchH: px(cs(".sm-search", "height")),
        searchRadius: cs(".sm-search", "borderRadius"),
        tile: box(".sm-tile"),
        tileSvg: box(".sm-tile svg"),
        tileLabelSize: cs(".sm-tile span", "fontSize"),
        secSize: cs(".sm-sec", "fontSize"),
        secColor: rgb(".sm-sec", "color"),
        riH: px(cs(".sm-ri", "height")),
        riSize: cs(".sm-ri", "fontSize"),
      },
      term: {
        fontSize: cs(".term-dom", "fontSize"),
        lineHeight: cs(".term-dom", "lineHeight"),
        color: rgb(".term-dom", "color"),
        bg: rgb(".term-dom", "backgroundColor"),
        padding: cs(".term-dom", "padding"),
      },
      panel: {
        thH: px(cs(".ptab th", "height")),
        thSize: cs(".ptab th", "fontSize"),
        thColor: rgb(".ptab th", "color"),
        tdH: cs(".ptab td", "height"),
        tdSize: cs(".ptab td", "fontSize"),
        tdColor: rgb(".ptab td", "color"),
        rowBorder: cs(".ptab td", "borderBottomColor"),
      },
      tokens: (() => {
        const r = getComputedStyle(document.documentElement);
        return {
          t1: r.getPropertyValue("--t1").trim(),
          t2: r.getPropertyValue("--t2").trim(),
          t3: r.getPropertyValue("--t3").trim(),
          s0: r.getPropertyValue("--s0").trim(),
          s1: r.getPropertyValue("--s1").trim(),
          s2: r.getPropertyValue("--s2").trim(),
          s3: r.getPropertyValue("--s3").trim(),
          s4: r.getPropertyValue("--s4").trim(),
          stroke: r.getPropertyValue("--stroke").trim(),
          acc: r.getPropertyValue("--acc").trim(),
          fontUi: r.getPropertyValue("--font-ui").trim().slice(0, 40),
        };
      })(),
      bodyFontSize: cs("body", "fontSize"),
    };
  });

  console.log(JSON.stringify(m, null, 1));
  await browser.close();
})();

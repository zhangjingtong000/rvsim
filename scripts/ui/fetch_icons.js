// 抓 Fluent UI System Icons（MIT，微软官方），提取 path 供内联使用
const https = require("https");
const fs = require("fs");
const { execSync } = require("child_process");

const ICONS = [
  ["terminal", "Code", "ic_fluent_code_24_regular"],
  ["monitor", "Pulse", "ic_fluent_pulse_24_regular"],
  ["files", "Folder", "ic_fluent_folder_24_regular"],
  ["settings", "Settings", "ic_fluent_settings_24_regular"],
  ["guide", "Book", "ic_fluent_book_24_regular"],
  ["about", "Info", "ic_fluent_info_24_regular"],
];

const base = "https://raw.githubusercontent.com/microsoft/fluentui-system-icons/main/assets/";
const out = {};

for (const [key, dir, name] of ICONS) {
  const url = base + dir + "/SVG/" + name + ".svg";
  try {
    const svg = execSync(
      `powershell -NoProfile -Command "Invoke-WebRequest -Uri '${url}' -Proxy 'http://127.0.0.1:7897' -UseBasicParsing -TimeoutSec 30 | Select-Object -ExpandProperty Content"`,
      { encoding: "utf8", maxBuffer: 10 * 1024 * 1024 }
    );
    // 提取所有 <path .../>，去掉 fill 属性（统一由外层控制）
    const paths = [...svg.matchAll(/<path\b[^>]*?\/?>/g)].map((m) => {
      let p = m[0];
      p = p.replace(/\s*fill="[^"]*"/g, "");
      p = p.replace(/\s*\/>$/, "/>");
      return p;
    });
    out[key] = paths.join("");
    console.log(key.padEnd(9), name.padEnd(34), paths.length, "path(s)", svg.length, "B");
  } catch (e) {
    console.log(key, "FAIL", e.message.slice(0, 80));
  }
}

fs.writeFileSync("E:\\fab\\_ref\\fluent-icons.json", JSON.stringify(out, null, 1));
console.log("\nwrote _ref/fluent-icons.json");

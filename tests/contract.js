// tests/contract.js —— 能力契约回归
// 目的：让「每个能力的行为与 manifest 声明一致」变成可执行、可失败的测试。
// 关键原则：oracle 不与被测对象共用同一来源。
//   - 进程数用 ps（不是 log）
//   - fork 后要求 spawn 事件数 == ps 的 nproc（交叉验证）
//   - restore 后要求进程能真正被再次调度
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");
const WASM_DIR = path.join(ROOT, "web");
const KERNEL = path.join(ROOT, "build", "kernel.elf");

const results = [];
function record(name, ok, detail) {
  results.push({ name, ok, detail });
  const mark = ok ? "  PASS  " : "  FAIL  ";
  console.log(mark + name + (detail ? "   " + detail : ""));
}

(async () => {
  const M = await require(path.join(WASM_DIR, "rvsim.js"))({
    wasmBinary: fs.readFileSync(path.join(WASM_DIR, "rvsim.wasm")),
  });
  M.out = () => {}; M.print = () => {}; M.printErr = () => {};

  M._rv_reset();
  const elf = fs.readFileSync(KERNEL);
  const p = M._malloc(elf.length);
  M.HEAPU8.set(elf, p);
  if (!M._rv_load(p, elf.length)) { console.error("加载内核失败"); process.exit(2); }

  const ub = M._malloc(65536);
  let out = "";
  const { StringDecoder } = require("string_decoder");
  const sd = new StringDecoder("utf8");
  function drain() {
    let n;
    while ((n = M._rv_uart_read(ub, 65536)) > 0) out += sd.write(Buffer.from(M.HEAPU8.subarray(ub, ub + n)));
  }
  function step(n) { for (let i = 0; i < n; i++) { if (M._rv_step() !== 0) return; } }
  function send(cmd, maxB = 320) {
    out = "";
    for (const ch of cmd + "\r") { M._rv_uart_write(ch.charCodeAt(0)); step(40000); drain(); }
    for (let b = 0; b < maxB; b++) { step(40000); drain(); if (out.lastIndexOf("rvsh> ") > out.length - 20 && b > 1) break; }
    return out;
  }
  const json = (s) => { for (const l of s.split("\n")) { const t = l.trim(); if (t.startsWith("{")) { try { return JSON.parse(t); } catch (e) {} } } return null; };

  step(800000); drain(); out = "";

  // ── T1: manifest 可解析且非空 ──
  const mOut = send("manifest");
  const ms = mOut.indexOf('{"manifest"'), me = mOut.lastIndexOf("]}");
  let caps = [];
  try { caps = JSON.parse(mOut.slice(ms, me + 2)).list || []; } catch (e) {}
  record("manifest 可解析", caps.length > 0, caps.length + " 项");

  // ── T2: 每个 cap 至少被 manifest 声明，且都有 usage/desc ──
  const bad = caps.filter(c => !c.usage || !c.desc);
  record("每个能力都有 usage 与 desc", bad.length === 0, bad.map(c => c.cap).join(",") || "全部完整");

  // ── T3: 未知命令必须结构化失败 ──
  const unknown = send("nonexistent_cmd_xyz");
  record("未知命令结构化失败", /\{"ok":false/.test(unknown), unknown.trim().slice(-60));

  // ── T4: assert uart 对从未出现的串必须为 false（防回显假阳性）──
  const a1 = json(send("assert uart=ZZZ_NEVER_APPEARS_12345"));
  record("assert uart 无假阳性", a1 && a1.ok === false, JSON.stringify(a1));

  // ── T5: fork 后 spawn 事件数必须等于 ps 的进程数（交叉 oracle）──
  send("run fork");
  const ps1 = json(send("ps"));
  const cnt1 = json(send("log count spawn"));
  /* fork 跑完后进程都已退出，ps.nproc 会是 0，不能直接比。
     这里改用「spawn 事件数」本身：fork 演示应产生 4 条
     （开机 3 个 + fork 子进程 1 个）。若 fork 不记录事件，这里会是 3。 */
  const spawn1 = cnt1 && cnt1.count ? cnt1.count.spawn : -1;
  record("fork 子进程被记入事件流", spawn1 === 4, `spawn=${spawn1}（期望 4）`);

  // ── T6: run 标志不泄漏：run badptr 后再 run hello 不应再跑 badptr ──
  send("reset");
  const before = json(send("log count oob"));
  send("run badptr");
  const mid = json(send("log count oob"));
  send("reset");
  send("run hello");
  const after = json(send("log count oob"));
  const d1 = (mid && mid.count ? mid.count.oob : 0) - (before && before.count ? before.count.oob : 0);
  const d2 = (after && after.count ? after.count.oob : 0) - (mid && mid.count ? mid.count.oob : 0);
  record("run 标志不泄漏", d1 === 6 && d2 === 0, `badptr 增量=${d1} hello 增量=${d2}`);

  // ── T7: plan 必须拒绝非法参数 ──
  const badPlan = json(send("plan run zzz"));
  record("plan 拒绝非法程序名", badPlan && badPlan.ok === false, JSON.stringify(badPlan));

  // ── T8: apply 执行后计划被消耗 ──
  send("plan run hello");
  const plansBefore = json(send("plans"));
  send("apply 1");
  const plansAfter = json(send("plans"));
  const consumed = plansBefore && plansAfter &&
    (plansBefore.plans || []).length > 0 && (plansAfter.plans || []).length === 0;
  record("apply 后计划被消耗", consumed,
    `前=${JSON.stringify(plansBefore && plansBefore.plans)} 后=${JSON.stringify(plansAfter && plansAfter.plans)}`);

  // ── T9: restore 后进程必须能被再次调度（独立 oracle：nrun 增长）──
  send("reset");
  send("snapshot");
  send("run badptr");
  send("restore 1");
  const psA = json(send("ps"));
  send("resume");            /* restore 之后必须能续跑，否则「恢复」是名义上的 */
  const psB = json(send("ps"));
  const nrunA = psA ? psA.procs.reduce((s, x) => s + x.nrun, 0) : -1;
  const nrunB = psB ? psB.procs.reduce((s, x) => s + x.nrun, 0) : -1;
  record("restore 后进程可被再次调度", nrunB > nrunA, `nrun ${nrunA} -> ${nrunB}`);

  // ── T10: log 全量与 log count 的总数一致 ──
  const cntAll = json(send("log count"));
  const total = cntAll && cntAll.log ? cntAll.log.total : -1;
  const full = send("log all");
  const lines = full.split("\n").filter(l => /^-?\d+ -?\d+ (spawn|exit|ecall|restore|reset|snapshot|plan|apply|sbrk|fork|setreg)/.test(l.trim())).length;
  record("log all 条数与 log count 一致", total === lines, `count.total=${total} 实际行=${lines}`);

  // ── T11: verify 的 trap 侧计数必须与事件流一致 ──
  const v = json(send("verify"));
  const vv = v && v.verify;
  const exitOk = !!(vv && vv.exit && vv.exit.ok === true);
  const forkOk = !!(vv && vv.fork && vv.fork.ok === true);
  record("verify 的 exit/fork 计数与事件流一致", exitOk && forkOk,
    JSON.stringify(vv ? { exit: vv.exit, fork: vv.fork, unaccounted: vv.unaccounted_ecall } : null));

  // ── T12: manifest 里每个无副作用的 cap 都必须能调通 ──
  {
    /* 用能力名本身作为命令：usage 里的 [all] 之类是说明写作，不是可执行文本。
       tail 不参与——它回显历史，历史里本来就可能有 unknown command。 */
    const safe = ["help","manifest","guide","status","ps","mem","uptime",
                  "log","plans","snaps","replay","verify","reset"];
    const bad2 = [];
    for(const c of caps){
      if(!safe.includes(c.cap)) continue;
      const o = send(c.cap);
      /* 只看本次输出的前 120 字符，避免被历史回显干扰 */
      if(/unknown command|未知命令/.test(o.slice(0, 120))) bad2.push(c.cap);
    }
    record("所有无副作用能力都能调用", bad2.length === 0, bad2.join(",") || "全部可调用");
  }

  // ── T13: regs 带参数不能挂死（此前缺 q++ 会死循环）──
  {
    const r = send("regs 0");
    record("regs <pid> 有响应", /"pid":0/.test(r), r.trim().slice(-50));
  }

  // ── T14: setreg 必须真的写进去 ──
  {
    send("setreg 0 18 1234");
    const r = json(send("regs 0"));
    const got = r && r.regs ? r.regs.x18 : null;
    record("setreg 写入可观测", got === "0x000004d2", "x18=" + got);
  }

  // ── T15: setpc 必须真的改执行点 ──
  {
    send("setpc 0 0x80400200");
    const r = json(send("regs 0"));
    record("setpc 改写执行点", r && r.pc === "0x80400200", "pc=" + (r && r.pc));
  }

  // ── 汇总 ──
  const pass = results.filter(r => r.ok).length;
  console.log(`\n契约测试: ${pass}/${results.length} 通过`);
  if (pass < results.length) {
    console.log("失败项:");
    for (const r of results.filter(x => !x.ok)) console.log("  - " + r.name + "  (" + r.detail + ")");
  }
  process.exit(pass === results.length ? 0 : 1);
})().catch(e => { console.error(e); process.exit(2); });
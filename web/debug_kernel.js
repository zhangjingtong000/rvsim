// 调试 kernel.elf 在 WASM 中的执行轨迹
const fs = require("fs");
const path = require("path");

const CSRS = {
  mstatus:0x300, medeleg:0x302, mideleg:0x303,
  mepc:0x341, mcause:0x342, mtval:0x343,
  sstatus:0x100, sepc:0x141, scause:0x142, stval:0x143,
  satp:0x180, mie:0x304, mip:0x344,
};

async function main() {
  const createRvsim = require("./rvsim.js");
  const wasmBinary = fs.readFileSync(path.join(__dirname, "rvsim.wasm"));
  const M = await createRvsim({ wasmBinary });

  M._rv_reset();
  const elfPath = process.argv[2] || "images/kernel.elf";
  const buf = fs.readFileSync(elfPath);
  const p = M._malloc(buf.length);
  M.HEAPU8.set(buf, p);
  const ok = M._rv_load(p, buf.length);
  console.log("加载:", ok ? "成功" : "失败", "大小", buf.length);
  if (!ok) process.exit(1);
  console.log("入口 PC = 0x" + (M._rv_pc() >>> 0).toString(16));

  const uartBuf = M._malloc(8192);
  function drainUart() {
    const n = M._rv_uart_read(uartBuf, 8192);
    if (n > 0) process.stdout.write(Buffer.from(M.HEAPU8.subarray(uartBuf, uartBuf + n)));
    return n;
  }

  const MAX = parseInt(process.argv[3] || "200000", 10);
  const REPORT = 10000;
  let steps = 0;
  let nextReport = REPORT;
  let lastCause = 0;

  while (steps < MAX) {
    const r = M._rv_step();
    if (r !== 0) { console.log("  [step 返回", r, "]"); break; }
    steps++;
    drainUart();

    const mc = M._rv_csr(CSRS.mcause) >>> 0;
    if (mc !== 0 && mc !== lastCause) {
      lastCause = mc;
      const mepc = M._rv_csr(CSRS.mepc) >>> 0;
      const mtval = M._rv_csr(CSRS.mtval) >>> 0;
      console.log("  !! 异常 @" + steps + ": mcause=0x" + mc.toString(16) + " mepc=0x" + mepc.toString(16) + " mtval=0x" + mtval.toString(16) + " priv=" + M._rv_priv());
    }

    if (steps >= nextReport) {
      nextReport += REPORT;
      const pc = M._rv_pc() >>> 0;
      const priv = M._rv_priv();
      const satp = M._rv_csr(CSRS.satp) >>> 0;
      const sc = M._rv_csr(CSRS.scause) >>> 0;
      const sepc = M._rv_csr(CSRS.sepc) >>> 0;
      const stval = M._rv_csr(CSRS.stval) >>> 0;
      console.log("@" + String(steps).padStart(7) + " pc=0x" + pc.toString(16).padStart(8,"0") + " priv=" + priv + " satp=0x" + satp.toString(16) + " scause=0x" + sc.toString(16) + " sepc=0x" + sepc.toString(16) + " stval=0x" + stval.toString(16));
    }
  }

  drainUart();
  console.log("---");
  console.log("执行条数 =", steps);
  console.log("最终 PC  = 0x" + (M._rv_pc() >>> 0).toString(16));
  console.log("特权级   =", ["U","S","?","M"][M._rv_priv()]);
  console.log("结果     =", M._rv_result());
  const abi = ["zero","ra","sp","gp","tp","t0","t1","t2","s0","s1","a0","a1","a2","a3","a4","a5","a6","a7"];
  for (let i = 0; i < abi.length; i++) {
    console.log("  " + abi[i].padEnd(5) + " = 0x" + (M._rv_reg(i) >>> 0).toString(16).padStart(8,"0"));
  }
}

main().catch(e => { console.error(e); process.exit(1); });
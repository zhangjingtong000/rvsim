// 用 Node 验证 WASM 模拟器
const fs = require('fs');
const path = require('path');

async function main() {
  const createRvsim = require('./rvsim.js');
  const wasmBinary = fs.readFileSync(path.join(__dirname, 'rvsim.wasm'));
  const M = await createRvsim({ wasmBinary });

  M._rv_reset();
  console.log('复位后 PC =', '0x' + (M._rv_pc() >>> 0).toString(16));

  // 加载 rv32ui-p-add
  const elfPath = process.argv[2];
  if (!elfPath) { console.error('用法: node test_wasm.js <elf>'); process.exit(2); }
  const buf = fs.readFileSync(elfPath);
  const ptr = M._malloc(buf.length);
  M.HEAPU8.set(buf, ptr);
  const ok = M._rv_load(ptr, buf.length);
  console.log('加载 ELF:', ok ? '成功' : '失败', `(${buf.length} 字节)`);
  if (!ok) process.exit(1);

  console.log('入口 PC =', '0x' + (M._rv_pc() >>> 0).toString(16));

  let steps = 0;
  const MAX = 200000;
  while (steps < MAX) {
    if (M._rv_step() !== 0) break;
    steps++;
  }

  const res = M._rv_result();
  console.log('执行条数 =', steps);
  console.log('最终 PC  =', '0x' + (M._rv_pc() >>> 0).toString(16));
  console.log('特权级   =', ['U','S','?','M'][M._rv_priv()]);
  console.log('结果     =', res === 1 ? 'PASS' : res === -1 ? 'FAIL' : '未判定');

  // 抽查几个寄存器
  const abi = ['zero','ra','sp','gp','tp','t0','t1','t2','s0','s1'];
  for (let i = 0; i < 10; i++) {
    console.log(`  ${abi[i].padEnd(5)} = 0x${(M._rv_reg(i) >>> 0).toString(16).padStart(8,'0')}`);
  }
  process.exit(res === 1 ? 0 : 1);
}

main().catch(e => { console.error(e); process.exit(1); });

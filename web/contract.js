const fs=require("fs");
(async()=>{
  const W="/home/rvsim/web/";
  const M=await require(W+"rvsim.js")({wasmBinary:fs.readFileSync(W+"rvsim.wasm")});
  M.out=function(){};M.print=function(){};M.printErr=function(){};
  M._rv_reset();
  const elf=fs.readFileSync("/home/rvsim/build/kernel.elf");
  const p=M._malloc(elf.length); M.HEAPU8.set(elf,p); M._rv_load(p,elf.length);
  const ub=M._malloc(65536);
  let out="";
  function drain(){ let n; while((n=M._rv_uart_read(ub,65536))>0) out+=Buffer.from(M.HEAPU8.subarray(ub,ub+n)).toString(); }
  function step(n){ for(let i=0;i<n;i++){ if(M._rv_step()!==0) return; } }
  function send(cmd,maxB=300){
    out="";
    for(const ch of cmd+"\r"){ M._rv_uart_write(ch.charCodeAt(0)); step(40000); drain(); }
    for(let b=0;b<maxB;b++){ step(40000); drain(); if(out.lastIndexOf("rvsh> ")>out.length-20 && b>1) break; }
    return out;
  }

  step(800000); drain(); out="";

  // 读 manifest
  const mOut=send("manifest");
  const caps=[];
  /* manifest 现在是 {"manifest":1,"caps":N,"list":[...]}，
     逐行 JSON.parse 会被行尾逗号打断，必须整段解析。 */
  {
    const ms = mOut.indexOf('{"manifest"');
    const me = mOut.lastIndexOf("]}");
    if(ms >= 0 && me > ms){
      try{ const o = JSON.parse(mOut.slice(ms, me+2)); if(o.list) caps.push(...o.list); }catch(e){}
    }
  }

  // 按依赖顺序逐条验证：每条给出「按 manifest 的 usage 该怎么调」
  const tests=[
    ["help","help"],["manifest","manifest"],["status","status"],["ps","ps"],
    ["mem","mem"],["uptime","uptime"],["tail","tail"],
    ["log","log"],["log count","log count"],["log count spawn","log count spawn"],
    ["log last 3","log last 3"],["log find 0","log find 0"],
    ["assert =","assert nproc=3"],["assert :","assert nproc: 3"],["assert 空格","assert nproc 3"],
    ["snapshot","snapshot"],["snaps","snaps"],
    ["plan run badptr","plan run badptr"],["plans","plans"],
    ["apply","apply 1"],
    ["reset","reset"],
    ["restore","restore 1"],
    ["run badptr","run badptr"],
  ];

  const rows=[];
  for(const [name,cmd] of tests){
    const o=send(cmd);
    const bad = /unknown command|未知命令/.test(o);
    const okFalse = /\{"ok":false/.test(o);
    const which = bad ? "未识别" : (okFalse ? "识别但拒绝" : "OK");
    rows.push({name,cmd,which, sample:o.replace(/\s+/g," ").slice(-70)});
  }

  console.log("manifest 声明能力数:", caps.length);
  console.log("");
  console.log("能力调用验证:");
  let pass=0, rejected=0, unknown=0;
  for(const r of rows){
    const mark = r.which==="OK" ? "  OK  " : (r.which==="识别但拒绝" ? " 拒绝 " : " 未知 ");
    if(r.which==="OK") pass++; else if(r.which==="识别但拒绝") rejected++; else unknown++;
    console.log(mark + " " + r.name.padEnd(18) + " $ " + r.cmd);
  }
  console.log("");
  console.log(`合计: OK ${pass} / 识别但拒绝 ${rejected} / 未识别 ${unknown} / 共 ${rows.length}`);

  // 契约一致性：manifest 里每条 cap 是否都被测到
  const tested = new Set(tests.map(t=>t[0].split(" ")[0]));
  const missing = caps.filter(c=>!tested.has(c.cap)).map(c=>c.cap);
  console.log("manifest 有但未测到:", missing.length? missing.join(",") : "无");
})();
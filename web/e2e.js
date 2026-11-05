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
  function send(cmd, maxBatches=260){
    out="";
    for(const ch of cmd+"\r"){ M._rv_uart_write(ch.charCodeAt(0)); step(40000); drain(); }
    for(let b=0;b<maxBatches;b++){
      step(40000); drain();
      if(out.lastIndexOf("rvsh> ")>out.length-20 && b>1) break;
    }
    return out;
  }
  function jsonLine(s){ for(const l of s.split("\n")){ const x=l.trim(); if(x.startsWith("{")){ try{return JSON.parse(x);}catch(e){} } } return null; }

  const results=[];
  function check(name, ok, detail){ results.push({name, ok, detail}); }

  step(800000); drain(); out="";
  console.log("=== 端到端验证 ===\n");

  // 1. 发现能力
  const caps=[];
  for(const l of send("manifest").split("\n").map(x=>x.trim()).filter(x=>x.startsWith("{"))){
    let o; try{ o=JSON.parse(l); }catch(e){ continue; }
    if(o.cap) caps.push(o);
  }
  const names = caps.map(c=>c.name||c.cap);
  console.log("[1] manifest 报告能力数:", caps.length);
  check("manifest 可解析", caps.length>0, caps.length+" 项");

  // 2. 检查所需元能力是否都在（缺了就报错，不盲试）
  const need=["manifest","assert","plan","apply","snapshot","restore"];
  const missing = need.filter(n=>!names.includes(n));
  console.log("[2] 元能力检查:", missing.length? "缺少 "+missing.join(","): "全部就绪");
  check("所需元能力齐备", missing.length===0, missing.length? missing.join(",") : need.length+" 项");

  // 3. 从 manifest 里读 run 的可选程序
  const runCap = caps.find(c=>c.cap==="run");
  const opts = runCap ? runCap.args.split("=")[1].split("|") : null;
  console.log("[3] manifest 声明 run 接受:", opts? opts.join("/") : "未声明");
  check("run 参数可枚举", !!opts, opts? opts.join("/") : "无");

  const prog = opts ? opts[2] : "badptr";

  // 4. 存档
  const sn = jsonLine(send("snapshot"));
  console.log("[4] snapshot:", JSON.stringify(sn));
  check("能创建快照", !!sn && sn.snapshot>0, "id="+(sn&&sn.snapshot));

  // 5. 初始状态断言
  const a1 = jsonLine(send("assert nproc=3"));
  console.log("[5] assert nproc=3:", JSON.stringify(a1));
  check("初始状态可断言", a1 && a1.ok===true, JSON.stringify(a1));

  // 6. 预演
  const pl = jsonLine(send("plan run "+prog));
  console.log("[6] plan run "+prog+":", JSON.stringify(pl));
  check("能生成计划", !!pl && pl.plan>0, "prog="+prog);

  // 7. 执行
  const ap = jsonLine(send("apply "+pl.plan));
  console.log("[7] apply:", JSON.stringify(ap));
  check("能执行计划", ap && ap.apply===true, "plan="+pl.plan);

  // 8. 执行后断言
  const a2 = jsonLine(send("assert nproc=0"));
  const a3 = jsonLine(send("assert spawn=3"));
  console.log("[8] assert nproc=0:", JSON.stringify(a2), " assert spawn=3:", JSON.stringify(a3));
  check("执行后状态可断言", a2&&a2.ok===true&&a3&&a3.ok===true, "nproc & spawn");

  // 9. 回滚
  const rs = jsonLine(send("restore "+sn.snapshot));
  console.log("[9] restore:", JSON.stringify(rs));
  check("能回滚", rs && rs.ok===true, "id="+sn.snapshot);

  // 10. 回滚后断言
  const a4 = jsonLine(send("assert nproc=3"));
  const a5 = jsonLine(send("assert restore=1"));
  console.log("[10] assert nproc=3:", JSON.stringify(a4), " assert restore=1:", JSON.stringify(a5));
  check("回滚后状态复原", a4&&a4.ok===true, "nproc");
  check("回滚本身被记录", a5&&a5.ok===true, "restore 事件");

  console.log("\n=== 结果 ===");
  let pass=0;
  for(const r of results){ console.log((r.ok?"  PASS  ":"  FAIL  ")+r.name+"  ("+r.detail+")"); if(r.ok)pass++; }
  console.log("\n"+pass+"/"+results.length+" 通过");
})();
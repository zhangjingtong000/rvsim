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

  /* 发送一行命令，然后一直跑到提示符重新出现为止 */
  function send(cmd, maxBatches=260){
    out="";
    for(const ch of cmd+"\r"){ M._rv_uart_write(ch.charCodeAt(0)); step(40000); drain(); }
    for(let b=0;b<maxBatches;b++){
      step(40000); drain();
      if(out.lastIndexOf("rvsh> ")>out.length-20 && b>0) break;
    }
    return out;
  }

  step(800000); drain(); out="";
  console.log("=== 启动完成 ===");

  const mOut = send("manifest");
  const caps=[];
  let header=null;
  for(const l of mOut.split("\n").map(x=>x.trim()).filter(x=>x.startsWith("{"))){
    let o; try{ o=JSON.parse(l); }catch(e){ continue; }
    if(o.cap) caps.push(o); else if(o.manifest) header=o;
  }
  console.log("头部:", JSON.stringify(header), "能力数:", caps.length);

  const target = caps.find(c=>c.args && c.args.includes("|"));
  const opts = target.args.split("=")[1].split("|");
  const pick = opts[2];
  console.log("[自动构造]", target.cap+" "+pick, "(候选:", opts.join(",")+")");

  const rOut = send(target.cap+" "+pick);
  console.log("[执行输出]", JSON.stringify(rOut.replace(/\s+/g," ").slice(-120)));

  const lOut = send("log");
  const evs = lOut.split("\n").map(x=>x.trim()).filter(x=>/^\d+ \d+ (spawn|exit|ecall)/.test(x));
  const counts={};
  for(const e of evs){ const t=e.split(" ")[2]; counts[t]=(counts[t]||0)+1; }
  console.log("\n事件流条数:", evs.length, " 按类型:", JSON.stringify(counts));
  console.log("前三条:\n"+evs.slice(0,3).join("\n"));
})();
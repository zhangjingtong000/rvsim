const fs=require("fs");
(async()=>{
  const W="/home/rvsim/web/";
  const createRvsim=require(W+"rvsim.js");
  const M=await createRvsim({wasmBinary:fs.readFileSync(W+"rvsim.wasm")});
  M.out=function(){};M.print=function(){};M.printErr=function(){};
  M._rv_reset();
  const buf=fs.readFileSync(W+"images/kernel.elf");
  const p=M._malloc(buf.length); M.HEAPU8.set(buf,p); M._rv_load(p,buf.length);
  const ub=M._malloc(65536);
  let out="";
  function drain(){
    let n;
    while((n=M._rv_uart_read(ub,65536))>0){
      out+=Buffer.from(M.HEAPU8.subarray(ub,ub+n)).toString();
    }
  }
  // 1) 跑到 shell
  for(let i=0;i<800000;i++){ if(M._rv_step()!==0) break; }
  drain();
  console.log("=== 启动阶段 (" + JSON.stringify(out) + ") ===");
  console.log(out);
  // 2) 注入 help\r
  const cmd="help\r";
  for(const ch of cmd){
    M._rv_uart_write(ch.charCodeAt(0));
    for(let i=0;i<20000;i++){ if(M._rv_step()!==0) break; }
  }
  for(let i=0;i<50000;i++){ if(M._rv_step()!==0) break; }
  drain();
  console.log("=== 注入 help 后 ===");
  console.log(out.slice(200));
})();
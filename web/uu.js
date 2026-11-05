const fs=require("fs");
(async()=>{
  const W="/home/rvsim/web/";
  const createRvsim=require(W+"rvsim.js");
  const wb=fs.readFileSync(W+"rvsim.wasm");
  const M=await createRvsim({wasmBinary:wb});
  M.out=function(){};          // 屏蔽 emcc 的 putchar 那一路
  M.print=function(){};
  M.printErr=function(){};
  M._rv_reset();
  const buf=fs.readFileSync(W+"images/kernel.elf");
  const p=M._malloc(buf.length);
  M.HEAPU8.set(buf,p);
  M._rv_load(p,buf.length);
  const ub=M._malloc(8192);
  for(let i=0;i<400000;i++){ if(M._rv_step()!==0) break; }
  const n=M._rv_uart_read(ub,8192);
  process.stdout.write("=== g_uout 原始内容 ("+n+" 字节) ===\n");
  process.stdout.write(Buffer.from(M.HEAPU8.subarray(ub,ub+n)));
  process.stdout.write("\n=== END ===\n");
})();
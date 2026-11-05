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
  let total=0;
  const marks=[];
  for(let i=1;i<=3000000;i++){
    if(M._rv_step()!==0){ console.log("step 返回非零 @",i); break; }
    if(i%50000===0){
      const n=M._rv_uart_read(ub,65536);
      if(n>0){ total+=n; process.stdout.write(Buffer.from(M.HEAPU8.subarray(ub,ub+n))); }
      process.stdout.write("\n--- @"+i+" pc=0x"+(M._rv_pc()>>>0).toString(16)+" priv="+M._rv_priv()+" uart="+total+" ---\n");
    }
  }
  const n=M._rv_uart_read(ub,65536);
  if(n>0){ total+=n; process.stdout.write(Buffer.from(M.HEAPU8.subarray(ub,ub+n))); }
  console.log("\n总步数上限 3000000, UART 共", total, "字节");
})();
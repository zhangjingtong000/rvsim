const fs=require("fs");
(async()=>{
  const W="/home/rvsim/web/";
  const M=await require(W+"rvsim.js")({wasmBinary:fs.readFileSync(W+"rvsim.wasm")});
  M.out=function(){};M.print=function(){};M.printErr=function(){};
  M._rv_reset();
  const buf=fs.readFileSync(W+"images/kernel.elf");
  const p=M._malloc(buf.length); M.HEAPU8.set(buf,p); M._rv_load(p,buf.length);
  const ub=M._malloc(65536);
  for(let i=0;i<800000;i++){ if(M._rv_step()!==0) break; }

  function dump(){
    const magic=M._rv_read32(0x83FFF000)>>>0;
    if(magic!==0x52565350){ console.log("  无进程表 magic=0x"+magic.toString(16)); return; }
    const n=M._rv_read32(0x83FFF004)>>>0;
    let line="  "+n+" 进程:";
    for(let i=0;i<n;i++){
      const b=0x83FFF008+i*16;
      const pid=M._rv_read32(b)>>>0, st=M._rv_read32(b+4)>>>0,
            pc=M._rv_read32(b+8)>>>0, nr=M._rv_read32(b+12)>>>0;
      line+=` [${pid} ${["READY","RUN","EXIT"][st]} pc=0x${pc.toString(16)} n=${nr}]`;
    }
    console.log(line);
  }
  console.log("进 shell 后:"); dump();

  const cmd="run badptr\r";
  for(const ch of cmd){
    M._rv_uart_write(ch.charCodeAt(0));
    for(let i=0;i<30000;i++){ if(M._rv_step()!==0) break; }
  }
  for(let i=0;i<20000;i++){ if(M._rv_step()!==0) break; }
  console.log("run badptr 之后:"); dump();

  let out="",n;
  while((n=M._rv_uart_read(ub,65536))>0) out+=Buffer.from(M.HEAPU8.subarray(ub,ub+n)).toString();
  console.log("输出尾部:", JSON.stringify(out.slice(-80)));
})();
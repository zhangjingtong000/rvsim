const fs=require("fs");

const KEY=process.env.DS_KEY;
const MODEL="deepseek-flash";
const TRIALS=parseInt(process.env.TRIALS||"3",10);
const MAX_STEPS=parseInt(process.env.MAX_STEPS||"14",10);
const KERNEL={A:"/home/rvsim/build/kernel-min.elf", B:"/home/rvsim/build/kernel.elf"};

/* 任务刻意选择「需要历史」的问题：
   A 组没有事件流，只有当前状态（ps），根本无法回答；
   B 组有 log 与 assert，能准确回答。
   答案由实验者从事件流独立核对，不采信模型自述。 */
const TASK = "连续运行 badptr 演示 3 次。然后回答三个问题：\n"
 + "1. 三次演示总共产生了多少条 exit 事件？\n"
 + "2. 三次演示总共有多少次地址越界写入被拒绝？\n"
 + "3. 最后一次演示中，哪个进程（PID）最先退出？\n\n"
 + "最终答案必须单独一行，格式为：ANSWER: exit=<数字> oob=<数字> first=<PID>";

const TRUTH = { exit: 9, oob: 18, first: 0 };

const SYS="你是终端操作员。规则：\n"
 + "1. 每次回复只给一条 shell 命令，不要解释，不要代码块。\n"
 + "2. 命令会以回车发送，随后你会看到新的终端输出。\n"
 + "3. 全部完成并确认后，单独一行输出 ANSWER: ...\n"
 + "不要复述任务，直接行动。";

function taskBlock(term){
  const tail = term.slice(-2000);
  return "任务：\n"+TASK+"\n\n当前终端：\n----------------\n"+tail+"\n----------------\n\n下一条命令（或最终 ANSWER 行）：";
}

async function llm(messages){
  const r=await fetch("https://api.deepseek.com/chat/completions",{
    method:"POST",
    headers:{"Content-Type":"application/json","Authorization":"Bearer "+KEY},
    body:JSON.stringify({model:MODEL,messages,temperature:0,max_tokens:4000})
  });
  if(!r.ok){ throw new Error("API "+r.status+" "+await r.text()); }
  const j=await r.json();
  const ch=j.choices[0];
  return { text:(ch.message.content||"").trim(), tokens:j.usage?j.usage.total_tokens:0,
           finish:ch.finish_reason };
}

async function runTrial(group){
  const W="/home/rvsim/web/";
  const M=await require(W+"rvsim.js")({wasmBinary:fs.readFileSync(W+"rvsim.wasm")});
  M.out=function(){};M.print=function(){};M.printErr=function(){};
  M._rv_reset();
  const elf=fs.readFileSync(KERNEL[group]);
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
  let term=send("help");
  const messages=[{role:"system",content:SYS},{role:"user",content:taskBlock(term)}];
  let steps=0, tokens=0, answer=null, err=null;
  const trace=[];

  while(steps<MAX_STEPS){
    let reply;
    try{ reply=await llm(messages); }catch(e){ err=String(e); break; }
    tokens+=reply.tokens; steps++;
    let text=reply.text.replace(/```[a-z]*\n?/gi,"").trim();
    if(!text){ err="空回复 finish="+reply.finish; break; }

    const mAns=text.match(/ANSWER:\s*exit=(-?\d+)\s+oob=(-?\d+)\s+first=(-?\d+)/i);
    if(mAns){ answer={exit:+mAns[1],oob:+mAns[2],first:+mAns[3]}; break; }

    const cmd=text.split("\n")[0].trim();
    const o=send(cmd);
    term+=o;
    trace.push({cmd, out:o.replace(/\s+/g," ").slice(-80)});
    messages.push({role:"assistant",content:cmd});
    messages.push({role:"user",content:taskBlock(term)});
  }

  const correct = !!answer && answer.exit===TRUTH.exit
    && answer.oob===TRUTH.oob && answer.first===TRUTH.first;

  return {group,steps,tokens,answer,correct,err,trace};
}

(async()=>{
  if(!KEY){ console.error("缺少 DS_KEY"); process.exit(2); }
  const all={A:[],B:[]};
  const ONLY=process.env.ONLY||"AB";
  console.log("真值:", JSON.stringify(TRUTH));
  for(const g of ["A","B"].filter(x=>ONLY.includes(x))){
    console.log("\n=== "+g+" 组（"+(g==="A"?"无事件流：只有 ps 当前状态":"事件流 + assert")+"）===");
    for(let t=1;t<=TRIALS;t++){
      const r=await runTrial(g);
      all[g].push(r);
      console.log(`  trial ${t}: steps=${r.steps} tokens=${r.tokens} answer=${JSON.stringify(r.answer)} `
        + `-> ${r.correct?"正确":"错误"}` + (r.err?" err="+r.err:""));
      for(const x of r.trace) console.log("      $ "+x.cmd+"   => "+x.out.slice(0,55));
      await new Promise(z=>setTimeout(z,500));
    }
  }

  const avg=(a,f)=>a.length? (a.reduce((s,x)=>s+f(x),0)/a.length) : 0;
  console.log("\n=== 汇总 ===");
  for(const g of ["A","B"].filter(x=>ONLY.includes(x))){
    const a=all[g]; if(!a.length) continue;
    console.log(`${g} 组: 正确 ${a.filter(x=>x.correct).length}/${a.length}`
      + `  平均步数 ${avg(a,x=>x.steps).toFixed(1)}`
      + `  平均 token ${Math.round(avg(a,x=>x.tokens))}`);
  }
})();
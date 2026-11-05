const fs=require("fs");

const KEY=process.env.DS_KEY;
const BASE=process.env.BASE_URL||"https://api.deepseek.com";
const MODEL=process.env.MODEL||"deepseek-flash";
const MAX_STEPS=parseInt(process.env.MAX_STEPS||"26",10);

/* 能力巡检：不看 A/B 差异，只看「AI 能不能把 17 个能力都用一遍，
   以及哪些用起来别扭」。这是最贴近「AI 完全使用这个 OS」的检验。 */
const TASK =
 "这个系统通过 manifest 声明了它的全部能力。请：\n"
 + "1. 先读 manifest，列出所有能力\n"
 + "2. 逐个试用每一个能力（每个至少调用一次）\n"
 + "3. 最后报告：哪些用起来顺畅？哪些描述不清、参数难懂、或输出难以理解？\n\n"
 + "报告必须包含两行：\n"
 + "USED: <用过的能力数>/<总数>\n"
 + "ISSUES: <之后每行一个，格式 name: 具体问题；没有问题就写 none>";

const SYS="你是终端操作员。规则：\n"
 + "1. 每次回复只给一条 shell 命令，不要解释，不要代码块。\n"
 + "2. 命令会以回车发送，随后你会看到新的终端输出。\n"
 + "3. 全部试完后，按要求的格式输出报告。\n"
 + "不要复述任务，直接行动。";

function taskBlock(term){
  return "任务：\n"+TASK+"\n\n当前终端：\n----------------\n"+term.slice(-4500)
   + "\n----------------\n\n下一条命令（或最终报告）：";
}

async function llm(messages){
  const r=await fetch(BASE+"/chat/completions",{
    method:"POST",
    headers:{"Content-Type":"application/json","Authorization":"Bearer "+KEY},
    body:JSON.stringify({model:MODEL,messages,temperature:0,max_tokens:4000})
  });
  if(!r.ok) throw new Error("API "+r.status+" "+await r.text());
  const j=await r.json(); const ch=j.choices[0];
  return { text:(ch.message.content||"").trim(), tokens:j.usage?j.usage.total_tokens:0, finish:ch.finish_reason };
}

(async()=>{
  if(!KEY){ console.error("缺少 DS_KEY"); process.exit(2); }
  const W="/home/rvsim/web/";
  const M=await require(W+"rvsim.js")({wasmBinary:fs.readFileSync(W+"rvsim.wasm")});
  M.out=function(){};M.print=function(){};M.printErr=function(){};
  M._rv_reset();
  const elf=fs.readFileSync("/home/rvsim/build/kernel.elf");
  const p=M._malloc(elf.length); M.HEAPU8.set(elf,p); M._rv_load(p,elf.length);
  const ub=M._malloc(65536);
  let out="";
  /* 必须用 StringDecoder：Buffer.toString() 每次独立解码，
     跨块的 UTF-8 多字节字符会被切断成乱码（◆◆），
     导致模型看到的中文 desc 是坏的。 */
  const { StringDecoder } = require("string_decoder");
  const sd = new StringDecoder("utf8");
  function drain(){
    let n;
    while((n=M._rv_uart_read(ub,65536))>0) out += sd.write(Buffer.from(M.HEAPU8.subarray(ub,ub+n)));
  }
  function step(n){ for(let i=0;i<n;i++){ if(M._rv_step()!==0) return; } }
  function send(cmd,maxB=300){
    out="";
    for(const ch of cmd+"\r"){ M._rv_uart_write(ch.charCodeAt(0)); step(40000); drain(); }
    for(let b=0;b<maxB;b++){ step(40000); drain(); if(out.lastIndexOf("rvsh> ")>out.length-20 && b>1) break; }
    return out;
  }

  step(800000); drain(); out="";
  let term=send("help");

  // 先拿到能力清单，供核对
  const trace0=[];
  const mOut=send("manifest");
  const caps=[];
  for(const l of mOut.split("\n").map(x=>x.trim()).filter(x=>x.startsWith("{"))){
    let o; try{ o=JSON.parse(l); }catch(e){ continue; }
    if(o.cap) caps.push(o);
  }
  term += mOut;
  /* 这次调用也要计入轨迹，否则统计会把 manifest 误报为「未使用」 */
  trace0.push({cmd:"manifest", out:"(开局读取)"});
  console.log("manifest 声明:", caps.length, "个能力");
  console.log(caps.map(c=>c.cap).join(" "), "\n");

  const messages=[{role:"system",content:SYS},{role:"user",content:taskBlock(term)}];
  let steps=0, tokens=0, report=null, err=null;
  const trace=trace0;

  while(steps<MAX_STEPS){
    let reply;
    try{ reply=await llm(messages); }catch(e){ err=String(e); break; }
    tokens+=reply.tokens; steps++;
    let text=reply.text.replace(/```[a-z]*\n?/gi,"").trim();
    if(!text){ err="空回复 finish="+reply.finish; break; }

    if(/USED:\s*\d+\s*\/\s*\d+/i.test(text)){
      report=text; break;
    }

    const cmd=text.split("\n")[0].trim();
    const o=send(cmd);
    term+=o;
    trace.push({cmd, out:o.replace(/\s+/g," ").slice(-60)});
    messages.push({role:"assistant",content:cmd});
    messages.push({role:"user",content:taskBlock(term)});
  }

  // 客观统计：trace 里实际用过哪些能力
  const used=new Set();
  for(const t of trace){
    const first=t.cmd.trim().split(/\s+/)[0];
    if(caps.some(c=>c.cap===first)) used.add(first);
  }
  const missing=caps.map(c=>c.cap).filter(c=>!used.has(c));

  console.log("=== 轨迹 ===");
  for(const t of trace) console.log("  $ "+t.cmd.padEnd(26)+" => "+t.out.slice(0,50));

  console.log("\n=== 客观统计 ===");
  console.log("实际用过:", used.size, "/", caps.length);
  console.log("用过:", [...used].join(" "));
  console.log("未用:", missing.length? missing.join(" ") : "无");
  console.log("步数:", steps, " token:", tokens);

  console.log("\n=== AI 自述报告 ===");
  console.log(report || "（未给出报告）" + (err? " err="+err : ""));
})();
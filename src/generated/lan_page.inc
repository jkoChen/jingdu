R"JDHTML(<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>静读 · 文件服务</title>
<style>
:root{color-scheme:light;--paper:#f8f6ef;--ink:#23372e;--line:#c6cbbf;--green:#295d47}*{box-sizing:border-box}
body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.55 system-ui,sans-serif}
main,header>div{max-width:850px;margin:auto;padding:0 24px}header{border-bottom:1px solid var(--line)}header>div{display:flex;align-items:center;justify-content:space-between;height:64px}
.brand,h1,h2{font-family:Georgia,"Songti SC",serif}.brand{font-size:25px;font-weight:700;letter-spacing:.13em}
h1{font-size:clamp(28px,5vw,40px);margin:0 0 12px}h2{font-size:22px;margin:0 0 6px}p{margin:4px 0 14px}section{margin:32px 0}
button,input{font:inherit;color:inherit}button,.target{cursor:pointer}button{background:transparent;border:1px solid var(--line);border-radius:5px;min-height:44px;padding:6px 13px}
button:disabled{opacity:.48;cursor:default}button:focus-visible,input:focus-visible,.target:focus-within{outline:3px solid #708447;outline-offset:2px}
.primary{background:var(--green);border-color:var(--green);color:white}.status,.hint,small{color:#4c6255}
#auth{max-width:440px;margin:12vh auto}#pin{font:24px ui-monospace,monospace;letter-spacing:.25em;width:9em;max-width:100%;padding:10px;border:1px solid #869688;border-radius:5px;background:#fff}
.pair{display:flex;flex-wrap:wrap;gap:10px}.targets{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
.target{position:relative;display:flex;justify-content:space-between;align-items:center;background:#fffef8;border:1px solid var(--line);border-radius:7px;padding:16px;color:var(--ink)}
.target b{display:block;font-size:18px}.target small{display:block}.target i{font:24px Georgia;color:var(--green)}.target input{position:absolute;inset:0;opacity:0;width:100%;height:100%;cursor:pointer}
.bar,.file,.actions,.job{display:flex;gap:8px;align-items:center}.bar,.file,.job{justify-content:space-between}.bar{flex-wrap:wrap}.file,.job{border-top:1px solid var(--line);padding:7px 0}.name{flex:1;min-width:0;overflow-wrap:anywhere;text-align:left;border:0}.actions button{min-width:44px;border:0;padding:4px 7px;font-size:14px}
.job>div{min-width:0;flex:1;overflow-wrap:anywhere}progress{display:block;width:100%;height:6px;accent-color:var(--green)}
ul{padding:0;list-style:none}#cwd{overflow-wrap:anywhere;font:15px ui-monospace,monospace}.error{color:#963627}[hidden]{display:none!important}
@media(max-width:560px){main,header>div{padding:0 16px}.targets{grid-template-columns:1fr;gap:6px}.target{padding:8px 14px}.target span{display:flex;align-items:baseline;gap:12px}.file{gap:2px}.actions{gap:0}section{margin:24px 0}}

#paging{display:grid;grid-template-columns:1fr auto 1fr;align-items:center;gap:10px}
#prev{grid-column:1;grid-row:1;justify-self:start}#more{grid-column:3;grid-row:1;justify-self:end}
#pageNumber{grid-column:2;grid-row:1}#editAction{padding:16px;border:1px solid var(--line);margin:14px 0}
#editAction input{display:block;width:100%;min-height:44px;margin:8px 0;padding:6px}
#editAction p,#editAction label{overflow-wrap:anywhere}.target,.file button{min-height:44px}
@media(max-width:400px){.target span{flex-wrap:wrap}.bar .actions{flex-wrap:wrap}.pair>*{max-width:100%}}
</style>
<header><div><span class="brand">静读</span><span class="status" id="state">等待配对</span></div></header>
<main><form id="auth"><h1>连接阅读器</h1><p>输入阅读器「文件服务」页面上的六位配对码。</p><label for="pin">配对码</label><div class="pair"><input id="pin" inputmode="numeric" pattern="[0-9]{6}" maxlength="6" autocomplete="off" required><button class="primary">连接设备</button></div></form>
<p role="status" id="notice"></p>
<div id="panel" hidden><section><h1>传到阅读器</h1><div class="targets" id="targets"></div></section>
<section id="jobs" hidden><h2>传输队列</h2><ul id="queue"></ul></section>
<section><h2>存储卡</h2><div class="bar"><span id="cwd"></span><div class="actions"><button id="up">上一级</button><button id="mk">新建目录</button><button id="upload">上传至此</button><input type="file" multiple id="any" hidden></div></div>
<form id="editAction" hidden><h2 id="editTitle"></h2><p id="editReason"></p><label id="editLabel">名称<input id="editName" autocomplete="off"></label><div class="actions"><button type="button" id="editCancel">取消</button><button class="primary" id="editSubmit">保存</button></div></form><ul id="files"></ul><div id="paging"><span id="pageNumber"></span><button id="prev">上一页</button><button id="more">下一页</button></div></section></div></main>
<script>
'use strict';
const el=id=>document.getElementById(id), destinations=[
 ['书籍','TXT · EPUB','/books','.txt,.epub'],['字体','TTF · OTF','/fonts','.ttf,.otf'],['英语练习','JDC · JSON','/courses/english','.jdc,.json']];
let token='',cwd='/',skip=0,running=false;const jobs=[];
const offsets=new Map();
const pathAt=(dir,name)=>(dir==='/'?'':dir)+'/'+name;
function say(text){el('notice').textContent=text}
function pairAgain(){token='';el('auth').hidden=false;el('panel').hidden=true;el('state').textContent='等待配对';el('pin').focus()}
async function api(op,path,method='GET',body,extra=''){
 const r=await fetch('/'+op+'?p='+encodeURIComponent(path)+extra,{method,body,headers:{'X-Jingdu-Token':token}});
 if(!r.ok){if(r.status===403&&op!=='pair')pairAgain();throw Error(await r.text())}return r
}
function button(label,fn,cls=''){const b=document.createElement('button');b.textContent=label;b.className=cls;b.onclick=()=>Promise.resolve().then(fn).catch(e=>say(e.message));return b}
for(const [name,type,dir,accept] of destinations){
 const label=document.createElement('label');label.className='target';
 label.innerHTML='<span><b></b><small></small></span><i aria-hidden="true">＋</i>';
 label.querySelector('b').textContent=name;label.querySelector('small').textContent=type;
 const input=document.createElement('input');input.type='file';input.multiple=true;input.accept=accept;input.setAttribute('aria-label','上传'+name);
 label.ondragover=e=>e.preventDefault();label.ondrop=e=>{e.preventDefault();enqueue(e.dataTransfer.files,dir,accept)};
 input.onchange=()=>{enqueue(input.files,dir,accept);input.value=''};label.append(input);el('targets').append(label)
}
function enqueue(files,dir,accept){for(const file of files)jobs.push({file,dir,accept,state:'等候',pc:0});showQueue();pump()}
async function validate(j){
 const name=(j.name||j.file.name).toLowerCase(),ext=name.slice(name.lastIndexOf('.'));
 if(j.file.size>32*1024*1024)throw Error('单个文件限 32 MiB');
 if(j.accept&&!j.accept.split(',').includes(ext))throw Error('文件类型不匹配');
 if(j.dir==='/courses/english'){
  if(ext==='.json'){if(j.file.size>24576)throw Error('JSON 限 24 KiB，大课包请转换为 JDC');const d=JSON.parse(await j.file.text());if(!Array.isArray(d.lessons)||!d.lessons.length||!d.lessons.every(x=>Array.isArray(x.passages)&&x.passages.length))throw Error('课包应包含 lessons 和 passages')}
  if(ext==='.jdc'&&await j.file.slice(0,4).text()!=='JDC2')throw Error('无法识别 JDC2 课包')
 }
}
function send(j){return new Promise((resolve,reject)=>{
 const x=new XMLHttpRequest();x.open('PUT','/put?p='+encodeURIComponent(pathAt(j.dir,j.name||j.file.name)));x.setRequestHeader('X-Jingdu-Token',token);
 x.upload.onprogress=e=>{if(e.lengthComputable){j.pc=Math.round(100*e.loaded/e.total);j.state=j.pc===100?'等待设备确认':'正在传输';showQueue()}};
 j.abort=()=>x.abort();x.onabort=()=>reject(Error('已取消'));x.onerror=()=>reject(Error('连接中断'));x.onload=()=>{if(x.status===403)pairAgain();x.status===200?resolve():reject(Error(x.responseText||'传输失败'))};x.send(j.file)
})}
async function pump(){
 if(running||!token)return;running=true;
 try{for(let j of jobs){if(!token)break;if(j.state!=='等候')continue;
  try{await validate(j);j.state='正在传输';showQueue();await send(j);j.state='已存入';j.pc=100}
  catch(e){j.state=e.message==='已取消'?'已取消':'失败';j.reason=e.message}showQueue()}
 }finally{running=false;if(token)await list().catch(e=>say(e.message))}
}
function showQueue(){el('jobs').hidden=!jobs.length;el('queue').replaceChildren();
 for(const j of jobs){const li=document.createElement('li'),info=document.createElement('div'),detail=document.createElement('small');li.className='job';info.textContent=j.name||j.file.name;detail.textContent=j.state+(j.state==='失败'?' · '+j.reason:' · '+j.dir);info.append(document.createElement('br'),detail);li.append(info);
 if(j.state==='正在传输'||j.state==='等待设备确认'){const p=document.createElement('progress');p.max=100;p.value=j.pc;info.append(p)}
 if(j.state==='等候')li.append(button('取消',()=>{j.state='已取消';showQueue()}));
 if((j.state==='正在传输'||j.state==='等待设备确认')&&j.abort)li.append(button('取消',()=>j.abort()));
 if(j.state==='已存入')li.append(button('查看目录',()=>{return locateUpload(j)}));
 if(j.state==='失败'&&j.reason.includes('同名'))li.append(button('换名重传',()=>edit('改名',j.name||j.file.name,async n=>{if(!n||n===(j.name||j.file.name))throw Error('请输入新的文件名');if(j.accept && !j.accept.split(',').includes(n.toLowerCase().slice(n.lastIndexOf('.'))))throw Error('请保留支持的文件扩展名');j.name=n;j.pc=0;j.state='等候';await pump()})));
 if(j.state==='失败'||j.state==='已取消')li.append(button('重试',()=>{j.state='等候';j.pc=0;pump()}));el('queue').append(li)}
}
async function locateUpload(job){
 const ticket=++listing,path=job.dir,name=job.name||job.file.name;
 let page=0;
 while(true){const d=await(await api('list',path,'GET',undefined,'&skip='+page)).json();
 if(ticket!==listing)return;
 if(d.files.some(f=>f.name===name)||!d.more)break;page+=16}
 cwd=path;skip=page;await list();
 if(cwd===path && skip===page)[...el('files').querySelectorAll('.name')].find(b=>b.textContent===name)?.focus()
}
let listing=0;
async function list(){const ticket=++listing,path=cwd,page=skip;const d=await(await api('list',path,'GET',undefined,'&skip='+page)).json();if(ticket!==listing)return;
 el('cwd').textContent=path==='/'?'TF 卡 /':path;el('files').replaceChildren();el('up').disabled=path==='/';el('prev').hidden=!page;el('more').hidden=!d.more;
 if(!d.files.length&&page>0){skip=Math.max(0,page-16);return list()}
 el('pageNumber').textContent=page||d.more?'第 '+(1+Math.floor(page/16))+' 页':'';el('paging').hidden=!page&&!d.more;
 if(!d.files.length){const empty=document.createElement('li');empty.textContent='此目录为空';el('files').append(empty)}
 for(const f of d.files){const li=document.createElement('li'),p=pathAt(path,f.name);li.className='file';
 li.append(button(f.name+(f.dir?' /':''),async()=>{if(f.dir){offsets.set(cwd,skip);cwd=p;skip=0;return list()}const url=URL.createObjectURL(await(await api('get',p)).blob());const a=document.createElement('a');a.href=url;a.download=f.name;a.click();setTimeout(()=>URL.revokeObjectURL(url),5000)},'name'));
 const ops=document.createElement('div');ops.className='actions';ops.append(button('改名',()=>edit('改名',f.name,async n=>{if(n!==f.name)await api('move',p,'POST',undefined,'&to='+encodeURIComponent(pathAt(path,n)))})),button('删除',()=>edit('删除',f.name,()=>api('delete',p,'POST'))));li.append(ops);el('files').append(li)}
}
el('auth').onsubmit=async e=>{e.preventDefault();try{token=(await(await api('pair','/','POST',el('pin').value)).json()).token;el('pin').value='';el('auth').hidden=true;el('panel').hidden=false;el('state').textContent='阅读器已连接';say('');await list();pump()}catch(e){say(e.message)}};
el('up').onclick=()=>{cwd=cwd.slice(0,cwd.lastIndexOf('/'))||'/';skip=offsets.get(cwd)||0;list().catch(e=>say(e.message))};
for(const [id,n] of [['prev',-16],['more',16]])el(id).onclick=()=>{skip=Math.max(0,skip+n);list().catch(e=>say(e.message))};
function edit(action,name,work){
 const form=el('editAction'),input=el('editName'),submit=el('editSubmit');
 form.hidden=false;el('editTitle').textContent=action==='改名'?'重命名':action;
 el('editReason').textContent=action==='删除'?'将从 TF 卡删除「'+name+'」。此操作无法撤销。':name||cwd;
 el('editLabel').hidden=action==='删除';input.required=action!=='删除';input.value=action==='改名'?name:'';
 submit.textContent=action==='删除'?'确认删除':'保存';const origin=document.activeElement;
 function close(){form.hidden=true;origin?.focus()}
 el('editCancel').onclick=close;form.onsubmit=async e=>{e.preventDefault();submit.disabled=true;
 try{await work(input.value.trim());close();say(action==='删除'?'已删除':'已保存');await list()}
 catch(e){say(e.message)}finally{submit.disabled=false}};
 (action==='删除'?el('editCancel'):input).focus()
}
el('mk').onclick=()=>edit('新建目录','',n=>api('mkdir',pathAt(cwd,n),'POST'));
el('upload').onclick=()=>el('any').click();el('any').onchange=()=>{enqueue(el('any').files,cwd,'');el('any').value=''};
</script></html>
)JDHTML"

// Unpack the deduplicated native bitmap assets before binding touch targets.
function resolvePixels(o) {
 for (const [k,v] of Object.entries(o)) {
  if (typeof v==='string' && v.length===17 && v[0]==='@' && pixels[v.slice(1)]) o[k]=pixels[v.slice(1)];
  else if (v && typeof v==='object') resolvePixels(v);
 }
}
resolvePixels(model);
const $=s=>document.querySelector(s), all=model.frames, frames=Object.fromEntries(all.map(f=>[f.k,f])),ids=model.names,groups=model.groups,rows=groups.flatMap(g=>g[1]);
const routeBy=Object.fromEntries(rows.map(x=>[x[0],x[2]]));
const el=(tag,txt,cls)=>{let e=document.createElement(tag);if(txt!==undefined)e.textContent=txt;if(cls)e.className=cls;return e},bt=(txt,act)=>{let b=el('button',txt);b.onclick=act;return b};
let current='book',tab='逐屏评审',historyKeys=[],foot=true,mark=false,chapter=false,fromJump=false,activeWifi=false,hasCard=true,wifiPwd='',pwShown=false,keys=0,lastAwake='home',scale='原生像素',mm=46.96,hot=false,menuFilter='',blinkTimer,serial=0,courseGroup='catalog',afterBook='book',readPos=0,learnReply='correct',allStates=false,touchedLayout=false,touchedTools=false,typed=false,rangePage=10,lockMin=10,light=4,grip=0,layoutVals=[20,7,16,1],trackSettings=false;
let anchor=model.reading.initial, activeAnchor=false, priorAnchor=anchor, pageTrail=[],savedMarks=new Set();
function targetByte(n){anchor=n;activeAnchor=true;mark=savedMarks.has(n)}
function turnPage(next){const p=model.reading.pages[anchor];if(!p)return;let n;if(next){n=foot?p.nextOn:p.nextOff;if(n!==anchor)pageTrail.push(anchor)}else if(pageTrail.length)n=pageTrail.pop();else{n=Object.keys(model.reading.pages).map(Number).filter(v=>v<anchor&&model.reading.pages[v][foot?'nextOn':'nextOff']===anchor).pop()??anchor}targetByte(n);readPos=1;goto(fbook(),false)}
let bookmarkPage=0, selectedBookmark=model.reading.initial, liveBookmarks=false, detailFile='', openedFile='';
function resetReviewList(k){
  detailFile=openedFile='';liveBookmarks=false;bookmarkPage=0;selectedBookmark=model.reading.initial;
  if(['marks','marks2','marks3'].includes(k)){savedMarks=new Set(model.markSample);bookmarkPage=['marks','marks2','marks3'].indexOf(k)}
  else if(k==='marksOne'||k==='markDetail')savedMarks=new Set([model.reading.initial]);
  else if(k==='marks0')savedMarks.clear();
}
function bookmarkView(){
 const count=savedMarks.size;
 bookmarkPage=Math.max(0,Math.min(bookmarkPage,Math.ceil(count/6)-1));
 if(!count)return 'marks0';
 if(count<=6)return 'marksOne';
 return ['marks','marks2','marks3'][Math.min(bookmarkPage,2)];
}
function openBookmarks(){bookmarkPage=0;liveBookmarks=true;goto(bookmarkView())}
function turnBookmarks(delta){bookmarkPage+=delta;liveBookmarks=true;goto(bookmarkView(),false)}
function bookmarkRows(){return Array.from(savedMarks).slice(bookmarkPage*6,bookmarkPage*6+6)}
function bookmarkHits(){
 const h=[[0,0,40,36]],n=bookmarkRows().length;
 if(!n)return h.concat([[16,364,208,44]]);
 for(let i=0;i<n;i++)h.push([0,52+i*47,240,44]);
 if(bookmarkPage>0)h.push([0,380,80,36]);
 if((bookmarkPage+1)*6<savedMarks.size)h.push([160,380,80,36]);
 return h;
}
const pictureCache=new Map();
function picture(url){
 if(!pictureCache.has(url))pictureCache.set(url,new Promise((resolve,reject)=>{const im=new Image;im.onload=()=>resolve(im);im.onerror=reject;im.src=url}));
 return pictureCache.get(url);
}
async function drawBookmarkList(ctx,tag){
 if(!savedMarks.size)return;
 const rows=await Promise.all(bookmarkRows().map(n=>picture(model.reading.pages[n].row)));
 const controls=await picture(frames.marks2.img);
 if(tag!==serial)return;
 ctx.fillStyle='white';ctx.fillRect(0,35,240,381);
 rows.forEach((row,i)=>ctx.drawImage(row,0,52+i*47));
 const pages=Math.ceil(savedMarks.size/6);
 if(pages>1){
   const s=(bookmarkPage+1)+' / '+pages;
   bitmapText(ctx,120-Math.floor(width(s,12)/2),394,s);
   if(bookmarkPage)ctx.drawImage(controls,0,380,80,36,0,380,80,36);
   if(bookmarkPage+1<pages)ctx.drawImage(controls,160,380,80,36,160,380,80,36)
 }
}
function listDirectory(path,picking=false){
 return Object.keys(model.lists).find(k=>ids[frames[k].v]==='Files'&&model.lists[k].dir===path&&model.lists[k].page===0&&model.lists[k].pick===picking);
}
function nextSheet(k,delta){const f=model.lists[k];if(!f)return;
 const base=k.replace(/[23]$/,'');const n=f.page+delta;
 if(n>=0&&n<f.total)goto(base+(n?String(n+1):''),false)
}
function shelfAndFileAction(k,x,y){
 const list=model.lists[k];if(!list)return k==='shelf0'?pack('从 TF 卡选书',()=>goto('pick')):null;
 if(y===380)return pack(x?'下一页':'上一页',()=>nextSheet(k,x?1:-1));
 if(ids[frames[k].v]==='Shelf'&&y>=335)return pack('添加书籍',()=>goto('pick'));
 const shelf=ids[frames[k].v]==='Shelf';
 const row=list.entries?.[Math.round((y-(shelf?52:73))/(shelf?47:45))];
 if(!row)return null;const name=row.path.split('/').pop();
 if(row.folder){const dest=listDirectory(row.path);return dest?pack(name,()=>goto(dest)):null}
 return pack(name,()=>{detailFile=row.path;if(shelf){openedFile=row.path;goto('book')}else goto(/\.(txt|epub)$/i.test(row.path)?'file':'otherFile')})
}
function drawBrightness(ctx){
 ctx.fillStyle='white';ctx.fillRect(0,44,240,60);ctx.fillStyle='black';
 const runs=model.lightRuns[light];for(let i=0;i<runs.length;i+=3)ctx.fillRect(runs[i],runs[i+1],runs[i+2],1)
}
function setBrightness(level){
 light=Math.max(0,Math.min(10,level));
 const canvas=$('.screen canvas');if(canvas)drawBrightness(canvas.getContext('2d'));
 for(const b of document.querySelectorAll('[data-light]')){
  const val=b.dataset.light;
  if(val==='dimmer'||val==='brighter')b.setAttribute('aria-disabled',String(val==='dimmer'?light===0:light===10));
  else b.setAttribute('aria-pressed',String(Number(val)===light));
 }
}
function lightTarget(x){return x===0?'dimmer':x===203?'brighter':(x-38)/15}
const studyFamilies = {};
for (const [k,s] of Object.entries(model.sheets)) (studyFamilies[s.family] ||= [])[s.page]=k;
let gradedAt=-1000, atLessonEnd=false;
let quizPick=-1, previousQuiz='quiz', clockValues=[2026,11,11,12,0], savedClock=null, clockFocus=0, clockDirty=false;
let selectedNetwork='', servicePage='svcOff', cardPresent=true, translatedPath='';
function studyAction(f,x,y){
 const s=model.sheets[f.k], pages=studyFamilies[s.family], label=s.buttons?.[x+','+y];
 if(!label && y===380 && s.total>1){const d=x===0?-1:1;return pack(d<0?'上一页':'下一页',()=>goto(pages[s.page+d],false))}
 if(!label && y===291)return pack('逐句阅读',()=>goto('chunks'));
 if(!label)return null;
 return pack(label,()=>{
   if(label.startsWith('选项 ')){
     const n=label.charCodeAt(3)-65,q=model.quizRoutes[s.family];if(q?.detail?.[n]){goto(q.detail[n]);return}
     quizPick=n;learnReply=quizPick===1?'correct':'wrong';const group=studyFamilies[q?.selected||'quizPromptChosen'];goto(group[group.length-1],false);return}
   switch(label){
    case '核对答案':{const q=model.quizRoutes[s.family];gradedAt=performance.now();goto(q?(quizPick===q.correct?q.good:q.bad):learnReply);break}
    case '取消': back();break;
    case '选这一项':{const route=model.optionRoutes[s.family];if(route){quizPick=route.index;const q=model.quizRoutes[route.quiz];if(model.quizRoutes[historyKeys.at(-1)])historyKeys.pop();const pages=studyFamilies[q.selected];goto(pages?pages[pages.length-1]:q.selected,false);break}}quizPick=3;learnReply='wrong';if(historyKeys.at(-1)?.startsWith('quiz'))historyKeys.pop();goto('quizLongSelected',false);break;
    case '重读':if(model.feedbackRoutes[s.family]){goto(model.feedbackRoutes[s.family].read,false);break}goto(atLessonEnd?'passageNext':'passage',false);break;
    case '继续':if(performance.now()-gradedAt<280)return;if(model.feedbackRoutes[s.family]){goto(model.feedbackRoutes[s.family].catalog,false);break}goto(atLessonEnd?'catalog':'passageNext',false);break;
    case '查词':goto('words');break;
    case '篇目':goto(model.readRoutes[s.family]?.catalog||(f.k.startsWith('qaRead')?'qaCatalog':'catalog'));break;
    case '理解题':atLessonEnd=s.family==='passageNext';quizPick=-1;goto(model.readRoutes[s.family]?.quiz||'quiz');break;
    case '上一句':goto(s.family==='chunks3'?'chunks2':'chunks',false);break;
    case '下一句':goto(s.family==='chunks'?'chunks2':'chunks3',false);break;
    case '收藏词义':goto('vocab');break;
    case '再练':case '记住了':goto('vocab',false);break;
   }
 })
}
function clockAction(x,y){
 const starts=[0,62,106,150,194],labels=['年','月','日','时','分'];
 if(y===215){const n=starts.indexOf(x);return pack('选择'+labels[n],()=>{clockFocus=n;clockDirty=true;render()})}
 if(y===279)return pack(x<100?'减一':'加一',()=>{
   let [year,month]=clockValues;
   const low=[2020,1,1,0,0],high=[2099,12,new Date(year,month,0).getDate(),23,59],j=clockFocus;
   const width=high[j]-low[j]+1;
   clockValues[j]=low[j]+(clockValues[j]-low[j]+width+(x<100?-1:1))%width;
   clockValues[2]=Math.min(clockValues[2],new Date(clockValues[0],clockValues[1],0).getDate());
   clockDirty=true;render()
 });
 return pack('保存时间',()=>{savedClock=[...clockValues];back()})
}
function drawClockFields(ctx){
 const starts=[8,65,109,153,197],widths=[54,41,41,41,38];
 ctx.fillStyle='white';ctx.fillRect(0,214,240,47);
 starts.forEach((x,i)=>{let w=widths[i],s=String(clockValues[i]);
   ctx.strokeStyle='black';ctx.lineWidth=1;ctx.strokeRect(x+.5,215.5,w-1,40);
   bitmapText(ctx,x+Math.floor((w-width(s,15))/2),228,s,15);
   if(i===clockFocus){ctx.fillStyle='black';ctx.fillRect(x,257,w,2)}
 })
}
function runScan(){
 goto('wifiScan',false);blinkTimer=setTimeout(()=>{if(current==='wifiScan')goto('wifi',false)},950)
}
function startPrep(){
 const stages=['fontOpen','fontPrep','fontDraw','fontGlyphs'];
 function show(i){
   if(i===stages.length){while(historyKeys.length&&['Fonts','FontPrep','Layout'].includes(ids[frames[historyKeys.at(-1)].v]))historyKeys.pop();goto(flayout(),false);return}
   goto(stages[i],i===0);blinkTimer=setTimeout(()=>{if(current===stages[i])show(i+1)},800)
 }show(0)
}
function drawMaterialState(ctx,v,f){
 if(v==='ClockEdit'&&clockDirty)drawClockFields(ctx);
 if(v==='Clock'&&savedClock){
   const [y,m,d,h,n]=savedClock;
   ctx.fillStyle='white';ctx.fillRect(14,54,211,89);
   bitmapText(ctx,16,62,y+'-'+String(m).padStart(2,'0')+'-'+String(d).padStart(2,'0'),15);
   bitmapText(ctx,16,115,String(h).padStart(2,'0')+':'+String(n).padStart(2,'0'),15)
 }
 if(v==='Password'&&selectedNetwork){ctx.fillStyle='white';ctx.fillRect(16,43,208,18);bitmapText(ctx,16,44,selectedNetwork,12)}
 if(v==='Quiz'&&quizPick>=0){
   const sheet=model.sheets[f.k];if(sheet&&sheet.page!==sheet.total-1)return;
   const row=sheet?Object.keys(sheet.buttons).find(key=>sheet.buttons[key]==='选项 '+String.fromCharCode(65+quizPick)):null;
   let y=sheet?(row?Number(row.split(',')[1])+12:0):186+quizPick*44;
   if(!y)return;ctx.fillStyle='white';ctx.fillRect(205,sheet?Math.min(...Object.keys(sheet.buttons).filter(k=>sheet.buttons[k].startsWith('选项 ')).map(k=>+k.split(',')[1])):174,28,sheet?Math.max(0,337-Math.min(...Object.keys(sheet.buttons).filter(k=>sheet.buttons[k].startsWith('选项 ')).map(k=>+k.split(',')[1]))):176);
   ctx.fillStyle='black';for(let i=0;i<5;i++)ctx.fillRect(208+i,y+i,1,2);for(let i=0;i<9;i++)ctx.fillRect(212+i,y+4-i,1,2)
 }
}

const tabNames=['逐屏评审','全部页面'];
let autoDays=0, castPending=false, renewed=false;
const note={Jump:'每 5% 一档。从阅读菜单进入时定位到当前进度所在页。点百分比跳转，点上一页或下一页只翻选项。',
 Book:'点正文中央打开菜单，两侧翻页。顶边下拉打开快捷设置。按住正文约 0.4 秒再下拉可增删本页书签。正文左上的小房子回到首页。',Menu:'跳转进度、章节目录和我的书签都从这里进入。左上箭头返回正文。',Layout:'点「显示阅读进度」整行开关。开启后可选全书或本章。只从阅读菜单进入。',Password:'可用屏上键盘试输，支持大小写、数字、更多符号、退格和空格。请使用示例密码。',Service:'在阅读器上开服务。电脑或手机上的界面见「浏览器文件服务」。',FontPrep:'开书建索引时等待完成。准备附近字形时可点「立即阅读」结束额外等待；换字体后回到阅读排版。',Tools:'点中间刻度直接选择 0–10 档，点两端的小太阳、大太阳分别调暗或调亮一档。全刷间隔和立即清屏在普通页面共用。从阅读页面进入时还可设置翻页手势。',Lock:'侧键单击唤醒。日期、农历、电量及温湿度使用示例值。',Catalog:'进入课包后在此选择篇目。点击课文页底的「篇目」也能切换。',Toc:'点章节回到该段正文。页末的小按钮切换目录页。',Shelf:'每页显示六本书。点底部按钮或单击侧键翻页，点「添加」从 TF 卡选书。',Files:'每页最多六项。页码显示当前位置，进入子目录或文件详情后，左上返回原列表页。',Marks:'每页最多六枚书签。点页脚翻页，或单击侧键前进。查看详情、删除书签后返回原页。',Choice:'长选项可以先读完整。末页点「选这一项」返回理解题，再核对答案。',Feedback:'可重读本段原文。多页解析先翻到末页，再点「继续」。',Message:'左上返回来处。底部提供返回、重新选字体、重新检测或回到书架。'};
function label(k){return frames[k]?.t||k}
function fbook(){return mark?(foot?'bookMark':'bookOffMark'):(foot?(readPos?'bookNext':'book'):'bookOff')}
function fmenu(){return fromJump?'menuJump':mark?'menuMark':'menu'}
function flayout(){return foot?(chapter?'layoutChapter':'layout'):'layoutOff'}
function setMode(k){if(['bookOff','bookOffMark','layoutOff'].includes(k))foot=false;else if(['book','bookNext','bookMark','layout','layoutChapter'].includes(k))foot=true;
 if(ids[frames[k]?.v]==='Book')mark=['bookMark','bookOffMark'].includes(k);if(k==='layoutChapter')chapter=true;if(k==='layout')chapter=false}
function goto(k,record=true){if(!frames[k])return;if(record&&(frames[k].v!==frames[current].v||ids[frames[k].v]==='Files'))historyKeys.push(current);clearTimeout(blinkTimer);current=k;tab='逐屏评审';render()}
function selectFrame(k){historyKeys=[];savedClock=null;clockValues=[2026,11,11,12,0];clockDirty=false;clockFocus=k==='clockHour'?3:0;selectedNetwork='';quizPick=-1;previousQuiz='quiz';if(k.startsWith('svc'))servicePage=k;setMode(k);activeAnchor=false;anchor=model.reading.initial;pageTrail=[];savedMarks.clear();if(mark)savedMarks.add(anchor);resetReviewList(k);readPos=k==='bookNext'?1:0;fromJump=k==='menuJump';typed=false;touchedLayout=false;touchedTools=false;trackSettings=false;gradedAt=-1000;atLessonEnd=k==='passageNext';if(k.startsWith('quiz'))learnReply=k==='quizLongSelected'?'wrong':'correct';if(['autoClock','clock','autoClockDay','clockDay','autoClockWeek','clockWeek'].includes(k))autoDays=k.endsWith('Day')?1:k.endsWith('Week')?7:0;if(['wifiOn','settingsOn','screenCanvas','screenInfo','screenKey','screenRenewed'].includes(k))activeWifi=true;if(k==='settings')activeWifi=false;if(k==='wifi0')activeWifi=false;if(k.startsWith('pass')){wifiPwd=k==='pass0'?'':'Paper24';pwShown=k==='passShow';keys=({passUpper:1,passNum:2,passSym:3})[k]||0}goto(k,false)}
const parents={Home:'home',Lock:'home',Shelf:'home',Files:'home',File:'filesBooks',Book:'home',Menu:'book',Tools:'settings',Layout:'menu',Toc:'menu',Jump:'menu',Marks:'menu',RemoveMark:'marks',Settings:'home',Wifi:'settings',Password:'wifi',Service:'settings',Learn:'home',Courses:'learn',Catalog:'learn',Passage:'learn',Chunks:'chunkPassage',Quiz:'passage',Choice:'quiz',Feedback:'quiz',Dicts:'learn',Words:'passage',Definition:'words',Review:'learn',Fonts:'layout',Vocab:'learn',Recall:'vocab',Message:'home',Clock:'settings',ClockEdit:'clock',FontPrep:'fonts',Refresh:'settings',AutoLock:'power',CardInfo:'about',About:'settings',Power:'settings',ConfirmRestart:'power',ConfirmLongSleep:'power',AutoClock:'clock',ScreenCanvas:'settings',ScreenInfo:'screenCanvas',ScreenConnect:'settings',ScreenKey:'screenInfo'};
function clockKey(){return autoDays===7?'clockWeek':autoDays===1?'clockDay':'clock'}
function back(){const old=historyKeys.pop()||parents[ids[frames[current].v]]||'home';let k=old;if(ids[frames[k].v]==='Clock')k=clockKey();if(ids[frames[k].v]==='Settings'){k=activeWifi?'settingsOn':'settings';castPending=false}if(ids[frames[k].v]==='Book'&& !['epub','resume'].includes(k))k=fbook();if(ids[frames[k].v]==='Menu')k=fmenu();if(ids[frames[k].v]==='Layout')k=flayout();if(ids[frames[k].v]==='Marks'&&liveBookmarks)k=bookmarkView();goto(k,false)}
function setTab(t){clearTimeout(blinkTimer);tab=t;render()}
function wake(){if(current==='longSleepLock')goto('home',false);else if(current==='lock'||current==='lock0')goto(lastAwake,false);else if(ids[frames[current].v]==='Book'){turnPage(true)}else if(ids[frames[current].v]==='Jump')turnJump(1);else if(ids[frames[current].v]==='Marks')turnBookmarks(1);else if(model.sheets[current]){const s=model.sheets[current];if(s.page+1<s.total)goto(studyFamilies[s.family][s.page+1],false);else if(s.family.startsWith('chunks')&&s.buttons?.['160,380']==='下一句')goto(current==='chunks'?'chunks2':'chunks3',false)}else if(model.catalogPages[current]){let s=model.catalogPages[current];if(!s.offset&&model.catalogData[s.list].rows.length>6)goto(s.list+'2',false)}else if(model.lists[current])nextSheet(current,1);else{let n=({toc:'toc2',wifi:'wifi2',words:'words2',qaCatalog:'qaCatalog2',qaRead:'qaRead2'})[current];if(n)goto(n,false)}}
function sleep(){if(['Lock','FontPrep'].includes(ids[frames[current].v]))return;lastAwake=current;goto('lock',false)}
function pull(){let v=ids[frames[current].v];if(['Lock','FontPrep','Tools'].includes(v))return;goto(['Book','Menu'].includes(v)?'bookTools':'tools')}
function read(k){historyKeys=[];afterBook=k;goto(k,false)}
function jumpPageIndex(key){return key==='jump'?0:Number(key.slice(4))-1}
function jumpPageKey(index){return index?'jump'+(index+1):'jump'}
function openJump(){const pct=model.reading.pages[anchor].progress;goto(jumpPageKey(Math.floor(pct/(model.reading.jumpStep*model.reading.jumpPerScreen))))}
function turnJump(delta){const key=jumpPageKey(jumpPageIndex(current)+delta);if(frames[key])goto(key,false)}
function jump(n){priorAnchor=anchor;fromJump=true;pageTrail=[];targetByte(n);readPos=1;read(fbook())}
function toggleMark(){mark=!mark;if(mark)savedMarks.add(anchor);else savedMarks.delete(anchor);read(fbook())}
function pack(label,fn){return [label,typeof fn==='string'?()=>goto(fn):fn]}
function action(f,h,i){let [x,y,w,H]=h,v=ids[f.v],k=f.k;const link=(key,title)=>frames[key]?pack(title||label(key),key):null;
 if(y===0&&x===0&&v==='Choice')return pack('取消',()=>{if(!historyKeys.length)goto(model.optionRoutes[model.sheets[k]?.family]?.quiz||'quizLong',false);else back()});
 if(model.sheets[k]&&y!==0)return studyAction(f,x,y);
 if(y===0&&x===0)return v==='Book'?link('home','回到首页'):pack('返回',back);
 let idx=Math.round((y-48)/47);
 switch(v){
 case 'Home':return y===109?link(k==='home0'?'pick':fbook()):link(['shelf','learn','files','settings'][Math.round((y-178)/55)]);
 case 'Shelf':case 'Files':if(k==='shelf0')return link('pick','从 TF 卡选书');if(k==='files0')return link('files');return shelfAndFileAction(k,x,y);
 case 'FontPrep':if(k==='bookChapters'||k==='chapterError')return link('file','返回');return pack('立即阅读',()=>{clearTimeout(blinkTimer);if(k==='bookGlyphs'){goto(fbook(),false)}else{while(historyKeys.length&&['Fonts','FontPrep','Layout'].includes(ids[frames[historyKeys.at(-1)].v]))historyKeys.pop();goto(flayout(),false)}});
 case 'File':return y===95?pack('打开阅读',()=>{openedFile=detailFile;goto('book')}):pack('加入书架',()=>goto('fileAdded',false));
 case 'Book':if(x>0&&x<150)return link(fmenu(),'阅读菜单');return pack(x===0?'上一页':'下一页',()=>turnPage(x!==0));
 case 'Menu':if(y===96)return pack('跳转进度',openJump);if(y===141)return link(model.reading.toc.slice(6).some(n=>n<=anchor)?'toc2':'toc','章节目录');if(y===186)return pack('我的书签',openBookmarks);if(y===238)return link(flayout(),'阅读排版');if(y===283)return link('bookTools');if(y===328)return pack(mark?'取消本页书签':'添加本页书签',toggleMark);return pack('回到跳转前',()=>{fromJump=false;targetByte(priorAnchor);readPos=0;read(fbook())});
 case 'Toc':if(y===380)return link(x?'toc2':'toc','目录翻页');if(k==='tocEmpty')return pack('跳转进度',openJump);return pack('打开这一章',()=>jump(model.reading.toc[(k==='toc2'?6:0)+idx]??0));
 case 'Jump':{
   if(y===380)return pack(x?'下一页':'上一页',()=>turnJump(x?1:-1));
   const index=jumpPageIndex(k)*model.reading.jumpPerScreen+Math.round((y-82)/45);
   return pack('跳到 '+index*model.reading.jumpStep+'%',()=>jump(model.reading.jump[index]));
 }
 case 'Marks':{
   if(y===380)return pack(x?'下一页':'上一页',()=>turnBookmarks(x?1:-1));
   if(y>300&&savedMarks.size===0)return pack('添加本页书签',()=>{mark=true;savedMarks.add(anchor);read(fbook())});
   const n=bookmarkRows()[Math.round((y-52)/47)];if(n===undefined)return null;
   return pack('书签详情',()=>{selectedBookmark=n;liveBookmarks=true;goto('markDetail')});
 }
 case 'RemoveMark':return y>330?pack('打开书签位置',()=>{priorAnchor=anchor;fromJump=true;openedFile='';targetByte(selectedBookmark);read(fbook())}):pack('移除此书签',()=>{savedMarks.delete(selectedBookmark);mark=savedMarks.has(anchor);liveBookmarks=true;back()});
 case 'Tools':if(y===44){
   let n=lightTarget(x);return n==='dimmer'?pack('调暗一档',()=>setBrightness(light-1)):n==='brighter'?pack('调亮一档',()=>setBrightness(light+1)):pack('亮度 '+n+' 档',()=>setBrightness(n))
 }if(y===110)return link('refresh');if(y===190)return pack('调整翻页手势',()=>{touchedTools=true;grip=Math.max(0,Math.min(2,grip+(x<150?-1:1)));render()});return pack('立即清屏并返回',back);
 case 'Layout':if(y===48)return link('fonts');if(y===304)return pack('显示阅读进度',()=>{foot=!foot;goto(flayout(),false)});if(y===354)return pack('切换进度范围',()=>{chapter=!chapter;goto(flayout(),false)});return pack('调整'+['字号','行距','边距','墨色'][Math.round((y-105)/47)],()=>{touchedLayout=true;const j=Math.round((y-105)/47);layoutVals[j]=Math.min([30,10,22,2][j],Math.max([18,4,10,0][j],layoutVals[j]+[2,3,6,1][j]*(x<150?-1:1)));render()});
 case 'Fonts':return y===48?pack('内置点阵',back):y===380?link('fontsRoot','上级'):k==='fontsRoot'?link('fonts','fonts'):pack('使用此字体',startPrep);
 case 'Settings':if(y===128)return pack('无线墨屏',()=>{castPending=!activeWifi;goto(activeWifi?'screenCanvas':'screenConnect')});return link(({40:activeWifi?'wifiOn':'wifi0',84:activeWifi?servicePage:'svcNet',181:'tools',225:'refresh',278:clockKey(),322:'power',366:'about'})[y]);
 case 'Wifi':if(y===45)return k==='wifi0'?pack('开启 Wi-Fi',runScan):pack('关闭 Wi-Fi',()=>{activeWifi=false;servicePage='svcOff';goto('wifi0',false)});
   if(y===380)return pack(x?'下一页':'上一页',()=>goto(x?'wifi2':'wifi',false));if(y>=330)return pack('重新扫描',runScan);
   return pack('输入网络密码',()=>{selectedNetwork=['Studio','Home Wi-Fi','Reading Room','Garden','Desk','Guest'][(k==='wifi2'?5:0)+Math.round((y-108)/44)];wifiPwd='';typed=true;keys=0;pwShown=false;goto('pass0')});
 case 'Password':return keyAction(f,h);
 case 'Service':{if(k==='svcNet')return link('wifi','连接网络');if(k==='noCardService')return link('card0','检查存储卡');
   let dest=y===172?'svcTx':y===188?'svcPair':k==='svcTx'?'svcStop':k==='svcOff'?'svcPair':'svcOff';
   let title=y===172?'继续传输':y===188?'更新配对码':k==='svcOff'?'开启文件服务':'停止服务';
   return pack(title,()=>{servicePage=dest;goto(dest,false)})}
 case 'Refresh':return pack('每 '+[5,10,20,50,100][Math.round((y-48)/48)]+' 页全刷',()=>{rangePage=[5,10,20,50,100][Math.round((y-48)/48)];trackSettings=true;back()});
 case 'AutoLock':return pack('选择锁屏时间',()=>{lockMin=[5,10,15,30,0][Math.round((y-48)/48)];trackSettings=true;back()});
 case 'CardInfo':return link(y===122?'files':'card');
 case 'Clock':return link(y===172?'clockEdit':y===223?(autoDays===7?'autoClockWeek':autoDays===1?'autoClockDay':'autoClock'):(activeWifi?'clockOk':'clockFail'));
 case 'AutoClock':return pack(['关闭','每天','每周'][(y-48)/48],()=>{autoDays=[0,1,7][(y-48)/48];back()});
 case 'About':return link('card');
 case 'Power':return link(({48:'auto',109:'longSleep',170:'restart'})[y]);
 case 'ConfirmLongSleep':return pack('进入长期休眠',()=>{lastAwake='home';historyKeys=[];goto('longSleepLock',false)});
 case 'ConfirmRestart':return pack('确认重启',()=>{historyKeys=[];goto('home',false)});
 case 'ScreenConnect':return pack('连接 Wi-Fi',()=>{castPending=true;goto('wifi')});
 case 'ScreenCanvas':return link(renewed?'screenRenewed':'screenInfo','查看连接信息');
 case 'ScreenInfo':return y===254?link('screenKey'):pack('结束展示',()=>{historyKeys=['home'];goto(activeWifi?'settingsOn':'settings',false)});
 case 'ScreenKey':return pack('确认重置',()=>{renewed=true;historyKeys.pop();goto('screenRenewed',false)});
 case 'ClockEdit':return clockAction(x,y);
 case 'Learn':return link(({48:'passage',116:'courses',163:courseGroup,226:'dict',273:'vocab',320:'review'})[y]);
 case 'Courses':return pack('选择当前课包',()=>{courseGroup=model.courseRows[y];goto(courseGroup)});
 case 'Catalog':{const page=model.catalogPages[k],list=model.catalogData[page.list];
   if(y===380)return pack(x?'下一页':'上一页',()=>goto(page.list+(x?'2':''),false));
   const item=list.rows[page.offset+Math.round((y-52)/47)];return pack(item.title,()=>{courseGroup=page.list;goto(item.read)})}
 case 'Passage':if(y===307)return link('chunks');if(y===380&&x===0)return link(k.startsWith('qa')?'qaCatalog':'catalog');if(y===380&&x===80)return link('words');if(y===380&&k==='qaRead')return link('qaRead2');return link('quiz');
 case 'Chunks':return link(k==='chunks'?'chunks2':x===0?'chunks':'quiz');
 case 'Quiz':if(model.quizRoutes[k]){
   const q=model.quizRoutes[k];if(y>=362)return pack('核对答案',()=>{gradedAt=performance.now();goto(quizPick===q.correct?q.good:q.bad)});
   if(y<174)return pack('阅读完整题干',()=>goto(k+'Prompt'));
   let pick=Math.round((y-174)/44);if(q.detail?.[pick])return pack('阅读选项 '+String.fromCharCode(65+pick),()=>goto(q.detail[pick]));
   return pack('选择答案',()=>{quizPick=pick;goto(q.selected,false)})}if(y>=362)return pack('核对答案',()=>{gradedAt=performance.now();goto(learnReply)});if(k.startsWith('quizLong')&&y===306)return pack('阅读长选项',()=>{previousQuiz=k;goto('choiceFirst')});return pack('选择答案',()=>{quizPick=Math.round((y-174)/44);learnReply=quizPick===1?'correct':'wrong';goto(k.startsWith('quizLong')?'quizLongSelected':'quizChoice',false)});
 case 'Words':return y===380?link(x?'words2':'words'):link('definition');
 case 'Definition':return link('vocab');
 case 'Dicts':return pack('选择词典',back);
 case 'Vocab':return link('recall');
 case 'Recall':return link('reveal','显示词义');
 case 'Review':return link('passage');
 case 'Message':return k==='msgCard'?link('card0','重新检测'):k==='msgEpub'?link('shelf','回到书架'):k==='msgFont'?link('fonts','重新选字体'):pack('返回',back);
 default:return null}
}
const keyboards=[['qwertyuiop','asdfghjkl','zxcvbnm'],['QWERTYUIOP','ASDFGHJKL','ZXCVBNM'],['1234567890','!@#$%&*()','-_=+./?'],['~`^[]{}<>',":;'\x22\\|",',!@#$%&*']];
function keyScreen(){return ['pass','passUpper','passNum','passSym'][keys]}
function keyAction(f,h){let [x,y]=h,write=fn=>()=>{typed=true;fn();goto(keyScreen(),false)};if(y===65)return pack('显示或隐藏密码',write(()=>pwShown=!pwShown));if(y>=127&&y<=219){let r=(y-127)/46,chars=keyboards[keys][r],w=Math.floor(232/chars.length),start=Math.floor((240-chars.length*w)/2),c=chars[Math.round((x-start)/w)];return pack(c,write(()=>{if(wifiPwd.length<64)wifiPwd+=c}))}if(y===268){if(x===6)return pack('切换大小写',write(()=>{keys=keys===1?0:1}));if(x===86)return pack('切换数字或符号',write(()=>{keys=keys===2?3:2}));return pack('退格',write(()=>wifiPwd=wifiPwd.slice(0,-1)))}if(y===309)return pack('空格',write(()=>{if(wifiPwd.length<64)wifiPwd+=' '}));return pack('连接示例网络',()=>{pwShown=false;typed=true;goto('passJoining',false);blinkTimer=setTimeout(()=>{activeWifi=wifiPwd==='Paper24';if(activeWifi&&castPending){castPending=false;historyKeys=['home','settingsOn'];goto('screenCanvas',false)}else goto(activeWifi?'wifiOn':'passErr',false)},900)})}
function bitmapText(ctx,x,y,s,sz=12){for(const c of String(s)){const g=model.ascii[c]?.[sz===12?0:1];if(!g)continue;ctx.fillStyle='black';for(let j=0;j<g[4].length;j++)for(let i=0;i<g[3];i++)if(g[4][j]&(32768>>i))ctx.fillRect(x+g[1]+i,y+g[2]+j,1,1);x+=g[0]}}
function width(s,n){return [...s].reduce((v,c)=>v+(model.ascii[c]?.[n===12?0:1]?.[0]||0),0)}
function overlays(ctx,f){const v=ids[f.v];function field(x,y,w,h,s,sz,center=false){ctx.fillStyle='white';ctx.fillRect(x,y,w,h);bitmapText(ctx,center?x+Math.floor((w-width(s,sz))/2):x,y,s,sz)}
 if(v==='Password'&&typed){let s=pwShown?wifiPwd:'*'.repeat(wifiPwd.length);while(width(s,15)>154)s=s.slice(1);field(16,79,155,19,s,15);ctx.fillStyle='white';ctx.fillRect(177,70,56,27);const state=frames[pwShown?'passShow':'pass'];const im=new Image();const tag=serial;im.onload=()=>{if(tag===serial)ctx.drawImage(im,177,70,56,27,177,70,56,27)};im.src=state.img}
 if(v==='Jump'&&activeAnchor){
   const pct=model.reading.pages[anchor].progress;
   field(model.reading.jumpValueX,55,46,16,pct+'%');ctx.fillStyle='white';ctx.fillRect(204,82,30,270);
   const row=pct/model.reading.jumpStep-jumpPageIndex(f.k)*model.reading.jumpPerScreen;
   if(Number.isInteger(row)&&row>=0&&row<model.reading.jumpPerScreen){
     const y=93+row*45;ctx.fillStyle='black';for(let i=0;i<5;i++)ctx.fillRect(208+i,y+5+i,1,2);
     for(let i=0;i<10;i++)ctx.fillRect(212+i,y+9-i,1,2);
   }
 }
 drawMaterialState(ctx,v,f);
 if(v==='Layout'&&touchedLayout){const labels=[String(layoutVals[0]),String(layoutVals[1]),String(layoutVals[2]),['清淡','标准','浓墨'][layoutVals[3]]];for(let i=0;i<4;i++)field(127,121+i*47,49,17,labels[i],12,true)}
 if(v==='Tools'){if(light!==4)drawBrightness(ctx);if(touchedTools&&f.k==='bookTools')field(127,206,49,17,['标准','左手','右手'][grip],12,true)}
 if(trackSettings&&(v==='Refresh'||v==='AutoLock')){let vals=v==='Refresh'?[5,10,20,50,100]:[5,10,15,30,0];let on=vals.indexOf(v==='Refresh'?rangePage:lockMin);ctx.fillStyle='white';ctx.fillRect(205,48,30,240);ctx.fillStyle='black';for(let t=0;t<4;t++)ctx.fillRect(208+t,64+48*on+t,1,2);for(let t=0;t<9;t++)ctx.fillRect(211+t,67+48*on-t,1,2)}
 if(trackSettings&&(v==='Settings'||v==='Tools')){let vals=v==='Tools'?[[125,rangePage+' 页']]:[[240,rangePage+' 页']];for(let [y,s] of vals){ctx.fillStyle='white';ctx.fillRect(141,y,62,16);bitmapText(ctx,202-width(s,12),y,s)}}
}
function screenNode(f){let box=el('div',undefined,'plate'),scr=el('div',undefined,'screen'+(hot?' marked':'')),c=el('canvas');c.width=240;c.height=416;c.setAttribute('aria-label',f.t);scr.append(c);box.append(scr);
 let ctx=c.getContext('2d'),im=new Image(),tag=serial;im.onload=()=>{if(tag===serial){ctx.drawImage(im,0,0);overlays(ctx,f)}};const p=model.reading.pages[anchor],dyn=activeAnchor&&['book','bookNext','bookMark','bookOff','bookOffMark','bookAgain','menu','menuMark','menuJump'].includes(f.k);
 if(dyn&&ids[f.v]==='Book'){im.onload=()=>{if(tag!==serial)return;ctx.drawImage(im,0,0);if(mark){ctx.fillStyle='black';ctx.fillRect(230,42,9,14);for(let j=0;j<4;j++){ctx.fillRect(230,56+j,4-j,1);ctx.fillRect(235+j,56+j,4-j,1)}}};im.src=p[foot?'on':'off']}
 else if(dyn&&ids[f.v]==='Menu'){im.onload=()=>{if(tag!==serial)return;ctx.drawImage(im,0,0);if(mark&&fromJump)picture(frames.menuMark.img).then(marked=>{if(tag===serial)ctx.drawImage(marked,0,329,240,42,0,329,240,42)});const top=new Image;top.onload=()=>{if(tag===serial)ctx.drawImage(top,0,34)};top.src=p.menu};im.src=f.img}
 else if(ids[f.v]==='Marks'&&liveBookmarks){im.onload=()=>{if(tag===serial){ctx.drawImage(im,0,0);drawBookmarkList(ctx,tag)}};im.src=f.img}
 else if(f.k==='markDetail'&&liveBookmarks)im.src=model.reading.pages[selectedBookmark].detail;
 else if(f.k==='fileAdded'&&detailFile){im.onload=async()=>{if(tag!==serial)return;ctx.drawImage(im,0,0);let original=await picture(model.files[detailFile]?.detail||f.img);if(tag===serial)ctx.drawImage(original,0,35,240,105,0,35,240,105)};im.src=f.img}
 else if(ids[f.v]==='File'&&detailFile)im.src=model.files[detailFile]?.detail||f.img;
 else if(f.k==='book'&&openedFile&&openedFile!=='/books/沿河散步.txt')im.src=model.files[openedFile]?.book||f.img;
 else im.src=f.img;
 let guard=false,down,still=0;
 scr.addEventListener('pointerdown',e=>{down={x:e.clientX,y:e.clientY,t:performance.now(),r:scr.getBoundingClientRect()};still=0});
 scr.addEventListener('pointermove',e=>{if(down&&!still&&Math.abs(e.clientY-down.y)*416/down.r.height>8)still=performance.now()-down.t});
 scr.addEventListener('pointerup',e=>{if(!down)return;let {x,y,t,r}=down;down=null;let sy=(y-r.y)*416/r.height,dy=(e.clientY-y)*416/r.height,dx=Math.abs(e.clientX-x)*240/r.width;if(Math.abs(dy)>15||dx>15){guard=true;setTimeout(()=>guard=false,400)}if(dy<44||dx>dy/2)return;guard=true;e.preventDefault();if(sy<32)pull();else if(ids[f.v]==='Book'&&f.k!=='resume'&&sy>=36&&sy<(foot?402:416)&&dy>=48&&(still||performance.now()-t)>=400)toggleMark();setTimeout(()=>guard=false,400)});
 const hits=ids[f.v]==='Marks'&&liveBookmarks?bookmarkHits():f.hits;
 for(let i=0;i<hits.length;i++){let h=hits[i],a=action(f,h,i);if(!a)continue;let [x,y,w,H]=h,b=bt('',e=>{if(!guard)a[1]()});b.className='hit';if(ids[f.v]==='Tools'&&y===44){
 let t=lightTarget(x);b.dataset.light=t;if(typeof t==='number')b.setAttribute('aria-pressed',String(light===t));else b.setAttribute('aria-disabled',String(t==='dimmer'?light===0:light===10));
 }b.title=a[0];b.setAttribute('aria-label',a[0]);Object.assign(b.style,{left:x/240*100+'%',top:y/416*100+'%',width:w/240*100+'%',height:H/416*100+'%'});scr.append(b)}
 return box}
function index(){const n=$('#index');n.replaceChildren();let q=menuFilter.toLowerCase();for(const [g,ls] of groups){let d=el('details'),h=el('summary',g);d.open=q!==''||ls.some(s=>s[0]===ids[frames[current].v]);d.append(h);for(const [n,t,k] of ls){let match=all.filter(f=>ids[f.v]===n&&f.t.toLowerCase().includes(q));if(q&&!t.toLowerCase().includes(q)&&!match.length)continue;let b=bt(t,()=>selectFrame(q&&match.length?match[0].k:k));if(ids[frames[current].v]===n&&tab==='逐屏评审')b.className='active';d.append(b);if(q)for(const f of match){if(f.k===k)continue;let child=bt(' · '+f.t,()=>selectFrame(f.k));d.append(child)}}if(d.children.length>1)n.append(d)}}
function screenReview(main){let f=frames[current],v=ids[f.v],group=groups.find(g=>g[1].some(r=>r[0]===v))[0],family=rows.find(r=>r[0]===v)[1];
 main.append(el('div',group+' / '+family,'bread'),el('h2',liveBookmarks&&v==='Marks'&&savedMarks.size>6?'我的书签 · 第'+(bookmarkPage+1)+'页':f.t));let reader=el('div',undefined,'reader'),left=el('div'),side=el('div',undefined,'panel');side.id='reviewSide';left.append(screenNode(f));
 let controls=el('div',undefined,'flex plateActions');controls.append(bt('单击侧键',wake),bt('双击侧键',sleep));if(!['Lock','FontPrep','Tools'].includes(v))controls.append(bt('顶边下拉',pull));left.append(controls);
 const routeNote=note[v]||'可点屏内控件查看下一步，也可直接选择下面的状态对照。';side.append(el('p',routeNote,'notes'));if(f.note)side.append(el('p',f.note,'muted'));if(v==='Catalog'&&current.startsWith('qa'))side.append(el('p','篇目取自 TF 课包。设计稿选取部分篇目和各篇首段演示。','muted'));if(v==='Password')side.append(el('p','示例网络 Studio，演示密码 Paper24。','muted'));
 side.append(el('h3','同页状态'));let variants=el('div',undefined,'variants');let extra=el('details'),extraItems=el('div',undefined,'variants');extra.append(el('summary','课包内容样例'),extraItems);for(const a of all.filter(x=>x.v===f.v)){let b=bt(a.t,()=>selectFrame(a.k));b.setAttribute('aria-pressed',String(a.k===f.k));(a.k.startsWith('sample')?extraItems:variants).append(b)}side.append(variants);if(extraItems.children.length){extra.style.margin='10px 0';extra.open=f.k.startsWith('sample');side.append(extra)}
 let path=el('div',undefined,'route');path.append(el('h3','预览设置'));let tools=el('div',undefined,'settingsrow'),sel=el('select');sel.setAttribute('aria-label','显示尺寸');for(let s of ['原生像素','放大两倍','3.7 英寸参考']){let o=el('option',s);o.selected=scale===s;sel.append(o)}sel.onchange=()=>{scale=sel.value;render()};let l=el('label'),c=el('input');c.type='checkbox';c.checked=hot;c.onchange=()=>{hot=c.checked;render()};l.append(c,' 显示交互热区');tools.append(sel,l);path.append(tools);
 if(scale==='3.7 英寸参考'){const p=el('p',undefined,'muted'),s=el('input');s.type='number';s.value=mm;s.min='25';s.max='90';s.step='0.1';s.style.width='6em';s.onchange=()=>{mm=Math.min(90,Math.max(25,Number(s.value)||46.96));render()};p.append('显示宽度 ',s,' mm。用直尺校准至约 47 mm。');path.append(p)}
 if(v==='FontPrep'&&['bookChapters','chapterError'].includes(current)){path.append(bt('打开目录整理流程',()=>location.assign('glyph-preparation.html#txt')))}else if(v==='FontPrep'){const stages=current.startsWith('book')?['bookCatalog','bookGlyphs','book']:['fontOpen','fontPrep','fontDraw','fontGlyphs','fontDone','layout'];path.append(bt('查看下一阶段',()=>goto(stages[(stages.indexOf(current)+1)%stages.length])))}
 if(f.k==='wifiScan')path.append(bt('查看扫描结果',()=>goto('wifi')));
 if(v==='Service')path.append(bt('打开浏览器端',()=>location.assign('files.html')));
 side.append(path);reader.append(left,side);main.append(reader)}
function overview(main){main.append(el('h2','全部页面'));let opts=el('div',undefined,'pageTools'),button=bt(allStates?'仅列出主页面':'展开所有状态',()=>{allStates=!allStates;render()});opts.append(el('span',ids.length+' 个设备页面 · '+all.length+' 个渲染状态'),button);main.append(opts);
 for(const [g,ls] of groups){main.append(el('h3',g));let grid=el('div',undefined,'gallery');for(const [n,t,k] of ls){let a=all.filter(f=>ids[f.v]===n);for(const f of allStates?a:[frames[k]]){let tile=bt('',()=>selectFrame(f.k));tile.className='tile';let im=new Image();im.loading='lazy';im.alt='';im.src=f.img;tile.append(im,el('strong',allStates?f.t:t));if(!allStates)tile.append(el('small',a.length+' 个状态'));grid.append(tile)}}main.append(grid)}}
function render(){++serial;document.body.classList.toggle('physical',scale==='3.7 英寸参考');document.body.classList.toggle('double',scale==='放大两倍');document.body.style.setProperty('--mm',mm+'mm');
 const n=$('#tabs');n.replaceChildren();for(let t of tabNames){let b=bt(t,()=>setTab(t));b.setAttribute('aria-pressed',String(t===tab));n.append(b)}let portalLink=el('a','网页文件服务');portalLink.href='files.html';n.append(portalLink);index();let main=$('#main');main.replaceChildren();if(tab==='逐屏评审')screenReview(main);else overview(main);
 const k=encodeURIComponent(tab==='逐屏评审'?current:tab);if(location.hash.slice(1)!==k)window.history.replaceState(null,'','#'+k);
}
$('#search').oninput=e=>{menuFilter=e.target.value.trim();index()};$('#scope').textContent=ids.length+' 个设备页面 · '+all.length+' 个状态 · 240 × 416 · 正式交互设计稿';
let initial=decodeURIComponent(location.hash.slice(1));if(!initial)initial='home';if(frames[initial]){selectFrame(initial)}else if(tabNames.includes(initial))tab=initial;
document.addEventListener('keydown',e=>{if(tab!=='逐屏评审'||e.target.tagName==='INPUT'||e.target.tagName==='SELECT')return;if(e.key==='Escape'){if(!['Lock','FontPrep'].includes(ids[frames[current].v]))back();return}if(ids[frames[current].v]==='Password'&&((e.key.length===1&&e.key>=' '&&e.key<='~')||e.key==='Backspace')){e.preventDefault();typed=true;wifiPwd=e.key==='Backspace'?wifiPwd.slice(0,-1):(wifiPwd+e.key).slice(0,64);goto(keyScreen(),false)}});
window.addEventListener('hashchange',()=>{const key=decodeURIComponent(location.hash.slice(1));if(frames[key])selectFrame(key);else if(tabNames.includes(key))setTab(key)});
render();

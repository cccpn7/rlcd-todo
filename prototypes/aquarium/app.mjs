import { Aquarium, MOUTH_OFFSET } from './model.mjs';
import { banYuLogo } from './logo.mjs';
const canvas = document.querySelector('#screen');
const ctx = canvas.getContext('2d', { willReadFrequently: true });
let tank = new Aquarium();
const $ = id => document.getElementById(id);
// Original monochrome sprite inspired by the supplied silhouette: forked tail,
// tapered body, projecting fins, gill and large eye. w is an opaque white pixel.
const fishArt = [
  '..............####.........',
  '.............######........',
  '##.........###########.....',
  '.##.....###wwwwwwwwwww##...',
  '..##..##wwwwwwwwwwwww#ww#..',
  '...###wwwwwwwwwwwwww#ww###.',
  '....##wwwwwwwwwwwwww#ww###.',
  '....##wwwwwwwwwwwwww#wwwww#',
  '...###wwwwwwwwwwwwww#wwwww#',
  '..###.##wwwwwwwwwwwww#www#.',
  '.###....###wwwwwwwwwwww##..',
  '##.........####wwwww###....',
  '..............######.......',
  '................###........',
];
function rect(x,y,w,h,color='#000') { ctx.fillStyle=color; ctx.fillRect(Math.round(x),Math.round(y),w,h); }
function pixelLine(x0,y0,x1,y1) {
  x0=Math.round(x0); y0=Math.round(y0); x1=Math.round(x1); y1=Math.round(y1);
  const dx=Math.abs(x1-x0),sx=x0<x1?1:-1,dy=-Math.abs(y1-y0),sy=y0<y1?1:-1;let e=dx+dy;
  for (;;) { rect(x0,y0,1,1); if(x0===x1&&y0===y1)break; const v=2*e;if(v>=dy){e+=dy;x0+=sx;}if(v<=dx){e+=dx;y0+=sy;} }
}
function text(value,x,y,size=12) {
  ctx.fillStyle='#000';ctx.font=`${size}px "PingFang SC", "Microsoft YaHei", sans-serif`;ctx.textBaseline='top';ctx.fillText(value,x,y);
}
function header() {
  if(tank.mode==='todo')text(['今日重点','今日杂项','今日追踪'][tank.page],12,10,26);
  else banYuLogo.forEach((row,y)=>[...row].forEach((pixel,x)=>{
    if(pixel!=='.')rect(12+x,6+y,1,1,pixel==='#'?'#000':'#fff');
  }));
  const date = new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',month:'2-digit',day:'2-digit'}).format(new Date()).replace('/','.');
  text(date,193,15,12);
  // Illustrative Wi-Fi/link icons, not a real connection indicator.
  for(let i=0;i<3;i++) rect(251+i*4,22-i*3,2,2+i*3);
  rect(272,17,6,6);rect(281,17,6,6);rect(278,19,3,2);
}
function plant(x,height,phase) {
  const base=375; let px=x,py=base;
  for(let j=1;j<=height;j+=3){const xx=x+Math.sin(j/13+tank.now/2200+phase)*4;pixelLine(px,py,xx,base-j);if(j%9===1){const dir=j%18===1?1:-1;pixelLine(xx,base-j,xx+dir*8,base-j-6);pixelLine(xx+dir*8,base-j-6,xx+dir*2,base-j-4);}px=xx;py=base-j;}
}
function drawFish(f,i) {
  const s=2, tail=Math.floor(tank.now/210+i)%2;
  const mouthX=f.x+MOUTH_OFFSET*f.direction;
  fishArt.forEach((row,y)=>[...row].forEach((v,x)=>{
    if(v==='.')return;
    const yy=y+(x<5?tail:0);
    const xx=f.direction===1?(x-26)*s:(26-x)*s-(s-1);
    rect(mouthX+xx,f.y+(yy-7)*s,s,s,v==='#'?'#000':'#fff');
  }));
  rect(mouthX-6*f.direction,f.y-4,1,1,'#fff');
  // Text is drawn separately so the name never mirrors when the fish turns.
  const labelX=Math.round(mouthX-24*f.direction);
  ctx.save();ctx.fillStyle='#000';ctx.font='600 14px "PingFang SC", "Microsoft YaHei", sans-serif';
  ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(['思','闪','点'][i],labelX,f.y);ctx.restore();
  if(f.biteAt && tank.now-f.biteAt<350){rect(mouthX+6*f.direction,f.y-7,2,2);rect(mouthX+9*f.direction,f.y,2,2);rect(mouthX+6*f.direction,f.y+7,2,2);}
}
function drawFood(p) {
  // Larger hollow grains mixed with smaller solid crumbs, all native black/white pixels.
  const width=p.size,height=p.shape===1?p.size-2:p.size;
  const left=Math.round(p.x-width/2),top=Math.round(p.y-height/2);
  if(p.size>=6 && p.shape!==3) {
    const rx=width/2,ry=height/2,thickness=p.size>=8?2:1;
    for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
      const dx=x+0.5-rx,dy=y+0.5-ry;
      const outer=dx*dx/(rx*rx)+dy*dy/(ry*ry);
      const inner=dx*dx/((rx-thickness)**2)+dy*dy/((ry-thickness)**2);
      if(outer<=1&&inner>=1)rect(left+x,top+y,1,1);
    }
    return;
  }
  for(let row=0;row<height;row++) {
    const edge=row===0||row===height-1;
    const inset=edge?(p.shape===2?2:1):0;
    const chip=p.shape===2&&row===1?1:0;
    rect(left+inset+chip,top+row,width-2*inset-chip,1);
  }
}
function drawTank() {
  for(let x=12;x<288;x+=4) rect(x,44+Math.round(Math.sin(x/26+tank.now/1100)),4,1);
  plant(34,59,0);plant(48,80,1);plant(58,43,3);plant(248,50,4);plant(265,67,2);
  for(let i=0;i<6;i++){const x=26+(i*41)%248,y=363-((tank.now/(45+i*8)+i*47)%297);if(i%2){rect(x,y,3,1);rect(x-1,y+1,1,3);rect(x+3,y+1,1,3);rect(x,y+4,3,1);}else rect(x,y,2,2);}
  for(let x=12;x<288;x+=2)rect(x,377+Math.round(Math.sin(x/21)*2),2,1);
  for(let i=0;i<23;i++)rect(18+(i*37)%269,385+(i*17)%9,i%3===0?3:1,1);
  for(const p of tank.food)if(!p.eaten&&!p.expired&&tank.now>=p.release)drawFood(p);
  tank.fish.forEach(drawFish);
  if(tank.mode==='feeding'&&tank.food.every(p=>p.eaten||p.expired)) text('看待办',129,65,14);
}
const samplePages=[['核对样品反馈','整理下一版方案','确认界面调整'],['归档本周资料'],[]];
function drawTodo() {
  rect(12,38,276,1);const items=samplePages[tank.page];
  if(!items.length)text('暂无事项',12,64,24);
  else items.forEach((t,i)=>{text(`${i+1}.`,12,54+i*64,24);text(t,43,54+i*64,24);});
  rect(12,369,276,1);text('1 / 1',12,380);text('示例清单',228,380);
}
function draw() {
  rect(0,0,300,400,'#fff');header();
  if(tank.mode==='todo')drawTodo();else drawTank();
  // Canvas text rasterization varies by OS; force the prototype to exactly two colors.
  const frame=ctx.getImageData(0,0,300,400);
  for(let i=0;i<frame.data.length;i+=4){const v=frame.data[i]<160?0:255;frame.data[i]=frame.data[i+1]=frame.data[i+2]=v;frame.data[i+3]=255;}
  ctx.putImageData(frame,0,0);
  const labels={aquarium:'鱼缸 · 自由游动',feeding:'喂食 · 小鱼追食',todo:'待办 · 每页 5 秒'};
  $('mode-label').textContent=labels[tank.mode];
  const left=Math.max(0,Math.ceil((tank.deadline-tank.now)/1000));
  $('countdown').textContent=tank.mode==='todo'?`${Math.floor(left/60)}:${String(left%60).padStart(2,'0')} 后回鱼缸`:tank.mode==='feeding'?`剩余 ${tank.food.filter(p=>!p.eaten&&!p.expired).length} 颗`:'常驻';
  ['a','b','c'].forEach((s,i)=>$('stage-'+s).classList.toggle('active',['aquarium','feeding','todo'][i]===tank.mode));
  $('bottle').setAttribute('aria-pressed',String(tank.near));
  $('bottle-label').textContent=tank.near?'拿开鱼粮瓶':'靠近鱼粮瓶';
  $('bottle-mark').classList.toggle('present',tank.near);
  const hint=tank.mode==='todo'?'移开再靠近鱼粮瓶，或短按 KEY，可重新喂鱼；吃完重新展示待办 3 分钟。':tank.mode==='feeding'?'鱼粮落下，小鱼追过去吃；移开后再次靠近，或短按 KEY，可追加12颗。':tank.near&&!tank.armed?'先拿开瓶子，稍等片刻，再靠近才能喂下一次。':'点击模拟磁铁靠近，再点击拿开。';
  if($('hint').textContent!==hint)$('hint').textContent=hint;
  ['prev','home','last-five'].forEach(id=>$(id).disabled=tank.mode!=='todo');
  $('next').disabled=false;
  $('next').textContent='KEY · 喂鱼';
  canvas.setAttribute('aria-label',tank.mode==='todo'?`${labels.todo}，${['今日重点','今日杂项','今日追踪'][tank.page]}，${itemsText()}`:labels[tank.mode]);
}
function itemsText(){return samplePages[tank.page].join('；')||'暂无事项';}
$('bottle').onclick=()=>{tank.presence(!tank.near);draw();};
$('reset').onclick=()=>{tank=new Aquarium();draw();};
$('last-five').onclick=()=>{if(tank.mode==='todo')tank.deadline=tank.now+5000;};
$('prev').onclick=()=>tank.turn(-1);$('next').onclick=()=>tank.feed();$('home').onclick=()=>tank.home();
let previous=performance.now(),accumulator=0,paintAt=0;
function loop(now) {
  const elapsed=now-previous;previous=now;
  // Foreground animation is fixed-step. Long hidden-tab gaps advance the viewing deadline directly.
  if(elapsed>1000 && tank.mode==='todo')tank.step(elapsed);
  else {accumulator+=Math.min(elapsed,1000);while(accumulator>=20){tank.step(20);accumulator-=20;}}
  if(now-paintAt>=1000/12){draw();paintAt=now;}requestAnimationFrame(loop);
}
draw();requestAnimationFrame(loop);

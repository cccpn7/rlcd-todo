import {Fishing,wrap,DROP_MS,WAIT_MS,APPROACH_MS,BITE_MS} from './model.mjs';
import {fishKinds} from './art.mjs';
import {banYuLogo} from '../aquarium/logo.mjs';
const residentArt=[
  "..............####.........",
  ".............######........",
  "##.........###########.....",
  ".##.....###wwwwwwwwwww##...",
  "..##..##wwwwwwwwwwwww#ww#..",
  "...###wwwwwwwwwwwwww#ww###.",
  "....##wwwwwwwwwwwwww#ww###.",
  "....##wwwwwwwwwwwwww#wwwww#",
  "...###wwwwwwwwwwwwww#wwwww#",
  "..###.##wwwwwwwwwwwww#www#.",
  ".###....###wwwwwwwwwwww##..",
  "##.........####wwwww###....",
  "..............######.......",
  "................###........",
];
const $=id=>document.getElementById(id),canvas=$('screen'),ctx=canvas.getContext('2d',{willReadFrequently:true});
const sim=new Fishing();let clock=0,food=[],pages=[],previous=performance.now(),drawAt=0,approachFrom=null;const poses=new Map();
const rect=(x,y,w,h,c='#000')=>{ctx.fillStyle=c;ctx.fillRect(Math.round(x),Math.round(y),w,h);};
function line(x,y,xx,yy){x=Math.round(x);y=Math.round(y);xx=Math.round(xx);yy=Math.round(yy);let dx=Math.abs(xx-x),sx=x<xx?1:-1,dy=-Math.abs(yy-y),sy=y<yy?1:-1,e=dx+dy;for(;;){rect(x,y,1,1);if(x===xx&&y===yy)break;let e2=2*e;if(e2>=dy){e+=dy;x+=sx;}if(e2<=dx){e+=dx;y+=sy;}}}
function text(t,x,y,size=12){ctx.fillStyle='#000';ctx.font=`${size}px "PingFang SC","Microsoft YaHei",sans-serif`;ctx.textBaseline='top';ctx.fillText(t,x,y);}
function border(x,y,w,h){rect(x,y,w,h,'#fff');rect(x,y,w,2);rect(x,y+h-2,w,2);rect(x,y,2,h);rect(x+w-2,y,2,h);}
function pose(id){if(!poses.has(id))poses.set(id,{x:30+Math.random()*238,y:65+Math.random()*175,vx:(Math.random()<.5?-1:1)*(13+Math.random()*14),vy:(Math.random()-.5)*12,kind:Math.floor(Math.random()*fishKinds.length),phase:Math.random()*7});return poses.get(id);}
function fish(x,y,d,id){if(String(id).startsWith('base')){const i=Number(id.slice(4)),mouth=x+26*d,tail=Math.floor(clock/210+i)%2;residentArt.forEach((row,j)=>[...row].forEach((v,k)=>{if(v!=='.')rect(mouth+(d===1?(k-26)*2:(26-k)*2-1),y+(j+(k<5?tail:0)-7)*2,2,2,v==='#'?'#000':'#fff');}));ctx.fillStyle='#000';ctx.font='14px "PingFang SC",sans-serif';ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(['思','闪','点'][i],mouth-24*d,y);ctx.textAlign='start';return;}const p=pose(id),art=fishKinds[p.kind].art,w=art[0].length,h=art.length,tail=Math.floor(clock/220+p.phase)%2;art.forEach((row,j)=>[...row].forEach((v,i)=>{if(v!=='.')rect(x+(d===1?i-w/2:w/2-i-1)*2,y+(j-h/2+(i<4?tail:0))*2,2,2,v==='#'?'#000':'#fff');}));}
function moveFish(dt){for(const id of ['base0','base1','base2',...sim.tasks.filter(t=>!t.done).map(t=>t.id)]){const p=pose(id);if(id===sim.returning){const k=Math.min(sim.returnElapsed/900,1),e=k*k*(3-2*k);p.x=262-62*e;p.y=38+57*e;p.vx=-Math.abs(p.vx);continue;}if(id===sim.current||(id===sim.candidate&&sim.phase!=='hook')||(id===sim.candidate&&sim.elapsed>=DROP_MS+WAIT_MS))continue;p.x+=p.vx*dt;p.y+=(p.vy+Math.sin(clock/1800+p.phase)*5)*dt;if(p.x<25){p.x=25;p.vx=Math.abs(p.vx);}if(p.x>275){p.x=275;p.vx=-Math.abs(p.vx);}if(p.y<65){p.y=65;p.vy=Math.abs(p.vy)+2;}if(p.y>249){p.y=249;p.vy=-Math.abs(p.vy)-2;}}}
function water(){for(let x=0;x<300;x+=4)rect(x,12+Math.round(Math.sin(clock/900+x/20)),4,1);}
function plants(){for(const [x,h,phase] of [[34,59,0],[48,80,1],[58,43,3],[248,50,4],[265,67,2]]){let px=x,py=375;for(let j=1;j<=h;j+=3){const xx=x+Math.sin(j/13+clock/2200+phase)*4;line(px,py,xx,375-j);if(j%9===1){const d=j%18===1?1:-1;line(xx,375-j,xx+d*8,369-j);line(xx+d*8,369-j,xx+d*2,371-j);}px=xx;py=375-j;}}for(let x=12;x<288;x+=2)rect(x,377+Math.round(Math.sin(x/21)*2),2,1);}

function cage(){
 // Tiny keeper hangs from the waterline, rather than sitting on the tank floor.
 border(237,15,51,45);rect(239,17,47,40,'#fff');
 for(let x=241;x<286;x+=7)line(x,18,x,57);for(let y=22;y<58;y+=7)line(239,y,285,y);
 if(sim.current!==null&&['idle','complete'].includes(sim.phase)){if(sim.phase==='complete'){const k=Math.min(1,sim.elapsed/1300);if(k<.8)fish(262,38-k*30,1,sim.current);for(let i=0;i<3;i++)border(250+i*9,30-k*20+i*3,3,3);}else fish(262+Math.sin(clock/1800)*2,38,Math.cos(clock/3500)>0?1:-1,sim.current);}
 border(232,8,61,10);rect(236,10,53,5,'#fff');
}
function taskLines(task,size,width){ctx.font=`${size}px "PingFang SC","Microsoft YaHei",sans-serif`;return wrap(task.text,s=>ctx.measureText(s).width,width);}
function plaque(){rect(134,371,3,9);rect(268,371,3,9);border(119,273,169,99);const task=sim.currentTask;
 banYuLogo.forEach((row,y)=>[...row].forEach((p,x)=>{if(p!=='.'&&x%2===0&&y%2===0)rect(129+x/2,280+y/2,1,1,p==='#'?'#000':'#fff');}));
 text(task?'正在做':'钓一件事来做',161,282,12);rect(129,300,149,1);
 if(task){const lines=taskLines(task,18,149);pages=Array.from({length:Math.ceil(lines.length/2)},(_,i)=>lines.slice(i*2,i*2+2));sim.page%=pages.length;pages[sim.page].forEach((s,i)=>text(s,129,306+i*23,18));if(pages.length>1)text(`${sim.page+1}/${pages.length} · 翻页看全文`,129,356,10);}
 else{pages=[];text('鱼在游，事不急。',129,310,16);text('钓到哪件就做哪件',129,334,14);}
}
function reveal(){const task=sim.currentTask;if(!task)return;border(20,100,260,134);text(`钓到了 · 今日${task.category}`,33,113,14);rect(33,136,234,1);const lines=taskLines(task,20,234);lines.slice(0,3).forEach((s,i)=>text(s,33,147+i*24,20));if(lines.length>3)text('完整原文已写到任务牌，可翻页查看',33,219,10);}
function draw(){rect(0,0,300,400,'#fff');water();plants();
 for(const id of ['base0','base1','base2',...sim.tasks.filter(t=>!t.done&&t.id!==sim.current&&t.id!==sim.returning&&(t.id!==sim.candidate||sim.phase==='hook'&&sim.elapsed<DROP_MS+WAIT_MS)).map(t=>t.id)]){const p=pose(id);fish(p.x,p.y,p.vx>=0?1:-1,id);}
 for(let i=0;i<4;i++){const y=360-((clock/(50+i*7)+i*40)%335);border(12+i*71,y,3,4);}
 food=food.filter(p=>clock-p.at<15000);for(const p of food){const age=(clock-p.at)/1000;if(age<0)continue;const y=-6+age*28;if(y>=377)continue;const x=p.x+Math.sin(age*2+p.x)*3;border(x,y,p.size,p.size);}
 cage();if(sim.returning!==null){const p=pose(sim.returning);fish(p.x,p.y,-1,sim.returning);}plaque();
 if(sim.phase==='hook'||sim.phase==='lift'){
  const y=sim.phase==='lift'?144-155*Math.min(sim.elapsed/900,1):-8+152*Math.min(sim.elapsed/DROP_MS,1);
  line(142,0,142,y);line(142,y,142,y+7);line(142,y+7,134,y+7);line(134,y+7,134,y+1);
  if(sim.candidate!==null&&(sim.phase==='lift'||sim.elapsed>=DROP_MS+WAIT_MS)){
   const p=pose(sim.candidate);approachFrom??={x:p.x,y:p.y};const k=sim.phase==='lift'?1:Math.min(1,(sim.elapsed-DROP_MS-WAIT_MS)/APPROACH_MS);const ease=k*k*(3-2*k),targetX=142-fishKinds[p.kind].art[0].length;
   fish(approachFrom.x+(targetX-approachFrom.x)*ease,approachFrom.y+(y+5-approachFrom.y)*ease,1,sim.candidate);
  }
 }
 if(sim.phase==='settle'){const p=Math.min(1,sim.elapsed/1200);fish(125+(262-125)*p,8+(38-8)*p+Math.sin(p*Math.PI)*22,1,sim.current);}
 if(sim.phase==='reveal')reveal();
 const frame=ctx.getImageData(0,0,300,400);for(let i=0;i<frame.data.length;i+=4){const v=frame.data[i]<160?0:255;frame.data[i]=frame.data[i+1]=frame.data[i+2]=v;frame.data[i+3]=255;}ctx.putImageData(frame,0,0);update();
}
function update(){
 $('notice').textContent=sim.notice;$('rod').textContent=sim.phase==='hook'?'S 极 · 拿开鱼竿，提钩':'S 极 · 靠近鱼竿 ↘';$('rod').disabled=!['idle','hook'].includes(sim.phase)||(sim.phase==='hook'&&sim.elapsed<BITE_MS);
 $('page').disabled=pages.length<=1;$('complete').disabled=sim.phase!=='idle'||sim.current===null;$('undo').disabled=sim.completed===null||sim.phase!=='idle';$('feed').disabled=sim.phase!=='idle';
 const active=sim.tasks.filter(t=>!t.done);$('fish-count').textContent=`3 常驻 + ${active.length} 任务 · ${sim.eligible.length} 可抽`;
 for(const [id,active] of [['step1',['hook','lift','reveal'].includes(sim.phase)],['step2',sim.current!==null&&sim.phase!=='complete'],['step3',sim.phase==='complete']])$(id).classList.toggle('active',active);
 const signature=JSON.stringify([sim.tasks,sim.current]);if($('tasks').dataset.signature!==signature){$('tasks').dataset.signature=signature;$('tasks').replaceChildren(...sim.tasks.map(t=>{const li=document.createElement('li'),label=document.createElement('span');label.textContent=t.category;li.append(label,document.createTextNode(t.text));li.className=t.done?'done':sim.current===t.id?'current':'';return li;}));}
 canvas.setAttribute('aria-label',`3条常驻鱼，${active.length}条任务鱼。${sim.currentTask?'鱼护里正在做：'+sim.currentTask.text:sim.candidateTask?'正在钓鱼':'鱼护空着'}。`);
}
function reset(){sim.reset(Number($('count').value));if($('length').value==='long')for(const t of sim.tasks)t.text='整理下一次讨论需要的材料，核对修改意见并补充操作说明\n确认后同步给相关人员。';food=[];poses.clear();approachFrom=null;draw();}
$('rod').onclick=()=>{if(sim.phase==='hook')sim.lift();else{sim.cast();approachFrom=null;}draw();};$('complete').onclick=()=>{sim.complete();draw();};$('undo').onclick=()=>{sim.undo();draw();};$('page').onclick=()=>{sim.page=(sim.page+1)%Math.max(1,pages.length);draw();};$('reset').onclick=reset;$('count').onchange=reset;$('length').onchange=reset;$('actual').onchange=()=>document.body.classList.toggle('actual',$('actual').checked);
$('feed').onclick=()=>{const kept=sim.current;if(!sim.feed())return;if(kept!==null)Object.assign(pose(kept),{x:262,y:38,vx:-18});for(let i=0;i<12;i++)food.push({x:45+(i*19)%165,size:[4,6,8][i%3],at:clock+(i%6)*80});if(kept===null)sim.notice='鱼粮从屏幕最上方落入水面。本页先确认布局，追食后续合并。';draw();};
function loop(now){const dt=Math.min(100,now-previous);previous=now;clock+=dt;sim.step(dt);moveFish(dt/1000);if(now-drawAt>=83){drawAt=now;draw();}requestAnimationFrame(loop);}draw();requestAnimationFrame(loop);

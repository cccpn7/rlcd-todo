export const DROP_MS=800, WAIT_MS=2000, APPROACH_MS=1100;
export const BITE_MS=DROP_MS+WAIT_MS+APPROACH_MS;
const samples=['整理评审材料','确认下一版界面方案','归档本周资料','回收样品测试结果','预约设备测试时间','检查网页提示文案','补充操作说明','确认寄送进度','整理照片','清理测试记录','准备讨论提纲','核对清单','整理附件','确认时间安排','更新说明'];
export class Fishing {
 constructor(count=8){this.reset(count);}
 reset(count){this.tasks=Array.from({length:count},(_,i)=>({id:i,text:samples[i%samples.length],category:i%4===3?'追踪':i%2?'杂项':'重点',done:false}));this.returning=null;this.returnElapsed=0;this.current=null;this.candidate=null;this.phase='idle';this.rod=false;this.elapsed=0;this.page=0;this.completed=null;this.notice='鱼护空着，钓一件事来做。';}
 get eligible(){return this.tasks.filter(t=>!t.done&&t.category!=='追踪'&&t.id!==this.current);}
 get currentTask(){return this.tasks.find(t=>t.id===this.current);}
 get candidateTask(){return this.tasks.find(t=>t.id===this.candidate);}
 cast(random=Math.random){
  if(this.phase!=='idle')return;
  if(this.current!==null){this.notice='鱼护里已经有一件正在做，完成后再钓下一件。';return;}
  const pool=this.eligible;if(!pool.length){this.notice='暂时没有可钓的事项。';return;}
  this.candidate=pool[Math.floor(random()*pool.length)].id;this.phase='hook';this.rod=true;this.elapsed=0;this.notice='鱼钩正在下落，稍等小鱼咬钩。';
 }
 lift(){if(this.phase==='hook'&&this.elapsed>=BITE_MS){this.rod=false;this.phase='lift';this.elapsed=0;this.notice='提竿，看看抽到了什么。';}}

 release(){if(this.current===null&&this.candidate===null)return;this.current=this.candidate=null;this.phase='idle';this.rod=false;this.page=0;this.notice='鱼已放回，任务仍未完成。';}
 feed(){if(this.phase!=='idle')return false;if(this.current!==null){this.returning=this.current;this.returnElapsed=0;this.release();this.notice='鱼已游回鱼缸，牌子复位；任务仍未完成，可以再次钓取。';}return true;}
 complete(){if(this.phase!=='idle'||this.current===null)return;this.completed=this.current;this.currentTask.done=true;this.phase='complete';this.elapsed=0;this.notice='模拟收到大圣／网页的完成同步。';}
 undo(){if(this.completed===null||this.phase!=='idle')return;this.tasks.find(t=>t.id===this.completed).done=false;this.completed=null;this.notice='模拟撤销完成：小鱼回到游动区。';}
 step(ms){
  this.elapsed+=ms;if(this.returning!==null){this.returnElapsed+=ms;if(this.returnElapsed>=900)this.returning=null;}
  if(this.phase==='hook')this.notice=this.elapsed<DROP_MS?'鱼钩从水面慢慢垂下。':this.elapsed<DROP_MS+WAIT_MS?'先等一会儿，让鱼儿再游两秒。':this.elapsed<BITE_MS?'有一条鱼游过来了……':'咬钩了，拿开鱼竿来揭晓。';
  if(this.phase==='lift'&&this.elapsed>=900){this.current=this.candidate;this.candidate=null;this.phase='reveal';this.elapsed=0;this.page=0;this.notice='钓到什么，就做什么。任务已写在小牌子上。';}
  if(this.phase==='reveal'&&this.elapsed>=2200){this.phase='settle';this.elapsed=0;}
  if(this.phase==='settle'&&this.elapsed>=1200){this.phase='idle';this.elapsed=0;}
  if(this.phase==='complete'&&this.elapsed>=1300){this.current=null;this.phase='idle';this.page=0;this.notice='完成一件，鱼护又空出来了。';}
 }
}
export function wrap(text,measure,width){const lines=[];let line='';for(const ch of text){if(ch==='\n'){lines.push(line);line='';continue;}if(line&&measure(line+ch)>width){lines.push(line);line=ch;}else line+=ch;}lines.push(line);return lines;}

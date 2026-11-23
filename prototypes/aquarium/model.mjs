// Original interaction model for the browser prototype. No device or network access.
// Coordinates describe body centers; changing direction never teleports the body.
export const FISH_HALF_WIDTH = 28;
export const FISH_HALF_HEIGHT = 16;
export const MOUTH_OFFSET = 26;

// A small visibility graph routes the body around the other two fish.
// Inflated rectangles allow a body-sized route, rather than steering only a point.
function route(start,goal,others) {
  const insideTank=p=>p.x>=30&&p.x<=270&&p.y>=62&&p.y<=350;
  if(!insideTank(goal))return null;
  const boxes=others.map(f=>({left:f.x-56,right:f.x+56,top:f.y-32,bottom:f.y+32}));
  const inside=(p,b)=>p.x>b.left&&p.x<b.right&&p.y>b.top&&p.y<b.bottom;
  if(boxes.some(b=>inside(goal,b)))return null;
  const clear=(a,b)=>!boxes.some(r=>{
    let enter=0,leave=1;
    for(const [v,d,lo,hi] of [[a.x,b.x-a.x,r.left+0.001,r.right-0.001],[a.y,b.y-a.y,r.top+0.001,r.bottom-0.001]]) {
      if(Math.abs(d)<1e-9){if(v<=lo||v>=hi)return false;continue;}
      const t1=(lo-v)/d,t2=(hi-v)/d;
      enter=Math.max(enter,Math.min(t1,t2));leave=Math.min(leave,Math.max(t1,t2));
      if(enter>=leave)return false;
    }
    return enter<leave;
  });
  if(clear(start,goal))return goal;
  const nodes=[start,goal];
  for(const b of boxes)for(const x of [b.left-0.5,b.right+0.5])for(const y of [b.top-0.5,b.bottom+0.5]) {
    const p={x,y};if(insideTank(p)&&!boxes.some(r=>inside(p,r)))nodes.push(p);
  }
  const cost=nodes.map(()=>Infinity),parent=nodes.map(()=>-1),visited=new Set();cost[0]=0;
  for(let pass=0;pass<nodes.length;pass++) {
    let u=-1;for(let i=0;i<nodes.length;i++)if(!visited.has(i)&&(u<0||cost[i]<cost[u]))u=i;
    if(u<0||!Number.isFinite(cost[u]))return null;
    if(u===1){while(parent[u]!==0)u=parent[u];return nodes[u];}
    visited.add(u);
    for(let v=0;v<nodes.length;v++)if(!visited.has(v)&&clear(nodes[u],nodes[v])) {
      const next=cost[u]+Math.hypot(nodes[u].x-nodes[v].x,nodes[u].y-nodes[v].y);
      if(next<cost[v]){cost[v]=next;parent[v]=u;}
    }
  }
  return null;
}
export class Aquarium {
  constructor() {
    this.mode = 'aquarium'; this.now = 0; this.near = false; this.armed = false;
    this.edgeAt = 0; this.deadline = 0; this.page = 0; this.pageAt = 0;
    this.food = []; this.eaten = 0; this.feedAt = 0; this.finishAt = null;
    this.fish = [
      { x: 77, y: 125, direction: 1, phase: 0 },
      { x: 215, y: 208, direction: -1, phase: 2.1 },
      { x: 112, y: 292, direction: 1, phase: 4.2 },
    ];
  }
  presence(near) {
    if (near !== this.near) { this.near = near; this.edgeAt = this.now; }
  }
  feed() {
    if (this.mode === 'feeding') return false;
    this.mode = 'feeding'; this.armed = false; this.feedAt = this.now;
    this.finishAt = null; this.eaten = 0; this.deadline = 0;
    this.fish.forEach(f=>{f.target=null;});
    this.food = Array.from({ length: 24 }, (_, i) => ({
      x: 150 + (i * 17 % 55) - 27, originX: 150 + (i * 17 % 55) - 27,
      y: 48,
      size: [8, 4, 10, 6, 7, 5, 9, 4][i % 8], shape: i % 4,
      spread: (i * 29 % 111) - 55, phase: i * 2.4,
      release: this.now + Math.floor(i / 6) * 520 + (i % 6) * 55, eaten: false,
    }));
    return true;
  }
  home() { if (this.mode === 'todo') { this.page = 0; this.pageAt = this.now; } }
  turn(delta) {
    if (this.mode !== 'todo') return;
    this.page = (this.page + delta + 3) % 3; this.pageAt = this.now;
  }
  returnToTank() {
    this.mode = 'aquarium'; this.food = []; this.armed = false;
    this.fish.forEach(f=>{f.target=null;});
    this.edgeAt = this.now; // Release must be observed after returning, not during the lockout.
  }
  constrainFish(f) {
    f.x=Math.max(30,Math.min(270,f.x));
    f.y=Math.max(62,Math.min(350,f.y));
  }
  separateFish() {
    // Resolve the complete silhouette bounds, including tail and fins.
    // Multiple passes handle three-way contacts and contact near the tank walls.
    for(let pass=0;pass<12;pass++) {
      this.fish.forEach(f=>this.constrainFish(f));
      for(let i=0;i<this.fish.length;i++)for(let j=i+1;j<this.fish.length;j++) {
        const a=this.fish[i],b=this.fish[j],dx=b.x-a.x,dy=b.y-a.y;
        const overlapX=FISH_HALF_WIDTH*2-Math.abs(dx);
        const overlapY=FISH_HALF_HEIGHT*2-Math.abs(dy);
        if(overlapX<=0||overlapY<=0)continue;
        if(overlapX<overlapY) {
          const push=(overlapX+0.02)/2*(dx<0?-1:1);
          a.x-=push;b.x+=push;
        } else {
          const push=(overlapY+0.02)/2*(dy<0?-1:1);
          a.y-=push;b.y+=push;
        }
      }
    }
    this.fish.forEach(f=>this.constrainFish(f));
  }
  step(ms) {
    this.now += ms;
    if (this.mode === 'todo') {
      if (this.now >= this.deadline) this.returnToTank();
      else if (this.now - this.pageAt >= 5000) {
        this.page = (this.page + Math.floor((this.now - this.pageAt) / 5000)) % 3;
        this.pageAt += Math.floor((this.now - this.pageAt) / 5000) * 5000;
      }
    }
    if (this.mode !== 'feeding') {
      if (!this.near && this.now - this.edgeAt >= 500) this.armed = true;
      if (this.near && this.armed && this.now - this.edgeAt >= 150) this.feed();
    } else { this.armed = false; this.edgeAt = this.now; }
    if (this.mode === 'todo') return;
    const dt = Math.min(ms, 40) / 1000;
    for (const p of this.food) {
      if (!p.eaten && this.now >= p.release) {
        const age=(this.now-p.release)/1000;
        p.x=p.originX+p.spread*(1-Math.exp(-age/0.9))
          +5*(Math.sin(age*2+p.phase)-Math.sin(p.phase));
        p.y=Math.min(330,48+age*(12+p.size*0.55));
      }
    }
    this.fish.forEach((f, i) => {
      if(f.biteAt && this.now-f.biteAt<650)return;
      let p=f.target;
      const others=this.fish.filter(other=>other!==f);
      let waypoint=p&&!p.eaten?route(f,{x:p.x-MOUTH_OFFSET*f.direction,y:p.y},others):null;
      if(!waypoint){p=null;f.target=null;}
      if(!p||p.eaten) {
        const candidates=this.food.filter(c=>!c.eaten&&this.now-c.release>=700&&!others.some(other=>other.target===c))
          .sort((a,b)=>Math.hypot(a.x-f.x,a.y-f.y)-Math.hypot(b.x-f.x,b.y-f.y));
        for (const candidate of candidates) {
          const preferred=candidate.x>=f.x?1:-1;
          for(const direction of [preferred,-preferred]) {
            waypoint=route(f,{x:candidate.x-MOUTH_OFFSET*direction,y:candidate.y},others);
            if(waypoint){p=candidate;f.direction=direction;break;}
          }
          if(p)break;
        }
        f.target=p;
      }
      if (p) {
        const dx=waypoint.x-f.x,dy=waypoint.y-f.y,d=Math.hypot(dx,dy);
        const move=Math.min(d,60*dt);
        if(d>0){f.x+=dx/d*move;f.y+=dy/d*move;}
      } else {
        f.x += f.direction * (15 + i * 3) * dt;
        f.y += Math.sin(this.now / 1800 + f.phase) * dt * 7;
        if (f.x > 265) f.direction = -1;
        if (f.x < 35) f.direction = 1;
      }
    });
    this.separateFish();
    // Only the mouth eats, and only after contacts have been resolved.
    for(const f of this.fish) {
      const p=f.target;
      if(!p||p.eaten||(f.biteAt && this.now-f.biteAt<650))continue;
      if(Math.hypot(p.x-(f.x+MOUTH_OFFSET*f.direction),p.y-f.y)<p.size/2+2) {
        p.eaten=true;this.eaten++;f.biteAt=this.now;f.target=null;
      }
    }
    if (this.mode === 'feeding' && this.eaten === this.food.length) {
      this.finishAt ??= this.now + 900;
      if (this.now >= this.finishAt) {
        this.mode = 'todo'; this.page = 0; this.pageAt = this.now;
        this.deadline = this.now + 180000;
      }
    }
  }
}

import test from 'node:test';
import assert from 'node:assert/strict';
import { Aquarium, MOUTH_OFFSET } from './model.mjs';
function advance(t,ms){for(let i=0;i<ms;i+=20)t.step(Math.min(20,ms-i));}
function feed(t){advance(t,500);t.presence(true);advance(t,160);assert.equal(t.mode,'feeding');}
function assertBodies(t) {
  for(const f of t.fish)assert.ok(f.x>=30&&f.x<=270&&f.y>=62&&f.y<=350,'fish stays inside tank');
  for(let i=0;i<3;i++)for(let j=i+1;j<3;j++) {
    const a=t.fish[i],b=t.fish[j];
    assert.ok(Math.abs(a.x-b.x)>=55.95||Math.abs(a.y-b.y)>=31.95,'full fish silhouettes do not overlap');
  }
}
function finishMeal(t) {
  const start=t.feedAt,lastBites=new Map();
  while(t.mode==='feeding'&&t.now-start<45000) {
    const previous=t.food.filter(p=>p.eaten).length;
    t.step(20);assertBodies(t);
    if(t.eaten>previous)for(const f of t.fish)if(f.biteAt===t.now) {
      const previousBite=lastBites.get(f);
      if(previousBite!==undefined)assert.ok(t.now-previousBite>=650,'separate bites have a chewing pause');
      lastBites.set(f,t.now);
      assert.ok(t.food.some(p=>p.eaten&&Math.hypot(p.x-f.x-MOUTH_OFFSET*f.direction,p.y-f.y)<p.size/2+2),'mouth touches food');
    }
  }
  assert.equal(t.mode,'todo','fish finish feeding without getting stuck');
  assert.ok(t.food.every(p=>p.eaten));
}

test('stable release and approach; slower feeding finishes only after every pellet is eaten',()=>{
  const t=new Aquarium();t.presence(true);advance(t,2000);assert.equal(t.mode,'aquarium');
  t.presence(false);advance(t,500);t.presence(true);advance(t,140);assert.equal(t.mode,'aquarium');
  advance(t,20);const start=t.now;
  finishMeal(t);
  assert.equal(t.mode,'todo');assert.equal(t.eaten,t.food.length);assert.ok(t.food.every(p=>p.eaten));
  assert.ok(t.now-start>=10000);
});
test('viewing lasts exactly 180 seconds; manual paging does not extend it',()=>{
  const t=new Aquarium();feed(t);finishMeal(t);const deadline=t.deadline;
  t.turn(-1);t.home();assert.equal(t.deadline,deadline);
  advance(t,deadline-t.now-1);assert.equal(t.mode,'todo');t.step(1);assert.equal(t.mode,'aquarium');
});
test('held magnet, ignored gestures and return do not queue feeding',()=>{
  const t=new Aquarium();feed(t);finishMeal(t);
  advance(t,1000);assert.equal(t.mode,'todo');
  advance(t,t.deadline-t.now);advance(t,2000);assert.equal(t.mode,'aquarium');
  t.presence(false);advance(t,500);t.presence(true);advance(t,160);assert.equal(t.mode,'feeding');
});
test('all three pages cycle at five seconds, including the empty category',()=>{
  const t=new Aquarium();feed(t);finishMeal(t);
  assert.equal(t.page,0);advance(t,5000);assert.equal(t.page,1);advance(t,5000);assert.equal(t.page,2);advance(t,5000);assert.equal(t.page,0);
});
test('thirty starting arrangements finish feeding without overlap or escape',()=>{
  for(let trial=0;trial<30;trial++){
    const t=new Aquarium();advance(t,trial*997);feed(t);finishMeal(t);
    assert.equal(t.mode,'todo');assert.equal(t.eaten,t.food.length);
    assert.ok(t.fish.every(f=>f.x>=0&&f.x<300&&f.y>=0&&f.y<400));
  }
});

test('overlapping bodies at the tank wall separate without leaving the water',()=>{
  const t=new Aquarium();
  for(const f of t.fish){f.x=30;f.y=62;}
  advance(t,200);assertBodies(t);
});

test('Todo accepts a fresh magnet approach or KEY and restarts viewing after the new meal',()=>{
  for(const key of [false,true]){
    const t=new Aquarium();feed(t);finishMeal(t);const oldDeadline=t.deadline;
    advance(t,5000);assert.equal(t.mode,'todo');
    if(key)assert.equal(t.feed(),true);
    else{t.presence(false);advance(t,500);t.presence(true);advance(t,160);}
    assert.equal(t.mode,'feeding');assert.equal(t.deadline,0);assert.equal(t.eaten,0);
    const at=t.feedAt;assert.equal(t.feed(),false);assert.equal(t.feedAt,at);
    finishMeal(t);assert.equal(t.deadline,t.now+180000);assert.ok(t.deadline>oldDeadline);
    advance(t,1000);assert.equal(t.mode,'todo');
  }
});

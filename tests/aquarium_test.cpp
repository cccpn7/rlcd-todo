#include "../firmware/main/aquarium.hpp"
#include <cassert>
#include <cstdio>
static void bodies(const tank::Aquarium &a){for(auto f:a.fish)assert(f.x>=30&&f.x<=270&&f.y>=62&&f.y<=350);for(int i=0;i<3;i++)for(int j=i+1;j<3;j++)assert(std::abs(a.fish[i].x-a.fish[j].x)>=55.9f||std::abs(a.fish[i].y-a.fish[j].y)>=31.9f);}
static void sense(tank::Aquarium &a,bool near,int64_t from,int64_t to){for(auto t=from;t<=to;t+=20)a.presence(near,true,t);}
int main(){
 int64_t longest=0;
 for(int trial=0;trial<100;trial++){
  tank::Aquarium a;int64_t t=0;for(;t<trial*997+500;t+=20)a.step(t);
  a.presence(true,true,t);a.presence(true,true,t+500);assert(a.mode==tank::Aquarium::Idle);
  sense(a,false,t+520,t+1020);sense(a,true,t+1040,t+1180);assert(a.mode==tank::Aquarium::Idle);a.presence(true,true,t+1200);assert(a.mode==tank::Aquarium::Feeding);
  t+=1200;auto start=t;int64_t bites[3]={};
  while(a.mode==tank::Aquarium::Feeding&&t-start<45000){t+=20;a.step(t);bodies(a);for(int i=0;i<3;i++){auto f=a.fish[i];if(f.bite==t){if(bites[i])assert(t-bites[i]>=650);bites[i]=t;bool contact=false;for(auto p:a.food)if(p.eaten&&tank::distance({f.x+26*f.direction,f.y},p)<p.size/2.f+2)contact=true;assert(contact);}}}
  assert(a.mode==tank::Aquarium::Todo&&a.eaten==24);longest=std::max(longest,t-start);
  assert(a.deadline==0);a.shown(t+100);auto deadline=a.deadline;a.shown(t+400);assert(a.deadline==deadline);
  a.presence(false,true,t+500);a.presence(true,true,t+1000);assert(a.mode==tank::Aquarium::Todo);
  a.step(deadline-1);assert(a.mode==tank::Aquarium::Todo);a.step(deadline);assert(a.mode==tank::Aquarium::Idle);
  a.presence(true,true,deadline+1000);a.presence(true,true,deadline+3000);assert(a.mode==tank::Aquarium::Idle);
  sense(a,false,deadline+3020,deadline+3520);sense(a,true,deadline+3540,deadline+3700);assert(a.mode==tank::Aquarium::Feeding);
 }
 // A new approach in Todo starts a new meal, but a held magnet never loops.
 for(bool button:{false,true}){
  tank::Aquarium a;int64_t t=1000;assert(a.feed(t));
  while(a.mode==tank::Aquarium::Feeding){t+=20;a.presence(true,true,t);a.step(t);}
  a.shown(t);auto old_deadline=a.deadline;
  sense(a,true,t+20,t+1020);assert(a.mode==tank::Aquarium::Todo);
  t+=1040;
  if(button){assert(a.feed(t));}
  else{sense(a,false,t,t+500);sense(a,true,t+520,t+680);t+=680;}
  assert(a.mode==tank::Aquarium::Feeding&&a.deadline==0&&a.eaten==0);
  auto feed_at=a.feed_at;assert(!a.feed(t+20));assert(a.feed_at==feed_at);
  while(a.mode==tank::Aquarium::Feeding){t+=20;a.presence(true,true,t);a.step(t);bodies(a);}
  assert(a.eaten==24&&a.deadline==0);a.shown(t+100);
  assert(a.deadline==t+100+180000&&a.deadline>old_deadline);
  sense(a,true,t+120,t+1120);assert(a.mode==tank::Aquarium::Todo);
 }
 // Slow rendering must not turn a sampling gap into a valid approach.
 {
  tank::Aquarium a;a.mode=tank::Aquarium::Todo;
  for(int64_t t=0;t<2000;t+=500)a.presence(false,true,t);
  assert(!a.armed&&a.presence_gaps==3&&a.max_presence_gap==500);
  sense(a,false,1520,2020);assert(a.armed);
  a.presence(true,true,2200);assert(!a.armed&&a.mode==tank::Aquarium::Todo);
  sense(a,false,2220,2720);sense(a,true,2740,2900);
  assert(a.mode==tank::Aquarium::Feeding);
 }
 tank::HallFilter h;h.sample(2000,true);assert(!h.near&&!h.calibrated);h.configure(2000,200,80);
 for(int i=0;i<30;i++)h.sample(2400,true);assert(h.near);
 h.sample(-1,false);assert(!h.valid&&!h.near);
 for(int i=0;i<30;i++)h.sample(1600,true);assert(h.near);
 for(int i=0;i<30;i++)h.sample(2000,true);assert(!h.near);
 printf("100 meals and collision checks passed; longest %.2fs; Hall polarity/fault checks passed\n",longest/1000.);
}

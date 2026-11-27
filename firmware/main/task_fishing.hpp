#pragma once
#include "aquarium.hpp"
#include "text_layout.hpp"
#include "task_fish_assets.hpp"
#include <string>
#include <vector>

namespace tank {
// Current-task state is device-owned. Canonical publications still own completion.
class Fishing {
public:
 enum Phase { Swim, Hook, Lift, Commit, Reveal, Settle } phase=Swim;
 static constexpr int DropMs=800,WaitMs=2000,ApproachMs=1100,BiteMs=3900;
 struct TaskFish : Fish { std::string id; uint8_t category=0,kind=0; };
 std::vector<TaskFish> fish;
 TaskFish departing;
 int64_t departed_at=-2000;
 std::string current,candidate,returning;
 int64_t return_at=0;
 int64_t now=0,started=0,notice_until=0;
 std::string notice;
 std::vector<uint32_t> notice_chars;
 bool magnetic_cast=false;
 unsigned plaque_page=0;
 std::vector<std::vector<uint32_t>> plaque_lines;
 void prepare_text(const rtd::TextDocument &doc,const rtd::Font &font){
  plaque_lines.clear();if(current.empty())return;
  for(auto &item:doc.items)if(item.id==current){
   plaque_lines.push_back({});uint32_t advance=0;
   for(auto cp:rtd::codes(item.text)){
    if(cp=='\n'){plaque_lines.push_back({});advance=0;continue;}
    rtd::Glyph g;if(!font.get(cp,18,g))continue;
    if(!plaque_lines.back().empty()&&advance+g.advance>149*64){plaque_lines.push_back({});advance=0;}
    plaque_lines.back().push_back(cp);advance+=g.advance;
   }break;
  }
  plaque_page%=std::max<size_t>(1,(plaque_lines.size()+1)/2);
 }
 Point approach;
 bool approach_set=false;
 bool armed=false;
 int64_t max_presence_gap=0;
 unsigned presence_gaps=0;

 static uint32_t hash(const std::string &id){uint32_t h=2166136261u;for(unsigned char c:id){h^=c;h*=16777619u;}return h;}
 TaskFish *find(const std::string &id){for(auto &f:fish)if(f.id==id)return &f;return nullptr;}
 const TaskFish *find(const std::string &id)const{for(auto &f:fish)if(f.id==id)return &f;return nullptr;}
 unsigned eligible()const{unsigned n=0;for(auto &f:fish)if(f.category<2&&f.id!=current)n++;return n;}
 void message(const char *s,int64_t at){notice=s;notice_chars=rtd::codes(notice);notice_until=at+2200;}
 void reconcile(const std::vector<rtd::TextItem> &items,int64_t at){
  std::vector<TaskFish> next;next.reserve(items.size());
  for(auto &item:items){TaskFish f;if(auto prior=find(item.id))f=*prior;else{
    auto h=hash(item.id);f.id=item.id;f.x=27+h%245;f.y=66+(h>>8)%174;f.direction=h&1?1:-1;f.phase=(h%628)/100.f;f.kind=(h>>16)%6;
   }f.category=item.category;next.push_back(std::move(f));}
  if(auto previous=find(current)){
   bool present=false;for(auto &f:next)if(f.id==current)present=true;
   if(!present){departing=*previous;departed_at=at;}
  }
  fish.swap(next);
  if(!returning.empty()&&!find(returning))returning.clear();
  if(!current.empty()&&(!find(current)||find(current)->category>=2)){current.clear();plaque_page=0;message("任务已同步",at);}
  if(!candidate.empty()&&(!find(candidate)||find(candidate)->category>=2)){candidate.clear();phase=Swim;message("任务已同步",at);}
  if(current.empty()&&(phase==Reveal||phase==Settle))phase=Swim;
 }
 bool cast(int64_t at,unsigned choice,bool magnet){
  if(phase!=Swim)return false;
  if(!current.empty()){plaque_page++;message("正在做",at);return false;}
  auto n=eligible();if(!n){message("暂时没有可钓的事项",at);return false;}
  choice%=n;for(auto &f:fish)if(f.category<2&&f.id!=current&&choice--==0){candidate=f.id;break;}
  now=started=at;phase=Hook;magnetic_cast=magnet;approach_set=false;return true;
 }
 bool lift(int64_t at){
  if(phase!=Hook)return false;
  if(at-started<BiteMs){phase=Swim;candidate.clear();message("未钓到，请重试",at);return true;}
  phase=Lift;started=at;return true;
 }
 // Commit the stable ID to NVS before presenting the task as accepted.
 void committed(bool ok,int64_t at){
  if(phase!=Commit)return;
  if(!ok){phase=Swim;candidate.clear();message("保存失败，请重试",at);return;}
  current=candidate;candidate.clear();plaque_page=0;phase=Reveal;started=at;
 }
 // Both physical feeding entrances use this transaction. Never clear only RAM.
 template<class Save> bool feed(Aquarium &a,int64_t at,Save save){
  if(phase!=Swim)return false;
  if(!current.empty()){
   if(!save(std::string{})){message("保存失败，请重试",at);return false;}
   returning=current;return_at=at;
   if(auto f=find(returning)){f->x=262;f->y=38;f->direction=-1;f->target=-1;f->bite=0;}
   current.clear();plaque_page=0;plaque_lines.clear();notice.clear();notice_chars.clear();notice_until=0;
  }
  return a.feed(at);
 }
 const char *phase_name()const{
  switch(phase){case Hook:return "hook";case Lift:return "lift";case Commit:return "saving";case Reveal:return "reveal";case Settle:return "settle";default:return "swimming";}
 }
 // Signed approach with release hysteresis; invalid/gapped input never lifts.
 // random is sampled by the caller using a uniform bounded hardware RNG.
 void presence(int polarity,bool valid,int64_t at,unsigned random){
  if(last_presence>=0){auto gap=at-last_presence;max_presence_gap=std::max(max_presence_gap,gap);if(gap>100){presence_gaps++;reset_input();}}
  last_presence=at;
  if(!valid){reset_input();return;}
  if(polarity==0){near_since=-1;near_polarity=0;if(away_since<0)away_since=at;
   if(at-away_since>=500){if(!armed&&phase==Hook&&magnetic_cast)lift(at);armed=true;}
   return;
  }
  away_since=-1;
  if(polarity!=near_polarity){near_polarity=polarity;near_since=at;}
  if(armed&&at-near_since>=150){armed=false;if(polarity>0)feed_requested=true;else{rod_requested=true;cast(at,random,true);}}
 }
 bool take_rod(){bool r=rod_requested;rod_requested=false;return r;}
 bool take_feed(){bool r=feed_requested;feed_requested=false;return r;}
 void reset_input(){armed=false;near_since=away_since=-1;near_polarity=0;}
 void step(int64_t at,Aquarium &a){
  float dt=std::clamp<int64_t>(at-now,0,40)/1000.f;now=at;
  if(phase==Hook&&at-started>=DropMs+WaitMs&&!approach_set){if(auto f=find(candidate))approach=*f;approach_set=true;}
  if(phase==Lift&&at-started>=900)phase=Commit;
  if(phase==Reveal&&at-started>=3000){phase=Settle;started=at;}
  if(phase==Settle&&at-started>=1200){phase=Swim;started=at;}
  for(auto &f:fish){
   if(f.id==returning){
    float k=std::clamp((at-return_at)/900.f,0.f,1.f),ease=k*k*(3-2*k);
    f.x=262-62*ease;f.y=38+57*ease;f.direction=-1;
    if(k<1)continue;
    returning.clear();
   }
   if(f.id==current||(f.id==candidate&&(phase!=Hook||at-started>=DropMs+WaitMs)))continue;
   if(f.bite&&at-f.bite<650)continue;
   // Task fish share the existing meal. Claim a pellet once; mouth contact eats it.
   int best=-1;float best_d=1e9f;int mouth=TaskFishArt[f.kind].w;
   if(a.mode==Aquarium::Feeding)for(int k=0;k<a.food_count;k++){
    auto &p=a.food[k];if(p.eaten||p.expired||at-p.release<700||p.y<45||p.y>350)continue;
    bool claimed=false;for(auto &r:a.fish)if(r.target==k)claimed=true;
    // Stable sequential updates avoid shared bites; no fish-to-fish collisions.
    if(claimed)continue;
    float d=distance(f,p);if(d<best_d){best=k;best_d=d;}
   }
   if(best>=0){auto &p=a.food[best];f.direction=p.x>=f.x?1:-1;Point goal{std::clamp(p.x-mouth*f.direction,25.f,275.f),p.y};float d=distance(f,goal),m=std::min(d,45*dt);if(d>0){f.x+=(goal.x-f.x)*m/d;f.y+=(goal.y-f.y)*m/d;}
    if(distance({f.x+mouth*f.direction,f.y},p)<p.size/2.f+2){p.eaten=true;a.eaten++;f.bite=at;}
   }else{f.x+=f.direction*(13+f.kind*2)*dt;f.y+=std::sin(at/1800.f+f.phase)*dt*7;}
   if(f.x<25){f.x=25;f.direction=1;}if(f.x>275){f.x=275;f.direction=-1;}f.y=std::clamp(f.y,45.f,350.f);
  }
 }
private:
 int64_t last_presence=-1,near_since=-1,away_since=-1;
 int near_polarity=0;
 bool feed_requested=false,rod_requested=false;
};
}

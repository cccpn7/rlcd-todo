#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace tank {
struct Point { float x=0,y=0; };
inline float distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
struct Fish : Point { int direction=1,target=-1; float phase=0; int64_t bite=0; };
struct Food : Point { float origin=0,spread=0,phase=0; int size=0,shape=0; int64_t release=0; bool eaten=false; };
class Aquarium {
public:
 enum Mode { Idle, Feeding, Todo } mode=Idle;
 std::array<Fish,3> fish{};
 std::array<Food,24> food{};
 int eaten=0;
 int64_t now=0,feed_at=0,finish_at=0,deadline=0;
 bool armed=false;
 int64_t max_presence_gap=0;
 unsigned presence_gaps=0;
 Aquarium(){fish[0].x=77;fish[0].y=125;fish[1].x=215;fish[1].y=208;fish[1].direction=-1;fish[1].phase=2.1;fish[2].x=112;fish[2].y=292;fish[2].phase=4.2;}
 const char *name()const{return mode==Idle?"aquarium":mode==Feeding?"feeding":"todo";}
 bool feed(int64_t at){
  if(mode==Feeding)return false;
  now=at;mode=Feeding;armed=false;feed_at=at;finish_at=deadline=0;eaten=0;
  constexpr int sizes[]={8,4,10,6,7,5,9,4};
  for(int i=0;i<24;i++){auto &p=food[i];p={};p.x=p.origin=150+(i*17%55)-27;p.y=48;p.size=sizes[i%8];p.shape=i%4;p.spread=(i*29%111)-55;p.phase=i*2.4;p.release=at+(i/6)*520+(i%6)*55;}
  for(auto &f:fish)f.target=-1;
  return true;
 }
 // Only a complete physical task refresh starts the three-minute deadline.
 void shown(int64_t at){if(mode==Todo&&!deadline)deadline=at+180000;}
 void presence(bool near,bool valid,int64_t at){
  if(last_presence>=0)max_presence_gap=std::max(max_presence_gap,at-last_presence);
  if(last_presence>=0&&at-last_presence>100){presence_gaps++;armed=false;near_since=away_since=-1;}
  last_presence=at;
  if(mode==Feeding||!valid){armed=false;near_since=away_since=-1;return;}
  if(near){away_since=-1;if(near_since<0)near_since=at;if(armed&&at-near_since>=150)feed(at);}
  else{near_since=-1;if(away_since<0)away_since=at;if(at-away_since>=500)armed=true;}
 }
 void idle(int64_t at){mode=Idle;now=at;deadline=finish_at=0;armed=false;near_since=away_since=-1;for(auto &f:fish)f.target=-1;}
 void separate(){
  for(int pass=0;pass<12;pass++){
   for(auto &f:fish)bound(f);
   for(int i=0;i<3;i++)for(int j=i+1;j<3;j++){
    auto &a=fish[i],&b=fish[j];float dx=b.x-a.x,dy=b.y-a.y,ox=56-std::abs(dx),oy=32-std::abs(dy);
    if(ox<=0||oy<=0)continue;
    if(ox<oy){float p=(ox+.02f)/2*(dx<0?-1:1);a.x-=p;b.x+=p;}
    else{float p=(oy+.02f)/2*(dy<0?-1:1);a.y-=p;b.y+=p;}
   }
  }
  for(auto &f:fish)bound(f);
 }
 bool step(int64_t at){
  auto previous=mode;float dt=std::clamp<int64_t>(at-now,0,40)/1000.f;now=at;
  if(mode==Todo){if(deadline&&at>=deadline)idle(at);else return false;}
  if(mode==Feeding)for(auto &p:food)if(!p.eaten&&at>=p.release){float age=(at-p.release)/1000.f;p.x=p.origin+p.spread*(1-std::exp(-age/.9f))+5*(std::sin(age*2+p.phase)-std::sin(p.phase));p.y=std::min(330.f,48+age*(12+p.size*.55f));}
  for(int i=0;i<3;i++){
   auto &f=fish[i];if(f.bite&&at-f.bite<650)continue;
   Point waypoint;bool found=f.target>=0&&!food[f.target].eaten&&route(i,{food[f.target].x-26*f.direction,food[f.target].y},waypoint);
   if(!found){
    f.target=-1;
    std::array<int,24> candidates{};int count=0;
    if(mode==Feeding)for(int k=0;k<24;k++){
     auto &p=food[k];bool claimed=false;for(int j=0;j<3;j++)if(j!=i&&fish[j].target==k)claimed=true;
     if(!p.eaten&&at-p.release>=700&&!claimed)candidates[count++]=k;
    }
    std::sort(candidates.begin(),candidates.begin()+count,[&](int a,int b){return distance(f,food[a])<distance(f,food[b]);});
    for(int k=0;k<count&&!found;k++){
     auto &p=food[candidates[k]];int preferred=p.x>=f.x?1:-1;
     for(int d:{preferred,-preferred})if(route(i,{p.x-26*d,p.y},waypoint)){f.direction=d;f.target=candidates[k];found=true;break;}
    }
   }
   if(found){float d=distance(f,waypoint),move=std::min(d,60*dt);if(d>0){f.x+=(waypoint.x-f.x)/d*move;f.y+=(waypoint.y-f.y)/d*move;}}
   else{f.x+=f.direction*(15+i*3)*dt;f.y+=std::sin(at/1800.f+f.phase)*dt*7;if(f.x>265)f.direction=-1;if(f.x<35)f.direction=1;}
  }
  separate();
  if(mode==Feeding){
   for(auto &f:fish)if(f.target>=0&&(!f.bite||at-f.bite>=650)){
    auto &p=food[f.target];if(!p.eaten&&distance({f.x+26*f.direction,f.y},p)<p.size/2.f+2){p.eaten=true;eaten++;f.bite=at;f.target=-1;}
   }
   if(eaten==24){if(!finish_at)finish_at=at+900;if(at>=finish_at){mode=Todo;deadline=0;}}
  }
  return previous!=mode;
 }
private:
 int64_t near_since=-1,away_since=-1,last_presence=-1;
 static bool inside(Point p){return p.x>=30&&p.x<=270&&p.y>=62&&p.y<=350;}
 static void bound(Fish &f){f.x=std::clamp(f.x,30.f,270.f);f.y=std::clamp(f.y,62.f,350.f);}
 struct Box{float l,r,t,b;bool contains(Point p)const{return p.x>l&&p.x<r&&p.y>t&&p.y<b;}};
 bool route(int index,Point goal,Point &next)const{
  if(!inside(goal))return false;
  std::array<Box,2> boxes{};int n=0;
  for(int i=0;i<3;i++)if(i!=index)boxes[n++]={fish[i].x-56,fish[i].x+56,fish[i].y-32,fish[i].y+32};
  for(auto b:boxes)if(b.contains(goal))return false;
  auto clear=[&](Point a,Point b){
   for(auto r:boxes){float enter=0,leave=1;bool crosses=true;
    const float values[2][4]={{a.x,b.x-a.x,r.l+.001f,r.r-.001f},{a.y,b.y-a.y,r.t+.001f,r.b-.001f}};
    for(auto &v:values){if(std::abs(v[1])<1e-9){if(v[0]<=v[2]||v[0]>=v[3]){crosses=false;break;}continue;}
     float t1=(v[2]-v[0])/v[1],t2=(v[3]-v[0])/v[1];enter=std::max(enter,std::min(t1,t2));leave=std::min(leave,std::max(t1,t2));if(enter>=leave){crosses=false;break;}}
    if(crosses&&enter<leave)return false;
   }return true;
  };
  Point start=fish[index];if(clear(start,goal)){next=goal;return true;}
  std::array<Point,10> nodes{};nodes[0]=start;nodes[1]=goal;n=2;
  for(auto b:boxes)for(float x:{b.l-.5f,b.r+.5f})for(float y:{b.t-.5f,b.b+.5f}){Point p{x,y};if(inside(p)&&!boxes[0].contains(p)&&!boxes[1].contains(p))nodes[n++]=p;}
  std::array<float,10> cost;cost.fill(std::numeric_limits<float>::infinity());cost[0]=0;
  std::array<int,10> parent;parent.fill(-1);std::array<bool,10> visited{};
  for(int pass=0;pass<n;pass++){
   int u=-1;for(int i=0;i<n;i++)if(!visited[i]&&(u<0||cost[i]<cost[u]))u=i;
   if(u<0||!std::isfinite(cost[u]))return false;
   if(u==1){while(parent[u]!=0){u=parent[u];if(u<0)return false;}next=nodes[u];return true;}
   visited[u]=true;for(int v=0;v<n;v++)if(!visited[v]&&clear(nodes[u],nodes[v])){float c=cost[u]+distance(nodes[u],nodes[v]);if(c<cost[v]){cost[v]=c;parent[v]=u;}}
  }return false;
 }
};

class HallFilter {
public:
 int baseline=0,trigger=0,release=0,raw=0,filtered=0;
 bool calibrated=false,valid=false,near=false;
 void configure(int b,int t,int r){baseline=b;trigger=t;release=r;calibrated=b>0&&b<4095&&t>r&&r>0;valid=near=false;initialized=false;}
 void sample(int value,bool ok){
  raw=value;valid=ok&&value>0&&value<4095;
  if(!valid){near=false;initialized=false;return;}
  average=initialized?average*.75f+value*.25f:value;initialized=true;filtered=std::lround(average);
  if(!calibrated){near=false;return;}
  int d=std::abs(filtered-baseline);if(d>=trigger)near=true;else if(d<=release)near=false;
 }
private:bool initialized=false;float average=0;
};
} // namespace tank

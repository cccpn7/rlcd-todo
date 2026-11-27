#pragma once
#include "aquarium.hpp"
#include "aquarium_assets.hpp"
#include "task_fishing.hpp"
#include "text_layout.hpp"
namespace tank {
class Painter {
 uint8_t *out;
public:
 explicit Painter(uint8_t *p):out(p){}
 void pixel(int x,int y,bool black=true){if(x<0||x>=300||y<0||y>=400)return;auto i=y*300+x;uint8_t mask=0x80>>(i%8);if(black)out[i/8]&=~mask;else out[i/8]|=mask;}
 void rect(float x,float y,int w,int h,bool black=true){int xx=std::lround(x),yy=std::lround(y);for(int j=0;j<h;j++)for(int i=0;i<w;i++)pixel(xx+i,yy+j,black);}
 void line(float ax,float ay,float bx,float by){int x=std::lround(ax),y=std::lround(ay),ex=std::lround(bx),ey=std::lround(by);int dx=std::abs(ex-x),sx=x<ex?1:-1,dy=-std::abs(ey-y),sy=y<ey?1:-1,e=dx+dy;for(;;){pixel(x,y);if(x==ex&&y==ey)break;int v=2*e;if(v>=dy){e+=dy;x+=sx;}if(v<=dx){e+=dx;y+=sy;}}}
 void plant(float x,int height,float phase,int64_t now){float px=x,py=375;for(int j=1;j<=height;j+=3){float xx=x+std::sin(j/13.f+now/2200.f+phase)*4;line(px,py,xx,375-j);if(j%9==1){int d=j%18==1?1:-1;line(xx,375-j,xx+d*8,369-j);line(xx+d*8,369-j,xx+d*2,371-j);}px=xx;py=375-j;}}
 void ascii(const rtd::Font &font,const char *s,int x,int y,unsigned px=12){
  while(*s){rtd::Glyph g;if(!font.get(uint8_t(*s++),px,g))continue;
   for(unsigned yy=0;yy<g.h;yy++)for(unsigned xx=0;xx<g.w;xx++){unsigned i=yy*g.w+xx;if(g.bits[i/8]&(0x80>>(i%8)))pixel(x+g.x+xx,y+px+g.y+yy);}
   x+=g.advance/64;
  }
 }
 void draw(const Aquarium &a,const rtd::Font &font,const Fishing *fishing=nullptr){
  memset(out,255,rtd::Bytes);
  if(!fishing)for(int y=0;y<32;y++)for(int x=0;x<48;x++)if(Logo[y][x]!='.')pixel(12+x,6+y,Logo[y][x]=='#');
  for(int x=fishing?0:12;x<(fishing?300:288);x+=4)rect(x,(fishing?12:44)+std::lround(std::sin(x/26.f+a.now/1100.f)),4,1);
  plant(34,59,0,a.now);plant(48,80,1,a.now);plant(58,43,3,a.now);plant(248,50,4,a.now);plant(265,67,2,a.now);
  for(int i=0;i<6;i++){float x=26+(i*41)%248,y=363-std::fmod(a.now/float(45+i*8)+i*47,297.f);if(i%2){rect(x,y,3,1);rect(x-1,y+1,1,3);rect(x+3,y+1,1,3);rect(x,y+4,3,1);}else rect(x,y,2,2);}
  for(int x=12;x<288;x+=2)rect(x,377+std::lround(std::sin(x/21.f)*2),2,1);
  for(int i=0;i<23;i++)rect(18+(i*37)%269,385+(i*17)%9,i%3==0?3:1,1);
  if(a.mode==Aquarium::Feeding)for(int k=0;k<a.food_count;k++)if(auto p=a.food[k];!p.eaten&&!p.expired&&a.now>=p.release){
   int w=p.size,h=p.shape==1?p.size-2:p.size,left=std::lround(p.x-w/2.f),top=std::lround(p.y-h/2.f);
   if(p.size>=6&&p.shape!=3){float rx=w/2.f,ry=h/2.f,t=p.size>=8?2:1;for(int y=0;y<h;y++)for(int x=0;x<w;x++){float dx=x+.5f-rx,dy=y+.5f-ry;if(dx*dx/(rx*rx)+dy*dy/(ry*ry)<=1&&dx*dx/((rx-t)*(rx-t))+dy*dy/((ry-t)*(ry-t))>=1)pixel(left+x,top+y);}}
   else for(int y=0;y<h;y++){int inset=(y==0||y==h-1)?(p.shape==2?2:1):0,chip=p.shape==2&&y==1?1:0;rect(left+inset+chip,top+y,w-2*inset-chip,1);}
  }
  constexpr uint32_t names[]={0x601d,0x95ea,0x70b9};
  for(int i=0;i<3;i++){
   auto f=a.fish[i];float mouth=f.x+26*f.direction;int tail=(a.now/210+i)%2;
   for(int y=0;y<14;y++)for(int x=0;x<27;x++){char c=FishArt[y][x];if(c=='.')continue;int xx=f.direction==1?(x-26)*2:(26-x)*2-1;rect(mouth+xx,f.y+(y+(x<5?tail:0)-7)*2,2,2,c=='#');}
   rect(mouth-6*f.direction,f.y-4,1,1,false);
   rtd::Glyph glyph;
   if(font.get(names[i],14,glyph)){int x=std::lround(mouth-24*f.direction)-glyph.w/2,y=std::lround(f.y)-glyph.h/2;for(int yy=0;yy<glyph.h;yy++)for(int xx=0;xx<glyph.w;xx++){int bit=yy*glyph.w+xx;if(glyph.bits[bit/8]&(0x80>>(bit%8)))pixel(x+xx,y+yy);}}
   if(f.bite&&a.now-f.bite<350){rect(mouth+6*f.direction,f.y-7,2,2);rect(mouth+9*f.direction,f.y,2,2);rect(mouth+6*f.direction,f.y+7,2,2);}
  }
  if(fishing)draw_fishing(*fishing,font);
 }
 void border(int x,int y,int w,int h){rect(x,y,w,h,false);rect(x,y,w,2);rect(x,y+h-2,w,2);rect(x,y,2,h);rect(x+w-2,y,2,h);}
 void task_fish(const Fishing::TaskFish &f,float x,float y,int direction,int64_t at){
  auto &s=TaskFishArt[f.kind];int tail=(int(at/220)+f.kind)%2;
  for(int j=0;j<s.h;j++)for(int i=0;i<s.w;i++){char c=s.rows[j][i];if(c!='.')rect(x+(direction==1?i-s.w/2.f:s.w/2.f-i-1)*2,y+(j-s.h/2.f+(i<4?tail:0))*2,2,2,c=='#');}
 }
 void draw_fishing(const Fishing &f,const rtd::Font &font){
  for(auto &p:f.fish)if(p.id!=f.current&&p.id!=f.returning&&(p.id!=f.candidate||(f.phase==Fishing::Hook&&f.now-f.started<Fishing::DropMs+Fishing::WaitMs)))task_fish(p,p.x,p.y,p.direction,f.now);
  border(237,15,51,45);for(int x=241;x<286;x+=7)line(x,18,x,57);for(int y=22;y<58;y+=7)line(239,y,285,y);
  if(auto p=f.find(f.current);p&&f.phase==Fishing::Swim)task_fish(*p,262+std::sin(f.now/1800.f)*2,38,std::cos(f.now/3500.f)>0?1:-1,f.now);
  if(f.now>=f.departed_at&&f.now-f.departed_at<1200){float k=(f.now-f.departed_at)/1200.f;if(k<.8f)task_fish(f.departing,262,38-k*30,1,f.now);for(int i=0;i<3;i++)border(250+i*9,30-k*20+i*3,3,3);}
  if(auto p=f.find(f.returning))task_fish(*p,p->x,p->y,-1,f.now);
  border(232,8,61,10);rect(236,10,53,5,false);
  rect(134,371,3,9);rect(268,371,3,9);border(119,273,169,99);
  for(int y=0;y<32;y+=2)for(int x=0;x<48;x+=2)if(Logo[y][x]!='.')pixel(129+x/2,280+y/2,Logo[y][x]=='#');
  static const auto doing=rtd::codes("正在做"),empty=rtd::codes("钓一件事来做"),hint=rtd::codes("长按看清单"),waiting=rtd::codes("等待咬钩"),lift=rtd::codes("移开提竿"),caught=rtd::codes("钓到了"),full=rtd::codes("完整原文翻页");
  rtd::draw_text(out,font,f.current.empty()?empty:doing,161,282,12);rect(129,300,149,1);
  auto count=std::max<size_t>(1,(f.plaque_lines.size()+1)/2),page=f.plaque_page%count;
  if(f.current.empty())rtd::draw_text(out,font,hint,129,312,18);
  else for(size_t i=0;i<2;i++)if(page*2+i<f.plaque_lines.size())rtd::draw_text(out,font,f.plaque_lines[page*2+i],129,306+i*23,18);
  char label[32];snprintf(label,sizeof(label),"%u/%u",unsigned(page+1),unsigned(count));if(count>1)ascii(font,label,129,351,12);
  if(f.phase==Fishing::Hook||f.phase==Fishing::Lift||f.phase==Fishing::Commit){
   float y=f.phase==Fishing::Hook?-8+152*std::min((f.now-f.started)/800.f,1.f):144-155*std::min((f.now-f.started)/900.f,1.f);
   line(142,0,142,y);line(142,y,142,y+7);line(142,y+7,134,y+7);line(134,y+7,134,y+1);
   if(auto p=f.find(f.candidate);p&&(f.phase!=Fishing::Hook||f.now-f.started>=Fishing::DropMs+Fishing::WaitMs)){
    float k=f.phase==Fishing::Hook?std::clamp((f.now-f.started-Fishing::DropMs-Fishing::WaitMs)/1100.f,0.f,1.f):1.f;k=k*k*(3-2*k);
    task_fish(*p,f.approach.x+(142-TaskFishArt[p->kind].w-f.approach.x)*k,f.approach.y+(y+5-f.approach.y)*k,1,f.now);
   }
   rect(12,250,105,18,false);rtd::draw_text(out,font,f.now-f.started<Fishing::BiteMs&&f.phase==Fishing::Hook?waiting:lift,12,252,12);
  }
  if(f.phase==Fishing::Settle)if(auto p=f.find(f.current)){float k=std::min((f.now-f.started)/1200.f,1.f);task_fish(*p,125+137*k,8+30*k+std::sin(k*3.14159f)*22,1,f.now);}
  if(f.phase==Fishing::Reveal){border(20,100,260,134);rtd::draw_text(out,font,caught,33,113,18);for(size_t i=0;i<std::min<size_t>(3,f.plaque_lines.size());i++)rtd::draw_text(out,font,f.plaque_lines[i],33,145+i*23,18);rtd::draw_text(out,font,full,33,219,12);}
  if(f.now<f.notice_until){rect(6,250,282,18,false);rtd::draw_text(out,font,f.notice_chars,12,252,12);}
 }
};
}

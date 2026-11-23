#pragma once
#include "aquarium.hpp"
#include "aquarium_assets.hpp"
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
 void draw(const Aquarium &a,const rtd::Font &font){
  memset(out,255,rtd::Bytes);
  for(int y=0;y<32;y++)for(int x=0;x<48;x++)if(Logo[y][x]!='.')pixel(12+x,6+y,Logo[y][x]=='#');
  for(int x=12;x<288;x+=4)rect(x,44+std::lround(std::sin(x/26.f+a.now/1100.f)),4,1);
  plant(34,59,0,a.now);plant(48,80,1,a.now);plant(58,43,3,a.now);plant(248,50,4,a.now);plant(265,67,2,a.now);
  for(int i=0;i<6;i++){float x=26+(i*41)%248,y=363-std::fmod(a.now/float(45+i*8)+i*47,297.f);if(i%2){rect(x,y,3,1);rect(x-1,y+1,1,3);rect(x+3,y+1,1,3);rect(x,y+4,3,1);}else rect(x,y,2,2);}
  for(int x=12;x<288;x+=2)rect(x,377+std::lround(std::sin(x/21.f)*2),2,1);
  for(int i=0;i<23;i++)rect(18+(i*37)%269,385+(i*17)%9,i%3==0?3:1,1);
  if(a.mode==Aquarium::Feeding)for(auto p:a.food)if(!p.eaten&&a.now>=p.release){
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
 }
};
}

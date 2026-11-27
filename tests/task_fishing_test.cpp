#include "../firmware/main/aquarium_draw.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace tank;
static std::vector<rtd::TextItem> tasks={{0,"a","整理评审材料"},{1,"b","确认使用说明"},{2,"c","等待反馈"}};
static void samples(Fishing &f,int pole,int from,int to){for(int t=from;t<=to;t+=20)f.presence(pole,true,t,0);}
static void caught(Fishing &f,Aquarium &a,int t=0){assert(f.cast(t,0,false));f.step(t+2800,a);f.step(t+3900,a);assert(f.lift(t+3900));f.step(t+4800,a);assert(f.phase==Fishing::Commit);f.committed(true,t+4800);f.step(t+9000,a);f.step(t+10200,a);assert(f.phase==Fishing::Swim&&f.current=="a");}
int main(int argc,char **argv){
 Aquarium a;a.return_to_todo=false;a.collisions=false;a.water_y=-6;
 for(int choice=0;choice<2;choice++){Fishing f;f.reconcile(tasks,0);assert(f.eligible()==2);assert(f.cast(100,choice,false));assert(f.candidate==tasks[choice].id);assert(!f.cast(120,1,false));f.step(2800,a);assert(!f.approach_set);f.step(2900,a);assert(f.approach_set);assert(f.lift(3999));assert(f.current.empty()&&f.phase==Fishing::Swim);}
 {Fishing f;f.reconcile(tasks,0);caught(f,a);assert(f.eligible()==1);assert(!f.cast(11000,0,false)&&f.current=="a");auto moved=tasks;moved[0].text="改标题";f.reconcile(moved,12000);assert(f.current=="a");moved[0].category=2;f.reconcile(moved,13000);assert(f.current.empty());assert(f.eligible()==1);}
 {Fishing f;f.reconcile(tasks,0);caught(f,a);auto changed=tasks;changed.erase(changed.begin());f.reconcile(changed,11000);assert(f.current.empty()&&f.fish.size()==2);f.reconcile(tasks,12000);assert(f.current.empty()&&f.fish.size()==3);}
 {Fishing f;f.reconcile(tasks,0);f.cast(0,0,false);f.step(3900,a);f.lift(3900);f.step(4800,a);f.committed(false,4800);assert(f.current.empty()&&f.phase==Fishing::Swim);}
 {Fishing f;f.current="a";f.reconcile(tasks,0);assert(f.current=="a");f.current="deleted";f.reconcile(tasks,0);assert(f.current.empty());}
 {Fishing f;f.reconcile(tasks,0);samples(f,1,0,1000);assert(!f.take_feed());samples(f,0,1020,1520);samples(f,1,1540,1700);assert(f.take_feed());samples(f,1,1720,3000);assert(!f.take_feed());samples(f,-1,3020,3400);assert(f.phase==Fishing::Swim);samples(f,0,3420,3920);samples(f,-1,3940,4100);assert(f.phase==Fishing::Hook);auto start=f.started;for(int64_t t=4120;t<=start+4000;t+=20){f.presence(-1,true,t,0);f.step(t,a);}samples(f,0,start+4020,start+4520);assert(f.phase==Fishing::Lift);}
 {Fishing f;f.reconcile(tasks,0);samples(f,0,0,500);f.presence(-1,true,900,0);assert(!f.armed&&f.phase==Fishing::Swim);samples(f,0,920,1420);samples(f,-1,1440,1600);assert(f.phase==Fishing::Hook);f.presence(0,false,1700,0);assert(f.phase==Fishing::Hook);}
 {Fishing f;f.reconcile({{2,"only","等待"}},0);assert(!f.cast(0,0,true)&&f.fish.size()==1);}
 {Fishing f;f.reconcile(tasks,0);f.cast(0,0,true);f.reconcile({{2,"c","追踪"}},100);assert(f.phase==Fishing::Swim&&f.candidate.empty());}
 // Raw meal timing remains independent of fishing; the feeding transaction below releases the keeper.
 {Fishing f;f.reconcile(tasks,0);caught(f,a);a.feed(11000);assert(a.food[0].y==-6);for(int t=11000;t<50000;t+=20){a.step(t);f.step(t,a);}assert(a.mode==Aquarium::Idle&&a.eaten+a.expired==12&&f.current=="a");a.feed(51000);a.feed(52000);assert(a.food_count==24);}
 // Feeding releases exactly the kept fish, persists the empty keeper, and resets the sign.
 for(bool magnetic:{false,true}){
  Aquarium meal;meal.return_to_todo=false;meal.collisions=false;
  Fishing f;f.reconcile(tasks,0);caught(f,meal);f.plaque_page=4;f.plaque_lines={{0x601d}};
  std::string saved="a";int writes=0;
  auto save=[&](const std::string &id){writes++;saved=id;return true;};
  if(magnetic){samples(f,0,10300,10800);samples(f,1,10820,10980);assert(f.take_feed());}
  assert(f.feed(meal,11000,save));assert(saved.empty()&&writes==1&&f.current.empty());
  assert(f.plaque_page==0&&f.plaque_lines.empty()&&f.returning=="a"&&f.eligible()==2&&f.fish.size()==3&&meal.food_count==12);
  assert(f.find("a")->x==262&&f.find("a")->y==38);
  f.step(11450,meal);assert(f.find("a")->x<262&&f.find("a")->y>38);
  f.step(11900,meal);assert(f.returning.empty()&&f.find("a")->x<210&&f.find("a")->y>90);
  assert(f.feed(meal,12000,save)&&writes==1&&meal.food_count==24);
  Fishing reboot;reboot.current=saved;reboot.reconcile(tasks,0);assert(reboot.current.empty()&&reboot.eligible()==2);
  assert(f.cast(13000,0,false)&&f.candidate=="a");
  assert(!f.feed(meal,13020,save)&&writes==1); // Mid-catch input still ignored.
 }
 {Aquarium meal;Fishing f;f.reconcile(tasks,0);caught(f,meal);f.plaque_page=2;f.plaque_lines={{0x601d}};
  assert(!f.feed(meal,11000,[](const std::string &){return false;}));
  assert(f.current=="a"&&f.plaque_page==2&&f.plaque_lines.size()==1&&f.returning.empty()&&meal.food_count==0);
 }
 // Small plaque preserves every codepoint across all pages, at actual glyph metrics.
 assert(argc==2);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};rtd::Font font;assert(font.open(bytes.data(),bytes.size()));
 for(auto label:{"正在做","钓一件事来做","长按看清单","等待咬钩","移开提竿","完整原文翻页","未钓到，请重试","保存失败，请重试","任务已同步","暂时没有可钓的事项"})for(auto cp:rtd::codes(label)){rtd::Glyph g;assert(font.get(cp,12,g));}
 Fishing f;rtd::TextDocument doc;doc.items=tasks;doc.items[0].text="整理下一次讨论需要的材料，核对修改意见并补充操作说明。\n确认后同步给相关人员。";f.current="a";f.reconcile(doc.items,0);f.prepare_text(doc,font);
 std::vector<uint32_t> joined,want;for(auto &line:f.plaque_lines){assert(rtd::width(font,line,18)<=149);joined.insert(joined.end(),line.begin(),line.end());}for(auto cp:rtd::codes(doc.items[0].text))if(cp!='\n')want.push_back(cp);assert(joined==want&&f.plaque_lines.size()>2);
 std::vector<uint8_t> pixels(rtd::Bytes+32,0xa5);Painter painter(pixels.data()+16);for(int t=0;t<10000;t+=83){a.step(t);f.step(t,a);f.plaque_page=t/1000;painter.draw(a,font,&f);}for(int i=0;i<16;i++)assert(pixels[i]==0xa5&&pixels[pixels.size()-1-i]==0xa5);
 std::ofstream image("local/fishing-validation/render.bin",std::ios::binary);image.write((char*)pixels.data()+16,rtd::Bytes);
 // All supported task glyphs must have a native 18px counterpart.
 for(uint32_t i=0;i<rtd::u32(bytes.data()+4);i++){uint32_t key=rtd::u32(bytes.data()+16+i*24);if((key>>24)==24){rtd::Glyph h;assert(font.get(key&0xffffff,18,h));}}
 std::cout<<"Fishing: selection, signed Hall, release, early lift, persistence, publication changes, food, full text and render bounds passed\n";
}

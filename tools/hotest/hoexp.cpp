// hoexp: exploration runs of the emulated Hang-On board (reverse engineering the program).
// usage: hoexp <hangon rom dir> <frames> <out prefix> [script]
//   script lines as hotest ("<frame> service|coinage|dsw|adc0..2 <hex>", "<frame> snap <file>"),
//   "<frame> autopilot 1|0" (the attract-mode rider steers in a game: hook at $AF26),
//   "<frame> freeze 1|0" (the race timer $20C500 held at 50), "<frame> dump <file>" (work RAM, then shared RAM),
//   "<frame> poke<addr> <byte>" (hex, work or shared RAM, e.g. "800 pokeC7E011 03")
// writes <prefix>_main.cov / <prefix>_sub.cov (1 byte per ROM byte: 1 = an instruction executed
// there), <prefix>_main.bin / <prefix>_sub.bin (the ROM images) and <prefix>_tasks.txt (the task
// dispatcher's slots, $119A: a line whenever the set of (slot, entry) pairs changes)
#include "crossover/hoboard.hpp"
#include "crossover/romset.hpp"
#ifdef HOTEST_VIDEO
#include "crossover/hovideo.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include "crossover/m68k/m68k.h"

static int cur_frame = 0;
static std::vector<std::pair<int,uint32_t>> now_tasks, last_tasks;
static FILE* tf;
static bool autopilot = false, freeze = false;
static void hook(void*, uint32_t pc)
{
    if (pc == 0xaf26) { if (autopilot) hoboard.set_main_reg(M68K_REG_PC, 0xaf30); return; }   // the demo rider in a game
    if (pc == 0x119a) now_tasks.push_back({ (hoboard.workram[0x406] << 8) | hoboard.workram[0x407], hoboard.main_reg(M68K_REG_A0) });
}
int main(int argc, char** argv){
  if(argc<4){fprintf(stderr,"usage: hoexp romdir frames prefix [script]\n");return 2;}
  if(!ho_roms.load(argv[1])){fprintf(stderr,"roms: %s\n", ho_roms.error.c_str());return 1;}
  if(!hoboard.init(ho_roms)){fprintf(stderr,"board init failed\n");return 1;}
#ifdef HOTEST_VIDEO
  if(!hovideo.init(ho_roms)){fprintf(stderr,"video init failed\n");return 1;}
#endif
  static uint8_t cov[2][0x80000];
  hoboard.coverage[0] = cov[0]; hoboard.coverage[1] = cov[1];
  int frames=atoi(argv[2]); std::string pre=argv[3];
  tf = fopen((pre+"_tasks.txt").c_str(), "w");
  uint32_t pcs[2] = { 0x119a, 0xaf26 };
  hoboard.set_pc_hooks(pcs, 2, hook, nullptr);
  std::multimap<int,std::pair<std::string,std::string>> sc;
  if(argc>4){ FILE* s=fopen(argv[4],"r"); char a[64],b[256]; int f; while(fscanf(s,"%d %63s %255s",&f,a,b)==3) sc.insert({f,{a,b}}); fclose(s); }
  for(int fr=1; fr<=frames; fr++){
    std::string snap;
    auto r=sc.equal_range(fr);
    for(auto it=r.first; it!=r.second; ++it){
      const std::string& k=it->second.first; unsigned v=strtoul(it->second.second.c_str(),0,16);
      if(k=="service") hoboard.port_service=v; else if(k=="dsw") hoboard.port_dsw=v; else if(k=="coinage") hoboard.port_coinage=v;
      else if(k.rfind("adc",0)==0) hoboard.adc[k[3]-'0']=v;
      else if(k=="snap") snap=it->second.second;
      else if(k=="autopilot") autopilot=v!=0;
      else if(k=="freeze") freeze=v!=0;
      else if(k.rfind("poke",0)==0) { unsigned a=strtoul(k.c_str()+4,0,16); if(a>=0xc7c000&&a<0xc80000) hoboard.subram[a-0xc7c000]=v; else if(a>=0x20c000&&a<0x210000) hoboard.workram[a-0x20c000]=v; }
      else if(k=="dump") { FILE* d=fopen(it->second.second.c_str(),"wb"); fwrite(hoboard.workram,1,0x4000,d); fwrite(hoboard.subram,1,0x4000,d); fclose(d); }
    }
    if (freeze && hoboard.workram[0x408]) hoboard.workram[0x500] = 0x50;   // the race timer (BCD seconds)
    cur_frame=fr; now_tasks.clear(); hoboard.run_frame();
    if (now_tasks != last_tasks) {
      fprintf(tf, "%d:", fr); for (auto& t : now_tasks) fprintf(tf, " %d:%x", t.first, t.second); fprintf(tf, "\n");
      last_tasks = now_tasks;
    }
#ifdef HOTEST_VIDEO
    hovideo.render(hoboard);
    if(!snap.empty()){ FILE* p=fopen(snap.c_str(),"wb"); fprintf(p,"P6\n%d 224\n255\n",hovideo.width);
      for(int i=0;i<hovideo.width*224;i++){ uint32_t c=hovideo.rgb(hoboard, hovideo.pixels[i]); uint8_t px[3]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)}; fwrite(px,1,3,p);} fclose(p); }
#endif
  }
  fclose(tf);
  auto w=[&](const std::string& n, const void* p, size_t len){ FILE* f=fopen(n.c_str(),"wb"); fwrite(p,1,len,f); fclose(f); };
  w(pre+"_main.cov", cov[0], 0x40000); w(pre+"_sub.cov", cov[1], 0x40000);
  w(pre+"_main.bin", hoboard.main_rom.data(), 0x40000); w(pre+"_sub.bin", hoboard.sub_rom.data(), 0x40000);
  return 0;
}

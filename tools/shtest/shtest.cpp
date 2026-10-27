// shtest: run the emulated Super Hang-On board headless and dump RAM per frame, in drive.lua's
// "dump" format ([u32 frame][bytes], big-endian frame), for comparison with MAME 0.264.
// usage: shtest <shangon1 rom dir> <frames> <out prefix> [script]
//   script lines: "<frame> <port> <value>" with port = buttons|dsw|coinage|adc0..adc3 (value hex)
//                 "<frame> trace <file>" / "<frame> untrace"
// writes <prefix>_main.ram (main $60000-$67FFF) and <prefix>_sub.ram (second CPU $60000-$67FFF)
#include "crossover/shboard.hpp"
#include "crossover/romset.hpp"
#include "crossover/shvideo.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>
static void put(FILE* f, uint32_t frame, const uint8_t* p, size_t n){ uint8_t b[4]={uint8_t(frame>>24),uint8_t(frame>>16),uint8_t(frame>>8),uint8_t(frame)}; fwrite(b,1,4,f); fwrite(p,1,n,f); }
int main(int argc, char** argv){
  if(argc<4){fprintf(stderr,"usage: shtest romdir frames prefix [script]\n");return 2;}
  if(!sh_roms.load(argv[1])){fprintf(stderr,"roms: %s\n", sh_roms.error.c_str());return 1;}
  if(!shboard.init(sh_roms)){fprintf(stderr,"board init failed\n");return 1;}
  if(!shvideo.init(sh_roms)){fprintf(stderr,"video init failed\n");return 1;}
  if(getenv("SHT_WIDE")){ shvideo.set_width(398); shboard.set_widescreen(shvideo.xoff); }
  if(getenv("SHT_NVRAM_LOAD")){ FILE* n=fopen(getenv("SHT_NVRAM_LOAD"),"rb"); if(n){ size_t r=fread(shboard.workram,1,0x8000,n); fclose(n); printf("nvram loaded %zu\n",r);} }
  static int cur_frame=0; FILE* sl = getenv("SHT_SOUNDLOG") ? fopen(getenv("SHT_SOUNDLOG"),"w") : nullptr;
  shboard.sound_command = [&](uint8_t b){ if(sl) fprintf(sl,"%d %02x\n",cur_frame,b); };
  shboard.sound_reset = [&](bool held){ if(sl) fprintf(sl,"%d reset %d\n",cur_frame,held?1:0); };
  int frames=atoi(argv[2]); std::string pre=argv[3];
  std::multimap<int,std::pair<std::string,std::string>> sc;
  if(argc>4){ FILE* s=fopen(argv[4],"r"); char a[64],b[256]; int f; while(fscanf(s,"%d %63s",&f,a)==2){ b[0]=0; if(strcmp(a,"untrace")) fscanf(s,"%255s",b); sc.insert({f,{a,b}}); } fclose(s); }
  std::map<int,std::string> snaps;
  FILE* fm=fopen((pre+"_main.ram").c_str(),"wb"); FILE* fs=fopen((pre+"_sub.ram").c_str(),"wb");
  int every = getenv("SHT_EVERY") ? atoi(getenv("SHT_EVERY")) : 1;
  for(int fr=1; fr<=frames; fr++){
    auto r=sc.equal_range(fr);
    for(auto it=r.first; it!=r.second; ++it){
      const std::string& k=it->second.first; unsigned v=strtoul(it->second.second.c_str(),0,16);
      if(k=="buttons") shboard.port_buttons=v; else if(k=="dsw") shboard.port_dsw=v; else if(k=="coinage") shboard.port_coinage=v;
      else if(k.rfind("adc",0)==0) shboard.adc[k[3]-'0']=v;
      else if(k=="trace") shboard.trace=fopen(it->second.second.c_str(),"w");
      else if(k=="poke"){ unsigned a=strtoul(it->second.second.c_str(),0,16); unsigned val=0; char* c=strchr((char*)it->second.second.c_str(),':'); if(c) val=strtoul(c+1,0,16); shboard.workram[a&0x7fff]=val; }
      else if(k=="snap"){ snaps[fr]=it->second.second; }
      else if(k=="wlog"){ shboard.wlog=fopen(it->second.second.c_str(),"w"); }
      else if(k=="untrace"){ if(shboard.trace) fclose(shboard.trace); shboard.trace=nullptr; }
    }
    cur_frame=fr; shboard.run_frame();
    if(getenv("SHT_WATCH")){ unsigned a=strtoul(getenv("SHT_WATCH"),0,16)&0x7fff; static int last=-1; int v=shboard.workram[a]<<8|shboard.workram[a+1]; if(v!=last){ printf("f%d %04x=%04x\n",fr,a,v); last=v; } }
    shvideo.render(shboard);
    if(getenv("SHT_REGS") && fr%atoi(getenv("SHT_REGS"))==0){ printf("f%d",fr); for(int i=0;i<4;i++) printf(" [%d] page %04x x %04x y %04x",i,shboard.latched_page[i],shboard.latched_xscroll[i],shboard.latched_yscroll[i]); printf("\n"); }
    if(snaps.count(fr) && getenv("SHT_VIRT")){
      // the whole 1024x512 virtual tilemap of layer SHT_VIRT (0 fg, 1 bg) with the 320-pixel window marked
      int which=atoi(getenv("SHT_VIRT")); uint16_t pages=shboard.latched_page[which];
      int effx=(0xc0-shboard.latched_xscroll[which])&0x3ff, effy=shboard.latched_yscroll[which]&0x1ff;
      std::string fn=snaps[fr]+".virt.ppm"; FILE* p=fopen(fn.c_str(),"wb"); fprintf(p,"P6\n1024 512\n255\n");
      for(int vy=0;vy<512;vy++) for(int vx=0;vx<1024;vx++){
        int page=(pages>>(4*((vy>=256?2:0)+(vx>=512?1:0))))&0xf; int tx=(vx&0x1ff)>>3, ty=(vy&0xff)>>3;
        uint16_t data=(shboard.tileram[(page*2048+ty*64+tx)*2]<<8)|shboard.tileram[(page*2048+ty*64+tx)*2+1];
        uint32_t c=shvideo.rgb(shboard,(uint16_t)(((data>>5)&0x7f)*8 + ((data&0x1fff)?4:0)));
        int inwin = (((vx-effx)&0x3ff) < 320) && (((vy-effy)&0x1ff) < 224);
        int edge = inwin && ((((vx-effx)&0x3ff)==0) || (((vx-effx)&0x3ff)==319));
        uint8_t px[3]={uint8_t(edge?255:(c>>16)),uint8_t(edge?0:(c>>8)),uint8_t(edge?0:c)}; if(!inwin){px[0]/=2;px[1]/=2;px[2]/=2;}
        fwrite(px,1,3,p);} fclose(p); }
    if(snaps.count(fr)){ FILE* p=fopen(snaps[fr].c_str(),"wb"); fprintf(p,"P6\n%d 224\n255\n",shvideo.width);
      for(int i=0;i<shvideo.width*224;i++){ uint32_t c=shvideo.rgb(shboard, shvideo.pixels[i]); uint8_t px[3]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)}; fwrite(px,1,3,p);} fclose(p); }
    if(fr%every==0){ put(fm,fr,shboard.workram,0x8000); put(fs,fr,shboard.cpu1ram,0x8000); }
  }
  fclose(fm); fclose(fs);
  if(getenv("SHT_NVRAM_SAVE")){ FILE* n=fopen(getenv("SHT_NVRAM_SAVE"),"wb"); fwrite(shboard.workram,1,0x8000,n); fclose(n); }
  printf("main pc %06x sub pc %06x\n", shboard.main_pc(), shboard.sub_pc());
  return 0;
}

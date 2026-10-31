// hotest: run the emulated Hang-On board headless and dump RAM per frame ([u32 frame][bytes],
// big-endian frame; the layout the MAME Lua dumps used), for comparison with MAME 0.264.
// usage: hotest <hangon rom dir> <frames> <out prefix> [script]
//   script lines: "<frame> <port> <value>" with port = service|coinage|dsw|adc0..adc2 (value hex),
//                 "<frame> snap <file.ppm>" (when built with the video), "<frame> trace <file>" / "untrace"
// writes <prefix>_main.ram (main $20C000-$20FFFF) and <prefix>_sub.ram ($C7C000-$C7FFFF)
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
static void put(FILE* f, uint32_t frame, const uint8_t* p, size_t n){ uint8_t b[4]={uint8_t(frame>>24),uint8_t(frame>>16),uint8_t(frame>>8),uint8_t(frame)}; fwrite(b,1,4,f); fwrite(p,1,n,f); }
int main(int argc, char** argv){
  if(argc<4){fprintf(stderr,"usage: hotest romdir frames prefix [script]\n");return 2;}
  if(!ho_roms.load(argv[1])){fprintf(stderr,"roms: %s\n", ho_roms.error.c_str());return 1;}
  FILE* sl = getenv("HOT_SOUNDLOG") ? fopen(getenv("HOT_SOUNDLOG"),"w") : nullptr;
  static int cur_frame=0;
  hoboard.sound_command = [&](uint8_t b){ if(sl) fprintf(sl,"%d %02x\n",cur_frame,b); };
  if(!hoboard.init(ho_roms)){fprintf(stderr,"board init failed\n");return 1;}
#ifdef HOTEST_VIDEO
  if(!hovideo.init(ho_roms)){fprintf(stderr,"video init failed\n");return 1;}
  if(getenv("HOT_WIDE")) hovideo.set_width(398);
#endif
  int frames=atoi(argv[2]); std::string pre=argv[3];
  std::multimap<int,std::pair<std::string,std::string>> sc;
  if(argc>4){ FILE* s=fopen(argv[4],"r"); char a[64],b[256]; int f; while(fscanf(s,"%d %63s",&f,a)==2){ b[0]=0; if(strcmp(a,"untrace")) fscanf(s,"%255s",b); sc.insert({f,{a,b}}); } fclose(s); }
  FILE* fm=fopen((pre+"_main.ram").c_str(),"wb"); FILE* fs=fopen((pre+"_sub.ram").c_str(),"wb");
  for(int fr=1; fr<=frames; fr++){
    std::string snap;
    auto r=sc.equal_range(fr);
    for(auto it=r.first; it!=r.second; ++it){
      const std::string& k=it->second.first; unsigned v=strtoul(it->second.second.c_str(),0,16);
      if(k=="service") hoboard.port_service=v; else if(k=="dsw") hoboard.port_dsw=v; else if(k=="coinage") hoboard.port_coinage=v;
      else if(k.rfind("adc",0)==0) hoboard.adc[k[3]-'0']=v;
      else if(k=="trace") hoboard.trace=fopen(it->second.second.c_str(),"w");
      else if(k=="untrace"){ if(hoboard.trace) fclose(hoboard.trace); hoboard.trace=nullptr; }
      else if(k=="snap") snap=it->second.second;
    }
    cur_frame=fr; hoboard.run_frame();
#ifdef HOTEST_VIDEO
    hovideo.render(hoboard);              // every frame, as MAME (the sprites' scratch words)
    if(!snap.empty()){ FILE* p=fopen(snap.c_str(),"wb"); fprintf(p,"P6\n%d 224\n255\n",hovideo.width);
      for(int i=0;i<hovideo.width*224;i++){ uint32_t c=hovideo.rgb(hoboard, hovideo.pixels[i]); uint8_t px[3]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)}; fwrite(px,1,3,p);} fclose(p); }
#endif
    put(fm,fr,hoboard.workram,0x4000); put(fs,fr,hoboard.subram,0x4000);
  }
  fclose(fm); fclose(fs);
  printf("main pc %06x sub pc %06x\n", hoboard.main_pc(), hoboard.sub_pc());
  return 0;
}

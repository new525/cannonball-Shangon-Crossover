// sbtest: run the virtual sound board on a logged command stream (drive.lua "soundlog": "<frame> <byte>")
// and write its YM2151 writes ("<frame> <reg> <val>") and a WAV.
// usage: sbtest <z80 rom> <pcm rom region file> <commands> <ym log out> <wav out> <frames> [rate] [z80 reset frame]
#include "crossover/soundboard.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <map>
static std::vector<uint8_t> slurp(const char* f){ FILE* h=fopen(f,"rb"); if(!h){perror(f);exit(1);} std::vector<uint8_t> v; int c; while((c=fgetc(h))!=EOF) v.push_back((uint8_t)c); fclose(h); return v; }
int main(int argc, char** argv){
  if(argc<7){fprintf(stderr,"usage\n");return 2;}
  int rate = argc>7 ? atoi(argv[7]) : 48000, rst = argc>8 ? atoi(argv[8]) : -1, fps=60, frames=atoi(argv[6]);
  SegaSoundBoard b; if(!b.init(getenv("SBT_HANGON") ? SegaSoundBoard::Kind::HANGON : SegaSoundBoard::Kind::OUTRUN, slurp(argv[1]), slurp(argv[2]), rate, fps)) return 1;
  std::multimap<int,int> cmds; FILE* c=fopen(argv[3],"r"); int fr; unsigned v; while(fscanf(c,"%d %x",&fr,&v)==2) cmds.insert({fr,(int)v}); fclose(c);
  FILE* yl=fopen(argv[4],"w"); b.set_ym_log(yl);
  FILE* w=fopen(argv[5],"wb"); std::vector<int16_t> out;
  int n=rate/fps; std::vector<int32_t> mix(n*2);
  for(int f=0; f<frames; f++){
    if(f==rst) b.reset_after_commands=true;
    auto r=cmds.equal_range(f); for(auto it=r.first; it!=r.second; ++it) b.command((uint8_t)it->second);
    std::fill(mix.begin(),mix.end(),0); b.frame(mix.data(), n);
    if(getenv("SBT_DEBUG") && f>=8 && f<=20){ printf("f%d pc %04x iff1 %d halted %d f800:",f,b.z80_state().pc,b.z80_state().iff1,b.z80_state().halted); for(int k=0;k<16;k++) printf(" %02x",b.z80_ram()[k]); printf("\n"); }
    for(int32_t s: mix){ if(s>32767)s=32767; if(s<-32768)s=-32768; out.push_back((int16_t)s); }
  }
  fclose(yl);
  uint32_t data=out.size()*2; auto w32=[&](uint32_t x){fwrite(&x,4,1,w);}; auto w16=[&](uint16_t x){fwrite(&x,2,1,w);};
  fwrite("RIFF",1,4,w); w32(36+data); fwrite("WAVEfmt ",1,8,w); w32(16); w16(1); w16(2); w32(rate); w32(rate*4); w16(4); w16(16); fwrite("data",1,4,w); w32(data); fwrite(out.data(),2,out.size(),w); fclose(w);
  return 0;
}

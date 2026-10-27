// sbtimed: drive the virtual sound board with MAME's timed command log (drive.lua soundlog: "frame byte time")
// usage: sbtimed <z80 rom> <pcm region> <commands> <ym log out> <wav out> <end seconds> <z80 reset time (s) or -1>
#include "crossover/soundboard.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
static std::vector<uint8_t> slurp(const char* f){ FILE* h=fopen(f,"rb"); if(!h){perror(f);exit(1);} std::vector<uint8_t> v; int c; while((c=fgetc(h))!=EOF) v.push_back((uint8_t)c); fclose(h); return v; }
int main(int argc, char** argv){
  if(argc<8){fprintf(stderr,"usage\n");return 2;}
  const double CLK=4e6; int rate=48000;
  SegaSoundBoard b; if(!b.init(getenv("SBT_HANGON") ? SegaSoundBoard::Kind::HANGON : SegaSoundBoard::Kind::OUTRUN, slurp(argv[1]), slurp(argv[2]), rate, 60)) return 1;
  struct C{double t; unsigned v;}; std::vector<C> cmds; FILE* c=fopen(argv[3],"r"); int fr; unsigned v; double t;
  double dt = getenv("SBT_DT") ? atof(getenv("SBT_DT")) : 0;
  while(fscanf(c,"%d %x %lf",&fr,&v,&t)==3) cmds.push_back({t+dt,v}); fclose(c);
  double tend=atof(argv[6]), trst=atof(argv[7]);
  std::vector<double> resets;   // further reset pulses (env SBT_RESETS="t1,t2,...", seconds)
  if(const char* r=getenv("SBT_RESETS")){ char* e; const char* q=r; while(*q){ resets.push_back(strtod(q,&e)); q=*e?e+1:e; } }
  size_t ri=0;
  FILE* yl=fopen(argv[4],"w"); b.set_ym_log(yl);
  FILE* pl=argc>8?fopen(argv[8],"w"):nullptr; if(pl) b.set_pcm_log(pl);
  std::vector<int16_t> out; std::vector<int32_t> mix(800*2);
  if(getenv("SBT_HOLD")) b.cpu_held=true;   // reset line held from power-on until trst
  size_t ci=0; bool rst_done = trst<0; double chunk=1.0/60; double next_out=chunk;
  while(b.time() < tend*CLK){
    double tn = next_out;
    if(ci<cmds.size() && cmds[ci].t < tn) tn = cmds[ci].t;
    if(!rst_done && trst < tn) tn = trst;
    if(ri<resets.size() && resets[ri] < tn) tn = resets[ri];
    b.run_to((int64_t)(tn*CLK));
    if(!rst_done && trst <= tn){ b.cpu_held=false; b.cpu_reset(); rst_done=true; continue; }
    if(ri<resets.size() && resets[ri] <= tn){ b.cpu_reset(); ri++; continue; }
    if(ci<cmds.size() && cmds[ci].t <= tn){ b.command_now((uint8_t)cmds[ci].v); ci++; continue; }
    std::fill(mix.begin(),mix.end(),0); b.drain(mix.data(), 800);
    for(int32_t s: mix){ if(s>32767)s=32767; if(s<-32768)s=-32768; out.push_back((int16_t)s); }
    b.frames_run++; next_out += chunk;
  }
  fclose(yl); if(pl) fclose(pl);
  FILE* w=fopen(argv[5],"wb"); uint32_t data=out.size()*2; auto w32=[&](uint32_t x){fwrite(&x,4,1,w);}; auto w16=[&](uint16_t x){fwrite(&x,2,1,w);};
  fwrite("RIFF",1,4,w); w32(36+data); fwrite("WAVEfmt ",1,8,w); w32(16); w16(1); w16(2); w32(rate); w32(rate*4); w16(4); w16(16); fwrite("data",1,4,w); w32(data); fwrite(out.data(),2,out.size(),w); fclose(w);
}

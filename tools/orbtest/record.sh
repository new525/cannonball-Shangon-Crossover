#!/bin/bash
# record.sh <mame rompath> <out dir> <name> [extra MAME options]: records the reference for
# orbtest with MAME 0.264 (orunnersj, a fresh cfg, a fresh EEPROM unless OR_NVFROM names a MAME
# nvram folder to start from) from the schedule <out dir>/<name>.sched (orb.lua's format):
#   <name>.hash .fgz .io .wav, snaps_<name>/ (two PNGs per "snap"), nv_<name>/ (the EEPROM at exit);
#   OR_ZLOG=<file> also logs the Z80's port and MultiPCM writes.
# MAME is $MAME (default: mame on the PATH). It runs with -debug -debugger none (as the project's earlier references); without -debug
# MAME 0.264 did not start the autoboot script in this setup. Needs g++ and zlib.
set -e
T=$(cd "$(dirname "$0")" && pwd); RP=$(cd "$1" && pwd); O=$(cd "$2" && pwd); N=$3; shift 3
g++ -O2 -o $O/framesink $T/framesink.cpp -lz
rm -rf $O/snaps_$N $O/nv_$N $O/cfg_$N $O/dump_$N; mkdir -p $O/dump_$N
if [ -n "$OR_NVFROM" ]; then mkdir -p $O/nv_$N; cp -r "$OR_NVFROM"/* $O/nv_$N/; fi
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
OR_SCHED=$O/$N.sched OR_IOLOG=$O/$N.io OR_FRAMES="$O/framesink $O/$N.fgz $O/$N.hash" OR_DUMPDIR=$O/dump_$N \
  ${MAME:-mame} orunnersj -rompath $RP -autoboot_script $T/orb.lua -debug -debugger none -video soft -nothrottle \
  -skip_gameinfo -window -cfg_directory $O/cfg_$N -nvram_directory $O/nv_$N -snapshot_directory $O/snaps_$N \
  -snapview native -samplerate 44100 -nocompressor -wavwrite $O/$N.wav "$@" > $O/$N.log 2>&1

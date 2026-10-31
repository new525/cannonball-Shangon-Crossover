#!/bin/bash
# build.sh <out dir>: builds ortrace (experiments on OutRunners' program on the emulated board; needs libpng)
# run: <out dir>/ortrace run <orunnersj rom dir> <script> [options] (see ortrace.cpp)
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=$1; mkdir -p $O; C=$R/src/main/crossover
gcc -O2 -c -I$R/src/main -o $O/z80.o $C/z80/z80.c
g++ -O2 -std=c++17 -Wall -I$R/src/main -o $O/ortrace $R/tools/ortrace/ortrace.cpp $C/orboard.cpp $C/s32io.cpp \
  $C/s32video.cpp $C/orsound.cpp $C/multipcm/multipcm.cpp $C/ymfm/ymfm_opn.cpp $C/ymfm/ymfm_ssg.cpp \
  $C/ymfm/ymfm_adpcm.cpp $C/v60/v60.cpp $C/v60/v60d.cpp $O/z80.o -lpng -lz -lm

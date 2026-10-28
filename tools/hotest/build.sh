#!/bin/bash
# build.sh <out dir> [video]: builds hotest from the crossover sources
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=$1; mkdir -p $O
M=$R/src/main/crossover/m68k
for f in m68kcpu m68kops m68kdasm m68k_glue; do [ $O/$f.o -nt $M/$f.c ] || gcc -O2 -c -I$M -I$R/src/main -o $O/$f.o $M/$f.c; done
gcc -O2 -c -I$M -o $O/softfloat.o $M/softfloat/softfloat.c 2>/dev/null || true
V=""; D=""; [ "$2" = video ] && { V="$R/src/main/crossover/hovideo.cpp"; D="-DHOTEST_VIDEO"; }
for T in hotest hoexp; do g++ -O2 -std=c++17 $D -I$R/src/main -o $O/$T $R/tools/hotest/$T.cpp $R/src/main/crossover/hoboard.cpp \
  $R/src/main/crossover/xobus.cpp $R/src/main/crossover/romset.cpp $R/src/main/crossover/fd1089.cpp $V \
  $O/m68kcpu.o $O/m68kops.o $O/m68kdasm.o $O/m68k_glue.o $(ls $O/softfloat.o 2>/dev/null) -lm; done

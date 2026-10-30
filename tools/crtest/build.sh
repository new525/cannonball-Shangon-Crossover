#!/bin/bash
# build.sh <out dir>: builds crtest from the crossover sources
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=$1; mkdir -p $O
M=$R/src/main/crossover/m68k
for f in m68kcpu m68kops m68kdasm m68k_glue; do [ $O/$f.o -nt $M/$f.c ] || gcc -O2 -c -I$M -I$R/src/main -o $O/$f.o $M/$f.c; done
[ -f $O/softfloat.o ] || gcc -O2 -c -I$M -o $O/softfloat.o $M/softfloat/softfloat.c 2>/dev/null || true
g++ -O2 -std=c++17 -Wall -I$R/src/main -o $O/crtest $R/tools/crtest/crtest.cpp $R/src/main/crossover/crsound.cpp \
  $R/src/main/crossover/xobus.cpp $R/src/main/crossover/sh/shcpu.cpp $R/src/main/crossover/scsp/scsp.cpp $R/src/main/crossover/scsp/scspdsp.cpp \
  $O/m68kcpu.o $O/m68kops.o $O/m68kdasm.o $O/m68k_glue.o $(ls $O/softfloat.o 2>/dev/null) -lm

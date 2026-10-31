#!/bin/bash
# build.sh <out dir>: builds ortest from the crossover sources
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=$1; mkdir -p $O; C=$R/src/main/crossover
gcc -O2 -c -I$R/src/main -o $O/z80.o $C/z80/z80.c
g++ -O2 -std=c++17 -I$R/src/main -o $O/ortest $R/tools/ortest/ortest.cpp $C/orsound.cpp $C/multipcm/multipcm.cpp \
  $C/ymfm/ymfm_opn.cpp $C/ymfm/ymfm_ssg.cpp $C/ymfm/ymfm_adpcm.cpp $O/z80.o -lm

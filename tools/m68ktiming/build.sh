#!/bin/bash
# build.sh <out dir>: builds mtime (the Musashi side of the 68000 timing comparison) from the
# crossover's Musashi sources
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=$1; mkdir -p $O
M=$R/src/main/crossover/m68k
for f in m68kcpu m68kops m68kdasm m68k_glue; do gcc -O2 -c -I$M -o $O/$f.o $M/$f.c; done
gcc -O2 -c -I$M -o $O/softfloat.o $M/softfloat/softfloat.c 2>/dev/null || true
gcc -O2 -I$M -o $O/mtime $R/tools/m68ktiming/mtime.c $O/m68kcpu.o $O/m68kops.o $O/m68kdasm.o $O/m68k_glue.o $(ls $O/softfloat.o 2>/dev/null) -lm

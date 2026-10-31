#!/bin/bash
# build.sh <out dir>: builds v60test (trace test of the V60/V70 core against MAME 0.264)
# run: <out dir>/v60test main.bin main.tr main.io [-o trace] [-romdir dir] [-n count] [-bench]
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=$1; mkdir -p $O; C=$R/src/main/crossover
g++ -O2 -std=c++17 -Wall -I$R/src/main -o $O/v60test $R/tools/v60test/v60test.cpp \
  $C/v60/v60.cpp $C/v60/v60d.cpp

#!/bin/bash
# build.sh <out dir>: builds s32vtest from the crossover sources (needs libpng)
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); O=$1; mkdir -p $O; C=$R/src/main/crossover
g++ -O2 -std=c++17 -Wall -I$R/src/main -o $O/s32vtest $R/tools/s32vtest/s32vtest.cpp $C/s32video.cpp -lpng

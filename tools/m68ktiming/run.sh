#!/bin/bash
# run.sh <work dir> <MAME home> [rom path]: the whole 68000 timing comparison.
#   builds mtime, lists the 68000's valid opcodes (Musashi's view), generates the tests, runs them
#   through Musashi and through MAME 0.264's Hang-On sub CPU, and writes <work>/report.txt.
# The MAME home needs the hangon ROM set (in <MAME home>/roms unless a rom path is given).
set -e
T=$(cd "$(dirname "$0")" && pwd); W=$(mkdir -p "$1" && cd "$1" && pwd); H=$(cd "$2" && pwd); RP=${3:-$H/roms}
$T/build.sh $W/bld
$W/bld/mtime valid > $W/valid.txt
python3 $T/gen.py $W
$W/bld/mtime run $W/batches.bin $W/musashi.txt $W
( cd $H && MT_BATCHES=$W/batches.bin MT_OUT=$W HOME=$H SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  mame hangon -rompath $RP -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run 280 \
  -debug -debugger none -autoboot_script $T/mame_run.lua > $W/mame.out 2>&1 )
python3 $T/compare.py $W $W/musashi.txt $W/mame.tr $W/report.txt
head -2 $W/report.txt

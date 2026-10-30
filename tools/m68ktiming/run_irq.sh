#!/bin/bash
# run_irq.sh <work dir> <MAME home> [rom path]: the 68000 interrupt latency check, MAME 0.264's
# Hang-On main CPU (VBLANK level 4 autovector) against Musashi on the same program and the same
# interrupt times. The interrupts come at frame boundaries: Hang-On's CPU clock is its pixel clock
# and a frame is 400 x 262 = 104800 cycles; mame_irq.lua starts the program at frame 200, so the
# first interrupt after it is at 199 * 104800 cycles.
set -e
T=$(cd "$(dirname "$0")" && pwd); W=$(mkdir -p "$1" && cd "$1" && pwd); H=$(cd "$2" && pwd); RP=${3:-$H/roms}
$T/build.sh $W/bld
T0=$((199 * 104800))
for p in prog:3000 stop:300; do
  n=${p%%:*}; f=${p##*:}
  python3 $T/irqprog.py $W/$n.bin $n
  ( cd $H && MT_PROG=$W/$n.bin MT_OUT=$W/mame_$n.tr MT_FRAMES=$f HOME=$H SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    mame hangon -rompath $RP -video none -sound none -nothrottle -skip_gameinfo -seconds_to_run 120 \
    -debug -debugger none -autoboot_script $T/mame_irq.lua > $W/mame_$n.out 2>&1 )
  start=$(grep -m1 -o "^[0-9A-F]* 20C000" $W/mame_$n.tr | cut -d' ' -f1)
  $W/bld/mtime irq $W/$n.bin 0x$start $T0 104800 $((f - 1)) $W/mus_$n.tr 2000
  echo "== $n"; python3 $T/irqcheck.py $W/mame_$n.tr $W/mus_$n.tr $T0 104800 $([ $n = stop ] && echo stop)
done

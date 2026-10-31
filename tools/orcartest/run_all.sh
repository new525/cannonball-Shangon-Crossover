#!/bin/bash
# run_all.sh <orcartest binary> <orunnersj rom dir> <out dir>: the test set of docs/OUTRUNNERS-RE.md
# section 13 (each run writes <name>.txt; the last lines of each are its result). Two runs at a time.
B=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); R=$(cd "$2" && pwd); O=$3
mkdir -p "$O"; cd "$O"
run() { n=$1; shift; ORCARTEST_COVER_FILE="$n.cov" "$B" "$R" "$@" > "$n.txt" 2>&1; echo "$n: exit $?"; }
run full_auto full &
run full_manual full -manual -left; wait
run kenya_auto kenya &
run kenya_manual kenya -manual; wait
run two_players two &
run time_up brief -nohold -frames 5600; wait
run full_overrev full -manual -abuse -frames 12000 &
run kenya_overrev kenya -manual -abuse; wait
run forced brief -force -frames 8000 &
run forced_car4 brief -force -car 4 -frames 8000; wait
for c in 0 1 2 3 4 5 6 7; do
  run car${c}_auto brief -car $c &
  run car${c}_manual brief -car $c -manual; wait
done

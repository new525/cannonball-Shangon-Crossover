#!/bin/bash
# run_all.sh <orradiotest> <orunnersj rom dir> <out dir>: every scenario (2 at a time); <name>.txt and
# <name>.log (the commands sent to $700700: frame, value, radio or game) in <out dir>; then the
# instructions of the transcribed routines that no scenario executed
B=$1; R=$2; O=$3; mkdir -p $O
run() { $B $R $1 -v -log $O/$1.log > $O/$1.txt 2>&1; echo "$1: $(tail -1 $O/$1.txt)"; }
run buttons & run dj & wait
run branch & run fast & wait
run two
grep -h "^  not executed" $O/buttons.txt | sort > $O/ne.0
for s in dj branch fast two; do grep -h "^  not executed" $O/$s.txt | sort | comm -12 $O/ne.0 - > $O/ne.1; mv $O/ne.1 $O/ne.0; done
echo "not executed by any scenario: $(wc -l < $O/ne.0)"; cat $O/ne.0; rm -f $O/ne.0

#!/bin/bash
# run_all.sh <ortrace binary> <orunnersj rom dir> <out dir>: runs the experiments of
# docs/OUTRUNNERS-RE.md (scripts/e*.scr) in <out dir>; each writes <name>.txt there (and its PNGs)
set -e
B=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); R=$(cd "$2" && pwd); O=$3; S=$(cd "$(dirname "$0")" && pwd)/scripts
mkdir -p "$O"; cd "$O"
for s in "$S"/e*.scr; do
    n=$(basename "$s" .scr)
    "$B" run "$R" "$s" -o "$n.txt" -draw 3
    echo "$n: $(wc -l < "$n.txt") lines"
done

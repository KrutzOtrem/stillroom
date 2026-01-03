#!/bin/sh
DIR="$(dirname "$0")"
cd "$DIR"

export LD_LIBRARY_PATH="$DIR:$DIR/bins:$LD_LIBRARY_PATH"

# Run
"$DIR/stillroom.elf" 2>&1 | tee "$DIR/log.txt"

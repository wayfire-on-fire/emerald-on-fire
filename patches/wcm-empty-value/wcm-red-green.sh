#!/bin/bash
# Regression test: wcm segfault on empty <value> element in plugin metadata
# (upstream issue WayfireWM/wcm#126; fix: print ERROR and exit(1) instead).
#
# Usage: wcm-red-green.sh <path-to-wcm-binary>
# Exit: 0 = GREEN (clean error + exit 1), 1 = RED (segfault)
set -u
BIN="${1:?usage: wcm-red-green.sh <wcm-binary>}"
DIR="$(cd "$(dirname "$0")" && pwd)"

env XDG_DATA_HOME="$DIR/data-bad" HOME="$DIR/home" \
    timeout 30 "$BIN" > "$DIR/run.log" 2>&1
rc=$?

if [ $rc -eq 139 ] || [ $rc -eq 134 ]; then
    echo "RED: wcm segfaulted (exit $rc)"
    exit 1
elif [ $rc -eq 1 ] && grep -q "ERROR: \[badplugin\] empty <value> element" "$DIR/run.log"; then
    echo "GREEN: clean error + exit(1):"
    grep "ERROR:" "$DIR/run.log"
    exit 0
else
    echo "UNEXPECTED: exit $rc"
    tail -5 "$DIR/run.log"
    exit 2
fi

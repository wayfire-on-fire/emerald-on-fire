#!/usr/bin/env bash
# test_makefile_docker_syntax.sh — the build-docker recipe's bash -c string
# must PARSE. The recipe is a single-quoted shell string; an apostrophe
# anywhere in it (even in a comment, e.g. "wayfire's") terminates the quote
# and the rest becomes an unterminated string. That only fails at runtime —
# on the CI runner, AFTER a ~30-minute image build — so pin it here:
# expand the recipe (make -n) and syntax-check it with sh -n.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
MAKEFILE="$ROOT/Makefile"
[ -f "$MAKEFILE" ] || { echo "FAIL: Makefile not found"; exit 1; }

expanded="$(make -n -C "$ROOT" build-docker 2>&1)"
status=$?
if [ "$status" -ne 0 ]; then
    echo "FAIL: make -n build-docker failed: $expanded"
    exit 1
fi
if ! printf '%s\n' "$expanded" | sh -n 2>/tmp/mk-syntax-err.txt; then
    echo "FAIL: the build-docker recipe does not parse as shell:"
    cat /tmp/mk-syntax-err.txt
    exit 1
fi
echo "ok: the build-docker recipe parses as shell (no quoting errors)"

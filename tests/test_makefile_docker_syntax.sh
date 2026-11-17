#!/usr/bin/env bash
# test_makefile_docker_syntax.sh — the build-docker recipe must be valid shell
# AND valid make. Two distinct failure modes, both caught here:
#   1. QUOTING: the recipe is a single-quoted bash -c string; an apostrophe
#      anywhere in it (even in a comment, e.g. "wayfire's") terminates the
#      quote, leaving an unterminated string.
#   2. CONTINUATION: every recipe line except the last must end with a
#      backslash (make continuation). A comment line without one ends the
#      bash -c command there — the quote never closes.
# Both only fail at runtime on the CI runner, AFTER a ~30-minute image
# build, so pin them here: expand the recipe (make -n), rejoin the
# backslash-continued lines into the logical commands the shell actually
# receives, and syntax-check each one.
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

# Rejoin backslash-continued lines into the logical commands make passes to
# sh. Each logical command is a SEPARATE sh -c invocation, so each must be
# self-contained: a quote opened on one must close on the same logical line
# (sh -n on a whole file would NOT catch this — sh legally joins an open
# quote across lines, but make does not).
logical="$(printf '%s\n' "$expanded" | awk '
    {
        line = $0
        while (line ~ /\\$/) {
            sub(/\\$/, "", line)
            if ((getline nl) > 0)
                line = line nl
            else
                break
        }
        print line
    }')"

fail=0
while IFS= read -r cmd; do
    [ -z "$cmd" ] && continue
    if ! sh -n -c "$cmd" 2>/tmp/mk-syntax-err.txt; then
        echo "FAIL: the build-docker recipe does not parse as shell:"
        cat /tmp/mk-syntax-err.txt
        echo "       offending command: ${cmd:0:100}..."
        fail=1
        break
    fi
done <<EOF
$logical
EOF
[ "$fail" -eq 0 ] && echo "ok: the build-docker recipe parses as shell (no quoting/continuation errors)"
exit "$fail"

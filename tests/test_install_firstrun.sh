#!/usr/bin/env bash
# test_install_firstrun.sh — install.sh must survive a CLEAN install: on a
# system with no previous default theme, refresh_default_theme() has no old
# dir to back up (backup stays empty). If its final line is
# `[ -n "$backup" ] && rm -rf "$backup"`, that line returns 1, the function
# returns 1, and install.sh's `set -e` kills the script AFTER all the work
# succeeded (observed in a clean ubuntu container: the log ends right after
# the libemeraldengine install, exit 1, no error message).
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

helper="$(sed -n '/^refresh_default_theme()/,/^}/p' "$SRC")"
if [ -n "$helper" ]; then
    echo "ok: refresh_default_theme helper exists"
else
    echo "FAIL: no refresh_default_theme helper"; fail=1
fi

# Behavioral, clean install: no pre-existing destination. install.sh runs
# with `set -euo pipefail`; the refresh call must return 0 when there is
# nothing to back up.
btmp="$(mktemp -d)"
trap 'rm -rf "$btmp"' EXIT
mkdir -p "$btmp/defaults/theme"
printf 'new' > "$btmp/defaults/theme/theme.ini"
mkdir -p "$btmp/clean"
DEFAULTS="$btmp/defaults"
eval "$(sed -n '/^DEFAULT_THEME_TMP=""/p' "$SRC")"
eval "$helper"
status=0
(
    set -euo pipefail
    refresh_default_theme "$btmp/clean/emeraldwf-defaults"
) || status=$?
if [ "$status" -eq 0 ]; then
    echo "ok: a clean (first) refresh returns 0"
else
    echo "FAIL: a clean (first) refresh returned $status (set -e would kill install.sh)"; fail=1
fi
if [ "$(cat "$btmp/clean/emeraldwf-defaults/theme/theme.ini" 2>/dev/null)" = "new" ]; then
    echo "ok: the clean refresh put the theme in place"
else
    echo "FAIL: the clean refresh did not put the theme in place"; fail=1
fi
leftover="$(ls -d "$btmp/clean"/.emeraldwf-defaults.* 2>/dev/null)"
if [ -z "$leftover" ]; then
    echo "ok: no leftover temp/backup dirs after a clean refresh"
else
    echo "FAIL: leftover temp/backup dirs: $leftover"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh's default-theme refresh survives a clean (first) install"

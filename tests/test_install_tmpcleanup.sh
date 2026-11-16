#!/usr/bin/env bash
# test_install_tmpcleanup.sh — install.sh's atomic_install / atomic_link create
# `.install.XXXXXX` / `.link.XXXXXX` temp files in the TARGET (system) dir and
# rename them into place. If the script is interrupted between the mktemp and
# the mv, the temp file is left behind in a system dir (e.g. /usr/lib/wayfire).
# Pre-fix there was no cleanup, so an interrupted install left stray temp
# files.
#
# The fix tracks the EXACT temp paths created by THIS invocation (each helper
# registers its temp in INSTALL_TMP_FILES right after mktemp) and removes only
# those on an EXIT trap. It does NOT use a dir-wide `.install.*` glob, which
# could delete a CONCURRENTLY running installer's temps (ChatGPT review).
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

# 1. The atomic helpers register their EXACT temp path (INSTALL_TMP_FILES+=),
#    so cleanup targets only this invocation's temps. Both helpers do it.
reg_refs="$(grep -cE 'INSTALL_TMP_FILES\+=' "$SRC")"
if [ "$reg_refs" -ge 2 ]; then
    echo "ok: atomic helpers register their exact temp path ($reg_refs sites)"
else
    echo "FAIL: helpers do not register their exact temp path (found $reg_refs, need >= 2)"; fail=1
fi

# 2. cleanup_on_exit removes the tracked temp files (iterates INSTALL_TMP_FILES).
if grep -qE 'for f in "\$\{INSTALL_TMP_FILES\[@\]\}"' "$SRC"; then
    echo "ok: cleanup iterates the tracked temp paths"
else
    echo "FAIL: cleanup does not iterate the tracked temp paths"; fail=1
fi

# 3. It is wired to an EXIT trap (so it runs on normal exit AND interruption).
if grep -qE 'trap [^ ]+ EXIT' "$SRC"; then
    echo "ok: an EXIT trap is set"
else
    echo "FAIL: no EXIT trap"; fail=1
fi

# 4. The dangerous dir-wide glob (rm -f .../.install.* / .link.*) is GONE.
if grep -qE 'rm -f .*(install|link)\.\*' "$SRC"; then
    echo "FAIL: a dir-wide .install.*/.link.* glob is present (could delete a concurrent installer's temps)"; fail=1
else
    echo "ok: no dir-wide .install.*/.link.* glob"
fi

# ── Behavioral: cleanup removes ONLY this invocation's temps ────────────────
btmp="$(mktemp -d)"
trap 'rm -rf "$btmp"' EXIT
mkdir -p "$btmp/dest"
# an "unrelated" temp from a CONCURRENT installer — must be PRESERVED.
printf 'x' > "$btmp/dest/.install.UNRELATED"

# Extract the helpers from install.sh (function bodies only).
for fn in atomic_install atomic_link cleanup_on_exit; do
    eval "$(sed -n "/^${fn}()/,/^}/p" "$SRC")"
done
INSTALL_TMP_FILES=()
DEFAULT_THEME_TMP=""

# Install a real file (its temp is registered, then renamed into place).
printf 'payload' > "$btmp/src.so"
atomic_install "$btmp/src.so" "$btmp/dest"
# Simulate an interruption: a temp that was registered but never mv'd.
leftover="$(mktemp "$btmp/dest/.install.XXXXXX")"
INSTALL_TMP_FILES+=("$leftover")

cleanup_on_exit

# 1. the installed file is intact.
if [ -f "$btmp/dest/src.so" ] && [ "$(cat "$btmp/dest/src.so")" = "payload" ]; then
    echo "ok: installed file intact after cleanup"
else
    echo "FAIL: installed file not intact after cleanup"; fail=1
fi
# 2. this invocation's in-flight (registered) temp is removed.
if [ ! -e "$leftover" ]; then
    echo "ok: this invocation's in-flight temp is removed"
else
    echo "FAIL: in-flight temp not removed: $leftover"; fail=1
fi
# 3. the UNRELATED temp (a concurrent installer) is PRESERVED.
if [ -f "$btmp/dest/.install.UNRELATED" ]; then
    echo "ok: unrelated temp (concurrent installer) preserved"
else
    echo "FAIL: unrelated temp was deleted (dir-wide glob)"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh cleans up atomic-install temp files on exit (exact paths only)"

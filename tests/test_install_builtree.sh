#!/usr/bin/env bash
# test_install_builtree.sh — install.sh must not blindly reuse a STALE build
# tree. Pre-fix the only configure condition was `[ ! -f build.ninja ]`, so a
# tree configured with a DIFFERENT wayfire prefix/libdir was reused as-is.
# That matters because libemeraldengine bakes ENGINE_DIR (<prefix>/<libdir>/
# emeraldwf/engines) in at compile time: a stale tree yields an engine that
# scans the wrong directory. The fix records the configured prefix/libdir in
# a marker and reconfigures when it no longer matches.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

# The staleness marker must be DEFINED (the literal path) and CONSULTED: the
# configure decision compares the marker's recorded prefix|libdir against the
# current one (so a stale tree is reconfigured, not reused).
if grep -qE '\.emeraldwf-cfg' "$SRC"; then
    echo "ok: build-tree staleness marker defined"
else
    echo "FAIL: no build-tree staleness marker defined (a stale tree is reused)"; fail=1
fi
if grep -qE 'CFG_MARKER.*!=.*WF_PREFIX.*WF_LIBDIR_REL' "$SRC"; then
    echo "ok: marker is compared against the current prefix/libdir"
else
    echo "FAIL: the marker is not compared against the current prefix/libdir"; fail=1
fi

# A stale tree must trigger a RECONFIGURE (not a silent reuse): the fix
# re-runs `meson setup --reconfigure` when the marker is stale.
if grep -qE 'meson setup --reconfigure' "$SRC"; then
    echo "ok: a stale tree triggers a reconfigure"
else
    echo "FAIL: no reconfigure path for a stale tree"; fail=1
fi

# A failed (re)configure must not proceed to build with stale settings.
if grep -qE 'meson setup .*[[:space:]]\|\| exit 1' "$SRC"; then
    echo "ok: a failed configure exits (no build with stale settings)"
else
    echo "FAIL: a failed configure does not exit"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh detects a stale build tree (prefix/libdir change)"

#!/usr/bin/env bash
# test_themer_runtime_dirs.sh — the docker-built themer must work on hosts
# whose libdir differs from the toolchain image's (the image is Ubuntu:
# lib/x86_64-linux-gnu; Gentoo uses /usr/lib64, Arch /usr/lib, RHEL
# /usr/lib64). Two baked values must not be image-specific:
#   1. the themer's RUNPATH (finds libemeraldengine.so.0) — must cover the
#      common distro libdirs, not just the build's own;
#   2. the engine dir the theme editor scans (ENGINE_DIR, baked into
#      libemeraldengine) — must be resolved AT RUNTIME from the library's
#      own location, with the baked prefix/libdir as a fallback only.
# The plugin's engine resolution is already location-independent (dladdr
# in emerald-settings.cpp) — only the themer was image-pinned, which made
# a docker install fail on non-Ubuntu hosts:
#   "error while loading shared libraries: libemeraldengine.so.0"
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
fail=0

# ── 1. RUNPATH coverage ────────────────────────────────────────────────────
# install.sh copies the build-tree binary with cp (meson's install-time
# rpath rewrite never runs), so the build_rpath is what the installed copy
# carries. It must cover the common distro libdirs.
TH="$ROOT/themer/meson.build"
[ -f "$TH" ] || { echo "FAIL: themer/meson.build not found"; exit 1; }
for ld in lib lib64 lib/x86_64-linux-gnu lib64/x86_64-linux-gnu; do
    if grep -qF "'$ld'" "$TH"; then
        echo "ok: themer rpath covers libdir $ld"
    else
        echo "FAIL: themer rpath must cover libdir '$ld' (image libdir is lib/x86_64-linux-gnu; hosts use lib/lib64/...)"; fail=1
    fi
done

# ── 2. ENGINE_DIR runtime resolution ───────────────────────────────────────
# The engine dir must be resolved from libemeraldengine.so's own location
# at runtime (installed: <so-dir>/engines; build tree: <so-dir>/../engines),
# not from a compile-time absolute path.
LE="$ROOT/libengine/meson.build"
[ -f "$LE" ] || { echo "FAIL: libengine/meson.build not found"; exit 1; }
if grep -qF 'ENGINE_DIR=emeraldwf_engine_dir' "$LE"; then
    echo "ok: ENGINE_DIR is the runtime resolver emeraldwf_engine_dir()"
else
    echo "FAIL: ENGINE_DIR must be emeraldwf_engine_dir() (runtime), not a baked absolute path"; fail=1
fi
if [ -f "$ROOT/compat/engine-dir.c" ]; then
    echo "ok: compat/engine-dir.c provides the runtime resolver"
else
    echo "FAIL: compat/engine-dir.c (the runtime engine-dir resolver) is missing"; fail=1
fi

exit "$fail"

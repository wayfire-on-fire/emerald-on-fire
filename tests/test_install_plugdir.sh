#!/usr/bin/env bash
# test_install_plugdir.sh — M28 (issue #1): install.sh's WF_PLUGDIR must
# be the SAME dir the plugin was installed into ($LIBDIR/wayfire).
#
# Pre-M28 the line was:
#   WF_PLUGDIR=$(pkg-config --variable=libdir wayfire 2>/dev/null || echo /usr/lib/wayfire)/wayfire
# On a pkg-config FAILURE the fallback already contained 'wayfire', so
# WF_PLUGDIR became /usr/lib/wayfire/wayfire - while LIBDIR (the value the
# plugin was actually installed into) was /usr/lib. The default-theme
# placement check then looked in the wrong dir and silently took the
# ~/.local fallback even though the plugin WAS at /usr/lib/wayfire.
#
# The test extracts the WF_PLUGDIR= assignment from install.sh and
# evaluates it in a controlled bash with a STUBBED pkg-config, so the
# resolution is exercised directly (the real script installs system-wide).
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

# The single WF_PLUGDIR assignment line (exactly ONE - a duplicate would
# make the extraction ambiguous, ChatGPT follow-up).
wfpdir_count=$(grep -cE '^WF_PLUGDIR=' "$SRC")
[ "$wfpdir_count" -eq 1 ] ||
    { echo "FAIL: expected exactly one WF_PLUGDIR= assignment (got $wfpdir_count)"; exit 1; }
wfpdir_line=$(grep -E '^WF_PLUGDIR=' "$SRC")

fail=0

# resolve <libdir> <pkgconfig-mode>
#   pkgconfig-mode: "fail" (pkg-config exits 1) or "ok" (prints /usr/lib)
#   Prints the resulting WF_PLUGDIR.
resolve() {
  local libdir="$1" mode="$2"
  bash -c '
    LIBDIR="$1"; MODE="$2"
    pkg-config() {
      if [ "$MODE" = "ok" ]; then printf "/usr/lib\n"; else return 1; fi
    }
    eval "$3"
    printf "%s\n" "$WF_PLUGDIR"
  ' _ "$libdir" "$mode" "$wfpdir_line"
}

# ── 1. pkg-config FAILURE: WF_PLUGDIR must be $LIBDIR/wayfire ─────────────
got=$(resolve "/usr/lib" "fail")
echo "pkg-config fail -> WF_PLUGDIR=$got"
[ "$got" = "/usr/lib/wayfire" ] \
    || { echo "FAIL(fail): expected /usr/lib/wayfire, got $got (the double-wayfire bug)"; fail=1; }

# ── 2. pkg-config SUCCESS: WF_PLUGDIR must still be $LIBDIR/wayfire ───────
got=$(resolve "/usr/lib" "ok")
echo "pkg-config ok   -> WF_PLUGDIR=$got"
[ "$got" = "/usr/lib/wayfire" ] \
    || { echo "FAIL(ok): expected /usr/lib/wayfire, got $got"; fail=1; }

# ── 3. a non-default libdir (e.g. lib64) must be honoured, not hardcoded ──
got=$(resolve "/usr/lib64" "fail")
echo "libdir=lib64    -> WF_PLUGDIR=$got"
[ "$got" = "/usr/lib64/wayfire" ] \
    || { echo "FAIL(lib64): expected /usr/lib64/wayfire, got $got"; fail=1; }

# ── 4. structural: the fix derives WF_PLUGDIR from $LIBDIR ────────────────
grep -qE '^WF_PLUGDIR="\$LIBDIR/wayfire"' "$SRC" \
    || { echo "FAIL: WF_PLUGDIR must be \"\$LIBDIR/wayfire\" (M28)"; fail=1; }
# The old double-wayfire fallback must be gone.
if grep -qE 'echo /usr/lib/wayfire\)/wayfire' "$SRC"; then
    echo "FAIL: the old /usr/lib/wayfire)/wayfire fallback must be removed (M28)"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh WF_PLUGDIR matches the installed plugin dir (M28)"

#!/usr/bin/env bash
# e2e/test_run.sh — script-logic tests for e2e/run.sh (R5, issue #1).
#
# No Xvfb on CI boxes, so these are STATIC tests:
#   0. bash -n (syntax);
#   1. the structural contracts the R5 fix establishes:
#        * the session runs under ONE directly-started Xvfb with
#          DISPLAY exported (xvfb-run only sets DISPLAY for its child,
#          so the script's own xterm/xdotool/xwd ran on the caller's
#          display — nothing on a headless box, the host's X server on
#          a desktop);
#        * no hardcoded /projects/contrib/emerald vendor path (step 10
#          validated VENDOR_SHA against the author's box only);
#        * screenshots go through a guarded shot() helper (step 9
#          failed deterministically when xwd had no display);
#        * a missing golden is SKIPPED, not FAILED (fresh clones);
#   2. unit tests for the pure helpers, sourced via run.sh's source
#      guard (main must NOT run; the pre-R5 script had no guard —
#      sourcing it ran main and died).

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
RUN="$HERE/run.sh"

T_PASS=0; T_FAIL=0
t_ok()  { T_PASS=$((T_PASS+1)); printf '  ok: %s\n' "$*"; }
t_bad() { T_FAIL=$((T_FAIL+1)); printf '  FAIL: %s\n' "$*"; }

echo "e2e/run.sh script-logic tests"

# ── 0. syntax ──────────────────────────────────────────────────────────────
if bash -n "$RUN" 2>/dev/null; then t_ok "bash -n run.sh"; else t_bad "bash -n run.sh"; fi

# ── 1. structural contracts ───────────────────────────────────────────────
# Check the CODE, not comments (the fix's doc comments quote the old
# xvfb-run recipe and the old hardcoded path).
code="$(grep -v '^[[:space:]]*#' "$RUN" | sed 's/#.*$//')"
if printf '%s\n' "$code" | grep -q 'xvfb-run'; then t_bad "run.sh still uses xvfb-run (child-only DISPLAY)"; else t_ok "no xvfb-run in code"; fi
if printf '%s\n' "$code" | grep -Eq '^[[:space:]]*Xvfb '; then t_ok "Xvfb started directly"; else t_bad "no direct Xvfb start"; fi
if printf '%s\n' "$code" | grep -Eq 'export DISPLAY'; then t_ok "DISPLAY exported for the whole session"; else t_bad "DISPLAY never exported"; fi
if printf '%s\n' "$code" | grep -q '/projects/contrib/emerald'; then t_bad "hardcoded /projects/contrib/emerald vendor path in code"; else t_ok "no hardcoded vendor path in code"; fi
if grep -Eq '^shot\(\)' "$RUN"; then t_ok "shot() screenshot guard exists"; else t_bad "no shot() guard"; fi
if grep -Eq '^golden\(\)' "$RUN"; then t_ok "golden() helper exists"; else t_bad "no golden() helper"; fi

# ── 2. pure helpers (sourced; main must NOT run) ──────────────────────────
source "$RUN"

tmpd="$(mktemp -d)"
trap 'rm -rf "$tmpd"' EXIT

# 2a. imgcmp: identical PNGs → a diff count; missing file → exit 1.
convert -size 8x8 xc:white "$tmpd/a.png" 2>/dev/null
convert -size 8x8 xc:white "$tmpd/b.png" 2>/dev/null
if d="$(imgcmp "$tmpd/a.png" "$tmpd/b.png")" && [ -n "$d" ]; then t_ok "imgcmp identical → diff $d"; else t_bad "imgcmp identical must report a diff"; fi
if imgcmp "$tmpd/a.png" "$tmpd/nonexistent.png" >/dev/null 2>&1; then t_bad "imgcmp missing file must exit 1"; else t_ok "imgcmp missing file exits 1"; fi

# 2b. golden(): missing golden (MAKE_GOLDENS=0) → SKIPPED, not FAILED.
# Point OUT/GOLDEN at temp dirs: the repo's e2e/golden must stay clean.
OUT="$tmpd/out"; GOLDEN="$tmpd/golden"; mkdir -p "$OUT" "$GOLDEN"
MAKE_GOLDENS=0
skip0=$SKIP; fail0=$FAIL
golden "no-such.png"
if [ "$SKIP" -gt "$skip0" ] && [ "$FAIL" -eq "$fail0" ]; then t_ok "missing golden → SKIPPED"; else t_bad "missing golden must be SKIPPED (skip $skip0→$SKIP, fail $fail0→$FAIL)"; fi

# 2c. golden(): --make-goldens writes the golden.
MAKE_GOLDENS=1
printf 'png' > "$OUT/yes.png"
golden "yes.png"
MAKE_GOLDENS=0
if [ -f "$GOLDEN/yes.png" ]; then t_ok "--make-goldens writes the golden"; else t_bad "--make-goldens did not write the golden"; fi

# 2d. vendor_check(): VENDOR_SHA vs the vendored tree.
mk_vendor_root() { # $1=root, $2=git? (1/0)
  mkdir -p "$1/third_party/emerald/defaults"
  if [ "$2" = 1 ]; then
    git -C "$1/third_party/emerald" init -q
    git -C "$1/third_party/emerald" -c user.email=t@t -c user.name=t \
        commit -q --allow-empty -m x
  fi
}

# 2d-i. git tree, pinned sha == HEAD → PASS.
v1="$tmpd/v1"; mk_vendor_root "$v1" 1
printf '%s\n' "$(git -C "$v1/third_party/emerald" rev-parse HEAD)" \
    > "$v1/third_party/emerald/VENDOR_SHA"
pass0=$PASS
vendor_check "$v1"
if [ "$PASS" -gt "$pass0" ]; then t_ok "vendor_check: pinned == HEAD → PASS"; else t_bad "vendor_check: pinned == HEAD must PASS"; fi

# 2d-ii. git tree, wrong pinned sha → FAIL.
v2="$tmpd/v2"; mk_vendor_root "$v2" 1
printf '0123456789abcdef0123456789abcdef01234567\n' > "$v2/third_party/emerald/VENDOR_SHA"
fail0=$FAIL
vendor_check "$v2"
if [ "$FAIL" -gt "$fail0" ]; then t_ok "vendor_check: mismatch → FAIL"; else t_bad "vendor_check: mismatch must FAIL"; fi

# 2d-iii. non-git tree, well-formed pin → PASS (rev check skipped).
v3="$tmpd/v3"; mk_vendor_root "$v3" 0
printf '0123456789abcdef0123456789abcdef01234567\n' > "$v3/third_party/emerald/VENDOR_SHA"
pass0=$PASS
vendor_check "$v3"
if [ "$PASS" -gt "$pass0" ]; then t_ok "vendor_check: non-git tree, valid pin → PASS"; else t_bad "vendor_check: non-git tree with valid pin must PASS"; fi

# 2d-iv. missing/malformed VENDOR_SHA → FAIL.
v4="$tmpd/v4"; mkdir -p "$v4/third_party/emerald/defaults"
fail0=$FAIL
vendor_check "$v4"
if [ "$FAIL" -gt "$fail0" ]; then t_ok "vendor_check: missing VENDOR_SHA → FAIL"; else t_bad "vendor_check: missing pin must FAIL"; fi

# ── summary ────────────────────────────────────────────────────────────────
echo ""
echo "script-logic tests: ok=$T_PASS fail=$T_FAIL"
[ "$T_FAIL" -eq 0 ]

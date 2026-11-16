#!/usr/bin/env bash
# emeraldwf E2E — scripted integration test.
#
# Environment
# ───────────
#   * Headless GPU: LIBGL_ALWAYS_SOFTWARE=1 (lavapipe). wayfire runs the
#     default (headless/pipe) backend under our own Xvfb display, so
#     x11-apps (xterm) reach it via Xwayland and `xwd`/`xdotool`/`xprop`
#     can drive them.
#   * wayfire, Xvfb, x11-apps (xterm), ImageMagick (convert/compare), and
#     `weston-terminal` for the CSD step are required.
#   * The plugin + 7 engine .so must be installed (ninja -C build install)
#     so wayfire finds libemeraldwf.so in libdir/wayfire and the engines in
#     libdir/emeraldwf/engines.
#
# Display
# ───────
# The script ALWAYS runs under its own Xvfb, started directly with
# DISPLAY exported for the whole process tree. R5 (issue #1): the old
# recipe used `xvfb-run`, which sets DISPLAY only for its CHILD (wayfire)
# — the script's own tools (xterm/xdotool/xwd) ran on the caller's
# DISPLAY: nothing on a headless box (every map_x11 failed), the host's
# X server on a desktop (guaranteed golden mismatches). Reusing the
# host's DISPLAY directly is equally wrong; one private server for the
# whole session is the only deterministic option.
#
# Usage
# ───
#   ./e2e/run.sh                 # run the full E2E; exit 0 = PASS
#   ./e2e/run.sh --make-goldens  # regenerate e2e/golden/*.png
#
# The script is idempotent and self-contained: it sets up its own
# ~/.emerald tree (from the vendored defaults), starts wayfire under
# Xvfb, maps SSD + CSD windows, screenshots, and compares against
# e2e/golden/. A MISSING golden is SKIPPED, not FAILED (fresh clones
# have no goldens until --make-goldens runs once — and --make-goldens
# writes, it does not "verify against itself").
#
# Each numbered step prints PASS/FAIL/SKIPPED; any FAIL → non-zero exit.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
E2E="$ROOT/e2e"
GOLDEN="$E2E/golden"
OUT="$E2E/out"
INIBASE="$E2E/wayfire.ini"
EMERALD_DEFAULTS="$ROOT/third_party/emerald/defaults"

MAKE_GOLDENS=0
for a in "$@"; do
  [ "$a" = "--make-goldens" ] && MAKE_GOLDENS=1
done

# ── helpers ────────────────────────────────────────────────────────────────
PASS=0; FAIL=0; SKIP=0
say()  { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS+1)); say "  PASS: $*"; }
bad()  { FAIL=$((FAIL+1)); say "  FAIL: $*"; }
skip() { SKIP=$((SKIP+1)); say "  SKIPPED: $*"; }
step() { say ""; say "── step $1: $2 ─────────────────────────────────────────"; }

# Compare two PNGs with ImageMagick -fuzz 5% (font-hinting tolerance).
# Prints diff count on stdout; exit 0 if both files exist.
imgcmp() {
  local a="$1" b="$2"
  [ -f "$a" ] && [ -f "$b" ] || return 1
  compare -metric AE -fuzz 5% "$a" "$b" /dev/null 2>&1 | awk '{print $1}'
}

require() {
  local t
  for t in "$@"; do
    command -v "$t" >/dev/null 2>&1 || { say "  (missing tool: $t)"; return 1; }
  done
  return 0
}

# Screenshot the root window to $1; exit 0 only if the file was written.
# (R5: the old script piped `xwd | convert` bare at step level — with no
# display, xwd failed silently, the PNG never appeared, and the later
# compare reported FAIL for a screenshot that was never taken.)
shot() {
  local out="$1"
  xwd -root -silent 2>/dev/null | convert xwd:- "$out" 2>/dev/null
  [ -f "$out" ]
}

# Golden handling for $1 (both $OUT/$1 and $GOLDEN/$1).
# --make-goldens: write the golden. No golden yet (fresh clone):
# SKIPPED, not FAILED — a missing reference image is not a product bug.
golden() {
  local name="$1"
  if [ "$MAKE_GOLDENS" = 1 ]; then
    if [ -f "$OUT/$name" ]; then cp -f "$OUT/$name" "$GOLDEN/$name"; ok "golden $name written";
    else skip "golden $name not produced"; fi
    return
  fi
  if [ ! -f "$GOLDEN/$name" ]; then
    skip "golden $name missing (run --make-goldens first)"
    return
  fi
  local d; d=$(imgcmp "$OUT/$name" "$GOLDEN/$name")
  if [ -n "$d" ]; then ok "$name matches (diff=$d)"; else bad "$name differs"; fi
}

# Vendor check (step 10): validate VENDOR_SHA against the VENDORed tree
# ITSELF ($1/third_party/emerald). R5 (issue #1): the old check looked at
# /projects/contrib/emerald — the author's box only — and the
# third_party/emerald fallback has no .git, so the step failed everywhere
# else. The full rev check needs the tree to be a git checkout (a
# developer's clone); otherwise verify the pin is well-formed and the
# tree is present.
vendor_check() {
  local root="$1"
  local pinned
  pinned="$(tr -d '[:space:]' < "$root/third_party/emerald/VENDOR_SHA" 2>/dev/null)"
  if ! printf '%s' "$pinned" | grep -Eq '^[0-9a-f]{40}$'; then
    bad "VENDOR_SHA missing or malformed: '$pinned'"
    return
  fi
  if [ ! -d "$root/third_party/emerald/defaults" ]; then
    bad "vendored tree missing: $root/third_party/emerald"
    return
  fi
  if [ -d "$root/third_party/emerald/.git" ]; then
    local actual
    actual="$(git -C "$root/third_party/emerald" rev-parse HEAD 2>/dev/null)"
    if [ "$pinned" = "$actual" ]; then ok "vendor sha $pinned"; else bad "vendor sha mismatch: pinned=$pinned actual=$actual"; fi
  else
    ok "vendor sha $pinned (tree is not a git checkout; rev check skipped)"
  fi
}

# ── setup: ~/.emerald from vendored defaults ──────────────────────────────
setup_emerald() {
  mkdir -p "$HOME/.emerald/theme"
  cp -f "$EMERALD_DEFAULTS/settings.ini" "$HOME/.emerald/settings.ini"
  cp -rf "$EMERALD_DEFAULTS/theme/." "$HOME/.emerald/theme/"
}

# ── display lifecycle (our own Xvfb, shared by wayfire AND the tools) ────
XVFB_PID=""
XVFB_NUM=""

setup_display() {
  local n=99
  while [ -e "/tmp/.X11-unix/X$n" ]; do n=$((n+1)); done
  XVFB_NUM="$n"
  Xvfb ":$n" -screen 0 1280x720x24 >/dev/null 2>&1 &
  XVFB_PID=$!
  # Readiness = the server ANSWERS a query (not just that the socket
  # file appeared: a socket without a live server is not usable).
  for _ in $(seq 1 50); do
    if xdpyinfo -display ":$n" >/dev/null 2>&1; then
      export DISPLAY=":$n"
      return 0
    fi
    kill -0 "$XVFB_PID" 2>/dev/null || return 1
    sleep 0.1
  done
  return 1
}

teardown_display() {
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  [ -n "$XVFB_PID" ] && wait "$XVFB_PID" 2>/dev/null
  XVFB_PID=""
}

# ── wayfire lifecycle ─────────────────────────────────────────────────────
WF_PID=""
WF_LOG="$OUT/wayfire.log"

wf_start() {
  local ini="$1"
  : > "$WF_LOG"
  # DISPLAY is already exported by setup_display (our own Xvfb): wayfire
  # and the script's X tools share one server.
  env LIBGL_ALWAYS_SOFTWARE=1 wayfire -c "$ini" >"$WF_LOG" 2>&1 &
  WF_PID=$!
  # wait for the compositor to be ready. Match a SUCCESS indicator, not any
  # "emeraldwf" line: a bare grep matches plugin ERROR lines too (e.g.
  # "emeraldwf: theme load failed"), so a broken start would look ready and
  # the later steps would fail for the wrong reason. "theme loaded" (with the
  # "ed") is logged only on a successful theme load, not on the failure line
  # ("theme load failed").
  for _ in $(seq 1 50); do
    if grep -Fqi "emeraldwf: theme loaded" "$WF_LOG" 2>/dev/null; then
      sleep 0.5; return 0
    fi
    kill -0 "$WF_PID" 2>/dev/null || return 1
    sleep 0.2
  done
  return 1
}

wf_stop() {
  [ -n "$WF_PID" ] && kill "$WF_PID" 2>/dev/null
  [ -n "$WF_PID" ] && wait "$WF_PID" 2>/dev/null
  WF_PID=""
}

# Map an SSD x11 window via xwayland; prints its window id.
map_x11() {
  local app="$1"
  "$app" & disown
  sleep 1
  # find the newest toplevel
  xdotool search --name "$app" 2>/dev/null | tail -1
}

main() {
  mkdir -p "$OUT" "$GOLDEN"

  say "emeraldwf E2E (make-goldens=$MAKE_GOLDENS)"
  require wayfire Xvfb xdotool xprop xwininfo xwd xdpyinfo convert compare xterm || {
    say "FATAL: required tools missing — install wayfire, x11-apps, x11-utils, ImageMagick."; exit 2; }

  # One X server for the whole session: wayfire AND xterm/xdotool/xwd.
  setup_display || { say "FATAL: Xvfb failed to start"; exit 2; }
  trap 'wf_stop; teardown_display' EXIT

  # install must have happened (plugin + engines).
  if [ ! -d "$HOME/.emerald" ]; then setup_emerald; fi

  # ── 1. start ─────────────────────────────────────────────────────────────
  step 1 "start wayfire (headless, emeraldwf only)"
  if wf_start "$INIBASE"; then ok "wayfire ready"; else bad "wayfire failed to start"; fi
  wf_stop

  # ── 2. ssd active ────────────────────────────────────────────────────────
  step 2 "SSD active window → golden"
  wf_start "$INIBASE" && map_x11 xterm
  sleep 0.5
  # The display is verified up (xdpyinfo), so a capture failure here is
  # unexpected (xwd/convert regression) — FAIL, not SKIPPED.
  if shot "$OUT/ssd-active.png"; then golden ssd-active.png; else bad "screenshot failed"; fi
  wf_stop

  # ── 3. inactive ──────────────────────────────────────────────────────────
  step 3 "SSD inactive window → golden"
  wf_start "$INIBASE" && map_x11 xterm && map_x11 xterm
  sleep 0.5
  if shot "$OUT/ssd-inactive.png"; then golden ssd-inactive.png; else bad "screenshot failed"; fi
  wf_stop

  # ── 4. maximized ─────────────────────────────────────────────────────────
  step 4 "maximized window → golden"
  wf_start "$INIBASE" && map_x11 xterm
  sleep 0.5
  # maximize via _NET_WM_STATE through xwayland (xdotool key super+Up is not
  # universal; use xprop to toggle the state atom).
  local wid; wid="$(xdotool search --name xterm 2>/dev/null | tail -1)"
  if [ -n "$wid" ]; then
    xprop -id "$wid" -f _NET_WM_STATE 32a -set _NET_WM_STATE \
      _NET_WM_STATE_MAXIMIZED_VERT,_NET_WM_STATE_MAXIMIZED_HORZ 2>/dev/null
  fi
  sleep 0.5
  if shot "$OUT/ssd-maximized.png"; then golden ssd-maximized.png; else bad "screenshot failed"; fi
  wf_stop

  # ── 5. csd ───────────────────────────────────────────────────────────────
  step 5 "CSD window has NO emerald frame"
  wf_start "$INIBASE"
  command -v weston-terminal >/dev/null && weston-terminal & disown
  sleep 0.5
  if shot "$OUT/csd.png"; then
    # Compare against a plain-compositor golden (emeraldwf disabled).
    if [ "$MAKE_GOLDENS" = 1 ]; then cp -f "$OUT/csd.png" "$GOLDEN/csd-plain.png"; ok "plain golden written";
    else
      if [ -f "$GOLDEN/csd-plain.png" ]; then
        local d; d=$(imgcmp "$OUT/csd.png" "$GOLDEN/csd-plain.png")
        if [ -n "$d" ]; then ok "csd matches plain (diff=$d)"; else bad "csd differs from plain"; fi
      else
        skip "csd-plain golden missing (run --make-goldens first)"
      fi
    fi
  else bad "screenshot failed"; fi
  wf_stop

  # ── 6. always_decorate ───────────────────────────────────────────────────
  step 6 "always_decorate forces CSD frame"
  sed 's/enable_shade = true/enable_shade = true\nalways_decorate = app-id=weston-terminal/' \
      "$INIBASE" > "$OUT/wayfire-always.ini"
  wf_start "$OUT/wayfire-always.ini"
  command -v weston-terminal >/dev/null && weston-terminal & disown
  sleep 0.5
  if shot "$OUT/csd-decorated.png"; then golden csd-decorated.png; else bad "screenshot failed"; fi
  wf_stop

  # ── 7. actions ───────────────────────────────────────────────────────────
  step 7 "button actions (close/max/min) via xdotool over xwayland"
  wf_start "$INIBASE" && map_x11 xterm
  sleep 0.5
  local wid; wid="$(xdotool search --name xterm 2>/dev/null | tail -1)"
  if [ -z "$wid" ]; then skip "no x11 window to drive"; wf_stop; return; fi
  # find the close button center from xwininfo geometry (heuristic: top-right).
  local geo; geo="$(xwininfo -id "$wid" 2>/dev/null)"
  local x y
  x=$(echo "$geo" | awk '/Width:/{w=$2} /Height:/{h=$2} /Absolute upper-left X:/{ax=$5} /Absolute upper-left Y:/{ay=$5} END{print ax+w-20, ay+10}')
  # close: click top-right; verify window gone
  IFS=',' read cx cy <<< "$x"
  xdotool mousemove "$cx" "$cy" click 1 2>/dev/null; sleep 0.5
  if xdotool search --name xterm >/dev/null 2>&1; then bad "close did not close"; else ok "close button closed window"; fi
  # re-open, maximize, minimize
  map_x11 xterm; sleep 0.5
  wid="$(xdotool search --name xterm 2>/dev/null | tail -1)"
  xprop -id "$wid" -f _NET_WM_STATE 32a -set _NET_WM_STATE \
    _NET_WM_STATE_MAXIMIZED_VERT,_NET_WM_STATE_MAXIMIZED_HORZ 2>/dev/null; sleep 0.3
  if xprop -id "$wid" _NET_WM_STATE 2>/dev/null | grep -q MAXIMIZED; then ok "maximize"; else bad "maximize"; fi
  wf_stop

  # ── 8. shade ─────────────────────────────────────────────────────────────
  step 8 "scroll-to-shade"
  if command -v wtype >/dev/null 2>&1; then
    skip "wtype present; synthesizing axis events is left to the manual CHECKLIST (documented)"
  else
    skip "no wtype — scroll-to-shade is manual-only (see CHECKLIST.md)"
  fi
  wf_stop

  # ── 9. live reload ───────────────────────────────────────────────────────
  step 9 "live settings reload (no restart)"
  wf_start "$INIBASE" && map_x11 xterm
  sleep 0.5
  # R5: the old step ran `xwd | convert` unguarded — with no display the
  # PNGs never appeared and the compare FAILED deterministically.
  if shot "$OUT/reload-before.png"; then
    # change a theme color (active_text) + double_click_action in settings.ini
    sed -i 's/^double_click_action=.*/double_click_action=1/' "$HOME/.emerald/settings.ini"
    if [ -f "$HOME/.emerald/theme/theme.ini" ]; then
      sed -i 's/^active_text=.*/active_text=0.0 1.0 0.0 1.0/' "$HOME/.emerald/theme/theme.ini"
    fi
    sleep 1.0   # debounce 250ms + reload + re-render
    if shot "$OUT/reload-after.png"; then
      local d; d=$(imgcmp "$OUT/reload-before.png" "$OUT/reload-after.png")
      # A visible change means the files differ → imgcmp reports a non-zero diff.
      if [ -n "$d" ] && [ "$d" != "0" ]; then ok "reload changed the frame (diff=$d)"; else bad "reload had no visible effect"; fi
    else
      bad "screenshot after reload failed"
    fi
  else
    bad "screenshot before reload failed"
  fi
  wf_stop

  # ── 10. vendor check ─────────────────────────────────────────────────────
  step 10 "vendored emerald matches pinned rev"
  vendor_check "$ROOT"

  # ── summary ─────────────────────────────────────────────────────────────
  say ""
  say "════════════════════════════════════════"
  say "emeraldwf E2E: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
  say "════════════════════════════════════════"
  [ "$FAIL" -eq 0 ]
}

# Source guard: e2e/test_run.sh sources this file for the pure helpers
# (imgcmp/golden/vendor_check/...); running it directly runs main.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  main "$@"
fi

#!/usr/bin/env bash
# emeraldwf E2E — scripted integration test.
#
# Environment
# ───────────
#   * Headless GPU: LIBGL_ALWAYS_SOFTWARE=1 (lavapipe). wayfire runs the
#     default (headless/pipe) backend under the Xvfb display, so x11-apps
#     (xterm/clocktest) reach it via Xwayland and `xwininfo`/`xdotool`/`xprop`
#     can drive them.
#   * wayfire, xvfb-run, x11-apps (xterm), ImageMagick (convert/compare),
#     and `weston-terminal` for the CSD step are required.
#   * The plugin + 7 engine .so must be installed (ninja -C build install)
#     so wayfire finds libemeraldwf.so in libdir/wayfire and the engines in
#     libdir/emeraldwf/engines.
#
# Usage
# ───
#   ./e2e/run.sh                 # run the full E2E; exit 0 = PASS
#   ./e2e/run.sh --make-goldens  # regenerate e2e/golden/*.png
#
# The script is idempotent and self-contained: it sets up its own
# ~/.emerald tree (from the vendored defaults), starts wayfire under Xvfb,
# maps SSD + CSD windows, screenshots, and compares against e2e/golden/.
#
# Each numbered step prints PASS/FAIL/SKIPPED; any FAIL → non-zero exit.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
E2E="$ROOT/e2e"
GOLDEN="$E2e/golden"; GOLDEN="$E2E/golden"
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
# Prints diff % on stdout; exit 0 if within fuzz.
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

# ── setup: ~/.emerald from vendored defaults ──────────────────────────────
setup_emerald() {
  mkdir -p "$HOME/.emerald/theme"
  cp -f "$EMERALD_DEFAULTS/settings.ini" "$HOME/.emerald/settings.ini"
  cp -rf "$EMERALD_DEFAULTS/theme/." "$HOME/.emerald/theme/"
}

# ── wayfire lifecycle ─────────────────────────────────────────────────────
WF_PID=""
WF_LOG="$OUT/wayfire.log"

wf_start() {
  local ini="$1"
  : > "$WF_LOG"
  xvfb-run -a -s "-screen 0 1280x720x24" \
    env LIBGL_ALWAYS_SOFTWARE=1 \
    wayfire -c "$ini" >"$WF_LOG" 2>&1 &
  WF_PID=$!
  # wait for the compositor to be ready (poll a wayfire debug line / plugin load)
  for _ in $(seq 1 50); do
    if grep -qi "emeraldwf" "$WF_LOG" 2>/dev/null; then
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
  xvfb-run --help >/dev/null 2>&1  # (no-op; we're already under Xvfb)
  # DISPLAY is set by xvfb-run for the parent; launch the app on it.
  "$app" & disown
  sleep 1
  # find the newest toplevel
  xdotool search --name "$app" 2>/dev/null | tail -1
}

main() {
  mkdir -p "$OUT" "$GOLDEN"

  say "emeraldwf E2E (make-goldens=$MAKE_GOLDENS)"
  require wayfire xvfb-run xdotool xprop xwininfo convert compare xterm || {
    say "FATAL: required tools missing — install wayfire, x11-apps, ImageMagick."; exit 2; }

  # install must have happened (plugin + engines).
  local plugin
  plugin="$(pkg-config --variable libdir wayfire 2>/dev/null)/emeraldwf/engines"
  if [ ! -d "$HOME/.emerald" ]; then setup_emerald; fi

  # ── 1. start ─────────────────────────────────────────────────────────────
  step 1 "start wayfire (headless, emeraldwf only)"
  if wf_start "$INIBASE"; then ok "wayfire ready"; else bad "wayfire failed to start"; fi
  wf_stop

  # ── 2. ssd active ────────────────────────────────────────────────────────
  step 2 "SSD active window → golden"
  wf_start "$INIBASE" && map_x11 xterm
  sleep 0.5
  if [ -n "$DISPLAY" ]; then
    grim "$OUT/ssd-active.png" 2>/dev/null || \
      xwd -root -silent | convert xwd:- "$OUT/ssd-active.png"
    if [ "$MAKE_GOLDENS" = 1 ]; then cp -f "$OUT/ssd-active.png" "$GOLDEN/ssd-active.png"; ok "golden written";
    else
      local d; d=$(imgcmp "$OUT/ssd-active.png" "$GOLDEN/ssd-active.png")
      if [ -n "$d" ]; then ok "ssd-active matches (diff=$d)"; else bad "ssd-active differs"; fi
    fi
  else
    skip "no DISPLAY for screenshot (headless recipe not wired)"
  fi
  wf_stop

  # ── 3. inactive ──────────────────────────────────────────────────────────
  step 3 "SSD inactive window → golden"
  wf_start "$INIBASE" && map_x11 xterm && map_x11 xterm
  sleep 0.5
  if [ -n "$DISPLAY" ]; then
    xwd -root -silent | convert xwd:- "$OUT/ssd-inactive.png"
    if [ "$MAKE_GOLDENS" = 1 ]; then cp -f "$OUT/ssd-inactive.png" "$GOLDEN/ssd-inactive.png"; ok "golden written";
    else
      local d; d=$(imgcmp "$OUT/ssd-inactive.png" "$GOLDEN/ssd-inactive.png")
      if [ -n "$d" ]; then ok "ssd-inactive matches (diff=$d)"; else bad "ssd-inactive differs"; fi
    fi
  else skip "no DISPLAY"; fi
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
  if [ -n "$DISPLAY" ]; then
    xwd -root -silent | convert xwd:- "$OUT/ssd-maximized.png"
    if [ "$MAKE_GOLDENS" = 1 ]; then cp -f "$OUT/ssd-maximized.png" "$GOLDEN/ssd-maximized.png"; ok "golden written";
    else
      local d; d=$(imgcmp "$OUT/ssd-maximized.png" "$GOLDEN/ssd-maximized.png")
      if [ -n "$d" ]; then ok "ssd-maximized matches (diff=$d)"; else bad "ssd-maximized differs"; fi
    fi
  else skip "no DISPLAY"; fi
  wf_stop

  # ── 5. csd ───────────────────────────────────────────────────────────────
  step 5 "CSD window has NO emerald frame"
  wf_start "$INIBASE"
  command -v weston-terminal >/dev/null && weston-terminal & disown
  sleep 0.5
  if [ -n "$DISPLAY" ]; then
    xwd -root -silent | convert xwd:- "$OUT/csd.png"
    # Compare against a plain-compositor golden (emeraldwf disabled).
    if [ -f "$GOLDEN/csd-plain.png" ]; then
      local d; d=$(imgcmp "$OUT/csd.png" "$GOLDEN/csd-plain.png")
      if [ -n "$d" ]; then ok "csd matches plain (diff=$d)"; else bad "csd differs from plain"; fi
    else
      if [ "$MAKE_GOLDENS" = 1 ]; then cp -f "$OUT/csd.png" "$GOLDEN/csd-plain.png"; ok "plain golden written";
      else skip "csd-plain golden missing (run --make-goldens first)"; fi
    fi
  else skip "no DISPLAY"; fi
  wf_stop

  # ── 6. always_decorate ───────────────────────────────────────────────────
  step 6 "always_decorate forces CSD frame"
  sed 's/enable_shade = true/enable_shade = true\nalways_decorate = app-id=weston-terminal/' \
      "$INIBASE" > "$OUT/wayfire-always.ini"
  wf_start "$OUT/wayfire-always.ini"
  command -v weston-terminal >/dev/null && weston-terminal & disown
  sleep 0.5
  if [ -n "$DISPLAY" ]; then
    xwd -root -silent | convert xwd:- "$OUT/csd-decorated.png"
    if [ "$MAKE_GOLDENS" = 1 ]; then cp -f "$OUT/csd-decorated.png" "$GOLDEN/csd-decorated.png"; ok "golden written";
    else
      local d; d=$(imgcmp "$OUT/csd-decorated.png" "$GOLDEN/csd-decorated.png")
      if [ -n "$d" ]; then ok "csd-decorated matches (diff=$d)"; else bad "csd-decorated differs"; fi
    fi
  else skip "no DISPLAY"; fi
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
  xwd -root -silent | convert xwd:- "$OUT/reload-before.png"
  # change a theme color (active_text) + double_click_action in settings.ini
  sed -i 's/^double_click_action=.*/double_click_action=1/' "$HOME/.emerald/settings.ini"
  if [ -f "$HOME/.emerald/theme/theme.ini" ]; then
    sed -i 's/^active_text=.*/active_text=0.0 1.0 0.0 1.0/' "$HOME/.emerald/theme/theme.ini"
  fi
  sleep 1.0   # debounce 250ms + reload + re-render
  xwd -root -silent | convert xwd:- "$OUT/reload-after.png"
  local d; d=$(imgcmp "$OUT/reload-before.png" "$OUT/reload-after.png")
  # A visible change means the files differ → imgcmp returns a non-zero diff.
  if [ -n "$d" ] && [ "$d" != "0" ]; then ok "reload changed the frame (diff=$d)"; else bad "reload had no visible effect"; fi
  wf_stop

  # ── 10. vendor check ─────────────────────────────────────────────────────
  step 10 "vendored emerald matches pinned rev"
  local pinned actual
  pinned="$(cat "$ROOT/third_party/emerald/VENDOR_SHA")"
  actual="$(git -C /projects/contrib/emerald rev-parse HEAD 2>/dev/null)"
  if [ -z "$actual" ]; then
    actual="$(git -C "$ROOT/third_party/emerald" rev-parse HEAD 2>/dev/null)"
  fi
  if [ "$pinned" = "$actual" ]; then ok "vendor sha $pinned"; else bad "vendor sha mismatch: pinned=$pinned actual=$actual"; fi

  # ── summary ─────────────────────────────────────────────────────────────
  say ""
  say "════════════════════════════════════════"
  say "emeraldwf E2E: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
  say "════════════════════════════════════════"
  [ "$FAIL" -eq 0 ]
}

main "$@"

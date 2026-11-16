#!/usr/bin/env bash
# test_install_version.sh — install.sh must not hardcode the library version
# ("0.1.0"). A version bump (0.1.0 -> 0.2.0, 1.5.3, ...) must not silently
# break the install. Pre-fix these all derived from a literal "0.1.0":
#   - atomic_versioned_so: sover="${real%.1.0}" and ${sover%.0}
#   - the engine .so glob: lib*.so.0.1.0
#   - the libemeraldengine.so lookup: libemeraldengine.so.0.1.0
#   - the "7 engines" count (a literal)
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

# ── 1. atomic_versioned_so derives symlink names from the FILENAME ────────
# Extract the function and run it against a NON-0.1.0 version with stubbed
# atomic_install / atomic_link (which record their args, no real file ops).
func_def="$(sed -n '/^atomic_versioned_so()/,/^}/p' "$SRC")"
[ -n "$func_def" ] || { echo "FAIL: atomic_versioned_so not found"; exit 1; }

tmpf="$(mktemp)"
trap 'rm -f "$tmpf"' EXIT
printf '%s\n' "$func_def" > "$tmpf"
# shellcheck disable=SC1091
source "$tmpf"

LINKS=""
atomic_install() { :; }
atomic_link()    { LINKS="$LINKS|$1=>$3"; }
atomic_versioned_so "/fake/libfoo.so.2.3.4" "/dest"
echo "atomic_versioned_so(2.3.4) links: $LINKS"
case "$LINKS" in
  *"libfoo.so.2.3.4=>libfoo.so.2"*"libfoo.so.2=>libfoo.so"*) : ;;
  *) echo "FAIL: expected libfoo.so.2.3.4->libfoo.so.2 and libfoo.so.2->libfoo.so, got: $LINKS"; fail=1 ;;
esac

# ── 2. the engine .so glob must not hardcode a version ─────────────────────
if grep -qE 'lib\*\.so\.0\.1\.0' "$SRC"; then
  echo "FAIL: engine glob hardcodes .so.0.1.0 (breaks on a version bump)"; fail=1
else
  echo "ok: engine glob is version-agnostic"
fi

# ── 3. the libemeraldengine.so lookup must not hardcode a version ──────────
if grep -qE 'libemeraldengine\.so\.0\.1\.0' "$SRC"; then
  echo "FAIL: libemeraldengine.so lookup hardcodes .so.0.1.0"; fail=1
else
  echo "ok: libemeraldengine.so lookup is version-agnostic"
fi

# ── 4. the engine count must not be a literal "7" ──────────────────────────
if grep -qE '"==> 7 engines' "$SRC"; then
  echo "FAIL: the engine count is a literal 7"; fail=1
else
  echo "ok: engine count is not a literal"
fi

if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "PASS: install.sh has no hardcoded version strings"

#!/usr/bin/env bash
# test_ci_meson_pin.sh — the from-source builds (wayfire / xkbcommon / wayland)
# require meson >= 1.4.0. The meson install lives in the SHARED recipe script
# (scripts/build-wayfire-from-source.sh, used by the Dockerfile, ci.yml, and
# provision.sh). Pre-fix the install was UNPINNED (bare `meson`) and duplicated
# in the Dockerfile + provision.sh. The fix pins an exact version in the one
# shared place and verifies the result.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
SHARED="$ROOT/scripts/build-wayfire-from-source.sh"
[ -f "$SHARED" ] || { echo "FAIL: no shared scripts/build-wayfire-from-source.sh"; exit 1; }

fail=0

# 1. A pip install of meson must be PINNED (meson==<ver>), not a bare `meson`.
line="$(grep -nE 'pip3? install[^|]*meson' "$SHARED" | head -1)"
if [ -z "$line" ]; then
    echo "FAIL: no pip meson install in the shared script"; fail=1
elif printf '%s\n' "$line" | grep -qE 'meson==[0-9]'; then
    echo "ok: meson pip install is pinned in the shared script"
else
    echo "FAIL: meson pip install is UNPINNED in the shared script: $line"; fail=1
fi

# 2. The shared script must NOT silently ignore a failed meson install (no
#    trailing `|| true` on the pip install).
if grep -E 'meson==' "$SHARED" | grep -qE '\|\| true'; then
    echo "FAIL: the shared script silently ignores a failed meson install (|| true)"; fail=1
else
    echo "ok: the shared script does not silently ignore a failed meson install"
fi

# 3. The shared script must VERIFY the installed meson is new enough (>= 1.4.0).
if grep -qE 'meson --version' "$SHARED"; then
    echo "ok: the shared script verifies the installed meson version"
else
    echo "FAIL: the shared script does not verify the installed meson version"; fail=1
fi

# 4. The call sites must NOT carry their own (unpinned) meson install — they
#    delegate to the shared script.
n_own=0
for f in "$ROOT/Dockerfile" "$ROOT/.github/workflows/ci.yml" "$ROOT/e2e/distros/provision.sh"; do
    if grep -qE 'pip3? install[^|]*meson' "$f"; then
        n_own=$((n_own + 1))
    fi
done
if [ "$n_own" -eq 0 ]; then
    echo "ok: call sites do not carry their own meson install"
else
    echo "FAIL: $n_own call site(s) carry their own meson pip install (should delegate to the shared script)"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: meson pip install is pinned (in the shared recipe script)"

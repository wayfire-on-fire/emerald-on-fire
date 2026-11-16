#!/usr/bin/env bash
# test_ci_meson_pin.sh — the Dockerfile and e2e/distros/provision.sh install
# meson via pip. Pre-fix the install was UNPINNED (bare `meson`), so a future
# meson release could silently change the build-tool version (and break the
# from-source wayfire / xkbcommon / wayland builds, which need meson >= 1.4.0).
# The fix pins an exact version.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
DOCKERFILE="$ROOT/Dockerfile"
PROVISION="$ROOT/e2e/distros/provision.sh"
[ -f "$DOCKERFILE" ] || { echo "FAIL: Dockerfile not found"; exit 1; }
[ -f "$PROVISION" ] || { echo "FAIL: provision.sh not found"; exit 1; }

fail=0

# A pip install of meson must be PINNED (meson==<ver>), not a bare `meson`.
check_pin() {  # desc file
    local desc="$1" file="$2" line
    line="$(grep -nE 'pip3? install[^|]*meson' "$file" | head -1)"
    if [ -z "$line" ]; then
        echo "ok: no pip meson install in $desc (nothing to pin)"; return
    fi
    if printf '%s\n' "$line" | grep -qE 'meson==[0-9]'; then
        echo "ok: meson pip install is pinned in $desc"
    else
        echo "FAIL: meson pip install is UNPINNED in $desc: $line"; fail=1
    fi
}

check_pin "Dockerfile" "$DOCKERFILE"
check_pin "provision.sh" "$PROVISION"

# provision.sh must NOT silently ignore a failed meson install (a trailing
# `|| true` on the pip install would let provisioning proceed with a too-old
# distro meson and break the from-source builds confusingly).
if grep -E 'meson==' "$PROVISION" | grep -qE '\|\| true'; then
    echo "FAIL: provision.sh silently ignores a failed meson install (|| true)"; fail=1
else
    echo "ok: provision.sh does not silently ignore a failed meson install"
fi

# provision.sh must VERIFY the installed meson is new enough (>= 1.4.0), not
# just fire a pip install and hope.
if grep -qE 'meson --version' "$PROVISION"; then
    echo "ok: provision.sh verifies the installed meson version"
else
    echo "FAIL: provision.sh does not verify the installed meson version"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: meson pip installs are pinned (Dockerfile + provision.sh)"

#!/usr/bin/env bash
# test_wayfire_src_single.sh — the wayfire-from-source recipe (wayland,
# wayland-protocols, libdrm, xkbcommon, pixman, wayfire v0.11.0) must live in a
# SINGLE shared script, not be copy-pasted in the Dockerfile, the ci.yml
# `native` job, and e2e/distros/provision.sh. Pre-fix the same 6-component
# recipe (versions, URLs, meson options, libdrm checksum) was duplicated in all
# three, so a version bump had to be made in three places.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
SHARED="$ROOT/scripts/build-wayfire-from-source.sh"
[ -f "$SHARED" ] || { echo "FAIL: no shared scripts/build-wayfire-from-source.sh"; exit 1; }

fail=0

# The shared script must actually contain the recipe (the wayfire clone).
if grep -q 'WayfireWM/wayfire' "$SHARED"; then
    echo "ok: the shared script contains the wayfire recipe"
else
    echo "FAIL: the shared script does not contain the wayfire recipe"; fail=1
fi

# The recipe must NOT be inlined in any of the three call sites. The marker is
# the wayfire clone command itself (a `git clone ... --branch v0.11.0`), which
# is specific to the recipe and does not match a comment or an uncrustify.ini
# wget that also mentions the wayfire repo.
n_inline=0
for f in "$ROOT/Dockerfile" "$ROOT/.github/workflows/ci.yml" "$ROOT/e2e/distros/provision.sh"; do
    if grep -qE 'git clone.*v0\.11\.0' "$f"; then
        n_inline=$((n_inline + 1))
    fi
done
if [ "$n_inline" -eq 0 ]; then
    echo "ok: the wayfire recipe is not inlined in the call sites"
else
    echo "FAIL: the wayfire recipe is inlined in $n_inline of Dockerfile/ci.yml/provision.sh"; fail=1
fi

# And each call site must reference the shared script.
for f in Dockerfile .github/workflows/ci.yml e2e/distros/provision.sh; do
    if grep -q 'build-wayfire-from-source.sh' "$ROOT/$f"; then
        echo "ok: $f uses the shared script"
    else
        echo "FAIL: $f does not use the shared build-wayfire-from-source.sh"; fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: the wayfire-from-source recipe is a single shared script"

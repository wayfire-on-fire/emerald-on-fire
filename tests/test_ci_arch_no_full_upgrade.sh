#!/usr/bin/env bash
# test_ci_arch_no_full_upgrade.sh — the ci.yml `arch` distro-compile job must use
# a DETERMINISTIC build environment. Two parts:
#   1. The archlinux image must be pinned to a date tag (not `:latest`), so the
#      base system does not drift between runs (Arch is rolling; a bare :latest
#      base + a full upgrade would let an Arch base release change the build
#      environment for a reason unrelated to the plugin).
#   2. The pacman install must use `-Syu` (a full, consistent upgrade), NOT a
#      bare `-Sy` (refresh db + install named packages, no full upgrade). Arch
#      has no safe partial upgrades: a bare -Sy can install packages built
#      against a newer base while the image's base stays old, breaking the
#      build with incompatible libraries (ArchWiki advises against -Sy without
#      a full upgrade).
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
CI="$ROOT/.github/workflows/ci.yml"
[ -f "$CI" ] || { echo "FAIL: ci.yml not found"; exit 1; }

fail=0

# Extract the arch job block (from `distro: arch` to the next `distro:`).
arch_block="$(sed -n '/distro: arch/,/distro:/p' "$CI")"
[ -n "$arch_block" ] || { echo "FAIL: no arch job block in ci.yml"; exit 1; }

# 1. The archlinux image must be pinned to a date tag, not `:latest`.
arch_image="$(printf '%s\n' "$arch_block" | grep -E 'image: archlinux' | head -1)"
if [ -z "$arch_image" ]; then
    echo "FAIL: no archlinux image line for the arch job"; fail=1
elif printf '%s' "$arch_image" | grep -qE 'archlinux:latest'; then
    echo "FAIL: the arch job uses archlinux:latest (non-deterministic base)"; fail=1
elif printf '%s' "$arch_image" | grep -qE 'archlinux:[A-Za-z0-9._-]+-[0-9]{8}'; then
    echo "ok: the arch image is pinned to a date tag"
else
    echo "FAIL: the arch image is not pinned to a date tag: $arch_image"; fail=1
fi

# 2. The pacman install must be a full, consistent upgrade (-Syu), not a bare
#    -Sy (unsafe partial upgrade on a rolling distro).
arch_pacman="$(printf '%s\n' "$arch_block" | grep -E 'pacman')"
if [ -z "$arch_pacman" ]; then
    echo "FAIL: no pacman install line for the arch job"; fail=1
elif printf '%s\n' "$arch_pacman" | grep -qE 'pacman -Sy[[:space:]]'; then
    echo "FAIL: the arch job uses a bare pacman -Sy (unsafe partial upgrade on Arch)"; fail=1
elif printf '%s\n' "$arch_pacman" | grep -qE 'pacman -Syu'; then
    echo "ok: the arch job uses a consistent full upgrade (pacman -Syu)"
else
    echo "FAIL: the arch job pacman line is not -Syu: $arch_pacman"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: the arch CI job uses a pinned image + a consistent pacman -Syu"

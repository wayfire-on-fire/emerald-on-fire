#!/usr/bin/env bash
# test_makefile_single_build.sh — the Makefile's CI (Docker) path must build the
# plugin ONCE, not twice. Pre-fix `make binary` did a release build and
# `make test` did a SEPARATE debug build (two full `meson setup` + `ninja`
# compiles of the same source in two Docker runs). The fix uses a single
# release build that serves both the artifact and the test suite.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
MAKEFILE="$ROOT/Makefile"
[ -f "$MAKEFILE" ] || { echo "FAIL: Makefile not found"; exit 1; }

fail=0

# Count the Docker `meson setup build-docker` build-tree configurations. There
# must be EXACTLY ONE (a single shared build), not one per target (binary +
# test) — a second one means the plugin is compiled twice in CI.
n_setup="$(grep -cE 'meson setup build-docker' "$MAKEFILE")"
if [ "$n_setup" -eq 1 ]; then
    echo "ok: a single Docker build (one meson setup)"
else
    echo "FAIL: the Docker path has $n_setup meson setups (expected 1; the plugin is built more than once)"; fail=1
fi

# The `test` target must NOT run its own separate compile — it must reuse the
# single shared build (delegate to it, not do its own meson setup + ninja).
test_body="$(sed -n '/^test:/,/^[^[:space:]]/p' "$MAKEFILE" | sed '$d')"
if printf '%s\n' "$test_body" | grep -qE 'meson setup'; then
    echo "FAIL: the test target runs its own separate build (double build)"; fail=1
else
    echo "ok: the test target reuses the single shared build"
fi

# Symmetrically, `binary` must not do a second, separate build either.
bin_body="$(sed -n '/^binary:/,/^[^[:space:]]/p' "$MAKEFILE" | sed '$d')"
if printf '%s\n' "$bin_body" | grep -qE 'meson setup'; then
    echo "FAIL: the binary target runs its own separate build (double build)"; fail=1
else
    echo "ok: the binary target reuses the single shared build"
fi

# CI must invoke the targets in a SINGLE make call (make binary test), not
# separate `make binary` + `make test`: build-docker is .PHONY, so two
# separate make invocations would run the build twice.
CI="$ROOT/.github/workflows/ci.yml"
if [ -f "$CI" ]; then
    if grep -qE 'run:[[:space:]]*make binary test' "$CI"; then
        echo "ok: CI invokes the targets in a single make call"
    else
        echo "FAIL: CI does not run make binary test in one call (the .PHONY build would run twice)"; fail=1
    fi
    if grep -qE 'run:[[:space:]]*make binary[[:space:]]*$' "$CI" \
        || grep -qE 'run:[[:space:]]*make test[[:space:]]*$' "$CI"; then
        echo "FAIL: CI runs make binary / make test as separate invocations"; fail=1
    else
        echo "ok: CI does not run make binary / make test separately"
    fi
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: the Makefile CI builds the plugin once"

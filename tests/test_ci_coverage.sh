#!/usr/bin/env bash
# test_ci_coverage.sh — CI must exercise the paths the other jobs skip:
#   1. install.sh (the real installer) must RUN in CI. The docker/native
#      jobs build the plugin but never run the install script, so a broken
#      install.sh (the atomic .so placement, the metadata/theme seeding, the
#      versioned-engine glob) would go undetected.
#   2. The emeraldwf-engine-pane test (the part-07 SEGV regression, finding
#      46) needs a display; it self-skips in headless CI. CI must run it under
#      Xvfb so the SEGV regression is actually tested, not silently skipped.
# (e2e/run.sh is intentionally NOT in CI: it needs /dev/kvm + a real display —
#  that is the local QEMU harness in e2e/distros/.)
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
CI="$ROOT/.github/workflows/ci.yml"
[ -f "$CI" ] || { echo "FAIL: ci.yml not found"; exit 1; }

fail=0

# 1. install.sh must be run in CI.
if grep -qE 'install\.sh' "$CI"; then
    echo "ok: CI runs install.sh"
else
    echo "FAIL: CI never runs install.sh"; fail=1
fi

# 2. The engine-pane test must run under a display (Xvfb) in CI, and the step
#    must REQUIRE a PASSED result (the test self-skips without a display, so a
#    bare `meson test` success is insufficient — a self-skip must fail the job).
if grep -qE 'xvfb-run' "$CI" && grep -qE 'emeraldwf-engine-pane' "$CI"; then
    echo "ok: CI runs the engine-pane test under Xvfb"
else
    echo "FAIL: CI does not run the engine-pane test under a display (Xvfb)"; fail=1
fi
if grep -qE 'PASSED' "$CI"; then
    echo "ok: the engine-pane step requires a PASSED result (not a self-skip)"
else
    echo "FAIL: the engine-pane step does not require a PASSED result (a self-skip would pass)"; fail=1
fi

# 3. The distro jobs must run the test suite (not just compile). gtest is
#    installed in both distro images, so the source-hygiene tests can run.
distro_block="$(sed -n '/^  distro-compile:/,/^  [a-z_]*:/p' "$CI")"
if printf '%s\n' "$distro_block" | grep -qE 'meson test'; then
    echo "ok: the distro jobs run the test suite"
else
    echo "FAIL: the distro jobs are compile-only (no meson test)"; fail=1
fi

# 4. The engine count check must count VERSIONED files (libX.so.M.N.P), not
#    every dir entry (the install leaves .so / .so.0 symlinks per engine).
eng_count_line="$(grep -F 'emeraldwf/engines' "$CI" | grep -F 'grep -c' || true)"
if printf '%s\n' "$eng_count_line" | grep -qF 'so\.[0-9]'; then
    echo "ok: the engine count check counts versioned .so files"
else
    echo "FAIL: the engine count check must count versioned .so files (libX.so.M.N.P), not all dir entries"; fail=1
fi

# 5. The distro images are minimal: the suite needs runtime tools they do
#    not ship (awk, git, ImageMagick `convert`, a font) — install steps must carry them.
arch_block="$(printf '%s\n' "$distro_block" | sed -n '/distro: arch/,/distro: opensuse/p')"
osu_block="$(printf '%s\n' "$distro_block" | sed -n '/distro: opensuse/,$p')"
for pkg in imagemagick; do
    if printf '%s\n' "$arch_block" | grep -q "$pkg"; then
        echo "ok: arch image installs $pkg (test runtime dep)"
    else
        echo "FAIL: arch image must install $pkg"; fail=1
    fi
done
for pkg in gawk git ImageMagick dejavu-fonts; do
    if printf '%s\n' "$osu_block" | grep -q "$pkg"; then
        echo "ok: opensuse image installs $pkg (test runtime dep)"
    else
        echo "FAIL: opensuse image must install $pkg"; fail=1
    fi
done

# 6. The Xvfb step must be diagnosable on failure AND must not go red on a
#    GREEN test:
#    (a) guard xvfb-run so a crash prints the log (a bare call dies
#        silently under set -e);
#    (b) the self-skip check must be an if statement — as the step's final
#        line, `grep -q SKIPPED && {...}` evaluates to 1 when the test
#        PASSED (no SKIPPED marker), and a script's exit status is its last
#        command's status: the 2026-11-16/17 runs were red with NO output
#        while the test itself passed.
xvfb_step="$(sed -n '/Test engine pane under a display/,/^      - name:/p' "$CI")"
if printf '%s\n' "$xvfb_step" | grep -qF 'if ! xvfb-run'; then
    echo "ok: the Xvfb step guards xvfb-run (log on crash)"
else
    echo "FAIL: the Xvfb step must guard xvfb-run (if ! ...) and cat the log on failure"; fail=1
fi
if printf '%s\n' "$xvfb_step" | grep -qF 'if grep -q'; then
    echo "ok: the self-skip check is an if statement, so a passing step cannot exit non-zero"
else
    echo "FAIL: the self-skip check must be an if statement — a trailing 'grep && {...}' list makes a passing step exit 1"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: CI exercises install.sh, the engine-pane (display) test, and the distro test suite"

#!/usr/bin/env bash
# test_install_precheck.sh — install.sh must verify the REQUIRED install
# directories (WCM metadata, plugin dir, engines dir — the ones with no
# user-dir fallback) BEFORE building. Pre-fix a fresh system without sudo
# built first (up to ~10 min in docker) and only then FATAL'd at the
# install step. Now: interactive runs ask once (offer to re-run under
# sudo); --non-interactive (or non-tty stdin) fails fast.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

# ── Static: the pre-check runs BEFORE the build ─────────────────────────────
pre_line="$(grep -n '^precheck_install_dirs$' "$SRC" | head -1 | cut -d: -f1)"
build_line="$(grep -n '^# ── 1\. build' "$SRC" | head -1 | cut -d: -f1)"
if [ -n "$pre_line" ] && [ -n "$build_line" ] && [ "$pre_line" -lt "$build_line" ]; then
    echo "ok: precheck runs before the build (line $pre_line < $build_line)"
else
    echo "FAIL: precheck must run before the build section (pre=$pre_line build=$build_line)"; fail=1
fi

# ── Static: the interactive sudo offer + the non-interactive fast-fail ──────
if grep -qF 'Re-run with sudo now?' "$SRC"; then
    echo "ok: interactive prompt offers sudo"
else
    echo "FAIL: no interactive sudo prompt"; fail=1
fi
if grep -qF 'exec sudo "$SCRIPT_PATH" "${SCRIPT_ARGS[@]}"' "$SRC"; then
    echo "ok: sudo re-exec preserves the script's argv"
else
    echo "FAIL: sudo re-exec does not pass the script's argv"; fail=1
fi
if grep -qF -- '--non-interactive)' "$SRC"; then
    echo "ok: --non-interactive flag is parsed"
else
    echo "FAIL: --non-interactive flag not parsed"; fail=1
fi

# ── Behavioral: the absolute script path survives a relative invocation ─────
# A relative $0 (./sub/install.sh) must not break the sudo re-exec: the
# script resolves SCRIPT_PATH to an absolute path BEFORE its cd.
block="$(awk '/^SCRIPT_DIR=\$\(cd/{f=1} f{print} f&&/^cd "\$SCRIPT_DIR"$/{exit}' "$SRC")"
if [ -z "$block" ]; then
    echo "FAIL: SCRIPT_DIR/SCRIPT_PATH resolution block not found"; fail=1
else
    d2="$(mktemp -d)"
    mkdir -p "$d2/parent/sub"
    got="$( cd "$d2/parent" && bash -c "$block; printf '%s' \"\$SCRIPT_PATH\"" ./sub/install.sh )"
    want="$d2/parent/sub/install.sh"
    if [ "$got" = "$want" ]; then
        echo "ok: relative invocation resolves to absolute SCRIPT_PATH"
    else
        echo "FAIL: relative invocation resolved to '$got' (want $want)"; fail=1
    fi
    rm -rf "$d2"
fi

# ── Static: root never gets the sudo offer (no re-exec loop) ────────────────
precheck_body="$(awk '/^precheck_install_dirs\(\) \{/{f=1} f{print} f&&/^\}$/{exit}' "$SRC")"
if grep -qF '[ "$EUID" -eq 0 ]' <<<"$precheck_body"; then
    echo "ok: root fails fast instead of offering sudo"
else
    echo "FAIL: no EUID=0 guard in the precheck (root sudo re-exec loop)"; fail=1
fi
if grep -qF 'exec sudo "$SCRIPT_PATH"' <<<"$precheck_body"; then
    echo "ok: sudo re-exec uses the absolute SCRIPT_PATH"
else
    echo "FAIL: sudo re-exec does not use \$SCRIPT_PATH"; fail=1
fi

# ── Behavioral: extract the precheck function and drive it ──────────────────
func="$(awk '/^precheck_install_dirs\(\) \{/{f=1} f{print} f&&/^\}$/{exit}' "$SRC")"
if [ -z "$func" ]; then
    echo "FAIL: precheck_install_dirs() function not found"
    exit 1
fi

# Writable case: LIBDIR/DATADIR under a temp dir -> precheck passes.
d="$(mktemp -d)"
trap 'rm -rf "$d"' EXIT
(
    eval "$func"
    LIBDIR="$d/lib" DATADIR="$d/share" NONINTERACTIVE=0 SCRIPT_ARGS=() \
        precheck_install_dirs
)
rc=$?
if [ "$rc" -eq 0 ]; then
    echo "ok: precheck passes when all required dirs are writable"
else
    echo "FAIL: precheck rejected writable dirs (rc=$rc)"; fail=1
fi

# Non-writable case. The paths must be non-writable DETERMINISTICALLY, on
# any system, for ANY user (the CI docker job runs the suite as root, and
# some CI runners let the build user write into /usr/share — a /usr-based
# path once made the metadata dir writable on an ubuntu runner, so the
# "listed" assertion there failed). procfs is the answer: /proc accepts no
# mkdir from ANYONE (root included), so every path below is non-writable
# everywhere, and the cases run under root too.
NOWR_BASE="/proc/emeraldwf-precheck-$$"
NOWR_LIB="$NOWR_BASE/lib"
NOWR_DATA="$NOWR_BASE/share"
# SCRIPT_PATH is set by install.sh itself in production; bind it here so
# the FATAL echoes under test print cleanly instead of dying on set -u.
FAKE_SCRIPT="$d/fake-install.sh"

# Interactive mode, non-tty stdin: no prompt possible -> fail, listing
# the missing dirs.
out="$(
    eval "$func"
    LIBDIR="$NOWR_LIB" DATADIR="$NOWR_DATA" NONINTERACTIVE=0 SCRIPT_ARGS=() \
        SCRIPT_PATH="$FAKE_SCRIPT" \
        precheck_install_dirs </dev/null 2>&1
)"
rc=$?
if [ "$rc" -ne 0 ] && grep -qF "$NOWR_LIB/wayfire" <<<"$out" \
    && grep -qF "$NOWR_DATA/wayfire/metadata" <<<"$out" \
    && grep -qF "$NOWR_LIB/emeraldwf/engines" <<<"$out" \
    && ! grep -qF 'unbound variable' <<<"$out"; then
    echo "ok: non-writable dirs fail fast and are listed (rc=$rc)"
else
    echo "FAIL: non-writable dirs not rejected (rc=$rc): $out"; fail=1
fi

# --non-interactive: fail fast, NO prompt.
out="$(
    eval "$func"
    LIBDIR="$NOWR_LIB" DATADIR="$NOWR_DATA" NONINTERACTIVE=1 SCRIPT_ARGS=--docker \
        SCRIPT_PATH="$FAKE_SCRIPT" \
        precheck_install_dirs </dev/null 2>&1
)"
rc=$?
if [ "$rc" -ne 0 ] && grep -qF 'FATAL' <<<"$out" \
    && grep -qF "$FAKE_SCRIPT" <<<"$out" \
    && ! grep -qF 'Re-run with sudo now?' <<<"$out"; then
    echo "ok: --non-interactive fails fast without prompting"
else
    echo "FAIL: --non-interactive should fail without a prompt (rc=$rc): $out"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh pre-checks required install dirs before building"

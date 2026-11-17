#!/usr/bin/env bash
# test_install_docker.sh — install.sh's --docker install path:
#   1. the --docker and --force flags are parsed;
#   2. check_host_compatibility() is behaviorally correct (the functions are
#      extracted from install.sh and run against stubbed wayfire/pkg-config/
#      ldd on PATH):
#        - wayfire version match                        -> ok
#        - version mismatch                             -> refuse
#        - mismatch + --force                           -> ok
#        - host version unknown                         -> refuse
#        - unknown + --force                            -> ok
#        - ABI identifier match (version strings differ)-> ok
#        - ABI identifier mismatch                      -> refuse
#        - missing runtime lib (ldd "not found")        -> refuse, even with --force
#        - malformed manifest (required field missing)  -> refuse
#   3. stage_artifacts() stages the UNIFIED layout from both the meson build
#      tree and the dist/ layout (meson .p object dirs are never staged; the
#      full 7-engine set is required; the docker build's .emeraldwf-build-info
#      manifest is carried along).
#   4. FULL --docker integration: the real install.sh runs end-to-end against
#      a scratch repo copy with stubbed make/docker/wayfire/pkg-config/ldd and
#      a fake sysroot. Proves that incompatibility (ABI mismatch, missing
#      runtime lib, failed docker build, broken dist/) modifies NOTHING on the
#      "host", and that a compatible install lands the fresh artifacts.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
SRC="$ROOT/install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

# The manifest a docker build records (what the guard reads).
MAN="$T/manifest"
printf 'wayfire_version=0.11.0\nabi_identifier=20260726\nbuilt_at=2026-11-17T00:00:00Z\n' > "$MAN"

expect() {  # name expected actual
    if [ "$2" = "$3" ]; then
        echo "ok: $1"
    else
        echo "FAIL: $1 (expected exit $2, got $3)"; fail=1
    fi
}

# ── 1. flags ────────────────────────────────────────────────────────────────
if grep -qF -- '--docker' "$SRC" && grep -qF -- '--force' "$SRC"; then
    echo "ok: install.sh accepts --docker and --force"
else
    echo "FAIL: install.sh must accept --docker and --force"; fail=1
fi

# ── 2. guard matrix ─────────────────────────────────────────────────────────
# run_guard NAME HOST_VER HOST_ABI_MACRO FORCE LDD_MODE [MAN_KIND]
#   -> prints the guard's exit status. Stubs wayfire / pkg-config / ldd on
#      PATH, evals the functions extracted from install.sh, and runs
#      check_host_compatibility on a staged tree. MAN_KIND: ok | noabi.
run_guard() {
    local name="$1" hv="$2" ha="$3" force="$4" ldd="$5" mankind="${6:-ok}"
    local d="$T/g-$name"
    mkdir -p "$d/bin" "$d/inc/wayfire" "$d/stage/wayfire"
    # wayfire stub (the running binary is the version source of truth)
    if [ "$hv" = "none" ]; then
        printf '#!/bin/sh\nexit 1\n' > "$d/bin/wayfire"
    else
        printf '#!/bin/sh\nprintf "%%s (stub)\\n" %s\n' "$hv" > "$d/bin/wayfire"
    fi
    chmod +x "$d/bin/wayfire"
    # pkg-config stub: --modversion -> host version (if any);
    # --variable=includedir -> $d/inc (the header exists only when ha != none)
    {
        printf '#!/bin/sh\n'
        if [ "$hv" = "none" ] && [ "$ha" = "none" ]; then
            printf 'exit 1\n'
        else
            printf 'case "$1" in\n'
            printf '  --modversion) if [ -n "%s" ]; then echo "%s"; exit 0; fi; exit 1;;\n' \
                "${hv/none/}" "${hv/none/}"
            printf '  --variable=includedir) if [ -n "%s" ]; then echo %s; exit 0; fi; exit 1;;\n' \
                "${ha/none/}" "$d/inc"
            printf '  *) exit 1;;\n'
            printf 'esac\n'
        fi
    } > "$d/bin/pkg-config"
    chmod +x "$d/bin/pkg-config"
    # the host's plugin.hpp with the ABI macro (only when ha != none)
    if [ "$ha" != "none" ]; then
        printf '#define WAYFIRE_API_ABI_VERSION_MACRO %s\n' "$ha" > "$d/inc/wayfire/plugin.hpp"
    fi
    # ldd stub
    case "$ldd" in
        ok)      printf '#!/bin/sh\necho "libc.so.6 => /lib/libc.so.6 (0x0000)"\n' > "$d/bin/ldd" ;;
        missing) printf '#!/bin/sh\necho "libfake.so.1 => not found"\n' > "$d/bin/ldd" ;;
    esac
    chmod +x "$d/bin/ldd"
    : > "$d/stage/wayfire/libemeraldwf.so"
    if [ "$mankind" = "noabi" ]; then
        printf 'wayfire_version=0.11.0\nbuilt_at=x\n' > "$d/manifest"
    else
        cp "$MAN" "$d/manifest"
    fi
    cat > "$d/run.sh" <<EOF
#!/bin/bash
set -u
PATH="$d/bin:\$PATH"
eval "\$(sed -n '/^is_versioned_so()/,/^}/p' '$SRC')"
eval "\$(sed -n '/^host_wayfire_version()/,/^}/p' '$SRC')"
eval "\$(sed -n '/^host_abi_identifier()/,/^}/p' '$SRC')"
eval "\$(sed -n '/^check_host_compatibility()/,/^}/p' '$SRC')"
FORCE=$force
check_host_compatibility "$d/stage" "$d/manifest"
EOF
    bash "$d/run.sh" > "$d/out.txt" 2>&1
    local status=$?
    if [ "${VERBOSE:-}" = "1" ] && [ "$status" -ne 0 ]; then
        cat "$d/out.txt"
    fi
    echo "$status"
}

expect "version match passes"                        0 "$(run_guard match    0.11.0 "2026'07'26" 0 ok)"      check_host_compatibility
expect "version mismatch refuses"                    1 "$(run_guard mismatch 0.10.0 none         0 ok)"      check_host_compatibility
expect "mismatch + --force passes"                   0 "$(run_guard force-mm 0.10.0 none         1 ok)"      check_host_compatibility
expect "unknown host version refuses"               1 "$(run_guard unknown  none   none         0 ok)"      check_host_compatibility
expect "unknown + --force passes"                    0 "$(run_guard force-uk none   none         1 ok)"      check_host_compatibility
expect "ABI match overrides version skew"           0 "$(run_guard abi-ok   0.11.1 "2026'07'26" 0 ok)"      check_host_compatibility
expect "ABI mismatch refuses"                        1 "$(run_guard abi-bad  0.11.0 "1999'01'01" 0 ok)"      check_host_compatibility
expect "missing runtime lib refuses even with --force" \
                                                   1 "$(run_guard no-lib   0.11.0 "2026'07'26" 1 missing)"  check_host_compatibility
expect "malformed manifest (no abi) refuses"        1 "$(run_guard no-abif  0.11.0 "2026'07'26" 0 ok noabi)" check_host_compatibility

# ── 3. staging ──────────────────────────────────────────────────────────────
STAGE_FN="$(sed -n '/^stage_artifacts()/,/^}/p' "$SRC")"
if [ -z "$STAGE_FN" ]; then
    echo "FAIL: no stage_artifacts() in install.sh"; fail=1
else
    eval "$STAGE_FN"
    eval "$(sed -n '/^is_versioned_so()/,/^}/p' "$SRC")"

    # (a) from a meson build tree (the full 7-engine set)
    b="$T/fakebuild"
    mkdir -p "$b/src" "$b/engines" "$b/libengine" "$b/themer"
    printf 'wf' > "$b/src/libemeraldwf.so"
    for e in legacy line oxygen pixmap vrunner zootreeves truglass; do
        printf 'e' > "$b/engines/lib$e.so.0.1.0"
        ln -s "lib$e.so.0.1.0" "$b/engines/lib$e.so.0"
        ln -s "lib$e.so.0" "$b/engines/lib$e.so"
        mkdir "$b/engines/lib$e.so.0.1.0.p"   # meson object dir — must NOT be staged
    done
    printf 'eng' > "$b/libengine/libemeraldengine.so.0.1.0"
    ln -s libemeraldengine.so.0.1.0 "$b/libengine/libemeraldengine.so.0"
    ln -s libemeraldengine.so.0 "$b/libengine/libemeraldengine.so"
    printf 'them' > "$b/themer/emerald-theme-manager"
    sb="$T/stage-build"
    stage_artifacts build "$b" "$sb"
    st=0
    for f in wayfire/libemeraldwf.so emeraldwf/engines/libline.so.0.1.0 \
             emeraldwf/engines/libline.so.0 emeraldwf/engines/libline.so \
             emeraldwf/libemeraldengine.so.0.1.0 emerald-theme-manager; do
        [ -e "$sb/$f" ] || { echo "FAIL: stage (build) missing $f"; fail=1; st=1; }
    done
    if [ "$(find "$sb/emeraldwf/engines" -maxdepth 1 -type f -name 'lib*.so.*' | wc -l)" -ne 7 ]; then
        echo "FAIL: stage (build) must stage all 7 engines"; fail=1; st=1
    fi
    if [ -e "$sb/emeraldwf/engines/libline.so.0.1.0.p" ] || [ -e "$sb/emeraldwf/libemeraldengine.so.0.1.0.p" ]; then
        echo "FAIL: stage (build) must not copy meson .p object dirs"; fail=1; st=1
    fi
    [ "$st" -eq 0 ] && echo "ok: stage_artifacts (build) lays out the unified artifact set"

    # (a2) a partial engine set must be rejected (a broken build must not
    #      silently install fewer engines)
    b2="$T/fakebuild-partial"
    cp -a "$b" "$b2"
    rm "$b2/engines/libline.so.0.1.0" "$b2/engines/libline.so.0" "$b2/engines/libline.so"
    sb2="$T/stage-partial"
    if stage_artifacts build "$b2" "$sb2"; then
        echo "FAIL: stage (build) must reject a partial engine set"; fail=1
    else
        echo "ok: stage_artifacts (build) rejects a partial engine set"
    fi

    # (b) from dist/ (the docker layout) — the build-info manifest is carried
    dd="$T/fakedist"
    mkdir -p "$dd/wayfire" "$dd/emeraldwf/engines"
    printf 'wf' > "$dd/wayfire/libemeraldwf.so"
    for e in legacy line oxygen pixmap vrunner zootreeves truglass; do
        printf 'e' > "$dd/emeraldwf/engines/lib$e.so.0.1.0"
        ln -s "lib$e.so.0.1.0" "$dd/emeraldwf/engines/lib$e.so.0"
    done
    printf 'eng' > "$dd/emeraldwf/libemeraldengine.so.0.1.0"
    printf 'them' > "$dd/emerald-theme-manager"
    cp "$MAN" "$dd/.emeraldwf-build-info"
    sd="$T/stage-dist"
    stage_artifacts dist "$dd" "$sd"
    st=0
    for f in wayfire/libemeraldwf.so emeraldwf/engines/libline.so.0.1.0 \
             emeraldwf/libemeraldengine.so.0.1.0 emerald-theme-manager \
             .emeraldwf-build-info; do
        [ -e "$sd/$f" ] || { echo "FAIL: stage (dist) missing $f"; fail=1; st=1; }
    done
    [ "$st" -eq 0 ] && echo "ok: stage_artifacts (dist) carries the build manifest"
fi

# ── 4. Makefile contract ────────────────────────────────────────────────────
# The docker build records the build manifest AND validates the image's
# actual wayfire version against the recipe's pinned version.
if grep -qF '.emeraldwf-build-info' "$ROOT/Makefile" \
   && grep -qF 'pkg-config --modversion wayfire' "$ROOT/Makefile"; then
    echo "ok: make build-docker records (and validates) dist/.emeraldwf-build-info"
else
    echo "FAIL: make build-docker must write dist/.emeraldwf-build-info and validate the image version"; fail=1
fi

# ── 5. full --docker integration ────────────────────────────────────────────
# The REAL install.sh runs end-to-end against a scratch repo copy, with
# make/docker/wayfire/pkg-config/ldd stubbed on PATH and pkg-config pointing
# the install destinations into a fake sysroot. A refusal must leave the
# sysroot untouched; a success must land the FRESH artifacts.
INT_DIR=""
setup_int() {  # name host_ver host_abi_macro ldd_mode
    local name="$1" hv="$2" ha="$3" ldd="$4"
    local e="$T/int-$name"
    mkdir -p "$e/bin" "$e/inc/wayfire" "$e/sys/lib" "$e/sys/share" "$e/home"
    # make stub — the "docker build": writes a fake dist/ into CWD (the repo
    # copy); FAKE_MAKE_FAIL / FAKE_MAKE_NO_THEMER simulate build failures.
    cat > "$e/bin/make" <<'MK'
#!/bin/sh
[ "$1" = build-docker ] || exit 0
if [ "${FAKE_MAKE_FAIL:-0}" = 1 ]; then echo "docker build failed" >&2; exit 1; fi
mkdir -p dist/wayfire dist/emeraldwf/engines
echo FRESH > dist/wayfire/libemeraldwf.so
i=1
while [ $i -le 7 ]; do
    echo FRESH > "dist/emeraldwf/engines/libeng$i.so.0.1.0"
    i=$((i+1))
done
echo FRESH > dist/emeraldwf/libemeraldengine.so.0.1.0
if [ "${FAKE_MAKE_NO_THEMER:-0}" != 1 ]; then echo FRESH > dist/emerald-theme-manager; fi
printf 'wayfire_version=0.11.0\nabi_identifier=20260726\nbuilt_at=x\n' > dist/.emeraldwf-build-info
MK
    chmod +x "$e/bin/make"
    : > "$e/bin/docker"; chmod +x "$e/bin/docker"
    # wayfire stub
    if [ "$hv" = "none" ]; then
        printf '#!/bin/sh\nexit 1\n' > "$e/bin/wayfire"
    else
        printf '#!/bin/sh\nprintf "%%s (stub)\\n" %s\n' "$hv" > "$e/bin/wayfire"
    fi
    chmod +x "$e/bin/wayfire"
    # pkg-config stub -> the fake sysroot
    {
        printf '#!/bin/sh\n'
        if [ "$hv" = "none" ] && [ "$ha" = "none" ]; then
            printf 'exit 1\n'
        else
            printf 'case "$1" in\n'
            printf '  --modversion) if [ -n "%s" ]; then echo "%s"; exit 0; fi; exit 1;;\n' \
                "${hv/none/}" "${hv/none/}"
            printf '  --variable=prefix) echo %s/sys;;\n' "$e"
            printf '  --variable=libdir) echo lib;;\n'
            printf '  --variable=datadir) echo share;;\n'
            printf '  --variable=icondir) echo share/wayfire/icons;;\n'
            printf '  --variable=includedir) if [ -n "%s" ]; then echo %s/inc; exit 0; fi; exit 1;;\n' \
                "${ha/none/}" "$e"
            printf '  *) exit 1;;\n'
            printf 'esac\n'
        fi
    } > "$e/bin/pkg-config"
    chmod +x "$e/bin/pkg-config"
    if [ "$ha" != "none" ]; then
        printf '#define WAYFIRE_API_ABI_VERSION_MACRO %s\n' "$ha" > "$e/inc/wayfire/plugin.hpp"
    fi
    # ldd stub
    case "$ldd" in
        ok)      printf '#!/bin/sh\necho "libc.so.6 => /lib/libc.so.6 (0x0000)"\n' > "$e/bin/ldd" ;;
        missing) printf '#!/bin/sh\necho "libfake.so.1 => not found"\n' > "$e/bin/ldd" ;;
    esac
    chmod +x "$e/bin/ldd"
    # scratch repo copy (fast: skip the build tree, VCS, and artifacts)
    mkdir -p "$e/repo"
    tar -C "$ROOT" --exclude=.git --exclude=build --exclude=build-docker \
        --exclude=dist --exclude=share -cf - . | tar -C "$e/repo" -xf -
    INT_DIR="$e"
}

run_int() {  # extra_args make_fail no_themer -> prints install.sh's exit status
    local args=()
    [ -n "$1" ] && args=("$1")
    env -i PATH="$INT_DIR/bin:/usr/bin:/bin" HOME="$INT_DIR/home" \
        FAKE_MAKE_FAIL="${2:-0}" FAKE_MAKE_NO_THEMER="${3:-0}" \
        bash "$INT_DIR/repo/install.sh" --docker --skip-tests --no-seed "${args[@]}" \
        > "$INT_DIR/out.txt" 2>&1
    local status=$?
    if [ "${VERBOSE:-}" = "1" ]; then
        cat "$INT_DIR/out.txt"
    fi
    echo "$status"
}

dest_count() { find "$INT_DIR/sys" -type f 2>/dev/null | wc -l; }

# (a) compatible host: the install succeeds and lands the FRESH artifacts
setup_int ok 0.11.0 "2026'07'26" ok
expect "integration: compatible host installs" 0 "$(run_int "")"
if [ "$(cat "$INT_DIR/sys/lib/wayfire/libemeraldwf.so" 2>/dev/null)" = "FRESH" ] \
   && [ "$(find "$INT_DIR/sys/lib/emeraldwf/engines" -name 'lib*.so.*' -type f | wc -l)" = 7 ] \
   && [ "$(cat "$INT_DIR/sys/lib/emeraldwf/libemeraldengine.so.0.1.0" 2>/dev/null)" = "FRESH" ]; then
    echo "ok: integration: fresh plugin + 7 engines + engine lib installed"
else
    echo "FAIL: integration: fresh artifacts not installed correctly"; fail=1
fi

# (b) ABI mismatch: refused, the "host" is untouched
setup_int abibad 0.11.0 "1999'01'01" ok
expect "integration: ABI mismatch refuses" 1 "$(run_int "")"
[ "$(dest_count)" = 0 ] || { echo "FAIL: integration: ABI mismatch modified the host"; fail=1; }

# (c) missing runtime lib + --force: refused, the "host" is untouched
setup_int nolib 0.11.0 "2026'07'26" missing
expect "integration: missing lib refuses even with --force" 1 "$(run_int --force)"
[ "$(dest_count)" = 0 ] || { echo "FAIL: integration: missing lib modified the host"; fail=1; }

# (d) docker build fails: the installer aborts, the "host" is untouched
setup_int buildfail 0.11.0 "2026'07'26" ok
expect "integration: docker build failure aborts" 1 "$(run_int "" 1)"
[ "$(dest_count)" = 0 ] || { echo "FAIL: integration: failed build modified the host"; fail=1; }

# (e) broken dist/ (themer missing): staging is fatal, the "host" is untouched
setup_int broken 0.11.0 "2026'07'26" ok
expect "integration: broken dist/ aborts staging" 1 "$(run_int "" 0 1)"
[ "$(dest_count)" = 0 ] || { echo "FAIL: integration: broken dist/ modified the host"; fail=1; }

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh's --docker path (flags, compatibility guard, unified staging, integration)"

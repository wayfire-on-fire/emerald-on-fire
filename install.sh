#!/usr/bin/env bash
#
# emeraldwf installer — build, install, and seed user data so a fresh
# machine has a working emerald theme with no hand-editing afterwards.
#
# Usage:
#   ./install.sh [--skip-tests] [--no-seed] [--docker] [--force]
#
#   --docker  build in the pinned Docker toolchain (make build-docker:
#             wayfire v0.11.0 from source) instead of a native meson build —
#             for hosts that run wayfire but have no build toolchain. Tests
#             run inside the container. Before installing, the host is
#             checked for compatibility with the image's wayfire (plugin ABI
#             identifier, falling back to the version string; plus the
#             plugin's runtime libraries). The check REFUSES by default on
#             mismatch or an undeterminable host version.
#   --force   override the --docker compatibility policy ONLY (never a
#             missing runtime library, which cannot load at all).
#
# Installs (all placement is this script's; meson's install rules exist
# for WPM — `wayfire-plugin install` — and are never run from here):
#   WCM metadata (emeraldwf.xml, the option panel incl. titlebar actions)
#                              → <datadir>/wayfire/metadata
#   libemeraldwf.so            → <libdir>/wayfire
#   libemeraldengine.so        → <libdir>/emeraldwf (shared settings lib;
#                                 user-writable fallback: engines/)
#   7 engine .so files         → <libdir>/emeraldwf/engines
#   (the .so files are placed by this script, ATOMICALLY — temp file +
#    rename — so installing over a running wayfire never touches its live
#    mmap of those files)
#   WCM plugin icon            → <datadir>/wayfire/icons (system; if that
#                                dir is not writable the icon is installed
#                                to ~/.local/share/wayfire/icons — WCM
#                                probes the user dir FIRST)
#   default theme (engine-override mismatch path) →
#                              <libdir>/wayfire/emeraldwf-defaults/theme
#                              (fallback ~/.local/share/emeraldwf/defaults)
#
# Installs (directly, with user-dir fallbacks — /usr/bin & /usr/share
# need root; the themer scans the user dirs too):
#   emerald-theme-manager      → /usr/bin (fallback ~/.local/bin)
#   emerald-theme-manager.desktop → <datadir>/applications
#                                (fallback ~/.local/share/applications)
#   default theme              → <datadir>/emeraldwf/themes/default
#                                (fallback ~/.local/share/emeraldwf/themes)
#   app icon (gem)             → hicolor apps icondir
#                                (fallback ~/.local/share/icons/hicolor)
#
# Seeds (never overwrites existing files):
#   ~/.emerald/settings.ini    — emerald core settings from emerald's
#                                defaults
#   ~/.emerald/theme/          — emerald theme (theme.ini + PNGs) from
#                                emerald's defaults
#
# Part 08 migration (one-time, idempotent): the global emerald settings
# that used to live in ~/.emerald/settings.ini (decoration cropping,
# no-shade-on-scroll, button fade/pulse + durations) are appended to the
# active wayfire config's [emeraldwf] section when not already defined
# there — WCM is their editor now.
#
# Root is needed only where dirs under /usr are root-owned (typically
# <libdir>/emeraldwf — the installer then falls back to the user-writable
# engines/ dir for libemeraldengine.so). ~/.emerald and the user icon
# dir are always written as the current user.

set -euo pipefail
cd "$(dirname "$0")"

SKIP_TESTS=0
SEED=1
USE_DOCKER=0
FORCE=0
for arg in "$@"; do
    case "$arg" in
        --skip-tests) SKIP_TESTS=1 ;;
        --no-seed)    SEED=0 ;;
        --docker)     USE_DOCKER=1 ;;
        --force)      FORCE=1 ;;
        -h|--help)    grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)            echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# ── 0. user home for user-state placement ────────────────────────────────────
# BEGIN M22-user-home
# M22 (issue #1): under `sudo`, HOME is /root — but the user's emerald
# state (the ~/.emerald seed, the settings.ini -> wayfire.ini migration,
# the XDG / .local/bin fallbacks) belongs to the INVOKING user, not to
# root. This script's FATAL messages push users to `sudo $0`, so resolve
# SUDO_USER's home when running as root. Non-root: no change. Root without
# sudo (a real root session): keeps its own home. SUDO_USER set but
# UNresolvable: fail visibly — silently targeting /root would recreate
# the very bug this resolves (ChatGPT follow-up).
resolve_user_home() {  # euid sudo_user home -> prints the resolved home
    local euid="$1" sudo_user="$2" home="$3" resolved
    if [ "$euid" -eq 0 ] && [ -n "$sudo_user" ]; then
        resolved=$(getent passwd "$sudo_user" | cut -d: -f6)
        if [ -z "$resolved" ]; then
            printf 'FATAL: cannot resolve home for sudo user %s\n' \
                "$sudo_user" >&2
            return 1
        fi
        printf '%s\n' "$resolved"
        return 0
    fi
    printf '%s\n' "$home"
}
# END M22-user-home
USER_HOME="$(resolve_user_home "$EUID" "${SUDO_USER:-}" "$HOME")" || exit 1

BUILD=build
DEFAULTS=third_party/emerald/defaults

# The install section (both backends) normalizes relative pkg-config
# libdir/datadir values against the wayfire prefix.
WF_PREFIX=$(pkg-config --variable=prefix wayfire 2>/dev/null)
[ -n "$WF_PREFIX" ] || WF_PREFIX=/usr

# ── 1. build ────────────────────────────────────────────────────────────────
if [ "$USE_DOCKER" -eq 1 ]; then
    # Docker backend: the pinned toolchain image builds wayfire from source,
    # compiles the plugin against it, runs the test suite in the container,
    # and packages the artifacts into dist/ (with the .emeraldwf-build-info
    # manifest the compatibility guard below reads). Nothing is installed
    # by the container — all placement stays with this script.
    command -v docker >/dev/null 2>&1 || {
        echo "FATAL: --docker requires docker" >&2; exit 1; }
    command -v make >/dev/null 2>&1 || {
        echo "FATAL: --docker requires make" >&2; exit 1; }
    echo "==> building in Docker (pinned toolchain; tests run in the container)"
    make build-docker || exit 1
    [ -f dist/.emeraldwf-build-info ] || {
        echo "FATAL: make build-docker produced no dist/.emeraldwf-build-info" >&2
        exit 1; }
else
# The build must target the installed wayfire's prefix/libdir, not meson's
# defaults (/usr/local, lib): libemeraldengine bakes ENGINE_DIR
# (<prefix>/<libdir>/emeraldwf/engines) in at compile time, and both the
# test suite and the installed theme editor scan that directory for the
# engines — the ones this script places in wayfire's libdir. A bare
# `meson setup` would point them at an empty /usr/local/lib*/emeraldwf/engines.
WF_LIBDIR_REL=$(pkg-config --variable=libdir wayfire 2>/dev/null)
[ -n "$WF_LIBDIR_REL" ] || WF_LIBDIR_REL=lib
case "$WF_LIBDIR_REL" in /*) WF_LIBDIR_REL=${WF_LIBDIR_REL#"$WF_PREFIX"/} ;; esac

# A marker recording the prefix/libdir the build tree was configured with.
# libemeraldengine bakes ENGINE_DIR (<prefix>/<libdir>/emeraldwf/engines) in
# at compile time, so a tree configured with a DIFFERENT wayfire prefix/libdir
# is stale (the engine would scan the wrong dir). Reconfigure when the marker
# no longer matches, not just when build.ninja is missing.
CFG_MARKER="$BUILD/.emeraldwf-cfg"
if [ ! -f "$BUILD/build.ninja" ]; then
    echo "==> configuring build tree (meson)"
    # Write the marker only AFTER a successful configure, and exit on failure
    # (never build against a tree that failed to (re)configure).
    meson setup "$BUILD" -Dprefix="$WF_PREFIX" -Dlibdir="$WF_LIBDIR_REL" || exit 1
    printf '%s' "$WF_PREFIX|$WF_LIBDIR_REL" > "$CFG_MARKER"
elif [ ! -f "$CFG_MARKER" ] || [ "$(cat "$CFG_MARKER" 2>/dev/null)" != "$WF_PREFIX|$WF_LIBDIR_REL" ]; then
    echo "==> reconfiguring build tree (wayfire prefix/libdir changed)"
    meson setup --reconfigure "$BUILD" -Dprefix="$WF_PREFIX" -Dlibdir="$WF_LIBDIR_REL" || exit 1
    printf '%s' "$WF_PREFIX|$WF_LIBDIR_REL" > "$CFG_MARKER"
fi
echo "==> building"
ninja -C "$BUILD"
fi

# ── 2. tests ────────────────────────────────────────────────────────────────
if [ "$USE_DOCKER" -eq 1 ]; then
    echo "==> tests already ran inside the container (make build-docker)"
elif [ "$SKIP_TESTS" -eq 0 ]; then
    echo "==> running tests"
    meson test -C "$BUILD"
fi

# ── 2b. stage artifacts + compatibility guard ──────────────────────────────
# Both backends stage the SAME layout into a FRESH temp dir (never reuse a
# stale dist/ after a failed build, and never mix backend artifacts):
#   <stage>/wayfire/libemeraldwf.so
#   <stage>/emeraldwf/libemeraldengine.so*   <stage>/emeraldwf/engines/lib*
#   <stage>/emerald-theme-manager
#   <stage>/.emeraldwf-build-info            (docker builds only)
# The placement code below reads from $STAGE, so it is backend-agnostic.
STAGE_DIR="$(mktemp -d)"

is_versioned_so() {
    local p="$1"
    [ -f "$p" ] && [ ! -L "$p" ] || return 1
    local base="${p##*/}"
    [[ "$base" =~ ^lib.*\.so\.[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

stage_artifacts() {  # kind root stage-dir — lay out the unified artifact set
    local kind="$1" root="$2" stage="$3" f
    mkdir -p "$stage/wayfire" "$stage/emeraldwf/engines"
    case "$kind" in
        build)
            cp "$root/src/libemeraldwf.so" "$stage/wayfire/"
            cp "$root/themer/emerald-theme-manager" "$stage/"
            # versioned .so + their symlinks; never the meson .p object dirs
            for f in "$root"/libengine/libemeraldengine.so*; do
                [ -e "$f" ] || continue
                if [ -L "$f" ] || is_versioned_so "$f"; then
                    cp -a "$f" "$stage/emeraldwf/"
                fi
            done
            for f in "$root"/engines/lib*.so*; do
                [ -e "$f" ] || continue
                if [ -L "$f" ] || is_versioned_so "$f"; then
                    cp -a "$f" "$stage/emeraldwf/engines/"
                fi
            done
            ;;
        dist)
            cp -a "$root/wayfire/libemeraldwf.so" "$stage/wayfire/"
            cp -a "$root/emerald-theme-manager" "$stage/"
            cp -a "$root"/emeraldwf/libemeraldengine.so* "$stage/emeraldwf/"
            cp -a "$root"/emeraldwf/engines/lib*.so* "$stage/emeraldwf/engines/"
            if [ -f "$root/.emeraldwf-build-info" ]; then
                cp "$root/.emeraldwf-build-info" "$stage/"
            fi
            ;;
    esac
    # Validate the staged set before it is trusted (a partial/broken build
    # must fail here, not silently install fewer artifacts):
    # (a) every staged symlink must resolve inside the stage;
    # (b) the full engine set must be present (7 engines).
    local dangling
    dangling="$(find "$stage" -type l ! -exec test -e {} \; -print)"
    if [ -n "$dangling" ]; then
        echo "FATAL: staged symlinks do not resolve: $dangling" >&2
        return 1
    fi
    local n
    n="$(find "$stage/emeraldwf/engines" -maxdepth 1 -type f -name 'lib*.so.*' | wc -l)"
    if [ "$n" -ne 7 ]; then
        echo "FATAL: staged engine count is $n (expected 7)" >&2
        return 1
    fi
}

# host_wayfire_version — the host's wayfire version (e.g. 0.11.0), or fail.
# The running binary is the source of truth; the pkg-config module is the
# fallback (a host with dev files but no wayfire on PATH).
host_wayfire_version() {
    local v
    v="$(wayfire --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
    if [ -n "$v" ]; then
        printf '%s\n' "$v"
        return 0
    fi
    v="$(pkg-config --modversion wayfire 2>/dev/null || true)"
    if [ -n "$v" ]; then
        printf '%s\n' "$v"
        return 0
    fi
    return 1
}

# host_abi_identifier — wayfire's plugin ABI macro from the host's dev header
# (<includedir>/wayfire/plugin.hpp: WAYFIRE_API_ABI_VERSION_MACRO, the value
# wayfire's loader compares a plugin's getWayfireVersion() against), or fail
# when the header is absent. Normalized to bare digits (2026'07'26 -> 20260726).
host_abi_identifier() {
    local inc hdr
    inc="$(pkg-config --variable=includedir wayfire 2>/dev/null || true)"
    [ -n "$inc" ] || return 1
    hdr="$inc/wayfire/plugin.hpp"
    [ -f "$hdr" ] || return 1
    grep -oE 'WAYFIRE_API_ABI_VERSION_MACRO[[:space:]].*' "$hdr" \
        | head -1 | sed 's/[^0-9]//g'
}

# check_host_compatibility STAGE MANIFEST — docker builds only: the artifacts
# were built against the IMAGE's wayfire, not the host's. Refuse (exit 1)
# when the host is not demonstrably compatible. --force (FORCE=1) overrides
# the version/ABI policy ONLY — never a missing runtime library (which
# cannot load at all).
check_host_compatibility() {
    local stage="$1" manifest="$2"
    local img_ver img_abi host_ver host_abi

    # Runtime deps first (not overridable): ldd reports "not found" for any
    # library the host lacks. Check EVERY staged ELF (plugin, engine lib,
    # engines, theme manager) — not just the plugin.
    local elf
    for elf in "$stage/wayfire/libemeraldwf.so" \
               "$stage"/emeraldwf/libemeraldengine.so.* \
               "$stage"/emeraldwf/engines/lib*.so.* \
               "$stage/emerald-theme-manager"; do
        [ -f "$elf" ] || continue
        if [ -L "$elf" ]; then
            continue   # resolves to the versioned file checked above
        fi
        if ldd "$elf" 2>/dev/null | grep -q 'not found'; then
            echo "FATAL: the docker-built $elf needs libraries this host lacks:" >&2
            ldd "$elf" 2>/dev/null | grep 'not found' >&2
            exit 1
        fi
    done

    if [ "${FORCE:-0}" -eq 1 ]; then
        echo "==> --force: skipping the wayfire compatibility policy"
        return 0
    fi

    img_ver="$(sed -n 's/^wayfire_version=//p' "$manifest" | head -1)"
    img_abi="$(sed -n 's/^abi_identifier=//p' "$manifest" | head -1)"
    # Strict: both fields are required. A malformed manifest must fail, not
    # be treated as "no constraint".
    if [ -z "$img_ver" ] || [ -z "$img_abi" ]; then
        echo "FATAL: the build manifest is missing required fields" >&2
        echo "       (wayfire_version and abi_identifier are both required):" >&2
        cat "$manifest" >&2
        exit 1
    fi
    host_ver="$(host_wayfire_version || true)"
    host_abi="$(host_abi_identifier || true)"

    # Primary: wayfire's plugin ABI identifier. Secondary: the version string
    # (a host without dev files only exposes its version).
    if [ -n "$img_abi" ] && [ -n "$host_abi" ]; then
        if [ "$img_abi" = "$host_abi" ]; then
            echo "==> wayfire ABI identifier matches ($img_abi)"
            return 0
        fi
        echo "FATAL: wayfire plugin ABI mismatch: build=$img_abi host=$host_abi" >&2
        echo "       re-run with --force to install anyway" >&2
        exit 1
    fi
    if [ -z "$host_ver" ]; then
        echo "FATAL: cannot determine the host's wayfire version" >&2
        echo "       (no 'wayfire' binary on PATH and no wayfire pkg-config module)" >&2
        echo "       re-run with --force to install anyway" >&2
        exit 1
    fi
    if [ "$host_ver" = "$img_ver" ]; then
        echo "==> wayfire version matches ($host_ver)"
        return 0
    fi
    echo "FATAL: wayfire version mismatch: build=$img_ver host=$host_ver" >&2
    echo "       re-run with --force to install anyway" >&2
    exit 1
}

if [ "$USE_DOCKER" -eq 1 ]; then
    stage_artifacts dist dist "$STAGE_DIR"
    check_host_compatibility "$STAGE_DIR" "$STAGE_DIR/.emeraldwf-build-info"
else
    stage_artifacts build "$BUILD" "$STAGE_DIR"
fi
STAGE="$STAGE_DIR"

# ── 3. install (system dirs) ────────────────────────────────────────────────
# This script does ALL placement itself (meson's install rules exist for
# WPM — wayfire-plugin install — and are never run from here). The .so
# files are placed ATOMICALLY (copy to a temp file in the target dir +
# rename over the target): a running wayfire has these files MMAP'D — an
# in-place copy (truncate + write, what `cp` and meson's installer do)
# unmapped live code pages and SIGBUS'd the session mid-install
# (2026-10-09). A rename swaps the directory entry; the old inode stays
# valid for the running process until it is closed. The WCM metadata xml
# is a text file read at WCM startup — a plain copy is fine.
LIBDIR=$(pkg-config --variable=libdir wayfire 2>/dev/null)
[ -n "$LIBDIR" ] || LIBDIR=/usr/lib
# Normalize to absolute: a wayfire .pc may carry a RELATIVE libdir/datadir
# (e.g. "lib64", "share"); a .pc libdir is relative to the prefix. A bare
# relative value would make the install land in ./lib64 (CWD-relative).
case "$LIBDIR" in /*) ;; *) LIBDIR="$WF_PREFIX/$LIBDIR" ;; esac
DATADIR=$(pkg-config --variable=datadir wayfire 2>/dev/null)
[ -n "$DATADIR" ] || DATADIR=/usr/share
case "$DATADIR" in /*) ;; *) DATADIR="$WF_PREFIX/$DATADIR" ;; esac

atomic_install() {  # srcfile destdir
    local src="$1" dir="$2" tmp
    tmp=$(mktemp "$dir/.install.XXXXXX")
    # Register the exact temp path so cleanup_on_exit removes ONLY this
    # invocation's temps (a dir-wide .install.* glob could delete a
    # CONCURRENT installer's temps).
    INSTALL_TMP_FILES+=("$tmp")
    cp "$src" "$tmp"
    # mktemp creates 0600 and cp into an EXISTING file never changes the
    # destination's mode — without this, every installed .so lands as
    # 0600 (unreadable for other users under a sudo install).
    chmod --reference="$src" "$tmp"
    mv -f "$tmp" "$dir/${src##*/}"
}

atomic_link() {  # target destdir linkname
    local tgt="$1" dir="$2" name="$3" tmp
    tmp=$(mktemp -u "$dir/.link.XXXXXX")
    # Register the exact temp path (see atomic_install).
    INSTALL_TMP_FILES+=("$tmp")
    ln -s "$tgt" "$tmp"
    mv -f "$tmp" "$dir/$name"
}

# A versioned .so plus its .so.<major> / .so symlinks (the build dir also
# contains .so.<ver>.p object dirs — copy the exact versioned file). The
# symlink names are DERIVED from the filename (strip .patch, then .minor),
# not hardcoded, so a version bump does not silently break the install.
atomic_versioned_so() {  # srcfile destdir
    local src="$1" dir="$2"
    local real="${src##*/}"            # libfoo.so.0.1.0
    local sover="${real%.*}"           # libfoo.so.0.1   (strip .patch)
    sover="${sover%.*}"                # libfoo.so.0     (strip .minor)
    local unver="${real%%.so.*}.so"    # libfoo.so
    atomic_install "$src" "$dir"
    atomic_link "$real" "$dir" "$sover"
    atomic_link "$sover" "$dir" "$unver"
}

# True if $1 is a versioned .so: a REGULAR file (not a symlink) whose basename
# is libNAME.so.MAJOR.MINOR.PATCH (three numeric components). This excludes the
# .so / .so.MAJOR symlinks, the .so.VER.p object dir, and unrelated regular
# files (libfoo.so.debug, libfoo.so.2.3.4.old, ...) that lib*.so.* would
# otherwise match.
# Refresh a directory's contents ATOMICALLY: populate a temp dir first, then
# swap it into place with a rename. A running wayfire plugin reads the default
# theme (the engine-override mismatch path); the old `rm -rf <dest> && cp -r`
# left a window where the theme was missing/incomplete. Populating the temp
# dir BEFORE the rm makes the swap a single fast rename (the window is one
# syscall, not the whole copy).
# ── temp-file cleanup (one EXIT trap, set early, not clobbered) ─────────────
# atomic_install / atomic_link create .install.XXXXXX / .link.XXXXXX temps in
# the target (system) dir and rename them into place; an interrupted script
# leaves them behind in a system dir. Each helper registers its EXACT temp path
# in INSTALL_TMP_FILES right after mktemp, so cleanup_on_exit removes only THIS
# invocation's temps (never a concurrent installer's, and never the installed
# files). It also removes an in-flight default-theme temp dir (tracked in
# DEFAULT_THEME_TMP). The trap is set ONCE, after the target dirs are known;
# refresh_default_theme tracks its temp in DEFAULT_THEME_TMP (no separate trap,
# so this trap is not clobbered by the theme refresh).
INSTALL_TMP_FILES=()
DEFAULT_THEME_TMP=""
cleanup_on_exit() {
    local f
    for f in "${INSTALL_TMP_FILES[@]}"; do
        [ -n "$f" ] && rm -f -- "$f" 2>/dev/null
    done
    if [ -n "$DEFAULT_THEME_TMP" ] && [ -d "$DEFAULT_THEME_TMP" ]; then
        rm -rf -- "$DEFAULT_THEME_TMP"
    fi
    if [ -n "${STAGE_DIR:-}" ] && [ -d "$STAGE_DIR" ]; then
        rm -rf -- "$STAGE_DIR"
    fi
}

refresh_default_theme() {  # destdir
    local dest="$1" parent tmp backup=""
    parent="$(dirname "$dest")"
    mkdir -p "$parent"
    tmp="$(mktemp -d "$parent/.emeraldwf-defaults.XXXXXX")" || return 1
    DEFAULT_THEME_TMP="$tmp"
    cp -r "$DEFAULTS/theme" "$tmp/theme"
    # Backup-and-swap: rename the old dir out (fast), rename the new one in
    # (fast), THEN remove the old (slow) - so the window of a missing theme is
    # two renames, not a slow rm -rf of the old theme.
    if [ -e "$dest" ]; then
        backup="$parent/.emeraldwf-defaults.old.$$"
        if ! mv "$dest" "$backup"; then
            rm -rf "$tmp"; DEFAULT_THEME_TMP=""; return 1
        fi
    fi
    if ! mv "$tmp" "$dest"; then
        # Roll back: restore the old theme, drop the temp.
        [ -n "$backup" ] && mv "$backup" "$dest"
        rm -rf "$tmp"; DEFAULT_THEME_TMP=""; return 1
    fi
    DEFAULT_THEME_TMP=""
    # An if (not `[ -n ] && rm`): on a CLEAN install there is no backup, and a
    # trailing `&&` list would make the function return 1 — install.sh runs
    # with set -e, so the script would die AFTER the install succeeded.
    if [ -n "$backup" ]; then
        rm -rf "$backup"
    fi
}

echo "==> installing (plugin, engines, metadata, icon)"
# [ -w ] is false for a missing path, so create the target dirs first.
mkdir -p "$DATADIR/wayfire/metadata" 2>/dev/null || true
if [ -w "$DATADIR/wayfire/metadata" ]; then
    cp metadata/emeraldwf.xml "$DATADIR/wayfire/metadata/"
    echo "==> emeraldwf.xml → $DATADIR/wayfire/metadata"
else
    echo "FATAL: $DATADIR/wayfire/metadata not writable. Re-run with:" >&2
    echo "    sudo $0 $*" >&2
    exit 1
fi

# runtime .so files — atomic placement (see note above); target dirs first
mkdir -p "$LIBDIR/wayfire" "$LIBDIR/emeraldwf" "$LIBDIR/emeraldwf/engines" 2>/dev/null || true
if [ ! -w "$LIBDIR/wayfire" ] || [ ! -w "$LIBDIR/emeraldwf/engines" ]; then
    echo "FATAL: $LIBDIR/wayfire and $LIBDIR/emeraldwf/engines must be" >&2
    echo "writable. Re-run with:  sudo $0 $*" >&2
    exit 1
fi

# Clean up any .install.* / .link.* temps an interrupted install leaves in the
# target (system) dirs, on exit (normal or interrupted). The helpers register
# their exact temp paths in INSTALL_TMP_FILES; the trap is set here, once,
# before any atomic placement runs.
trap cleanup_on_exit EXIT

atomic_install "$STAGE/wayfire/libemeraldwf.so" "$LIBDIR/wayfire"
echo "==> libemeraldwf.so → $LIBDIR/wayfire"

# Install every versioned engine .so (a REGULAR file with a version suffix —
# skips the .so / .so.<major> symlinks and the .so.<ver>.p object dirs). The
# glob is version-agnostic so a version bump does not silently install zero
# engines.
n_engines=0
for f in "$STAGE"/emeraldwf/engines/lib*.so.*; do
    is_versioned_so "$f" || continue
    atomic_versioned_so "$f" "$LIBDIR/emeraldwf/engines"
    n_engines=$((n_engines + 1))
done
echo "==> $n_engines engines → $LIBDIR/emeraldwf/engines"

# The engine library (libemeraldengine.so) is a versioned .so too; find the
# exact versioned file (a regular file, not a symlink) rather than hardcoding
# the version.
eng_lib=""
eng_count=0
for f in "$STAGE"/emeraldwf/libemeraldengine.so.*; do
    is_versioned_so "$f" || continue
    eng_lib="$f"
    eng_count=$((eng_count + 1))
done
# Exactly one: stale build artifacts (two versioned files) would otherwise be
# an ambiguous selection.
if [ "$eng_count" -ne 1 ]; then
    echo "FATAL: expected exactly one versioned libemeraldengine.so in the staged artifacts (found $eng_count)" >&2
    exit 1
fi
if [ -w "$LIBDIR/emeraldwf" ]; then
    atomic_versioned_so "$eng_lib" "$LIBDIR/emeraldwf"
    echo "==> libemeraldengine.so → $LIBDIR/emeraldwf"
else
    atomic_versioned_so "$eng_lib" "$LIBDIR/emeraldwf/engines"
    echo "==> libemeraldengine.so → $LIBDIR/emeraldwf/engines (user-writable fallback)"
fi

# ── 3b. default theme next to the installed plugin ─────────────────────────
# Engine-override mismatch path (part 07): a mismatched engine runs on
# emerald's default theme, which the installed plugin resolves at
# <so-dir>/emeraldwf-defaults/theme (so-dir = <libdir>/wayfire). Fallback:
# <XDG>/emeraldwf/defaults/theme (the plugin probes both).
# M28 (issue #1): derive from $LIBDIR (the SAME value the plugin was
# just installed into, line above) - the old inline pkg-config fallback
# was /usr/lib/wayfire (already containing 'wayfire'), so on a pkg-config
# failure WF_PLUGDIR became /usr/lib/wayfire/wayfire: the placement check
# looked in the wrong dir and silently took the ~/.local fallback even
# though the plugin WAS at /usr/lib/wayfire.
WF_PLUGDIR="$LIBDIR/wayfire"
if [ -d "$WF_PLUGDIR" ] && [ -w "$WF_PLUGDIR" ]; then
    refresh_default_theme "$WF_PLUGDIR/emeraldwf-defaults"
    echo "==> default theme → $WF_PLUGDIR/emeraldwf-defaults/theme"
else
    refresh_default_theme "${XDG_DATA_HOME:-$USER_HOME/.local/share}/emeraldwf/defaults"
    echo "==> $WF_PLUGDIR not writable — default theme → ~/.local/share/emeraldwf/defaults/theme"
fi

# ── 4. WCM icon: system icondir, with user-dir fallback ─────────────────────
# WCM's Plugin::init_widget() probes, in order:
#   $XDG_DATA_HOME/wayfire/icons/plugin-emeraldwf.svg  (≈ ~/.local/share/...)
#   <wayfire icondir>/plugin-emeraldwf.svg             (≈ /usr/share/wayfire/icons)
ICON_SRC=metadata/plugin-emeraldwf.svg
# resolve the icondir from the same wayfire pkg-config variable the build
# used (meson's install_dir: wayfire.get_variable(pkgconfig: 'icondir')).
ICONDIR=$(pkg-config --variable=icondir wayfire 2>/dev/null || echo /usr/share/wayfire/icons)

install_icon() {
    local dir="$1"
    mkdir -p "$dir"
    cp "$ICON_SRC" "$dir/plugin-emeraldwf.svg"
    echo "==> WCM icon → $dir/plugin-emeraldwf.svg"
}

if [ -f "$ICONDIR/plugin-emeraldwf.svg" ] && [ -r "$ICONDIR/plugin-emeraldwf.svg" ]; then
    : # installed by ninja install
elif install -d "$ICONDIR" 2>/dev/null && [ -w "$ICONDIR" ]; then
    install_icon "$ICONDIR"
else
    echo "==> system icon dir not writable ($ICONDIR) — using user dir"
    install_icon "${XDG_DATA_HOME:-$USER_HOME/.local/share}/wayfire/icons"
fi

# ── 4b. emerald-theme-manager (theme editor): system dirs, user-dir fallback ─
# The themer itself scans:
#   $(datadir)/emeraldwf/themes/          (dist tier, read-only list)
#   $XDG_DATA_HOME/emeraldwf/themes/      (user "system" tier)
#   ~/.emerald/themes/                    (user themes, editable)
# so the user-dir fallbacks below are first-class, not degraded.
XDG="${XDG_DATA_HOME:-$USER_HOME/.local/share}"

install_file() {  # src dest-dir — temp + rename: overwriting a binary
                 # that is CURRENTLY RUNNING fails with ETXTBSY (the
                 # theme manager often is); a rename swaps the directory
                 # entry and the running process keeps its old inode.
    local src="$1" dir="$2" tmp
    mkdir -p "$dir"
    tmp=$(mktemp "$dir/.install.XXXXXX")
    cp "$src" "$tmp"
    # mktemp creates 0600 and cp into an EXISTING file never changes the
    # destination's mode — without this the installed binary lands as
    # 0600: not executable even for its owner (this is how
    # emerald-theme-manager ended up unrunnable in 2026-10-14).
    chmod --reference="$src" "$tmp"
    mv -f "$tmp" "$dir/${src##*/}"
}

# binary (remove the previous name — part 07 was shipped as
# "emerald-on-fire" for one release; do not leave a stale duplicate)
rm -f "$USER_HOME/.local/bin/emerald-on-fire" "$XDG/applications/emerald-on-fire.desktop"
if [ -w /usr/bin ]; then
    install_file "$STAGE/emerald-theme-manager" /usr/bin
    BIN_DEST=/usr/bin/emerald-theme-manager
    rm -f /usr/bin/emerald-on-fire /usr/share/applications/emerald-on-fire.desktop
    echo "==> emerald-theme-manager → /usr/bin"
else
    install_file "$STAGE/emerald-theme-manager" "$USER_HOME/.local/bin"
    BIN_DEST="$USER_HOME/.local/bin/emerald-theme-manager"
    echo "==> /usr/bin not writable — emerald-theme-manager → ~/.local/bin"
fi

# desktop entry (Exec rewritten to the ABSOLUTE binary path: panel
# launchers don't add ~/.local/bin to PATH, so a bare
# "emerald-theme-manager" finds nothing when the binary lives in the user
# dir)
DESKTOP_SRC=themer/emerald-theme-manager.desktop
if [ -w /usr/share/applications ]; then
    install_file "$DESKTOP_SRC" /usr/share/applications
    DESKTOP_DEST=/usr/share/applications/emerald-theme-manager.desktop
    echo "==> emerald-theme-manager.desktop → /usr/share/applications"
else
    install_file "$DESKTOP_SRC" "$XDG/applications"
    DESKTOP_DEST="$XDG/applications/emerald-theme-manager.desktop"
    echo "==> /usr/share/applications not writable — desktop → ~/.local/share/applications"
fi
sed -i "s|^Exec=.*|Exec=$BIN_DEST|" "$DESKTOP_DEST"

# default theme (system tier) - refreshed atomically, like the plugin's
# default theme above (the themer reads this tier; a running themer would see
# a missing theme during a non-atomic rm -rf + cp -r).
if install -d /usr/share/emeraldwf/themes 2>/dev/null; then
    refresh_default_theme /usr/share/emeraldwf/themes/default
    echo "==> default theme → /usr/share/emeraldwf/themes/default"
else
    refresh_default_theme "$XDG/emeraldwf/themes/default"
    echo "==> /usr/share not writable — default theme → ~/.local/share/emeraldwf/themes/default"
fi

# app icon (the gem; WCM icon is the same artwork under a different name)
ICON_SRC=metadata/plugin-emeraldwf.svg
for d in /usr/share/icons/hicolor/scalable/apps "$XDG/icons/hicolor/scalable/apps"; do
    if mkdir -p "$d" 2>/dev/null && [ -w "$d" ]; then
        cp "$ICON_SRC" "$d/emerald-theme-manager.svg"
        rm -f "$d/emerald-on-fire.svg"
        echo "==> app icon → $d/emerald-theme-manager.svg"
        break
    fi
done

# ── 5. seed ~/.emerald (never overwrite) ────────────────────────────────────
if [ "$SEED" -eq 1 ]; then
    EMERALD_DIR="$USER_HOME/.emerald"
    if [ ! -f "$EMERALD_DIR/settings.ini" ]; then
        mkdir -p "$EMERALD_DIR"
        cp "$DEFAULTS/settings.ini" "$EMERALD_DIR/settings.ini"
        echo "==> seeded $EMERALD_DIR/settings.ini (emerald defaults)"
    else
        echo "==> $EMERALD_DIR/settings.ini already present — left untouched"
    fi
    if [ ! -d "$EMERALD_DIR/theme" ]; then
        mkdir -p "$EMERALD_DIR"
        cp -r "$DEFAULTS/theme" "$EMERALD_DIR/theme"
        echo "==> seeded $EMERALD_DIR/theme (emerald default theme)"
    else
        echo "==> $EMERALD_DIR/theme already present — left untouched"
    fi
    # M22 (issue #1, ChatGPT follow-up): under sudo the seed files are
    # root-owned — the invoking user could not edit their theme/settings.
    # Hand the dir back to them (a plain run already creates everything
    # as the user, so this is a no-op there).
    if [ "$EUID" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
        chown -R "$SUDO_USER": "$EMERALD_DIR"
    fi
fi

# ── 5b. part 08: migrate the global emerald settings to the wayfire config ─
# These keys used to be read from ~/.emerald/settings.ini; WCM (the
# [emeraldwf] section) owns them now. One-time, idempotent: for each key
# present in settings.ini, append it to the active wayfire config if the
# [emeraldwf] section does not define it yet.
if [ "$SEED" -eq 1 ]; then
    ESET="$USER_HOME/.emerald/settings.ini"
    WF_INI=""
    for cand in "$USER_HOME/.config/lxqt/wayland/lxqt-wayfire.ini" \
                "$USER_HOME/.config/wayfire.ini"; do
        if [ -f "$cand" ]; then
            WF_INI="$cand"
            break
        fi
    done
    if [ -f "$ESET" ] && [ -n "$WF_INI" ]; then
        migrated=0
        for key in no_scroll_shade use_decoration_cropping use_button_fade \
                   use_button_fade_pulse button_fade_total_duration \
                   button_fade_step_duration button_fade_pulse_wait_duration \
                   button_fade_pulse_min_opacity; do
            val=$(sed -n "s/^${key}=//p" "$ESET" | head -1)
            [ -z "$val" ] && continue
            # Skip when [emeraldwf] already defines the key (the user's
            # WCM value wins).
            if awk -v k="$key" '
                /^\[emeraldwf\]/ { f = 1; next }
                /^\[/            { f = 0 }
                f && $0 ~ "^" k "=" { found = 1 }
                END { exit !found }' "$WF_INI"; then
                continue
            fi
            if ! grep -q '^\[emeraldwf\]' "$WF_INI"; then
                printf '\n[emeraldwf]\n%s=%s\n' "$key" "$val" >> "$WF_INI"
            else
                # Insert under [emeraldwf] — NOT at the end of the file:
                # keys outside any section belong to glib's null group and
                # are never read (neither by the plugin nor by WCM). The
                # key must print BEFORE the following section header, so
                # it lands in [emeraldwf], not in the next section.
                awk -v k="$key" -v v="$val" '
                    { if (insec && /^\[/) { print k "=" v; insec = 0 }
                      print }
                    /^\[emeraldwf\]/ { insec = 1 }
                    END { if (insec) print k "=" v }
                ' "$WF_INI" > "${WF_INI}.tmp" && mv "${WF_INI}.tmp" "$WF_INI"
            fi
            migrated=$((migrated + 1))
        done
        if [ "$migrated" -gt 0 ]; then
            echo "==> migrated $migrated emerald settings: settings.ini → $WF_INI ([emeraldwf])"
            # M22 (issue #1, ChatGPT follow-up): the awk tmp + mv made the
            # file root-owned under sudo — hand it back so the invoking
            # user can still edit their wayfire config.
            if [ "$EUID" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
                chown "$SUDO_USER": "$WF_INI"
            fi
        fi
    fi
fi

# ── 6. done ─────────────────────────────────────────────────────────────────
echo ""
echo "emeraldwf installed."
echo "  - restart wayfire (log out/in, or Ctrl-Alt-Backspace-equivalent)"
echo "  - configure in WCM (Plugins → emeraldwf: engine, maximized borders/"
echo "    shadows, shade, titlebar double-/middle-click actions, and the"
echo "    button fade/pulse + cropping/decoration-scroll globals)"
echo "  - theme tweaks live-reload from ~/.emerald/theme (no restart)"
echo "  - full theme editing: run 'emerald-theme-manager'"

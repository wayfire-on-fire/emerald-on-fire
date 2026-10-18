#!/usr/bin/env bash
#
# emeraldwf installer — build, install, and seed user data so a fresh
# machine has a working emerald theme with no hand-editing afterwards.
#
# Usage:
#   ./install.sh [--skip-tests] [--no-seed]
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
for arg in "$@"; do
    case "$arg" in
        --skip-tests) SKIP_TESTS=1 ;;
        --no-seed)    SEED=0 ;;
        -h|--help)    grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)            echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

BUILD=build
DEFAULTS=third_party/emerald/defaults

# ── 1. build ────────────────────────────────────────────────────────────────
# The build must target the installed wayfire's prefix/libdir, not meson's
# defaults (/usr/local, lib): libemeraldengine bakes ENGINE_DIR
# (<prefix>/<libdir>/emeraldwf/engines) in at compile time, and both the
# test suite and the installed theme editor scan that directory for the
# engines — the ones this script places in wayfire's libdir. A bare
# `meson setup` would point them at an empty /usr/local/lib*/emeraldwf/engines.
WF_PREFIX=$(pkg-config --variable=prefix wayfire 2>/dev/null)
[ -n "$WF_PREFIX" ] || WF_PREFIX=/usr
WF_LIBDIR_REL=$(pkg-config --variable=libdir wayfire 2>/dev/null)
[ -n "$WF_LIBDIR_REL" ] || WF_LIBDIR_REL=lib
case "$WF_LIBDIR_REL" in /*) WF_LIBDIR_REL=${WF_LIBDIR_REL#"$WF_PREFIX"/} ;; esac

if [ ! -f "$BUILD/build.ninja" ]; then
    echo "==> configuring build tree (meson)"
    meson setup "$BUILD" -Dprefix="$WF_PREFIX" -Dlibdir="$WF_LIBDIR_REL"
fi
echo "==> building"
ninja -C "$BUILD"

# ── 2. tests ────────────────────────────────────────────────────────────────
if [ "$SKIP_TESTS" -eq 0 ]; then
    echo "==> running tests"
    meson test -C "$BUILD"
fi

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
DATADIR=$(pkg-config --variable=datadir wayfire 2>/dev/null)
[ -n "$DATADIR" ] || DATADIR=/usr/share

atomic_install() {  # srcfile destdir
    local src="$1" dir="$2" tmp
    tmp=$(mktemp "$dir/.install.XXXXXX")
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
    ln -s "$tgt" "$tmp"
    mv -f "$tmp" "$dir/$name"
}

# A versioned .so plus its .so.0 / .so symlinks (the build dir also
# contains .so.0.1.0.p object dirs — copy the exact versioned file).
atomic_versioned_so() {  # srcfile destdir
    local src="$1" dir="$2"
    local real="${src##*/}"           # libfoo.so.0.1.0
    local sover="${real%.1.0}"        # libfoo.so.0
    atomic_install "$src" "$dir"
    atomic_link "$real" "$dir" "$sover"
    atomic_link "$sover" "$dir" "${sover%.0}"
}

echo "==> installing (plugin, engines, metadata, icon)"
if [ -d "$DATADIR/wayfire/metadata" ] && [ -w "$DATADIR/wayfire/metadata" ]; then
    cp metadata/emeraldwf.xml "$DATADIR/wayfire/metadata/"
    echo "==> emeraldwf.xml → $DATADIR/wayfire/metadata"
else
    echo "FATAL: $DATADIR/wayfire/metadata not writable. Re-run with:" >&2
    echo "    sudo $0 $*" >&2
    exit 1
fi

# runtime .so files — atomic placement (see note above)
if [ ! -w "$LIBDIR/wayfire" ] || [ ! -w "$LIBDIR/emeraldwf/engines" ]; then
    echo "FATAL: $LIBDIR/wayfire and $LIBDIR/emeraldwf/engines must be" >&2
    echo "writable. Re-run with:  sudo $0 $*" >&2
    exit 1
fi

atomic_install "$BUILD/src/libemeraldwf.so" "$LIBDIR/wayfire"
echo "==> libemeraldwf.so → $LIBDIR/wayfire"

for f in "$BUILD"/engines/lib*.so.0.1.0; do
    atomic_versioned_so "$f" "$LIBDIR/emeraldwf/engines"
done
echo "==> 7 engines → $LIBDIR/emeraldwf/engines"

if [ -w "$LIBDIR/emeraldwf" ]; then
    atomic_versioned_so "$BUILD/libengine/libemeraldengine.so.0.1.0" \
        "$LIBDIR/emeraldwf"
    echo "==> libemeraldengine.so → $LIBDIR/emeraldwf"
else
    atomic_versioned_so "$BUILD/libengine/libemeraldengine.so.0.1.0" \
        "$LIBDIR/emeraldwf/engines"
    echo "==> libemeraldengine.so → $LIBDIR/emeraldwf/engines (user-writable fallback)"
fi

# ── 3b. default theme next to the installed plugin ─────────────────────────
# Engine-override mismatch path (part 07): a mismatched engine runs on
# emerald's default theme, which the installed plugin resolves at
# <so-dir>/emeraldwf-defaults/theme (so-dir = <libdir>/wayfire). Fallback:
# <XDG>/emeraldwf/defaults/theme (the plugin probes both).
WF_PLUGDIR=$(pkg-config --variable=libdir wayfire 2>/dev/null || echo /usr/lib/wayfire)/wayfire
if [ -d "$WF_PLUGDIR" ] && [ -w "$WF_PLUGDIR" ]; then
    rm -rf "$WF_PLUGDIR/emeraldwf-defaults"
    mkdir -p "$WF_PLUGDIR/emeraldwf-defaults"
    cp -r "$DEFAULTS/theme" "$WF_PLUGDIR/emeraldwf-defaults/theme"
    echo "==> default theme → $WF_PLUGDIR/emeraldwf-defaults/theme"
else
    rm -rf "${XDG_DATA_HOME:-$HOME/.local/share}/emeraldwf/defaults"
    mkdir -p "${XDG_DATA_HOME:-$HOME/.local/share}/emeraldwf/defaults"
    cp -r "$DEFAULTS/theme" "${XDG_DATA_HOME:-$HOME/.local/share}/emeraldwf/defaults/theme"
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
    install_icon "${XDG_DATA_HOME:-$HOME/.local/share}/wayfire/icons"
fi

# ── 4b. emerald-theme-manager (theme editor): system dirs, user-dir fallback ─
# The themer itself scans:
#   $(datadir)/emeraldwf/themes/          (dist tier, read-only list)
#   $XDG_DATA_HOME/emeraldwf/themes/      (user "system" tier)
#   ~/.emerald/themes/                    (user themes, editable)
# so the user-dir fallbacks below are first-class, not degraded.
XDG="${XDG_DATA_HOME:-$HOME/.local/share}"

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
rm -f "$HOME/.local/bin/emerald-on-fire" "$XDG/applications/emerald-on-fire.desktop"
if [ -w /usr/bin ]; then
    install_file "$BUILD/themer/emerald-theme-manager" /usr/bin
    BIN_DEST=/usr/bin/emerald-theme-manager
    rm -f /usr/bin/emerald-on-fire /usr/share/applications/emerald-on-fire.desktop
    echo "==> emerald-theme-manager → /usr/bin"
else
    install_file "$BUILD/themer/emerald-theme-manager" "$HOME/.local/bin"
    BIN_DEST="$HOME/.local/bin/emerald-theme-manager"
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

# default theme (system tier)
if install -d /usr/share/emeraldwf/themes 2>/dev/null; then
    rm -rf /usr/share/emeraldwf/themes/default
    cp -r "$DEFAULTS/theme" /usr/share/emeraldwf/themes/default
    echo "==> default theme → /usr/share/emeraldwf/themes/default"
else
    rm -rf "$XDG/emeraldwf/themes/default"
    mkdir -p "$XDG/emeraldwf/themes"
    cp -r "$DEFAULTS/theme" "$XDG/emeraldwf/themes/default"
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
    EMERALD_DIR="$HOME/.emerald"
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
fi

# ── 5b. part 08: migrate the global emerald settings to the wayfire config ─
# These keys used to be read from ~/.emerald/settings.ini; WCM (the
# [emeraldwf] section) owns them now. One-time, idempotent: for each key
# present in settings.ini, append it to the active wayfire config if the
# [emeraldwf] section does not define it yet.
if [ "$SEED" -eq 1 ]; then
    ESET="$HOME/.emerald/settings.ini"
    WF_INI=""
    for cand in "$HOME/.config/lxqt/wayland/lxqt-wayfire.ini" \
                "$HOME/.config/wayfire.ini"; do
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

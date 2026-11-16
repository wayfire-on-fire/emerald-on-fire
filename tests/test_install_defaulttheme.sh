#!/usr/bin/env bash
# test_install_defaulttheme.sh — install.sh's default-theme refresh must be
# ATOMIC. Pre-fix it was `rm -rf <dest> && mkdir -p <dest> && cp -r theme
# <dest>/theme`: between the rm and the cp, a RUNNING plugin that reads the
# default theme (the engine-override mismatch path) saw a missing/incomplete
# theme. The fix populates a temp dir first, then swaps it into place with a
# rename, so the window of a missing theme is a single fast syscall, not the
# whole copy.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

# 1. A refresh helper exists and is atomic (populate a temp dir, then rename).
helper="$(sed -n '/^refresh_default_theme()/,/^}/p' "$SRC")"
if [ -n "$helper" ]; then
    echo "ok: refresh_default_theme helper exists"
else
    echo "FAIL: no refresh_default_theme helper (non-atomic inline refresh)"; fail=1
fi
if printf '%s\n' "$helper" | grep -qE 'mktemp -d' \
   && printf '%s\n' "$helper" | grep -qE 'mv "\$dest" "\$backup"' \
   && printf '%s\n' "$helper" | grep -qE 'mv "\$tmp" "\$dest"'; then
    echo "ok: refresh uses backup-and-swap (temp dir, rename old out, new in)"
else
    echo "FAIL: refresh_default_theme is not a backup-and-swap"; fail=1
fi
# The slow rm -rf must happen AFTER the swap (on the backup), never before
# the new theme is in place.
if printf '%s\n' "$helper" | grep -qE 'rm -rf "\$dest"'; then
    echo "FAIL: refresh still rm -rf's the destination before the swap"; fail=1
else
    echo "ok: no pre-swap rm -rf of the destination"
fi

# 2. The default-theme placement uses the helper.
if grep -qE 'refresh_default_theme "\$WF_PLUGDIR/emeraldwf-defaults"' "$SRC"; then
    echo "ok: default-theme placement uses the helper"
else
    echo "FAIL: default-theme placement does not use the helper"; fail=1
fi

# 3. The old inline non-atomic pattern (rm -rf the target then repopulate) is
#    gone.
if grep -qE 'rm -rf "\$WF_PLUGDIR/emeraldwf-defaults"' "$SRC"; then
    echo "FAIL: inline rm -rf of the default theme is still present"; fail=1
else
    echo "ok: inline rm -rf of the default theme is gone"
fi

# 3b. The themer's system-tier default theme must ALSO be refreshed
#     atomically (a running themer reads that tier too).
if grep -qE 'rm -rf /usr/share/emeraldwf/themes/default' "$SRC"; then
    echo "FAIL: the themer system-tier default theme is still non-atomic"; fail=1
else
    echo "ok: themer system-tier default theme is not a bare rm -rf + cp -r"
fi

# 4. Behavioral: a refresh puts the NEW theme in place and leaves no
#    temp/backup dir behind.
btmp="$(mktemp -d)"
trap 'rm -rf "$btmp"' EXIT
mkdir -p "$btmp/defaults/theme"
printf 'new' > "$btmp/defaults/theme/theme.ini"
mkdir -p "$btmp/dest/emeraldwf-defaults/theme"
printf 'old' > "$btmp/dest/emeraldwf-defaults/theme/theme.ini"
DEFAULTS="$btmp/defaults"
# source the helper + its deps (global, cleanup, refresh) from install.sh
eval "$(sed -n '/^DEFAULT_THEME_TMP=""/p' "$SRC")"
eval "$(sed -n '/^cleanup_default_theme_tmp()/,/^}/p' "$SRC")"
eval "$(sed -n '/^refresh_default_theme()/,/^}/p' "$SRC")"
refresh_default_theme "$btmp/dest/emeraldwf-defaults"
if [ "$(cat "$btmp/dest/emeraldwf-defaults/theme/theme.ini" 2>/dev/null)" = "new" ]; then
    echo "ok: refresh puts the new theme in place"
else
    echo "FAIL: refresh did not put the new theme in place"; fail=1
fi
leftover="$(ls -d "$btmp/dest"/.emeraldwf-defaults.* 2>/dev/null)"
if [ -z "$leftover" ]; then
    echo "ok: no leftover temp/backup dirs"
else
    echo "FAIL: leftover temp/backup dirs: $leftover"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh refreshes the default theme atomically"

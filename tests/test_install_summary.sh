#!/usr/bin/env bash
# test_install_summary.sh — install.sh must end with a status summary that
# reports what was installed WHERE: each artifact's final destination
# (system dir vs user-dir fallback). Pre-fix the script ended with a bare
# "emeraldwf installed." and no per-artifact locations, so a user could not
# tell at a glance where the plugin / engines / theme editor landed.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

# ── The script records each artifact's final destination ───────────────────
for var in DEST_METADATA DEST_PLUGIN DEST_ENGINES DEST_ENGINE_LIB \
           DEST_DEFAULT_THEME_PLUGIN DEST_ICON DEST_APP_ICON \
           DEST_DEFAULT_THEME_THEMER; do
    if grep -qE "^${var}=" "$SRC"; then
        echo "ok: install.sh tracks $var"
    else
        echo "FAIL: install.sh does not track $var"; fail=1
    fi
done

# ── The end-of-script summary prints those destinations ─────────────────────
# Extract the summary block: from the 'echo "emeraldwf installed:"' line to
# the line before 'echo "Next steps:"'.
block="$(awk '/^echo "emeraldwf installed:"$/{f=1} /^echo "Next steps:"$/{exit} f{print}' "$SRC")"
if [ -z "$block" ]; then
    echo "FAIL: end-of-script summary block not found"
    exit 1
fi

# Run the block with known values; it must print each artifact's destination.
out="$(
    DEST_METADATA=/usr/share/wayfire/metadata/emeraldwf.xml \
    DEST_PLUGIN=/usr/lib64/wayfire/libemeraldwf.so \
    DEST_ENGINES=/usr/lib64/emeraldwf/engines \
    DEST_ENGINE_LIB=/usr/lib64/emeraldwf/engines/libemeraldengine.so \
    DEST_DEFAULT_THEME_PLUGIN=/usr/lib64/wayfire/emeraldwf-defaults/theme \
    DEST_ICON=/home/u/.local/share/wayfire/icons/plugin-emeraldwf.svg \
    DEST_APP_ICON=/home/u/.local/share/icons/hicolor/scalable/apps/emerald-theme-manager.svg \
    DEST_DEFAULT_THEME_THEMER=/home/u/.local/share/emeraldwf/themes/default \
    BIN_DEST=/home/u/.local/bin/emerald-theme-manager \
    DESKTOP_DEST=/home/u/.local/share/applications/emerald-theme-manager.desktop \
    n_engines=7 \
    bash -c "$block"
)"

for want in \
    "/usr/lib64/wayfire/libemeraldwf.so" \
    "/usr/lib64/emeraldwf/engines" \
    "/usr/lib64/emeraldwf/engines/libemeraldengine.so" \
    "/home/u/.local/bin/emerald-theme-manager" \
    "/usr/share/wayfire/metadata/emeraldwf.xml"; do
    if grep -qF "$want" <<<"$out"; then
        echo "ok: summary reports $want"
    else
        echo "FAIL: summary does not report $want"; fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh ends with a what-is-installed-where summary"

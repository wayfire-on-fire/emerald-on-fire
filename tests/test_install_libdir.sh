#!/usr/bin/env bash
# test_install_libdir.sh — install.sh must normalize LIBDIR/DATADIR to absolute
# paths. A wayfire .pc file may carry a RELATIVE libdir/datadir (e.g. "lib64",
# "share"); pre-fix those were used as-is, so the install landed in ./lib64
# (CWD-relative) instead of <prefix>/lib64. The fix prefixes a relative value
# with the wayfire prefix (a .pc libdir is relative to the prefix).
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

fail=0

# ── Behavioral: a relative libdir/datadir is made absolute ──────────────────
d="$(mktemp -d)"
trap 'rm -rf "$d"' EXIT
# stub pkg-config that returns RELATIVE libdir/datadir (as a .pc may).
cat > "$d/pkg-config" <<'PC'
#!/usr/bin/env bash
case "$1" in
  --variable=libdir)  echo lib64 ;;
  --variable=datadir) echo share ;;
  *) : ;;
esac
PC
chmod +x "$d/pkg-config"
export PATH="$d:$PATH"
WF_PREFIX=/opt/wayfire

# Extract the LIBDIR + DATADIR normalization block (from the LIBDIR= line to
# the first blank line) and eval it with the stubbed pkg-config + WF_PREFIX.
block="$(awk '/^LIBDIR=/{f=1} f&&/^[[:space:]]*$/{exit} f{print}' "$SRC")"
[ -n "$block" ] || { echo "FAIL: LIBDIR/DATADIR block not found"; exit 1; }
eval "$block"

if [ "$LIBDIR" = "/opt/wayfire/lib64" ]; then
    echo "ok: relative libdir normalized to absolute ($LIBDIR)"
else
    echo "FAIL: relative libdir not normalized (got: '$LIBDIR', want /opt/wayfire/lib64)"; fail=1
fi
if [ "$DATADIR" = "/opt/wayfire/share" ]; then
    echo "ok: relative datadir normalized to absolute ($DATADIR)"
else
    echo "FAIL: relative datadir not normalized (got: '$DATADIR', want /opt/wayfire/share)"; fail=1
fi

# ── An already-absolute libdir is left unchanged ────────────────────────────
cat > "$d/pkg-config" <<'PC'
#!/usr/bin/env bash
case "$1" in
  --variable=libdir)  echo /usr/lib64 ;;
  --variable=datadir) echo /usr/share ;;
  *) : ;;
esac
PC
WF_PREFIX=/opt/wayfire
eval "$block"
if [ "$LIBDIR" = "/usr/lib64" ] && [ "$DATADIR" = "/usr/share" ]; then
    echo "ok: absolute libdir/datadir left unchanged"
else
    echo "FAIL: absolute libdir/datadir was altered (libdir='$LIBDIR' datadir='$DATADIR')"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh normalizes LIBDIR/DATADIR to absolute paths"

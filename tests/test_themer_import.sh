#!/usr/bin/env bash
# test_themer_import.sh — M23 (issue #1): pin the TAR signatures the
# themer's import_theme() validation relies on.
#
# import_theme() (themer/main.c, a non-linkable GTK TU) validates an
# archive by line-parsing `tar -tzf` (member paths) and `tar -tvzf`
# (entry types: first char of each line). The source-structure test
# pins the CODE; this test pins the CONTRACT against real tar output:
#
#   - regular files start with '-', directories with 'd' (allowed);
#   - symlinks start with 'l', hardlinks with 'h', FIFOs with 'p'
#     (rejected - a theme is PNGs + an ini, nothing else);
#   - a member with a '..' component appears in the `tar -tzf` listing
#     as a path containing '..' (rejected by the path check).
#
# If GNU tar ever changes any of these signatures, the validation in
# import_theme() silently stops catching that attack class — this test
# fails first.
set -u

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

# check <desc> <actual> <want>
check() {
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1 (got: $2, want: $3)"
        fail=1
    fi
}

# ── a benign theme archive: only files + dirs ────────────────────────────
mkdir -p "$work/good/theme"
echo "[theme]" > "$work/good/theme/theme.ini"
echo png > "$work/good/theme/title.png"
tar -czf "$work/good.emerald" -C "$work/good" theme
types=$(tar -tvzf "$work/good.emerald" | cut -c1 | sort -u)
check "benign archive entry types" "$types" $'-\nd'

# ── symlink member: type char 'l' ────────────────────────────────────────
mkdir -p "$work/sym"
ln -s /etc/passwd "$work/sym/evil"
tar -czf "$work/sym.emerald" -C "$work/sym" evil
t=$(tar -tvzf "$work/sym.emerald" | grep 'evil' | head -1 | cut -c1)
check "symlink member type char" "$t" 'l'

# ── hardlink member: type char 'h' ───────────────────────────────────────
mkdir -p "$work/hard"
echo data > "$work/hard/real.png"
ln "$work/hard/real.png" "$work/hard/hard.png"
tar -czf "$work/hard.emerald" -C "$work/hard" real.png hard.png
hlines=$(tar -tvzf "$work/hard.emerald" | grep -c '^h' || true)
[ "$hlines" -ge 1 ] || { echo "FAIL: hardlink member not detected as 'h'"; fail=1; }

# ── FIFO member: type char 'p' ───────────────────────────────────────────
mkdir -p "$work/fifo"
mkfifo "$work/fifo/pipe"
tar -czf "$work/fifo.emerald" -C "$work/fifo" pipe
t=$(tar -tvzf "$work/fifo.emerald" | grep 'pipe' | head -1 | cut -c1)
check "FIFO member type char" "$t" 'p'

# ── '..' member: appears in the -tzf listing with a '..' component ───────
# GNU tar SANITIZES '..' at creation time, so a malicious archive is
# built with python3 tarfile (arbitrary member names) - the way such
# archives actually arrive.
command -v python3 >/dev/null || { echo "SKIP: python3 not available"; exit 0; }
mkdir -p "$work/parent/sib" "$work/parent/arc"
echo x > "$work/parent/sib/file"
python3 - "$work/slip.emerald" "$work/parent/sib/file" <<'PYEOF'
import sys, tarfile
arc, src = sys.argv[1], sys.argv[2]
tf = tarfile.open(arc, 'w:gz')
ti = tarfile.TarInfo('../sib/file')
f = open(src, 'rb')
ti.size = f.seek(0, 2)
f.seek(0)
tf.addfile(ti, f)
tf.close()
PYEOF
listing=$(tar -tzf "$work/slip.emerald" 2>/dev/null)
case "$listing" in
    *..*) ;;
    *) echo "FAIL: '..' member not visible in tar -tzf listing: $listing"; fail=1 ;;
esac

# ── absolute-path member: appears in the -tzf listing '/'-prefixed ───────
python3 - "$work/abs.emerald" "$work/parent/sib/file" <<'PYEOF'
import sys, tarfile
arc, src = sys.argv[1], sys.argv[2]
tf = tarfile.open(arc, 'w:gz')
ti = tarfile.TarInfo('/etc/pwned')
f = open(src, 'rb')
ti.size = f.seek(0, 2)
f.seek(0)
tf.addfile(ti, f)
tf.close()
PYEOF
listing=$(tar -tzf "$work/abs.emerald" 2>/dev/null)
case "$listing" in
    /*) ;;
    *) echo "FAIL: absolute member not '/'-prefixed in tar -tzf listing: $listing"; fail=1 ;;
esac

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: tar signatures for the M23 import validation are stable"

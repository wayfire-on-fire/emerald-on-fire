#!/usr/bin/env bash
# test_install_home.sh — M22 (issue #1): install.sh under `sudo` must
# place the user's emerald state in the INVOKING user's home, not /root.
#
# `sudo` resets HOME to /root; the script's FATAL messages push users to
# `sudo $0`. Pre-M22, `EMERALD_DIR="$HOME/.emerald"` (and the settings.ini
# -> wayfire.ini migration, and the XDG / .local/bin fallbacks) all targeted
# /root, so the real user's session got nothing.
#
# The test extracts the resolve_user_home() block from install.sh (between
# the BEGIN/END markers) and exercises it in controlled environments
# (EUID is a read-only bash variable, so the function takes it as an
# argument), then asserts the user-state sites use $USER_HOME.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../install.sh"
[ -f "$SRC" ] || { echo "FAIL: install.sh not found"; exit 1; }

# Extract the function block between the markers (exactly ONE block).
block=$(awk '/# BEGIN M22-user-home/{f=1; next}
             /# END M22-user-home/{f=0}
             f' "$SRC")
[ -n "$block" ] || { echo "FAIL: M22-user-home block not found in install.sh"; exit 1; }
nbeg=$(grep -c '^# BEGIN M22-user-home' "$SRC")
nend=$(grep -c '^# END M22-user-home' "$SRC")
[ "$nbeg" -eq 1 ] && [ "$nend" -eq 1 ] ||
    { echo "FAIL: expected exactly one M22-user-home block (begin=$nbeg end=$nend)"; exit 1; }

fail=0

# run_case <euid> <sudo_user> <home> <want> <getent_behavior>
# getent_behavior: "alice" (a passwd entry for alice),
#                  "alice smith" (a passwd entry with a spaced home),
#                  "none" (no entries)
run_case() {
    local euid="$1" sudo_user="$2" home="$3" want="$4" which="$5"
    local out
    out=$(bash -c '
        which="$1"; euid="$2"; sudo_user="$3"; home="$4"; block="$5"
        getent() {
            if [ "$1" = "passwd" ]; then
                if [ "$which" = "alice" ] && [ "$2" = "alice" ]; then
                    printf "alice:x:1000:1000:alice:/home/alice:/bin/bash\n"
                elif [ "$which" = "alice smith" ] && [ "$2" = "bob" ]; then
                    printf "bob:x:1001:1001:bob:/home/alice smith:/bin/bash\n"
                fi
            fi
        }
        eval "$block"
        resolve_user_home "$euid" "$sudo_user" "$home"
    ' _ "$which" "$euid" "$sudo_user" "$home" "$block")
    if [ "$out" != "$want" ]; then
        echo "FAIL: resolve_user_home($euid, $sudo_user, $home) = $out (want $want)"
        fail=1
    fi
}

# 1. root + SUDO_USER with a known passwd entry -> SUDO_USER's home.
run_case 0 alice /root /home/alice alice
# 2. root without SUDO_USER (real root session) -> its own home.
run_case 0 "" /root /root none
# 3. non-root -> $HOME (sudo not involved).
run_case 1000 alice /home/alice /home/alice alice
# 4. home with spaces survives the quoting (ChatGPT follow-up).
run_case 0 bob /root "/home/alice smith" "alice smith"
# 5. root + SUDO_USER with NO passwd entry -> VISIBLE failure (not a
#    silent /root fallback, which would recreate the M22 bug).
out=$(bash -c '
    getent() { :; }
    block="$1"
    eval "$block"
    resolve_user_home 0 bogus /root
' _ "$block" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || { echo "FAIL: unresolvable SUDO_USER must fail (rc=0)"; fail=1; }
case "$out" in
    *FATAL*sudo\ user\ bogus*) ;;
    *) echo "FAIL: unresolvable SUDO_USER must print a FATAL message (got: $out)"; fail=1 ;;
esac

# 6. The user-state sites must use $USER_HOME, not raw $HOME: the seed,
# the migration source/targets, and the XDG / .local/bin fallbacks.
for site in 'EMERALD_DIR="$USER_HOME/.emerald"' \
            'ESET="$USER_HOME/.emerald/settings.ini"' \
            '$USER_HOME/.config/lxqt/wayland/lxqt-wayfire.ini' \
            '$USER_HOME/.config/wayfire.ini' \
            '${XDG_DATA_HOME:-$USER_HOME/.local/share}'; do
    grep -qF "$site" "$SRC" || { echo "FAIL: missing user-state site: $site"; fail=1; }
done

# 7. No raw $HOME user-state reference may remain anywhere in the script
# (the only legitimate one is the fallback argument to resolve_user_home).
leftover=$(grep -nE '\$HOME/\.?(emerald|config|local)' "$SRC" \
    | grep -v 'resolve_user_home' || true)
if [ -n "$leftover" ]; then
    echo "FAIL: raw \$HOME user-state reference remains:"
    echo "$leftover"
    fail=1
fi

# 8. Ownership (ChatGPT follow-up): under sudo the seed files and the
# migrated wayfire.ini would be root-owned - the invoking user must be
# able to edit them. Both user-state creation paths must chown the
# result back to SUDO_USER when elevated.
grep -qF 'chown -R "$SUDO_USER": "$EMERALD_DIR"' "$SRC" \
    || { echo "FAIL: the seed dir must be chowned to SUDO_USER under sudo"; fail=1; }
grep -qF 'chown "$SUDO_USER": "$WF_INI"' "$SRC" \
    || { echo "FAIL: the migrated wayfire.ini must be chowned to SUDO_USER under sudo"; fail=1; }

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: install.sh USER_HOME resolution (M22)"

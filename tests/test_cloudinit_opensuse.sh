#!/usr/bin/env bash
# test_cloudinit_opensuse.sh — the openSUSE cloud-init user-data must not ship
# a fixed, guessable root password with password auth enabled. The test VM is
# accessed via SSH key (run-kvm.sh injects the host's public key), so password
# auth is unnecessary; a fixed root password equal to the project name
# ("emeraldwf") is a guessable secret. The arch user-data already does this
# right (ssh_pwauth: false, no chpasswd); this brings openSUSE in line.
# The fix: ssh_pwauth: false (key-only) and no chpasswd root password.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
UD="$ROOT/e2e/distros/cloud-init/opensuse/user-data"
[ -f "$UD" ] || { echo "FAIL: opensuse user-data not found"; exit 1; }

fail=0

# 1. Password auth must be DISABLED (we use key-based auth).
if grep -qE 'ssh_pwauth:[[:space:]]*true' "$UD"; then
    echo "FAIL: ssh_pwauth is true (password auth enabled)"; fail=1
elif grep -qE 'ssh_pwauth:[[:space:]]*false' "$UD"; then
    echo "ok: ssh_pwauth is false (key-only auth)"
else
    echo "FAIL: ssh_pwauth is not set to false"; fail=1
fi

# 2. No fixed, guessable root password (the chpasswd root:emeraldwf entry).
if grep -qE 'root:emeraldwf' "$UD"; then
    echo "FAIL: a fixed, guessable root password is set (root:emeraldwf)"; fail=1
else
    echo "ok: no fixed, guessable root password"
fi

# 3. The root password is explicitly LOCKED (dropping chpasswd only stops
#    setting a password; it does not guarantee the base image has none).
if grep -qE 'usermod -L root' "$UD"; then
    echo "ok: the root password is explicitly locked"
else
    echo "FAIL: the root password is not explicitly locked (usermod -L root)"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: the opensuse cloud-init uses key-only auth with no guessable root password"

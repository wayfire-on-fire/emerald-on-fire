#!/usr/bin/env bash
# test_run_kvm_livewm.sh — run-kvm.sh's bring_up() must refuse to launch a new
# QEMU when one is ALREADY running for the distro. Pre-fix it used a
# deterministic port (2200 + idx) and left the VM running; a re-run would try to
# bind the same port (the old QEMU owns it), the new QEMU would fail to start,
# and the pgrep that finds the "new" pid would instead match the OLD QEMU — so
# the script silently provisioned the stale guest instead of a fresh one.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
RUNKVM="$ROOT/e2e/distros/run-kvm.sh"
[ -f "$RUNKVM" ] || { echo "FAIL: run-kvm.sh not found"; exit 1; }

fail=0

bring_up_body="$(sed -n '/^bring_up()/,/^}/p' "$RUNKVM")"
[ -n "$bring_up_body" ] || { echo "FAIL: bring_up() not found"; exit 1; }

# bring_up must detect an already-running QEMU for the distro (pgrep on the
# per-distro QEMU name) ...
if printf '%s\n' "$bring_up_body" | grep -qE 'pgrep.*emeraldwf-\$d'; then
    echo "ok: bring_up checks for a running per-distro QEMU"
else
    echo "FAIL: bring_up does not check for a running QEMU"; fail=1
fi

# ... and REFUSE (a hard die, not just a log) when one is found.
if printf '%s\n' "$bring_up_body" | grep -q 'already running'; then
    echo "ok: bring_up refuses to launch over a running VM"
else
    echo "FAIL: bring_up does not refuse to launch over a running VM (a re-run would target the stale guest)"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: run-kvm.sh refuses to launch over a live VM"

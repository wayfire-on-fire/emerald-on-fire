#!/usr/bin/env bash
# test_matrix_collect.sh — M26 (issue #1): matrix.sh must exit NON-ZERO
# when any distro's provisioning fails.
#
# Pre-M26 the per-distro `wait` loop only PRINTED failures; matrix.sh
# always exited 0, so automation saw green. The fix accumulates failures
# into COLLECT_RC (in collect_results()) and `exit "$COLLECT_RC"`.
#
# The test extracts the collect_results() block from matrix.sh (between
# the BEGIN/END markers) and runs it in a controlled bash with a STUBBED
# `wait` (the real script needs qemu + /dev/kvm, which the test does not
# have). The stub returns success/failure per pid, so the accumulation
# is exercised directly.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../e2e/distros/matrix.sh"
[ -f "$SRC" ] || { echo "FAIL: matrix.sh not found"; exit 1; }

# Extract the function block between the markers (exactly ONE block).
block=$(awk '/# BEGIN M26-collect/{f=1; next}
             /# END M26-collect/{f=0}
             f' "$SRC")
[ -n "$block" ] || { echo "FAIL: M26-collect block not found in matrix.sh"; exit 1; }
nbeg=$(grep -c '^# BEGIN M26-collect' "$SRC")
nend=$(grep -c '^# END M26-collect' "$SRC")
[ "$nbeg" -eq 1 ] && [ "$nend" -eq 1 ] ||
    { echo "FAIL: expected exactly one M26-collect block (begin=$nbeg end=$nend)"; exit 1; }

fail=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# run_collect <name> <fail-pid-or-empty>
#   pids=(1 2 3) ds=(a b c); the stubbed wait FAILS only for <fail-pid>.
#   Prints the resulting COLLECT_RC.
run_collect() {
  local name="$1" failpid="$2"
  local out rc
  out=$(FAILPID="$failpid" HERE="$work" bash -c '
    HERE="$HERE"; FAILPID="$FAILPID"
    pids=(1 2 3); ds=(a b c)
    wait() {
      if [ -n "$FAILPID" ] && [ "$1" = "$FAILPID" ]; then return 1; fi
      return 0
    }
    eval "$1"
    collect_results
    printf "rc=%s\n" "$COLLECT_RC"
  ' _ "$block")
  rc=$(printf '%s\n' "$out" | sed -n 's/^rc=//p')
  printf '%s: %s\n' "$name" "$out"
  COLLECT_GOT="$rc"
}

# ── 1. one distro fails -> COLLECT_RC=1 ───────────────────────────────────
run_collect one-fails "2"
[ "$COLLECT_GOT" = "1" ] || { echo "FAIL(one-fails): a failed distro must set COLLECT_RC=1 (got $COLLECT_GOT)"; fail=1; }

# ── 2. all succeed -> COLLECT_RC=0 ───────────────────────────────────────
run_collect all-pass ""
[ "$COLLECT_GOT" = "0" ] || { echo "FAIL(all-pass): all-success must leave COLLECT_RC=0 (got $COLLECT_GOT)"; fail=1; }

# ── 3. the LAST distro fails -> still COLLECT_RC=1 (not masked by order) ─
run_collect last-fails "3"
[ "$COLLECT_GOT" = "1" ] || { echo "FAIL(last-fails): a late failure must still set COLLECT_RC=1 (got $COLLECT_GOT)"; fail=1; }

# ── 3b. multiple failures -> every child is waited on and reported ────────
# (ChatGPT follow-up: the loop must collect ALL outcomes, not stop at the
# first failure.) The stubbed wait fails for pids 1 AND 3.
multi_out=$(HERE="$work" bash -c '
  HERE="$HERE"
  pids=(1 2 3); ds=(a b c)
  wait() {
    case "$1" in 1|3) return 1 ;; *) return 0 ;; esac
  }
  eval "$1"
  collect_results
  printf "rc=%s\n" "$COLLECT_RC"
' _ "$block")
multi_rc=$(printf '%s\n' "$multi_out" | sed -n 's/^rc=//p')
nfail=$(printf '%s\n' "$multi_out" | grep -c 'failed')
printf 'multi-fail: rc=%s failures-reported=%s\n' "$multi_rc" "$nfail"
[ "$multi_rc" = "1" ] || { echo "FAIL(multi-fail): two failures must set COLLECT_RC=1"; fail=1; }
[ "$nfail" -eq 2 ] || { echo "FAIL(multi-fail): both failures must be reported (got $nfail)"; fail=1; }

# ── 4. main() must exit with the accumulated rc, not unconditionally 0 ────
grep -qE 'exit *"\$COLLECT_RC"' "$SRC" \
    || { echo "FAIL: main() must exit with \$COLLECT_RC (M26)"; fail=1; }
# The old unconditional green must be gone: no bare `exit 0` after the
# wait/collect loop in the bring-up path (the --teardown/--summary
# shortcuts legitimately exit 0 - those are checked by their own flags).
tail_path=$(awk '/^main\(\) {/{f=1} f' "$SRC")
case "$tail_path" in
    *'--teardown) teardown; exit 0'*) ;;  # expected shortcut
esac
if printf '%s' "$tail_path" | grep -qE '^[[:space:]]*exit *0[[:space:]]*$'; then
    echo "FAIL: the bring-up path must not end with a bare 'exit 0' (M26)"; fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: matrix.sh accumulates distro failures (M26)"

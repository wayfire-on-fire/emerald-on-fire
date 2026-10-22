#!/usr/bin/env bash
# emeraldwf multi-distro — bring up ALL distros in distros.conf, provision each
# (in parallel), leave them running, and print the summary table.
#
# Usage:
#   matrix.sh                  bring up + provision all (leave running)
#   matrix.sh --wpm            also validate the WPM install path on each
#   matrix.sh --headless-e2e   also run the headless e2e on each
#   matrix.sh --teardown       destroy all VMs
#   matrix.sh --summary        just (re)print the summary table
#
# The visual QA is by eye: after this returns, watch the N QEMU windows and
# work through e2e/CHECKLIST.md on each.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF="$HERE/distros.conf"
RESULTS="$HERE/results"

list_distros() { awk '/^[[:space:]]*#/{next} NF>=6{print $1}' "$CONF"; }

json_field() {  # file key
  grep -oE "\"$2\": *\"[^\"]*\"" "$1" 2>/dev/null | sed 's/.*: *"//; s/"$//' || true
}

summary() {
  printf '%-8s  %-24s  %-14s  %-8s  %-14s  %-16s\n' \
    "distro" "wayfire" "install.sh" "wpm" "headless_e2e" "manual-checklist"
  local d f
  for d in $(list_distros); do
    f="$RESULTS/$d.json"
    if [ -f "$f" ]; then
      printf '%-8s  %-24s  %-14s  %-8s  %-14s  %-16s\n' \
        "$d" \
        "$(json_field "$f" wayfire_version) ($(json_field "$f" wayfire_source))" \
        "$(json_field "$f" install_sh)" \
        "$(json_field "$f" wpm)" \
        "$(json_field "$f" headless_e2e)" \
        "(by eye)"
    else
      printf '%-8s  %-24s  %-14s  %-8s  %-14s  %-16s\n' "$d" "-" "-" "-" "-" "-"
    fi
  done
}

teardown() { local d; for d in $(list_distros); do "$HERE/run-kvm.sh" destroy "$d"; done; }

main() {
  local a
  for a in "$@"; do
    case "$a" in
      --teardown) teardown; exit 0 ;;
      --summary)  summary; exit 0 ;;
    esac
  done

  local extra=()
  for a in "$@"; do
    case "$a" in
      --wpm)          extra+=(--wpm) ;;
      --headless-e2e) extra+=(--headless-e2e) ;;
    esac
  done

  local d pids=() ds=()
  for d in $(list_distros); do
    if [ ${#extra[@]} -gt 0 ]; then
      "$HERE/run-kvm.sh" "$d" "${extra[@]}" &
    else
      "$HERE/run-kvm.sh" "$d" &
    fi
    pids+=($!); ds+=("$d")
  done

  local i
  for i in "${!pids[@]}"; do
    if ! wait "${pids[$i]}"; then
      echo "[matrix] ${ds[$i]} failed (see $HERE/run/${ds[$i]}/serial.log)"
    fi
  done

  echo
  summary
  echo
  echo "[matrix] all VMs left RUNNING — watch the QEMU windows; run e2e/CHECKLIST.md by eye."
  echo "[matrix] teardown: $0 --teardown"
}
main "$@"

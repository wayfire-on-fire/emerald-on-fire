#!/usr/bin/env bash
# test_run_kvm_fetch.sh — M25 (issue #1): run-kvm.sh fetch_image() must
# verify the downloaded .part BEFORE promoting it to the cache.
#
# Pre-M25 the `mv "$img.part" "$img"` ran FIRST, so a sha256 mismatch
# (die) left the bad file as images/$d.qcow2 and every subsequent run
# "reused the cache" — booting VMs from the corrupt image. The fix
# verifies the .part, rm's it on mismatch, and only promotes it after
# the check passes.
#
# The test extracts the fetch_image() block from run-kvm.sh (between the
# BEGIN/END markers) and runs it in a controlled bash with stubbed
# curl/log/die/pin_sha (the real script needs qemu + network, which the
# test does not have).
set -u

SRC="$(cd "$(dirname "$0")" && pwd)/../e2e/distros/run-kvm.sh"
[ -f "$SRC" ] || { echo "FAIL: run-kvm.sh not found"; exit 1; }

# Extract the function block between the markers (exactly ONE block).
block=$(awk '/# BEGIN M25-fetch/{f=1; next}
             /# END M25-fetch/{f=0}
             f' "$SRC")
[ -n "$block" ] || { echo "FAIL: M25-fetch block not found in run-kvm.sh"; exit 1; }
nbeg=$(grep -c '^# BEGIN M25-fetch' "$SRC")
nend=$(grep -c '^# END M25-fetch' "$SRC")
[ "$nbeg" -eq 1 ] && [ "$nend" -eq 1 ] ||
    { echo "FAIL: expected exactly one M25-fetch block (begin=$nbeg end=$nend)"; exit 1; }

fail=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# run_case <name> <pinned-sha> <download-content>
#   runs fetch_image in a controlled bash; the stubbed curl writes
#   <download-content> to the -o target. Prints one line per assertion
#   and sets fail on any mismatch.
run_case() {
  local name="$1" sha="$2" content="$3"
  local images="$work/$name"
  mkdir -p "$images"
  local out rc
  out=$(CURL_CONTENT="$content" IMAGES="$images" \
        PINNED_OUT="$work/$name.pinned" bash -c '
    IMAGES="$IMAGES"; CURL_CONTENT="$CURL_CONTENT"; PINNED_OUT="$PINNED_OUT"
    curl() {
      local target="" prev=""
      for a in "$@"; do
        if [ "$prev" = "-o" ]; then target="$a"; fi
        prev="$a"
      done
      printf "%s" "$CURL_CONTENT" > "$target"
    }
    log() { :; }
    die() { printf "DIE: %s\n" "$*" >&2; exit 1; }
    pin_sha() { printf "%s %s\n" "$1" "$2" > "$PINNED_OUT"; }
    eval "$1"
    fetch_image testdistro "https://example.com/x.qcow2" "$2"
  ' _ "$block" "$sha")
  rc=$?
  local cached=0 part=0
  [ -f "$images/testdistro.qcow2" ] && cached=1
  [ -f "$images/testdistro.qcow2.part" ] && part=1
  printf '%s: rc=%s cached=%s part=%s\n' "$name" "$rc" "$cached" "$part"
  CASE_RC="$rc"; CASE_CACHED="$cached"; CASE_PART="$part"
}

# ── 1. sha mismatch: die, .part REMOVED, nothing promoted to cache ───────
run_case mismatch "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" "corrupt-bytes"
[ "$CASE_RC" -ne 0 ] || { echo "FAIL(mismatch): must fail on sha mismatch"; fail=1; }
[ "$CASE_PART" -eq 0 ] || { echo "FAIL(mismatch): the bad .part must be removed"; fail=1; }
[ "$CASE_CACHED" -eq 0 ] || { echo "FAIL(mismatch): the bad file must NOT be cached (pre-M25 it was)"; fail=1; }

# ── 2. sha match: promoted to cache, no .part left behind ────────────────
good_sha=$(printf '%s' "good-bytes" | sha256sum | awk '{print $1}')
run_case match "$good_sha" "good-bytes"
[ "$CASE_RC" -eq 0 ] || { echo "FAIL(match): must succeed when the sha matches"; fail=1; }
[ "$CASE_CACHED" -eq 1 ] || { echo "FAIL(match): the verified image must be cached"; fail=1; }
[ "$CASE_PART" -eq 0 ] || { echo "FAIL(match): no .part may remain after a successful fetch"; fail=1; }
[ "$(cat "$work/match/testdistro.qcow2")" = "good-bytes" ] ||
    { echo "FAIL(match): cached content must be the downloaded bytes"; fail=1; }

# ── 3. no pinned sha ("-"): pin the computed sha, then promote ───────────
run_case nopin "-" "unpinned-bytes"
[ "$CASE_RC" -eq 0 ] || { echo "FAIL(nopin): must succeed with no pinned sha"; fail=1; }
[ "$CASE_CACHED" -eq 1 ] || { echo "FAIL(nopin): the image must be cached"; fail=1; }
want_pin="testdistro $(printf '%s' "unpinned-bytes" | sha256sum | awk '{print $1}')"
[ "$(cat "$work/nopin.pinned" 2>/dev/null)" = "$want_pin" ] ||
    { echo "FAIL(nopin): must pin the computed sha (got: $(cat "$work/nopin.pinned" 2>/dev/null))"; fail=1; }

# ── 4. already cached: early return, no download at all ──────────────────
run_case_cached_setup() { :; }
images_c="$work/cached"; mkdir -p "$images_c"
printf 'existing-cache' > "$images_c/testdistro.qcow2"
out=$(CURL_CONTENT="should-not-be-written" IMAGES="$images_c" \
      PINNED_OUT="$work/cached.pinned" bash -c '
    IMAGES="$IMAGES"; CURL_CONTENT="$CURL_CONTENT"; PINNED_OUT="$PINNED_OUT"
    curl() { printf "CURL-CALLED\n" >&2; exit 1; }
    log() { :; }
    die() { printf "DIE: %s\n" "$*" >&2; exit 1; }
    pin_sha() { :; }
    eval "$1"
    fetch_image testdistro "https://example.com/x.qcow2" "-"
  ' _ "$block")
rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL(cached): must return 0 for a cached image"; fail=1; }
case "$out" in
    *CURL-CALLED*) echo "FAIL(cached): curl must not be called when cached"; fail=1 ;;
esac
[ "$(cat "$images_c/testdistro.qcow2")" = "existing-cache" ] ||
    { echo "FAIL(cached): the cached image must be left untouched"; fail=1; }

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS: run-kvm.sh fetch_image verifies before caching (M25)"

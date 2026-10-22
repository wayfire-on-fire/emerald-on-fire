#!/usr/bin/env bash
# emeraldwf multi-distro — host-side QEMU/KVM driver (per distro).
# Brings up a disposable VM (qcow2 overlay on a cached cloud base image),
# provisions it in-guest over SSH, and leaves it running for by-eye QA.
#
# Usage:
#   run-kvm.sh <distro>                  bring up + provision (leave running)
#   run-kvm.sh <distro> --wpm            also validate the WPM install path
#   run-kvm.sh <distro> --headless-e2e   also run the headless e2e in-guest
#   run-kvm.sh destroy <distro>          shut down + delete the overlay
#   run-kvm.sh status [distro]           show what is running
#
# Host requirements: qemu-system-x86_64 (KVM + display backend), an ISO tool
# (genisoimage/mkisofs/xorriso), curl, ssh, tar, /dev/kvm, a local display
# for the `-display gtk` window (use -vnc when driving remotely).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"          # repo root
CONF="$HERE/distros.conf"
IMAGES="$HERE/images"
RUN="$HERE/run"
RESULTS="$HERE/results"
TOPDIR="$(basename "$REPO")"
mkdir -p "$IMAGES" "$RUN" "$RESULTS"

log() { printf '[kvm] %s\n' "$*"; }
die() { printf '[kvm] ERROR: %s\n' "$*" >&2; exit 1; }

require_host_tools() {
  command -v qemu-system-x86_64 >/dev/null 2>&1 \
    || die "qemu-system-x86_64 missing — emerge sys-emulators/qemu (use kvm + gtk/sdl)"
  command -v genisoimage >/dev/null 2>&1 || command -v mkisofs >/dev/null 2>&1 \
    || command -v xorriso >/dev/null 2>&1 \
    || die "no ISO tool (genisoimage/mkisofs/xorriso) — emerge app-cdr/cdrtools"
  [ -e /dev/kvm ] || die "/dev/kvm missing — KVM not available on this host"
}

# ── distros.conf access ─────────────────────────────────────────────────────
get_row() {  # -> "name url sha disk user wfsrc"
  awk -v w="$1" '/^[[:space:]]*#/{next} NF>=6 && $1==w {printf "%s %s %s %s %s %s\n",$1,$2,$3,$4,$5,$6; exit}' "$CONF"
}
row_index() {  # 1-based data-row number (drives a stable SSH port)
  awk -v w="$1" '/^[[:space:]]*#/{next} NF>=6 {n++; if($1==w){print n; exit}}' "$CONF"
}

# ── host ssh keypair (auto-create if missing) ───────────────────────────────
ensure_ssh_key() {
  local pub
  pub="$(ls -1 "$HOME/.ssh"/*.pub 2>/dev/null | head -1 || true)"
  if [ -z "$pub" ]; then
    log "no host ssh pubkey — generating $HOME/.ssh/id_ed25519"
    mkdir -p "$HOME/.ssh" && chmod 700 "$HOME/.ssh"
    ssh-keygen -t ed25519 -N '' -f "$HOME/.ssh/id_ed25519" >/dev/null
    pub="$HOME/.ssh/id_ed25519.pub"
  fi
  PUBKEY="$(cat "$pub")"
}

# ── fetch + cache base image; verify/pin sha ────────────────────────────────
pin_sha() {  # distro sha — rewrite the sha field in distros.conf
  awk -v d="$1" -v s="$2" '{ if ($1==d && NF>=6) $3=s; print }' "$CONF" > "$CONF.tmp" && mv "$CONF.tmp" "$CONF"
}
fetch_image() {  # distro url sha
  local d="$1" url="$2" sha="$3"
  local img="$IMAGES/$d.qcow2"
  if [ -f "$img" ]; then log "base image cached: $img"; return 0; fi
  log "downloading $d base image: $url"
  curl -fL --progress-bar -o "$img.part" "$url"
  mv "$img.part" "$img"
  local actual; actual="$(sha256sum "$img" | awk '{print $1}')"
  if [ "$sha" = "-" ] || [ -z "$sha" ]; then
    log "no pinned sha — pinning computed sha for $d: $actual"
    pin_sha "$d" "$actual"
  elif [ "$actual" != "$sha" ]; then
    die "sha256 mismatch for $d: expected $sha got $actual"
  else
    log "sha256 ok: $actual"
  fi
}

# ── build the NoCloud cloud-init seed ISO ───────────────────────────────────
# NoCloud seed: ISO9660, volid "cidata", user-data + meta-data at the root.
build_seed() {  # distro  -> sets SEED_ISO
  local d="$1" seaddir="$RUN/$d/seed"
  mkdir -p "$seaddir"
  sed "s|__PUBKEY__|${PUBKEY}|" "$HERE/cloud-init/$d/user-data" > "$seaddir/user-data"
  printf 'instance-id: emeraldwf-%s\nlocal-hostname: emeraldwf-%s\n' "$d" "$d" > "$seaddir/meta-data"
  SEED_ISO="$RUN/$d/seed.iso"
  rm -f "$SEED_ISO"
  local iso_tool=""
  for t in genisoimage mkisofs; do
    command -v "$t" >/dev/null 2>&1 && { iso_tool="$t"; break; }
  done
  if [ -n "$iso_tool" ]; then
    ( cd "$seaddir" && "$iso_tool" -output "$SEED_ISO" -volid cidata -joliet -rock user-data meta-data ) >/dev/null 2>&1
  elif command -v xorriso >/dev/null 2>&1; then
    xorriso -as iso9660 -use -iso_level 1 -full_iso9660 -volid cidata \
      -rock "$seaddir/user-data" "$seaddir/meta-data" -o "$SEED_ISO" >/dev/null 2>&1
  fi
  [ -s "$SEED_ISO" ] || die "seed ISO not built — no working ISO tool. Run: emerge app-cdr/cdrtools (mkisofs). (The installed xorriso lacks the iso9660 frontend.)"
  log "seed iso: $SEED_ISO"
}

# ── ssh/scp helpers for a given port ────────────────────────────────────────
ssh_for() { printf "ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR -p %s root@127.0.0.1" "$1"; }
scp_for() { printf "scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -P %s" "$1"; }

# ── deliver the working-tree source + provision.sh into the VM ──────────────
deliver() {  # distro port
  local d="$1" port="$2"
  local tb="$RUN/$d/src.tar.gz"
  log "building source tarball (working tree, minus heavy/generated dirs)"
  # -C the repo's PARENT so the archive's top level is "$TOPDIR/".
  tar -czf "$tb" \
    --exclude='.git' --exclude='build' --exclude='build-docker' --exclude='dist' \
    --exclude='e2e/out' --exclude='e2e/distros/run' --exclude='e2e/distros/images' \
    -C "$(dirname "$REPO")" "$TOPDIR"
  local nfiles; nfiles="$(tar -tzf "$tb" 2>/dev/null | wc -l)"
  [ "$nfiles" -gt 50 ] || die "source tarball looks empty/truncated ($nfiles entries)"
  local scp; scp="$(scp_for "$port")"
  local ssh; ssh="$(ssh_for "$port")"
  $scp "$tb" "$HERE/provision.sh" root@127.0.0.1:/root/
  # Extract in-guest and verify: a bare tar over ssh has produced 0-byte files
  # on occasion, which made install.sh a silent no-op and left no plugin.
  $ssh bash -s <<GUEST
set -e
TOPDIR='$TOPDIR'
ok=0
for a in 1 2 3; do
  rm -rf "/root/\$TOPDIR"
  tar -xzf /root/src.tar.gz -C /root
  if [ -s "/root/\$TOPDIR/meson.build" ] && [ -s "/root/\$TOPDIR/install.sh" ]; then
    ok=1; break
  fi
  echo "[deliver] extraction attempt \$a gave empty files — retrying" >&2
done
[ "\$ok" = 1 ] || { echo "[deliver] ERROR: in-guest source tree is corrupt (0-byte files)" >&2; exit 1; }
chmod +x /root/provision.sh
GUEST
  log "delivered $TOPDIR/ + provision.sh to /root (verified)"
}

# ── wait for ssh ────────────────────────────────────────────────────────────
wait_ssh() {  # port
  local port="$1" i ssh
  ssh="$(ssh_for "$port")"
  log "waiting for ssh on 127.0.0.1:$port ..."
  for i in $(seq 1 90); do
    if $ssh true 2>/dev/null; then log "ssh up"; return 0; fi
    sleep 2
  done
  die "ssh did not come up on port $port (check $RUN/*/serial.log)"
}

# ── bring up + provision (leave running) ────────────────────────────────────
bring_up() {  # distro [extra provision args...]
  local d="$1"; shift
  require_host_tools
  local row; row="$(get_row "$d")"
  [ -n "$row" ] || die "distro '$d' not found in $CONF"
  local NAME URL SHA DISK SU WFSRC
  read -r NAME URL SHA DISK SU WFSRC <<< "$row"
  local idx port
  idx="$(row_index "$d")"; port=$((2200 + idx))

  ensure_ssh_key
  fetch_image "$d" "$URL" "$SHA"
  local base="$IMAGES/$d.qcow2"
  local rundir="$RUN/$d"; mkdir -p "$rundir"
  local overlay="$rundir/overlay.qcow2"
  if [ -f "$overlay" ]; then
    log "overlay exists — reusing (run 'destroy $d' first for a clean slate)"
  else
    # Overlay virtual size must match the base's (a smaller one breaks
    # dracut's root-UUID lookup); query the base, fall back to disk_gb.
    local vsize
    vsize="$(qemu-img info "$base" 2>/dev/null | awk '/virtual size/ {gsub(/[()]/,"",$5); print $5; exit}')"
    [ -n "$vsize" ] || vsize="${DISK}G"
    log "creating ${vsize}-byte overlay on $base (matching base virtual size)"
    qemu-img create -f qcow2 -F qcow2 -o "backing_file=$base,compat=1.1" "$overlay" "$vsize" >/dev/null
  fi
  build_seed "$d"

  log "launching QEMU (KVM, gtk display, ssh -> 127.0.0.1:$port)"
  # setsid so QEMU survives this script's process group exiting
  setsid qemu-system-x86_64 \
    -name "emeraldwf-$d" \
    -enable-kvm -m 4G -smp 4 \
    -drive "if=virtio,file=$overlay,format=qcow2" \
    -drive "if=none,id=seed,file=$SEED_ISO,format=raw" \
    -device ide-cd,drive=seed \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$port-:22" \
    -device virtio-net,netdev=n0 \
    -display gtk \
    -serial "file:$rundir/serial.log" \
    -monitor "unix:$rundir/mon.sock,server,nowait" \
    >"$rundir/qemu.log" 2>&1 &
  sleep 2
  local qpid; qpid="$(pgrep -f "qemu-system-x86_64.*emeraldwf-$d" | head -1)"
  [ -n "$qpid" ] || die "QEMU did not start (see $rundir/qemu.log)"
  echo "$qpid" > "$rundir/qemu.pid"
  log "qemu pid=$qpid (window title: 'emeraldwf-$d')"

  wait_ssh "$port"
  deliver "$d" "$port"

  local ssh; ssh="$(ssh_for "$port")"
  local scp; scp="$(scp_for "$port")"
  log "provisioning in-guest (a few minutes)..."
  $ssh "bash /root/provision.sh --session-user $SU --src-dir /root/$TOPDIR $*"
  $scp "root@127.0.0.1:/root/distros-result.json" "$RESULTS/$d.json"
  log "result: $RESULTS/$d.json"
  cat "$RESULTS/$d.json"
  echo
  log "VM left RUNNING — watch the QEMU window; run e2e/CHECKLIST.md by eye."
  log "teardown: $0 destroy $d"
}

# ── destroy ─────────────────────────────────────────────────────────────────
destroy() {  # distro
  local d="$1"
  local rundir="$RUN/$d"
  [ -d "$rundir" ] || { log "nothing to destroy for $d"; return 0; }
  local pidfile="$rundir/qemu.pid" qpid
  if [ -f "$pidfile" ]; then
    qpid="$(cat "$pidfile")"
    log "stopping qemu pid=$qpid"
    kill "$qpid" 2>/dev/null || true
    sleep 3
    kill -9 "$qpid" 2>/dev/null || true
  fi
  log "removing overlay + run dir for $d"
  rm -rf "$rundir"
  log "destroyed $d"
}

# ── status ──────────────────────────────────────────────────────────────────
status() {
  local found=0 p d qpid
  for p in "$RUN"/*/qemu.pid; do
    [ -f "$p" ] || continue
    found=1
    d="$(basename "$(dirname "$p")")"
    qpid="$(cat "$p")"
    if kill -0 "$qpid" 2>/dev/null; then
      log "$d: RUNNING (pid $qpid, ssh port $((2200 + $(row_index "$d"))))"
    else
      log "$d: stale pidfile (not running)"
    fi
  done
  [ "$found" -eq 1 ] || log "no VMs running"
}

# ── dispatch ────────────────────────────────────────────────────────────────
main() {
  local cmd="${1:-}"
  case "$cmd" in
    "")      die "usage: $0 <distro> [--wpm] [--headless-e2e] | destroy <distro> | status" ;;
    destroy) shift; destroy "${1:-}" ;;
    status)  shift; status "${1:-}" ;;
    *)       bring_up "$@" ;;
  esac
}
main "$@"

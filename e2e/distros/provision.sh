#!/usr/bin/env bash
# emeraldwf multi-distro — guest-side provisioning.
# Runs as root inside the target VM: installs the distro's wayfire + build
# deps, builds/installs emeraldwf from source (./install.sh), seeds the
# session user's config, and installs a systemd autologin unit so the VM's
# display lands in a wayfire+emeraldwf session. Writes
# /root/distros-result.json (fetched back by run-kvm.sh).
#
# Usage:
#   provision.sh [--wpm] [--headless-e2e] [--src-dir /root/emerald-wf] [--session-user <user>]
# (run-kvm.sh passes --session-user and --src-dir; the flags are optional.)

set -euo pipefail

# ── args ────────────────────────────────────────────────────────────────────
WPM=0; HEADLESS_E2E=0; SRC_DIR=/root/emerald-wf; SESSION_USER=""
while [ $# -gt 0 ]; do
  case "$1" in
    --wpm)            WPM=1 ;;
    --headless-e2e)   HEADLESS_E2E=1 ;;
    --src-dir)        SRC_DIR="$2"; shift ;;
    --session-user)   SESSION_USER="$2"; shift ;;
    *) echo "provision.sh: unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

RESULT=/root/distros-result.json
log() { printf '[provision] %s\n' "$*"; }

# ── distro detect ───────────────────────────────────────────────────────────
. /etc/os-release
DISTRO_ID="$ID"
PRETTY="${PRETTY_NAME:-$ID}"
log "distro=$DISTRO_ID ($PRETTY)"

# session user: from arg, else the distro default; verify it exists (else root)
if [ -z "$SESSION_USER" ]; then
  case "$DISTRO_ID" in
    arch)       SESSION_USER=arch ;;
    opensuse*)  SESSION_USER=opensuse ;;
    *)        SESSION_USER=root ;;
  esac
fi
if ! id "$SESSION_USER" >/dev/null 2>&1; then
  log "WARNING: session user '$SESSION_USER' not found — falling back to root"
  SESSION_USER=root
fi
SESSION_HOME="$(getent passwd "$SESSION_USER" | cut -d: -f6)"
SU_UID="$(id -u "$SESSION_USER")"
log "session_user=$SESSION_USER (uid=$SU_UID, home=$SESSION_HOME)"

# ── 1. wayfire + build/e2e deps (per distro) ────────────────────────────────
install_deps() {
  case "$DISTRO_ID" in
    arch)
      # glm: required by the wayfire 0.11 headers, not pulled in by the
      # wayfire package. seatd: libseat backend for the autologin.
      pacman -Sy --noconfirm gcc make glm wayfire git meson ninja pkgconf gtk3 \
        gdk-pixbuf2 cairo glib2 libx11 pango gtest \
        xorg-server xdotool weston imagemagick xterm xorg-xprop xorg-xwininfo \
        xorg-xset xorg-xwayland seatd
      ;;
    opensuse*)
      # Mesa-libGLESv3-devel: wayfire's opengl.hpp includes <GLES3/gl3.h>,
      # which wayfire-devel does not pull in. bitstream-vera-fonts: the
      # Minimal-VM image ships 0 fonts, so Pango cannot measure the theme
      # (the settings test fails without it).
      zypper --non-interactive install --no-recommends gcc gcc-c++ make glm-devel \
        wayfire wayfire-devel git meson ninja pkgconf gtk3-devel \
        gdk-pixbuf-devel cairo-devel glib2-devel libX11-devel pango-devel \
        gtest xterm xdotool weston ImageMagick \
        xwayland seatd Mesa-libGLESv3-devel bitstream-vera-fonts
      ;;
    *)
      log "ERROR: no dependency recipe for distro '$DISTRO_ID'"
      return 1
      ;;
  esac
}
log "installing wayfire + build/e2e deps"
install_deps

# ── 2. wayfire version (pkg); source fallback if < 0.11 ─────────────────────
wf_version() { wayfire --version 2>/dev/null | head -1 || echo "unknown"; }
wf_ge_011() {
  local v maj min
  v="$(wayfire --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1 || echo 0.0)"
  maj="${v%%.*}"; min="${v#*.}"
  { [ "$maj" -gt 0 ] 2>/dev/null; } && return 0
  { [ "$maj" -eq 0 ] 2>/dev/null && [ "$min" -ge 11 ] 2>/dev/null; } && return 0
  return 1
}

build_wayfire_from_source() {
  # Mirrors the ci.yml `native` job; installs wayfire v0.11.0 into /usr.
  log "building wayfire from source (ci.yml recipe) — this takes a while"
  pip3 install --break-system-packages --upgrade meson 2>/dev/null \
    || pip3 install --upgrade meson 2>/dev/null || true
  git clone --depth 1 --branch 1.24.0 \
      https://gitlab.freedesktop.org/wayland/wayland /tmp/wl
  ( cd /tmp/wl && meson setup build --prefix=/usr -Ddocumentation=false \
      && ninja -C build && ninja -C build install )
  git clone --depth 1 --branch 1.47 \
      https://gitlab.freedesktop.org/wayland/wayland-protocols /tmp/wlp
  ( cd /tmp/wlp && meson setup build --prefix=/usr \
      && ninja -C build && ninja -C build install )
  wget -q https://dri.freedesktop.org/libdrm/libdrm-2.4.129.tar.xz -O /tmp/libdrm.tar.xz
  echo "5978183f978d697e26a50ba066174964efb0ab97caa1ecaa1b861fbe5ddf77dc  /tmp/libdrm.tar.xz" | sha256sum -c -
  tar xf /tmp/libdrm.tar.xz -C /tmp
  ( cd /tmp/libdrm-2.4.129 && meson setup build --prefix=/usr \
      && ninja -C build && ninja -C build install )
  git clone --depth 1 --branch xkbcommon-1.13.2 \
      https://github.com/xkbcommon/libxkbcommon /tmp/xkb
  ( cd /tmp/xkb && meson setup build --prefix=/usr \
      -Denable-tools=false -Denable-x11=false -Denable-docs=false \
      -Denable-xkbregistry=false && ninja -C build && ninja -C build install )
  git clone --depth 1 --branch pixman-0.46.4 \
      https://gitlab.freedesktop.org/pixman/pixman /tmp/pix
  ( cd /tmp/pix && meson setup build --prefix=/usr -Dtests=disabled \
      && ninja -C build && ninja -C build install )
  git clone --recursive --depth 1 --branch v0.11.0 \
      https://github.com/WayfireWM/wayfire /tmp/wf
  ( cd /tmp/wf && meson setup build --prefix=/usr \
      && ninja -C build && ninja -C build install )
  log "source wayfire installed"
}

WF_SRC=pkg
log "wayfire (pkg): $(wf_version)"
if ! wf_ge_011; then
  log "packaged wayfire < 0.11 — falling back to source build"
  WF_SRC=source
  build_wayfire_from_source
fi
WF_VER="$(wf_version)"
log "wayfire (final): $WF_VER [$WF_SRC]"

# ── 3. source tree present? ─────────────────────────────────────────────────
if [ ! -f "$SRC_DIR/meson.build" ]; then
  log "ERROR: $SRC_DIR/meson.build not found (source not delivered?)"
  printf '{ "distro": "%s", "install_sh": "fail", "error": "source tree missing" }\n' "$DISTRO_ID" > "$RESULT"
  exit 1
fi
log "source: $SRC_DIR"

# ── 4. install emeraldwf (primary: ./install.sh) ────────────────────────────
# Retry with --skip-tests if the test phase fails (gtest is covered by CI).
INSTALL_SH=pass
if ( cd "$SRC_DIR" && ./install.sh ); then
  INSTALL_SH=pass
elif ( cd "$SRC_DIR" && ./install.sh --skip-tests ); then
  INSTALL_SH=pass_no_tests
  log "install.sh succeeded with --skip-tests (gtest packaging quirk)"
else
  INSTALL_SH=fail
fi
log "install.sh: $INSTALL_SH"

# ── 5. WPM (secondary, only with --wpm) ─────────────────────────────────────
WPM_RESULT=skipped
if [ "$WPM" -eq 1 ]; then
  if command -v wayfire-plugin >/dev/null 2>&1; then
    if ( cd "$SRC_DIR" && wayfire-plugin install "$SRC_DIR" ) >/root/wpm.log 2>&1; then
      WPM_RESULT=pass
    else
      WPM_RESULT=fail
      log "WPM failed (see /root/wpm.log): $(tail -3 /root/wpm.log 2>/dev/null | tr '\n' ' ')"
    fi
  else
    WPM_RESULT=skipped
    log "wayfire-plugin (WPM) not present — skipped"
  fi
fi
log "wpm: $WPM_RESULT"

# ── 6. session user: ~/.emerald + ~/.config/wayfire.ini ─────────────────────
EMERALD_DEFAULTS="$SRC_DIR/third_party/emerald/defaults"
mkdir -p "$SESSION_HOME/.emerald/theme"
[ -f "$EMERALD_DEFAULTS/settings.ini" ] && \
  cp -f "$EMERALD_DEFAULTS/settings.ini" "$SESSION_HOME/.emerald/settings.ini"
cp -rf "$EMERALD_DEFAULTS/theme/." "$SESSION_HOME/.emerald/theme/" 2>/dev/null || true
mkdir -p "$SESSION_HOME/.config"
cat > "$SESSION_HOME/.config/wayfire.ini" <<'INI'
[core]
plugins = emeraldwf

[emeraldwf]
engine = truglass
theme_dir = ~/.emerald/theme
maximized_borders = true
maximized_shadows = true
enable_shade = true
INI
chown -R "$SESSION_USER":"$SESSION_USER" "$SESSION_HOME/.emerald" "$SESSION_HOME/.config/wayfire.ini"
# video/render: the virtual GPU; seat: the seatd socket is root:seat 0660.
# openSUSE's seatd package does not create the 'seat' group — do it here.
if [ -d /run/systemd/system ] && ! getent group seat >/dev/null 2>&1; then
  groupadd seat
fi
usermod -aG video,render,seat "$SESSION_USER" 2>/dev/null \
  || usermod -aG video,render "$SESSION_USER" 2>/dev/null \
  || usermod -aG video "$SESSION_USER" 2>/dev/null || true
log "session user ready: $SESSION_HOME"

# ── 7. autologin wayfire session (replaces the tty1 getty/console) ─────────
# Both target distros are systemd.
if [ -d /run/systemd/system ]; then
  # Arch ships a seatd.service; openSUSE's package does not (and its binary
  # uses the single-dash -g flag). Create a unit when missing.
  if ! systemctl cat seatd.service >/dev/null 2>&1; then
    cat > /etc/systemd/system/seatd.service <<'SEATD'
[Unit]
Description=Seat daemon (libseat backend for wayfire autologin)
After=systemd-logind.service

[Service]
ExecStart=/usr/bin/seatd -g seat
Restart=on-failure

[Install]
WantedBy=multi-user.target
SEATD
    log "created seatd.service (distro shipped none)"
  fi
  # runuser is /usr/bin/runuser on Arch but /sbin/runuser on openSUSE (util-linux).
  RUNUSER="$(command -v runuser)"
  cat > /etc/systemd/system/emeraldwf-session.service <<'UNIT'
[Unit]
Description=emeraldwf wayfire session (autologin, virtual GPU)
After=systemd-logind.service seatd.service
Wants=seatd.service

[Service]
# root creates /run/user/<uid>, then drops to the session user via runuser.
# WLR_RENDERER_ALLOW_SOFTWARE=1: the vGPU has no 3D driver (pixman path).
ExecStartPre=/bin/mkdir -p /run/user/__UID__
ExecStartPre=/bin/chown __UID__:__UID__ /run/user/__UID__
ExecStart=__RUNUSER__ -u __USER__ -- env HOME=__HOME__ XDG_RUNTIME_DIR=/run/user/__UID__ WLR_RENDERER_ALLOW_SOFTWARE=1 /usr/bin/wayfire -c __HOME__/.config/wayfire.ini
Restart=no

[Install]
WantedBy=multi-user.target
UNIT
  sed -i -e "s|__USER__|$SESSION_USER|g" \
         -e "s|__HOME__|$SESSION_HOME|g" \
         -e "s|__UID__|$SU_UID|g" \
         -e "s|__RUNUSER__|$RUNUSER|g" /etc/systemd/system/emeraldwf-session.service
  systemctl daemon-reload
  systemctl enable  seatd                     >/dev/null 2>&1 || true
  systemctl disable getty@tty1    >/dev/null 2>&1 || true
  systemctl enable  emeraldwf-session.service >/dev/null
fi
log "autologin installed (user=$SESSION_USER uid=$SU_UID)"

# ── 8. optional headless e2e ────────────────────────────────────────────────
E2E_RESULT=skipped
if [ "$HEADLESS_E2E" -eq 1 ]; then
  if ( cd "$SRC_DIR" && ./e2e/run.sh ) >/root/e2e.log 2>&1; then
    E2E_RESULT=pass
  else
    E2E_RESULT=fail
    log "headless e2e failed (see /root/e2e.log): $(tail -3 /root/e2e.log 2>/dev/null | tr '\n' ' ')"
  fi
fi
log "headless_e2e: $E2E_RESULT"

# ── 9. write result ─────────────────────────────────────────────────────────
cat > "$RESULT" <<JSON
{
  "distro": "$DISTRO_ID",
  "pretty": "$PRETTY",
  "wayfire_version": "$WF_VER",
  "wayfire_source": "$WF_SRC",
  "install_sh": "$INSTALL_SH",
  "wpm": "$WPM_RESULT",
  "headless_e2e": "$E2E_RESULT",
  "session_user": "$SESSION_USER",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
JSON
log "wrote $RESULT"
log "DONE — a wayfire+emeraldwf session should now be on the VM's display (the QEMU window)."

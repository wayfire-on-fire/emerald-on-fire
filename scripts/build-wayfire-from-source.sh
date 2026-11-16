#!/usr/bin/env bash
# build-wayfire-from-source.sh — build wayfire v0.11.0 (and the deps its
# bundled wlroots needs) from source into /usr. This is the SINGLE source of
# truth for the from-source recipe, shared by:
#   - the Dockerfile (toolchain image),
#   - the ci.yml `native` job,
#   - e2e/distros/provision.sh (KVM guest).
# Pre-fix the same 6-component recipe (versions, URLs, meson options, and the
# libdrm checksum) was copy-pasted in all three, so a version bump had to be
# made in three places.
set -euo pipefail

# The install step needs root; use sudo only when not already root (the
# Docker image and the KVM guest run as root; the CI native job does not).
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

# The from-source builds require a KNOWN meson. Install the pinned version
# (meson 1.12.1) UNCONDITIONALLY so every environment (Docker image, CI host,
# KVM guest) builds with the SAME meson — a distro's preinstalled meson varies
# and may be older than 1.4.0, so it is not relied on. Install system-wide so
# BOTH 'meson setup' and the install step use the SAME meson (a user-site meson
# in ~/.local/bin is dropped from sudo's PATH, which produced a
# version-mismatched install.dat). Do NOT swallow failure.
$SUDO python3 -m pip install --break-system-packages "meson==1.12.1" 2>/dev/null \
    || $SUDO python3 -m pip install "meson==1.12.1"
# Verify the installed meson is new enough (>= 1.4.0).
meson_ver="$(meson --version 2>/dev/null | head -1 || echo 0.0.0)"
if [ "$(printf '%s\n1.4.0\n' "$meson_ver" | sort -V | head -1)" != "1.4.0" ]; then
    echo "ERROR: meson >= 1.4.0 required for the from-source builds (got: ${meson_ver:-none})" >&2
    exit 1
fi

# build_git BRANCH URL DIR [extra meson args...]: clone, meson setup + build +
# install into /usr, then remove the source dir.
build_git() {
    local branch="$1" url="$2" dir="$3"
    shift 3
    git clone --depth 1 --branch "$branch" "$url" "$dir"
    ( cd "$dir" && meson setup build --prefix=/usr "$@" \
        && ninja -C build && $SUDO ninja -C build install )
    rm -rf "$dir"
}

# wayland 1.24.0 (noble ships 1.22; wlroots needs wayland-server >= 1.24).
build_git 1.24.0 https://gitlab.freedesktop.org/wayland/wayland /tmp/wl \
    -Ddocumentation=false

# wayland-protocols 1.47 (noble ships 1.45; wlroots needs >= 1.47). A separate
# project from wayland; the wayland build above does not install it.
build_git 1.47 https://gitlab.freedesktop.org/wayland/wayland-protocols /tmp/wlp

# libdrm 2.4.129 (noble ships 2.4.125; wlroots needs >= 2.4.129). Official
# signed release tarball, checksum-verified.
wget -q https://dri.freedesktop.org/libdrm/libdrm-2.4.129.tar.xz -O /tmp/libdrm.tar.xz
echo "5978183f978d697e26a50ba066174964efb0ab97caa1ecaa1b861fbe5ddf77dc  /tmp/libdrm.tar.xz" | sha256sum -c -
tar xf /tmp/libdrm.tar.xz -C /tmp
( cd /tmp/libdrm-2.4.129 && meson setup build --prefix=/usr \
    && ninja -C build && $SUDO ninja -C build install )
rm -rf /tmp/libdrm-2.4.129 /tmp/libdrm.tar.xz

# xkbcommon 1.13.2 (noble ships 1.6.0; wlroots needs >= 1.8.0).
build_git xkbcommon-1.13.2 https://github.com/xkbcommon/libxkbcommon /tmp/xkb \
    -Denable-tools=false -Denable-x11=false \
    -Denable-docs=false -Denable-xkbregistry=false

# pixman 0.46.4 (noble ships 0.42.2; wlroots needs >= 0.46.0, render backend).
build_git pixman-0.46.4 https://gitlab.freedesktop.org/pixman/pixman /tmp/pix \
    -Dtests=disabled

# wayfire v0.11.0 (recursive: the bundled wlroots subproject). Provides the
# wayfire.pc + headers the plugin compiles against (scene-graph API, >= 0.11).
git clone --recursive --depth 1 --branch v0.11.0 \
    https://github.com/WayfireWM/wayfire /tmp/wf
( cd /tmp/wf && meson setup build --prefix=/usr \
    && ninja -C build && $SUDO ninja -C build install )
rm -rf /tmp/wf

echo "wayfire v0.11.0 (from source) installed into /usr"

# Toolchain image for emerald-on-fire (the `emeraldwf` wayfire plugin).
#
# Same shape as a typical "toolchain image" build: the image carries only the
# build toolchain — including wayfire >= 0.11, which is built from source
# because it is not packaged for the base distro. The actual plugin build runs
# in an ephemeral container with the project mounted at /app; the artifacts
# land in ./dist (see the Makefile `binary` target).
#
# Base: ubuntu:24.04 (noble) — ships the GTK3 / cairo / pango / gdk-pixbuf /
# glib versions wayfire 0.11 builds against. Bump as needed.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Build tools + the full wayfire build-dependency closure (wayfire is built
# from source below, so it needs its own deps, not just the plugin's) + the
# plugin's own dependencies.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        pkg-config \
        meson \
        ninja-build \
        cmake \
        bison \
        git \
        wget \
        ca-certificates \
        # plugin deps
        libgtk-3-dev \
        libgdk-pixbuf-2.0-dev \
        libcairo2-dev \
        libglib2.0-dev \
        libx11-dev \
        libgtest-dev \
        # wayfire build deps
        libwayland-dev \
        wayland-protocols \
        # wayland-from-source build deps (noble ships wayland 1.22, but
        # wayfire 0.11's bundled wlroots needs wayland-server >= 1.24)
        libexpat1-dev \
        libffi-dev \
        python3 \
        python3-pip \
        libinput-dev \
        libevdev-dev \
        libxkbcommon-dev \
        libpixman-1-dev \
        libglm-dev \
        libdrm-dev \
        libudev-dev \
        libgbm-dev \
        libseat-dev \
        libdisplay-info-dev \
        hwdata \
        libxml2-dev \
        libpango1.0-dev \
    && rm -rf /var/lib/apt/lists/*

# Install a newer meson (>= 1.4.0) via pip BEFORE the from-source builds below:
# noble ships meson 1.3.2, but xkbcommon 1.13.x and a wayfire subproject require
# meson >= 1.4.0. The pip meson lands in /usr/local/bin (ahead of the apt one in
# PATH), so every from-source build below uses it. (--break-system-packages:
# noble enables PEP 668.)
RUN python3 -m pip install --break-system-packages --upgrade meson

# Build wayland 1.24.0 from source: noble ships wayland 1.22, but wayfire
# 0.11.0's bundled wlroots requires wayland-server >= 1.24.0. Installed to
# /usr so it shadows the distro wayland for the wayfire build below.
RUN set -eux; \
    git clone --depth 1 --branch 1.24.0 \
        https://gitlab.freedesktop.org/wayland/wayland /tmp/wayland; \
    cd /tmp/wayland; \
    meson setup build --prefix=/usr -Ddocumentation=false; \
    ninja -C build; \
    ninja -C build install; \
    rm -rf /tmp/wayland

# Build wayland-protocols 1.47 from source: noble ships wayland-protocols 1.45,
# but wayfire 0.11.0's bundled wlroots needs wayland-protocols >= 1.47.
# (wayland-protocols is a separate project from wayland; the wayland source
# build above does not install it.) Installed to /usr so it shadows the distro
# wayland-protocols for the wayfire build below.
RUN set -eux; \
    git clone --depth 1 --branch 1.47 \
        https://gitlab.freedesktop.org/wayland/wayland-protocols /tmp/wayland-protocols; \
    cd /tmp/wayland-protocols; \
    meson setup build --prefix=/usr; \
    ninja -C build; \
    ninja -C build install; \
    rm -rf /tmp/wayland-protocols

# Build libdrm 2.4.129 from the official signed release tarball: noble ships
# libdrm 2.4.125, but wayfire 0.11.0's bundled wlroots needs libdrm >= 2.4.129.
# The checksum is the published SHA256 for libdrm-2.4.129.tar.xz. Installed to
# /usr so it shadows the distro libdrm for the wayfire build below.
RUN set -eux; \
    wget -q https://dri.freedesktop.org/libdrm/libdrm-2.4.129.tar.xz; \
    echo "5978183f978d697e26a50ba066174964efb0ab97caa1ecaa1b861fbe5ddf77dc  libdrm-2.4.129.tar.xz" | sha256sum -c -; \
    tar xf libdrm-2.4.129.tar.xz; \
    cd libdrm-2.4.129; \
    meson setup build --prefix=/usr; \
    ninja -C build; \
    ninja -C build install; \
    cd /; rm -rf libdrm-2.4.129 libdrm-2.4.129.tar.xz

# Build xkbcommon 1.13.2 from source: noble ships xkbcommon 1.6.0, but wayfire
# 0.11.0's bundled wlroots needs xkbcommon >= 1.8.0. Installed to /usr so it
# shadows the distro xkbcommon for the wayfire build below.
RUN set -eux; \
    git clone --depth 1 --branch xkbcommon-1.13.2 \
        https://github.com/xkbcommon/libxkbcommon /tmp/xkbcommon; \
    cd /tmp/xkbcommon; \
    meson setup build --prefix=/usr \
        -Denable-tools=false -Denable-x11=false \
        -Denable-docs=false -Denable-xkbregistry=false; \
    ninja -C build; \
    ninja -C build install; \
    rm -rf /tmp/xkbcommon

# Build pixman 0.46.4 from source: noble ships pixman 0.42.2, but wayfire
# 0.11.0's bundled wlroots needs pixman >= 0.46.0 (render backend). Installed
# to /usr so it shadows the distro pixman for the wayfire build below.
RUN set -eux; \
    git clone --depth 1 --branch pixman-0.46.4 \
        https://gitlab.freedesktop.org/pixman/pixman /tmp/pixman; \
    cd /tmp/pixman; \
    meson setup build --prefix=/usr -Dtests=disabled; \
    ninja -C build; \
    ninja -C build install; \
    rm -rf /tmp/pixman

# wayfire >= 0.11 is not packaged for ubuntu: build it (and its bundled
# wlroots subproject) from source, pinned to a tag for reproducibility, into
# /usr. This mirrors WayfireWM/wayfire's own CI. The result provides the
# wayfire.pc + headers the plugin compiles against (scene-graph API, >= 0.11).
RUN set -eux; \
    git clone --recursive --depth 1 --branch v0.11.0 \
        https://github.com/WayfireWM/wayfire /tmp/wayfire; \
    cd /tmp/wayfire; \
    meson setup build --prefix=/usr; \
    ninja -C build; \
    ninja -C build install; \
    rm -rf /tmp/wayfire

WORKDIR /app

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
        git \
        ca-certificates \
        # plugin deps
        libgtk-3.0-dev \
        libpangocairo-1.0-dev \
        libgdk-pixbuf-2.0-dev \
        libcairo2-dev \
        libglib2.0-dev \
        libx11-dev \
        libgtest-dev \
        # wayfire build deps
        wayland-dev \
        libwayland-dev \
        wayland-protocols \
        libinput-dev \
        libevdev-dev \
        libxkbcommon-dev \
        libpixman-1-dev \
        libglm-dev \
        libdrm-dev \
        libgbm-dev \
        libseat-dev \
        libdisplay-info-dev \
        hwdata \
        libxml2-dev \
        libpango1.0-dev \
    && rm -rf /var/lib/apt/lists/*

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

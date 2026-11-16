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
        # test deps (meson test)
        imagemagick \
    && rm -rf /var/lib/apt/lists/*

# Build wayfire v0.11.0 (and the deps its bundled wlroots needs: wayland,
# wayland-protocols, libdrm, xkbcommon, pixman) from source into /usr. The
# recipe lives in ONE shared script (scripts/build-wayfire-from-source.sh) so
# a version bump is made in a single place, not copy-pasted here, in the
# ci.yml `native` job, and in e2e/distros/provision.sh. The script also installs
# a pinned meson >= 1.4.0 (noble ships 1.3.2) if the system one is too old.
COPY scripts/build-wayfire-from-source.sh /tmp/build-wayfire-from-source.sh
RUN chmod +x /tmp/build-wayfire-from-source.sh && /tmp/build-wayfire-from-source.sh

WORKDIR /app

# emerald-on-fire (emeraldwf) — build entrypoints.
#
# Same shape as wf-recorder-lite: `make binary` builds the toolchain image
# (Dockerfile) and compiles the plugin in an ephemeral container with the
# project mounted at /app; the built plugin + engines + themer land in ./dist.
#
# PWD via `pwd` so the Docker volume mount works reliably across environments.
PWD   := $(shell pwd)
IMAGE := emeraldwf-dev

.PHONY: build binary test build-local clean

# ==========================================
# TOOLCHAIN IMAGE
# ==========================================
build:
	@echo "Building the wayfire toolchain image (first run builds wayfire from source)..."
	docker build -t $(IMAGE) .

# ==========================================
# PRODUCTION (ephemeral Docker) -> ./dist
# ==========================================
binary: build
	@echo "Compiling the plugin inside Docker..."
	docker run --rm -v $(PWD):/app $(IMAGE) bash -c '\
		git config --global --add safe.directory /app; \
		rm -rf build-docker dist; \
		meson setup build-docker --buildtype=release; \
		ninja -C build-docker; \
		mkdir -p dist/wayfire dist/emeraldwf/engines; \
		cp build-docker/src/libemeraldwf.so dist/wayfire/; \
		cp build-docker/libengine/libemeraldengine.so* dist/emeraldwf/; \
		cp build-docker/engines/lib*.so* dist/emeraldwf/engines/; \
		cp metadata/emeraldwf.xml dist/; \
		cp build-docker/themer/emerald-theme-manager dist/'
	@echo "Artifacts in ./dist:"
	@ls -R dist

# ==========================================
# TESTS (gtest suite inside the toolchain container)
# ==========================================
test: build
	docker run --rm -v $(PWD):/app $(IMAGE) bash -c '\
		git config --global --add safe.directory /app; \
		rm -rf build-docker; \
		meson setup build-docker; \
		ninja -C build-docker; \
		meson test -C build-docker'

# ==========================================
# LOCAL (host needs wayfire>=0.11, gtk3, meson, ninja)
# ==========================================
build-local:
	meson setup build --buildtype=release
	ninja -C build

clean:
	rm -rf build build-docker dist

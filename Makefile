# emerald-on-fire (emeraldwf) — build entrypoints.
#
# Same shape as wf-recorder-lite: `make binary` builds the toolchain image
# (Dockerfile) and compiles the plugin in an ephemeral container with the
# project mounted at /app; the built plugin + engines + themer land in ./dist.
#
# PWD via `pwd` so the Docker volume mount works reliably across environments.
PWD   := $(shell pwd)
IMAGE := emeraldwf-dev

.PHONY: build binary test build-docker build-local clean

# ==========================================
# TOOLCHAIN IMAGE
# ==========================================
build:
	@echo "Building the wayfire toolchain image (first run builds wayfire from source)..."
	docker build -t $(IMAGE) .

# ==========================================
# SINGLE Docker build: compile ONCE (release), run the tests against that same
# build, and package the artifacts into ./dist. Pre-fix CI built the plugin
# TWICE (a release `binary` build AND a separate debug `test` build); now one
# release build serves both the artifact and the test suite (a release build
# runs the gtest suite fine).
# ==========================================
build-docker: build
	@echo "Compiling the plugin inside Docker (one build: tests + artifact)..."
	docker run --rm -v $(PWD):/app $(IMAGE) bash -c '\
		git config --global --add safe.directory /app; \
		rm -rf build-docker dist; \
		meson setup build-docker --buildtype=release --prefix=/usr; \
		ninja -C build-docker; \
		meson test -C build-docker; \
		mkdir -p dist/wayfire dist/emeraldwf/engines; \
		cp build-docker/src/libemeraldwf.so dist/wayfire/; \
		cp build-docker/libengine/libemeraldengine.so* dist/emeraldwf/; \
		cp build-docker/engines/lib*.so* dist/emeraldwf/engines/; \
		cp metadata/emeraldwf.xml dist/; \
		cp build-docker/themer/emerald-theme-manager dist/'
	@echo "Artifacts in ./dist:"
	@ls -R dist

# ==========================================
# PRODUCTION artifact (the single shared release build; also runs the tests)
# ==========================================
binary: build-docker

# ==========================================
# TESTS (run against the same single release build; no second compile)
# ==========================================
test: build-docker

# ==========================================
# LOCAL (host needs wayfire>=0.11, gtk3, meson, ninja)
# ==========================================
build-local:
	meson setup build --buildtype=release
	ninja -C build

clean:
	rm -rf build build-docker dist

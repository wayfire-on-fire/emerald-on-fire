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
		cp build-docker/themer/emerald-theme-manager dist/; \
		# Build manifest: the wayfire version the image was built against
		# (from the shared recipe script, the single source of truth) and the
		# plugin ABI identifier compiled into plugin.hpp. The --docker
		# compatibility guard in install.sh reads this. No apostrophes in
		# this comment: the whole recipe is a single-quoted bash -c string.
		V=$$(grep -oE -- "--branch v[0-9]+\.[0-9]+\.[0-9]+" /app/scripts/build-wayfire-from-source.sh | head -1 | sed "s/^--branch v//"); \
		A=$$(grep -oE "WAYFIRE_API_ABI_VERSION_MACRO[[:space:]].*" /usr/include/wayfire/plugin.hpp 2>/dev/null | head -1 | sed "s/[^0-9]//g" || true); \
		# The manifest must be factually correct: the ACTUAL wayfire in the
		# image must match the pinned version in the recipe (a stale image
		# cache or a recipe drift would otherwise produce a
		# consistent-but-wrong manifest).
		test -n "$$V" && test "$$(pkg-config --modversion wayfire 2>/dev/null)" = "$$V" \
			|| { echo "FATAL: image wayfire version ($$(pkg-config --modversion wayfire 2>/dev/null)) does not match the recipe ($$V)" >&2; exit 1; }; \
		{ echo "wayfire_version=$$V"; echo "abi_identifier=$$A"; echo "built_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ)"; } > dist/.emeraldwf-build-info'
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

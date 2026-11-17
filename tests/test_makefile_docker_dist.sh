#!/usr/bin/env bash
# test_makefile_docker_dist.sh — the build-docker recipe must pack dist/ as a
# true replica of the build tree: versioned .so files PLUS their symlinks
# (plain `cp` dereferences symlinks and turns each engine into 3 identical
# regular files; it also matches the meson .p object dirs and warns
# "cp: -r not specified"). A dereferenced dist breaks install.sh --docker
# staging (engine count check) and bloats the artifact 3x, so pin the
# contract here: extract the dist-copy commands from the expanded recipe and
# run them against a fake build tree, then verify the layout.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)/.."
MAKEFILE="$ROOT/Makefile"
[ -f "$MAKEFILE" ] || { echo "FAIL: Makefile not found"; exit 1; }

expanded="$(make -n -C "$ROOT" build-docker 2>&1)"
status=$?
if [ "$status" -ne 0 ]; then
    echo "FAIL: make -n build-docker failed: $expanded"
    exit 1
fi

# The dist-copy segment, as PHYSICAL recipe lines (backslash+newline kept):
# make passes the logical recipe line to sh with physical newlines intact,
# and the inner bash -c string sees each physical line (a '#' comment ends
# at its own newline; a backslash+newline outside quotes is a continuation).
# Running the awk-joined single-line form here would be wrong: the inner
# bash comment would swallow the rest of the line.
# (the last extracted line keeps its continuation backslash in the real
# recipe, where it flows into the manifest comment; drop it here)
seg="$(printf '%s\n' "$expanded" | sed -n '/^[[:space:]]*mkdir -p dist\/wayfire dist\/emeraldwf\/engines;/,/^[[:space:]]*# Build manifest/p' | sed '$d' | sed '$ s/\\$//')"
if [ -z "$seg" ]; then
    echo "FAIL: could not extract the dist-copy commands from the build-docker recipe"
    echo "       (the 'mkdir -p dist/...' .. '# Build manifest' anchors are missing)"
    exit 1
fi

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

# Fake build tree: 7 engines (versioned .so + 2 symlinks + meson .p dir),
# the engine lib, the plugin, the themer, the metadata.
sb="$T/sandbox"
mkdir -p "$sb/build-docker/src" "$sb/build-docker/libengine" \
         "$sb/build-docker/engines" "$sb/build-docker/themer" "$sb/metadata"
printf 'wf' > "$sb/build-docker/src/libemeraldwf.so"
for e in legacy line oxygen pixmap vrunner zootreeves truglass; do
    printf 'e' > "$sb/build-docker/engines/lib$e.so.0.1.0"
    ln -s "lib$e.so.0.1.0" "$sb/build-docker/engines/lib$e.so.0"
    ln -s "lib$e.so.0" "$sb/build-docker/engines/lib$e.so"
    mkdir "$sb/build-docker/engines/lib$e.so.0.1.0.p"
done
printf 'eng' > "$sb/build-docker/libengine/libemeraldengine.so.0.1.0"
ln -s libemeraldengine.so.0.1.0 "$sb/build-docker/libengine/libemeraldengine.so.0"
ln -s libemeraldengine.so.0 "$sb/build-docker/libengine/libemeraldengine.so"
mkdir "$sb/build-docker/libengine/libemeraldengine.so.0.1.0.p"
printf 'them' > "$sb/build-docker/themer/emerald-theme-manager"
printf 'meta' > "$sb/metadata/emeraldwf.xml"

( cd "$sb" && bash -c "$seg" ) > "$T/copy.log" 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: the dist-copy commands did not run cleanly:"
    cat "$T/copy.log"
    exit 1
fi

fail=0
check() {  # description test-command...
    local desc="$1"; shift
    if "$@"; then
        echo "ok: $desc"
    else
        echo "FAIL: $desc"; fail=1
    fi
}

cd "$sb"
check "dist/wayfire/libemeraldwf.so is a regular file" \
    test -f dist/wayfire/libemeraldwf.so
check "dist/emeraldwf.xml present" test -f dist/emeraldwf.xml
check "dist/emerald-theme-manager present" test -f dist/emerald-theme-manager
check "dist/emeraldwf/libemeraldengine.so.0.1.0 is a regular file" \
    test -f dist/emeraldwf/libemeraldengine.so.0.1.0
check "dist/emeraldwf/libemeraldengine.so.0 is a SYMLINK" \
    test -L dist/emeraldwf/libemeraldengine.so.0
check "dist/emeraldwf/libemeraldengine.so is a SYMLINK" \
    test -L dist/emeraldwf/libemeraldengine.so
check "engine .p object dir NOT copied" \
    test ! -e dist/emeraldwf/engines/libline.so.0.1.0.p
check "libengine .p object dir NOT copied" \
    test ! -e dist/emeraldwf/libemeraldengine.so.0.1.0.p

for e in legacy line oxygen pixmap vrunner zootreeves truglass; do
    check "engine $e: versioned .so is a regular file" \
        test -f "dist/emeraldwf/engines/lib$e.so.0.1.0"
    check "engine $e: .so.0 is a SYMLINK" \
        test -L "dist/emeraldwf/engines/lib$e.so.0"
    check "engine $e: .so is a SYMLINK" \
        test -L "dist/emeraldwf/engines/lib$e.so"
done

# the copy must be silent (no "cp: -r not specified" directory warnings)
if grep -q 'omitting directory' "$T/copy.log"; then
    echo "FAIL: the dist-copy emitted directory warnings:"
    cat "$T/copy.log"
    fail=1
else
    echo "ok: dist-copy is silent (no .p dir warnings)"
fi

exit "$fail"

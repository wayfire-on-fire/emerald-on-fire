# Contributing to emerald-on-fire (`emeraldwf`)

Thanks for helping improve **Emerald On Fire** — emerald themes & engines for
wayfire.

## Build & test

The fastest way to build and test with **zero host dependencies** is Docker:

```sh
make binary   # build the toolchain image + the plugin (-> dist/)
make test     # run the gtest suite in the toolchain container
```

Native build (host needs wayfire ≥ 0.11, gtk3, meson, ninja):

```sh
make build-local
meson test -C build
```

See the [README](README.md#build) for the full build matrix.

## Code style

The project matches wayfire's bar: **C++17**, compiled with
`-Wall -Wextra -Wshadow`, and formatted with **uncrustify** using wayfire's
[uncrustify.ini](https://github.com/WayfireWM/wayfire/blob/master/uncrustify.ini)
(enforced in CI). Format your changes before pushing:

```sh
wget -q https://raw.githubusercontent.com/WayfireWM/wayfire/master/uncrustify.ini
git ls-files | grep -vE '^third_party/' | grep -E '\.(cpp|hpp)$' \
  | xargs uncrustify -c uncrustify.ini --no-backup --replace
```

## The vendored emerald

`third_party/emerald/` contains emerald's sources, **vendored unmodified**
(GPLv2 — see `third_party/emerald/COPYING`). Please keep it pristine: make your
changes in the project's own code (`src/`, `compat/`, `engines/`, …), **not** in
the vendored tree. The pinned upstream rev is recorded in
`third_party/emerald/VENDOR_SHA`.

## Pull requests

- Keep the diff focused — one logical change per PR.
- Make sure CI is green (docker build, native build, tests, code style).
- Update the README if user-visible behavior or options change.
- Don't bump the wayfire dependency or the vendored emerald rev without saying
  why in the description.

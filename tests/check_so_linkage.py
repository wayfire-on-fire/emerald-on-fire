#!/usr/bin/env python3
"""Regression test: the plugin .so must reference wlroots (a C library)
symbols with C linkage.

A bare `#include <wlr/...>` from C++ (without an extern "C" wrapper) mangles
the symbol — e.g. wlr_xcursor_get_resize_name(wlr_edges) becomes
_Z27wlr_xcursor_get_resize_name9wlr_edges. wayfire's dlopen() of the plugin
then cannot resolve it against libwlroots, so the plugin is silently never
loaded and nothing is painted at all (part 06 integration defect).

Check: no C++-mangled (Itanium `_Z*`) dynamic symbol of the plugin may
encode a wlroots C function name.
"""
import re
import subprocess
import sys


def main(so_path: str) -> int:
    out = subprocess.run(["nm", "-D", so_path],
                         capture_output=True, text=True)
    if out.returncode != 0:
        print(f"FAIL: nm failed on {so_path}: {out.stderr.strip()}")
        return 1

    bad = []
    for line in out.stdout.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 2:
            continue
        name = parts[-1]
        # A bare C function mangles as _Z<total-len><funcname>... — so a
        # wlroots C function mangled as C++ is _Z<digits>wlr_.... C++ *methods*
        # that merely take a wlr_* struct argument start with _ZN/_ZSt/_ZThn
        # and are fine.
        if re.match(r"_Z\d+wlr_", name):
            bad.append(name)

    if bad:
        print(f"FAIL: C++-mangled wlroots symbols in {so_path} — "
              "the .so will not dlopen inside wayfire:")
        for s in bad:
            print("   ", s)
        print('Fix: include wlroots headers via <wayfire/nonstd/wlroots*.hpp>'
              ' or wrap a bare include in extern "C" { ... }.')
        return 1

    print(f"OK: no mangled wlroots symbols in {so_path}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <plugin.so>")
        sys.exit(2)
    sys.exit(main(sys.argv[1]))

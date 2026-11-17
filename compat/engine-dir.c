// engine-dir.c — emeraldwf_engine_dir(): the engine directory, resolved at
// runtime from libemeraldengine.so's OWN location:
//   installed:  <libdir>/emeraldwf/libemeraldengine.so -> <libdir>/emeraldwf/engines
//   build tree: build/libengine/libemeraldengine.so    -> build/engines
//
// The compile-time EMERALDWF_ENGINE_DIR_BAKED (prefix/libdir/emeraldwf/
// engines) is a FALLBACK only. In a docker build it is the toolchain
// IMAGE's libdir (Ubuntu: /usr/lib/x86_64-linux-gnu) — wrong on hosts with
// a different libdir (Gentoo /usr/lib64, Arch /usr/lib): the theme
// editor's engine scan (themer.c: engine_scan_dir(ENGINE_DIR)) would open
// an empty directory and list zero installed engines.
//
// The plugin's engine resolution already uses this pattern (dladdr in
// emerald-settings.cpp); this brings the theme editor's scan to the same
// location-independent contract.
#define _GNU_SOURCE // dladdr/Dl_info are GNU extensions
#include <dlfcn.h>
#include <libgen.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

const char *emeraldwf_engine_dir(void)
{
    static char dir[4096];
    Dl_info info;
    if (dladdr((void *)emeraldwf_engine_dir, &info) && (info.dli_fname != NULL))
    {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s", info.dli_fname);
        const char *so_dir = dirname(buf);
        // installed layout: <libdir>/emeraldwf/engines (next to this .so)
        snprintf(dir, sizeof(dir), "%s/engines", so_dir);
        if (access(dir, R_OK) == 0)
        {
            return dir;
        }
        // build tree: build/libengine/.. = build/engines
        snprintf(dir, sizeof(dir), "%s/../engines", so_dir);
        if (access(dir, R_OK) == 0)
        {
            return dir;
        }
    }
    return EMERALDWF_ENGINE_DIR_BAKED;
}

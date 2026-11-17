// engine-dir.h — prototype for the runtime engine-dir resolver
// (compat/engine-dir.c, linked into libemeraldengine).
//
// themer.c calls engine_scan_dir(ENGINE_DIR); with
// -DENGINE_DIR=emeraldwf_engine_dir() the vendored file needs the
// prototype, else the call compiles as an implicit `int` declaration.
// Force-included via -include in libengine/meson.build (same pattern as
// themer-version-stub.h) so the vendored source stays unmodified.
#ifndef EMERALDWF_ENGINE_DIR_H
#define EMERALDWF_ENGINE_DIR_H

const char *emeraldwf_engine_dir(void);

#endif

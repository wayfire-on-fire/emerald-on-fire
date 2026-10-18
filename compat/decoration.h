/* compat/decoration.h
 *
 * Shim replacing GNOME's libdecoration header for the emeraldwf build.
 * Part 02 ships this (spec 02: "defines decor_extents_t only"); Part 03
 * adds the quad/property constants below because emerald's own
 * main.c/my_set_window_quads() references them unconditionally, and the
 * layout tests build from the same include tree. Keep it minimal —
 * only what emerald's headers and the vendored code need.
 */
#ifndef EMERALDWF_COMPAT_DECORATION_H
#define EMERALDWF_COMPAT_DECORATION_H

typedef struct {
    int left, right, top, bottom;
} decor_extents_t;

/* Quad/property constants used by emerald's decoration-quad code. */
#define N_QUADS_MAX 12
#define BASE_PROP_SIZE 12
#define QUAD_PROP_SIZE 9
#define PROP_HEADER_SIZE 4

/* Quad anchor/align flags (values must match libdecoration; the numerics
   below mirror the upstream enum ordering). */
#define GRAVITY_NORTH 0
#define GRAVITY_SOUTH 1
#define GRAVITY_WEST 2
#define GRAVITY_EAST 3

#define ALIGN_LEFT 0
#define ALIGN_RIGHT 1
#define ALIGN_TOP 2
#define ALIGN_BOTTOM 3

#define STRETCH_X 1
#define STRETCH_Y 2

#endif /* EMERALDWF_COMPAT_DECORATION_H */

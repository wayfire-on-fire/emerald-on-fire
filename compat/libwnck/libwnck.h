/* compat/libwnck/libwnck.h
 *
 * Shim replacing the libwnck-3.0 dev headers for the emeraldwf build.
 * emeraldwf no longer depends on libwnck: the vendored emerald headers
 * (third_party/emerald/include/emerald.h) do `#include
 * <libwnck/libwnck.h>` and use exactly three of its types —
 * WnckWindow (opaque pointer in the event_callback typedef),
 * WnckWindowState and WnckWindowActions (the _decor state/action
 * bitmask fields). This header provides just those, resolved via
 * `-I compat` ahead of any system include path.
 *
 * The enum values are a VERBATIM copy of libwnck 3.0's window.h (the
 * headers this build used to compile against), so every bitmask the
 * bridge writes (to_wnck_state in src/emerald-engine.cpp maps into the
 * named constants below) and every bitmask the vendored engines read
 * keeps the exact same bit positions — no engine or bridge change.
 *
 * NOTE: these are the installed libwnck 3.0 values, NOT the D4 values
 * pinned in specs/00-overview.md for the plugin's own EWS_* and EWA_*
 * enums — the two sets differ, and the bridge performs an explicit
 * mapping between them (see src/emerald-types.hpp + compat/README).
 */
#ifndef EMERALDWF_COMPAT_LIBWNCK_H
#define EMERALDWF_COMPAT_LIBWNCK_H

#include <X11/Xlib.h> /* XEvent: emerald.h's event_callback takes XEvent* */

/* Opaque window handle; emerald uses it only as a pointer. */
typedef struct _WnckWindow WnckWindow;

/* libwnck 3.0 window.h, verbatim. */
typedef enum
{
  WNCK_WINDOW_STATE_MINIMIZED              = 1 << 0,
  WNCK_WINDOW_STATE_MAXIMIZED_HORIZONTALLY = 1 << 1,
  WNCK_WINDOW_STATE_MAXIMIZED_VERTICALLY   = 1 << 2,
  WNCK_WINDOW_STATE_SHADED                 = 1 << 3,
  WNCK_WINDOW_STATE_SKIP_PAGER             = 1 << 4,
  WNCK_WINDOW_STATE_SKIP_TASKLIST          = 1 << 5,
  WNCK_WINDOW_STATE_STICKY                 = 1 << 6,
  WNCK_WINDOW_STATE_HIDDEN                 = 1 << 7,
  WNCK_WINDOW_STATE_FULLSCREEN             = 1 << 8,
  WNCK_WINDOW_STATE_DEMANDS_ATTENTION      = 1 << 9,
  WNCK_WINDOW_STATE_URGENT                 = 1 << 10,
  WNCK_WINDOW_STATE_ABOVE                  = 1 << 11,
  WNCK_WINDOW_STATE_BELOW                  = 1 << 12
} WnckWindowState;

/* libwnck 3.0 window.h, verbatim. */
typedef enum
{
  WNCK_WINDOW_ACTION_MOVE                    = 1 << 0,
  WNCK_WINDOW_ACTION_RESIZE                  = 1 << 1,
  WNCK_WINDOW_ACTION_SHADE                   = 1 << 2,
  WNCK_WINDOW_ACTION_STICK                   = 1 << 3,
  WNCK_WINDOW_ACTION_MAXIMIZE_HORIZONTALLY   = 1 << 4,
  WNCK_WINDOW_ACTION_MAXIMIZE_VERTICALLY     = 1 << 5,
  WNCK_WINDOW_ACTION_CHANGE_WORKSPACE        = 1 << 6,
  WNCK_WINDOW_ACTION_CLOSE                   = 1 << 7,
  WNCK_WINDOW_ACTION_UNMAXIMIZE_HORIZONTALLY = 1 << 8,
  WNCK_WINDOW_ACTION_UNMAXIMIZE_VERTICALLY   = 1 << 9,
  WNCK_WINDOW_ACTION_UNSHADE                 = 1 << 10,
  WNCK_WINDOW_ACTION_UNSTICK                 = 1 << 11,
  WNCK_WINDOW_ACTION_MINIMIZE                = 1 << 12,
  WNCK_WINDOW_ACTION_UNMINIMIZE              = 1 << 13,
  WNCK_WINDOW_ACTION_MAXIMIZE                = 1 << 14,
  WNCK_WINDOW_ACTION_UNMAXIMIZE              = 1 << 15,
  WNCK_WINDOW_ACTION_FULLSCREEN              = 1 << 16,
  WNCK_WINDOW_ACTION_ABOVE                   = 1 << 17,
  WNCK_WINDOW_ACTION_BELOW                   = 1 << 18
} WnckWindowActions;

#endif /* EMERALDWF_COMPAT_LIBWNCK_H */

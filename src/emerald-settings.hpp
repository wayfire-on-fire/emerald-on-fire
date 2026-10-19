// emerald-settings.hpp — ThemeSettings (overview §5.3, owned by part 02).
//
// STUB: Part 02 ships the full implementation (settings.ini + theme.ini
// loaders, engine sections, button PNGs) in emerald-settings.cpp. The shape
// below is the frozen seam that Parts 01/03/04 compile against.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <emerald.h>

#include "emerald-types.hpp"

namespace wf
{
namespace emeraldwf
{
class ThemeSettings
{
  public:
    // Part 02: full implementation (settings.ini + theme.ini).
    ThemeSettings();
    ~ThemeSettings();

    // Move support: transfers ownership of ws_/fs_act_/fs_inact_ and
    // nulls the source so its destructor is a no-op.
    ThemeSettings(ThemeSettings && other) noexcept;
    ThemeSettings& operator =(ThemeSettings && other) noexcept;

    // Non-copyable (raw ownership of emerald structs).
    ThemeSettings(const ThemeSettings &) = delete;
    ThemeSettings& operator =(const ThemeSettings&) = delete;

    // Engine override semantics (Part 07, emerald-theme-manager):
    // - engine_override empty            → the theme's own [engine]
    // (theme.ini); themes are self-contained packages.
    // - engine_override == theme's engine → identical to the empty case.
    // - engine_override != theme's engine → the override engine runs with
    // emerald's DEFAULT theme (its stock look): a theme's colors/
    // geometry/[<engine>_settings] are authored for its own engine, so
    // applying them to a foreign engine is meaningless. (Bare engine
    // struct defaults are zero-initialized = transparent, so the
    // defaults theme's per-engine sections are what "engine stock
    // look" means — same as emerald with no theme installed.)
    // settings.ini (engine-independent user settings) still applies.
    bool load(const std::string & settings_ini, const std::string & theme_dir,
        const std::string & engine_override = {});

    window_settings *ws() const;
    frame_settings *fs_active() const;
    frame_settings *fs_inactive() const;
    const std::string& engine() const;
    const std::string& theme_dir() const;
    bool valid() const;

  private:
    window_settings *ws_ = nullptr;
    frame_settings *fs_act_ = nullptr, *fs_inact_ = nullptr;
    std::string engine_ = "legacy";
    bool valid_ = false;

    // paths in effect for this load (used by copy-from-defaults fallback)
    std::string settings_path_;
    std::string theme_dir_;
};

// Port of emerald main.c:250-262 (get_b_t_offset): maps the B_T_*
// titlebar-object index into the B_* button/surface index (the index space
// of ws->button_surface / ws->button_glow_surface). Bistate pairs are
// interleaved in B_* (MAXIMIZE/RESTORE, SHADE/UNSHADE, ...), so a direct
// B_T→B_* lookup is required before indexing the surface arrays; the +1
// bistate step in the draw path then selects the twin surface.
int get_b_t_offset(gint b_t);

// Finding 42: re-point the DecorContext's frame_settings pointer at a (new)
// theme. emerald_decorator_t::set_theme() must call this on EVERY theme
// swap: the node's render() refreshes ctx.fs per frame, but pointer events
// (hover/click → layout.hit_button / resize_edges / middle_click_action)
// read ctx.fs BETWEEN renders. After reload_theme() the previous
// ThemeSettings is destroyed — a stale ctx.fs is a use-after-free, and the
// first hover after any live reload segfaulted wayfire (whole session).
inline void decor_ctx_set_theme(DecorContext & ctx, const ThemeSettings & ts,
    bool active)
{
    ctx.fs = active ? ts.fs_active() : ts.fs_inactive();
}

// Cairo-only port of emerald's XRender shadow texture generation (original
// main.c:5280-5412; see emerald-settings.cpp for the blur approximation).
// (Re)generates ws->large_shadow_surface + ws->shadow_pattern from the
// shadow settings already loaded; call after update_shadow(). The silhouette
// is the engine-rendered frame's alpha (rounded window corners, border
// width, semi-transparency), rendered with the shadow background suppressed
// so the alpha is the window alone; it is box-blurred with the original's
// kernel width and then shifted by (shadow_offset_x, shadow_offset_y) —
// the drop-shadow direction (no shadow on top/left once the offset
// reaches the blur size).
// Requires the theme's engine loaded (ThemeSettings::load does that before
// calling); falls back to the original's sharp rectangle if the reference
// render fails. Destroys any previous textures; leaves both NULL for
// shadow-free themes.
void create_large_shadow_surface(ThemeSettings & ts);

// Port of emerald's update_shadow (original main.c:5183-5275) minus the
// XRender parts: (re)computes the derived *_space fields (frame margins,
// shadow spread per side, corner spaces) from shadow_radius/offsets +
// win_extents. The shadow spread is asymmetric per side — the offsets
// implement the drop-shadow direction (shadow falls toward +x/+y).
void update_shadow(frame_settings *fs);

// Part 06: map the emeraldwf/double_click_action and
// emeraldwf/middle_click_action option strings to emerald's titlebar
// action enum (titlebar.h DOUBLE_CLICK_*: 0 shade, 1 maximize, 2
// minimize, 3 send_to_back, 4 close, 5 none). Accepted names are
// case-insensitive. An EMPTY string and unknown names yield nullopt —
// the caller falls back to emerald's own default (DOUBLE_CLICK_SHADE /
// DOUBLE_CLICK_SEND_TO_BACK), so a typo in the wayfire config never
// breaks the click behavior.
std::optional<int> parse_titlebar_action(const std::string & name);

// Resolve the effective titlebar action from the emeraldwf option value:
// a parseable value is used; anything else (empty/typo) yields `base`,
// which the call sites set to emerald's own default (the WCM option
// DEFINES the click action — it is a global emerald setting, not an
// override over settings.ini).
int resolve_titlebar_action(const std::string & override, int base);

// Part 08: WCM owns the button fade/pulse settings (moved off the theme
// manager's Emerald Settings tab — one setting, one editor). Convert the
// WCM option values (durations in ms, opacity in percent) into emerald's
// ws step fields, exactly like emerald's own loader (main.c:5753-5779):
// num_steps = total/step (min 1), pulse_len = num*(100-min_opacity)/100,
// pulse_wait = wait/step. pulse_len == 0 (and the wait zeroed) is the
// "pulse off" marker the consumers use.
void emeraldwf_apply_button_fade(window_settings *ws,
    bool use_fade, bool use_pulse,
    int total_duration_ms, int step_duration_ms,
    int pulse_wait_ms, int pulse_min_opacity_pct);
} // namespace emeraldwf
} // namespace wf

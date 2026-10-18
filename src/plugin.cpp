// plugin.cpp — emeraldwf plugin skeleton (Part 01).
//
// Registers the plugin, declares all options (overview D9), wires the
// signals later parts depend on, creates/removes emerald_decorator_t per
// toplevel, and negotiates frame margins. No visual output yet — the scene
// node lands in Part 04.
#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/bindings.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <map>

#include <sys/stat.h>

#include <glib.h>

#include "emerald-config-sync.hpp"
#include "emerald-decorator.hpp"
#include "emerald-file-stamp.hpp"
#include "emerald-settings.hpp"
#include "emerald-shade.hpp"
#include "emerald-shadow-margins.hpp"

// Part 07: WCM's metadata parser segfaults on an empty <value> element
// (regression: Metadata.NoEmptyValueElements), so the "- No Override -"
// entry of the Engine Override combo carries the marker "no_override".
// Map it to an empty override (the theme's own engine).
static std::string engine_override_value(const std::string &v)
{
    return v == "no_override" ? std::string() : v;
}

// ── Part 06: wayfire config watching (WCM engine switch) ───────────────────
//
// WCM (wayfire's config manager) only WRITES the ini — the installed wayfire
// never re-reads its own config (config auto-reload landed in a newer source
// tree, not in the running binary), so the in-memory option values — and the
// plugin's set_callback() handlers — are never triggered by a WCM edit.
// To make emeraldwf/* changes live, the mtime poller also watches the active
// wayfire config file and syncs EVERY emeraldwf option (finding 24 — the
// original sync only covered the string options, so checkbox options like
// enable_shade never reached the plugin) into wayfire's config manager on
// change (set_value() fires the option callbacks, which arm the theme reload
// and apply the per-option side effects). The sync lives in
// emerald-config-sync.cpp (pure, headless-testable).
//
// The active config file is the one the running wayfire was launched with
// (`wayfire -c <file>`; default $XDG_CONFIG_HOME/wayfire.ini). The plugin
// runs INSIDE the wayfire process, so /proc/self/cmdline gives it directly.
namespace
{
std::string find_wayfire_config_file()
{
    std::ifstream cf("/proc/self/cmdline", std::ios::binary);
    if (cf)
    {
        std::string arg, prev;
        while (std::getline(cf, arg, '\0'))
        {
            if (prev == "-c" && !arg.empty())
            {
                return arg;
            }
            prev = arg;
        }
    }
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base =
        (xdg != nullptr && *xdg != '\0') ? std::string(xdg) :
        std::string(g_get_home_dir()) + "/.config";
    return base + "/wayfire.ini";
}

} // namespace

namespace wf
{
namespace emeraldwf
{

class emeraldwf_plugin : public wf::plugin_interface_t
{
  public:
    emeraldwf_plugin();
    void init() override;
    void fini() override;

  private:
    // ── Options (overview D9) ──────────────────────────────────────────────
    wf::option_wrapper_t<std::string> engine{"emeraldwf/engine"};
    wf::option_wrapper_t<std::string> theme_dir{"emeraldwf/theme_dir"};
    wf::option_wrapper_t<bool> maximized_borders{"emeraldwf/maximized_borders"};
    wf::option_wrapper_t<bool> maximized_shadows{"emeraldwf/maximized_shadows"};
    wf::option_wrapper_t<std::string> ignore_views_string{"emeraldwf/ignore_views"};
    wf::option_wrapper_t<std::string> always_decorate_string{"emeraldwf/always_decorate"};
    wf::option_wrapper_t<bool> enable_shade{"emeraldwf/enable_shade"};
    wf::option_wrapper_t<wf::keybinding_t> shade_modifier{"emeraldwf/shade_modifier"};
    wf::option_wrapper_t<int> csd_titlebar_height{"emeraldwf/csd_titlebar_height"};
    wf::option_wrapper_t<wf::animation_description_t> shade_duration{"emeraldwf/shade_duration"};
    // Part 05 (D12): non-zero shadow pad for CSD views (documented in 06).
    wf::option_wrapper_t<int> shadow_pad{"emeraldwf/shadow_pad"};
    // Part 06 (product): titlebar click actions configurable from WCM, so
    // basic behavior needs no ~/.emerald/settings.ini edit. Empty (the
    // default) = keep the emerald settings.ini value; names are parsed by
    // parse_titlebar_action() (emerald-settings.cpp).
    wf::option_wrapper_t<std::string> double_click_action{"emeraldwf/double_click_action"};
    wf::option_wrapper_t<std::string> middle_click_action{"emeraldwf/middle_click_action"};
    // Part 08: the remaining global emerald settings, moved off the theme
    // manager's Emerald Settings tab (one setting, one editor). Applied to
    // the theme's window_settings by apply_emerald_global_options().
    wf::option_wrapper_t<bool> no_scroll_shade{"emeraldwf/no_scroll_shade"};
    wf::option_wrapper_t<bool> use_decoration_cropping{"emeraldwf/use_decoration_cropping"};
    wf::option_wrapper_t<bool> use_button_fade{"emeraldwf/use_button_fade"};
    wf::option_wrapper_t<bool> use_button_fade_pulse{"emeraldwf/use_button_fade_pulse"};
    wf::option_wrapper_t<int> button_fade_total_duration{"emeraldwf/button_fade_total_duration"};
    wf::option_wrapper_t<int> button_fade_step_duration{"emeraldwf/button_fade_step_duration"};
    wf::option_wrapper_t<int> button_fade_pulse_wait_duration{"emeraldwf/button_fade_pulse_wait_duration"};
    wf::option_wrapper_t<int> button_fade_pulse_min_opacity{"emeraldwf/button_fade_pulse_min_opacity"};

    // Part 05: IPC activator (wcm bindable, spec 05 §2).
    wf::ipc_activator_t emeraldwf_shade_toggle{"emeraldwf/shade_toggle"};

    // Part 06: live settings reload (spec 06). A repeating timer watches the
    // mtimes of ~/.emerald/settings.ini, <theme_dir>/theme.ini, and the theme
    // dir itself (so engine PNG/dir changes are picked up). Changes are
    // debounced (250 ms), then ThemeSettings is rebuilt, the engine swapped,
    // and every decorator re-renders. This replaces emerald's SIGUSR1/dbus
    // reload, so emerald-theme-manager works with no restart and no signal.
    //
    // NOTE: these use wf::wl_timer (not GLib g_timeout and not g_file_monitor).
    // g_timeout sources are added to the default GLib main context, which
    // wayfire NEVER iterates — so the timers would never fire (live reload was
    // dead). g_file_monitor's inotify teardown (g_file_monitor_cancel /
    // g_object_unref on unload) crashed wayfire on plugin fini. wf::wl_timer
    // integrates with the Wayland event loop, fires on the main thread, and is
    // safe to disconnect on fini.
    wf::wl_timer<true>  poll_timer;   // 500 ms mtime poller (repeatable)
    wf::wl_timer<false> reload_timer; // 250 ms reload debounce (one-shot)
    std::map<std::string, std::uint64_t> last_stamp; // file stamps (ns; finding 44)
    std::string wayfire_config_file;          // active wayfire ini (poller-watched)

    wf::axis_callback shade_axis_cb;

    wf::view_matcher_t ignore_views{"emeraldwf/ignore_views"};
    wf::view_matcher_t always_decorate{"emeraldwf/always_decorate"};

    // ── Theme (part 02 provides the loader; here: store + log) ─────────────
    std::shared_ptr<const ThemeSettings> theme;

    // ── Signals ───────────────────────────────────────────────────────────
    wf::signal::connection_t<wf::txn::new_transaction_signal> on_new_tx;
    wf::signal::connection_t<wf::view_decoration_state_updated_signal> on_decoration_state_changed;
    wf::signal::connection_t<wf::view_tiled_signal> on_view_tiled;
    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id_changed;
    wf::signal::connection_t<wf::view_title_changed_signal> on_title_changed;
    // The toplevel's wm geometry is populated AFTER the mapping transaction
    // (adjust_new_decorations runs while pending.geometry is still 0×0).
    // Without this, the node renders a zero-client frame and never updates.
    // See the regression test in tests/test_render_node.cpp.
    wf::signal::connection_t<wf::view_geometry_changed_signal> on_geometry_changed;

    // ── Helpers ───────────────────────────────────────────────────────────
    bool should_decorate_view(::wayfire_toplevel_view view);
    void adjust_new_decorations(::wayfire_toplevel_view view);
    void remove_decoration(::wayfire_toplevel_view view);
    void update_view_decoration(wayfire_view view);
    void recreate();

    // Part 05 (spec 05 §2): shade triggers.
    void toggle_shade(wayfire_view view);
    // part 06 finding 18: direction-aware scroll shade (wheel up = shade,
    // wheel down = unshade) for both SSD (decorator) and CSD (direct
    // transformer) views — the <ctrl>+<super>+wheel binding entry point.
    void scroll_shade(wayfire_view view, double delta);
    void export_csd_shadow_margins(wayfire_view view);
    void publish_shade_duration(wayfire_view view);

    // Part 06 (spec 06): live settings reload (mtime polling).
    void emerald_watched_paths(std::vector<std::string> &out);
    void start_poller();
    void stop_poller();
    // wf::wl_timer<true> callback. Polls the watched files' mtimes; on change,
    // arms the 250 ms debounce. Returns true to keep the timer repeating.
    bool poll_tick();
    // wf::wl_timer<false> callback. Runs the debounced reload once.
    void reload_debounced();
    void reload_theme();   // rebuild ThemeSettings, swap engine, re-render all
    // Part 06 (product): apply the WCM titlebar click-action options to a
    // freshly loaded theme. The option DEFINES the action (a global emerald
    // setting — WCM is its editor, not an override over settings.ini; the
    // settings.ini keys are no longer consulted for click actions).
    // Unparseable value → emerald's own default.
    void apply_titlebar_action_options(const ThemeSettings &ts);
    // Part 08: apply the WCM-owned global emerald settings (no_scroll_shade,
    // use_decoration_cropping, the button fade/pulse set) to a freshly
    // loaded theme's window_settings. Called after every theme load so the
    // WCM values always stand (the loader no longer reads these keys from
    // settings.ini).
    void apply_emerald_global_options(const ThemeSettings &ts);

    // Last in-effect config (used by the reload path, mirrors init()).
    std::string last_settings_path;
    std::string last_theme_dir;

  public:
    // Called from option callbacks and signal handlers.
    // Part 06: also called from the mtime poller.
    void arm_reload();

  private:
    // (declared above under the Part-06 helpers)
};

// Expand a leading "~" / "~user" to the home directory. Wayfire does NOT do
// this for option values, so `theme_dir = "~/.emerald/theme"` would otherwise
// be treated as a literal path (a dir named "~" under $PWD) and the theme
// would never be found.
std::string expand_home(const std::string &path)
{
    if (path.empty())
        return path;
    // Only expand a leading "~" (avoids touching absolute/relative paths).
    // Handles "~" and "~/..." (the common cases). "~user" is left as-is —
    // it is not a valid theme_dir in practice.
    if (path[0] == '~')
    {
        std::string home = g_get_home_dir();
        if (home.empty())
            return path;
        if (path.size() == 1)
            return home;                       // "~"
        if (path[1] == '/')
            return home + path.substr(1);      // "~/..."
    }
    return path;
}

// ── Decoration decision (D6) ───────────────────────────────────────────────
bool emeraldwf_plugin::should_decorate_view(::wayfire_toplevel_view view)
{
    if (view == nullptr)
    {
        return false;
    }
    return (view->should_be_decorated() && !ignore_views.matches(view)) ||
           always_decorate.matches(view);
}

// ── Create decorator + set margins (pixdecor decoration.cpp adjust_new_decorations) ──
void emeraldwf_plugin::adjust_new_decorations(::wayfire_toplevel_view view)
{
    auto toplevel = view->toplevel();

    if (!toplevel->get_data<emerald_decorator_t>())
    {
        toplevel->store_data(
            std::make_unique<emerald_decorator_t>(view, theme));
    }

    auto deco     = toplevel->get_data<emerald_decorator_t>();

    // Attach the scene node FIRST. get_margins() derives from the live
    // toplevel state (client size, titlebar, tiled edges); on a freshly
    // mapping toplevel the pending geometry is not yet populated, so
    // computing margins before attach_node() yields {0,0,0,0} and the node
    // renders a zero-size frame. attach_node() also re-derives the bounding
    // box from get_margins(), so the correct order is: node, then margins.
    // contract: attach_node() is implemented by part 04
    deco->attach_node();

    auto &pending = toplevel->pending();
    pending.margins = deco->get_margins(pending);

    if (!pending.fullscreen && (pending.tiled_edges == 0))
    {
        pending.geometry =
            wf::expand_geometry_by_margins(pending.geometry, pending.margins);
    }

    // Part 05: publish the shade_duration option (so the transformer picks
    // it up) and export CSD shadow margins for views without a decorator.
    publish_shade_duration(view);
    export_csd_shadow_margins(view);

    // Finding 19: the maximized_borders / maximized_shadows options must
    // reach the margin math — the callbacks recreate() the decoration, so
    // setting them on the freshly created decorator here propagates changes
    // (WCM checkboxes, wayfire.ini edits) to every existing window.
    deco->set_maximized_borders(bool(maximized_borders));
    deco->set_maximized_shadows(bool(maximized_shadows));
}

// ── Remove decorator (pixdecor decoration.cpp remove_decoration) ───────────
void emeraldwf_plugin::remove_decoration(::wayfire_toplevel_view view)
{
    auto toplevel = view->toplevel();
    // Review N3: if a DECORATED window is currently SHADED, the transformer
    // holds the collapsed state and the pending margins are the SHADE
    // margins. Shrinking the geometry with those (below) and dropping the
    // decorator without detaching the transformer would leave a collapsed
    // client with nothing left to animate it back. Reset to the unshaded
    // state first — the same reset the enable_shade off-path does: detach
    // the shade transformer, then on_shade_progress(0.0) (margins →
    // exactly the unshaded ones, and the pre-shade frame geometry is
    // restored when a reference was captured).
    // Review R5: the transformer strip (and the settle reset below it) is
    // for DECORATED views only. A shaded CSD window has no decorator but a
    // legitimately attached shade transformer (toggle_shade/scroll_shade
    // attach it directly to the client surface) — remove_decoration() is
    // called unconditionally from on_decoration_state_changed, and stripping
    // that transformer here unshaded the window instantly (no animation, no
    // settle bookkeeping). Undecorated views keep their transformer; the
    // geometry zeroing below stays for all views (it is safe: undecorated
    // views have no emerald frame margins).
    if (auto deco = toplevel->get_data<emerald_decorator_t>())
    {
        auto tmgr = view->get_transformed_node();
        if (auto tr = tmgr->get_transformer<wf::emeraldwf::emerald_shade_t>(
                wf::emeraldwf::SHADE_TRANSFORMER_NAME))
        {
            tmgr->rem_transformer<wf::emeraldwf::emerald_shade_t>(tr);
        }
        deco->on_shade_progress(0.0);
        // contract: detach_node() is implemented by part 04
        deco->detach_node();
    }

    // Review N4: the emerald_node_t's deco back-reference is a no-op-
    // deleter shared_ptr — the node is a latent UAF if the decorator is
    // erased from the toplevel's data map WITHOUT going through
    // remove_decoration() (detach_node() runs first, dropping the node).
    // Every erase_data<emerald_decorator_t>() in the plugin goes through
    // here (fini() included — it calls remove_decoration()).
    toplevel->erase_data<emerald_decorator_t>();
    view->erase_data(
        std::string(wf::emeraldwf::SHADOW_MARGIN_DATA_KEY));

    auto &pending = toplevel->pending();
    if (!pending.fullscreen && (pending.tiled_edges == 0))
    {
        pending.geometry =
            wf::shrink_geometry_by_margins(pending.geometry, pending.margins);
    }

    pending.margins = {0, 0, 0, 0};
}

// ── Re-evaluate matchers + decoration state (pixdecor update_view_decoration) ──
void emeraldwf_plugin::update_view_decoration(wayfire_view view)
{
    if (auto toplevel = wf::toplevel_cast(view))
    {
        // The decorator lives on the toplevel_t (view->toplevel()), NOT on
        // the view — both are separate object_base_t instances with separate
        // data maps. Reading via the view would always miss the stored
        // decorator and re-run adjust_new_decorations() on every signal
        // (title change, geometry change), re-expanding the geometry each
        // time (the "window grows on every title change" bug).
        auto &t = *toplevel->toplevel();
        bool has_deco = (bool)t.get_data<emerald_decorator_t>();
        bool should   = should_decorate_view(toplevel);
        if (has_deco && !should)
        {
            remove_decoration(toplevel);
        } else if (!has_deco && should)
        {
            adjust_new_decorations(toplevel);
        }

        if (auto deco = t.get_data<emerald_decorator_t>())
        {
            // contract: on_theme_changed() is implemented by part 04
            deco->on_theme_changed();
        }

        wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
    }
}

// ── Option change: recreate decorations (part 04 stubs the real work) ──────
void emeraldwf_plugin::recreate()
{
    for (const auto &view : wf::get_core().get_all_views())
    {
        auto toplevel = wf::toplevel_cast(view);
        if (!toplevel)
        {
            continue;
        }

        remove_decoration(toplevel);
        adjust_new_decorations(toplevel);
        wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
    }
}

void emeraldwf_plugin::toggle_shade(wayfire_view view)
{
    if (!bool(enable_shade) || view == nullptr)
        return;
    auto toplevel = wf::toplevel_cast(view);
    if (toplevel == nullptr)
        return;
    if (auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>())
        deco->toggle_shade();
    else if (toplevel->is_mapped())
    {
        // CSD view: no decorator, but shade still works (spec 05 §5) —
        // the transformer is attached directly to the client surface with
        // the `csd_titlebar_height` option.
        auto tmgr = view->get_transformed_node();
        auto tr = tmgr->get_transformer<wf::emeraldwf::emerald_shade_t>(
            wf::emeraldwf::SHADE_TRANSFORMER_NAME);
        if (tr)
        {
            tr->set_strip_h(int(csd_titlebar_height));
            tr->init_animation(!tr->get_direction());
        }
        else
        {
            // Finding 22: wire emeraldwf/shade_duration into the animation.
            auto new_tr = std::make_shared<wf::emeraldwf::emerald_shade_t>(
                toplevel, int(csd_titlebar_height), shade_duration);
            // Review R4: re-export the shadow margins per tick — the CSD
            // view has no decorator for the transformer's tick() to drive,
            // so without this a shadow plugin would see the pre-shade
            // margins for the whole animation. export_csd_shadow_margins
            // reads the transformer's own progression.
            new_tr->set_shadow_update(
                [this, view] { export_csd_shadow_margins(view); });
            tmgr->add_transformer(new_tr, wf::TRANSFORMER_2D,
                                  wf::emeraldwf::SHADE_TRANSFORMER_NAME);
            new_tr->init_animation(true);
        }
        export_csd_shadow_margins(view);
    }
}

// part 06 finding 18: same view/deco resolution as toggle_shade() (the
// decorator lives on the toplevel_t, NOT on the view), but direction-aware:
// wheel up (delta < 0) shades, wheel down unshades. CSD views get the same
// treatment with the transformer attached directly to the client surface.
void emeraldwf_plugin::scroll_shade(wayfire_view view, double delta)
{
    if (!bool(enable_shade) || view == nullptr)
        return;
    auto toplevel = wf::toplevel_cast(view);
    if (toplevel == nullptr)
        return;
    if (auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>())
    {
        deco->scroll_shade(delta);
        return;
    }
    if (toplevel->is_mapped())
    {
        // CSD view: no decorator — the transformer goes directly on the
        // client surface (spec 05 §5).
        if (delta == 0.0)
        {
            return;
        }
        const bool to_shade = scroll_shades(delta);
        auto tmgr = view->get_transformed_node();
        auto tr = tmgr->get_transformer<wf::emeraldwf::emerald_shade_t>(
            wf::emeraldwf::SHADE_TRANSFORMER_NAME);
        if (tr == nullptr && !to_shade)
        {
            return; // unshaded; nothing to unshade
        }
        if (tr)
        {
            tr->set_strip_h(int(csd_titlebar_height));
            tr->init_animation(to_shade);
        }
        else
        {
            // Finding 22: wire emeraldwf/shade_duration into the animation.
            auto new_tr = std::make_shared<wf::emeraldwf::emerald_shade_t>(
                toplevel, int(csd_titlebar_height), shade_duration);
            // Review S1: same per-tick shadow re-export as toggle_shade's
            // creation site — the R4 callback must be registered at EVERY
            // CSD transformer creation branch, or a wheel-shaded window
            // would keep its pre-shade shadow margins for the whole
            // animation (and stay stale afterwards).
            new_tr->set_shadow_update(
                [this, view] { export_csd_shadow_margins(view); });
            tmgr->add_transformer(new_tr, wf::TRANSFORMER_2D,
                                  wf::emeraldwf::SHADE_TRANSFORMER_NAME);
            new_tr->init_animation(to_shade);
        }
        export_csd_shadow_margins(view);
    }
}

// Part 05 (D12): CSD views have no emerald frame (hence no shadow inside
// the extents), so we export a minimal shadow-margin custom data with
// `thickness = shadow_pad` (default 0) so a standalone shadow plugin can
// still shadow them. Documented in 06's README.
void emeraldwf_plugin::export_csd_shadow_margins(wayfire_view view)
{
    auto toplevel = wf::toplevel_cast(view);
    if (toplevel == nullptr)
        return;
    const auto &pending = toplevel->toplevel()->pending();
    if (pending.fullscreen)
        return;
    int client_h = toplevel->get_geometry().height;
    double progress = 0.0;
    auto tmgr = view->get_transformed_node();
    if (auto tr = tmgr->get_transformer<wf::emeraldwf::emerald_shade_t>(
            wf::emeraldwf::SHADE_TRANSFORMER_NAME))
    {
        progress = tr->get_progression();
    }
    wf::emeraldwf::export_shadow_margins(
        view, client_h, int(shadow_pad), int(csd_titlebar_height), progress,
        toplevel->toplevel()->get_min_size().height);
}

// Publish the `emeraldwf/shade_duration` + `enable_shade` options to a
// decorator so its shade transformer/entry points pick them up (finding 23:
// enable_shade must reach the node-level scroll/click paths, not just the
// plugin-side IPC/wheel checks). Called from init() and from
// adjust_new_decorations.
void emeraldwf_plugin::publish_shade_duration(wayfire_view view)
{
    auto toplevel = wf::toplevel_cast(view);
    if (!toplevel)
        return;
    auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>();
    if (deco)
    {
        deco->set_shade_duration_opt(shade_duration);
        deco->set_enable_shade(bool(enable_shade));
    }
}

// ── Part 06: live settings reload ─────────────────────────────────────────
void emeraldwf_plugin::emerald_watched_paths(std::vector<std::string> &out)
{
    out = {last_settings_path, last_theme_dir + "/theme.ini",
           last_theme_dir};
    // The active wayfire config file too: WCM writes it (and never notifies
    // the running wayfire), so its emeraldwf/* values are synced on mtime
    // change — this is what makes WCM engine switches live (part 06).
    if (!wayfire_config_file.empty())
    {
        out.push_back(wayfire_config_file);
    }
}

bool emeraldwf_plugin::poll_tick()
{
    std::vector<std::string> paths;
    emerald_watched_paths(paths);

    bool emerald_changed = false;
    bool config_changed = false;
    for (auto &p : paths)
    {
        std::uint64_t m = emwf_file_stamp(p);
        auto it = last_stamp.find(p);
        if (it == last_stamp.end() || it->second != m)
        {
            last_stamp[p] = m;
            if (p == wayfire_config_file)
            {
                config_changed = true;
            }
            else
            {
                emerald_changed = true;
            }
        }
    }

    if (config_changed)
    {
        // WCM (or a hand edit) touched the ini: push the emeraldwf values
        // into the in-memory config manager. set_value() fires the option
        // callbacks, which arm the debounced reload and apply the side
        // effects (finding 24: ALL options — the old sync only covered the
        // four string options, so WCM checkbox/keybinding/animation edits
        // like enable_shade never reached the running plugin).
        emerald_config_options_t targets{engine, theme_dir,
            ignore_views_string, always_decorate_string,
            double_click_action, middle_click_action,
            maximized_borders, maximized_shadows, enable_shade,
            csd_titlebar_height, shadow_pad, shade_modifier,
            shade_duration, no_scroll_shade, use_decoration_cropping,
            use_button_fade, use_button_fade_pulse,
            button_fade_total_duration, button_fade_step_duration,
            button_fade_pulse_wait_duration,
            button_fade_pulse_min_opacity};
        const bool value_changed =
            sync_emeraldwf_options_from_config(wayfire_config_file, targets);
        if (value_changed)
        {
            arm_reload();
        }
    }
    if (emerald_changed)
    {
        arm_reload();
    }
    return true; // keep the poller repeating
}

void emeraldwf_plugin::start_poller()
{
    if (poll_timer.is_connected())
    {
        return;
    }
    // Prime the stamp cache so the first poll does not trigger a spurious reload.
    std::vector<std::string> paths;
    emerald_watched_paths(paths);
    for (const auto &p : paths)
    {
        last_stamp[p] = emwf_file_stamp(p);
    }
    // cppcheck-suppress knownConditionTrueFalse
    // poll_tick() intentionally always returns true: it is a periodic poller
    // (repeat every 500 ms), so the timer's "keep repeating" flag is true.
    poll_timer.set_timeout(500, [this] { return this->poll_tick(); });
}

void emeraldwf_plugin::stop_poller()
{
    poll_timer.disconnect();
}

void emeraldwf_plugin::reload_debounced()
{
    reload_theme();
}

void emeraldwf_plugin::arm_reload()
{
    if (reload_timer.is_connected())
    {
        // Already armed — the pending timeout covers this change too.
        return;
    }
    reload_timer.set_timeout(250, [this] { this->reload_debounced(); });
}

void emeraldwf_plugin::apply_titlebar_action_options(const ThemeSettings &ts)
{
    // The WCM option defines the click action (global emerald setting;
    // WCM is the editor, not an override). Unparseable value → emerald's
    // own default.
    ts.ws()->double_click_action =
        resolve_titlebar_action(std::string(double_click_action),
                                DOUBLE_CLICK_SHADE);
    ts.ws()->middle_click_action =
        resolve_titlebar_action(std::string(middle_click_action),
                                DOUBLE_CLICK_SEND_TO_BACK);
}

void emeraldwf_plugin::apply_emerald_global_options(const ThemeSettings &ts)
{
    // Part 08: WCM defines these global emerald settings; the loader no
    // longer reads them from settings.ini, so they must be applied here on
    // every theme load (init + reload) or the WCM values would be lost.
    ts.ws()->titlebar_no_scroll_shade = no_scroll_shade;
    ts.ws()->use_decoration_cropping = use_decoration_cropping;
    emeraldwf_apply_button_fade(ts.ws(),
        use_button_fade, use_button_fade_pulse,
        button_fade_total_duration, button_fade_step_duration,
        button_fade_pulse_wait_duration, button_fade_pulse_min_opacity);
}

void emeraldwf_plugin::reload_theme()
{
    std::string settings_ini = last_settings_path;
    std::string theme_dir_opt = last_theme_dir;

    auto new_theme = std::make_shared<ThemeSettings>();
    const std::string ov = engine_override_value(std::string(engine));
    bool ok =
        new_theme->load(settings_ini, theme_dir_opt, ov);
    if (!ok)
    {
        LOGW("emeraldwf: reload failed, keeping previous theme");
        return;
    }

    apply_titlebar_action_options(*new_theme);
    apply_emerald_global_options(*new_theme);

    // Swap the engine before publishing the new theme (the loader already
    // dlopen'd it; emerald_engine_load() closes the previous engine's ws).
    if (new_theme->engine() != ov)
    {
        LOGI("emeraldwf: engine changed %s → %s", ov,
             new_theme->engine());
    }

    theme = std::move(new_theme);

    // Re-render every existing decorator with the new theme.
    for (const auto &view : wf::get_core().get_all_views())
    {
        auto toplevel = wf::toplevel_cast(view);
        if (!toplevel)
        {
            continue;
        }
        // decorator lives on the toplevel_t, not the view (see
        // update_view_decoration)
        if (auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>())
        {
            // Hand the decorator the NEW theme (finding 14): the engine is
            // a process-wide global, so the decorator's old window_settings
            // (engine_ws of a dlclose'd engine) must never be rendered again.
            // This also makes theme-only edits (colors etc.) reach existing
            // windows, which they did not before.
            deco->set_theme(theme);
            // The node re-renders from the decorator's theme_ref() on the
            // next frame when marked dirty.
            deco->on_theme_changed();
            // Margins can change with the theme (spaces/titlebar).
            auto &pending = toplevel->toplevel()->pending();
            pending.margins = deco->get_margins(pending);
        }
        wf::get_core().tx_manager->schedule_object(toplevel->toplevel());
    }

    LOGI("emeraldwf: theme reloaded (engine=%s)",
         theme->engine().c_str());
}

emeraldwf_plugin::emeraldwf_plugin()
{
}

void emeraldwf_plugin::init()
{

    // ── Theme: load (part 02 loader) + live reload monitors (part 06) ───────
    // D10: core settings live in ~/.emerald/settings.ini; the theme (engine
    // sections + PNGs) in <theme_dir>/ (default ~/.emerald/theme). The
    // engine option (part 07 "Engine Override"): empty/"no_override"
    // → the theme's own [engine]; a different value → that engine,
    // running on emerald's default theme (a theme's assets are
    // engine-specific).
    last_settings_path =
        std::string(g_get_home_dir()) + "/.emerald/settings.ini";
    // Wayfire does not expand "~" in option values — do it here, or the
    // default `~/.emerald/theme` is treated as a literal path.
    last_theme_dir = expand_home(std::string(theme_dir));

    theme = std::make_shared<ThemeSettings>();
    {
        // ThemeSettings::load is non-const (part 02 owns it); drop const for
        // the one-time init, then publish via shared_ptr<const>.
        auto &ts = *const_cast<ThemeSettings *>(theme.get());
        // Review R3: load() reports failure (engine dlopen failed, legacy
        // fallback included). A failed load leaves valid() == false — the
        // decorators and nodes check it before rendering, so the plugin
        // runs frameless instead of with a silently dead theme.
        const bool theme_ok = ts.load(last_settings_path, last_theme_dir,
                engine_override_value(std::string(engine)));
        if (!theme_ok)
        {
            LOGW("emeraldwf: theme load failed (dir=%s) — no engine could "
                 "be loaded; windows will be undecorated",
                 last_theme_dir.c_str());
        }
        // The WCM option defines the click actions (settings.ini values are
        // superseded here).
        apply_titlebar_action_options(ts);
        apply_emerald_global_options(ts);
    }
    const std::string ov_init = engine_override_value(std::string(engine));
    if (!ov_init.empty() && theme->engine() != ov_init)
    {
        LOGI("emeraldwf: engine override '%s' — theme engine '%s' does not "
             "match; the engine runs on the default theme", ov_init,
             theme->engine());
    }
    LOGI("emeraldwf: theme loaded (engine=%s, dir=%s)",
         theme->engine().c_str(), last_theme_dir);


    // Part 06: live reload — poll the mtimes of settings.ini, theme.ini, and
    // the theme dir (engine PNG/dir changes). emerald-theme-manager edits both
    // files, so this makes it work with no restart and no signal. The active
    // wayfire config file is watched too, so WCM edits to emeraldwf/* (e.g.
    // the engine) are synced into the in-memory options and reloaded live.
    wayfire_config_file = find_wayfire_config_file();
    LOGI("emeraldwf: watching config file %s for live option changes",
         wayfire_config_file.c_str());
    start_poller();


    // CSD windows use the csd_titlebar_height option (pixdecor parity).
    theme_dir.set_callback([=]
    {
        // theme_dir changed → the poller picks up the new dir on its next
        // poll (it reads last_theme_dir dynamically). Re-arm the debounce.
        last_theme_dir = expand_home(std::string(theme_dir));
        arm_reload();
    });
    engine.set_callback([=]
    {
        // Engine option override → rebuild the theme, forcing the engine
        // swap via emerald_engine_load() inside ThemeSettings::load.
        arm_reload();
    });

    // Titlebar click actions are read at click time from the shared
    // window_settings (layout.double_click_action → ctx.fs->ws), so an
    // in-place update reaches every existing window immediately — no theme
    // rebuild, no recreate. (ws() returns a non-const pointer even through
    // the const ThemeSettings.) The option defines the action; an
    // unparseable value (typo) falls back to emerald's own default.
    double_click_action.set_callback([=]
    {
        if (theme)
        {
            theme->ws()->double_click_action =
                resolve_titlebar_action(std::string(double_click_action),
                                        DOUBLE_CLICK_SHADE);
        }
    });
    middle_click_action.set_callback([=]
    {
        if (theme)
        {
            theme->ws()->middle_click_action =
                resolve_titlebar_action(std::string(middle_click_action),
                                        DOUBLE_CLICK_SEND_TO_BACK);
        }
    });

    // Part 08: the WCM-owned global emerald settings. Each change arms the
    // debounced reload, which re-applies them to a fresh theme (and
    // recomputes margins — use_decoration_cropping affects the maximized
    // crop — and resets the nodes' button-fade state).
    no_scroll_shade.set_callback([=] { arm_reload(); });
    use_decoration_cropping.set_callback([=] { arm_reload(); });
    use_button_fade.set_callback([=] { arm_reload(); });
    use_button_fade_pulse.set_callback([=] { arm_reload(); });
    button_fade_total_duration.set_callback([=] { arm_reload(); });
    button_fade_step_duration.set_callback([=] { arm_reload(); });
    button_fade_pulse_wait_duration.set_callback([=] { arm_reload(); });
    button_fade_pulse_min_opacity.set_callback([=] { arm_reload(); });

    // NOTE: csd_titlebar_height.set_callback() is registered once, below
    // (with the other Part-05 callbacks) — it covers CSD decorators, live
    // transformers, and the shadow-margin export. (Finding 32: there used to
    // be a second, redundant callback here that only updated the decorators.)

    // ── Margins-affecting options → recreate (part 04 does the node part) ──
    maximized_borders.set_callback([=] { recreate(); });
    maximized_shadows.set_callback([=] { recreate(); });
    ignore_views_string.set_callback([=]
    {
        for (const auto &view : wf::get_core().get_all_views())
        {
            update_view_decoration(view);
        }
    });
    always_decorate_string.set_callback([=]
    {
        for (const auto &view : wf::get_core().get_all_views())
        {
            update_view_decoration(view);
        }
    });

    // ── Signal wiring ──────────────────────────────────────────────────────
    on_new_tx = [this] (wf::txn::new_transaction_signal *ev)
    {
        for (const auto &obj : ev->tx->get_objects())
        {
            auto toplevel = std::dynamic_pointer_cast<wf::toplevel_t>(obj);
            if (toplevel == nullptr)
            {
                continue;
            }

            // Already decorated → update margins (part 04 adds redraw).
            if (auto deco = toplevel->get_data<emerald_decorator_t>())
            {
                auto &pending = toplevel->pending();
                pending.margins = deco->get_margins(pending);
                continue;
            }

            // Not mapping this transaction → nothing to do.
            if (toplevel->current().mapped || !toplevel->pending().mapped)
            {
                continue;
            }

            // Transaction maps the toplevel → decorate if applicable.
            auto view = wf::find_view_for_toplevel(toplevel);
            wf::dassert(view != nullptr,
                "Mapping a toplevel means there must be a corresponding view!");
            if (should_decorate_view(view))
            {
                adjust_new_decorations(view);
            }
        }
    };

    on_decoration_state_changed = [this] (wf::view_decoration_state_updated_signal *ev)
    {
        auto toplevel = wf::toplevel_cast(ev->view);
        if (toplevel)
        {
            remove_decoration(toplevel);
        }

        update_view_decoration(ev->view);
    };

    on_view_tiled = [this] (wf::view_tiled_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    on_app_id_changed = [this] (wf::view_app_id_changed_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    on_title_changed = [this] (wf::view_title_changed_signal *ev)
    {
        update_view_decoration(ev->view);
    };

    // Wayfire populates the toplevel's wm geometry after the mapping
    // transaction. Re-negotiate margins + tell the node to recompute its
    // bounding box so the emerald frame is sized for the real client (not the
    // 0×0 placeholder from mapping time). This is the fix for the "no
    // decoration painted on first map" bug — without it, the frame is
    // computed for a zero client and never updated.
    on_geometry_changed = [this] (wf::view_geometry_changed_signal *ev)
    {
        auto toplevel = wf::toplevel_cast(ev->view);
        if (toplevel == nullptr)
        {
            return;
        }
        // decorator lives on the toplevel_t, not the view
        auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>();
        if (deco)
        {
            auto &pending = toplevel->toplevel()->pending();
            pending.margins = deco->get_margins(pending);
            // contract: on_geometry_changed() implemented by part 04 —
            // marks the node size dirty so it re-renders at the new size.
            deco->on_geometry_changed();
        }
    };

    wf::get_core().tx_manager->connect(&on_new_tx);
    wf::get_core().connect(&on_decoration_state_changed);
    wf::get_core().connect(&on_view_tiled);
    wf::get_core().connect(&on_app_id_changed);
    wf::get_core().connect(&on_title_changed);
    wf::get_core().connect(&on_geometry_changed);

    // ── Existing views ─────────────────────────────────────────────────────
    for (const auto &view : wf::get_core().get_all_views())
    {
        update_view_decoration(view);
    }

    // ── Part 05: shade (spec 05 §2) ─────────────────────────────────────────
    // Publish the shade_duration option to every decorator (so the
    // transformer picks it up) and register the global axis binding + IPC.
    for (const auto &view : wf::get_core().get_all_views())
    {
        publish_shade_duration(view);
        export_csd_shadow_margins(view);
    }

    if (bool(enable_shade))
    {
        shade_axis_cb = [=] (const wlr_pointer_axis_event *ev)
        {
            if (ev->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
            {
                // part 06 finding 17: shade the window UNDER THE CURSOR,
                // not the keyboard-focused one. Axis bindings are global,
                // and with overlapping windows the focused view is often
                // the one BELOW the cursor — the old code shaded that
                // one regardless of which window the pointer was on.
                // get_view_at() is a front-to-back scene hit-test, so it
                // returns the topmost view at the pointer (and, because
                // the emerald node overrides find_node_at, hits on our
                // titlebar/border resolve to the right view).
                // part 06 finding 18: the previous iteration of this
                // callback read the decorator from the VIEW's data map —
                // it lives on the toplevel_t (view->toplevel()), so the
                // lookup was always null and the gesture did nothing.
                // scroll_shade() (below) does the resolution correctly.
                auto v = wf::get_core().get_view_at(
                    wf::get_core().get_cursor_position());
                if (v != nullptr)
                {
                    scroll_shade(v, ev->delta);
                    return true;
                }
                return true;
            }
            return false;
        };
        wf::get_core().bindings->add_axis(shade_modifier, &shade_axis_cb);
    }

    emeraldwf_shade_toggle.set_handler([=] (wf::output_t *, wayfire_view view)
    {
        if (!bool(enable_shade))
            return false;
        if (view != nullptr)
        {
            toggle_shade(view);
            return true;
        }
        return false;
    });

    // Finding 29: changing the gesture must REBIND the axis binding — the
    // value sync alone (finding 24) would update the option while the
    // binding stayed on the old modifier (the gesture would do nothing, or
    // worse, the old modifier would keep shading). rem_binding on an
    // unregistered callback is a no-op, so this is safe in both directions.
    shade_modifier.set_callback([=]
    {
        wf::get_core().bindings->rem_binding(&shade_axis_cb);
        if (bool(enable_shade))
        {
            wf::get_core().bindings->add_axis(shade_modifier, &shade_axis_cb);
        }
    });

    enable_shade.set_callback([=]
    {
        // Finding 23: the node-level entry points (titlebar scroll, click
        // actions) read the option from their decorator — publish the new
        // value to every decorator in BOTH directions, not just the
        // plugin-side binding bookkeeping below.
        // Finding 25: the decorator lives on the toplevel_t
        // (toplevel->toplevel()), NOT on the view — a view-based get_data()
        // always missed it, so the publish (and the on_shade_progress reset
        // below) never reached any decorator.
        for (const auto &view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (toplevel == nullptr)
                continue;
            if (auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>())
                deco->set_enable_shade(bool(enable_shade));
        }
        if (bool(enable_shade))
        {
            wf::get_core().bindings->add_axis(shade_modifier, &shade_axis_cb);
        }
        else
        {
            wf::get_core().bindings->rem_binding(&shade_axis_cb);
            // Unchecking `enable_shade` (e.g. in WCM) must unshade every
            // currently-shaded window. The transformer normally detaches
            // itself only when its own animation finishes in the unshaded
            // direction — with the binding gone that can never happen, so a
            // shaded window would stay collapsed into its titlebar strip
            // forever (part-06 finding 12).
            for (const auto &view : wf::get_core().get_all_views())
            {
                auto toplevel = wf::toplevel_cast(view);
                if (toplevel == nullptr)
                    continue;
                auto tmgr = view->get_transformed_node();
                if (auto tr = tmgr->get_transformer<wf::emeraldwf::emerald_shade_t>(
                        wf::emeraldwf::SHADE_TRANSFORMER_NAME))
                {
                    tmgr->rem_transformer<wf::emeraldwf::emerald_shade_t>(tr);
                }
                // Finding 25: toplevel_t data map (see the publish loop
                // above) — the view-based lookup always missed the decorator,
                // so a disabled shade left stale (shaded) margins on SSD
                // windows until the next transaction.
                if (auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>())
                {
                    // Reset margins/shadow-custom-data/node to the
                    // unshaded state (progress 0).
                    deco->on_shade_progress(0.0);
                }
                else if (toplevel->is_mapped())
                {
                    // CSD view: re-export shadow margins (progress is 0 now
                    // that the transformer is gone).
                    export_csd_shadow_margins(view);
                    wf::get_core().tx_manager->schedule_object(
                        toplevel->toplevel());
                }
            }
        }
    });

    csd_titlebar_height.set_callback([=]
    {
        for (const auto &view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel)
                continue;
            auto deco = toplevel->toplevel()->get_data<emerald_decorator_t>();
            if (deco && deco->is_csd())
            {
                deco->set_titlebar_height(int(csd_titlebar_height));
            }
            if (auto tr = view->get_transformed_node()
                             ->get_transformer<wf::emeraldwf::emerald_shade_t>(
                                 wf::emeraldwf::SHADE_TRANSFORMER_NAME))
            {
                if (!deco)
                    tr->set_strip_h(int(csd_titlebar_height));
            }
            export_csd_shadow_margins(view);
        }
    });

    shadow_pad.set_callback([=]
    {
        for (const auto &view : wf::get_core().get_all_views())
        {
            export_csd_shadow_margins(view);
        }
    });


}

void emeraldwf_plugin::fini()
{

    // 1. Stop the mtime poller and cancel the reload debounce. Both are
    //    wf::wl_timer sources on the Wayland event loop; if left armed they
    //    would fire on a destroyed plugin (use-after-free).
    stop_poller();
    reload_timer.disconnect();

    // 2. Disconnect signals BEFORE removing decorations, so no handler
    //    re-enters (on_new_tx would otherwise re-create decorators mid-fin).
    on_new_tx.disconnect();
    on_decoration_state_changed.disconnect();
    on_view_tiled.disconnect();
    on_app_id_changed.disconnect();
    on_title_changed.disconnect();
    on_geometry_changed.disconnect();
    wf::get_core().bindings->rem_binding(&shade_axis_cb);


    // 3. Remove the shade transformers (they reference plugin state) and the
    //    scene nodes; erase the per-toplevel decorators — via
    //    remove_decoration() (review N4: never erase_data the decorator
    //    directly, detach_node() must run first).
    for (const auto &view : wf::get_core().get_all_views())
    {
        if (auto toplevel = wf::toplevel_cast(view))
        {
            auto tmgr = view->get_transformed_node();
            if (auto tr = tmgr->get_transformer<wf::emeraldwf::emerald_shade_t>(
                    wf::emeraldwf::SHADE_TRANSFORMER_NAME))
            {
                tmgr->rem_transformer<wf::emeraldwf::emerald_shade_t>(tr);
            }

            remove_decoration(toplevel);

            wf::get_core().tx_manager->schedule_object(toplevel->toplevel());

        }
    }


}

} // namespace emeraldwf
} // namespace wf

DECLARE_WAYFIRE_PLUGIN(wf::emeraldwf::emeraldwf_plugin);

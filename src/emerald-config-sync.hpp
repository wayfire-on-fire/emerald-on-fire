// emerald-config-sync.hpp — sync emeraldwf/* options from the wayfire ini
// (part 06 finding 24).
//
// WCM (wayfire's config manager) is a separate process: when the user
// toggles a checkbox it writes the ini file via its own config manager and
// never notifies the running wayfire. The installed wayfire (0.11) does not
// auto-reload its config, so the in-memory option values — and the plugin's
// option callbacks with them — never update for WCM edits. The plugin's mtime
// poller (plugin.cpp) watches the active wayfire config file and, on change,
// calls sync_emeraldwf_options_from_config() to push every emeraldwf/* value
// into the in-memory config manager. set_value() fires the option callbacks,
// which apply the side effects (binding add/remove, decorator publish,
// decoration recreate) that a plain value change would not.
//
// This TU is deliberately pure (GKeyFile + wf::config::option_t only, no
// wayfire core symbols) so the sync logic is headless-testable.
#pragma once

#include <memory>
#include <string>

#include <wayfire/config/option.hpp>
#include <wayfire/config/types.hpp>
#include <wayfire/util/duration.hpp>

namespace wf
{
namespace emeraldwf
{
// Every emeraldwf/* option the poller keeps in sync with the ini. A null
// entry is skipped, so tests can register a subset.
struct emerald_config_options_t
{
    std::shared_ptr<wf::config::option_t<std::string>> engine;
    std::shared_ptr<wf::config::option_t<std::string>> theme_dir;
    std::shared_ptr<wf::config::option_t<std::string>> ignore_views;
    std::shared_ptr<wf::config::option_t<std::string>> always_decorate;
    std::shared_ptr<wf::config::option_t<std::string>> double_click_action;
    std::shared_ptr<wf::config::option_t<std::string>> middle_click_action;
    std::shared_ptr<wf::config::option_t<bool>> maximized_borders;
    std::shared_ptr<wf::config::option_t<bool>> maximized_shadows;
    std::shared_ptr<wf::config::option_t<bool>> enable_shade;
    std::shared_ptr<wf::config::option_t<int>> csd_titlebar_height;
    std::shared_ptr<wf::config::option_t<int>> shadow_pad;
    std::shared_ptr<wf::config::option_t<wf::keybinding_t>> shade_modifier;
    std::shared_ptr<wf::config::option_t<wf::animation_description_t>>
    shade_duration;
    // Part 08: the WCM-owned global emerald settings (moved off the theme
    // manager's Emerald Settings tab).
    std::shared_ptr<wf::config::option_t<bool>> no_scroll_shade;
    std::shared_ptr<wf::config::option_t<bool>> use_decoration_cropping;
    std::shared_ptr<wf::config::option_t<bool>> use_button_fade;
    std::shared_ptr<wf::config::option_t<bool>> use_button_fade_pulse;
    std::shared_ptr<wf::config::option_t<int>> button_fade_total_duration;
    std::shared_ptr<wf::config::option_t<int>> button_fade_step_duration;
    std::shared_ptr<wf::config::option_t<int>> button_fade_pulse_wait_duration;
    std::shared_ptr<wf::config::option_t<int>> button_fade_pulse_min_opacity;
};

// Read the [emeraldwf] section of `ini_path` and push any changed value into
// the in-memory options (firing their update callbacks). Absent keys are
// left alone; unparseable values are ignored (the in-memory value stays).
// Returns true if any option value actually changed (i.e. the theme reload
// should be armed).
bool sync_emeraldwf_options_from_config(const std::string & ini_path,
    emerald_config_options_t & opts);
}
}

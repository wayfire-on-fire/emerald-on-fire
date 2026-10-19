// emerald-config-sync.cpp — see emerald-config-sync.hpp.
#include "emerald-config-sync.hpp"

#include <glib.h>

#include <optional>

namespace wf
{
namespace emeraldwf
{
namespace
{
// Parse one ini key into a typed option and push it into the in-memory
// config manager when the value differs. Returns true if the option changed.
template<typename T>
bool sync_one(GKeyFile *kf, const char *key,
    std::shared_ptr<wf::config::option_t<T>> & opt)
{
    if (opt == nullptr)
    {
        return false;
    }

    gchar *val = g_key_file_get_string(kf, "emeraldwf", key, nullptr);
    if (val == nullptr)
    {
        return false; // key absent — leave the in-memory value alone
    }

    const std::string str(val);
    g_free(val);

    std::optional<T> parsed = wf::option_type::from_string<T>(str);
    if (!parsed)
    {
        return false; // unparseable — leave the in-memory value alone
    }

    if (!(opt->get_value() == *parsed))
    {
        opt->set_value(*parsed);
        return true;
    }

    return false;
}
} // namespace

bool sync_emeraldwf_options_from_config(const std::string & ini_path,
    emerald_config_options_t & opts)
{
    GKeyFile *kf = g_key_file_new();
    GError *err  = nullptr;
    if (!g_key_file_load_from_file(kf, ini_path.c_str(), G_KEY_FILE_NONE, &err))
    {
        if (err != nullptr)
        {
            g_error_free(err);
        }

        g_key_file_free(kf);
        return false;
    }

    bool changed = false;
    changed |= sync_one(kf, "engine", opts.engine);
    changed |= sync_one(kf, "theme_dir", opts.theme_dir);
    changed |= sync_one(kf, "ignore_views", opts.ignore_views);
    changed |= sync_one(kf, "always_decorate", opts.always_decorate);
    changed |= sync_one(kf, "double_click_action", opts.double_click_action);
    changed |= sync_one(kf, "middle_click_action", opts.middle_click_action);
    changed |= sync_one(kf, "maximized_borders", opts.maximized_borders);
    changed |= sync_one(kf, "maximized_shadows", opts.maximized_shadows);
    changed |= sync_one(kf, "enable_shade", opts.enable_shade);
    changed |= sync_one(kf, "csd_titlebar_height", opts.csd_titlebar_height);
    changed |= sync_one(kf, "shadow_pad", opts.shadow_pad);
    changed |= sync_one(kf, "shade_modifier", opts.shade_modifier);
    changed |= sync_one(kf, "shade_duration", opts.shade_duration);
    changed |= sync_one(kf, "no_scroll_shade", opts.no_scroll_shade);
    changed |= sync_one(kf, "use_decoration_cropping",
        opts.use_decoration_cropping);
    changed |= sync_one(kf, "use_button_fade", opts.use_button_fade);
    changed |= sync_one(kf, "use_button_fade_pulse",
        opts.use_button_fade_pulse);
    changed |= sync_one(kf, "button_fade_total_duration",
        opts.button_fade_total_duration);
    changed |= sync_one(kf, "button_fade_step_duration",
        opts.button_fade_step_duration);
    changed |= sync_one(kf, "button_fade_pulse_wait_duration",
        opts.button_fade_pulse_wait_duration);
    changed |= sync_one(kf, "button_fade_pulse_min_opacity",
        opts.button_fade_pulse_min_opacity);

    g_key_file_free(kf);
    return changed;
}
}
}

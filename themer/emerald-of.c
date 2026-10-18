/* emerald-of.c — emerald-theme-manager (part 07): emeraldwf-specific glue.
 *
 * send_reload_signal() interposes the vendored libengine/themer.c copy
 * (libemeraldengine): the executable's definition wins over the archive's
 * at link time, so every call site (apply_settings, cb_load, cb_save)
 * resolves to this no-op.
 *
 * The upstream implementation pings the emerald X11 plugin for a theme
 * reload (D-Bus signal when built with USE_DBUS, otherwise an X client
 * message atom, with a `killall -SIGUSR1 emerald` last resort). Under
 * wayfire none of those applies — emeraldwf's mtime poller
 * (plugin.cpp: poll_tick) watches ~/.emerald/settings.ini,
 * ~/.emerald/theme/theme.ini and the theme dir, and reloads the theme
 * within ~750 ms of the file write that apply_settings() just did.
 * Writing the files IS the live reload; the ping is unnecessary.
 */

#include <string.h>
#include <glib.h>

void send_reload_signal()
{
    /* no-op — see the file header. */
}

/* emerald-of.c — part 07, engine override.
 *
 * Read the wayfire config's `emeraldwf` `engine` option (the WCM "Engine
 * Override"): when it names an engine different from the active theme's,
 * the plugin runs that engine with an empty theme, so the theme editor
 * shows a warning in the Frame Engine pane. Probes the standard config
 * locations. Returns a newly allocated string (possibly empty) or NULL
 * when no config file exists.
 */
gchar * emeraldof_config_engine_override(void)
{
    /* The active session config comes first (lxqt wayland sessions run
     * wayfire -c ~/.config/lxqt/wayland/lxqt-wayfire.ini; stock setups
     * use ~/.config/wayfire.ini). Both may exist. */
    static const char * names[] = {
        ".config/lxqt/wayland/lxqt-wayfire.ini",
        ".config/wayfire.ini",
    };
    for (guint i = 0; i < G_N_ELEMENTS(names); i++)
    {
        gchar * p = g_strdup_printf("%s/%s", g_get_home_dir(), names[i]);
        gchar * data = NULL;
        gboolean read = g_file_get_contents(p, &data, NULL, NULL);
        g_free(p);
        if (!read)
            continue;
        GKeyFile * f = g_key_file_new();
        g_key_file_load_from_data(f, data, strlen(data), G_KEY_FILE_NONE,
                                  NULL);
        gchar * v = g_key_file_get_string(f, "emeraldwf", "engine", NULL);
        g_key_file_free(f);
        g_free(data);
        if (v != NULL)
            return v; /* config found; the value may be empty */
    }
    return NULL;
}

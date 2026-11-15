// emerald-config-sync.cpp — see emerald-config-sync.hpp.
#include "emerald-config-sync.hpp"

#include <glib.h>

#include <fstream>
#include <optional>
#include <sstream>

namespace wf
{
namespace emeraldwf
{
namespace
{
// Strip leading/trailing ASCII whitespace (wf-config
// ignore_leading_trailing_whitespace).
std::string trim(const std::string & s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
    {
        return {};
    }

    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// M21 (issue #1): mirror wf-config's remove_comments +
// remove_escaped_sharps in one pass: cut at the first UNescaped '#'
// (a '\' escapes it); consume the escape backslash before any '#' so
// the value keeps the '#' as a plain character — GKeyFile rejects a raw
// '\' in an UNQUOTED value, so the escape must be resolved here, not by
// GKeyFile.
std::string strip_comment(const std::string & line)
{
    std::string out;
    out.reserve(line.size());
    bool prev_backslash = false;
    for (char ch : line)
    {
        if (ch == '#')
        {
            if (prev_backslash)
            {
                out.pop_back(); // consume the escape backslash
                out += '#';
            } else
            {
                break; // an unescaped '#' starts a comment
            }
        } else
        {
            out += ch;
        }

        prev_backslash = (ch == '\\');
    }

    return out;
}

// M21 (issue #1): sanitize ini content the way wayfire's own LENIENT
// parser treats it (wf-config file.cpp: strip '#' comments, trim
// whitespace, skip lines without '=', log-and-continue). GKeyFile is
// STRICT: a UTF-8 BOM, a ';' comment line, a 'key: value' line, or ANY
// single malformed line fails the WHOLE file. A perfectly normal
// wayfire.ini (wayfire parses it fine) would then kill the entire live
// option sync silently. Keep only what GKeyFile accepts: section
// headers and trimmed key=value lines.
std::string sanitize_ini(const std::string & content)
{
    std::string out;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line))
    {
        line = strip_comment(line);
        line = trim(line);
        if (line.empty())
        {
            continue;
        }

        if ((line[0] == '[') && (line.back() == ']'))
        {
            out += line + "\n";
            continue;
        }

        // ';' comments and any line without '=' are skipped by
        // wayfire's parser (wrong format → log + continue).
        const size_t eq = line.find('=');
        if ((eq == std::string::npos) || (line[0] == ';'))
        {
            continue;
        }

        const std::string key = trim(line.substr(0, eq));
        if (key.empty())
        {
            continue; // "=value" — no option name
        }

        out += key + "=" + trim(line.substr(eq + 1)) + "\n";
    }

    return out;
}

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
    // M21 (issue #1): read the file ourselves (g_key_file_load_from_file
    // is strict — see sanitize_ini) and strip a UTF-8 BOM, which GKeyFile
    // rejects but wayfire's parser tolerates.
    std::ifstream in(ini_path);
    if (!in)
    {
        // M21: log, don't fail silently — the poller retries on every
        // mtime change, so a silent failure is invisible. (glib logger,
        // not wayfire LOGW: this TU is headless-pure.)
        g_warning("emeraldwf: cannot read config file %s",
            ini_path.c_str());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    if ((content.size() >= 3) &&
        (static_cast<unsigned char>(content[0]) == 0xEF) &&
        (static_cast<unsigned char>(content[1]) == 0xBB) &&
        (static_cast<unsigned char>(content[2]) == 0xBF))
    {
        content = content.substr(3); // UTF-8 BOM
    }

    const std::string data = sanitize_ini(content);

    GKeyFile *kf = g_key_file_new();
    GError *err  = nullptr;
    if (!g_key_file_load_from_data(kf, data.c_str(), data.size(),
        G_KEY_FILE_NONE, &err))
    {
        if (err != nullptr)
        {
            // M21: log the parse failure (pre-fix: a strict GKeyFile
            // rejection of a normal wayfire.ini was silent).
            g_warning("emeraldwf: failed to parse config file %s: %s",
                ini_path.c_str(), err->message);
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

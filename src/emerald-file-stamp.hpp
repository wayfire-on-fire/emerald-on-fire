// emerald-file-stamp.hpp — file stamps for the mtime poller (finding 44).
//
// st.st_mtime has ONE-SECOND granularity: a second write that lands in
// the same wall-clock second as the first leaves it unchanged, so a
// time_t-based poller never sees it. That is not a corner case for this
// product — the theme manager's apply is debounced 500 ms and the plugin
// polls every 500 ms, so quick successive checkbox edits (check, then
// uncheck) silently vanished: the running theme kept its old values
// (e.g. button pulse kept breathing after the checkbox was unchecked).
// Nanosecond stamps make every write visible.
#pragma once

#include <cstdint>
#include <string>

#include <sys/stat.h>

namespace wf
{
namespace emeraldwf
{
// Stamp of the file at `path` (nanoseconds since epoch); 0 when the file
// does not exist (the poller treats "missing" as a change too).
inline std::uint64_t emwf_file_stamp(const std::string & path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
    {
        return 0;
    }

    return (std::uint64_t)st.st_mtim.tv_sec * 1000000000ull +
           (std::uint64_t)st.st_mtim.tv_nsec;
}
} // namespace emeraldwf
} // namespace wf

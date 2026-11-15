/* engine-fake-nodraw.c — a fake engine .so WITHOUT engine_draw_frame
 * (M2, issue #1).
 *
 * Exports the three optional bridge symbols but NOT the required
 * engine_draw_frame. Pre-fix, emerald_engine_load() dlopen'd it,
 * published engine_handle + active_engine_name, resolved the symbols,
 * and returned false - leaving a half-published bridge. The NEXT load
 * of the same name hit the same-engine fast path and returned TRUE
 * with e_draw still null: the engine was "loaded" forever and drew
 * nothing, and the handle was leaked (never dlclose'd).
 */
#include <stdio.h>
#include <stdlib.h>

typedef struct window_settings_s window_settings;

static void log_call(const char *what)
{
    const char *p = getenv("EMERALDWF_FAKE_ENGINE_LOG");
    if (!p)
    {
        return;
    }

    FILE *f = fopen(p, "a");
    if (f)
    {
        fprintf(f, "%s\n", what);
        fclose(f);
    }
}

void init_engine(window_settings *ws)
{
    (void)ws;
    log_call("fakenodraw.init");
}

void fini_engine(window_settings *ws)
{
    (void)ws;
    log_call("fakenodraw.fini");
}

void load_engine_settings(void *keyfile, window_settings *ws)
{
    (void)keyfile;
    (void)ws;
}

/* engine-fake-ok.c — a fake engine .so for the loader tests (M1-M2, issue #1).
 *
 * Exports exactly the four symbols emerald_engine_load() resolves
 * (init_engine / fini_engine / load_engine_settings / engine_draw_frame).
 * init/fini append a line to $EMERALDWF_FAKE_ENGINE_LOG (if set) so a
 * test can assert the fini/init ordering across a swap.
 *
 * The loader dlsym()s by name, so C linkage is what the bridge sees.
 * The test dlopens it via the bridge with a temp HOME (no
 * ~/.emerald/engines) and a system_dir pointing at this built .so.
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
    log_call("fakeok.init");
}

void fini_engine(window_settings *ws)
{
    (void)ws;
    log_call("fakeok.fini");
}

void load_engine_settings(void *keyfile, window_settings *ws)
{
    (void)keyfile;
    (void)ws;
}

void engine_draw_frame(void *decor, void *cairo_ctx)
{
    (void)decor;
    (void)cairo_ctx;
    log_call("fakeok.draw");
}

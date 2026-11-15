/* engine-fake-alt.c — a SECOND full fake engine (M1-M2, issue #1).
 *
 * Identical in capability to fakeok (all four bridge symbols), but a
 * DIFFERENT module: a successful swap fakeok -> fakealt must finalize
 * the old engine (fakeok.fini) before the new one is inited
 * (fakealt.init). init/fini log to $EMERALDWF_FAKE_ENGINE_LOG.
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
    log_call("fakealt.init");
}

void fini_engine(window_settings *ws)
{
    (void)ws;
    log_call("fakealt.fini");
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
}

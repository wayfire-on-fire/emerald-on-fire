/* engine-fake-ok.c — a fake engine .so for the loader tests (M1, issue #1).
 *
 * Exports exactly the four symbols emerald_engine_load() resolves
 * (init_engine / fini_engine / load_engine_settings / engine_draw_frame),
 * all no-ops. The loader dlsym()s by name, so C linkage is what the
 * bridge sees. The test dlopens it via the bridge with a temp HOME (no
 * ~/.emerald/engines) and a system_dir pointing at this built .so.
 */
typedef struct window_settings_s window_settings;

void init_engine(window_settings *ws)
{
    (void)ws;
}

void fini_engine(window_settings *ws)
{
    (void)ws;
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

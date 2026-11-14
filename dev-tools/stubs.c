/* frontend stubs for the native test programs */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "puzzles.h"

__attribute__((weak)) void frontend_default_colour(frontend *fe, float *output)
{
    output[0] = output[1] = output[2] = 1.0F;       /* white */
}
void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "fatal: ");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
    abort();
}
void get_random_seed(void **randseed, int *randseedsize)
{
    /* deterministic by default (so that a failing test run can be repeated);
     * STUBSEED=time gives the time of day */
    static unsigned long counter = 12345;
    struct timespec t;
    const char *e = getenv("STUBSEED");
    if (e && !strcmp(e, "time")) {
        clock_gettime(CLOCK_REALTIME, &t);
    } else {
        memset(&t, 0, sizeof t);
        t.tv_nsec = (long)(counter++ * 2654435761UL % 1000000007UL);
        if (e) t.tv_sec = atol(e);
    }
    *randseed = snew(struct timespec);
    memcpy(*randseed, &t, sizeof t);
    *randseedsize = sizeof t;
}
void deactivate_timer(frontend *fe) { }
void activate_timer(frontend *fe) { }
void android_completed(frontend *fe) { }
int allow_flash(frontend *fe) { return 1; }

/* a drawing API that draws nothing (shared by the tests) */
static void d_text(drawing *dr, int x, int y, int ft, int fs, int al, int c, const char *t) {}
static void d_rect(drawing *dr, int x, int y, int w, int h, int c) {}
static void d_line(drawing *dr, int x1, int y1, int x2, int y2, int c) {}
static void d_poly(drawing *dr, const int *co, int n, int f, int o) { int i; volatile int s = 0; for (i = 0; i < 2 * n; i++) s += co[i]; }
static void d_tpoly(drawing *dr, float t, const int *co, int n, int f, int o) { int i; volatile int s = 0; for (i = 0; i < 2 * n; i++) s += co[i]; }
static void d_circle(drawing *dr, int x, int y, int r, int f, int o) {}
static void d_tcircle(drawing *dr, float t, float x, float y, float r, int f, int o) {}
static void d_update(drawing *dr, int x, int y, int w, int h) {}
static void d_clip(drawing *dr, int x, int y, int w, int h) {}
static void d_unclip(drawing *dr) {}
static void d_start(drawing *dr) {}
static void d_end(drawing *dr) {}
static void d_status(drawing *dr, const char *t) { volatile size_t n = strlen(t); (void)n; }
static blitter *d_bnew(drawing *dr, int w, int h) { return (blitter *)snew(int); }
static void d_bfree(drawing *dr, blitter *b) { sfree(b); }
static void d_bsave(drawing *dr, blitter *b, int x, int y) {}
static void d_bload(drawing *dr, blitter *b, int x, int y) {}
static void d_lw(drawing *dr, float w) {}
static void d_ld(drawing *dr, bool d) {}
static char *d_fallback(drawing *dr, const char *const *s, int n) { return dupstr(s[0]); }
static void d_changed(drawing *dr, int u, int r) {}
static void d_purging(drawing *dr) {}
static void d_tline(drawing *dr, float t, float x1, float y1, float x2, float y2, int c) {}
static void d_inertia(drawing *dr, bool s) {}

const drawing_api null_drawing_api = {
    1, d_text, d_rect, d_line, d_poly, d_tpoly, d_circle, d_tcircle, d_update, d_clip, d_unclip,
    d_start, d_end, d_status, d_bnew, d_bfree, d_bsave, d_bload,
    NULL, NULL, NULL, NULL, NULL, NULL,         /* printing */
    d_lw, d_ld, d_fallback, d_changed, d_purging, d_tline, d_inertia, NULL
};


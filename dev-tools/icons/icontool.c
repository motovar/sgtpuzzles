/*
 * icontool: plays a game through the midend (game id + a list of events) with a
 * drawing API that writes down what is drawn, for render.py to make the icons.
 *
 *   icontool OUT.txt SIZE GAMEID [event ...]
 *     events:  key:<code>  mouse:<x>:<y>:<button>  step  solve  undo  timer:<seconds>
 *              (codes are decimal; button = key code with LEFT_BUTTON etc. from puzzles.h)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puzzles.h"

extern const game GAMESYM;
static FILE *out;

static void r_text(drawing *dr, int x, int y, int ft, int fs, int al, int col, const char *t)
{ fprintf(out, "T %d %d %d %d %d %d %s\n", x, y, ft, fs, al, col, t); }
static void r_rect(drawing *dr, int x, int y, int w, int h, int col)
{ fprintf(out, "R %d %d %d %d %d\n", x, y, w, h, col); }
static void r_line(drawing *dr, int x1, int y1, int x2, int y2, int col)
{ fprintf(out, "L %d %d %d %d %d\n", x1, y1, x2, y2, col); }
static void r_tline(drawing *dr, float th, float x1, float y1, float x2, float y2, int col)
{ fprintf(out, "TL %g %g %g %g %g %d\n", th, x1, y1, x2, y2, col); }
static void poly(const char *tag, float th, const int *c, int n, int f, int o)
{
    int i;
    fprintf(out, "%s %g %d %d %d", tag, th, f, o, n);
    for (i = 0; i < 2 * n; i++) fprintf(out, " %d", c[i]);
    fprintf(out, "\n");
}
static void r_poly(drawing *dr, const int *c, int n, int f, int o) { poly("P", 1.0f, c, n, f, o); }
static void r_tpoly(drawing *dr, float th, const int *c, int n, int f, int o) { poly("P", th, c, n, f, o); }
static void r_circle(drawing *dr, int cx, int cy, int r, int f, int o)
{ fprintf(out, "C 1 %d %d %d %d %d\n", cx, cy, r, f, o); }
static void r_tcircle(drawing *dr, float th, float cx, float cy, float r, int f, int o)
{ fprintf(out, "C %g %g %g %g %d %d\n", th, cx, cy, r, f, o); }
static void r_update(drawing *dr, int x, int y, int w, int h) {}
static void r_clip(drawing *dr, int x, int y, int w, int h) { fprintf(out, "CLIP %d %d %d %d\n", x, y, w, h); }
static void r_unclip(drawing *dr) { fprintf(out, "UNCLIP\n"); }
static void r_start(drawing *dr) { fprintf(out, "FRAME\n"); }
static void r_end(drawing *dr) {}
static void r_status(drawing *dr, const char *t) {}
struct blitter { int w, h; };
static blitter *r_bnew(drawing *dr, int w, int h) { blitter *b = snew(blitter); b->w = w; b->h = h; return b; }
static void r_bfree(drawing *dr, blitter *b) { sfree(b); }
static void r_bsave(drawing *dr, blitter *b, int x, int y) {}
static void r_bload(drawing *dr, blitter *b, int x, int y) {}
static void r_lw(drawing *dr, float w) {}
static void r_ld(drawing *dr, bool d) {}
static char *r_fallback(drawing *dr, const char *const *s, int n) { return dupstr(s[n - 1]); }
static void r_changed(drawing *dr, int a, int b) {}
static void r_purging(drawing *dr) {}
static void r_inertia(drawing *dr, bool s) {}

static const drawing_api rec_api = {
    1, r_text, r_rect, r_line, r_poly, r_tpoly, r_circle, r_tcircle, r_update, r_clip, r_unclip,
    r_start, r_end, r_status, r_bnew, r_bfree, r_bsave, r_bload,
    NULL, NULL, NULL, NULL, NULL, NULL,
    r_lw, r_ld, r_fallback, r_changed, r_purging, r_tline, r_inertia, NULL
};

void frontend_default_colour(frontend *fe, float *output)
{
    output[0] = output[1] = output[2] = 0.8F;    /* #cccccc, as the app's day background */
}

int main(int argc, char **argv)
{
    midend *me;
    int w, h, i, ncol;
    const char *err;
    float *col;

    if (argc < 4) { fprintf(stderr, "usage: icontool OUT SIZE GAMEID [events]\n"); return 2; }
    out = fopen(argv[1], "w");
    if (!out) { perror(argv[1]); return 1; }
    w = h = atoi(argv[2]);

    me = midend_new(NULL, &GAMESYM, &rec_api, NULL);
    err = midend_game_id(me, argv[3]);
    if (err) { fprintf(stderr, "game id: %s\n", err); return 1; }
    midend_new_game(me);
    midend_size(me, &w, &h, true, 1.0);
    fprintf(out, "SIZE %d %d\n", w, h);
    col = midend_colours(me, &ncol);
    fprintf(out, "PALETTE %d", ncol);
    for (i = 0; i < 3 * ncol; i++) fprintf(out, " %.6f", col[i]);
    fprintf(out, "\n");
    midend_redraw(me);

    for (i = 4; i < argc; i++) {
        const char *e = argv[i];
        if (!strncmp(e, "key:", 4)) {
            midend_process_key(me, 0, 0, atoi(e + 4));
        } else if (!strncmp(e, "mouse:", 6)) {
            int x, y, b;
            if (sscanf(e + 6, "%d:%d:%d", &x, &y, &b) != 3) { fprintf(stderr, "bad %s\n", e); return 1; }
            midend_process_key(me, x, y, b);
        } else if (!strcmp(e, "step")) {
            const char *m = midend_solve_step(me);
            if (m) { fprintf(stderr, "step: %s\n", m); }
        } else if (!strcmp(e, "solve")) {
            const char *m = midend_solve(me);
            if (m) { fprintf(stderr, "solve: %s\n", m); }
        } else if (!strcmp(e, "undo")) {
            midend_process_key(me, 0, 0, 'u');
        } else if (!strncmp(e, "timer:", 6)) {
            midend_timer(me, (float)atof(e + 6));
        } else {
            fprintf(stderr, "unknown event %s\n", e); return 1;
        }
    }
    midend_force_redraw(me);
    fprintf(out, "END\n");
    fclose(out);
    return 0;
}

/* evfuzz.c: random events through the midend with a drawing API that does nothing,
 * to be run under ASAN/UBSAN.
 *   evfuzz <nseeds> <nevents> [first seed]
 * For every preset and seed: new game, then nevents random mouse events, keys, undo, redo,
 * restart, Solve, next step, text format, serialise/deserialise, new game from the same id. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puzzles.h"

extern const game GAMESYM;

extern const drawing_api null_drawing_api;
#define api null_drawing_api

static unsigned long long rng = 88172645463325252ULL;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)((rng >> 11) % n); }

struct ent { char *title; game_params *params; };
static struct ent ents[200]; static int nents;
static void flatten(struct preset_menu *m)
{
    int i;
    for (i = 0; i < m->n_entries; i++) {
        if (m->entries[i].params) { ents[nents].title = m->entries[i].title; ents[nents].params = m->entries[i].params; nents++; }
        else flatten(m->entries[i].submenu);
    }
}

struct buf { char *p; size_t n, cap; };
static void bwrite(void *ctx, const void *b, int len)
{
    struct buf *bb = ctx;
    if (bb->n + len > bb->cap) { bb->cap = (bb->n + len) * 2 + 64; bb->p = realloc(bb->p, bb->cap); }
    memcpy(bb->p + bb->n, b, len); bb->n += len;
}
struct rd { struct buf *b; size_t pos; };
static bool bread(void *ctx, void *out, int len)
{
    struct rd *r = ctx;
    if (r->pos + len > r->b->n) return false;
    memcpy(out, r->b->p + r->pos, len); r->pos += len; return true;
}

int main(int argc, char **argv)
{
    int nseeds = argc > 1 ? atoi(argv[1]) : 3;
    int nevents = argc > 2 ? atoi(argv[2]) : 300;
    int seed0 = argc > 3 ? atoi(argv[3]) : 1;
    const game *g = &GAMESYM;
    midend *me = midend_new(NULL, g, &api, NULL);
    int id, limit;
    long total = 0;

    flatten(midend_get_presets(me, &limit));
    for (id = 0; id < nents; id++) {
        char *pstr = g->encode_params(ents[id].params, true);
        int s;
        for (s = 0; s < nseeds; s++) {
            char idbuf[400];
            int w, h, e;
            snprintf(idbuf, sizeof idbuf, "%s#%d", pstr, seed0 + s);
            if (midend_game_id(me, idbuf)) continue;
            midend_new_game(me);
            w = h = 0; midend_size(me, &w, &h, false, 1.0);
            midend_redraw(me);
            for (e = 0; e < nevents; e++) {
                int k = rnd(100), x = rnd(w + 40) - 20, y = rnd(h + 40) - 20, b;
                if (getenv("EVTRACE")) { fprintf(stderr, "[%s seed %d ev %d] k=%d x=%d y=%d\n", idbuf, seed0 + s, e, k, x, y); }
                if (k < 55) {
                    static const int mb[] = { LEFT_BUTTON, LEFT_DRAG, LEFT_RELEASE, RIGHT_BUTTON, RIGHT_DRAG, RIGHT_RELEASE, MIDDLE_BUTTON, MIDDLE_DRAG, MIDDLE_RELEASE };
                    b = mb[rnd(9)];
                    if (getenv("EVTRACE")) fprintf(stderr, "   mouse b=%d\n", b);
                    midend_process_key(me, x, y, b);
                } else if (k < 70) {
                    static const int kb[] = { CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT, CURSOR_SELECT, CURSOR_SELECT2, '\b', ' ', '\r', '+', '-', 'h', 'H', 'x', 'u', 'l', 'r', 'd' };
                    b = kb[rnd(sizeof kb / sizeof *kb)];
                    if (rnd(4) == 0) b = '0' + rnd(10);
                    if (rnd(5) == 0) b |= MOD_CTRL;
                    if (rnd(5) == 0) b |= MOD_SHFT;
                    if (getenv("EVTRACE")) fprintf(stderr, "   key b=%d\n", b);
                    midend_process_key(me, x, y, b);
                } else if (k < 76) midend_process_key(me, 0, 0, 'u');      /* undo */
                else if (k < 80) midend_process_key(me, 0, 0, 'r');         /* redo */
                else if (k < 82) midend_restart_game(me);
                else if (k < 85) { if (g->can_solve) midend_solve(me); }
                else if (k < 91) midend_solve_step(me);
                else if (k < 93) { if (midend_can_format_as_text_now(me)) { char *t = midend_text_format(me); sfree(t); } }
                else if (k < 95) {
                    struct buf bb = { NULL, 0, 0 };
                    struct rd r;
                    midend_serialise(me, bwrite, &bb);
                    r.b = &bb; r.pos = 0;
                    { const char *er = midend_deserialise(me, bread, &r); if (er) { printf("%s: deserialise: %s\n", idbuf, er); } }
                    free(bb.p);
                    w = h = 0; midend_size(me, &w, &h, false, 1.0);
                } else if (k < 96) { char *gid = midend_get_game_id(me); sfree(gid); }
                else if (k < 97) { midend_force_redraw(me); }
                else if (k < 98) { midend_timer(me, 0.05F); }
                else { midend_new_game(me); w = h = 0; midend_size(me, &w, &h, false, 1.0); }
                midend_redraw(me);
                total++;
            }
        }
        sfree(pstr);
    }
    printf("evfuzz %s: %ld events, no crash\n", GAMENAME, total);
    midend_free(me);
    return 0;
}

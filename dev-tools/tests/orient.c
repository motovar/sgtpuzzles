/* orient.c: does the parameter string of every preset survive the automatic orientation of the app?
 *
 * The Android front-end (GamePlay.orientGameType) takes any parameter string that matches
 *     (\d+)( ?)x\2(\d+)(.*)
 * as "<width>x<height><rest>" and, when the screen has the other orientation, turns it into
 * "<height>x<width><rest>" before the game is generated. A game whose parameters merely look like
 * that (Decanting: "<tubes>x<layers>w<colours>") would be generated with nonsense parameters, so
 * such games are listed in BackendName.isNotWidthByHeight and left alone.
 *
 * For every preset of the game, in the full and the short encoding, plus the default parameters,
 * this program does the same turning and asks the midend whether the result is still accepted
 * by decode_params + validate_params.
 *   orient [-x]    prints one line per problem; exit status 1 if there was any problem
 *                  (-x: the game is exempt, so a problem is expected and only reported as a note) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "puzzles.h"

extern const game GAMESYM;
extern const drawing_api null_drawing_api;

struct ent { char *title; game_params *params; };
static struct ent ents[200]; static int nents;

static void flatten(struct preset_menu *m)
{
    int i;
    for (i = 0; i < m->n_entries; i++) {
        if (m->entries[i].params) {
            ents[nents].title = m->entries[i].title;
            ents[nents].params = m->entries[i].params;
            nents++;
        } else flatten(m->entries[i].submenu);
    }
}

/* The same as the Kotlin code: returns a new string if the string is "WxH..." with W and H
 * different (or the same, then the result is equal), otherwise NULL. */
static char *turn(const char *s)
{
    const char *p = s, *w0, *w1, *h0, *h1;
    int sp;
    char *out;
    size_t n;
    w0 = p; while (isdigit((unsigned char)*p)) p++;
    if (p == w0) return NULL;
    w1 = p;
    sp = (*p == ' '); if (sp) p++;
    if (*p != 'x') return NULL;
    p++;
    if (sp) { if (*p != ' ') return NULL; p++; }
    h0 = p; while (isdigit((unsigned char)*p)) p++;
    if (p == h0) return NULL;
    h1 = p;
    n = strlen(s) + 4;
    out = malloc(n);
    sprintf(out, "%.*s%s%s%s%.*s%s", (int)(h1 - h0), h0, sp ? " " : "", "x", sp ? " " : "",
            (int)(w1 - w0), w0, h1);
    return out;
}

static int problems;
static int expected;

static void report(const char *what, const char *title, const char *from, const char *to,
                   const char *why)
{
    printf("%s: preset \"%s\": %s -> %s: %s\n", expected ? "note" : "PROBLEM", title, from, to, why);
    (void)what;
    problems++;
}

static void check(midend *me, const char *title, const char *pstr)
{
    char *t = turn(pstr);
    const char *err;
    if (!t) return;                           /* not of the form WxH..., nothing happens */
    err = midend_game_id(me, t);
    if (err) report("turn", title, pstr, t, err);
    free(t);
}

int main(int argc, char **argv)
{
    const game *g = &GAMESYM;
    midend *me = midend_new(NULL, g, &null_drawing_api, NULL);
    int id, limit;
    game_params *dp;
    char *s;
    expected = argc > 1 && !strcmp(argv[1], "-x");
    flatten(midend_get_presets(me, &limit));
    for (id = 0; id < nents; id++) {
        s = g->encode_params(ents[id].params, true);  check(me, ents[id].title, s); sfree(s);
        s = g->encode_params(ents[id].params, false); check(me, ents[id].title, s); sfree(s);
    }
    dp = g->default_params();
    s = g->encode_params(dp, true);  check(me, "(default)", s); sfree(s);
    s = g->encode_params(dp, false); check(me, "(default)", s); sfree(s);
    g->free_params(dp);
    printf("%s: %d presets, %d problems%s\n", GAMENAME, nents, problems, expected ? " (expected)" : "");
    midend_free(me);
    return expected ? 0 : (problems ? 1 : 0);
}

/* bintest.c: white-box tests of Binary (includes binary.c itself).
 *   bintest <seeds> [w h goalindex [rules]]
 *  1. slide_board against a plain reference of the rules of 2048 (random boards, all sizes);
 *  2. Threes: the move of a line against a reference that is written by the letter of the rules,
 *     with directions done by turning the board, known cases of the real game, properties of
 *     what happens (the order of the tiles, the sum, the end of the line);
 *  3. the trace of a move (for the animation): every tile goes forward, the tiles that share a
 *     square merge and their numbers add up to the tile that is there afterwards;
 *  4. the bag and the bonus tiles of Threes;
 *  5. parameters (old game IDs still mean 2048) and game descriptions: round trips, validate_desc
 *     against damaged strings;
 *  6. random play of both rules: the sum of the tiles, the score, the new tile, the end of the game;
 *  7. the midend: moves with the timer run through the animation, undo and redo run it backwards;
 *  8. hint: every move is legal; play to the goal, rate of wins and time per hint; for Threes the
 *     score against random play.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "binary.c"

extern const drawing_api null_drawing_api;

#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int fails;

static unsigned long long rng_s = 88172645463325252ULL;
static unsigned rnd(void) { rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17; return (unsigned)(rng_s >> 11); }

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

/* reference: one line, tiles move to index 0 */
static int ref_line(const unsigned char *in, unsigned char *out, int len)
{
    int tmp[16], n = 0, i, o = 0, score = 0;
    for (i = 0; i < len; i++) if (in[i]) tmp[n++] = in[i];
    for (i = 0; i < n; i++) {
        if (i + 1 < n && tmp[i] == tmp[i+1] && tmp[i] < MAXPOWER) {
            out[o++] = tmp[i] + 1; score += POWER[tmp[i] + 1]; i++;
        } else out[o++] = tmp[i];
    }
    while (o < len) out[o++] = 0;
    return score;
}

/* where the i'th tile of line k is on the board, for a move in direction d */
static int ref_xy(int w, int h, int d, int k, int i)
{
    int x, y;
    switch (d) { case DIR_L: x = i; y = k; break; case DIR_R: x = w-1-i; y = k; break;
                 case DIR_U: x = k; y = i; break; default: x = k; y = h-1-i; }
    return y * w + x;
}

static int ref_slide(unsigned char *t, int w, int h, int d)
{
    int k, i, score = 0;
    int nl = (d == DIR_L || d == DIR_R) ? h : w, len = (d == DIR_L || d == DIR_R) ? w : h;
    for (k = 0; k < nl; k++) {
        unsigned char in[16], out[16];
        for (i = 0; i < len; i++) in[i] = t[ref_xy(w, h, d, k, i)];
        score += ref_line(in, out, len);
        for (i = 0; i < len; i++) t[ref_xy(w, h, d, k, i)] = out[i];
    }
    return score;
}

/* reference for Threes: one line, by the letter of the rules; the tiles move to index 0 */
static void ref_threes_line(const unsigned char *in, unsigned char *out, int len, bool *moved)
{
    int i, j;
    memcpy(out, in, len);
    *moved = false;
    for (i = 0; i + 1 < len; i++) {
        int a = in[i], b = in[i+1];
        bool can = (a == 0 && b != 0)
            || (a == 1 && b == 2) || (a == 2 && b == 1)
            || (a >= 3 && a == b && a < MAXPOWER);
        if (!can) continue;
        out[i] = a == 0 ? b : a < 3 ? 3 : a + 1;
        for (j = i + 1; j + 1 < len; j++) out[j] = in[j+1];
        out[len-1] = 0;
        *moved = true;
        return;
    }
}

static bool ref_threes_slide(unsigned char *t, int w, int h, int d, bool *movedlines)
{
    int k, i;
    bool any = false;
    int nl = (d == DIR_L || d == DIR_R) ? h : w, len = (d == DIR_L || d == DIR_R) ? w : h;
    for (k = 0; k < nl; k++) {
        unsigned char in[16], out[16];
        bool mv;
        for (i = 0; i < len; i++) in[i] = t[ref_xy(w, h, d, k, i)];
        ref_threes_line(in, out, len, &mv);
        for (i = 0; i < len; i++) t[ref_xy(w, h, d, k, i)] = out[i];
        if (movedlines) movedlines[k] = mv;
        if (mv) any = true;
    }
    return any;
}

static long tilesum_rules(int rules, const unsigned char *t, int n)
{
    long s = 0; int i;
    for (i = 0; i < n; i++) if (t[i]) s += tile_number(rules, t[i]);
    return s;
}

static long tilesum(const unsigned char *t, int n)
{
    return tilesum_rules(RULES_2048, t, n);
}

static void test_slide(int rounds)
{
    int r;
    unsigned char a[81], b[81], orig[81];
    for (r = 0; r < rounds; r++) {
        int w = 2 + rnd() % 8, h = 2 + rnd() % 8, n = w * h, i, d, sa = 0, sb;
        int maxe = 1 + rnd() % MAXPOWER, fill = rnd() % 100;
        bool ch, rch;
        for (i = 0; i < n; i++) a[i] = (int)(rnd() % 100) < fill ? 1 + rnd() % maxe : 0;
        memcpy(orig, a, n); memcpy(b, a, n);
        d = rnd() % NDIRS;
        ch = slide_board(a, w, h, d, &sa);
        sb = ref_slide(b, w, h, d);
        rch = memcmp(orig, b, n) != 0;
        CHECK(memcmp(a, b, n) == 0);
        CHECK(sa == sb);
        CHECK(ch == rch);
        CHECK(tilesum(a, n) == tilesum(orig, n) || maxe >= MAXPOWER);
    }
    /* the chain merge of the original */
    {
        unsigned char l[4] = { 1, 1, 2, 3 };
        int sc = 0;
        CHECK(slide_board(l, 4, 1, DIR_L, &sc));
        CHECK(l[0] == 2 && l[1] == 2 && l[2] == 3 && l[3] == 0 && sc == 4);
    }
    {
        unsigned char l[4] = { 1, 1, 1, 1 };
        int sc = 0;
        slide_board(l, 4, 1, DIR_R, &sc);
        CHECK(l[0] == 0 && l[1] == 0 && l[2] == 2 && l[3] == 2 && sc == 8);
    }
    {   /* two of the biggest tiles do not merge (and so can not make a tile that is out of range) */
        unsigned char l[2] = { MAXPOWER, MAXPOWER };
        int sc = 0;
        CHECK(!slide_board(l, 2, 1, DIR_L, &sc));
    }
}

/* known cases of Threes: row in, row out after a move to the left (0 = empty) */
static void test_threes_cases(void)
{
    static const struct { int in[4], out[4]; } cases[] = {
        { {1,2,3,3}, {3,3,3,0} },          /* 1 and 2 make 3, the others follow */
        { {0,3,3,0}, {3,3,0,0} },          /* the gap closes first, the 3s merge next time */
        { {3,3,3,3}, {4,3,3,0} },          /* only the first pair, 3 + 3 = 6 (code 4) */
        { {1,1,2,2}, {1,3,2,0} },          /* 1 1 does not merge, 1 2 does */
        { {2,2,0,0}, {2,2,0,0} },          /* nothing can move */
        { {3,0,0,0}, {3,0,0,0} },
        { {0,0,4,5}, {0,4,5,0} },          /* two gaps: one square only */
        { {1,2,0,0}, {3,0,0,0} },
        { {4,4,3,0}, {5,3,0,0} },          /* 6 + 6 = 12 */
        { {1,1,1,1}, {1,1,1,1} },
        { {2,2,2,2}, {2,2,2,2} },
        { {3,4,0,5}, {3,4,5,0} },          /* the gap at the third place */
        { {0,1,2,3}, {1,2,3,0} },          /* a gap first: 1 and 2 do not merge in the same move */
        { {MAXPOWER,MAXPOWER,0,0}, {MAXPOWER,MAXPOWER,0,0} },
    };
    int i, j;
    for (i = 0; i < (int)lenof(cases); i++) {
        unsigned char t[4];
        bool moved;
        for (j = 0; j < 4; j++) t[j] = cases[i].in[j];
        moved = slide_threes(t, 4, 1, DIR_L, NULL, NULL);
        for (j = 0; j < 4; j++) {
            if (t[j] != cases[i].out[j]) { printf("Threes case %d wrong at %d: %d, wanted %d\n", i, j, t[j], cases[i].out[j]); fails++; break; }
        }
        CHECK(moved == (memcmp(cases[i].in, cases[i].out, sizeof(cases[i].in)) != 0));
    }
}

static void test_threes_slide(int rounds)
{
    int r;
    for (r = 0; r < rounds; r++) {
        int w = 2 + rnd() % 8, h = 2 + rnd() % 8, n = w * h, i, d, k;
        int maxc = 1 + rnd() % MAXPOWER, fill = rnd() % 100;
        unsigned char a[81], b[81], orig[81];
        bool ma[MAX_SIZE], mb[MAX_SIZE], cha, chb;
        for (i = 0; i < n; i++) {
            int c = 0;
            if ((int)(rnd() % 100) < fill) {
                /* small tiles often, so that the 1 and 2 meet */
                c = rnd() % 3 ? 1 + rnd() % 3 : 1 + rnd() % maxc;
                if (c > maxc && maxc >= 1) c = 1 + rnd() % maxc;
            }
            a[i] = c;
        }
        memcpy(orig, a, n); memcpy(b, a, n);
        d = rnd() % NDIRS;
        cha = slide_threes(a, w, h, d, ma, NULL);
        chb = ref_threes_slide(b, w, h, d, mb);
        CHECK(memcmp(a, b, n) == 0);
        CHECK(cha == chb);
        CHECK(cha == (memcmp(orig, a, n) != 0));
        for (k = 0; k < ((d == DIR_L || d == DIR_R) ? h : w); k++) CHECK(ma[k] == mb[k]);
        CHECK(tilesum_rules(RULES_THREES, a, n) == tilesum_rules(RULES_THREES, orig, n));
        /* the square at the far end of a line that has moved is empty */
        for (k = 0; k < ((d == DIR_L || d == DIR_R) ? h : w); k++) {
            int len = (d == DIR_L || d == DIR_R) ? w : h;
            if (ma[k]) CHECK(a[line_pos(w, h, d, k, len - 1)] == 0);
        }
        /* the same move in the same direction is refused (or not) as move_possible says */
        CHECK(move_possible(orig, w, h, RULES_THREES, d) == cha);
    }
}

/* the trace of a move: every tile goes forward, merging tiles share a square, the numbers add up */
static void test_trace(int rounds)
{
    int r;
    for (r = 0; r < rounds; r++) {
        int rules = rnd() % 2;
        int w = 2 + rnd() % 8, h = 2 + rnd() % 8, n = w * h, i, j, d, fill = rnd() % 100;
        int maxc = 1 + rnd() % 12;
        unsigned char a[81], orig[81];
        struct trace tr;
        long sum[81];
        int cnt[81];
        bool ch;
        for (i = 0; i < n; i++)
            a[i] = (int)(rnd() % 100) < fill ? 1 + rnd() % (rules == RULES_THREES && rnd() % 2 ? 3 : maxc) : 0;
        memcpy(orig, a, n);
        d = rnd() % NDIRS;
        ch = slide_any(rules, a, w, h, d, NULL, NULL, &tr);
        for (j = 0; j < n; j++) { sum[j] = 0; cnt[j] = 0; }
        for (i = 0; i < n; i++) {
            if (!orig[i]) { CHECK(tr.dst[i] == -1); continue; }
            CHECK(tr.dst[i] >= 0 && tr.dst[i] < n);
            if (tr.dst[i] < 0 || tr.dst[i] >= n) continue;
            j = tr.dst[i];
            sum[j] += tile_number(rules, orig[i]);
            cnt[j]++;
            /* forward: the same line, not behind */
            switch (d) {
              case DIR_L: CHECK(j / w == i / w && j % w <= i % w); break;
              case DIR_R: CHECK(j / w == i / w && j % w >= i % w); break;
              case DIR_U: CHECK(j % w == i % w && j / w <= i / w); break;
              default:    CHECK(j % w == i % w && j / w >= i / w); break;
            }
        }
        for (j = 0; j < n; j++) {
            CHECK(cnt[j] <= 2);
            if (cnt[j] == 0) CHECK(a[j] == 0);
            else CHECK(sum[j] == tile_number(rules, a[j]));
        }
        for (i = 0; i < n; i++)
            if (orig[i]) CHECK(tr.join[i] == (cnt[tr.dst[i]] == 2));
            else CHECK(tr.join[i] == 0);
        (void)ch;
        /* the trace changes nothing about the move itself */
        {
            unsigned char b[81];
            memcpy(b, orig, n);
            slide_any(rules, b, w, h, d, NULL, NULL, NULL);
            CHECK(memcmp(a, b, n) == 0);
        }
    }
}

static void test_bag(void)
{
    int i, g, n;
    random_state *rs = random_new("bag", 3);
    unsigned char tiles[16];
    int bag[3] = {4, 4, 4}, counts[4];
    long nbonus = 0, ndraw = 0;
    int minb = 99, maxb = 0;

    /* with small tiles on the board there are no bonus tiles: every twelve draws are four 1s, 2s and 3s */
    memset(tiles, 0, sizeof tiles); tiles[0] = 6;
    for (g = 0; g < 200; g++) {
        counts[1] = counts[2] = counts[3] = 0;
        for (i = 0; i < 12; i++) {
            n = threes_draw(rs, bag, tiles, 16);
            CHECK(n >= 1 && n <= 3);
            if (n >= 1 && n <= 3) counts[n]++;
        }
        CHECK(counts[1] == 4 && counts[2] == 4 && counts[3] == 4);
        CHECK(bag[0] == 0 && bag[1] == 0 && bag[2] == 0);
    }
    /* with a tile of 48 or more there are bonus tiles now and then */
    {
        int hist[20];
        memset(hist, 0, sizeof hist);
        for (g = 0; g < 2000; g++) {
            int maxc = 7 + g % 8;                  /* 48 ... */
            memset(tiles, 0, sizeof tiles); tiles[3] = maxc;
            for (i = 0; i < 100; i++) {
                int b0 = bag[0] + bag[1] + bag[2];
                n = threes_draw(rs, bag, tiles, 16);
                ndraw++;
                if (n >= 4) {
                    nbonus++;
                    CHECK(n <= maxc - 3);
                    CHECK(bag[0] + bag[1] + bag[2] == b0);     /* the bag is not touched */
                    hist[n]++;
                } else {
                    int b1 = bag[0] + bag[1] + bag[2];
                    CHECK(n >= 1 && n <= 3);
                    CHECK(b1 == (b0 == 0 ? 11 : b0 - 1));
                }
                for (n = 0; n < 3; n++) { if (bag[n] < minb) minb = bag[n]; if (bag[n] > maxb) maxb = bag[n]; }
            }
        }
        /* one in 21 */
        CHECK(nbonus > ndraw / 21 * 9 / 10 && nbonus < ndraw / 21 * 11 / 10);
        CHECK(minb >= 0 && maxb <= 4);
        /* all the bonus tiles from 6 up to the biggest / 8 turn up */
        CHECK(hist[4] > 0 && hist[7] > 0 && hist[10] > 0);
        CHECK(hist[3] == 0 && hist[1] == 0);
    }
    random_free(rs);
}

static game_state *make(const game_params *p, const char *seed, char **descout)
{
    random_state *rs = random_new(seed, strlen(seed));
    char *aux = NULL, *desc = new_game_desc(p, rs, &aux, false);
    game_state *s;
    CHECK(validate_desc(p, desc) == NULL);
    s = new_game(NULL, p, desc);
    if (descout) *descout = desc; else sfree(desc);
    sfree(aux);
    random_free(rs);
    return s;
}

static void test_params(void)
{
    int w, g, r;
    game_params p, q;
    char *enc;
    const char *err;

    for (r = 0; r < RULESCOUNT; r++)
        for (g = 0; g < GOALCOUNT; g++)
            for (w = 2; w <= 9; w++) {
                p.w = w; p.h = 2 + (w * 5) % 8; p.goal = r == RULES_THREES ? GOAL_ENDLESS : g; p.mode = MODE_RECT; p.rules = r;
                err = validate_params(&p, true);
                CHECK(err == NULL);
                enc = encode_params(&p, true);
                decode_params(&q, enc);
                CHECK(q.w == p.w && q.h == p.h && q.goal == p.goal && q.mode == p.mode && q.rules == p.rules);
                sfree(enc);
                enc = encode_params(&p, false);
                decode_params(&q, enc);
                CHECK(q.w == p.w && q.h == p.h && q.goal == p.goal && q.mode == p.mode && q.rules == p.rules);
                sfree(enc);
            }
    /* the IDs of the old games: no rules means 2048, nothing else changed */
    enc = encode_params(&(game_params){4, 4, GOAL_2048, MODE_RECT, RULES_2048}, true);
    CHECK(strcmp(enc, "4x4g7mR") == 0);
    sfree(enc);
    decode_params(&q, "5x5");
    CHECK(q.w == 5 && q.h == 5 && q.goal == GOAL_2048 && q.rules == RULES_2048);
    decode_params(&q, "4");
    CHECK(q.w == 4 && q.h == 4 && q.goal == GOAL_2048 && q.rules == RULES_2048);
    decode_params(&q, "4x4g3mR");
    CHECK(q.goal == GOAL_128 && q.rules == RULES_2048);
    decode_params(&q, "4x4g5mRvt");
    CHECK(q.rules == RULES_THREES && q.goal == GOAL_ENDLESS);          /* Threes has no goal */
    decode_params(&q, "4x4gemR");
    CHECK(q.rules == RULES_2048 && q.goal == GOAL_ENDLESS);
    decode_params(&q, "4x4gemRvt");
    CHECK(q.rules == RULES_THREES && q.goal == GOAL_ENDLESS);
    /* validation */
    p.w = p.h = 4; p.goal = GOAL_512; p.mode = MODE_RECT; p.rules = RULES_THREES;
    CHECK(validate_params(&p, true) != NULL);
    p.goal = GOAL_ENDLESS;
    CHECK(validate_params(&p, true) == NULL);
    p.rules = RULESCOUNT;
    CHECK(validate_params(&p, true) != NULL);
    p.rules = RULES_2048; p.w = 1;
    CHECK(validate_params(&p, true) != NULL);
    p.w = 10;
    CHECK(validate_params(&p, true) != NULL);
    /* the presets: valid, and their names */
    {
        int i;
        char *name;
        game_params *pp;
        for (i = 0; game_fetch_preset(i, &name, &pp); i++) {
            CHECK(validate_params(pp, true) == NULL);
            enc = encode_params(pp, true);
            decode_params(&q, enc);
            CHECK(q.w == pp->w && q.h == pp->h && q.goal == pp->goal && q.rules == pp->rules);
            sfree(enc);
            sfree(name);
            free_params(pp);
        }
        CHECK(i == (int)lenof(binary_presets));
    }
    /* the custom dialog: the rules and the goal in the order of the items */
    {
        config_item *cfg = game_configure(&(game_params){4, 5, GOAL_1024, MODE_RECT, RULES_2048});
        game_params *pp;
        CHECK(strcmp(cfg[0].name, "Width") == 0 && strcmp(cfg[2].name, "Rules") == 0);
        CHECK(cfg[2].u.choices.selected == RULES_2048 && cfg[3].u.choices.selected == GOAL_1024);
        cfg[2].u.choices.selected = RULES_THREES;
        pp = custom_params(cfg);
        CHECK(pp->w == 4 && pp->h == 5 && pp->rules == RULES_THREES && pp->goal == GOAL_ENDLESS);
        CHECK(validate_params(pp, true) == NULL);
        free_params(pp);
        free_cfg(cfg);
    }
}

static void test_random_play(int games)
{
    int g;
    for (g = 0; g < games; g++) {
        game_params p;
        game_state *s;
        char sb[32];
        int moves = 0, nfinish = 0;
        p.w = 2 + rnd() % 8; p.h = 2 + rnd() % 8; p.goal = rnd() % GOALCOUNT; p.mode = MODE_RECT;
        p.rules = g % 2 ? RULES_THREES : RULES_2048;
        if (p.rules == RULES_THREES) p.goal = GOAL_ENDLESS;
        CHECK(validate_params(&p, true) == NULL);
        sprintf(sb, "t%d", g);
        s = make(&p, sb, NULL);
        if (p.rules == RULES_2048)
            CHECK(tilesum(s->tiles, p.w * p.h) >= 4 && tilesum(s->tiles, p.w * p.h) <= 8);
        else {
            int ntiles = p.w * p.h * 9 / 16, i, cnt = 0, c[4] = {0, 0, 0, 0};
            if (ntiles < 2) ntiles = 2;
            for (i = 0; i < p.w * p.h; i++) if (s->tiles[i]) { cnt++; CHECK(s->tiles[i] <= 3); c[s->tiles[i]]++; }
            CHECK(cnt == ntiles);
            CHECK(s->next >= 1 && s->next <= 3);
            c[s->next]++;
            /* the tiles and the next one came out of the bag (as long as it has not been filled again) */
            if (ntiles + 1 <= 12) {
                CHECK(c[1] + s->bag[0] == 4 && c[2] + s->bag[1] == 4 && c[3] + s->bag[2] == 4);
            }
            CHECK(s->score == threes_score(s->tiles, p.w * p.h));
        }
        CHECK(s->lastdir == -1 && s->spawn == -1);
        while (!s->finished && moves < 3000) {
            char mv[2];
            int d, tries;
            game_state *n;
            long before = tilesum_rules(p.rules, s->tiles, p.w * p.h), after;
            int score0 = s->score;
            for (tries = 0; tries < 16; tries++) {
                d = rnd() % NDIRS;
                if (move_possible(s->tiles, p.w, p.h, p.rules, d)) break;
            }
            if (tries == 16) {
                /* every direction by hand */
                for (d = 0; d < NDIRS && !move_possible(s->tiles, p.w, p.h, p.rules, d); d++) ;
                if (d == NDIRS) { CHECK(0); break; }
            }
            mv[0] = dir_chars[d]; mv[1] = 0;
            n = execute_move(s, mv);
            CHECK(n != NULL);
            if (!n) break;
            after = tilesum_rules(p.rules, n->tiles, p.w * p.h);
            CHECK(n->lastdir == d);
            CHECK(n->spawn >= 0 && n->spawn < p.w * p.h);
            if (p.rules == RULES_2048) {
                /* a tile of 2 or 4 turned up; the merges keep the sum */
                CHECK(after - before == 2 || after - before == 4);
                CHECK(n->tiles[n->spawn] == 1 || n->tiles[n->spawn] == 2);
                CHECK(n->score >= score0);
            } else {
                int b0 = s->bag[0] + s->bag[1] + s->bag[2], b1 = n->bag[0] + n->bag[1] + n->bag[2];
                int maxc = 0, i;
                /* the tile that was shown as the next one turned up */
                CHECK(after - before == threes_value(s->next));
                CHECK(n->tiles[n->spawn] == s->next);
                CHECK(n->score == threes_score(n->tiles, p.w * p.h));
                CHECK(n->score >= score0);
                for (i = 0; i < p.w * p.h; i++) if (n->tiles[i] > maxc) maxc = n->tiles[i];
                CHECK(n->next >= 1 && (n->next <= 3 || maxc >= 7));
                CHECK(n->next <= 3 || n->next <= maxc - 3);
                if (n->next <= 3) CHECK(b1 == (b0 == 0 ? 11 : b0 - 1)); else CHECK(b1 == b0);
                CHECK(n->bag[0] >= 0 && n->bag[0] <= 4 && n->bag[1] >= 0 && n->bag[1] <= 4 && n->bag[2] >= 0 && n->bag[2] <= 4);
                CHECK(!n->won);
                /* the new tile came in at the far end of a line that moved */
                {
                    int len = (d == DIR_L || d == DIR_R) ? p.w : p.h;
                    int line = (d == DIR_L || d == DIR_R) ? n->spawn / p.w : n->spawn % p.w;
                    CHECK(n->spawn == line_pos(p.w, p.h, d, line, len - 1));
                }
            }
            CHECK(n->finished == !moves_possible(n->tiles, p.w, p.h, p.rules) || n->finished);
            CHECK(game_status(n) == (n->finished ? (n->won ? 1 : -1) : 0));
            if (p.goal == GOAL_ENDLESS) CHECK(!n->won);
            /* the old state is untouched */
            CHECK(tilesum_rules(p.rules, s->tiles, p.w * p.h) == before);
            free_game(s);
            s = n;
            moves++;
        }
        if (s->finished) nfinish++;
        /* a finished game refuses moves */
        if (s->finished) { CHECK(execute_move(s, "L") == NULL); CHECK(execute_move(s, "U") == NULL); }
        /* garbage moves */
        CHECK(execute_move(s, "") == NULL);
        CHECK(execute_move(s, "X") == NULL);
        CHECK(execute_move(s, "LL") == NULL);
        free_game(s);
    }
}

static void test_desc(int n)
{
    int i;
    for (i = 0; i < n; i++) {
        game_params p, q;
        char sb[32], *desc, *enc, *bad;
        game_state *s;
        size_t len, k;
        p.w = 2 + rnd() % 8; p.h = 2 + rnd() % 8; p.goal = rnd() % GOALCOUNT; p.mode = MODE_RECT;
        p.rules = i % 2 ? RULES_THREES : RULES_2048;
        if (p.rules == RULES_THREES) p.goal = GOAL_ENDLESS;
        sprintf(sb, "d%d", i);
        s = make(&p, sb, &desc);
        enc = encode_params(&p, true);
        decode_params(&q, enc);
        CHECK(q.w == p.w && q.h == p.h && q.goal == p.goal && q.mode == p.mode && q.rules == p.rules);
        sfree(enc);
        /* same description -> same game */
        {
            game_state *s2 = new_game(NULL, &p, desc);
            CHECK(memcmp(s->tiles, s2->tiles, p.w * p.h) == 0);
            CHECK(s->next == s2->next && memcmp(s->bag, s2->bag, sizeof s->bag) == 0);
            free_game(s2);
        }
        /* damaged descriptions never crash and are mostly refused */
        len = strlen(desc);
        bad = dupstr(desc);
        for (k = 0; k < 60; k++) {
            size_t pos = rnd() % (len + 1);
            char c = (char)(rnd() % 128);
            int mode = rnd() % 4;
            free(bad);
            bad = dupstr(desc);
            if (mode == 0 && pos < len) bad[pos] = c ? c : 'z';
            else if (mode == 1) bad[pos] = 0;
            else if (mode == 2 && len > 4) { memmove(bad + pos, bad + pos + 1, len - pos); }
            else { bad = sresize(bad, len + 2, char); memmove(bad + pos + 1, bad + pos, len - pos + 1); bad[pos] = c ? c : 'a'; }
            if (validate_desc(&p, bad) == NULL) {
                game_state *s3 = new_game(NULL, &p, bad);   /* must be safe */
                free_game(s3);
            }
            len = strlen(desc);
        }
        sfree(bad);
        CHECK(validate_desc(&p, "") != NULL);
        CHECK(validate_desc(&p, ",") != NULL);
        CHECK(validate_desc(&p, "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz,") != NULL);
        free_game(s);
        sfree(desc);
    }
    {   /* the two kinds of description are not mixed up */
        game_params a = {3, 3, GOAL_2048, MODE_RECT, RULES_2048}, t = {3, 3, GOAL_ENDLESS, MODE_RECT, RULES_THREES};
        char *da, *dt;
        random_state *rs = random_new("x", 1);
        char *aux;
        da = new_game_desc(&a, rs, &aux, false);
        dt = new_game_desc(&t, rs, &aux, false);
        CHECK(validate_desc(&a, da) == NULL && validate_desc(&t, dt) == NULL);
        CHECK(validate_desc(&t, da) != NULL);
        CHECK(validate_desc(&a, dt) != NULL);
        sfree(da); sfree(dt);
        random_free(rs);
    }
    {   /* a Threes description by hand: 2 1 3 / . . . / . . . / with a 1 in the corner, next: 2, bag 2 1 3 */
        game_params t = {3, 3, GOAL_ENDLESS, MODE_RECT, RULES_THREES};
        char buf[300];
        random_state *rs = random_new("y", 1);
        char *rsd = random_state_encode(rs);
        game_state *s;
        sprintf(buf, "213e1,2213,%s", rsd);
        CHECK(validate_desc(&t, buf) == NULL);
        s = new_game(NULL, &t, buf);
        CHECK(s->tiles[0] == 2 && s->tiles[1] == 1 && s->tiles[2] == 3 && s->tiles[3] == 0 && s->tiles[8] == 1);
        CHECK(s->tiles[6] == 0 && s->tiles[7] == 0 && s->tiles[5] == 0 && s->tiles[4] == 0);
        CHECK(s->next == 2 && s->bag[0] == 2 && s->bag[1] == 1 && s->bag[2] == 3);
        CHECK(s->score == 3);
        free_game(s);
        sprintf(buf, "213e1,4213,%s", rsd);
        CHECK(validate_desc(&t, buf) != NULL);          /* next tile 4 */
        sprintf(buf, "213e1,2513,%s", rsd);
        CHECK(validate_desc(&t, buf) != NULL);          /* five 1s in the bag */
        sprintf(buf, "213e1,221,%s", rsd);
        CHECK(validate_desc(&t, buf) != NULL);
        sprintf(buf, "4 3e1,2213,%s", rsd);
        CHECK(validate_desc(&t, buf) != NULL);
        sprintf(buf, "213e1,2213");
        CHECK(validate_desc(&t, buf) != NULL);
        sfree(rsd);
        random_free(rs);
    }
}

struct playstats { int games, won, finished, hints; double tsum, tmax; long moves; double score; double bigsum; };

static int bigtile_value(const game_state *s)
{
    int i, best = 0;
    for (i = 0; i < s->w * s->h; i++) if (s->tiles[i] > best) best = s->tiles[i];
    return best ? tile_number(s->rules, best) : 0;
}

/* play games with the hint (hint = true) or at random; stops at the goal if stopwon */
static void test_hint(int games, int w, int h, int goal, int rules, bool use_hint, bool stopwon,
                      int maxmoves, struct playstats *st, bool verbose)
{
    int g;
    memset(st, 0, sizeof(*st));
    for (g = 0; g < games; g++) {
        game_params p;
        game_state *s;
        char sb[40];
        int moves = 0;
        p.w = w; p.h = h; p.goal = goal; p.mode = MODE_RECT; p.rules = rules;
        sprintf(sb, "h%d-%d-%d-%d-%d", w, h, goal, rules, g);
        s = make(&p, sb, NULL);
        while (!s->finished && !(stopwon && s->won) && moves < maxmoves) {
            const char *err = NULL;
            game_state *n;
            if (use_hint) {
                double t0 = now(), dt;
                char *mv = hint_game(s, s, &err);
                dt = now() - t0;
                st->hints++; st->tsum += dt; if (dt > st->tmax) st->tmax = dt;
                if (!mv) { printf("hint gave no move: %s\n", err); fails++; break; }
                n = execute_move(s, mv);
                CHECK(n != NULL);
                sfree(mv);
            } else {
                int d, tries;
                char mv[2];
                for (tries = 0; tries < 16; tries++) {
                    d = rnd() % NDIRS;
                    if (move_possible(s->tiles, w, h, rules, d)) break;
                }
                if (tries == 16)
                    for (d = 0; d < NDIRS && !move_possible(s->tiles, w, h, rules, d); d++) ;
                mv[0] = dir_chars[d]; mv[1] = 0;
                n = execute_move(s, mv);
                CHECK(n != NULL);
            }
            if (!n) break;
            free_game(s);
            s = n;
            moves++;
        }
        st->games++;
        if (s->won) st->won++;
        if (s->finished) st->finished++;
        st->moves += moves;
        st->score += s->score;
        st->bigsum += bigtile_value(s);
        if (verbose)
            printf("   game %d: %d moves, best tile %d, score %d%s\n", g, moves, bigtile_value(s), s->score, s->won ? " WON" : s->finished ? " lost" : "");
        if (s->finished) {
            const char *err = NULL;
            char *mv = hint_game(s, s, &err);
            CHECK(mv == NULL && err != NULL);
            sfree(mv);
        }
        free_game(s);
    }
}

/* the midend with the animation: moves, timer steps, undo and redo, restart, hints */

/* ---- the geometry of the picture and where the drawing goes ---- */

static void test_geometry(void)
{
    int rules, p, w, h;
    for (rules = 0; rules < RULESCOUNT; rules++) {
        for (w = 1; w <= MAX_SIZE; w++) {
            for (h = 1; h <= MAX_SIZE; h++) {
                int lastx = 0, lasty = 0;
                for (p = 6; p <= 240; p++) {
                    struct geom G;
                    game_params q;
                    int x, y;
                    memset(&q, 0, sizeof(q));
                    q.w = w; q.h = h; q.rules = rules;
                    geom_set(&G, rules, w, h, p);
                    game_compute_size(&q, p, NULL, &x, &y);
                    CHECK(x == G.width && y == G.height);
                    /* growing with the tile size, never shrinking */
                    CHECK(x >= lastx && y >= lasty);
                    lastx = x; lasty = y;
                    /* the last tile and the gap after it end at the edge of the board */
                    CHECK(TILE_X(&G, w - 1) + G.tw + G.g == G.m + G.bw);
                    CHECK(TILE_Y(&G, h - 1) + G.th + G.g == G.top + G.m + G.bh);
                    CHECK(G.m + G.bw + G.m == G.width);
                    CHECK(G.top + G.m + G.bh + G.m == G.height);
                    CHECK(G.tw == p - G.g && G.th == G.py - G.g);
                    CHECK(G.g >= 1 && G.tw >= 1 && G.th >= 1);
                    if (p >= 16) {
                        if (rules == RULES_2048) {
                            /* the proportions of the page: gap 15 and tile 106.25 of a pitch of 121.25 */
                            CHECK(G.top == 0 && G.py == G.p);
                            CHECK(fabs((double)G.g / p - 15.0 / 121.25) < 1.0 / p);
                        } else {
                            /* upright cards (5 wide to 7 high) and a strip for the next card */
                            CHECK(G.top == p * 7 / 8);
                            CHECK(G.th * 5 >= G.tw * 6 && G.th * 5 <= G.tw * 8);
                        }
                    }
                }
            }
        }
    }
    /* the 4x4 board of 2048 at the size of the original (pitch 121.25 -> 500 by 500 units) */
    {
        struct geom G;
        geom_set(&G, RULES_2048, 4, 4, 121);
        CHECK(G.g == 15 && G.tw == 106);
        CHECK(G.bw == 499 && G.bh == 499);
    }
}

/* a drawing API that notes what is drawn and where, to see that nothing goes beyond the picture */
static int rec_w, rec_h, rec_bad, rec_ops, rec_texts, rec_clip_depth;
static int rec_cx0, rec_cy0, rec_cx1, rec_cy1;
static int rec_maxcol;

static void rec_box(const char *what, int x0, int y0, int x1, int y1, int col)
{
    /* the box is x0 <= x < x1, y0 <= y < y1 */
    rec_ops++;
    if (col > rec_maxcol) rec_maxcol = col;
    if (col < 0 || col >= NCOLOURS) { rec_bad++; printf("  %s: bad colour %d\n", what, col); }
    if (rec_clip_depth) {
        if (x0 < rec_cx0) x0 = rec_cx0;
        if (y0 < rec_cy0) y0 = rec_cy0;
        if (x1 > rec_cx1) x1 = rec_cx1;
        if (y1 > rec_cy1) y1 = rec_cy1;
        if (x0 >= x1 || y0 >= y1) return;      /* nothing visible */
    }
    if (x0 < 0 || y0 < 0 || x1 > rec_w || y1 > rec_h) {
        rec_bad++;
        if (rec_bad < 6)
            printf("  %s outside the picture: %d,%d-%d,%d of %dx%d\n", what, x0, y0, x1, y1, rec_w, rec_h);
    }
}
static void rec_text(drawing *dr, int x, int y, int ft, int fs, int al, int col, const char *t)
{
    rec_texts++;
    if (fs < 1 || fs > 1000) { rec_bad++; printf("  text size %d\n", fs); }
    if (!t || !*t) { rec_bad++; printf("  empty text\n"); }
    if (!rec_clip_depth && (x < 0 || y < 0 || x >= rec_w || y >= rec_h)) {
        rec_bad++;
        printf("  text \"%s\" at %d,%d outside %dx%d\n", t, x, y, rec_w, rec_h);
    }
    if (col < 0 || col >= NCOLOURS) { rec_bad++; printf("  text colour %d\n", col); }
}
static void rec_rect(drawing *dr, int x, int y, int w, int h, int col)
{
    if (w < 0 || h < 0) { rec_bad++; printf("  negative rect %d,%d %dx%d\n", x, y, w, h); }
    rec_box("rect", x, y, x + w, y + h, col);
}
static void rec_line(drawing *dr, int x1, int y1, int x2, int y2, int col)
{
    rec_box("line", x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2, (x1 > x2 ? x1 : x2) + 1, (y1 > y2 ? y1 : y2) + 1, col);
}
static void rec_poly(drawing *dr, const int *c, int n, int f, int o)
{
    int i, x0 = c[0], x1 = c[0], y0 = c[1], y1 = c[1];
    for (i = 1; i < n; i++) {
        if (c[2*i] < x0) x0 = c[2*i];
        if (c[2*i] > x1) x1 = c[2*i];
        if (c[2*i+1] < y0) y0 = c[2*i+1];
        if (c[2*i+1] > y1) y1 = c[2*i+1];
    }
    if (n < 3) { rec_bad++; printf("  polygon with %d points\n", n); }
    rec_box("polygon", x0, y0, x1 + 1, y1 + 1, f >= 0 ? f : o);
}
static void rec_tpoly(drawing *dr, float th, const int *c, int n, int f, int o) { rec_poly(dr, c, n, f, o); }
static void rec_circle(drawing *dr, int cx, int cy, int r, int f, int o)
{ rec_box("circle", cx - r, cy - r, cx + r + 1, cy + r + 1, f >= 0 ? f : o); }
static void rec_tcircle(drawing *dr, float th, float cx, float cy, float r, int f, int o)
{ rec_box("circle", (int)(cx - r), (int)(cy - r), (int)(cx + r) + 1, (int)(cy + r) + 1, f >= 0 ? f : o); }
static void rec_update(drawing *dr, int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || x + w > rec_w || y + h > rec_h || w <= 0 || h <= 0) {
        rec_bad++;
        printf("  update outside the picture: %d,%d %dx%d of %dx%d\n", x, y, w, h, rec_w, rec_h);
    }
}
static void rec_clip(drawing *dr, int x, int y, int w, int h)
{
    if (rec_clip_depth) { rec_bad++; printf("  clip inside clip\n"); }
    rec_clip_depth++;
    rec_cx0 = x; rec_cy0 = y; rec_cx1 = x + w; rec_cy1 = y + h;
    if (x < 0 || y < 0 || x + w > rec_w || y + h > rec_h) {
        rec_bad++;
        printf("  clip beyond the picture: %d,%d %dx%d of %dx%d\n", x, y, w, h, rec_w, rec_h);
    }
}
static void rec_unclip(drawing *dr)
{
    if (!rec_clip_depth) { rec_bad++; printf("  unclip without clip\n"); }
    rec_clip_depth = 0;
}
static void rec_start(drawing *dr) {}
static void rec_end(drawing *dr) {}
static void rec_status(drawing *dr, const char *t) {}
struct blitter { int w, h; };
static blitter *rec_bnew(drawing *dr, int w, int h) { blitter *b = snew(blitter); b->w = w; b->h = h; return b; }
static void rec_bfree(drawing *dr, blitter *b) { sfree(b); }
static void rec_bsave(drawing *dr, blitter *b, int x, int y) {}
static void rec_bload(drawing *dr, blitter *b, int x, int y) {}
static void rec_lw(drawing *dr, float w) {}
static void rec_ld(drawing *dr, bool d) {}
static char *rec_fallback(drawing *dr, const char *const *s, int n) { return dupstr(s[n - 1]); }
static void rec_changed(drawing *dr, int a, int b) {}
static void rec_purging(drawing *dr) {}
static void rec_tline(drawing *dr, float th, float x1, float y1, float x2, float y2, int col)
{ rec_line(dr, (int)x1, (int)y1, (int)x2, (int)y2, col); }
static void rec_inertia(drawing *dr, bool s) {}

static const drawing_api rec_api = {
    1, rec_text, rec_rect, rec_line, rec_poly, rec_tpoly, rec_circle, rec_tcircle, rec_update,
    rec_clip, rec_unclip, rec_start, rec_end, rec_status, rec_bnew, rec_bfree, rec_bsave, rec_bload,
    NULL, NULL, NULL, NULL, NULL, NULL,
    rec_lw, rec_ld, rec_fallback, rec_changed, rec_purging, rec_tline, rec_inertia, NULL
};

/*
 * Draws whole games on a recording API at many tile sizes: the first picture, every
 * step of every animation (forwards and backwards) and the pictures after each move.
 * Nothing may be drawn beyond the picture or with a colour that is not in the palette.
 */
static void test_drawing(int games)
{
    int g;
    for (g = 0; g < games; g++) {
        game_params p;
        game_ui *ui;
        game_drawstate *ds;
        midend *me = midend_new(NULL, &thegame, &rec_api, NULL);     /* status_bar() wants a midend */
        drawing *dr = drawing_new(&rec_api, me, NULL);
        game_state *st, *nx;
        char *aux = NULL, *desc;
        random_state *rs = random_new((char *)&g, sizeof(g));
        int tilesize = 6 + (g * 37) % 150, i, step, nmoves = 0;

        p.w = 2 + rnd() % 8; p.h = 2 + rnd() % 8;
        p.goal = GOAL_ENDLESS; p.mode = MODE_RECT;
        p.rules = g % 2 ? RULES_THREES : RULES_2048;
        if (g % 7 == 0) { p.w = 4; p.h = 4; }
        desc = new_game_desc(&p, rs, &aux, false);
        st = new_game(NULL, &p, desc);
        ui = new_ui(st);
        ui->background = g % 4;
        ui->anim = ANIM_NORMAL;
        ds = game_new_drawstate(dr, st);
        game_set_size(dr, ds, &p, tilesize);
        {
            int x, y;
            game_compute_size(&p, tilesize, ui, &x, &y);
            rec_w = x; rec_h = y;
        }
        rec_clip_depth = 0;
        game_redraw(dr, ds, NULL, st, 0, ui, 0.0F, 0.0F);
        for (step = 0; step < 14 && !st->finished; step++) {
            char mv[2];
            int d = rnd() % 4, tries;
            for (tries = 0; tries < 4 && !move_possible(st->tiles, st->w, st->h, st->rules, d); tries++)
                d = (d + 1) % 4;
            if (!move_possible(st->tiles, st->w, st->h, st->rules, d)) break;
            mv[0] = dir_chars[d]; mv[1] = 0;
            nx = execute_move(st, mv);
            CHECK(nx != NULL);
            if (!nx) break;
            nmoves++;
            for (i = 0; i <= 10; i++) {
                float dur = anim_duration(ui, st->rules);
                game_redraw(dr, ds, st, nx, +1, ui, dur * i / 10.0F, 0.0F);
            }
            game_redraw(dr, ds, NULL, nx, 0, ui, 0.0F, 0.0F);
            if (step % 3 == 2) {
                /* undo: the same animation backwards, then the older picture */
                for (i = 0; i <= 10; i++) {
                    float dur = anim_duration(ui, st->rules);
                    game_redraw(dr, ds, nx, st, -1, ui, dur * i / 10.0F, 0.0F);
                }
                game_redraw(dr, ds, NULL, st, 0, ui, 0.0F, 0.0F);
                free_game(nx);
            } else {
                free_game(st);
                st = nx;
            }
        }
        CHECK(rec_clip_depth == 0);
        CHECK(rec_bad == 0);
        if (rec_bad) {
            printf("  (game %d: rules %d, %dx%d, tile size %d, background %d)\n", g, p.rules, p.w, p.h, tilesize, ui->background);
            rec_bad = 0;
        }
        game_free_drawstate(dr, ds);
        free_ui(ui);
        free_game(st);
        sfree(aux);
        sfree(desc);
        random_free(rs);
        drawing_free(dr);
        midend_free(me);
    }
    CHECK(rec_ops > 1000 && rec_texts > 100);
}

static void test_midend(int games)
{
    int g;
    for (g = 0; g < games; g++) {
        midend *me = midend_new(NULL, &thegame, &null_drawing_api, NULL);
        game_params p;
        char *enc, id[100];
        int i, x, y, step;
        const char *err;
        float *col;
        int ncol;
        p.w = 2 + rnd() % 6; p.h = 2 + rnd() % 6; p.goal = g % 3 == 0 ? GOAL_32 : GOAL_ENDLESS; p.mode = MODE_RECT;
        p.rules = g % 2 ? RULES_THREES : RULES_2048;
        if (p.rules == RULES_THREES) p.goal = GOAL_ENDLESS;
        enc = encode_params(&p, true);
        sprintf(id, "%s#seed%d", enc, g);
        sfree(enc);
        err = midend_game_id(me, id);
        CHECK(err == NULL);
        midend_new_game(me);
        x = y = 0;
        midend_size(me, &x, &y, true, 1.0);
        CHECK(x > 0 && y > 0);
        col = midend_colours(me, &ncol);
        CHECK(ncol == NCOLOURS);
        sfree(col);
        midend_redraw(me);
        for (i = 0; i < 120; i++) {
            int k = rnd() % 12;
            if (k < 7) midend_process_key(me, 0, 0, CURSOR_UP + rnd() % 4);
            else if (k == 7) midend_process_key(me, 0, 0, 'u');
            else if (k == 8) midend_process_key(me, 0, 0, 'r' & 0x1f);
            else if (k == 9) { const char *e = midend_solve_step(me); (void)e; }
            else if (k == 10) {
                /* a swipe with the mouse */
                int sx = x / 2, sy = y / 2;
                midend_process_key(me, sx, sy, LEFT_BUTTON);
                midend_process_key(me, sx + (int)(rnd() % 400) - 200, sy + (int)(rnd() % 400) - 200, LEFT_RELEASE);
            } else midend_process_key(me, 0, 0, CURSOR_LEFT);
            for (step = 0; step < 12; step++) midend_timer(me, 0.03F);
            if (rnd() % 7 == 0) midend_force_redraw(me);
            if (rnd() % 40 == 0) { char *s = NULL; (void)s; midend_restart_game(me); }
            if (rnd() % 60 == 0) midend_new_game(me);
        }
        /* undo all the way and redo all the way, with the animation of every step */
        for (i = 0; i < 400; i++) { midend_process_key(me, 0, 0, 'u'); midend_timer(me, 0.05F); midend_timer(me, 0.05F); }
        for (i = 0; i < 400; i++) { midend_process_key(me, 0, 0, 'r' & 0x1f); midend_timer(me, 0.05F); midend_timer(me, 0.05F); }
        midend_free(me);
    }
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 10;
    const char *what = argc > 2 ? argv[2] : "all";
    struct playstats st, base;
    static const int cfg[][3] = { {4,4,GOAL_512}, {4,4,GOAL_2048}, {5,5,GOAL_2048}, {6,6,GOAL_2048}, {3,3,GOAL_64}, {2,2,GOAL_32}, {9,9,GOAL_2048}, {4,6,GOAL_1024} };
    static const int tcfg[][2] = { {4,4}, {5,5}, {3,3}, {2,2}, {6,6}, {4,6} };
    int i;
    bool unit = !strcmp(what, "all") || !strcmp(what, "unit");
    bool classic = !strcmp(what, "all") || !strcmp(what, "classic");
    bool threes = !strcmp(what, "all") || !strcmp(what, "threes");

    if (unit) {
        test_slide(20000);
        printf("slide ok (%d failures so far)\n", fails);
        test_threes_cases();
        test_threes_slide(40000);
        printf("Threes slide ok (%d failures so far)\n", fails);
        test_trace(40000);
        printf("trace ok (%d failures so far)\n", fails);
        test_bag();
        printf("bag ok (%d failures so far)\n", fails);
        test_params();
        printf("parameters ok (%d failures so far)\n", fails);
        test_random_play(80);
        printf("random play ok (%d failures so far)\n", fails);
        test_desc(60);
        printf("descriptions ok (%d failures so far)\n", fails);
        test_geometry();
        printf("geometry ok (%d failures so far)\n", fails);
        test_drawing(400);
        printf("drawing stays in the picture ok (%d failures so far)\n", fails);
        test_midend(24);
        printf("midend and animation ok (%d failures so far)\n", fails);
        fflush(stdout);
    }
    if (!strcmp(what, "one")) {
        /* bintest <n> one <w> <h> <goal> [rules [maxmoves]] */
        int w = atoi(argv[3]), h = atoi(argv[4]), goal = atoi(argv[5]);
        int rules = argc > 6 ? atoi(argv[6]) : RULES_2048;
        int maxmoves = argc > 7 ? atoi(argv[7]) : 20000;
        test_hint(n, w, h, goal, rules, true, rules == RULES_2048 && goal != GOAL_ENDLESS, maxmoves, &st, true);
        printf("%dx%d goal %d rules %d: %d/%d won, %d finished, %d hints, avg %.2f ms, max %.2f ms, avg score %.0f\n", w, h, goal, rules, st.won, st.games, st.finished, st.hints, st.hints ? st.tsum / st.hints * 1000 : 0, st.tmax * 1000, st.score / st.games);
    }
    if (classic) {
        for (i = 0; i < (int)lenof(cfg); i++) {
            int games = n;
            if (cfg[i][0] * cfg[i][1] > 36) games = n / 4 + 1;
            test_hint(games, cfg[i][0], cfg[i][1], cfg[i][2], RULES_2048, true, true, 20000, &st, false);
            printf("%dx%d goal %s: won %d/%d, game over %d, %d hints, avg %.2f ms, max %.2f ms\n", cfg[i][0], cfg[i][1], binary_goalnames[cfg[i][2]], st.won, st.games, st.finished, st.hints, st.hints ? st.tsum / st.hints * 1000 : 0, st.tmax * 1000);
            fflush(stdout);
        }
        /* Endless: the hint plays on until the game is over, and nothing is ever won */
        test_hint(3, 3, 3, GOAL_ENDLESS, RULES_2048, true, false, 20000, &st, false);
        CHECK(st.won == 0 && st.finished == st.games);
        printf("Endless 3x3: %d games played out, %.0f points on average\n", st.games, st.score / st.games);
    }
    if (threes) {
        int cap = argc > 3 ? atoi(argv[3]) : 400;          /* moves per game */
        for (i = 0; i < (int)lenof(tcfg); i++) {
            int games = n;
            if (tcfg[i][0] * tcfg[i][1] > 36) games = n / 2 + 1;
            test_hint(games, tcfg[i][0], tcfg[i][1], GOAL_ENDLESS, RULES_THREES, true, false, cap, &st, false);
            test_hint(games * 4, tcfg[i][0], tcfg[i][1], GOAL_ENDLESS, RULES_THREES, false, false, cap, &base, false);
            printf("Threes %dx%d (at most %d moves): hint: avg score %.0f, avg biggest tile %.0f, avg %.0f moves, %d finished of %d, avg %.2f ms, max %.2f ms; random: avg score %.0f, avg biggest tile %.0f, %.0f moves\n",
                   tcfg[i][0], tcfg[i][1], cap, st.score / st.games, st.bigsum / st.games, (double)st.moves / st.games, st.finished, st.games,
                   st.hints ? st.tsum / st.hints * 1000 : 0, st.tmax * 1000, base.score / base.games, base.bigsum / base.games, (double)base.moves / base.games);
            /* the hint is much better than random play (on big boards the random games last long, too) */
            CHECK(st.score / st.games > (tcfg[i][0] * tcfg[i][1] <= 25 ? 2.0 : 1.2) * base.score / base.games
                  || tcfg[i][0] * tcfg[i][1] < 9);
            fflush(stdout);
        }
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}

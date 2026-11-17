/* bintest.c: white-box tests of Binary (includes binary.c itself).
 *   bintest <seeds> [w h goalindex]
 *  1. slide_board against a plain reference of the rules of 2048 (random boards, all sizes);
 *  2. random play: the sum of the tiles, the score, the end of the game;
 *  3. game descriptions: round trips, validate_desc against damaged strings;
 *  4. hint: every move is legal; play to the goal, rate of wins and time per hint.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "binary.c"

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

static int ref_slide(unsigned char *t, int w, int h, int d)
{
    int k, i, score = 0;
    int nl = (d == DIR_L || d == DIR_R) ? h : w, len = (d == DIR_L || d == DIR_R) ? w : h;
    for (k = 0; k < nl; k++) {
        unsigned char in[16], out[16];
        for (i = 0; i < len; i++) {
            int x, y;
            switch (d) { case DIR_L: x = i; y = k; break; case DIR_R: x = w-1-i; y = k; break;
                         case DIR_U: x = k; y = i; break; default: x = k; y = h-1-i; }
            in[i] = t[y*w+x];
        }
        score += ref_line(in, out, len);
        for (i = 0; i < len; i++) {
            int x, y;
            switch (d) { case DIR_L: x = i; y = k; break; case DIR_R: x = w-1-i; y = k; break;
                         case DIR_U: x = k; y = i; break; default: x = k; y = h-1-i; }
            t[y*w+x] = out[i];
        }
    }
    return score;
}

static long tilesum(const unsigned char *t, int n)
{
    long s = 0; int i;
    for (i = 0; i < n; i++) if (t[i]) s += POWER[t[i]];
    return s;
}

static void test_slide(int rounds)
{
    int r;
    unsigned char a[81], b[81], orig[81];
    for (r = 0; r < rounds; r++) {
        int w = 2 + rnd() % 8, h = 2 + rnd() % 8, n = w * h, i, d, sa = 0, sb;
        int maxe = 1 + rnd() % 15, fill = rnd() % 100;
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
    {   /* two 2^15 do not merge (and so can not make a tile that is out of range) */
        unsigned char l[2] = { 15, 15 };
        int sc = 0;
        CHECK(!slide_board(l, 2, 1, DIR_L, &sc));
    }
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

static void test_random_play(int games)
{
    int g;
    for (g = 0; g < games; g++) {
        game_params p;
        game_state *s;
        char sb[32];
        int moves = 0, nfinish = 0;
        p.w = 2 + rnd() % 8; p.h = 2 + rnd() % 8; p.goal = rnd() % GOALCOUNT; p.mode = MODE_RECT;
        CHECK(validate_params(&p, true) == NULL);
        sprintf(sb, "t%d", g);
        s = make(&p, sb, NULL);
        CHECK(tilesum(s->tiles, p.w * p.h) >= 4 && tilesum(s->tiles, p.w * p.h) <= 8);
        while (!s->finished && moves < 4000) {
            char mv[2];
            int d, tries;
            game_state *n;
            long before = tilesum(s->tiles, p.w * p.h), after;
            int score0 = s->score;
            for (tries = 0; tries < 16; tries++) {
                d = rnd() % NDIRS;
                if (move_possible(s->tiles, p.w, p.h, d)) break;
            }
            if (tries == 16) {
                /* every direction by hand */
                for (d = 0; d < NDIRS && !move_possible(s->tiles, p.w, p.h, d); d++) ;
                if (d == NDIRS) { CHECK(0); break; }
            }
            mv[0] = dir_chars[d]; mv[1] = 0;
            n = execute_move(s, mv);
            CHECK(n != NULL);
            if (!n) break;
            after = tilesum(n->tiles, p.w * p.h);
            /* a tile of 2 or 4 turned up; the merges keep the sum */
            CHECK(after - before == 2 || after - before == 4);
            CHECK(n->score >= score0);
            CHECK(n->finished == !moves_possible(n->tiles, p.w, p.h) || n->finished);
            CHECK(game_status(n) == (n->finished ? (n->won ? 1 : -1) : 0));
            /* the old state is untouched */
            CHECK(tilesum(s->tiles, p.w * p.h) == before);
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
        sprintf(sb, "d%d", i);
        s = make(&p, sb, &desc);
        enc = encode_params(&p, true);
        decode_params(&q, enc);
        CHECK(q.w == p.w && q.h == p.h && q.goal == p.goal && q.mode == p.mode);
        sfree(enc);
        /* same description -> same game */
        {
            game_state *s2 = new_game(NULL, &p, desc);
            CHECK(memcmp(s->tiles, s2->tiles, p.w * p.h) == 0);
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
    {   /* a game id without the goal part has the default goal (2048) */
        game_params q;
        decode_params(&q, "5x5");
        CHECK(q.w == 5 && q.h == 5 && q.goal == GOAL_2048);
        decode_params(&q, "4");
        CHECK(q.w == 4 && q.h == 4 && q.goal == GOAL_2048);
        decode_params(&q, "4x4g3mR");
        CHECK(q.goal == GOAL_128);
    }
}

struct playstats { int games, won, finished, hints; double tsum, tmax; long moves; };

static void test_hint(int games, int w, int h, int goal, struct playstats *st, bool verbose)
{
    int g;
    memset(st, 0, sizeof(*st));
    for (g = 0; g < games; g++) {
        game_params p;
        game_state *s;
        char sb[32];
        int moves = 0;
        p.w = w; p.h = h; p.goal = goal; p.mode = MODE_RECT;
        sprintf(sb, "h%d-%d-%d-%d", w, h, goal, g);
        s = make(&p, sb, NULL);
        while (!s->finished && !s->won && moves < 20000) {
            const char *err = NULL;
            double t0 = now(), dt;
            char *mv = hint_game(s, s, &err);
            game_state *n;
            dt = now() - t0;
            st->hints++; st->tsum += dt; if (dt > st->tmax) st->tmax = dt;
            if (!mv) { printf("hint gave no move: %s\n", err); fails++; break; }
            n = execute_move(s, mv);
            CHECK(n != NULL);
            sfree(mv);
            if (!n) break;
            free_game(s);
            s = n;
            moves++;
        }
        st->games++;
        if (s->won) st->won++;
        if (s->finished) st->finished++;
        st->moves += moves;
        if (verbose) {
            int i, best = 0;
            for (i = 0; i < w * h; i++) if (s->tiles[i] > best) best = s->tiles[i];
            printf("   game %d: %d moves, best tile %d, score %d%s\n", g, moves, POWER[best], s->score, s->won ? " WON" : s->finished ? " lost" : "");
        }
        if (s->finished) {
            const char *err = NULL;
            char *mv = hint_game(s, s, &err);
            CHECK(mv == NULL && err != NULL);
            sfree(mv);
        }
        free_game(s);
    }
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 10;
    struct playstats st;
    static const int cfg[][3] = { {4,4,GOAL_512}, {4,4,GOAL_2048}, {5,5,GOAL_2048}, {6,6,GOAL_2048}, {3,3,GOAL_64}, {2,2,GOAL_32}, {9,9,GOAL_2048}, {4,6,GOAL_1024} };
    int i;

    test_slide(20000);
    printf("slide ok (%d failures so far)\n", fails);
    test_random_play(60);
    printf("random play ok (%d failures so far)\n", fails);
    test_desc(40);
    printf("descriptions ok (%d failures so far)\n", fails);
    if (argc > 4) {
        test_hint(n, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), &st, true);
        printf("%dx%d goal %d: %d/%d won, %d finished, %d hints, avg %.2f ms, max %.2f ms\n", atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), st.won, st.games, st.finished, st.hints, st.hints ? st.tsum / st.hints * 1000 : 0, st.tmax * 1000);
    } else {
        for (i = 0; i < (int)lenof(cfg); i++) {
            int games = n;
            if (cfg[i][0] * cfg[i][1] > 36) games = n / 4 + 1;
            test_hint(games, cfg[i][0], cfg[i][1], cfg[i][2], &st, false);
            printf("%dx%d goal %s: won %d/%d, game over %d, %d hints, avg %.2f ms, max %.2f ms\n", cfg[i][0], cfg[i][1], binary_goalnames[cfg[i][2]], st.won, st.games, st.finished, st.hints, st.hints ? st.tsum / st.hints * 1000 : 0, st.tmax * 1000);
            fflush(stdout);
        }
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}

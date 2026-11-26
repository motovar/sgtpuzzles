/* moscogen.c: how well does the Mosco generator do?  (includes mosco.c itself)
 *   moscogen <w> <h> <level 0|1|2> <seeds> [count]
 *
 * Every puzzle is made, then checked by a solver of its own that knows
 * nothing about how it was made: at the level of the puzzle it has to come to
 * the arrangement the puzzle was made from, and for Hard the level Normal has
 * to fail (the puzzle needs a "what if").  With count, the number of
 * solutions is also counted (up to 2) by a search that doesn't use any of
 * the narrowing of the levels, as far as it can within a number of guesses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define MOSCO_DEBUG
#include "mosco.c"

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* the number of solutions, up to limit; -1 if it took too many guesses */
static long count_sols(struct msolver *sv, struct sstate *ss, long limit, long *budget)
{
    int t, best = -1, bestn = NDIR + 1, v;
    long total = 0;
    for (t = 0; t < sv->n; t++) {
        unsigned dom = ss->dom[t];
        if (dom & (dom - 1)) {
            int pc = popcnt8(dom);
            if (pc < bestn) { best = t; bestn = pc; }
        }
    }
    if (best < 0) return 1;
    for (v = 0; v < NDIR; v++) {
        struct sstate *child;
        long r;
        if (!(ss->dom[best] & (1u << v))) continue;
        if (--*budget < 0) return -1;
        child = ss_dup(sv, ss);
        if (ss_assign(sv, child, best, v) && ss_propagate(sv, child)) {
            r = count_sols(sv, child, limit - total, budget);
            if (r < 0) { ss_free(child); return -1; }
            total += r;
        } else sv_drain(sv);
        ss_free(child);
        if (total >= limit) return total;
    }
    return total;
}

int main(int argc, char **argv)
{
    game_params p;
    int nseeds, seed, docount = argc > 5;
    int good = 0, bad = 0, easyhard = 0, uniq = 0, counted = 0, multi = 0;
    double tsum = 0, tmax = 0;
    long maxclue_sum = 0, zeros_sum = 0;
    int maxclue_max = 0;

    if (argc < 5) { printf("usage: moscogen w h level seeds [count]\n"); return 2; }
    if (getenv("CORE_EASY")) dbg_core_easy = atoi(getenv("CORE_EASY"));
    if (getenv("CORE_HARD")) dbg_core_hard = atoi(getenv("CORE_HARD"));
    if (getenv("CORE_ITERS")) dbg_core_iters = atoi(getenv("CORE_ITERS"));
    if (getenv("CORE_GOES")) dbg_core_goes = atoi(getenv("CORE_GOES"));
    p.w = atoi(argv[1]);
    p.h = atoi(argv[2]);
    p.diff = atoi(argv[3]);
    nseeds = atoi(argv[4]);
    if (validate_params(&p, true)) { printf("bad params: %s\n", validate_params(&p, true)); return 2; }

    for (seed = 1; seed <= nseeds; seed++) {
        char sb[32], *aux, *desc;
        random_state *rs;
        int n = p.w * p.h, k = 2 * p.w + 2 * p.h + 4, i;
        int *vals = snewn(k, int);
        signed char *sol = snewn(n, signed char);
        struct mosco_clues *cl;
        struct msolver *sv;
        struct sstate *s1, *s2;
        double t0, dt;
        int res, mx = 0, zeros = 0;
        bool match = true;

        sprintf(sb, "%d", seed * 7919 + p.w * 31 + p.h * 17 + p.diff);
        rs = random_new(sb, strlen(sb));
        t0 = now();
        desc = new_game_desc(&p, rs, &aux, false);
        dt = now() - t0;
        tsum += dt;
        if (dt > tmax) tmax = dt;
        if (validate_desc(&p, desc)) { printf("seed %d: bad desc: %s\n", seed, validate_desc(&p, desc)); return 1; }
        parse_clues(&p, desc, vals);
        if (!parse_aux(aux, n, sol)) { printf("seed %d: bad aux\n", seed); return 1; }
        for (i = 0; i < k; i++) { if (vals[i] > mx) mx = vals[i]; if (!vals[i]) zeros++; }
        maxclue_sum += mx;
        zeros_sum += zeros;
        if (mx > maxclue_max) maxclue_max = mx;

        cl = clues_new(p.w, p.h, vals);
        sv = msolver_new(cl);
        s1 = ss_new(sv);
        s2 = ss_new(sv);
        ss_reset(sv, s1);
        res = solve_level(sv, s1, s2, p.diff);
        if (res == 1) {
            for (i = 0; i < n; i++) if (lowbit(s1->dom[i]) != sol[i]) match = false;
        }
        if (res == 1 && match) {
            good++;
            if (p.diff >= DIFF_NORMAL) {
                /* does the level below get it?  (it must not) */
                ss_reset(sv, s1);
                if (solve_level(sv, s1, s2, p.diff - 1) == 1) easyhard++;
            }
        } else {
            bad++;
            if (bad <= 5) printf("seed %d: level solver result %d match %d (fallback?)\n", seed, res, (int)match);
        }
        if (docount && n <= 40) {
            struct sstate *c = ss_new(sv);
            long budget = 3000000, r;
            ss_reset(sv, c);
            if (ss_propagate(sv, c)) {
                r = count_sols(sv, c, 2, &budget);
                if (r >= 0) { counted++; if (r == 1) uniq++; else multi++; }
            }
            ss_free(c);
        }
        ss_free(s1);
        ss_free(s2);
        msolver_free(sv);
        clues_free(cl);
        sfree(vals); sfree(sol); sfree(desc); sfree(aux);
        random_free(rs);
    }
    printf("stats: blocks %ld toobig %ld infeasible %ld nobalance %ld coresearch %ld (avg core %.1f) corefail %ld verifyfail %ld found %ld\n",
           mstats.blocks, mstats.toobig, mstats.infeasible, mstats.nobalance, mstats.coresearch,
           mstats.coresearch ? (double)mstats.coresum / mstats.coresearch : 0.0, mstats.corefail, mstats.verifyfail, mstats.found);
    printf("%dx%d %s: %d puzzles, level solver gets it right %d (fails %d%s); avg %.3fs max %.3fs; "
           "avg max clue %.1f (largest %d), avg zeros %.1f of %d",
           p.w, p.h, diff_names[p.diff], nseeds, good, bad,
           p.diff == DIFF_HARD ? "" : "", tsum / nseeds, tmax,
           (double)maxclue_sum / nseeds, maxclue_max, (double)zeros_sum / nseeds, 2 * p.w + 2 * p.h + 4);
    if (p.diff >= DIFF_NORMAL) printf("; the level below also solves %d", easyhard);
    if (docount) printf("; counted %d: unique %d, several %d", counted, uniq, multi);
    printf("\n");
    return bad ? 1 : 0;
}

/*
 * shiftsearch.h: bidirectional breadth-first search over arrangements of
 * a small array of bytes under a set of permutation moves.  Used for the
 * "show next step" hints of Sixteen, Twiddle and Netslide.
 *
 * Every move m is a permutation: new[i] = old[perm[m][i]].  inv[m] is the
 * index of the move that undoes it.
 */
#ifndef SHIFTSEARCH_H
#define SHIFTSEARCH_H

struct ss_side {
    unsigned char *st;    /* node states, n bytes each */
    unsigned char *mv;    /* forward: first move; backward: move to target */
    int *tab;             /* hash table of node index + 1 */
    size_t tabcap;
    int count, cap;
    int *front, nfront, *next, nnext;
};

static unsigned long long ss_hash(const unsigned char *s, int n)
{
    unsigned long long h = 1469598103934665603ULL;
    int i;
    for (i = 0; i < n; i++) { h ^= s[i]; h *= 1099511628211ULL; }
    return h;
}

static int ss_find(struct ss_side *sd, const unsigned char *s, int n)
{
    size_t i = (size_t)(ss_hash(s, n) % sd->tabcap);
    while (sd->tab[i]) {
        if (!memcmp(sd->st + (size_t)(sd->tab[i]-1) * n, s, n))
            return sd->tab[i]-1;
        i = (i + 1) % sd->tabcap;
    }
    return -1;
}

static void ss_rehash(struct ss_side *sd, int n)
{
    int k; size_t i;
    sfree(sd->tab);
    sd->tabcap *= 2;
    sd->tab = snewn(sd->tabcap, int);
    memset(sd->tab, 0, sd->tabcap * sizeof(int));
    for (k = 0; k < sd->count; k++) {
        i = (size_t)(ss_hash(sd->st + (size_t)k*n, n) % sd->tabcap);
        while (sd->tab[i]) i = (i + 1) % sd->tabcap;
        sd->tab[i] = k+1;
    }
}

static int ss_add(struct ss_side *sd, const unsigned char *s, int n,
                  unsigned char mv)
{
    size_t i;
    if (sd->count >= sd->cap) {
        sd->cap *= 2;
        sd->st = sresize(sd->st, (size_t)sd->cap * n, unsigned char);
        sd->mv = sresize(sd->mv, sd->cap, unsigned char);
    }
    if ((size_t)(sd->count + 1) * 2 > sd->tabcap) ss_rehash(sd, n);
    memcpy(sd->st + (size_t)sd->count * n, s, n);
    sd->mv[sd->count] = mv;
    i = (size_t)(ss_hash(s, n) % sd->tabcap);
    while (sd->tab[i]) i = (i + 1) % sd->tabcap;
    sd->tab[i] = ++sd->count;
    return sd->count - 1;
}

static void ss_init(struct ss_side *sd, int n, const unsigned char *root)
{
    sd->cap = 1024;
    sd->st = snewn((size_t)sd->cap * n, unsigned char);
    sd->mv = snewn(sd->cap, unsigned char);
    sd->tabcap = 4096;
    sd->tab = snewn(sd->tabcap, int);
    memset(sd->tab, 0, sd->tabcap * sizeof(int));
    sd->count = 0;
    sd->front = snewn(1, int); sd->next = snewn(1, int);
    sd->nfront = 1; sd->nnext = 0;
    ss_add(sd, root, n, 255);
    sd->front[0] = 0;
}

static void ss_free(struct ss_side *sd)
{
    sfree(sd->st); sfree(sd->mv); sfree(sd->tab);
    sfree(sd->front); sfree(sd->next);
}

/* Returns index of first move to make from start, -1 if not found within
 * budget, -2 if start already equals target. */
static int ss_search(int n, const unsigned char *start,
                     const unsigned char *target, int nm,
                     unsigned char **perm, const int *inv, long budget)
{
    struct ss_side sd[2];
    unsigned char *c = snewn(n, unsigned char);
    int result = -1, k, m, i, s;
    if (!memcmp(start, target, n)) { sfree(c); return -2; }
    ss_init(&sd[0], n, start);
    ss_init(&sd[1], n, target);
    while (result == -1 && (sd[0].nfront || sd[1].nfront) &&
           (long)(sd[0].count + sd[1].count) < budget) {
        struct ss_side *me, *ot;
        int bwd;
        if (!sd[0].nfront) bwd = 1;
        else if (!sd[1].nfront) bwd = 0;
        else bwd = sd[1].nfront < sd[0].nfront;
        me = &sd[bwd]; ot = &sd[1-bwd];
        me->next = sresize(me->next, (size_t)me->nfront * nm + 1, int);
        me->nnext = 0;
        for (k = 0; k < me->nfront && result == -1; k++) {
            int p = me->front[k];
            for (m = 0; m < nm; m++) {
                int o;
                unsigned char mvv;
                for (i = 0; i < n; i++)
                    c[i] = me->st[(size_t)p*n + perm[m][i]];
                if (ss_find(me, c, n) >= 0) continue;
                if (!bwd)
                    mvv = (me->mv[p] == 255) ? m : me->mv[p];
                else
                    mvv = inv[m];
                o = ss_find(ot, c, n);
                if (o >= 0) {
                    if (!bwd) result = mvv;
                    else result = (o == 0) ? mvv : ot->mv[o];
                    break;
                }
                s = ss_add(me, c, n, mvv);
                me->next[me->nnext++] = s;
            }
        }
        {
            int *t = me->front; me->front = me->next; me->next = t;
            me->nfront = me->nnext;
        }
    }
    ss_free(&sd[0]); ss_free(&sd[1]); sfree(c);
    return result;
}
#endif

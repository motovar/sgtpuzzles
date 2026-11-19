#!/bin/sh
# Makes orig_gen.c, the file that tests/mfgen.c compares the generator of Minor Finder with.
#
#   sh tests/mkorig.sh <minorfinder.c of the original> > orig_gen.c
#
# The original is https://github.com/elsamuray7/Minorfinder, branch isomtest-srchtree-optim (dc9580c).
# The generator is used as it is.  Two things change: the loop that places the points of the big graph
# gets a guard against writing past the end of the array when the number of points of the big graph is
# a multiple of the number of points of the minor (without it the original overruns its array, found
# with AddressSanitizer), and the "struct game" at the end is replaced by a function orig_gen() that
# calls new_game_desc().
set -e
src=$1
[ -f "$src" ] || { echo "usage: $0 <original minorfinder.c>" >&2; exit 1; }
last=$(grep -n '^#ifdef COMBINED' "$src" | tail -1 | cut -d: -f1)
guard=$(grep -n 'for (i = 0; i < tmp; i++)' "$src" | sed -n 2p | cut -d: -f1)
[ -n "$last" ] && [ -n "$guard" ] || { echo "$0: unexpected source" >&2; exit 1; }
head -n $((last - 1)) "$src" | sed "${guard}s/for (i = 0; i < tmp; i++)/for (i = 0; i < tmp \&\& tmp2 < n_base; i++)/"
echo
cat <<'EOT'
char *orig_gen(const game_params *params, random_state *rs, char **aux)
{
    return new_game_desc(params, rs, aux, false);
}
EOT

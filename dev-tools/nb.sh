#!/bin/sh
# Native test build helper for the extra puzzles.
#   sh nb.sh core                 build the core objects (o/*.o) once
#   sh nb.sh prog <game> <src.c> <test.c> <out> [extra flags]
#        compiles <src.c> (the game, with -DCOMBINED so that "thegame" is called <game>) and
#        links it with <test.c> and the core into b/<out>
# ASAN=1 uses the sanitizers (objects in o_asan/, binaries get the suffix _asan).
J=${JNI:-/home/claude/sgtpuzzles/app/src/main/jni}
H=/home/claude/work/scratch
JV=/usr/lib/jvm/java-21-openjdk-amd64/include
D="-DANDROID -DCOMBINED -DSMALL_SCREEN -DSTYLUS_BASED -DNO_PRINTING -DVIVID_COLOURS"
if [ -n "$ASAN" ]; then OPT="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"; OD=$H/o_asan; SUF=_asan; else OPT="-g -O2"; OD=$H/o; SUF=""; fi
mkdir -p $OD
CORE="random malloc misc drawing tree234 sort midend dsf findloop combi"
cmd=$1; shift
case $cmd in
core)
  for f in $CORE; do
    [ -f $J/$f.c ] || continue
    gcc $OPT -std=gnu99 -w $D -I$H/inc -I$J -I$JV -I$JV/linux -c $J/$f.c -o $OD/$f.o || exit 1
  done
  gcc $OPT -std=gnu99 -w $D -I$H/inc -I$J -I$JV -I$JV/linux -c $H/stubs.c -o $OD/stubs.o || exit 1
  echo core built ;;
prog)
  game=$1; src=$2; test=$3; out=$4; shift 4
  mkdir -p $H/inc/$game
  echo "GAME($game)" > $H/inc/$game/generated-games.h
  gcc $OPT -std=gnu99 -w $D -fsigned-char -I$H/inc/$game -I$J -I$JV -I$JV/linux "$@" -c $src -o $OD/$game.game.o || exit 1
  XO=""; xi=0
  for xs in $XSRC; do
    xi=$((xi+1))
    gcc $OPT -std=gnu99 -w $D -fsigned-char -I$H/inc/$game -I$J -I$JV -I$JV/linux "$@" -c $xs -o $OD/$game.x$xi.o || exit 1
    XO="$XO $OD/$game.x$xi.o"
  done
  gcc $OPT -std=gnu99 -w $D -DGAMESYM=$game -DGAMENAME=\"$game\" -I$H/inc/$game -I$J -I$JV -I$JV/linux "$@" -c $test -o $OD/$game.test.o || exit 1
  gcc $OPT -o $H/b/$out$SUF $OD/$game.test.o $OD/$game.game.o $XO $(for f in $CORE stubs; do [ -f $OD/$f.o ] && echo $OD/$f.o; done) -lm 2>&1 | head -30
  [ -x $H/b/$out$SUF ] && echo built b/$out$SUF ;;
esac

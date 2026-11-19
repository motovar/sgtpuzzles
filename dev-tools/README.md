# Test and icon tools used while adding the extra games

Not part of the app. Everything runs on a normal Linux machine with gcc (and ASAN/UBSAN, valgrind, Python 3
with Pillow for the icons).

* `nb.sh core` builds the shared objects (midend, random, malloc, ...) from `app/src/main/jni` into `o/`
  (`ASAN=1` for the sanitizer build in `o_asan/`).
* `nb.sh prog <game> <game.c> <test.c> <out> [flags]` builds one game with a test program into `b/<out>`;
  `XSRC="other.c ..."` adds more source files for games with several (Maze 3D, Binary, ...).
* `stubs.c` - front end stubs, `null_drawing_api`, deterministic random seeds for the midend
  (`STUBSEED=<n>`, or `STUBSEED=time`).
* `tests/smoke.c` - plays each preset and a number of seeds with the hint until the game is solved, also tries Solve.
* `tests/evfuzz.c` - random key/mouse/undo/redo/restart/serialise/new game events (use with ASAN; the leak in
  `newgame_undo_deserialise_check` of the midend is in `lsan.supp`).
* `tests/bintest.c`, `tests/moscotest.c` (+ `moscosa*.c`, `moscoexp.c`), `tests/dectest.c` - white-box tests of Binary,
  Mosco and Decanting: they `#include` the game source (build with `tests/empty.c` as the game file and
  `-I<game directory>`). `dectest <scale> [mode]` compares the Decanting solver with an exhaustive search, replays
  backward scrambles, fuzzes descriptions and moves, and checks generator and hint times.
* `m3dtest/` - wall symmetry / reachability test for the Maze 3D generator.
* `icons/icontool.c` + `icons/render.py` - records what a game draws and renders the day and night icons.
* Minor Finder (`tests/mf*.c`; they `#include` the game, so build them with `tests/empty.c` as the game file and
  `-I<app>/app/src/main/jni/extras-holderbach`; `JNI=<app>/app/src/main/jni` if this directory is not inside the app):
  - `mftest` - graph operations against a plain adjacency matrix, the solver against brute force over all
    labellings and against the rules of the game themselves (contract an edge, delete an edge, delete a point), planted
    instances (a minor that is known to be there), and the hints of 260 generated games, every sequence of which must
    end in a win. `MF_VERBOSE=1` also re-solves the instances that the solver gave up on with 300 million nodes.
  - `mfgen <seeds> [first seed]` - the generator against the original one, game by game and for every combination of
    parameters (the descriptions must be identical), and the check that every game it makes is valid and good. Needs
    `orig_gen.c`: `sh tests/mkorig.sh <original minorfinder.c> > orig_gen.c`, then
    `XSRC=orig_gen.c sh nb.sh prog minorfinder tests/empty.c tests/mfgen.c mfgen -I<...>/extras-holderbach -DUI_UPDATE=MOVE_UI_UPDATE`.
  - `mfperf <seeds> [node limit] [damage] [only n_base]` - how hard the first hint of a game is (nodes, time), also for
    positions after random damage; `mfui [rounds]` - gestures in pixels through `interpret_move` (a drag, two taps, long
    press, release outside the window, ...): what comes out has to be what the player meant, and always a move that
    `execute_move` accepts; `mfconn` - the minors of the generated games are connected; `mfspecial` - the K5 / K3,3 modes.
* `icons/mf_search.py`, `mf_icon.py`, `mf_staged.py` - pick a good game and make the ten icons of Minor Finder
  (`mf_staged.py make <res directory>`); they run `b/icontool_minorfinder` (or `$ICONTOOL`).

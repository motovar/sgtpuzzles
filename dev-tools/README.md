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
* `m3dtest/` - wall symmetry / reachability test for the Maze 3D generator.
* `icons/icontool.c` + `icons/render.py` - records what a game draws and renders the day and night icons.

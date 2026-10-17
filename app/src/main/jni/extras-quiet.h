/* Force-included into the extra puzzles: the generator process writes the saved game to stdout,
 * so stray debug printf() calls in ported code would corrupt it. */
#include <stdio.h>
#include <stdarg.h>
#define printf(...) ((void)0)
#define vprintf(f, a) ((void)0)
#define puts(s) ((void)0)
#define putchar(c) ((void)0)

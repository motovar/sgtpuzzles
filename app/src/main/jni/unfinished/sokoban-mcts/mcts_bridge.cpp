// C entry point around lkseg/sokoban-level-generator-mcts, used by sokoban.c.
#include "mcts.h"
#include "allocator.h"

extern "C" int sokoban_mcts_generate(int width, int height, unsigned long long seed,
                                     double timeout_sec, int keep, int pick, unsigned rnd,
                                     char *out, int outcap)
{
    if (width < 1 || height < 1 || width * height < 16 || width * height > 4000)
        return 0;

    Default_Allocator default_allocator;
    global_default_allocator = &default_allocator;
    global_allocator = global_default_allocator;
    Arena_Allocator arena_allocator = make_arena_allocator(global_default_allocator);
    global_arena_allocator = &arena_allocator;

    Mcts *mcts = new_mcts((u64) seed, Vector2i{width, height}, Vector2i{-1, 0});
    mcts->start();

    const Chrono_Clock t0 = get_time();
    for (;;) {
        mcts->next_rollout(node_ucb1_tuned);
        if (time_diff(t0, get_time()) >= timeout_sec) break;
    }
    global_allocator = global_default_allocator;

    int ok = 0;
    if (mcts->finished_nodes.count >= 1) {
        Array<Level> set = mcts->get_level_set(keep < 1 ? 1 : keep);
        if (set.count >= 1) {
            /* the set is sorted from the lowest to the highest score */
            /* pick = rank from the hardest (1) to the easiest */
            int sel = (int) set.count - (pick < 1 ? 1 : pick);
            if (sel < 0) sel = 0;
            (void) rnd;
            String s = str(set[sel].grid);
            if (s.count + 1 <= outcap) {
                for (isize i = 0; i < s.count; i++) out[i] = (char) s.data[i];
                out[s.count] = 0;
                ok = 1;
            }
            s.destroy();
        }
    }
    delete_mcts(mcts);
    arena_allocator.destroy();
    global_arena_allocator = nullptr;
    global_allocator = nullptr;
    global_default_allocator = nullptr;
    return ok;
}

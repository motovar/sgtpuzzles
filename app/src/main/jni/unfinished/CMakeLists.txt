puzzle(group
  DISPLAYNAME "Group"
  DESCRIPTION "Group theory puzzle"
  OBJECTIVE "Complete the unfinished Cayley table of a group.")
solver(group ${CMAKE_SOURCE_DIR}/latin.c)

puzzle(separate
  DISPLAYNAME "Separate"
  DESCRIPTION "Rectangle-dividing puzzle"
  OBJECTIVE "Partition the grid into regions containing one of each letter.")

puzzle(slide
  DISPLAYNAME "Slide"
  DESCRIPTION "Sliding block puzzle"
  OBJECTIVE "Slide the blocks to let the key block out.")
solver(slide)

puzzle(sokoban
  DISPLAYNAME "Sokoban"
  DESCRIPTION "Barrel-pushing puzzle"
  OBJECTIVE "Push all the barrels into the target squares.")

# Sokoban's YASGen level generator (plain C).
set(puzzle_sources ${puzzle_sources}
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-yasgen/yasgen.c)

# Sokoban's MCTS level generator is written in C++.
set(puzzle_sources ${puzzle_sources}
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/mcts_bridge.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/allocator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/mcts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/mcts_util.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/sokoban.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/util.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/xmath.cpp)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/mcts_bridge.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/allocator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/mcts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/mcts_util.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/sokoban.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/util.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts/xmath.cpp
  PROPERTIES COMPILE_OPTIONS "-O2;-std=c++17;-Wno-unused-value;-Wno-deprecated;-I${CMAKE_CURRENT_SOURCE_DIR}/sokoban-mcts")

# These unfinished programs don't even have the structure of a puzzle
# game yet; they're just command-line programs containing test
# implementations of some of the needed functionality.

cliprogram(numgame numgame.c)

cliprogram(path path.c COMPILE_DEFINITIONS TEST_GEN)

export_variables_to_parent_scope()

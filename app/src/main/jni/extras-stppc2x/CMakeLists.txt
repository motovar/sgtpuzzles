# Puzzles ported from Lee Dowling's stppc2x collection (Simon Tatham's puzzles for the
# Sharp Zaurus / Pocket PC and similar handhelds): https://github.com/ledow/stppc2x

puzzle(maze3d
  DISPLAYNAME "Maze 3D"
  DESCRIPTION "First-person three-dimensional maze"
  OBJECTIVE "Find your way through the three-dimensional maze to the target.")

# The maze library that Maze 3D is built on.
set(puzzle_sources ${puzzle_sources} ${CMAKE_CURRENT_SOURCE_DIR}/maze3dc.c)

export_variables_to_parent_scope()

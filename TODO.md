# TODO

## C Side (`extension.c`)

1.  **`world_raycast` Naming:** The `world_raycast` function should be renamed to something more descriptive, like `world_check_clear_line`.
2.  **Splitting Bodies:** When destroying shapes, the affected body should be split into multiple bodies (one body per each remaining shape). Shape positions and "masses" must not change. For example, an I-shape with one shape removed, leaving three remaining shapes, should be split into three separate, unconnected boxes.

## Ruby Side (`main.rb`)

1.  **Improve Rendering:** Improve rendering of destroyed shapes to show a nice "puff" effect.

## Current Issues and Future Work:

1.  **`world_raycast` Naming:** The `world_raycast` function should be renamed to something more descriptive, like `world_check_clear_line`.
2.  **Tolerance Values:** We need to experiment with different `vertical_tolerance` values.
3.  **Angle Checks:** We should check that the bodies / their constituent shapes are roughly horizontal in orientation.
4.  **Feel:** we should check what angles of horizontal rows are okay; now in the test level it's easy to create rows that _feel_ like they should be okay. Maybe a bit more raycasts?
5.  **Bugs:** restart does not work after game over (immediate game over)

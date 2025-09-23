# TODO

## C Side (`extension.c`)

1.  **`world_raycast` Naming:** The `world_raycast` function should be renamed to something more descriptive, like `world_check_clear_line`.

## Ruby Side (`main.rb`)

1.  **Improve Rendering:** Improve rendering of destroyed shapes to show a nice "puff" effect.
2.  **Move game logic to Ruby:** Alternatively, we could define Shape classes as needed and move all the game logic implementation into the ruby side; i.e only leave the core raycasting in native code (and still call the func world_raycast); return shapes to Ruby; and determine all line clearing logic in ruby side (-> faster iteration, cleaner separation). 

## Current Issues and Future Work:

1.  **`world_raycast` Naming:** The `world_raycast` function should be renamed to something more descriptive, like `world_check_clear_line`.
2.  **Angle Checks:** We could check that the bodies / their constituent shapes are roughly horizontal in orientation.
3.  **Feel:** we should check what angles of horizontal rows are okay; now in the test level it's easy to create rows that _feel_ like they should be okay. Maybe a bit more raycasts?

# Using Box2D in DragonRuby

This document summarizes how we use Box2D v3 in this project, how it's integrated with the DragonRuby runtime, and how to use the C extension to create Box2D objects.

## Box2D v3 Concepts

This project uses Box2D v3, which is a C implementation of the Box2D physics engine. Here are some key concepts:

*   **World:** The physics world where all bodies live. It manages the simulation and collision detection.
*   **Body:** A rigid body with properties like position, velocity, and mass. Bodies can be static, kinematic, or dynamic.
*   **Shape:** A 2D geometric object attached to a body. Shapes are used for collision detection. Box2D v3 supports various shapes, including polygons, circles, capsules, and chain shapes.
*   **Chain Shape:** A chain shape is a sequence of line segments. It's ideal for creating complex terrain or boundaries in a level. Chain shapes can be open (a "fan") or closed (a "loop").

## DragonRuby Integration via C Extension

We use a C extension to integrate Box2D with the DragonRuby runtime. The C extension is located in `mygame/app/extension.c`. It exposes Box2D functionality to the Ruby environment.

Here's how the integration works:

1.  **C Functions:** The C extension defines functions that wrap Box2D functionality. For example, `world_create_body` creates a new Box2D body.
2.  **mruby API:** The C functions use the mruby C API to interact with the Ruby environment. They take Ruby objects as input and return Ruby objects as output.
3.  **`drb_api`:** We use the `drb_api` struct to call mruby C API functions. This is necessary for cross-platform compatibility.
4.  **Registration:** The C functions are registered with the mruby runtime in the `drb_register_c_extensions_with_api` function. This makes them available as methods on Ruby objects.

## Using the C Extension

The C extension exposes a `FFI::Box2D` module in Ruby. This module contains the `World` and `Body` classes.

### Creating a World

To create a new Box2D world, you can use the `FFI::Box2D::World.new` method:

```ruby
args.state.world = FFI::Box2D::World.new(args.grid.w, args.grid.h)
```

### Creating a Body

To create a new body, you can use the `create_body` method on the world object:

```ruby
body = args.state.world.create_body("dynamic", x, y)
```

The first argument is the body type (`"static"`, `"kinematic"`, or `"dynamic"`), and the next two arguments are the initial x and y coordinates.

### Creating Shapes

Once you have a body, you can add shapes to it.

#### Box Shape

To create a box shape, you can use the `create_box_shape` method on the body object:

```ruby
body.create_box_shape(width, height, density)
```

#### Chain Shape

To create a chain shape, you can use the `create_chain_shape` method on the body object:

```ruby
points = [
  { x: 0, y: 100 },
  { x: 200, y: 150 },
  { x: 400, y: 120 }
]
body.create_chain_shape(points, false)
```

The first argument is an array of points, and the second argument is a boolean that indicates whether the chain should be a loop or not.

**Important:** Chain shapes are one-sided. The collision normal points to the right of the segment direction. This means that the winding order of the points is important. If you want objects to collide with the top of the terrain, you should define the points from right to left.

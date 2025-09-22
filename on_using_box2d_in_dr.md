# Using Box2D in DragonRuby

This project uses a simplified, custom Box2D integration for a DragonRuby game jam. It's not a general-purpose solution.

## Core Concepts

The integration exposes two main classes:

*   **`World`**: The physics world that manages all bodies and the simulation.
*   **`Body`**: A rigid body with position, velocity, and mass. Bodies can be `static`, `kinematic`, or `dynamic`.

Currently, only **box** and **chain** shapes are supported.

## How it Works: C Extension

The integration is a C extension (`mygame/app/extension.c`) that wraps Box2D functions and exposes them to Ruby. It uses the `mruby` C API to communicate between the C and Ruby environments.

The C functions are registered in the `drb_register_c_extensions_with_api` function, making them available in Ruby under the `FFI::Box2D` module.

## How to Use It

### 1. Create a World

Create a new Box2D world:

```ruby
args.state.world = FFI::Box2D::World.new(args.grid.w, args.grid.h)
```

### 2. Create a Body

Create a body within the world:

```ruby
body = args.state.world.create_body("dynamic", x, y)
```

The first argument is the body type (`"static"`, `"kinematic"`, or `"dynamic"`), followed by the initial x and y coordinates.

### 3. Add Shapes to a Body

#### Box Shape

```ruby
body.create_box_shape(width, height, density)
```

#### Chain Shape

```ruby
points = [
  { x: 0, y: 100 },
  { x: 200, y: 150 },
  { x: 400, y: 120 }
]
body.create_chain_shape(points, false) # false = not a loop
```

**Important:** Chain shapes are one-sided. For collisions from above (like terrain), define the points from **right to left**.
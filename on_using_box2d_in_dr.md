# Using Box2D in DragonRuby

This project uses the Box2D library for physics. It's not a general-purpose solution and was quickly bashed together with DragonRuby's C extension setup / mruby FFI.

## Core Concepts

The integration exposes two main classes:

*   **`World`**: The physics world that manages all bodies and the simulation.
*   **`Body`**: A rigid body with position, velocity, and mass. Bodies can be `static`, `kinematic`, or `dynamic`.

Currently, only **box** and **chain** shapes are supported; nothing else was needed for the jam game.

## How it Works: C Extension

The integration is a C extension (`mygame/app/extension.c`) that wraps Box2D functions and exposes them to Ruby. It uses the `mruby` C API to communicate between the C and Ruby environments.

The C functions are registered in the `drb_register_c_extensions_with_api` function, making them available in Ruby under the `FFI::Box2D` module.

Simplistic build scripts (bash in *nix systems and batch files on Windows) are used to compile the extension as a shared library / DLL.

## How to Use It

### 0. Load the DLL and include Box2D

Typicaly somewhere in your setup method, load the shared library

```ruby
GTK.ffi_misc.gtk_dlopen('ext')
```

You can also include Box2D for easy access to the Ruby API.

```ruby
include FFI::Box2D
```

### 1. Create a World

Create a new Box2D world:

```ruby
args.state.world = World.new
```

### 2. Create a Body

Create a body within the world:

```ruby
body = args.state.world.create_body("dynamic", x, y)
```

The first argument is the body type (`"static"`, `"kinematic"`, or `"dynamic"`), followed by the initial x and y coordinates.

### 3. Add Shapes to a Body

Shapes define the collision shape and other physical characteristics (such as friction values) of the bodies. There can be many shapes per body.

#### Box Shape

```ruby
body.create_box_shape(width, h, density, friction, restitution, enable_contact_reporting)
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

*Important:* Chain shapes are one-sided. For collisions from above (like terrain), define the points from *right to left*

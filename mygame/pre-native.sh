#!/bin/sh

OSTYPE=`uname -s`
if [ "x$OSTYPE" = "xDarwin" ]; then
  PLATFORM=macos
  DLLEXT=dylib
else
  PLATFORM=linux-amd64
  DLLEXT=so
fi

DRB_ROOT=.
mkdir -p mygame/native/$PLATFORM

OPTIMIZATION_FLAGS="-g"
if [ "$1" = "--optimize" ]; then
  echo "Compiling with optimizations..."
  OPTIMIZATION_FLAGS="-O2"
else
	echo "Compiling with debug flags..."
fi

clang \
  -isystem $DRB_ROOT/include -isystem $DRB_ROOT -Imygame -Imygame/lib/box2d/include \
  -fPIC -shared mygame/app/extension.c \
  mygame/lib/box2d/src/wheel_joint.c \
mygame/lib/box2d/src/weld_joint.c \
mygame/lib/box2d/src/types.c \
mygame/lib/box2d/src/timer.c \
mygame/lib/box2d/src/table.c \
mygame/lib/box2d/src/solver_set.c \
mygame/lib/box2d/src/solver.c \
mygame/lib/box2d/src/shape.c \
mygame/lib/box2d/src/sensor.c \
mygame/lib/box2d/src/revolute_joint.c \
mygame/lib/box2d/src/prismatic_joint.c \
mygame/lib/box2d/src/physics_world.c \
mygame/lib/box2d/src/mover.c \
mygame/lib/box2d/src/motor_joint.c \
mygame/lib/box2d/src/math_functions.c \
mygame/lib/box2d/src/manifold.c \
mygame/lib/box2d/src/joint.c \
mygame/lib/box2d/src/id_pool.c \
mygame/lib/box2d/src/island.c \
mygame/lib/box2d/src/hull.c \
mygame/lib/box2d/src/geometry.c \
mygame/lib/box2d/src/distance_joint.c \
mygame/lib/box2d/src/dynamic_tree.c \
mygame/lib/box2d/src/distance.c \
mygame/lib/box2d/src/core.c \
mygame/lib/box2d/src/contact_solver.c \
mygame/lib/box2d/src/contact.c \
mygame/lib/box2d/src/constraint_graph.c \
mygame/lib/box2d/src/broad_phase.c \
mygame/lib/box2d/src/body.c \
mygame/lib/box2d/src/bitset.c \
mygame/lib/box2d/src/array.c \
mygame/lib/box2d/src/arena_allocator.c \
mygame/lib/box2d/src/aabb.c \
  $OPTIMIZATION_FLAGS -o mygame/native/$PLATFORM/ext.$DLLEXT

echo "Done..!"

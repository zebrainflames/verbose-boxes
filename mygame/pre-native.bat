@echo off
setlocal

REM Determine repository root based on this script's location so it works from root or mygame
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.." >nul 2>&1
if errorlevel 1 (
  echo ERROR: Failed to change directory to repository root.>&2
  endlocal & exit /b 1
)

set "PLATFORM=windows-amd64"
set "DLLEXT=dll"
set "DRB_ROOT=."

REM Ensure Git submodules are initialized and updated (required for Box2D sources)
where git >nul 2>&1
if errorlevel 1 (
  echo ERROR: Git is not installed or not in PATH. Please install Git to fetch submodules.>&2
  set "ERR=1"
  goto :end
)

git submodule update --init --recursive
if errorlevel 1 (
  echo ERROR: Failed to initialize/update Git submodules.>&2
  set "ERR=%errorlevel%"
  goto :end
)

if not exist mygame\native\%PLATFORM% (
    mkdir mygame\native\%PLATFORM%
)

set "OPTIMIZATION_FLAGS=-g"
if "%1"=="--optimize" (
    echo Compiling with optimizations...
    set "OPTIMIZATION_FLAGS=-O2"
) else (
    echo Compiling with debug flags...
)

echo Building native extension for DragonRuby...
clang ^
  --sysroot=C:\mingw64 ^
  --target=x86_64-w64-mingw32 ^
  -fuse-ld=lld ^
  -shared ^
  -fPIC ^
  -isystem "%DRB_ROOT%\include" ^
  -I. ^
  -Imygame ^
  -Imygame\lib\box2d\include ^
  -Imygame\lib\box2d\include\box2d ^
  mygame\app\extension.c ^
  mygame\lib\box2d\src\wheel_joint.c ^
  mygame\lib\box2d\src\weld_joint.c ^
  mygame\lib\box2d\src\types.c ^
  mygame\lib\box2d\src\timer.c ^
  mygame\lib\box2d\src\table.c ^
  mygame\lib\box2d\src\solver_set.c ^
  mygame\lib\box2d\src\solver.c ^
  mygame\lib\box2d\src\shape.c ^
  mygame\lib\box2d\src\sensor.c ^
  mygame\lib\box2d\src\revolute_joint.c ^
  mygame\lib\box2d\src\prismatic_joint.c ^
  mygame\lib\box2d\src\physics_world.c ^
  mygame\lib\box2d\src\mover.c ^
  mygame\lib\box2d\src\motor_joint.c ^
  mygame\lib\box2d\src\math_functions.c ^
  mygame\lib\box2d\src\manifold.c ^
  mygame\lib\box2d\src\joint.c ^
  mygame\lib\box2d\src\id_pool.c ^
  mygame\lib\box2d\src\island.c ^
  mygame\lib\box2d\src\hull.c ^
  mygame\lib\box2d\src\geometry.c ^
  mygame\lib\box2d\src\distance_joint.c ^
  mygame\lib\box2d\src\dynamic_tree.c ^
  mygame\lib\box2d\src\distance.c ^
  mygame\lib\box2d\src\core.c ^
  mygame\lib\box2d\src\contact_solver.c ^
  mygame\lib\box2d\src\contact.c ^
  mygame\lib\box2d\src\constraint_graph.c ^
  mygame\lib\box2d\src\broad_phase.c ^
  mygame\lib\box2d\src\body.c ^
  mygame\lib\box2d\src\bitset.c ^
  mygame\lib\box2d\src\array.c ^
  mygame\lib\box2d\src\arena_allocator.c ^
  mygame\lib\box2d\src\aabb.c ^
  %OPTIMIZATION_FLAGS% ^
  -o mygame\native\%PLATFORM%\ext.%DLLEXT%

if errorlevel 1 (
  echo Build FAILED.>&2
  set "ERR=%errorlevel%"
  goto :end
) else (
  echo Build completed: mygame\native\%PLATFORM%\ext.%DLLEXT%
)

set "ERR=0"

:end
popd >nul 2>&1
endlocal & exit /b %ERR%

@echo off
REM Initialize and update all Git submodules recursively.
where git >nul 2>&1
if errorlevel 1 (
  echo ERROR: Git is not installed or not in PATH.
  exit /b 1
)

echo Updating Git submodules recursively...
git submodule update --init --recursive
if errorlevel 1 (
  echo ERROR: Failed to initialize/update Git submodules.
  exit /b 1
)

echo Submodules are ready.

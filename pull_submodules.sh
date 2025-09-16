#!/bin/sh
# Initialize and update all Git submodules recursively.
if ! command -v git >/dev/null 2>&1; then
  echo "ERROR: Git is not installed or not in PATH." >&2
  exit 1
fi

echo "Updating Git submodules recursively..."
if ! git submodule update --init --recursive; then
  echo "ERROR: Failed to initialize/update Git submodules." >&2
  exit 1
fi

echo "Submodules are ready."

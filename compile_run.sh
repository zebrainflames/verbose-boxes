#!/bin/sh
# This script compiles the native extension and then runs the game if compilation succeeds.
# Pass --optimize to the script to compile with optimizations.

sh mygame/pre-native.sh "$1" && ./dragonruby

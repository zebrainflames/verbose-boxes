@echo off
REM This script compiles the native extension and then runs the game if compilation succeeds.
REM Pass --optimize to the script to compile with optimizations.

call mygame\pre-native.bat %1
if %errorlevel% equ 0 (
    dragonruby.exe
)
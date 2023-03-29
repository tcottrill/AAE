@echo off
rem This batch file was designed for use with Watcom C/C++ but can be easily
rem adapted to your particular compiler

rem Make the 6502 emitter
wcl386 make6502.c

rem Emit the zero page version of the 6502 emulator
make6502 m6502zp.asm -z

rem For MSVC, DJGPP, Borland, or gcc, use "make6502 m6502zp.asm -s -z"
rem for stack calling conventions.

rem Assemble this with NASM
nasm -f win32 m6502zp.asm

rem And now link it in with the cpubench program
wcl386 /s cpubench.c m6502zp.obj

rem Now clean up
erase *.obj
erase make6502.exe
erase m6502zp.asm



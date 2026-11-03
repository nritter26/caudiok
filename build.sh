#!/bin/sh
# cross-compile Windows x64 GUI app with zig (bundled mingw-w64 headers)
set -e
cd "$(dirname "$0")"
ZIG="${ZIG:-/mnt/storage/Projects/Dev/rea/.tools/zig-x86_64-linux-0.17.0/zig}"
mkdir -p build
$ZIG cc -target x86_64-windows-gnu -municode -O2 -lm \
  -lkernel32 -luser32 -lgdi32 -lwinmm -lcomdlg32 -lcomctl32 -lshell32 -lole32 \
  -Wl,--subsystem,windows \
  -o build/Caudiok.exe \
  src/main.c src/theme.c src/aux.c src/waveio.c src/audio.c src/dialogs.c src/dsp/declick.c src/dsp/denoise.c
file build/Caudiok.exe

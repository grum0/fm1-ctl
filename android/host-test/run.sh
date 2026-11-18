#!/bin/sh
# Builds and runs the engine's host test with every warning on, as the Android build does, then checks the
# generators' output against the golden fingerprints (render.c; `build/render golden --update` after a change made
# on purpose, said in the commit).
set -e
cd "$(dirname "$0")"
mkdir -p build
FLAGS="-std=c11 -I../../app -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined"
cc $FLAGS -o build/engine_test engine_test.c
./build/engine_test
cc $FLAGS -o build/render render.c
./build/render golden

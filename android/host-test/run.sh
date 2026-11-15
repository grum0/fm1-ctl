#!/bin/sh
# Builds and runs the engine's host test with every warning on, as the Android build does.
set -e
cd "$(dirname "$0")"
mkdir -p build
cc -std=c11 -I../../app -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -o build/engine_test engine_test.c
./build/engine_test

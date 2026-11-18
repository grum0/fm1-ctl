#!/bin/sh
# The size of the shared generators (app/gen.c with gen_cad.h and grids_tables.h) on a 32-bit microcontroller, so
# merging them into the FM-1 firmware (branch gen-1) holds no surprise. The FM-1's chip (JieLi AC79, pi32v2) needs
# JieLi's own toolchain, which only the firmware builder has; this compiles for ARM Cortex-M3 with clang at -Os
# instead. RAM (.data + .bss) carries over closely (32-bit, same structures); code size is an estimate.
# Budget: the FM-1's USB image has about 57 KB of RAM spare (gen1-priorities.md).
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
clang --target=armv7m-none-eabi -mthumb -Os -std=c11 -ffreestanding -fno-common -Wall -Wextra -Werror \
      -DGEN_STANDALONE -Iapp -Iandroid/tools/fm1size -c app/gen.c -o "$out/gen.o"
SIZE=$(command -v llvm-size || command -v llvm-size-18 || command -v size)
"$SIZE" -A "$out/gen.o" | awk '
    $1 ~ /^\.text/ || $1 ~ /^\.rodata/ { flash += $2 }
    $1 ~ /^\.data/ { flash += $2; ram += $2 }
    $1 ~ /^\.bss/ { ram += $2 }
    END { printf "gen.c for a 32-bit microcontroller (Cortex-M3, clang -Os; estimate for the FM-1):\n" \
                 "  Flash (code + constants + initial data): %d bytes (%.1f KB)\n" \
                 "  RAM (data + bss):                        %d bytes (%.1f KB)\n", flash, flash / 1024, ram, ram / 1024 }'
rm -rf "$out"

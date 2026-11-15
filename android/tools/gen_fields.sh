#!/bin/sh
# Regenerates app/src/main/java/com/grum0/gen1/Fields.kt. Run it after changing gen1_engine.h.
set -e
cd "$(dirname "$0")"
cc -I../../app -I../app/src/main/cpp -o /tmp/gen_fields gen_fields.c
/tmp/gen_fields > ../app/src/main/java/com/grum0/gen1/Fields.kt

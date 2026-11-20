#!/bin/sh
# Two cheap checks that catch what the Android build cannot: that Kotlin and C agree on the native functions (or the
# app dies at start with UnsatisfiedLinkError), that Fields.kt is what gen_fields.c makes of the headers now, and that
# app/style_grids.h is what make_grids.py makes of the style grids, TanzData.kt what make_tanz.py makes of the
# Tanzbär profile, and JvData.kt and jv1010_user01.syx what make_jv.py makes of the JV-1010 tables.
cd "$(dirname "$0")/.." || exit 1
kt=app/src/main/java/com/grum0/gen1/Native.kt
c=app/src/main/cpp/gen1_jni.c
bad=0
for n in $(sed -n 's/^ *external fun \([A-Za-z0-9_]*\).*/\1/p' "$kt"); do
    grep -q "JNICALL J($n)" "$c" || { echo "Kotlin has $n, gen1_jni.c does not"; bad=1; }
done
for n in $(sed -n 's/.*JNICALL J(\([A-Za-z0-9_]*\)).*/\1/p' "$c"); do
    grep -q "external fun $n(" "$kt" || { echo "gen1_jni.c has $n, Native.kt does not"; bad=1; }
done
cc -I../app -Iapp/src/main/cpp -o /tmp/gen_fields_check tools/gen_fields.c || exit 1
/tmp/gen_fields_check | cmp -s - app/src/main/java/com/grum0/gen1/Fields.kt || { echo "Fields.kt is out of date: run tools/gen_fields.sh"; bad=1; }
python3 ../app/styles/make_grids.py --check || bad=1
python3 tools/tanzbar/make_tanz.py --check || bad=1
python3 tools/jv1010/make_jv.py --check || bad=1
[ $bad = 0 ] && echo "native functions and field ids agree"
exit $bad

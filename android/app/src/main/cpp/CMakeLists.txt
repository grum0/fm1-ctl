# SPDX-License-Identifier: GPL-3.0-or-later
# The native half of GEN-1 for Android: the generators (app/gen.c, with app/gen_cad.h) and the engine around them.
# gen_cad.h is GPL-3.0-only (Cut-A-Dor's drum generator), so the library as a whole is under GPL-3.0-only.
cmake_minimum_required(VERSION 3.22.1)
project(gen1 C)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

add_library(gen1 SHARED
    gen1_jni.c
    gen1_engine.c
    ${REPO}/app/gen.c)
target_include_directories(gen1 PRIVATE ${REPO}/app)
target_compile_definitions(gen1 PRIVATE GEN_STANDALONE)
target_compile_options(gen1 PRIVATE -Wall -Wextra -O2)

find_library(log-lib log)
target_link_libraries(gen1 amidi ${log-lib})

# SPDX-License-Identifier: GPL-3.0-or-later
# The native half of GEN-1 for Android: the firmware's own generators (app/gen.c, unchanged) and the engine around them.
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

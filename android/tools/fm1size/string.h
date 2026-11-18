/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Just enough of <string.h> to compile app/gen.c for a bare 32-bit target (tools/gen_size.sh), where no C library
 * headers are installed. The firmware's own build uses its toolchain's. */
#pragma once
#include <stddef.h>
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strcpy(char *d, const char *s);

/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include <stdint.h>

/* Read a big-endian 32-bit unsigned integer. */
static __inline__ uint32_t read_u32(const void *p) {
    const unsigned char *c = p;
    return ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) |
           ((uint32_t)c[2] << 8) | (uint32_t)c[3];
}

/* Read a big-endian 32-bit signed integer. */
static __inline__ int32_t read_i32(const void *p) {
    return read_u32(p);
}

/* Read a big-endian 16-bit unsigned integer. */
static __inline__ uint16_t read_u16(const void *p) {
    const unsigned char *c = p;
    return ((uint16_t)c[0] << 8) | (uint16_t)c[1];
}

/* Read a big-endian 16-bit signed integer. */
static __inline__ int16_t read_i16(const void *p) {
    return read_u16(p);
}

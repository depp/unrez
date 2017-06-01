/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

struct char_state {
    uint8_t code;
    uint8_t table;
};

struct char_table {
    int16_t offset;
    uint8_t min;
    uint8_t max;
};

#include "macroman.h"

void unrez_from_macroman(char **outptr, char *outend, const char **inptr,
                         const char *inend) {
    char *out = *outptr;
    const char *in = *inptr;
    unsigned cp;
    while (in < inend) {
        cp = kToUnicodeTable[(uint8_t)*in];
        if (cp < 0x80) {
            if (outend - out < 1) {
                break;
            }
            out[0] = cp;
            out++;
        } else if (cp <= 0x3ff) {
            if (outend - out < 2) {
                break;
            }
            out[0] = (cp >> 6) | 0xc0;
            out[1] = (cp & 0x3f) | 0x80;
            out += 2;
        } else {
            out[0] = (cp >> 12) | 0xe0;
            out[1] = ((cp >> 6) & 0x3f) | 0x80;
            out[2] = (cp & 0x3f) | 0x80;
            out += 3;
        }
        in++;
    }
    *outptr = out;
    *inptr = in;
}

void unrez_to_macroman(char **outptr, char *outend, const char **inptr,
                       const char *inend) {
    char *out = *outptr;
    const char *in = *inptr, *scan = in, *last_pos = NULL;
    int table = 0, byte, last_code = 0;
    struct char_table t;
    struct char_state s;
    if (in == inend) {
        return;
    }
    while (1) {
        if (scan < inend) {
            byte = (uint8_t)*scan;
            scan++;
            t = kFromUnicodeTable[table];
            if (byte >= t.min && byte <= t.max) {
                s = kFromUnicodeState[t.offset + byte];
                if (s.code != 0 || byte == 0) {
                    last_pos = scan;
                    last_code = s.code;
                }
                table = s.table;
                if (table != 0) {
                    continue;
                }
            }
        }
        if (last_pos == NULL || out >= outend) {
            break;
        }
        *out = last_code;
        out++;
        in = scan = last_pos;
        last_pos = NULL;
        table = 0;
        if (in >= inend) {
            break;
        }
    }
    *outptr = out;
    *inptr = in;
}

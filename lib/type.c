/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int hexdigit(const char *s) {
    int v, i, c;
    v = 0;
    for (i = 0; i < 2; i++) {
        v = v << 4;
        c = (unsigned char)s[i];
        if (c >= '0' && c <= '9') {
            v |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v |= c - 'A' + 10;
        } else {
            return -1;
        }
    }
    return v;
}

int unrez_type_fromstring(uint32_t *type, const char *str) {
    int i, v;
    size_t len;
    uint32_t type_code;
    char buf[4], *out, *outend;
    const char *in, *inend;
    len = strlen(str);
    if (len == 10 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        type_code = 0;
        for (i = 0; i < 8; i++) {
            v = hexdigit(str + 2 + i);
            if (v == -1) {
                goto nothex;
            }
            type_code = (type_code << 4) | v;
        }
        *type = type_code;
        return 0;
    }
nothex:
    out = buf;
    outend = buf + sizeof(buf);
    in = str;
    inend = str + len;
    unrez_to_macroman(&out, outend, &in, inend);
    if (in < inend) {
        return -1;
    }
    while (out < outend) {
        *out++ = ' ';
    }
    type_code = 0;
    for (i = 0; i < 4; i++) {
        type_code = (type_code << 8) | (uint8_t)buf[i];
    }
    *type = type_code;
    return 0;
}

int unrez_type_tostring(char *buf, size_t bufsize, uint32_t type) {
    int i, c;
    char mc[4], uc[12], *out, *outend;
    const char *in, *inend;
    size_t sz, amt;
    for (i = 0; i < 4; i++) {
        c = (type >> (24 - i * 8)) & 0xff;
        if (c < 32 || c == 0x7f || c == 0xf1) {
            break;
        }
        mc[i] = c;
    }
    if (i == 4) {
        in = mc;
        inend = mc + sizeof(mc);
        out = uc;
        outend = uc + sizeof(uc);
        unrez_from_macroman(&out, outend, &in, inend);
        if (in == inend && out < outend) {
            sz = out - uc;
            if (sz >= bufsize) {
                amt = bufsize - 1;
            } else {
                amt = sz;
            }
            memcpy(buf, uc, amt);
            buf[amt] = '\0';
            return sz;
        }
    }
    return snprintf(buf, bufsize, "0x%08" PRIx32, type);
}

/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "defs.h"

#include "unrez.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

void errorf(const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    verrorf(msg, ap);
    va_end(ap);
}

void verrorf(const char *msg, va_list ap) {
    fputs("error: ", stderr);
    vfprintf(stderr, msg, ap);
    fputc('\n', stderr);
}

void error_errf(int errcode, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    verror_errf(errcode, msg, ap);
    va_end(ap);
}

void verror_errf(int errcode, const char *msg, va_list ap) {
    char buf[256];
    int r;
    fputs("error: ", stderr);
    vfprintf(stderr, msg, ap);
    r = unrez_strerror(errcode, buf, sizeof(buf));
    if (r == 0) {
        fprintf(stderr, ": %s\n", buf);
    } else {
        fprintf(stderr, ": error #%d occurred\n", errcode);
    }
}

void dief(int status, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    vdief(status, msg, ap);
    va_end(ap);
}

void vdief(int status, const char *msg, va_list ap) {
    verrorf(msg, ap);
    exit(status);
}

void die_errf(int status, int errcode, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    vdie_errf(status, errcode, msg, ap);
    va_end(ap);
}

void vdie_errf(int status, int errcode, const char *msg, va_list ap) {
    verror_errf(errcode, msg, ap);
    exit(status);
}

static int hexbyte(const char *s) {
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

void parse_type(unsigned char *type_code, const char *s) {
    int i, v, c;
    size_t len;
    const char *p;
    len = strlen(s);
    if (len == 10 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (i = 0; i < 4; i++) {
            v = hexbyte(s + 2 + 2 * i);
            if (v == -1) {
                goto nothex;
            }
            type_code[i] = v;
        }
        return;
    }
nothex:
    p = s;
    i = 0;
    while (*p != '\0') {
        if (i >= 4) {
            dief(EX_USAGE, "resource type is too long: '%s'", s);
        }
        c = *p++;
        if (c == '\\') {
            c = *p;
            switch (c) {
            case '\0':
                dief(EX_USAGE, "invalid escape sequence in resource type: '%s'",
                     s);
            case '\\':
            case '\'':
            case '"':
                p++;
                break;
            case 'x':
                v = hexbyte(p + 1);
                if (v == -1) {
                    dief(EX_USAGE,
                         "invalid hex escape sequence in resource type: '%s'",
                         s);
                }
                p += 3;
                c = v;
                break;
            case '0':
                p++;
                c = '\0';
                break;
            default:
                dief(EX_USAGE, "unknown escape sequence in resource type: '%s'",
                     s);
            }
        }
        type_code[i] = c;
        i++;
    }
    for (; i < 4; i++) {
        type_code[i] = ' ';
    }
}

int sprint_type(char *buf, size_t bufsz, const unsigned char *type_code) {
    int i, j, n = 0, v, d;
    char *p = buf, *e = buf + bufsz;
    if (p != e) {
        *p++ = '\'';
    }
    n++;
    for (i = 0; i < 4; i++) {
        v = type_code[i];
        if (v >= 32 && v <= 126) {
            if (v == '\'' || v == '\\') {
                if (p != e) {
                    *p++ = '\\';
                }
                n++;
            }
            if (p != e) {
                *p++ = v;
            }
            n++;
        } else {
            n += 4;
            if (p != e) {
                *p++ = '\\';
            }
            if (p != e) {
                *p++ = 'x';
            }
            for (j = 0; j < 2; j++) {
                if (p != e) {
                    d = (v >> (4 * (1 - j))) & 15;
                    if (d < 10) {
                        d += '0';
                    } else {
                        d += 'a' - 10;
                    }
                    *p++ = d;
                }
            }
        }
    }
    if (p != e) {
        *p++ = '\'';
    }
    n++;
    if (p == e) {
        if (p != buf) {
            p--;
            *p = '\0';
        }
    } else {
        *p = '\0';
    }
    return n;
}

int parse_id(const char *s) {
    char *end;
    long value;
    value = strtol(s, &end, 0);
    if (!*s || *end) {
        dief(EX_USAGE, "invalid resource id '%s'", s);
    }
    if (value > 0x7fff || value < -0x8000) {
        dief(EX_USAGE,
             "resource id %ld out of range, must be between -32768 and +32767",
             value);
    }
    return value;
}

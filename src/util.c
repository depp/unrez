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

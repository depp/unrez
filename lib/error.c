/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include <errno.h>
#include <string.h>

static const char *unrez_strerr(int code) {
    switch (code) {
    case kUnrezErrFormat:
        return "file does not have specified format";
    case kUnrezErrInvalid:
        return "file is corrupt";
    case kUnrezErrUnsupported:
        return "file is unspported";
    case kUnrezErrResourceNotFound:
        return "resource not found";
    case kUnrezErrNoResourceFork:
        return "file has no resource fork";
    case kUnrezErrTooLarge:
        return "file is too large";
    case kUnrezErrResourceForkTooLarge:
        return "resource fork is too large";
    }
    return NULL;
}

int unrez_strerror(int code, char *buf, size_t buflen) {
    const char *str;
    size_t len;
    str = unrez_strerr(code);
    if (str == NULL) {
        return strerror_r(code, buf, buflen);
    }
    len = strlen(str);
    if (len >= buflen) {
        return ERANGE;
    }
    memcpy(buf, str, len + 1);
    return 0;
}

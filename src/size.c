/*
 * Copyright 2009-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "defs.h"

#include <inttypes.h>
#include <stdio.h>

static const char kPrefixes[] = "kMGTPEZY";

int opt_bytes;

int sprint_size(char *buf, size_t bufsize, int64_t size) {
    int has_rem, rem, pfx, n, m;
    if (opt_bytes) {
        return snprintf(buf, bufsize, "%" PRId64 " B", size);
    }
    if (size <= 0) {
        return snprintf(buf, bufsize, "0 B");
    }
    if (size < 1000) {
        return snprintf(buf, bufsize, "%d B", (int)size);
    }
    has_rem = 0;
    for (pfx = 0;; pfx++) {
        rem = size % 1000;
        size /= 1000;
        if (size < 1000 || kPrefixes[pfx + 1] == '\0') {
            break;
        }
        if (rem > 0) {
            has_rem = 1;
        }
    }
    n = size;
    if (n < 10) {
        m = rem / 10;
        rem %= 10;
        if (rem > 5 || (rem == 5 && ((m & 1) != 0 || has_rem))) {
            m++;
            if (m == 100) {
                m = 0;
                n++;
                if (n == 10) {
                    return snprintf(buf, bufsize, "10.0 %cB", kPrefixes[pfx]);
                }
            }
        }
        return snprintf(buf, bufsize, "%d.%02d %cB", n, m, kPrefixes[pfx]);
    }
    if (n < 100) {
        m = rem / 100;
        rem %= 100;
        if (rem > 50 || (rem == 50 && ((m & 1) != 0 || has_rem))) {
            m++;
            if (m == 10) {
                m = 0;
                n++;
                if (n == 100) {
                    return snprintf(buf, bufsize, "100 %cB", kPrefixes[pfx]);
                }
            }
        }
        return snprintf(buf, bufsize, "%d.%d %cB", n, m, kPrefixes[pfx]);
    }
    if (rem > 500 || (rem == 500 && ((n & 1) != 0 || has_rem))) {
        n++;
    }
    if (n >= 1000 && kPrefixes[pfx + 1]) {
        return snprintf(buf, bufsize, "1.00 %cB", kPrefixes[pfx + 1]);
    }
    return snprintf(buf, bufsize, "%d %cB", n, kPrefixes[pfx]);
}

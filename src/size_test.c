/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "defs.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tcase {
    int64_t input;
    char output[8];
};

const struct tcase kCases[] = {
    {0, "0 B"},
    {5, "5 B"},
    {20, "20 B"},
    {100, "100 B"},
    {500, "500 B"},
    {999, "999 B"},
    {1000, "1.00 kB"},
    {1005, "1.00 kB"},
    {1006, "1.01 kB"},
    {2334, "2.33 kB"},
    {2335, "2.34 kB"},
    {2995, "3.00 kB"},
    {9994, "9.99 kB"},
    {9995, "10.0 kB"},
    {10000, "10.0 kB"},
    {10050, "10.0 kB"},
    {10061, "10.1 kB"},
    {99949, "99.9 kB"},
    {99950, "100 kB"},
    {999499, "999 kB"},
    {999500, "1.00 MB"},
    {1000000, "1.00 MB"},
    {952500000, "952 MB"},
    {952500001, "953 MB"},
    {1000000000, "1.00 GB"},
    {2300000000000, "2.30 TB"},
    {9700000000000000, "9.70 PB"},
};

int main(int argc, char **argv) {
    const struct tcase *p, *e;
    char out[16];
    int r, failure = 0;
    (void)argc;
    (void)argv;

    p = kCases;
    e = kCases + sizeof(kCases) / sizeof(*kCases);
    for (; p != e; p++) {
        memset(out, '\xff', sizeof(out));
        r = sprint_size(out, sizeof(out), p->input);
        if (r < 0 || (size_t)r >= sizeof(out)) {
            fprintf(stderr,
                    "case %d: %" PRId64 ": unexpected return value: %d\n",
                    (int)(p - kCases), p->input, r);
            failure = 1;
            continue;
        }
        if (strcmp(p->output, out) != 0) {
            fprintf(stderr,
                    "case %d: %" PRId64 ": got \"%s\", expected \"%s\"\n",
                    (int)(p - kCases), p->input, out, p->output);
            failure = 1;
            continue;
        }
    }
    if (failure) {
        fputs("FAILED\n", stderr);
        return 1;
    }
    return 0;
}

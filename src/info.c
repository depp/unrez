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

static const struct option kOptions[] = {
    {"bytes", &opt_bytes, 0, opt_parse_true}, {0},
};

static void info_usage(FILE *fp) {
    fputs("usage: unrez info [<options>] <file>...\n", fp);
}

void info_exec(int argc, char **argv) {
    int i, r;
    struct unrez_forkedfile forks;
    const char *file, *ds, *rs;
    char dsize[SIZE_WIDTH], rsize[SIZE_WIDTH];
    parse_options(kOptions, &argc, &argv);
    for (i = 0; i < argc; i++) {
        file = argv[i];
        r = unrez_forkedfile_open(&forks, file);
        if (r != 0) {
            die_errf(r > 0 ? EX_NOINPUT : EX_DATAERR, r, "%s", file);
        }
        if (forks.data.size > 0) {
            sprint_size(dsize, sizeof(dsize), forks.data.size);
            ds = dsize;
        } else {
            ds = "--";
        }
        if (forks.rsrc.size > 0) {
            sprint_size(rsize, sizeof(rsize), forks.rsrc.size);
            rs = rsize;
        } else {
            rs = "--";
        }
        printf("%10s data,  %10s rsrc  %s\n", ds, rs, file);
        unrez_forkedfile_close(&forks);
    }
}

void info_help(void) {
    info_usage(stdout);
    fputs(
        "Print information about a file and its resource fork.\n"
        "\n"
        "options:\n"
        "  -bytes        display sizes in bytes instead of using prefixes\n",
        stdout);
}

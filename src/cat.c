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
#include <unistd.h>

static void cat_usage(FILE *fp) {
    fputs("usage: unrez cat <file> <type> <id>\n", fp);
}

void cat_exec(int argc, char **argv) {
    struct unrez_resourcefork rfork;
    const char *file;
    unsigned char type_code[4];
    int res_id, err;
    const void *data;
    uint32_t size, pos;
    ssize_t amt;
    char stype[TYPE_WIDTH];
    if (argc != 3) {
        errorf("expected three arguments");
        cat_usage(stderr);
        exit(EX_USAGE);
    }
    file = argv[0];
    parse_type(type_code, argv[1]);
    res_id = parse_id(argv[2]);
    err = unrez_resourcefork_open(&rfork, file);
    if (err != 0) {
        die_errf(err > 0 ? EX_NOINPUT : EX_DATAERR, err, "%s", file);
    }
    err = unrez_resourcefork_findrsrc(&rfork, type_code, res_id, &data, &size);
    if (err != 0) {
        sprint_type(stype, sizeof(stype), type_code);
        die_errf(EX_DATAERR, err, "could not load resource %s #%d", stype,
                 res_id);
    }
    for (pos = 0; pos < size;) {
        amt =
            write(STDOUT_FILENO, (const unsigned char *)data + pos, size - pos);
        if (amt < 0) {
            die_errf(EX_OSERR, err, "could not write output");
        }
        pos += amt;
    }
    unrez_resourcefork_close(&rfork);
}

void cat_help(void) {
    cat_usage(stdout);
    fputs("Print a resource from a file's resource fork to standard output.\n",
          stdout);
}

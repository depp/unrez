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
    struct unrez_resource *rsrc;
    const char *file;
    uint32_t type_code;
    int res_id, err;
    const void *data;
    uint32_t size, pos;
    ssize_t amt;
    char stype[kUnrezTypeWidth];
    if (argc != 3) {
        errorf("expected three arguments");
        cat_usage(stderr);
        exit(EX_USAGE);
    }
    file = argv[0];
    err = unrez_type_fromstring(&type_code, argv[1]);
    if (err != 0) {
        dief(EX_USAGE, "invalid type code: '%s'", argv[1]);
    }
    unrez_type_tostring(stype, sizeof(stype), type_code);
    res_id = parse_id(argv[2]);
    err = unrez_resourcefork_open(&rfork, file);
    if (err != 0) {
        die_errf(err > 0 ? EX_NOINPUT : EX_DATAERR, err, "%s", file);
    }
    err = unrez_resourcefork_findrsrc(&rfork, &rsrc, type_code, res_id);
    if (err != 0) {
        die_errf(EX_DATAERR, err, "could not find resource %s #%d", stype,
                 res_id);
    }
    err = unrez_resourcefork_getdata(&rfork, rsrc, &data, &size);
    if (err != 0) {
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

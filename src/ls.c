/*
 * Copyright 2008-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include "defs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

typedef int (*compare_t)(const void *, const void *);

struct rlist {
    struct rsrc *rsrc;
    int size, capacity;
    int64_t total_size;
};

struct rsrc {
    char type[kUnrezTypeWidth];
    int id;
    int size;
};

static int compare_size(const void *x, const void *y) {
    const struct rsrc *rx = x, *ry = y;
    if (rx->size < ry->size) {
        return -1;
    } else if (rx->size > ry->size) {
        return 1;
    } else {
        return 0;
    }
}

static int compare_id(const void *x, const void *y) {
    const struct rsrc *rx = x, *ry = y;
    return rx->id - ry->id;
}

static int opt_flat;
static compare_t opt_sort = compare_id;
static int opt_reverse;

static void opt_parse_sort(void *value, const char *option, const char *arg) {
    compare_t v, *ptr = value;
    if (strcmp(arg, "id") == 0) {
        v = compare_id;
    } else if (strcmp(arg, "index") == 0) {
        v = NULL;
    } else if (strcmp(arg, "size") == 0) {
        v = compare_size;
    } else {
        dief(EX_USAGE, "%s: unknown sort key '%s'", option, arg);
    }
    *ptr = v;
}

static const struct option kOptions[] = {
    {"bytes", &opt_bytes, 0, opt_parse_true},
    {"flat", &opt_flat, 0, opt_parse_true},
    {"sort", &opt_sort, 1, opt_parse_sort},
    {"reverse", &opt_reverse, 0, opt_parse_true},
    {0},
};

static void ls_usage(FILE *fp) {
    fputs("usage: unrez ls [<options>] <file> [<type> [<id>]]\n", fp);
}

static void print_rlist(struct rlist *rlist) {
    char ssize[SIZE_WIDTH], sid[8];
    struct rsrc *rp = rlist->rsrc, *re = rp + rlist->size, *p, *q, t;
    if (opt_sort != NULL) {
        qsort(rp, rlist->size, sizeof(*rp), opt_sort);
    }
    if (opt_reverse) {
        p = rp;
        q = re;
        while (p != q) {
            q--;
            if (p == q) {
                break;
            }
            t = *q;
            *q = *p;
            *p = t;
            p++;
        }
    }
    for (rp = rlist->rsrc, re = rp + rlist->size; rp != re; rp++) {
        snprintf(sid, sizeof(sid), "#%d", rp->id);
        sprint_size(ssize, sizeof(ssize), rp->size);
        if (opt_flat) {
            printf("%s  %7s  %10s\n", rp->type, sid, ssize);
        } else {
            printf("    %7s  %10s\n", sid, ssize);
        }
    }
}

static void ls_rsrc(struct unrez_resourcefork *rfork, uint32_t type_code,
                    int res_id) {
    char stype[kUnrezTypeWidth], ssize[SIZE_WIDTH];
    int err;
    const void *data;
    uint32_t size;
    unrez_type_tostring(stype, sizeof(stype), type_code);
    err = unrez_resourcefork_findrsrc(rfork, type_code, res_id, &data, &size);
    if (err != 0) {
        die_errf(EX_DATAERR, err, "could not load resource %s #%d", stype,
                 res_id);
    }
    sprint_size(ssize, sizeof(ssize), size);
    printf("%s  #%d  %s\n", stype, res_id, ssize);
}

static void ls_type(struct rlist *rlist, struct unrez_resourcefork *rfork,
                    int type_index) {
    struct rsrc *r;
    char stype[kUnrezTypeWidth], ssize[SIZE_WIDTH];
    struct unrez_resourcetype *type;
    struct unrez_resource *rsrc;
    int err, rsrc_index, rsrc_count, ncap;
    const void *data;
    uint32_t size;
    int64_t total_size = 0;
    void *narr;
    type = &rfork->types[type_index];
    unrez_type_tostring(stype, sizeof(stype), type->type_code);
    err = unrez_resourcefork_loadtype(rfork, type_index);
    if (err != 0) {
        die_errf(EX_DATAERR, err, "could not load type %s", stype);
    }
    rsrc_count = type->count;
    rsrc = type->resources;
    if (rsrc_count > rlist->capacity - rlist->size) {
        ncap = rlist->capacity;
        if (ncap == 0) {
            ncap = 8;
        }
        while (rsrc_count > ncap - rlist->size) {
            ncap *= 2;
        }
        narr = realloc(rlist->rsrc, ncap * sizeof(*rlist->rsrc));
        if (narr == NULL) {
            die_errf(EX_OSERR, errno, "realloc");
        }
        rlist->rsrc = narr;
        rlist->capacity = ncap;
    }
    r = &rlist->rsrc[rlist->size];
    for (rsrc_index = 0; rsrc_index < rsrc_count; rsrc_index++) {
        err = unrez_resourcefork_getrsrc(rfork, type_index, rsrc_index, &data,
                                         &size);
        if (err != 0) {
            die_errf(EX_DATAERR, err, "could not load resource %s #%d", stype,
                     rsrc[rsrc_index].id);
        }
        memcpy(r->type, stype, sizeof(r->type));
        r->id = rsrc[rsrc_index].id;
        r->size = size;
        total_size += size;
        r++;
    }
    rlist->size = r - rlist->rsrc;
    if (!opt_flat) {
        sprint_size(ssize, sizeof(ssize), total_size);
        printf("type %s (%d resources, %s):\n", stype, rlist->size, ssize);
        print_rlist(rlist);
        fputc('\n', stdout);
        rlist->size = 0;
    }
    rlist->total_size += total_size;
}

void ls_exec(int argc, char **argv) {
    char ssize[SIZE_WIDTH];
    const char *file;
    uint32_t type_code;
    int err, res_id = 0, type_index, type_count;
    struct unrez_resourcefork rfork;
    struct rlist rlist = {0};
    parse_options(kOptions, &argc, &argv);
    if (argc < 1 || argc > 3) {
        errorf("expected 1-3 arguments");
        ls_usage(stderr);
        exit(EX_USAGE);
    }
    if (argc >= 2) {
        err = unrez_type_fromstring(&type_code, argv[1]);
        if (err != 0) {
            dief(EX_USAGE, "invalid resource type: '%s'", argv[1]);
        }
    }
    if (argc >= 3) {
        res_id = parse_id(argv[2]);
    }
    file = argv[0];
    err = unrez_resourcefork_open(&rfork, file);
    if (err != 0) {
        die_errf(err > 0 ? EX_NOINPUT : EX_DATAERR, err, "%s", file);
    }
    switch (argc) {
    default:
    case 1:
        type_count = rfork.type_count;
        for (type_index = 0; type_index < type_count; type_index++) {
            ls_type(&rlist, &rfork, type_index);
        }
        break;
    case 2:
        type_index = unrez_resourcefork_findtype(&rfork, type_code);
        if (type_index < 0) {
            dief(EX_DATAERR, "resource type not found: %s", argv[1]);
        }
        ls_type(&rlist, &rfork, type_index);
        break;
    case 3:
        ls_rsrc(&rfork, type_code, res_id);
        break;
    }
    if (opt_flat) {
        sprint_size(ssize, sizeof(ssize), rlist.total_size);
        printf("%d resources, %s:\n", rlist.size, ssize);
        print_rlist(&rlist);
    }
    free(rlist.rsrc);
    unrez_resourcefork_close(&rfork);
}

void ls_help(void) {
    ls_usage(stdout);
    fputs(
        "List resources in a file's resource fork.\n"
        "\n"
        "options:\n"
        "  -bytes        display sizes in bytes instead of using prefixes\n"
        "  -sort <key>   "
        "sort resources, key can be id (default), index, or size\n"
        "  -flat         "
        "display all resources in one list, instead of one per type\n"
        "  -reverse      reverse sort order\n",
        stdout);
}

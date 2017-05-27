/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

enum {
    kMmapMinimum = 16 * 1024,
};

enum {
    kTypeNone,
    kTypeMalloc,
    kTypeMmap,
};

void unrez_data_destroy(struct unrez_data *d) {
    switch (d->type) {
    case kTypeMalloc:
        free(d->mem);
        break;
    case kTypeMmap:
        munmap(d->mem, d->memsz);
        break;
    }
}

int unrez_fork_read(const struct unrez_fork *fork, struct unrez_data *d) {
    static long page_size;
    void *ptr;
    off_t start, end, map_start;
    size_t map_size, size, pos;
    ssize_t amt;
    int err;
    if (fork->size < 0) {
        return EINVAL;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
    if (fork->size > (size_t)-1) {
        return kUnrezErrTooLarge;
    }
#pragma GCC diagnostic pop
    size = fork->size;
    start = fork->offset;
    end = fork->offset + fork->size;
    if (size >= kMmapMinimum) {
        if (page_size == 0) {
            page_size = sysconf(_SC_PAGESIZE);
        }
        map_start = start & ~(off_t)(page_size - 1);
        map_size = end - map_start;
        size = fork->size;
        ptr =
            mmap(NULL, map_size, PROT_READ, MAP_SHARED, fork->file, map_start);
        if (ptr != MAP_FAILED) {
            d->data = (char *)ptr + (start - map_start);
            d->size = size;
            d->type = kTypeMmap;
            d->mem = ptr;
            d->memsz = map_size;
            return 0;
        }
    }
    ptr = malloc(size);
    if (ptr == NULL) {
        return errno;
    }
    for (pos = 0; pos < size;) {
        amt = pread(fork->file, (char *)ptr + pos, size - pos, start + pos);
        if (amt < 0) {
            err = errno;
            if (err == EINTR) {
                continue;
            }
            free(ptr);
            return err;
        } else if (amt == 0) {
            free(ptr);
            return kUnrezErrInvalid;
        }
        pos += amt;
    }
    d->data = ptr;
    d->size = size;
    d->type = kTypeMalloc;
    d->mem = ptr;
    d->memsz = size;
    return 0;
}

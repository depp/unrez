/*
 * Copyright 2008-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include "binary.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

/*
The resource fork format is found in Inside Macintosh: More Macintosh Toolbox
https://developer.apple.com/legacy/library/documentation/mac/pdf/MoreMacintoshToolbox.pdf
Reachable as of 2017
See p. 1-121.

A resource fork consists of a header, some data, and a resource map. The header
is at the start of the fork.

Resource Header, length 16
off len
 0   4  data offset (from start of fork)
 4   4  map offset
 8   4  data length (from start of fork)
12   4  map length

The

Resource Data entry, length 4 + variable
off len
 0   4  resource data length
 4  var resource data

Resource Map header, length 30
off len
 0  22  don't care
22   2  attributes
24   2  offset from map start to type list, minus two
26   2  offset from map start to name list
28   2  number of types minus one

Resource Type entry, length 8
off len
 0   4  type code
 4   2  number of resources of this type minus one
 6   2  offset from type list start to ref list for this type

Resource Ref entry, length 12
off len
 0   2  resource ID
 2   2  offset from beginning of names to this resource's name
 4   1  attributes
 5   3  offset from data start to this resource's data
 8   4  don't care

Resource Name entry, length 1 + variable
off len
 0   1  name length
 1  var name
*/

int unrez_resourcefork_openmem(struct unrez_resourcefork *rfork,
                               const void *data, size_t size) {
    int32_t doff, moff, dsize, msize, tcount, toff, i, rmax;
    const uint8_t *ptr = data, *mptr, *tptr;
    struct unrez_resourcetype *type, *t;

    if (size < 16) {
        return kUnrezErrInvalid;
    }

    /* Read the header with the map and data offsets. */
    doff = read_i32(ptr);
    moff = read_i32(ptr + 4);
    dsize = read_i32(ptr + 8);
    msize = read_i32(ptr + 12);
    if (moff < 0 || msize < 30 || (size_t)moff > size ||
        (size_t)msize > size - moff) {
        /* Bad map location. */
        return kUnrezErrInvalid;
    }
    if (doff < 0 || dsize < 0 || (size_t)doff > size ||
        (size_t)dsize > size - doff) {
        /* Bad data location. */
        return kUnrezErrInvalid;
    }
    rfork->map = mptr = ptr + moff;
    rfork->map_size = msize;
    rfork->data = ptr + doff;
    rfork->data_size = dsize;

    /* Read the map header */
    rfork->attr = read_u16(mptr + 22);
    rfork->toff = toff = read_i16(mptr + 24);
    rfork->noff = read_i16(mptr + 26);
    tcount = read_i16(mptr + 28) + 1;
    if (toff < 0 || tcount * 8 + 2 > msize - toff) {
        return kUnrezErrInvalid;
    }
    if (tcount == 0) {
        return 0;
    }

    /* Read the types. */
    type = malloc(sizeof(*type) * tcount);
    if (type == NULL) {
        return errno;
    }
    for (i = 0; i < tcount; i++) {
        t = &type[i];
        /*
         * Having read the docs a few times, I still can't figure out where the
         * +2 comes from. My current theory is that the docs are incorrect.
         */
        tptr = mptr + toff + 2 + 8 * i;
        t->type_code = read_u32(tptr);
        t->resources = NULL;
        rmax = read_i16(tptr + 4);
        t->count = rmax >= 0 ? rmax + 1 : 0;
        t->ref_offset = read_i16(tptr + 6);
    }
    rfork->types = type;
    rfork->type_count = tcount;
    memset(&rfork->owner, 0, sizeof(rfork->owner));
    return 0;
}

int unrez_resourcefork_openfork(struct unrez_resourcefork *rfork,
                                const struct unrez_fork *fork) {
    struct unrez_data d;
    int r;
    if (fork->size == 0) {
        return kUnrezErrNoResourceFork;
    } else if (fork->size > ((int64_t)1 << 25)) {
        /*
         * This is 32 MiB. Maximum amount of data in a resource fork is 16 MiB,
         * but there could theoretically also be some extra map data which
         * pushes it over. but that's pathological, right?
         */
        return kUnrezErrResourceForkTooLarge;
    } else if (fork->size < 16) {
        return kUnrezErrInvalid;
    }
    r = unrez_fork_read(fork, &d);
    if (r != 0) {
        return r;
    }
    r = unrez_resourcefork_openmem(rfork, d.data, d.size);
    if (r != 0) {
        unrez_data_destroy(&d);
        return r;
    }
    rfork->owner = d;
    return 0;
}

int unrez_resourcefork_open(struct unrez_resourcefork *rfork,
                            const char *path) {
    return unrez_resourcefork_openat(rfork, AT_FDCWD, path);
}

int unrez_resourcefork_openat(struct unrez_resourcefork *rfork, int dirfd,
                              const char *path) {
    struct unrez_forkedfile forks;
    int err;
    err = unrez_forkedfile_openat(&forks, dirfd, path);
    if (err != 0) {
        return err;
    }
    err = unrez_resourcefork_openfork(rfork, &forks.rsrc);
    unrez_forkedfile_close(&forks);
    return err;
}

void unrez_resourcefork_close(struct unrez_resourcefork *rfork) {
    struct unrez_resourcetype *type = rfork->types;
    int32_t ti, tn = rfork->type_count;
    for (ti = 0; ti < tn; ti++) {
        free(type[ti].resources);
    }
    free(type);
}

int unrez_resourcefork_findtype(struct unrez_resourcefork *rfork,
                                struct unrez_resourcetype **type,
                                uint32_t type_code) {
    struct unrez_resourcetype *types = rfork->types;
    int32_t err, i, n = rfork->type_count;
    for (i = 0; i < n; i++) {
        if (types[i].type_code == type_code) {
            err = unrez_resourcefork_loadtype(rfork, &types[i]);
            if (err != 0) {
                return err;
            }
            *type = &types[i];
            return 0;
        }
    }
    return kUnrezErrResourceNotFound;
}

int unrez_resourcefork_loadtype(struct unrez_resourcefork *rfork,
                                struct unrez_resourcetype *type) {
    struct unrez_resource *resources, *r;
    int32_t count, roff, i;
    const uint8_t *ptr, *rptr;
    if (type->resources != NULL) {
        return 0;
    }
    if (type->ref_offset < 0) {
        return kUnrezErrInvalid;
    }
    count = type->count;
    roff = rfork->toff + type->ref_offset;
    if (count * 12 > rfork->map_size || roff > rfork->map_size - count * 12) {
        return kUnrezErrInvalid;
    }
    resources = malloc(sizeof(*resources) * count);
    if (resources == NULL) {
        return errno;
    }
    ptr = rfork->map + roff;
    *(volatile const uint8_t *)ptr;
    for (i = 0; i < count; i++) {
        r = &resources[i];
        rptr = ptr + 12 * i;
        r->id = read_i16(rptr);
        r->name_offset = read_i16(rptr + 2);
        r->attr = rptr[4];
        /* A 24 bit integer, big endian. */
        r->offset = (rptr[5] << 16) | (rptr[6] << 8) | rptr[7];
        r->size = -1;
    }
    type->resources = resources;
    return 0;
}

int unrez_resourcefork_findrsrc(struct unrez_resourcefork *rfork,
                                struct unrez_resource **rsrc,
                                uint32_t type_code, int rsrc_id) {
    struct unrez_resourcetype *type;
    struct unrez_resource *rsrcs;
    int err, i, n;
    err = unrez_resourcefork_findtype(rfork, &type, type_code);
    if (err != 0) {
        return err;
    }
    rsrcs = type->resources;
    n = type->count;
    for (i = 0; i < n; i++) {
        if (rsrcs[i].id == rsrc_id) {
            *rsrc = &rsrcs[i];
            return 0;
        }
    }
    return kUnrezErrResourceNotFound;
}

int unrez_resourcefork_getdata(struct unrez_resourcefork *rfork,
                               struct unrez_resource *rsrc, const void **data,
                               uint32_t *size) {
    int32_t roff, rsize, dsize;
    roff = rsrc->offset;
    rsize = rsrc->size;
    if (rsize < 0) {
        dsize = rfork->data_size;
        if (dsize < 4 || roff > dsize - 4 || roff < 0) {
            return kUnrezErrInvalid;
        }
        rsize = read_i32(rfork->data + roff);
        if (rsize > dsize - 4 - roff || rsize < 0) {
            return kUnrezErrInvalid;
        }
        rsrc->size = rsize;
    }
    *data = rfork->data + roff + 4;
    *size = rsize;
    return 0;
}

int unrez_resourcefork_getname(struct unrez_resourcefork *rfork,
                               struct unrez_resource *rsrc, const char **name,
                               size_t *size) {
    const uint8_t *ndata;
    size_t nsize, rem;
    if (rsrc->name_offset < 0) {
        *name = NULL;
        *size = 0;
        return 0;
    }
    if (rfork->noff < 0 || rsrc->name_offset >= rfork->map_size - rfork->noff) {
        return kUnrezErrInvalid;
    }
    ndata = rfork->map + rfork->noff + rsrc->name_offset;
    rem = rfork->map_size - rfork->noff - rsrc->name_offset;
    nsize = *ndata;
    ndata++;
    rem--;
    if (nsize > rem) {
        return kUnrezErrInvalid;
    }
    *name = (const char *)ndata;
    *size = nsize;
    return 0;
}

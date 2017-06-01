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
Dug this out of Apple's legacy documentation:
http://developer.apple.com/documentation/mac/MoreToolbox/MoreToolbox-99.html
checked 2008, unreachable as of 2017

Resource fork is header + data + map

Resource Header, length 16
off len
 0   4  data offset
 4   4  map offset
 8   4  data length
12   4  map length

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
    const unsigned char *ptr = data, *mptr, *tptr;
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
    tcount = read_i16(mptr + 28);
    tcount = tcount >= 0 ? tcount + 1 : 0;
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

int unrez_resourcefork_findrsrc(struct unrez_resourcefork *rfork,
                                uint32_t type_code, int rsrc_id,
                                const void **data, uint32_t *size) {
    int type_index, rsrc_index, err;
    type_index = unrez_resourcefork_findtype(rfork, type_code);
    if (type_index < 0) {
        return kUnrezErrResourceNotFound;
    }
    err = unrez_resourcefork_loadtype(rfork, type_index);
    if (err != 0) {
        return err;
    }
    rsrc_index = unrez_resourcefork_findid(rfork, type_index, rsrc_id);
    if (rsrc_index < 0) {
        return kUnrezErrResourceNotFound;
    }
    return unrez_resourcefork_getrsrc(rfork, type_index, rsrc_index, data,
                                      size);
}

int unrez_resourcefork_findtype(struct unrez_resourcefork *rfork,
                                uint32_t type_code) {
    struct unrez_resourcetype *types = rfork->types;
    int32_t i, n = rfork->type_count;
    for (i = 0; i < n; i++) {
        if (types[i].type_code == type_code) {
            return i;
        }
    }
    return -1;
}

int unrez_resourcefork_loadtype(struct unrez_resourcefork *rfork,
                                int type_index) {
    struct unrez_resourcetype *t;
    struct unrez_resource *resources, *r;
    int32_t count, roff, i;
    const unsigned char *ptr, *rptr;
    if (type_index < 0 || type_index >= rfork->type_count) {
        return EINVAL;
    }
    t = &rfork->types[type_index];
    if (t->count == 0 || t->resources != NULL) {
        return 0;
    }
    if (t->ref_offset < 0) {
        return kUnrezErrInvalid;
    }
    count = t->count;
    roff = rfork->toff + t->ref_offset;
    if (count * 12 > rfork->map_size || roff > rfork->map_size - count * 12) {
        return kUnrezErrInvalid;
    }
    resources = malloc(sizeof(*resources) * count);
    if (resources == NULL) {
        return errno;
    }
    ptr = rfork->map + roff;
    *(volatile const unsigned char *)ptr;
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
    t->resources = resources;
    return 0;
}

int unrez_resourcefork_findid(struct unrez_resourcefork *rfork, int type_index,
                              int rsrc_id) {
    struct unrez_resourcetype *type;
    struct unrez_resource *resources;
    int32_t i, n;
    if (type_index < 0 || type_index >= rfork->type_count) {
        return -1;
    }
    type = &rfork->types[type_index];
    n = type->count;
    resources = type->resources;
    if (resources == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (resources[i].id == rsrc_id) {
            return i;
        }
    }
    return -1;
}

int unrez_resourcefork_getrsrc(struct unrez_resourcefork *rfork, int type_index,
                               int rsrc_index, const void **data,
                               uint32_t *size) {
    struct unrez_resourcetype *type;
    struct unrez_resource *r;
    int32_t roff, rsize, dsize;
    int err;
    if (type_index < 0 || type_index >= rfork->type_count) {
        return EINVAL;
    }
    type = &rfork->types[type_index];
    if (rsrc_index < 0 || rsrc_index >= type->count) {
        return EINVAL;
    }
    if (type->resources == NULL) {
        err = unrez_resourcefork_loadtype(rfork, type_index);
        if (err != 0) {
            return err;
        }
    }
    r = &type->resources[rsrc_index];
    roff = r->offset;
    rsize = r->size;
    if (rsize < 0) {
        dsize = rfork->data_size;
        if (dsize < 4 || roff > dsize - 4 || roff < 0) {
            return kUnrezErrInvalid;
        }
        rsize = read_i32(rfork->data + roff);
        if (rsize > dsize - 4 - roff || rsize < 0) {
            return kUnrezErrInvalid;
        }
        r->size = rsize;
    }
    *data = rfork->data + roff + 4;
    *size = rsize;
    return 0;
}

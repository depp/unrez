/*
Dug this out of Apple's legacy documentation:
http://developer.apple.com/documentation/mac/MoreToolbox/MoreToolbox-99.html

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
#include "scprsrc/resourcefork.h"
#include "scpbase/ifile.h"
#include "scpbase/binary.h"
#include "scpbase/error.h"
#include "scpbase/error_handler.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

/* The offsets in a resource fork take 24 bits, making the maximum
   size of a resource fork 16 MiB.  Resource forks larger than this
   size will be rejected when opened.  */
#define RESOURCE_FORK_MAX (1 << 24)

static const char
    RESOURCEFORK_MAP[] = "Resource fork map is corrupt",
    RESOURCEFORK_TYPE[] = "Resource fork type is corrupt",
    RESOURCEFORK_RSRC[] = "Resource fork resource is corrupt";

int
resourcefork_open(struct resourcefork *rfork,
                  struct ifile *file, struct error_handler *err)
{
    struct resourcefork_type *types = NULL, *tptr;
    int32_t doff, dlen, moff, mlen, flen, tcount, trem;
    int16_t toff, rmax;
    uint8_t *fptr, *mptr, *ptr;
    int64_t flen_o;
    struct error *e = NULL;

    flen_o = ifile_length(file);
    if (flen_o < 0) {
        error_handle(err, &file->err);
        return -1;
    }
    if (flen_o == 0) {
        rfork->data = NULL;
        rfork->datalen = 0;
        rfork->moff = 0;
        rfork->mlen = 0;
        rfork->doff = 0;
        rfork->dlen = 0;
        rfork->types = NULL;
        rfork->tcount = 0;
        rfork->attr = 0;
        rfork->toff = 0;
        rfork->noff = 0;
        return 0;
    }
    if (flen_o > RESOURCE_FORK_MAX || flen_o < 16) {
        error_msg(err, RESOURCEFORK_MAP);
        return -1;
    }
    flen = flen_o;
    if (ifile_readall(file, (void **)&rfork->data, &rfork->datalen)) {
        error_handle(err, &file->err);
        return -1;
    }
    fptr = rfork->data;

    // Read the header with the map and data offsets
    rfork->doff = doff = read_bs32(fptr);
    rfork->moff = moff = read_bs32(fptr + 4);
    rfork->dlen = dlen = read_bs32(fptr + 8);
    rfork->mlen = mlen = read_bs32(fptr + 12);
    if (moff < 0 || mlen < 30 || moff > flen || moff > flen - mlen) {
        // Bad map location
        error_msg(err, RESOURCEFORK_MAP);
        goto err;
    }
    if (doff < 0 || dlen < 0 || doff > flen || doff > flen - dlen) {
        // Bad data location
        error_msg(err, RESOURCEFORK_MAP);
        goto err;
    }

    // Read the map header
    mptr = fptr + moff;
    rfork->attr = read_bu16(mptr + 22);
    rfork->toff = toff = read_bs16(mptr + 24);
    rfork->noff = read_bs16(mptr + 26);
    tcount = read_bs16(mptr + 28);
    tcount = tcount >= 0 ? tcount + 1 : 0;
    rfork->tcount = tcount;

    // Read the types
    if (toff < 0 || tcount * 8 + 2 > mlen - toff) {
        error_msg(err, RESOURCEFORK_MAP);
        goto err;
    }
    if (!tcount) {
        rfork->types = NULL;
        goto success;
    }
    types = malloc(tcount * sizeof(*types));
    if (!types) {
        error_memory(&e);
        error_handle(err, &e);
        goto err;
    }
    rfork->types = types;
    ptr = mptr + toff + 2; // I don't know where the 2 is from
    trem = tcount;
    tptr = types;
    for (; trem > 0; --trem, ptr += 8, ++tptr) {
        memcpy(tptr->code, ptr, 4);
        tptr->resources = NULL;
        rmax = read_bs16(ptr + 4);
        tptr->rcount = rmax >= 0 ? rmax + 1 : 0;
        tptr->ref_offset = read_bs16(ptr + 6);
    }

success:
    return 0;
err:
    free(rfork->data);
    return -1;
}

void
resourcefork_close(struct resourcefork *rfork)
{
    struct resourcefork_type *tptr, *tend, *types = rfork->types;
    free(rfork->data);
    if (types) {
        tptr = types;
        tend = types + rfork->tcount;
        for (; tptr != tend; ++tptr) {
            if (tptr->resources)
                free(tptr->resources);
        }
        free(rfork->types);
    }
}

int
resourcefork_find_type(struct resourcefork *rfork,
                       uint8_t const *code)
{
    struct resourcefork_type *tptr = rfork->types,
        *tend = tptr + rfork->tcount;
    int idx = 0;
    for (; tptr != tend; ++idx, ++tptr)
        if (!memcmp(code, tptr->code, 4))
            return idx;
    return -1;
}

int
resourcefork_load_type(struct resourcefork *rfork,
                       int type_index, struct error_handler *err)
{
    struct resourcefork_type *tptr;
    struct resourcefork_rsrc *resources, *rptr;
    int32_t rcount, roff, rrem;
    uint8_t *ptr;
    struct error *e = NULL;

    assert(type_index >= 0 && type_index < rfork->tcount);
    tptr = rfork->types + type_index;
    if (!tptr->rcount || tptr->resources)
        return 0;
    if (tptr->ref_offset < 0) {
        error_msg(err, RESOURCEFORK_TYPE);
        return -1;
    }
    rcount = tptr->rcount;
    roff = rfork->toff + tptr->ref_offset;
    if (roff > rfork->mlen - rcount * 12) {
        error_msg(err, RESOURCEFORK_TYPE);
        return -1;
    }
    resources = malloc(sizeof(*resources) * rcount);
    if (!resources) {
        error_memory(&e);
        error_handle(err, &e);
        return -1;
    }
    tptr->resources = resources;
    rrem = rcount;
    ptr = rfork->data + rfork->moff + roff;
    rptr = resources;
    for (; rrem > 0; ++rptr, --rrem, ptr += 12) {
        rptr->id = read_bs16(ptr);
        rptr->name_offset = read_bs16(ptr + 2);
        rptr->attr = ptr[4];
        // A 24 bit integer, big endian...
        rptr->offset = (ptr[5] << 16)
            | (ptr[6] << 8)
            | ptr[7];
        rptr->length = -1;
    }
    return 0;
}

int
resourcefork_find_rsrc(struct resourcefork *rfork,
                       int type_index, int rsrc_id)
{
    struct resourcefork_type *tptr;
    struct resourcefork_rsrc *rptr, *rend;
    int rsrc_index;
    assert(type_index >= 0 && type_index < rfork->tcount);
    tptr = rfork->types + type_index;
    rsrc_index = 0;
    rptr = tptr->resources;
    rend = rptr + tptr->rcount;
    assert(!tptr->rcount || tptr->resources);
    for (; rptr != rend; ++rsrc_index, ++rptr)
        if (rptr->id == rsrc_id)
            return rsrc_index;
    return -1;
}

int
resourcefork_rsrc_loc(struct resourcefork *rfork,
                      int type_index, int rsrc_index,
                      int32_t *offset, int32_t *length,
                      struct error_handler *err)
{
    struct resourcefork_type *tptr;
    struct resourcefork_rsrc *rptr;
    int32_t off, len, dlen, doff;

    assert(type_index >= 0 && type_index < rfork->tcount);
    tptr = rfork->types + type_index;
    assert(rsrc_index >= 0 && rsrc_index < tptr->rcount);
    assert(tptr->resources != NULL);
    rptr = tptr->resources + rsrc_index;
    doff = rfork->doff;
    off = rptr->offset;
    len = rptr->length;
    if (len < 0) {
        dlen = rfork->dlen;
        if (off + 4 > rfork->dlen || off < 0)
            goto err;
        len = read_bs32(rfork->data + doff + off);
        if (len > dlen - 4 - off)
            goto err;
        rptr->length = len;
    }
    if (offset)
        *offset = doff + off + 4;
    if (length)
        *length = len;
    return 0;
err:
    error_msg(err, RESOURCEFORK_RSRC);
    return -1;
}

int
resourcefork_rsrc_read(struct resourcefork *rfork,
                       int type_index, int rsrc_index,
                       void **data, uint32_t *length,
                       struct error_handler *err)
{
    int32_t off, len;
    if (resourcefork_rsrc_loc(rfork, type_index, rsrc_index,
                              &off, &len, err))
        return -1;
    if (data)
        *data = rfork->data + off;
    if (length)
        *length = len;
    return 0;
}

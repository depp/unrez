/*
 * Copyright 2008-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include "binary.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
From:
- http://users.phg-online.de/tk/netatalk/doc/Apple/v1/
  checked 2008, unreachable as of 2017

Header (26 bytes):
off len
 0   4  Magic number (0x00051600 = single, 0x00051607 = double)
 4   4  Version number
 8  16  Home file system, space padded (such as 'Macintosh       ')
24   2  Number of entries

Entries (12 bytes):
off len
 0   4  Entry ID
 4   4  Data offset
 8   4  Data length

Entry IDs:
1   Data fork (Apple Single)
2   Resource Fork
3   Real name
4   Comment
5   Icon, monochrome
6   Icon, color
7   File info
9   Finder info
*/

static const unsigned char kAppleDoubleMagic[4] = {0x00, 0x05, 0x16, 0x07},
                           kAppleSingleMagic[4] = {0x00, 0x05, 0x16, 0x00};

/* Entry IDs. */
enum {
    kEntryData = 1,
    kEntryRsrc = 2,

    /* This should be a reasonable maximum.  */
    kMaxEntries = 16,
    kHeaderSize = 26,
    kEntrySize = 12
};

int unrez_applefile_parse(struct unrez_metadata *mdata, int fdes,
                          int64_t fsize) {
    unsigned char header[kHeaderSize + kEntrySize * kMaxEntries];
    const unsigned char *eptr;
    ssize_t amt;
    uint32_t version, eid, eoffset, esize;
    int num_entries, r, header_size, i;
    int has_data = 0, has_rsrc = 0;
    struct stat st;

    if (fsize < 0) {
        r = fstat(fdes, &st);
        if (r == -1) {
            return errno;
        }
        fsize = st.st_size;
    }

    amt = pread(fdes, header, sizeof(header), 0);
    if (amt < 0) {
        return errno;
    }
    if (amt < kHeaderSize) {
        return kUnrezErrFormat;
    }

    /* Read magic header */
    if (memcmp(header, kAppleDoubleMagic, 4) == 0) {
        mdata->type = kUnrezTypeAppleDouble;
    } else if (memcmp(header, kAppleSingleMagic, 4) == 0) {
        mdata->type = kUnrezTypeAppleSingle;
    } else {
        return kUnrezErrFormat;
    }

    /* Check version */
    version = read_u32(header + 4);
    if (version > 0x00020000) {
        return kUnrezErrUnsupported;
    }

    /* Validate table entry count */
    num_entries = read_u16(header + 24);
    /* Will not overflow. */
    header_size = kHeaderSize + num_entries * kEntrySize;
    if (header_size > fsize) {
        return kUnrezErrInvalid;
    }
    if (num_entries > kMaxEntries) {
        return kUnrezErrUnsupported;
    }
    if (header_size > amt) {
        return kUnrezErrInvalid;
    }
    memset(mdata, 0, sizeof(*mdata));
    for (i = 0; i < num_entries; i++) {
        eptr = header + kHeaderSize + kEntrySize * i;
        eid = read_u32(eptr);
        eoffset = read_u32(eptr + 4);
        esize = read_u32(eptr + 8);
        if (eoffset > fsize || esize > fsize - eoffset) {
            return kUnrezErrInvalid;
        }
        switch (eid) {
        case kEntryData:
            if (has_data) {
                return kUnrezErrInvalid;
            }
            has_data = 1;
            mdata->data_offset = eoffset;
            mdata->data_size = esize;
            break;
        case kEntryRsrc:
            if (has_rsrc) {
                return kUnrezErrInvalid;
            }
            has_rsrc = 1;
            mdata->rsrc_offset = eoffset;
            mdata->rsrc_size = esize;
            break;
        }
    }
    return 0;
}

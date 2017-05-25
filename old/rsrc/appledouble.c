/*
From:
- http://users.phg-online.de/tk/netatalk/doc/Apple/v1/

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
#include "scprsrc/appledouble.h"
#include "scprsrc/rerror.h"
#include "scpbase/binary.h"
#include "scpbase/ifile.h"
#include "scpbase/error.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static const uint8_t
    APPLEDOUBLE_MAGIC[4] = {0x00, 0x05, 0x16, 0x07},
    APPLESINGLE_MAGIC[4] = {0x00, 0x05, 0x16, 0x00};

#define ENTRY_DATA 1
#define ENTRY_RSRC 2
/* This should be a reasonable maximum.  */
#define MAX_ENTRIES 16

static char const
    ERR_UNKNOWN_VERSION[] = "Unknown AppleDouble version number",
    ERR_CORRUPT_DATA[] = "AppleDouble file is corrupt",
    ERR_MANY_ENTRIES[] = "AppleDouble file has too many entries";

#define error_ad(err, msg) \
    error_sets(err, &ERROR_RSRC, 0, msg)

int
appledouble_parse(struct ifile *file, struct error **err,
                  uint32_t *doffset, uint32_t *dlength,
                  uint32_t *roffset, uint32_t *rlength)
{
    ssize_t amt;
    uint8_t header[26], entries[12 * MAX_ENTRIES], *eptr;
    uint32_t version, id, off, len;
    uint16_t entry_count, e;
    bool has_data, has_rsrc;

    amt = ifile_pread(file, header, 26, 0);
    if (amt < 0) {
        memcpy(err, &file->err, sizeof(*err));
        return -1;
    }
    if (amt != 26
        || (memcmp(header, APPLEDOUBLE_MAGIC, 4)
            && memcmp(header, APPLESINGLE_MAGIC, 4)))
        return 0;
    version = read_bu32(header + 4);
    if (version > 0x00020000) {
        error_ad(err, ERR_UNKNOWN_VERSION);
        return -1;
    }

    // Read offsets for data and resource forks
    entry_count = read_bu16(header + 24);
    if (entry_count > MAX_ENTRIES) {
        error_ad(err, ERR_MANY_ENTRIES);
        return -1;
    }
    amt = ifile_read(file, entries, 12 * entry_count);
    if (amt < 0) {
        memcpy(err, &file->err, sizeof(*err));
        return -1;
    }
    if (amt != (12 * entry_count)) {
        error_ad(err, ERR_CORRUPT_DATA);
        return -1;
    }

    has_data = false;
    has_rsrc = false;
    for (e = 0; e < entry_count; ++e) {
        eptr = entries + 12 * e;
        id = read_bu32(eptr);
        if (id != ENTRY_DATA && id != ENTRY_RSRC)
            continue;
        off = read_bu32(eptr + 4);
        len = read_bu32(eptr + 8);
        if (off > (uint32_t)INT32_MAX
            || len > (uint32_t)INT32_MAX - off
            || (id == ENTRY_DATA && has_data)
            || (id == ENTRY_RSRC && has_rsrc)) {
            error_ad(err, ERR_CORRUPT_DATA);
            return -1;
        }
        if (id == ENTRY_DATA) {
            if (doffset)
                *doffset = off;
            if (dlength)
                *dlength = len;
            has_data = true;
        } else {
            if (roffset)
                *roffset = off;
            if (rlength)
                *rlength = len;
            has_rsrc = true;
        }
    }
    if (!has_data) {
        if (doffset)
            *doffset = 0;
        if (dlength)
            *dlength = 0;
    }
    if (!has_rsrc) {
        if (roffset)
            *roffset = 0;
        if (rlength)
            *rlength = 0;
    }
    return 1;
}

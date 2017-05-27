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
- http://www.lazerware.com/macbinary/macbinary.html
- http://www.lazerware.com/macbinary/macbinary_ii.html
- http://www.lazerware.com/macbinary/macbinary_iii.html
  checked 2008, unreachable as of 2017

off len
  0  1  Zero
  1  1  Filename length
  2 63  Filename
 65  4  File Type
 69  4  File Creator
 73  1  Finder Flags
 74  1  Zero
 75  2  Vertical Position
 77  2  Horizontal Position
 79  2  Window / folder ID
 81  1  "Protected" flag
 82  1  Zero
 83  4  Data fork length
 87  4  Resource fork length
 91  4  Creation date
 95  4  Modification date
 99  2  Get Info comment length
-- Version >= 2 --
101  1  More finder flags
102  4  Signature "mBin" (version 3)
106  1  Filename script (version 3)
107  1  Extended Finder flags (version 3)
116  4  Something to do with compression
120  2  Future expansion
122  1  Version number of MacBinary
        (129 for MacBinary II, 130 for MacBinary III)
123  1  Minimum version number for extraction
124  2  CRC
126  2  Reserved

Start by checking offset 102, the value "mBin" indicates MacBinary III.
Then check bytes 0 and 74, which should both be zero.
Then check the CRC, which indicates MacBinary II.
Then check that byte 82 is zero.

The header is followed by the data fork, padded to a multiple of 128 bytes,
then the resource fork, similarly padded, then the file's comment.
*/

static int64_t align(int64_t value, int bits) {
    int64_t mask = ((int64_t)1 << bits) - 1;
    return (value + mask) & (~mask);
}

static uint16_t crc(const void *data, size_t length) {
    const unsigned char *ptr = data;
    uint16_t result = 0, d;
    int bit;
    for (; length > 0; --length, ++ptr) {
        d = *ptr << 8;
        /*
         * Bit by bit is slow, but we are only calculating the CRC of a few
         * bytes.
         */
        for (bit = 0; bit < 8; ++bit) {
            if ((d ^ result) & 0x8000) {
                result = (result << 1) ^ 0x1021;
            } else {
                result = (result << 1);
            }
            d <<= 1;
        }
    }
    return result;
}

int unrez_macbinary_parse(struct unrez_metadata *mdata, int fdes,
                          int64_t fsize) {
    ssize_t amt;
    unsigned char header[128];
    uint16_t file_crc, calculated_crc;
    uint32_t dsize, rsize;
    int64_t doff, roff;
    int r;
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
    if ((size_t)amt < sizeof(header) || header[0] || header[74] || header[82] ||
        header[1] > 63 || header[123] > 129) {
        return kUnrezErrFormat;
    }
    /* memcmp(header + 102, kSignature, 4) */
    file_crc = read_u16(header + 124);
    calculated_crc = crc(header, 124);
    if (file_crc != calculated_crc) {
        return kUnrezErrFormat;
    }

    dsize = read_u32(header + 83);
    rsize = read_u32(header + 87);
    doff = 128;
    roff = align(doff + dsize, 7);

    if (dsize > fsize - doff || roff > fsize || rsize > fsize - roff) {
        return kUnrezErrInvalid;
    }

    memset(mdata, 0, sizeof(*mdata));
    mdata->data_offset = doff;
    mdata->data_size = dsize;
    mdata->rsrc_offset = roff;
    mdata->rsrc_size = rsize;

    return 0;
}

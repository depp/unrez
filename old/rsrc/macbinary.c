/*
From:
- http://www.lazerware.com/macbinary/macbinary.html
- http://www.lazerware.com/macbinary/macbinary_ii.html
- http://www.lazerware.com/macbinary/macbinary_iii.html

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
#include "scprsrc/macbinary.h"
#include "scprsrc/rerror.h"
#include "scpbase/binary.h"
#include "scpbase/ifile.h"
#include "scpbase/error.h"
#include <string.h>
#include <stdio.h>

static int align(int value, int bits) {
    int mask = (1 << bits) - 1;
    return (value + mask) & (~mask);
}

static uint16_t
crc(uint8_t const *restrict data, size_t length)
{
    uint16_t result = 0, d;
    int bit;
    for (; length > 0; --length, ++data) {
        d = *data << 8;
        for (bit = 0; bit < 8; ++bit) {
            if ((d ^ result) & 0x8000)
                result = (result << 1) ^ 0x1021;
            else
                result = (result << 1);
            d <<= 1;
        }
    }
    return result;
}

int
macbinary_parse(struct ifile *file, struct error **err,
                uint32_t *doffset, uint32_t *dlength,
                uint32_t *roffset, uint32_t *rlength)
{
    ssize_t amt;
    uint8_t header[128];
    uint16_t file_crc, calculated_crc;
    uint32_t doff, dlen, roff, rlen;
    amt = ifile_pread(file, header, 128, 0);
    if (amt < 0) {
        memcpy(err, &file->err, sizeof(*err));
        return -1;
    }
    if (amt != 128
        || header[0] || header[74] || header[82]
        || header[1] > 63 || header[123] > 129)
        return 0;
    // memcmp(header + 102, kSignature, 4)
    file_crc = read_bu16(header + 124);
    calculated_crc = crc(header, 124);
    if (file_crc != calculated_crc)
        return 0;

    // Read header
    doff = 128;
    dlen = read_bu32(header + 83);
    roff = align(dlen + doff, 7);
    rlen = read_bu32(header + 87);

    if (dlen > (uint32_t)INT32_MAX - doff
        || roff > (uint32_t)INT32_MAX
        || rlen > (uint32_t)INT32_MAX - roff) {
        error_sets(err, &ERROR_RSRC, 0,
                   "MacBinary file is corrupt");
        return -1;
    }

    *doffset = doff;
    *dlength = dlen;
    *roffset = roff;
    *rlength = rlen;

    return 1;
}

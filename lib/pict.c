/*
 * Copyright 2007-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include "binary.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
  Information about the format of QuickDraw pictures is in the book "Inside
  Macintosh: Imaging With QuickDraw" (1994). Chapter 7 defines the picture
  format and describe how pictures work, and appendix A contais a list of the
  picture opcodes. The book is available online as a PDF in Apple's legacy
  documentation section, as of 2017:

  https://developer.apple.com/legacy/library/documentation/mac/pdf/ImagingWithQuickDraw.pdf

  Previously, this book was also available in HTML format on Apple's website,
  which was more convenient. These links are now dead, but in 2007 they
  contained the same documentation for QuickDraw pictures:

  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-333.html

  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-458.html
*/

/* Definitions for a range of opcodes. */
struct opcode_range {
    /* Closed (inclusive) range of opcode values. */
    uint16_t start;
    uint16_t end;
    /* Index into kOpcodeNames, plus 1. If 0, then the opcode has no name. */
    uint16_t name;
    /*
     * Describes the data payload. If zero or positive, the opcode data has a
     * fixed size, and this value is the size. If negative, then it is equal to
     * -1-x, where x is one of the data enumerations below.
     */
    int16_t data;
};

/*
 * Types of variable-length data that can appear in pictures. This must stay
 * synchronized with the kDataHandlers table below.
 */
enum {
    /* Format version. */
    kTypeVersion,
    /* End of picture. */
    kTypeEnd,
    /* 16-bit size, followed by data. */
    kTypeData16,
    /* 32-bit size, followed by data. */
    kTypeData32,
    /* 16-bit kind, 16-bit size, followed by data. */
    kTypeLongComment,
    /* Clipping region. */
    kTypeRegion,
    /* Pixel pattern. */
    kTypePattern,
    /* Text (various). */
    kTypeText,
    /* Opcode data is not specified. */
    kTypeNotDetermined,
    /* Polygon. */
    kTypePolygon,
    /* Pixel data. */
    kTypePixelData,
    /* Embedded QuickTime image */
    kTypeQuickTime
};

#include "pict_opcode.h"

static const struct opcode_range *find_opcode(int opcode) {
    int i = 0, n = sizeof(kOpcodeRanges) / sizeof(*kOpcodeRanges);
    for (; i < n; i++) {
        if (kOpcodeRanges[i].end >= opcode) {
            if (kOpcodeRanges[i].start <= opcode) {
                return &kOpcodeRanges[i];
            }
            return NULL;
        }
    }
    return NULL;
}

const char *unrez_pict_opname(int opcode) {
    int name_index;
    const struct opcode_range *range;
    if (opcode >= 0 && opcode < 256) {
        name_index = kOpcodeNameTable[opcode];
    } else {
        range = find_opcode(opcode);
        if (range != NULL) {
            name_index = range->name;
        } else {
            name_index = 0;
        }
    }
    if (name_index == 0) {
        return NULL;
    }
    return kOpcodeNames + (name_index - 1);
}

static const char kErrUnexpectedEof[] = "unexpected end of file",
                  kErrInvalidLength[] = "invalid length";

static ptrdiff_t pict_eof(const struct unrez_pict_callbacks *cb, int opcode) {
    cb->error(cb->ctx, kUnrezErrInvalid, opcode, kErrUnexpectedEof);
    return -1;
}

static void read_rect(struct unrez_rect *r, const uint8_t *p) {
    r->top = read_i16(p);
    r->left = read_i16(p + 2);
    r->bottom = read_i16(p + 4);
    r->right = read_i16(p + 6);
}

static ptrdiff_t data_version(const struct unrez_pict_callbacks *cb,
                              int version, int opcode, const uint8_t *start,
                              const uint8_t *end) {
    int r;
    if (start == end) {
        return pict_eof(cb, opcode);
    }
    if (*start != version) {
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, "invalid format version");
        return -1;
    }
    r = cb->opcode(cb->ctx, opcode, start, 1);
    if (r != 0) {
        return -1;
    }
    return 1;
}

static ptrdiff_t data_end(const struct unrez_pict_callbacks *cb, int version,
                          int opcode, const uint8_t *start,
                          const uint8_t *end) {
    (void)cb;
    (void)version;
    (void)opcode;
    (void)start;
    (void)end;
    return -1;
}

static ptrdiff_t data_data16(const struct unrez_pict_callbacks *cb, int version,
                             int opcode, const uint8_t *start,
                             const uint8_t *end) {
    const uint8_t *ptr = start;
    int size, r;
    (void)version;
    if (end - ptr < 2) {
        return pict_eof(cb, opcode);
    }
    size = read_i16(ptr);
    ptr += 2;
    if (size < 0) {
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, kErrInvalidLength);
        return -1;
    }
    if (size > end - ptr) {
        return pict_eof(cb, opcode);
    }
    ptr += size;
    r = cb->opcode(cb->ctx, opcode, start, ptr - start);
    if (r != 0) {
        return -1;
    }
    return ptr - start;
}

static ptrdiff_t data_data32(const struct unrez_pict_callbacks *cb, int version,
                             int opcode, const uint8_t *start,
                             const uint8_t *end) {
    const uint8_t *ptr = start;
    int size, r;
    (void)version;
    if (end - ptr < 4) {
        return pict_eof(cb, opcode);
    }
    size = read_i32(ptr);
    ptr += 4;
    if (size < 0) {
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, kErrInvalidLength);
        return -1;
    }
    if (size > end - ptr) {
        return pict_eof(cb, opcode);
    }
    ptr += size;
    r = cb->opcode(cb->ctx, opcode, start, ptr - start);
    if (r != 0) {
        return -1;
    }
    return ptr - start;
}

static ptrdiff_t data_longcomment(const struct unrez_pict_callbacks *cb,
                                  int version, int opcode, const uint8_t *start,
                                  const uint8_t *end) {
    const uint8_t *ptr = start;
    int size, r;
    (void)version;
    if (end - ptr < 4) {
        return pict_eof(cb, opcode);
    }
    size = read_i16(ptr + 2);
    ptr += 4;
    if (size < 0) {
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, kErrInvalidLength);
        return -1;
    }
    if (size > end - ptr) {
        return pict_eof(cb, opcode);
    }
    ptr += size;
    r = cb->opcode(cb->ctx, opcode, start, ptr - start);
    if (r != 0) {
        return -1;
    }
    return ptr - start;
}

static ptrdiff_t data_region(const struct unrez_pict_callbacks *cb, int version,
                             int opcode, const uint8_t *start,
                             const uint8_t *end) {
    int size, r;
    (void)version;
    if (end - start < 2) {
        return pict_eof(cb, opcode);
    }
    size = read_u16(start);
    if (size < 2) {
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, "invalid region size");
        return -1;
    }
    if (size != 10) {
        cb->error(cb->ctx, kUnrezErrUnsupported, opcode,
                  "unsupported region format");
        return -1;
    }
    if (size > end - start) {
        return pict_eof(cb, opcode);
    }
    r = cb->opcode(cb->ctx, opcode, start, size);
    if (r != 0) {
        return -1;
    }
    return size;
}

static ptrdiff_t data_pattern(const struct unrez_pict_callbacks *cb,
                              int version, int opcode, const uint8_t *start,
                              const uint8_t *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrUnsupported, opcode, "patterns not supported");
    return -1;
}

static ptrdiff_t data_text(const struct unrez_pict_callbacks *cb, int version,
                           int opcode, const uint8_t *start,
                           const uint8_t *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrUnsupported, opcode, "text not supported");
    return -1;
}

static ptrdiff_t data_not_determined(const struct unrez_pict_callbacks *cb,
                                     int version, int opcode,
                                     const uint8_t *start, const uint8_t *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrInvalid, opcode,
              "reserved opcode has undetermined size");
    return -1;
}

static ptrdiff_t data_polygon(const struct unrez_pict_callbacks *cb,
                              int version, int opcode, const uint8_t *start,
                              const uint8_t *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrUnsupported, opcode, "polygons not supported");
    return -1;
}

enum {
    kBitMapSize = 14,
    kPixMapSize = 50,
};

#if 0
static void read_bitmap(struct unrez_pixdata *m, const uint8_t *p) {
    /*
     * The older cousin to PixMap for B&W graphics.
     * off len
     *   0   4  baseAddr (ignored)
     *   4   2  rowBytes
     *   6   8  bounds
     * Total size: 14
     *
     * For this function, however, we skip baseAddr and stort with rowBytes.
     */
    m->rowBytes = read_i16(p + 0);
    read_rect(&m->bounds, p + 2);
}
#endif

static void read_pixmap(struct unrez_pixdata *m, const uint8_t *p) {
    /*
     * From Imaging With QuickDraw p 4-10 "The pixel map"
     * Or struct PixMap in QuickDraw.h
     * off len
     *   0   4  baseAddr (ignored)
     *   4   2  rowBytes
     *   6   8  bounds
     *  14   2  pmVersion (ignored, flag 4 = 32-bit clean)
     *  16   2  packType
     *  18   4  packSize
     *  22   4  hRes
     *  26   4  vRes
     *  30   2  pixelType
     *  32   2  pixelSize
     *  34   2  cmpCount
     *  36   2  cmpSize
     *  38   4  planeBytes (ignored)
     *  42   4  pmTable (ignored)
     *  46   4  pmExt (ignored)
     * Total size: 50
     *
     * For this function, however, we skip baseAddr and start with
     * rowBytes. Note that the high bit of rowBytes is used to tell the
     * difference between monochrome BitMap and color PixMap structures, so we
     * strip it out here.
     */
    m->rowBytes = read_i16(p + 0) & 0x7fff;
    read_rect(&m->bounds, p + 2);
    m->packType = read_i16(p + 12);
    m->packSize = read_i32(p + 14);
    m->hRes = read_i32(p + 18);
    m->vRes = read_i32(p + 22);
    m->pixelType = read_i16(p + 26);
    m->pixelSize = read_i16(p + 28);
    m->cmpCount = read_i16(p + 30);
    m->cmpSize = read_i16(p + 32);
}

/*
 * Error return codes for the unpacking functions, unpack_XXX(), and bitmap
 * decoding functions, read_XXX(). These are not used by the opcode handlers,
 * data_XXX(), because those functions can signal errors through the callbacks.
 */
enum {
    /* Unexpected end of file. */
    kErrEof = -1,
    /* Bad pixel data. */
    kErrBadPixels = -2,
    /* See errno. */
    kErrErrno = -3
};

/*
 * Decode 8-bit run-length encoded data. This uses the PackBits compression
 * scheme.
 *
 * See "TN1023: Understanding PackBits" (no longer accessible)
 * http://developer.apple.com/technotes/tn/tn1023.html
 */
static int unpack_8(uint8_t *dptr, uint8_t *dend, const uint8_t *sptr,
                    const uint8_t *send) {
    int control, runsize;
    while (sptr < send) {
        control = (int8_t)*sptr;
        sptr++;
        if (control > 0) {
            /* Literal data follows. */
            runsize = control + 1;
            if (send - sptr < runsize) {
                return kErrEof;
            }
            if (dend - dptr < runsize) {
                return kErrBadPixels;
            }
            memcpy(dptr, sptr, runsize);
            sptr += runsize;
            dptr += runsize;
        } else if (control != -128) {
            /* Repeated data follows. */
            runsize = (-control) + 1;
            if (send - sptr < 1) {
                return kErrEof;
            }
            if (dend - dptr < runsize) {
                return kErrBadPixels;
            }
            memset(dptr, *sptr, runsize);
            sptr++;
            dptr += runsize;
        }
        /* Control 0x80 is ignored, see tech note. */
    }
    memset(dptr, 0, dend - dptr);
    return 0;
}

/*
 * Decode 16-bit run-length encoded data. This is similar to the PackBits
 * compression scheme for 8-bit data, but operates on 16-bit units instead of
 * 8-bit units. The control bytes are still 8-bit, however. This function will
 * also convert data to native byte order.
 *
 * See "TN1023: Understanding PackBits" (no longer accessible)
 * http://developer.apple.com/technotes/tn/tn1023.html
 */
static int unpack_16(uint16_t *dptr, uint16_t *dend, const uint8_t *sptr,
                     const uint8_t *send) {
    int control, runsize, i;
    uint16_t v;
    while (sptr < send) {
        control = (int8_t)*sptr;
        sptr++;
        if (control > 0) {
            /* Literal data follows. */
            runsize = control + 1;
            if (send - sptr < runsize * 2) {
                return kErrEof;
            }
            if (dend - dptr < runsize) {
                return kErrBadPixels;
            }
            for (i = 0; i < runsize; i++) {
                dptr[i] = read_u16(sptr + i * 2);
            }
            sptr += runsize * 2;
            dptr += runsize;
        } else if (control != -128) {
            /* Repeated data follows. */
            runsize = (-control) + 1;
            if (send - sptr < 2) {
                return kErrEof;
            }
            if (dend - dptr < runsize) {
                return kErrBadPixels;
            }
            v = read_u16(sptr);
            sptr += 2;
            for (i = 0; i < runsize; i++) {
                dptr[i] = v;
            }
            dptr += runsize;
        }
        /* Control 0x80 is ignored, see tech note. */
    }
    memset(dptr, 0, (dend - dptr) * sizeof(*dptr));
    return 0;
}

/* Read an 8-bit packed image (pack type 0). */
static ptrdiff_t read_packed_8(int rowcount, int rowbytes, uint8_t *dest,
                               const uint8_t *start, const uint8_t *end) {
    const uint8_t *ptr = start;
    int rowsize, i, r;
    for (i = 0; i < rowcount; i++) {
        if (rowbytes <= 250) {
            if (end - ptr < 1) {
                return kErrEof;
            }
            rowsize = *ptr;
            ptr++;
        } else {
            if (end - ptr < 2) {
                return kErrEof;
            }
            rowsize = read_u16(ptr);
            ptr += 2;
        }
        if (end - ptr < rowsize) {
            return kErrEof;
        }
        r = unpack_8(dest + i * rowbytes, dest + (i + 1) * rowbytes, ptr,
                     ptr + rowsize);
        if (r != 0) {
            return r;
        }
        ptr += rowsize;
    }
    return ptr - start;
}

/* Read an 8-bit unpacked image (pack type 1). */
static ptrdiff_t read_unpacked_8(int rowcount, int rowbytes, uint8_t *dest,
                                 const uint8_t *start, const uint8_t *end) {
    int size = rowbytes * rowcount;
    if (end - start < size) {
        return kErrEof;
    }
    memcpy(dest, start, size);
    return size;
}

/* Read a 16-bit packed image (pack type 3). */
static ptrdiff_t read_packed_16(int rowcount, int rowbytes, uint16_t *dest,
                                const uint8_t *start,
                                const uint8_t *end) {
    const uint8_t *ptr = start;
    int rowpix, rowsize, i, r;
    rowpix = rowbytes >> 1;
    for (i = 0; i < rowcount; i++) {
        if (rowbytes <= 250) {
            if (end - ptr < 1) {
                return kErrEof;
            }
            rowsize = *ptr;
            ptr++;
        } else {
            if (end - ptr < 2) {
                return kErrEof;
            }
            rowsize = read_u16(ptr);
            ptr += 2;
        }
        if (end - ptr < rowsize) {
            return kErrEof;
        }
        r = unpack_16(dest + i * rowpix, dest + (i + 1) * rowpix, ptr,
                      ptr + rowsize);
        if (r != 0) {
            return r;
        }
        ptr += rowsize;
    }
    return ptr - start;
}

/* Read a 16-bit unpacked image (pack type 1). */
static ptrdiff_t read_unpacked_16(int rowcount, int rowbytes, uint16_t *dest,
                                  const uint8_t *start, const uint8_t *end) {
    int pixcount, size, i;
    size = rowbytes * rowcount;
    pixcount = size >> 1;
    if (end - start < size) {
        return kErrEof;
    }
    for (i = 0; i < pixcount; i++) {
        dest[i] = read_u16(start + i * 2);
    }
    return size;
}

/*
 * Unshuffle shuffled 32-bit pixels. The pixels are stored by row, component,
 * then column. A row of pixels will be stored with all the red components, then
 * the green, then blue. This makes the compression more efficient.
 */
static void unshuffle_32(uint8_t *dest, const uint8_t *src, int n) {
    int cmp, x;
    for (cmp = 0; cmp < 3; ++cmp) {
        for (x = 0; x < n; x++) {
            dest[x * 4 + cmp] = src[cmp * n + x];
        }
    }
    for (x = 0; x < n; x++) {
        dest[x * 4 + 3] = 0;
    }
}

/* Read a 32-bit unpacked image (pack type 1). */
static ptrdiff_t read_unpacked_32(int rowcount, int rowbytes, uint8_t *dest,
                                  const uint8_t *start, const uint8_t *end) {
    int i, rowpix, srcrowbytes;
    rowpix = rowbytes >> 2;
    srcrowbytes = rowpix * 3;
    if (end - start < srcrowbytes * rowcount) {
        return kErrEof;
    }
    for (i = 0; i < rowcount; i++) {
        unshuffle_32(dest + i * rowbytes, start + i * srcrowbytes, rowpix);
    }
    return srcrowbytes * rowcount;
}

/* Read a 32-bit packed image (pack type 4). */
static ptrdiff_t read_packed_32(int rowcount, int rowbytes, uint8_t *dest,
                                const uint8_t *start, const uint8_t *end) {
    const uint8_t *ptr = start;
    uint8_t *tmp;
    int rowpix, srcrowbytes, rowsize, i, r;
    rowpix = rowbytes >> 2;
    srcrowbytes = rowpix * 3;
    tmp = malloc(srcrowbytes);
    if (tmp == NULL) {
        return kErrErrno;
    }
    for (i = 0; i < rowcount; i++) {
        if (rowbytes <= 250) {
            if (end - ptr < 1) {
                r = kErrEof;
                goto done;
            }
            rowsize = *ptr;
            ptr++;
        } else {
            if (end - ptr < 2) {
                r = kErrEof;
                goto done;
            }
            rowsize = read_u16(ptr);
            ptr += 2;
        }
        r = unpack_8(tmp, tmp + srcrowbytes, ptr, ptr + rowsize);
        if (r != 0) {
            goto done;
        }
        unshuffle_32(dest + i * rowbytes, tmp, rowpix);
        ptr += rowsize;
    }
    r = 0;
    goto done;

done:
    free(tmp);
    return r == 0 ? ptr - start : r;
}

static ptrdiff_t data_pixel_data(const struct unrez_pict_callbacks *cb,
                                 int version, int opcode, const uint8_t *start,
                                 const uint8_t *end) {
    char buf[128];
    const uint8_t *ptr = start;
    struct unrez_pixdata pix = {0};
    struct unrez_color *colors = NULL;
    int success = 0;
    int i, n, r, rowcount, rowbytes, align;
    ptrdiff_t pr;

    (void)version;

    /*
     * These opcodes record a blit operation, known as CopyBits in
     * QuickDraw. The source and destination will be described in BitMap or
     * PixMap data structures. In pictures, the BitMap or PixMap is copied
     * directly to the picture data. If the structure is a PixMap, then the high
     * bit of rowBytes will be set to distinguish it from a BitMap. PixMap
     * structures only appear in version 2 pictures.
     */

    /*
     * Decode the operation header.
     */
    switch (opcode) {
    /* case kOp_BitsRect: */
    /* case kOp_BitsRgn: */
    case kOp_PackBitsRect:
        /*
         * len
         *  46  PixMap, no baseAddr
         * >=8  ColorTable (len = 12 + 8 * ctSize)
         *   8  srcRect
         *   8  destRect
         *   2  mode
         */
        if (end - ptr < 46 + 8) {
            goto eof;
        }
        read_pixmap(&pix, ptr);
        ptr += 46;
        /*
         * ColorTable
         *   ctSeed: int32
         *   ctFlags: int16
         *   ctSize: int16
         * Color
         *   value: int16
         *   r, g, b: uint16
         * Total size: 8 + 8 * ctSize
         */
        n = read_i16(ptr + 6) + 1;
        ptr += 8;
        if (n < 0 || n > 256) {
            snprintf(buf, sizeof(buf), "invalid color table size: %d", n);
            cb->error(cb->ctx, kUnrezErrInvalid, opcode, buf);
            goto done;
        }
        if (end - ptr < 8 * n + 18) {
            goto eof;
        }
        colors = malloc(sizeof(*pix.ctTable) * n);
        if (colors == NULL) {
            cb->error(cb->ctx, errno, opcode, NULL);
            goto done;
        }
        for (i = 0; i < n; i++, ptr += 8) {
            colors[i].v = read_i16(ptr);
            colors[i].r = read_u16(ptr + 2);
            colors[i].g = read_u16(ptr + 4);
            colors[i].b = read_u16(ptr + 6);
        }
        pix.ctSize = n;
        pix.ctTable = colors;
        break;
    /* case kOp_PackBitsRgn: */
    case kOp_DirectBitsRect:
        /*
         * len
         *  50  PixMap, with baseAddr = $000000FF for compatibility
         *   8  srcRect
         *   8  destRect
         *   2  mode
         */
        if (end - ptr < 68) {
            goto eof;
        }
        read_pixmap(&pix, ptr + 4);
        ptr += 50;
        break;
    /* case kOp_DirectBitsRgn: */
    default:
        cb->error(cb->ctx, kUnrezErrInvalid, opcode,
                  "unsupported pixel data opcode");
        goto done;
    }

    read_rect(&pix.srcRect, ptr);
    read_rect(&pix.destRect, ptr + 8);
    pix.mode = read_i16(ptr + 16);
    ptr += 18;

    align = pix.pixelSize == 32 ? 3 : 1;
    rowbytes = pix.rowBytes;
    if ((rowbytes & align) != 0 || rowbytes <= 0 || rowbytes > 0x4000) {
        goto bad_rowbytes;
    }
    rowcount = pix.bounds.bottom - pix.bounds.top;
    if (rowcount <= 0) {
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, "invalid bounds");
        goto done;
    }
    /* Can't overflow 32-bit signed int. */
    pix.data = malloc(rowbytes * rowcount);
    if (pix.data == NULL) {
        cb->error(cb->ctx, errno, opcode, NULL);
        goto done;
    }

    switch (pix.rowBytes < 8 ? 1 : pix.packType) {
    case 0:
        if (pix.pixelSize != 8) {
            goto bad_packtype;
        }
        pr = read_packed_8(rowcount, rowbytes, pix.data, ptr, end);
        break;
    case 1:
        switch (pix.pixelSize) {
        case 8:
            pr = read_unpacked_8(rowcount, rowbytes, pix.data, ptr, end);
            break;
        case 16:
            pr = read_unpacked_16(rowcount, rowbytes, pix.data, ptr, end);
            break;
        case 32:
            pr = read_unpacked_32(rowcount, rowbytes, pix.data, ptr, end);
            break;
        default:
            goto bad_packtype;
        }
        break;
    case 3:
        if (pix.pixelSize != 16) {
            goto bad_packtype;
        }
        pr = read_packed_16(rowcount, rowbytes, pix.data, ptr, end);
        break;
    case 4:
        if (pix.pixelSize != 32) {
            goto bad_packtype;
        }
        pr = read_packed_32(rowcount, rowbytes, pix.data, ptr, end);
        break;
    default:
        snprintf(buf, sizeof(buf), "unsupported packType value: %d",
                 pix.packType);
        cb->error(cb->ctx, kUnrezErrUnsupported, opcode, buf);
        goto done;
    }
    if (pr < 0) {
        switch (pr) {
        default:
        case kErrEof:
            goto eof;
        case kErrBadPixels:
            cb->error(cb->ctx, kUnrezErrInvalid, opcode, "invalid pixel data");
            goto done;
        case kErrErrno:
            cb->error(cb->ctx, errno, opcode, NULL);
            goto done;
        }
    }
    ptr += pr;
    r = cb->pixels(cb->ctx, opcode, &pix);
    if (r == 0) {
        success = 1;
    }
    goto done;

done:
    unrez_pixdata_destroy(&pix);
    return success ? ptr - start : -1;

eof:
    pict_eof(cb, opcode);
    goto done;

bad_rowbytes:
    snprintf(buf, sizeof(buf),
             "bad number of bytes per row: pixelSize=%d, rowBytes=%d",
             pix.pixelSize, pix.rowBytes);
    cb->error(cb->ctx, kUnrezErrInvalid, opcode, buf);
    goto done;

bad_packtype:
    snprintf(buf, sizeof(buf),
             "bad pixel packing type: pixelSize=%d, packType=%d", pix.pixelSize,
             pix.packType);
    cb->error(cb->ctx, kUnrezErrInvalid, opcode, buf);
    goto done;
}

static ptrdiff_t data_quicktime(const struct unrez_pict_callbacks *cb,
                                int version, int opcode, const uint8_t *start,
                                const uint8_t *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrUnsupported, opcode,
              "embedded QuickTime images not supported");
    return -1;
}

typedef ptrdiff_t (*data_handler_t)(const struct unrez_pict_callbacks *cb,
                                    int version, int opcode,
                                    const uint8_t *start, const uint8_t *end);

static const data_handler_t kDataHandlers[] = {
    data_version,        data_end,     data_data16,     data_data32,
    data_longcomment,    data_region,  data_pattern,    data_text,
    data_not_determined, data_polygon, data_pixel_data, data_quicktime,
};

void unrez_pict_decode(const struct unrez_pict_callbacks *cb, const void *data,
                       size_t size) {
    const uint8_t *ptr = data, *end = ptr + size;
    int version, r, opcode, opdata;
    struct unrez_rect frame;
    const struct opcode_range *range;
    data_handler_t handler;
    ptrdiff_t hr = 0;

    if (size < 11) {
        cb->error(cb->ctx, kUnrezErrInvalid, -1, kErrUnexpectedEof);
        return;
    }

    /*
     * Header - from Imaging With QuickDraw p. 7-28
     * off len
     *   0   2  size for a version 1 picture (ignored for version 2)
     *   2   2  frame top
     *   4   2  frame left
     *   6   2  frame bottom
     *   8   2  frame right
     *  10 var  picture
     */
    read_rect(&frame, ptr + 2);
    ptr += 10;

    /*
     * Figure out the picture version. See A-3 "Version and Header Opcodes".
     * Since $00 is no-op, $0011 works as a version opcode which is compatible
     * both with version 1 (8-bit opcodes) and version 2 (16-bit opcodes). It is
     * followed by $FF, which tells version 1 parsers to stop parsing. Version 2
     * parsers skip the $FF because the payload of a version opcode is an odd
     * number of bytes, and version 2 parsers read opcodes on 16-bit boundaries.
     */
    if (end - ptr >= 2) {
        opcode = read_u16(ptr);
        if (opcode == 0x11) {
            version = 2;
        } else {
            version = 1;
        }
    } else {
        version = 1;
    }

    r = cb->header(cb->ctx, version, &frame);
    if (r != 0) {
        return;
    }

    while (1) {
        if (version == 1) {
            if (ptr == end) {
                break;
            }
            opcode = *ptr;
            ptr++;
            opdata = kOpcodeDataTable[opcode];
        } else {
            if (end - ptr < 2 + (hr & 1)) {
                break;
            }
            ptr += (hr & 1);
            opcode = read_u16(ptr);
            ptr += 2;
            if (opcode <= 0xff) {
                opdata = kOpcodeDataTable[opcode];
            } else {
                range = find_opcode(opcode);
                if (range == NULL) {
                    cb->error(cb->ctx, kUnrezErrInvalid, opcode,
                              "unknown opcode");
                    return;
                }
                opdata = range->data;
            }
        }
        if (opdata >= 0) {
            r = cb->opcode(cb->ctx, opcode, ptr, opdata);
            if (r != 0) {
                return;
            }
            ptr += opdata;
            hr = opdata;
        } else {
            handler = kDataHandlers[-1 - opdata];
            hr = handler(cb, version, opcode, ptr, end);
            if (hr < 0) {
                return;
            }
            ptr += hr;
        }
    }

    cb->error(cb->ctx, kUnrezErrInvalid, -1, kErrUnexpectedEof);
}

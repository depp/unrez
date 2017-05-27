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

static void read_rect(struct unrez_rect *r, const unsigned char *p) {
    r->top = read_i16(p);
    r->left = read_i16(p + 2);
    r->bottom = read_i16(p + 4);
    r->right = read_i16(p + 6);
}

static ptrdiff_t data_version(const struct unrez_pict_callbacks *cb,
                              int version, int opcode,
                              const unsigned char *start,
                              const unsigned char *end) {
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
                          int opcode, const unsigned char *start,
                          const unsigned char *end) {
    (void)cb;
    (void)version;
    (void)opcode;
    (void)start;
    (void)end;
    return -1;
}

static ptrdiff_t data_data16(const struct unrez_pict_callbacks *cb, int version,
                             int opcode, const unsigned char *start,
                             const unsigned char *end) {
    const unsigned char *ptr = start;
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
                             int opcode, const unsigned char *start,
                             const unsigned char *end) {
    const unsigned char *ptr = start;
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
                                  int version, int opcode,
                                  const unsigned char *start,
                                  const unsigned char *end) {
    const unsigned char *ptr = start;
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
                             int opcode, const unsigned char *start,
                             const unsigned char *end) {
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
                              int version, int opcode,
                              const unsigned char *start,
                              const unsigned char *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrUnsupported, opcode, "patterns not supported");
    return -1;
}

static ptrdiff_t data_text(const struct unrez_pict_callbacks *cb, int version,
                           int opcode, const unsigned char *start,
                           const unsigned char *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrUnsupported, opcode, "text not supported");
    return -1;
}

static ptrdiff_t data_not_determined(const struct unrez_pict_callbacks *cb,
                                     int version, int opcode,
                                     const unsigned char *start,
                                     const unsigned char *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrInvalid, opcode,
              "reserved opcode has undetermined size");
    return -1;
}

static ptrdiff_t data_polygon(const struct unrez_pict_callbacks *cb,
                              int version, int opcode,
                              const unsigned char *start,
                              const unsigned char *end) {
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
static void read_bitmap(struct unrez_pixdata *m, const unsigned char *p) {
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

static void read_pixmap(struct unrez_pixdata *m, const unsigned char *p) {
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
     * For this function, however, we skip baseAddr and start with rowBytes.
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

enum {
    kErrEof = -1,
    kErrBadPixels = -2,
    kErrMemory = -3,
};

/* Decode 8-bit run-length encoded data. */
static int decode_8(unsigned char *dptr, unsigned char *dend,
                    const unsigned char *sptr, const unsigned char *send) {
    int control, runsize;
    /*
     * See "TN1023: Understanding PackBits" (no longer accessible)
     * http://developer.apple.com/technotes/tn/tn1023.html
     */
    while (sptr < send) {
        control = (signed char)*sptr;
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

/* Decode 16-bit run-length encoded data. */
static int decode_16(uint16_t *dptr, uint16_t *dend, const unsigned char *sptr,
                     const unsigned char *send) {
    int control, runsize, i;
    uint16_t v;
    /*
     * See "TN1023: Understanding PackBits" (no longer accessible)
     * http://developer.apple.com/technotes/tn/tn1023.html
     */
    while (sptr < send) {
        control = (signed char)*sptr;
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

/* Read an 8-bit packed image. */
static ptrdiff_t read_packed_8(int height, int width, unsigned char *dptr,
                               int drowbytes, const unsigned char *sstart,
                               const unsigned char *send, int srowbytes) {
    const unsigned char *sptr = sstart;
    int rowsize, y, r;
    (void)width;
    for (y = 0; y < height; y++) {
        if (srowbytes <= 250) {
            if (send - sptr < 1) {
                return kErrEof;
            }
            rowsize = *sptr;
            sptr++;
        } else {
            if (send - sptr < 2) {
                return kErrEof;
            }
            rowsize = read_u16(sptr);
            sptr += 2;
        }
        if (send - sptr < rowsize) {
            return kErrEof;
        }
        r = decode_8(dptr, dptr + srowbytes, sptr, sptr + rowsize);
        if (r != 0) {
            return r;
        }
        memset(dptr + srowbytes, 0, drowbytes - srowbytes);
        sptr += rowsize;
        dptr += drowbytes;
    }
    return sptr - sstart;
}

static ptrdiff_t read_unpacked_8(int height, int width, unsigned char *dptr,
                                 int drowbytes, const unsigned char *sstart,
                                 const unsigned char *send, int srowbytes) {
    const unsigned char *sptr = sstart;
    int y;
    (void)width;
    if (send - sstart < srowbytes * height) {
        return kErrEof;
    }
    for (y = 0; y < height; y++) {
        memcpy(dptr, sptr, srowbytes);
        memset(dptr + srowbytes, 0, drowbytes - srowbytes);
        sptr += srowbytes;
        dptr += drowbytes;
    }
    return sptr - sstart;
}

static void unpack_16(unsigned char *dest, const uint16_t *src, int width) {
    unsigned v;
    int x;
    for (x = 0; x < width; x++) {
        v = src[x];
        dest[x * 4 + 0] = ((v >> 7) & 0xf8) | ((v >> 12) & 7);
        dest[x * 4 + 1] = ((v >> 2) & 0xf8) | ((v >> 7) & 7);
        dest[x * 4 + 2] = ((v << 3) & 0xf8) | ((v >> 2) & 7);
        dest[x * 4 + 3] = 0;
    }
}

static ptrdiff_t read_packed_16(int height, int width, unsigned char *dptr,
                                int drowbytes, const unsigned char *sstart,
                                const unsigned char *send, int srowbytes) {
    const unsigned char *sptr = sstart;
    uint16_t *tmp;
    int rowsize, y, r;
    /* srowbytes always even */
    tmp = malloc(srowbytes);
    if (tmp == NULL) {
        return kErrMemory;
    }
    for (y = 0; y < height; y++) {
        if (srowbytes <= 250) {
            if (send - sptr < 1) {
                r = kErrEof;
                goto done;
            }
            rowsize = *sptr;
            sptr++;
        } else {
            if (send - sptr < 2) {
                r = kErrEof;
                goto done;
            }
            rowsize = read_u16(sptr);
            sptr += 2;
        }
        if (send - sptr < rowsize) {
            r = kErrEof;
            goto done;
        }
        r = decode_16(tmp, tmp + srowbytes / 2, sptr, sptr + rowsize);
        if (r != 0) {
            goto done;
        }
        unpack_16(dptr, tmp, width);
        memset(dptr + width * 4, 0, drowbytes - width * 4);
        sptr += rowsize;
        dptr += drowbytes;
    }
    r = 0;
    goto done;

done:
    free(tmp);
    return r == 0 ? sptr - sstart : r;
}

static ptrdiff_t read_unpacked_16(int height, int width, unsigned char *dptr,
                                  int drowbytes, const unsigned char *sstart,
                                  const unsigned char *send, int srowbytes) {
    const unsigned char *sptr = sstart;
    uint16_t *tmp;
    int y, x, psize;
    (void)psize;
    if (send - sstart < srowbytes * height) {
        return kErrEof;
    }
    /* srowbytes always even */
    tmp = malloc(srowbytes);
    if (tmp == NULL) {
        return kErrMemory;
    }
    psize = width * 4;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            tmp[x] = read_u16(sptr + x * 2);
        }
        sptr += srowbytes;
        unpack_16(dptr, tmp, width);
        memset(dptr + psize, 0, drowbytes - psize);
        dptr += drowbytes;
    }
    free(tmp);
    return sptr - sstart;
}

static void unpack_32(unsigned char *dest, const unsigned char *src,
                      int width) {
    int cmp, x;
    /*
     * Unpacked 32-bit pixels are stored by row, component, then
     * column. So each row has all the red samples, all the green, then
     * all the blue.
     */
    for (cmp = 0; cmp < 3; ++cmp) {
        for (x = 0; x < width; x++) {
            dest[x * 4 + cmp] = src[cmp * width + x];
        }
    }
    for (x = 0; x < width; x++) {
        dest[x * 4 + 3] = 0;
    }
}

static ptrdiff_t read_unpacked_32(int height, int width, unsigned char *dptr,
                                  int drowbytes, const unsigned char *sstart,
                                  const unsigned char *send, int srowbytes) {
    const unsigned char *sptr = sstart;
    int y, psize;
    (void)psize;
    if (send - sstart < srowbytes * height) {
        return kErrEof;
    }
    psize = width * 4;
    for (y = 0; y < height; y++) {
        unpack_32(dptr, sptr, width);
        memset(dptr + psize, 0, drowbytes - psize);
        sptr += srowbytes;
        dptr += drowbytes;
    }
    return sptr - sstart;
}

static ptrdiff_t read_packed_32(int height, int width, unsigned char *dptr,
                                int drowbytes, const unsigned char *sstart,
                                const unsigned char *send, int srowbytes) {
    const unsigned char *sptr = sstart;
    unsigned char *tmp;
    int rowsize, y, r;
    tmp = malloc(srowbytes);
    if (tmp == NULL) {
        return kErrMemory;
    }
    for (y = 0; y < height; y++) {
        if (srowbytes <= 250) {
            if (send - sptr < 1) {
                return kErrEof;
            }
            rowsize = *sptr;
            sptr++;
        } else {
            if (send - sptr < 2) {
                return kErrEof;
            }
            rowsize = read_u16(sptr);
            sptr += 2;
        }
        r = decode_8(tmp, tmp + srowbytes, sptr, sptr + rowsize);
        if (r != 0) {
            goto done;
        }
        unpack_32(dptr, tmp, width);
        memset(dptr + width * 4, 0, drowbytes - width * 4);
        sptr += rowsize;
        dptr += drowbytes;
    }
    r = 0;
    goto done;

done:
    free(tmp);
    return r == 0 ? sptr - sstart : r;
}

static ptrdiff_t data_pixel_data(const struct unrez_pict_callbacks *cb,
                                 int version, int opcode,
                                 const unsigned char *start,
                                 const unsigned char *end) {
    void *pixdata = NULL;
    char buf[128];
    const unsigned char *ptr = start;
    struct unrez_pixdata pix = {0};
    struct unrez_color *colors = NULL;
    int success = 0;
    int i, n, r, width, height, srcRowBytes, destRowBytes;
    size_t size;
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

    srcRowBytes = pix.rowBytes;
    if ((srcRowBytes & 1) != 0 || srcRowBytes <= 0 || srcRowBytes > 0x4000) {
        goto bad_rowbytes;
    }
    height = pix.bounds.bottom - pix.bounds.top;
    width = pix.bounds.right - pix.bounds.left;
    if (height <= 0 || width <= 0) {
        snprintf(buf, sizeof(buf),
                 "invalid bounds: top=%d, left=%d, bottom=%d, right=%d",
                 pix.bounds.top, pix.bounds.left, pix.bounds.bottom,
                 pix.bounds.right);
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, buf);
        goto done;
    }
    switch (pix.pixelSize) {
    case 8:
        pix.dataPixelSize = 8;
        destRowBytes = (width + 3) & ~3;
        if (srcRowBytes < width) {
            goto bad_rowbytes;
        }
        break;
    case 16:
        pix.dataPixelSize = 32;
        destRowBytes = width * 4;
        if (srcRowBytes < width * 2) {
            goto bad_rowbytes;
        }
        break;
    case 32:
        pix.dataPixelSize = 32;
        destRowBytes = width * 4;
        srcRowBytes = (srcRowBytes * 3) >> 2;
        if (srcRowBytes < width * 3) {
            goto bad_rowbytes;
        }
        break;
    default:
        goto bad_pixelsize;
    }
    if ((unsigned)destRowBytes > (unsigned)-1 / height) {
        cb->error(cb->ctx, kUnrezErrInvalid, opcode, "image too large");
        goto done;
    }
    size = (size_t)destRowBytes * (size_t)height;
    pixdata = malloc(size);
    if (pixdata == NULL) {
        cb->error(cb->ctx, errno, opcode, NULL);
        goto done;
    }
    pix.data = pixdata;
    pix.rowBytes = destRowBytes;

    switch (pix.rowBytes < 8 ? 1 : pix.packType) {
    case 0:
        if (pix.pixelSize != 8) {
            goto bad_pixelsize;
        }
        pr = read_packed_8(height, width, pixdata, destRowBytes, ptr, end,
                           srcRowBytes);
        break;
    case 1:
        switch (pix.pixelSize) {
        case 8:
            pr = read_unpacked_8(height, width, pixdata, destRowBytes, ptr, end,
                                 srcRowBytes);
            break;
        case 16:
            pr = read_unpacked_16(height, width, pixdata, destRowBytes, ptr,
                                  end, srcRowBytes);
            break;
        case 32:
            pr = read_unpacked_32(height, width, pixdata, destRowBytes, ptr,
                                  end, srcRowBytes);
            break;
        default:
            goto bad_pixelsize;
        }
        break;
    case 3:
        if (pix.pixelSize != 16) {
            goto bad_pixelsize;
        }
        pr = read_packed_16(height, width, pixdata, destRowBytes, ptr, end,
                            srcRowBytes);
        break;
    case 4:
        if (pix.pixelSize != 32) {
            goto bad_pixelsize;
        }
        pr = read_packed_32(height, width, pixdata, destRowBytes, ptr, end,
                            srcRowBytes);
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
        case kErrMemory:
            cb->error(cb->ctx, ENOMEM, opcode, NULL);
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
    free(colors);
    free(pixdata);
    return success ? ptr - start : -1;

eof:
    pict_eof(cb, opcode);
    goto done;

bad_rowbytes:
    snprintf(buf, sizeof(buf), "bad rowBytes value: %d", pix.rowBytes);
    cb->error(cb->ctx, kUnrezErrInvalid, opcode, buf);
    goto done;

bad_pixelsize:
    snprintf(buf, sizeof(buf), "bad pixelSize value: %d", pix.pixelSize);
    cb->error(cb->ctx, kUnrezErrInvalid, opcode, buf);
    goto done;
}

static ptrdiff_t data_quicktime(const struct unrez_pict_callbacks *cb,
                                int version, int opcode,
                                const unsigned char *start,
                                const unsigned char *end) {
    (void)version;
    (void)start;
    (void)end;
    cb->error(cb->ctx, kUnrezErrUnsupported, opcode,
              "embedded QuickTime images not supported");
    return -1;
}

typedef ptrdiff_t (*data_handler_t)(const struct unrez_pict_callbacks *cb,
                                    int version, int opcode,
                                    const unsigned char *start,
                                    const unsigned char *end);

static const data_handler_t kDataHandlers[] = {
    data_version,        data_end,     data_data16,     data_data32,
    data_longcomment,    data_region,  data_pattern,    data_text,
    data_not_determined, data_polygon, data_pixel_data, data_quicktime,
};

void unrez_pict_decode(const struct unrez_pict_callbacks *cb, const void *data,
                       size_t size) {
    const unsigned char *ptr = data, *end = ptr + size;
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

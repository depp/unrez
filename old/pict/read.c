/*
  Apple is pretty good about keeping their ancient ancient
  documentation online.

  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-333.html

  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-458.html
*/
#include "scpimage/pict.h"
#include "scpimage/image.h"
#include "scpbase/binary.h"
#include "scpbase/error_handler.h"
#include "scpbase/error.h"
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <setjmp.h>

static char const
    ERR_EOF[] = "Unexpected end of data",
    ERR_UNKOPCODE[] = "Encountered unknown opcode",
    ERR_UNKVERSION[] = "Unknown PICT version number: %i",
    ERR_REGIONSIZE[] = "Region has invalid size",
    ERR_REGIONFMT[] = "Region has unknown format",
    ERR_PIXMAPVERSION[] = "PixMap has unknown version",
    ERR_CLUTSIZE[] = "Color table has invalid size",
    ERR_PIXELBIG[] = "Pixel data is too large",
    ERR_PIXELDIM[] = "PixMap dimensions are invalid",
    ERR_PIXELSLONG[] = "Pixel data runs past end of row",
    ERR_COMMENTLENGTH[] = "Comment has invalid length",
    ERR_PIXRECTMATCH[] = "Pixel operation dimensions don't match",
    ERR_DEPTH[] = "Pixel depth is unsupported",
    ERR_XFERMODE[] = "Transfer mode is unsupported",
    ERR_ROWBYTES[] = "Number of bytes per row is strange",
    ERR_NOPIX[] = "Picture contains no pixel data";

struct pict_state {
    struct error_handler *errh;
    struct error *err;
    jmp_buf jmpbuf;
    int version;
    struct image *img;
    struct palette *plt;
    uint8_t const *ptr;
    uint8_t const *end;
    // a temporary buffer
    uint8_t *buffer;
    bool has_pixels;
    bool has_error;
};

static void pict_error(struct pict_state *s, char const *msg, ...)
{
    va_list ap;
    s->has_error = true;
    va_start(ap, msg);
    s->errh->message(s->errh->cxt, true, NULL, msg, ap);
    va_end(ap);
}

struct pict_opcode {
    uint16_t opcode;
    // used by "pict_op_nop" to know how many bytes to skip
    unsigned int size;
    char const *name;
    void (*handler)(struct pict_state *state,
                    struct pict_opcode const *opcode);
};

static void
pict_op_version(struct pict_state *state,
                struct pict_opcode const *opcode)
{
    uint8_t const *ptr = state->ptr, *end = state->end;
    int version;
    (void)opcode;
    if (state->version == 2) {
        if ((unsigned int)(end - ptr) < 2) {
            pict_error(state, ERR_EOF);
            return;
        }
        version = read_bu16(ptr);
        ptr += 2;
        if (version != 0x02ff)
            pict_error(state, ERR_UNKVERSION);
    } else {
        if ((unsigned int)(end - ptr) < 2) {
            pict_error(state, ERR_EOF);
            return;
        }
        version = read_u8(ptr);
        ptr += 1;
        if (version != 0x01) {
            pict_error(state, ERR_UNKVERSION, version);
            return;
        }
    }
    state->ptr = ptr;
}

static void
pict_op_header(struct pict_state *state,
               struct pict_opcode const *opcode)
{
    uint8_t const *ptr = state->ptr, *end = state->end;
    int version;
    (void)opcode;
    if ((unsigned int)(end - ptr) < 24) {
        pict_error(state, ERR_EOF);
        return;
    }
    version  = read_bs16(ptr);
    if (version != -2 && version != -1) {
        pict_error(state, ERR_UNKVERSION, version);
        return;
    }
    state->ptr = ptr + 24;
}

static void
pict_op_region(struct pict_state *state,
               struct pict_opcode const *opcode)
{
    uint8_t const *ptr = state->ptr, *end = state->end;
    uint16_t length;
    (void)opcode;
    if ((unsigned int)(end - ptr) < 10) {
        pict_error(state, ERR_EOF);
        return;
    }
    length = read_bu16(ptr);
    if (length < 10) {
        pict_error(state, ERR_REGIONSIZE);
        return;
    }
    if (length > 10) {
        pict_error(state, ERR_REGIONFMT);
        return;
    }
    state->ptr = ptr + 10;
}

static void
pict_read_unpacked_8(struct pict_state *state,
                     uint16_t in_rowbytes, struct image *img)
{
    uint8_t const *sptr = state->ptr, *send = state->end;
    uint8_t *dptr = img->plane[0];
    int32_t out_rowbytes = img->rowbytes;
    int32_t row = 0, height = img->height, width = img->width;
    if ((unsigned int)(send - sptr) < (unsigned int)(height * in_rowbytes)) {
        pict_error(state, ERR_EOF);
        return;
    }
    for (; row < height; ++row, dptr += out_rowbytes) {
        memcpy(dptr, sptr, width);
        if (width < out_rowbytes)
            memset(dptr + width, 0, out_rowbytes - width);
        sptr += in_rowbytes;
    }
    state->ptr = sptr;
}

// Decode run length encoded bytes.
static void
pict_decode_8(struct pict_state *state,
              uint8_t *dptr, int dsize,
              uint8_t const *sptr, int ssize)
{
    uint8_t const *send = sptr + ssize;
    uint8_t *dend = dptr + dsize, v;
    int8_t control;
    unsigned int run_size;
    /* See "TN1023: Understanding PackBits"
       http://developer.apple.com/technotes/tn/tn1023.html */
    while (sptr != send) {
        control = read_s8(sptr);
        sptr += 1;
        if (control > 0) {
            // literal data follows
            run_size = control + 1;
            if ((unsigned int)(send - sptr) < run_size) {
                pict_error(state, ERR_EOF);
                return;
            }
            if ((unsigned int)(dend - dptr) < run_size) {
                pict_error(state, ERR_PIXELSLONG);
                return;
            }
            memcpy(dptr, sptr, run_size);
            sptr += run_size;
            dptr += run_size;
        } else if (control != -128) {
            // repeated data follows
            run_size = (-control) + 1;
            if ((unsigned int)(send - sptr) < 1) {
                pict_error(state, ERR_EOF);
                return;
            }
            if ((unsigned int)(dend - dptr) < run_size) {
                pict_error(state, ERR_PIXELSLONG);
                return;
            }
            v = *sptr;
            sptr += 1;
            memset(dptr, v, run_size);
            dptr += run_size;
        }
        // control 0x80 is ignored, see Apple tech note TN1023
    }
    if (dptr != dend)
        memset(dptr, 0, dend - dptr);
}

static void
pict_read_packed_8(struct pict_state *state,
                   uint16_t in_rowbytes, struct image *img)
{
    uint8_t const *sptr = state->ptr, *send = state->end;
    uint8_t *dptr = img->plane[0];
    uint32_t out_rowbytes = img->rowbytes, packed_rowbytes;
    uint32_t row = 0, height = img->height, width = img->width;
    for (; row < height; ++row, dptr += out_rowbytes) {
        if (in_rowbytes <= 250) {
            if ((unsigned int)(send - sptr) < 1) {
                pict_error(state, ERR_EOF);
                return;
            }
            packed_rowbytes = read_u8(sptr);
            sptr += 1;
        } else {
            if ((unsigned int)(send - sptr) < 2) {
                pict_error(state, ERR_EOF);
                return;
            }
            packed_rowbytes = read_bu16(sptr);
            sptr += 2;
        }
        if ((unsigned int)(send - sptr) < packed_rowbytes) {
            pict_error(state, ERR_EOF);
            return;
        }
        pict_decode_8(state, dptr, in_rowbytes,
                      sptr, packed_rowbytes);
        if (state->has_error)
            return;
        if (width < out_rowbytes)
            memset(dptr + width, 0,
                   out_rowbytes - width);
        sptr += packed_rowbytes;
    }
    state->ptr = sptr;
}

// Decode run length encoded 16 bit integers
// All sizes are in bytes, not words
static void
pict_decode_16(struct pict_state *state,
               uint8_t *dptr, int dsize,
               uint8_t const *sptr, int ssize)
{
    uint8_t const *send = sptr + ssize;
    uint8_t *dend = dptr + dsize;
    int8_t control;
    uint16_t v, *cptr, *cend, run_size;
    while (sptr != send) {
        control = read_s8(sptr);
        sptr += 1;
        if (control > 0) {
            // literal data follows
            run_size = (control + 1) * 2;
            if ((unsigned int)(send - sptr) < run_size) {
                pict_error(state, ERR_EOF);
                return;
            }
            if ((unsigned int)(dend - dptr) < run_size) {
                pict_error(state, ERR_PIXELSLONG);
                return;
            }
            memcpy(dptr, sptr, run_size);
            sptr += run_size;
            dptr += run_size;
        } else if (control != -128) {
            // repeated data follows
            run_size = (-control) + 1;
            if ((unsigned int)(send - sptr) < 2) {
                pict_error(state, ERR_EOF);
                return;
            }
            if ((unsigned int)(dend - dptr) < run_size * 2U) {
                pict_error(state, ERR_PIXELSLONG);
                return;
            }
            memcpy(&v, sptr, 2);
            sptr += 2;
            // dptr should be correctly aligned
            // because it's going into a buffer we allocated
            cptr = (uint16_t *)(void *)dptr;
            cend = cptr + run_size;
            for (; cptr != cend; ++cptr)
                *cptr = v;
            dptr = (uint8_t *)cptr;
        }
        // control 0x80 is ignored, see Apple tech note TN1023
    }
    if (dptr != dend)
        // incomplete row
        memset(dptr, 0, dend - dptr);
}

/* 16-bit pixels are packed in 1.5.5.5 format, where the high bit is
   empty.  This unpacks them into separate bytes.  The new bits are
   filled with copies of the high bits shifted downwards.  */
static void
pict_unpack_16(uint8_t *dest, uint8_t const *src, int count)
{
    uint16_t x;
    for (; count > 0; --count, src += 2, dest += 3) {
        x = read_bu16(src);
        dest[0] = ((x >> 7) & 0xF8) | ((x >> 12) & 7);
        dest[1] = ((x >> 2) & 0xF8) | ((x >> 7) & 7);
        dest[2] = ((x << 3) & 0xF8) | ((x >> 2) & 7);
    }
}

static void
pict_read_unpacked_16(struct pict_state *state,
                      uint16_t in_rowbytes, struct image *img)
{
    uint8_t const *sptr = state->ptr, *send = state->end;
    uint8_t *dptr = img->plane[0];
    uint32_t row = 0, height = img->height, width = img->width;
    uint32_t out_rowbytes = img->rowbytes;
    if ((unsigned int)(send - sptr) < height * in_rowbytes) {
        pict_error(state, ERR_EOF);
        return;
    }
    for (; row < height; ++row, dptr += out_rowbytes) {
        pict_unpack_16(dptr, sptr, width);
        if (width * 3 < out_rowbytes)
            memset(dptr + width * 3, 0,
                   out_rowbytes - width * 3);
        sptr += in_rowbytes;
    }
    state->ptr = sptr;
}

static void
pict_read_packed_16(struct pict_state *state,
                    uint16_t in_rowbytes, struct image *img)
{
    uint8_t const *sptr = state->ptr, *send = state->end;
    uint8_t *row_buf, *dptr = img->plane[0];
    uint32_t out_rowbytes = img->rowbytes, packed_rowbytes;
    uint32_t row = 0, height = img->height, width = img->width;
    state->buffer = row_buf = malloc(in_rowbytes);
    if (!row_buf) {
        error_memory(&state->err);
        longjmp(state->jmpbuf, 1);
    }
    for (; row < height; ++row, dptr += out_rowbytes) {
        if (in_rowbytes <= 250) {
            if ((unsigned int)(send - sptr) < 1) {
                pict_error(state, ERR_EOF);
                return;
            }
            packed_rowbytes = read_u8(sptr);
            sptr += 1;
        } else {
            if ((unsigned int)(send - sptr) < 2) {
                pict_error(state, ERR_EOF);
                return;
            }
            packed_rowbytes = read_bu16(sptr);
            sptr += 2;
        }
        if ((unsigned int)(send - sptr) < packed_rowbytes) {
            pict_error(state, ERR_EOF);
            return;
        }
        pict_decode_16(state, row_buf, in_rowbytes, sptr, packed_rowbytes);
        if (state->has_error)
            return;
        pict_unpack_16(dptr, row_buf, width);
        if (width * 3 < out_rowbytes)
            memset(dptr + width * 3, 0, out_rowbytes - width * 3);
        sptr += packed_rowbytes;
    }
    free(row_buf);
    state->buffer = NULL;
    state->ptr = sptr;
}

/* 32-bit pixels are packed by row, component, then column.  So each
   row has all the red samples, then all the green, then all the blue.
   I have not seen any alpha samples but I am unsure.  */
static void
pict_unpack_32(uint8_t *dest, uint8_t const *src, int count)
{
    uint8_t *dptr;
    int cmp, px;
    for (cmp = 0; cmp < 3; ++cmp) {
        dptr = dest + cmp;
        for (px = 0; px < count; ++px, dptr += 3, ++src)
            *dptr = *src;
    }
}

static void
pict_read_unpacked_32(struct pict_state *state,
                      uint16_t in_rowbytes, struct image *img)
{
    uint8_t const *sptr = state->ptr, *send = state->end;
    uint8_t *dptr = img->plane[0];
    uint32_t row = 0, height = img->height, width = img->width;
    uint32_t out_rowbytes = img->rowbytes;
    if ((unsigned int)(send - sptr) < height * in_rowbytes) {
        pict_error(state, ERR_EOF);
        return;
    }
    for (; row < height; ++row, dptr += out_rowbytes) {
        pict_unpack_32(dptr, sptr, width);
        if (width * 3 < out_rowbytes)
            memset(dptr + width * 3, 0, out_rowbytes - width * 3);
        sptr += in_rowbytes;
    }
    state->ptr = sptr;
}

static void
pict_read_packed_32(struct pict_state *state,
                    uint16_t in_rowbytes, struct image *img)
{
    uint8_t const *sptr = state->ptr, *send = state->end;
    uint8_t *row_buf, *dptr = img->plane[0];
    uint32_t out_rowbytes = img->rowbytes, packed_rowbytes;
    uint32_t row = 0, height = img->height, width = img->width;
    state->buffer = row_buf = malloc(in_rowbytes);
    if (!row_buf) {
        error_memory(&state->err);
        longjmp(state->jmpbuf, 1);
    }
    for (; row < height; ++row, dptr += out_rowbytes) {
        if (in_rowbytes <= 250) {
            if ((unsigned int)(send - sptr) < 1) {
                pict_error(state, ERR_EOF);
                return;
            }
            packed_rowbytes = read_u8(sptr);
            sptr += 1;
        } else {
            if ((unsigned int)(send - sptr) < 2) {
                pict_error(state, ERR_EOF);
                return;
            }
            packed_rowbytes = read_bu16(sptr);
            sptr += 2;
        }
        if ((unsigned int)(send - sptr) < packed_rowbytes) {
            pict_error(state, ERR_EOF);
            return;
        }
        pict_decode_8(state, row_buf, in_rowbytes, sptr, packed_rowbytes);
        if (state->has_error)
            return;
        pict_unpack_32(dptr, row_buf, width);
        if (width * 3 < out_rowbytes)
            memset(dptr + width * 3, 0, out_rowbytes - width * 3);
        sptr += packed_rowbytes;
    }
    free(row_buf);
    state->buffer = NULL;
    state->ptr = sptr;
}

/*
  The PackBitsRect and BitsRect data types:
  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-461.html

  PixMap data type (the stored version skips the "baseAddr field")
  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-202.html

  The PixData type:
  http://developer.apple.com/documentation/mac/QuickDraw/QuickDraw-460.html
*/
static void
pict_op_pixels(struct pict_state *state,
               struct pict_opcode const *opcode)
{
    uint8_t const *ptr = state->ptr, *end = state->end, *ptr2;
    struct image *img = NULL;
    struct palette *plt = NULL;
    uint16_t rowbytes;
    int16_t bounds[4], pm_version, pack_type;
    int16_t pix_size, cmp_count, cmp_size;
    int16_t ct_size = 0, src_rect[4], dest_rect[4], x_mode;
    uint32_t height, width, channels;
    int i, j;

    switch(opcode->opcode) {
    case 0x98:
        // packbits
        if ((unsigned int)(end - ptr) < 72) {
            pict_error(state, ERR_EOF);
            return;
        }
        ct_size = read_bs16(ptr + 52) + 1;
        if (ct_size < 0 || ct_size > 256) {
            pict_error(state, ERR_CLUTSIZE);
            return;
        }
        if ((unsigned int)(end - ptr) < (unsigned int)(72 + 8 * ct_size)) {
            pict_error(state, ERR_EOF);
            return;
        }
        break;
    case 0x9a:
        // direct
        // skip baseAddr field
        if ((unsigned int)(end - ptr) < 68) {
            pict_error(state, ERR_EOF);
            return;
        }
        ptr += 4;
        break;
    default:
        assert(0);
    }
    if ((unsigned int)(end - ptr) < 46) {
        pict_error(state, ERR_EOF);
        return;
    }

    // Read 46 bytes...
    rowbytes = read_bu16(ptr) & 0x7fff;
    for (i = 0; i < 4; ++i)
        bounds[i] = read_bs16(ptr + 2 + 2*i);
    pm_version = read_bs16(ptr + 10);
    // Check version number
    if (pm_version != 0) {
        pict_error(state, ERR_PIXMAPVERSION);
        return;
    }
    pack_type = read_bs16(ptr + 12);
    // skip 14 bytes: packSize, hRes, vRes, pixType
    pix_size = read_bs16(ptr + 28);
    cmp_count = read_bs16(ptr + 30);
    cmp_size = read_bs16(ptr + 32);
    // skip 12 bytes: planeBytes, pmTable, pmReserved 
    ptr += 46;

    // printf("packType: %i\n", packType);

    if (opcode->opcode == 0x0098) {
        // Read the palette
        // skip ctSeed, flags
        ptr += 8;
        state->plt = plt = palette_create(3, ct_size, &state->err);
        if (!plt)
            longjmp(state->jmpbuf, 1);
        for (i = 0; i < ct_size; ++i, ptr += 8)
            for (j = 0; j < 3; ++j)
                plt->data[i*3+j] = read_bu16(ptr+2+2*j);
    }

    for (i = 0; i < 4; ++i)
        src_rect[i] = read_bs16(ptr + 2*i);
    for (i = 0; i < 4; ++i)
        dest_rect[i] = read_bs16(ptr + 8 + 2*i);
    x_mode = read_bs16(ptr + 16);
    ptr += 18;

    if (memcmp(src_rect, dest_rect, sizeof(src_rect))
        || memcmp(src_rect, bounds, sizeof(src_rect))) {
        pict_error(state, ERR_PIXRECTMATCH);
        return;
    }

    if (x_mode != 0 && x_mode != 64) {
        pict_error(state, ERR_XFERMODE);
        return;
    }

    // Unpack pixel data
    if (bounds[2] <= bounds[0] || bounds[3] <= bounds[1]) {
        pict_error(state, ERR_PIXELDIM);
        return;
    }
    height = bounds[2] - bounds[0];
    width = bounds[3] - bounds[1];
    // printf("    dimensions %i x %i\n", width, height);
    
    switch (pix_size) {
    case 8:
        channels = 3;
        if (cmp_count != 1 || cmp_size != 8 ||
            (pack_type != 0 && pack_type != 1)) {
            pict_error(state, ERR_DEPTH);
            return;
        }
        if (rowbytes < width) {
            pict_error(state, ERR_ROWBYTES);
            return;
        }
        break;
    case 16:
        channels = 3;
        if (cmp_count != 3 || cmp_size != 5 ||
            (pack_type != 3 && pack_type != 1)) {
            pict_error(state, ERR_DEPTH);
            return;
        }
        if (rowbytes < width * 2) {
            pict_error(state, ERR_ROWBYTES);
            return;
        }
        break;
    case 32:
        channels = 3;
        if (cmp_count != 3 || cmp_size != 8 ||
            (pack_type != 4 && pack_type != 1)) {
            pict_error(state, ERR_DEPTH);
            return;
        }
        if (rowbytes & 0x3) {
            pict_error(state, ERR_ROWBYTES);
            return;
        }
        rowbytes = (rowbytes * 3) >> 2;
        if (rowbytes < width * 3) {
            pict_error(state, ERR_ROWBYTES);
            return;
        }
        break;
    default:
        pict_error(state, ERR_DEPTH);
        return;
    }

    state->ptr = ptr;

    img = image_create(width, height, channels,
                       PIXEL_8, false, plt, &state->err);
    if (!img)
        longjmp(state->jmpbuf, 1);
    state->img = img;
    palette_release_maybe(plt);
    state->plt = NULL;
    if (rowbytes > img->rowbytes) {
        pict_error(state, ERR_ROWBYTES);
        return;
    }
    for (i = 0; i < 4; ++i)
        img->sigbits[i] = cmp_size;

    if (rowbytes < 8)
        pack_type = 1;
    switch (pack_type) {
    case 0:
        pict_read_packed_8(state, rowbytes, img);
        break;
    case 1:
        switch (pix_size) {
        case 8:
            pict_read_unpacked_8(state, rowbytes, img);
            break;
        case 16:
            pict_read_unpacked_16(state, rowbytes, img);
            break;
        case 32:
            pict_read_unpacked_32(state, rowbytes, img);
            break;
        default:
            assert(0);
        }
        break;
    case 3:
        pict_read_packed_16(state, rowbytes, img);
        break;
    case 4:
        pict_read_packed_32(state, rowbytes, img);
        break;
    default:
        assert(0);
        break;
    }
    if (state->has_error)
        return;

    // Align the read position to an even number.
    ptr2 = state->ptr;
    if ((unsigned int)(ptr2 - ptr) & 1) {
        if (end - ptr2 < 1) {
            pict_error(state, ERR_EOF);
            return;
        }
        state->ptr = ptr2 + 1;
    }
}

static void
pict_op_nop(struct pict_state *state,
            struct pict_opcode const *opcode)
{
    uint8_t const *ptr = state->ptr, *end = state->end;
    if ((unsigned int)(end - ptr) < opcode->size) {
        pict_error(state, ERR_EOF);
        return;
    }
    state->ptr = ptr;
}

static void
pict_op_longcomment(struct pict_state *state,
                    struct pict_opcode const *opcode)
{
    uint8_t const *ptr = state->ptr, *end = state->end;
    int16_t length;
    (void)opcode;
    if ((unsigned int)(end - ptr) < 4) {
        pict_error(state, ERR_EOF);
        return;
    }
    length = read_bs16(ptr + 2);
    if (length < 0) {
        pict_error(state, ERR_COMMENTLENGTH);
        return;
    }
    state->ptr = ptr + 4 + length;
    //printf("  comment: type %i, length %i\n", type, length);
}

#define PICT_OPCODE_COUNT 8
static const struct pict_opcode PICT_OPCODES[PICT_OPCODE_COUNT] = {
    {   0x00, 0, "NOP",            pict_op_nop },
    {   0x01, 0, "Clip",           pict_op_region },
    {   0x11, 0, "VersionOp",      pict_op_version },
    {   0x1e, 0, "DefHilite",      pict_op_nop },
    {   0x98, 0, "PackBitsRect",   pict_op_pixels },
    {   0x9a, 0, "DirectBitsRect", pict_op_pixels },
    {   0xa1, 0, "LongComment",    pict_op_longcomment },
    { 0x0c00, 0, "HeaderOp",       pict_op_header }
};

/* The largest picture in the Marathon data is the high color version
   of Marathon Infinity's "Despair" chapter, at 1.45 MB. There are a
   few others around the same size.  */
struct image *
pict_read(void const *data, uint32_t length,
          struct error_handler *err)
{
    struct pict_opcode const *opinfo;
    uint32_t opcount, middle;
    uint16_t opcode, opref;
    uint8_t const *ptr = data, *end = ptr + length;
    struct pict_state state = { .errh = err };

    if (length < 11) {
        pict_error(&state, ERR_EOF);
        return NULL;
    }

    if (setjmp(state.jmpbuf)) {
        palette_release_maybe(state.plt);
        image_release_maybe(state.img);
        free(state.buffer);
        error_handle(err, &state.err);
        return NULL;
    }

    // Skip datasize (invalid for version 2) and bounds
    ptr += 10;
    
    // Figure out the picture version
    if (end - ptr > 1) {
        opcode = read_bu16(ptr);
        if (opcode == 0x11)
            state.version = 2;
        else
            state.version = 1;
    } else
        state.version = 1;
    
    state.end = end;
    // Read the picture opcodes
    while (!state.has_error) {
        // Version 2 has two-byte opcodes
        if (state.version == 2) {
            if ((unsigned int)(end - ptr) < 2) {
                pict_error(&state, ERR_EOF);
                break;
            }
            opcode = read_bu16(ptr);
            ptr += 2;
        } else {
            if ((unsigned int)(end - ptr) < 1) {
                pict_error(&state, ERR_EOF);
                break;
            }
            opcode = read_u8(ptr);
            ptr += 1;
        }
        // this is the "end" opcode
        if (opcode == 0xff)
            break;
        // Find the opcode handler
        opinfo = PICT_OPCODES;
        opcount = PICT_OPCODE_COUNT;
        while (opcount > 0) {
            middle = opcount / 2;
            opref = opinfo[middle].opcode;
            if (opcode < opref) {
                opcount = middle;
            } else if (opcode > opref) {
                opcount -= middle + 1;
                opinfo += middle + 1;
            } else {
                opinfo += middle;
                break;
            }
        }
        if (opcount) {
            state.ptr = ptr;
            opinfo->handler(&state, opinfo);
            ptr = state.ptr;
        } else
            pict_error(&state, "Unknown opcode: $%04x",
                       opcode);
    }

    if (!state.img && !state.has_error) {
        error_msg(err, ERR_NOPIX);
        return NULL;
    }
    if (state.has_error)
        return NULL;

    return state.img;
}

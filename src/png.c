/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "defs.h"

#include "unrez.h"

#include <png.h>

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct wpng {
    const char *name;
    int fdes;
};

static void error_cb(png_struct *pngp, const char *msg) {
    (void)pngp;
    dief(EX_SOFTWARE, "libpng: %s\n", msg);
}

static void warning_cb(png_struct *pngp, const char *msg) {
    (void)pngp;
    fprintf(stderr, "warning: libpng: %s\n", msg);
}

static void write_cb(png_struct *pngp, png_byte *data, png_size_t length) {
    struct wpng *w = png_get_io_ptr(pngp);
    ssize_t amt;
    png_size_t pos = 0;
    int fdes = w->fdes, err;
    while (pos < length) {
        amt = write(fdes, data + pos, length - pos);
        if (amt < 0) {
            err = errno;
            if (err == EINTR) {
                continue;
            }
            die_errf(EX_CANTCREAT, err, "%s", w->name);
        }
        pos += amt;
    }
}

static void flush_cb(png_struct *pngp) {
    (void)pngp;
}

void write_png(int dirfd, const char *name, const struct unrez_pixdata *pix) {
    struct wpng w;
    png_struct *png;
    png_info *info;
    int width, height, rowbytes, ctype, i, col_count;
    const unsigned char *data;
    png_byte **rows = NULL;
    png_color *col = NULL;
    const struct unrez_color *icol;

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, error_cb,
                                  warning_cb);
    if (png == NULL) {
        dief(EX_SOFTWARE, "cannot initialize LibPNG");
    }
    info = png_create_info_struct(png);
    if (info == NULL) {
        dief(EX_SOFTWARE, "cannot initialize LibPNG");
    }

    w.name = name;
    w.fdes = openat(dirfd, name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (w.fdes == -1) {
        die_errf(EX_CANTCREAT, errno, "%s", name);
    }
    png_set_write_fn(png, &w, write_cb, flush_cb);

    height = pix->bounds.bottom - pix->bounds.top;
    width = pix->bounds.right - pix->bounds.left;
    rowbytes = pix->rowBytes;
    switch (pix->dataPixelSize) {
    case 8:
        ctype = PNG_COLOR_TYPE_PALETTE;
        col_count = pix->ctSize;
        if (col_count == 0) {
            dief(EX_SOFTWARE, "missing pallette for 8-bit image");
        }
        col = malloc(sizeof(*col) * col_count);
        if (col == NULL) {
            die_errf(EX_SOFTWARE, errno, "malloc");
        }
        icol = pix->ctTable;
        for (i = 0; i < col_count; i++) {
            col[i].red = icol[i].r >> 8;
            col[i].green = icol[i].g >> 8;
            col[i].blue = icol[i].b >> 8;
        }
        png_set_PLTE(png, info, col, col_count);
        break;
    case 32:
        ctype = PNG_COLOR_TYPE_RGB;
        break;
    default:
        dief(EX_SOFTWARE, "unknown pixel size: %d", pix->dataPixelSize);
    }
    png_set_IHDR(png, info, width, height, 8, ctype, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    switch (pix->dataPixelSize) {
    case 32:
        png_set_filler(png, 0, PNG_FILLER_AFTER);
        break;
    }

    data = pix->data;
    rows = malloc(sizeof(*rows) * height);
    if (rows == NULL) {
        die_errf(EX_OSERR, errno, "malloc");
    }
    for (i = 0; i < height; i++) {
        rows[i] = (png_byte *)(data + i * rowbytes);
    }
    png_write_image(png, rows);
    png_write_end(png, NULL);
    free(rows);
    free(col);
    png_destroy_write_struct(&png, &info);
}

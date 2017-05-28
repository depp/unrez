/*
 * Copyright 2007-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include <errno.h>
#include <stdlib.h>

void unrez_pixdata_destroy(struct unrez_pixdata *pix) {
    free(pix->data);
    free(pix->ctTable);
}

int unrez_pixdata_16to32(struct unrez_pixdata *pix) {
    uint16_t *src;
    uint8_t *dest;
    int i, width, height, pixcount;
    unsigned v;
    width = pix->rowBytes >> 1;
    height = pix->bounds.bottom - pix->bounds.top;
    if (pix->pixelSize != 16 || (pix->rowBytes & 1) != 0 || width <= 0 ||
        height <= 0) {
        return EINVAL;
    }
    pixcount = width * height;
    src = pix->data;
    dest = malloc(pixcount * 4);
    if (dest == NULL) {
        return errno;
    }
    for (i = 0; i < pixcount; i++) {
        v = src[i];
        dest[i * 4 + 0] = ((v >> 7) & 0xf8) | ((v >> 12) & 7);
        dest[i * 4 + 1] = ((v >> 2) & 0xf8) | ((v >> 7) & 7);
        dest[i * 4 + 2] = ((v << 3) & 0xf8) | ((v >> 2) & 7);
        dest[i * 4 + 3] = 0;
    }
    free(src);
    pix->data = dest;
    pix->rowBytes = width * 4;
    pix->pixelSize = 32;
    pix->cmpSize = 8;
    return 0;
}

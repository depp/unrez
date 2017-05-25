#ifndef SCPIMAGE_IMAGE_H
#define SCPIMAGE_IMAGE_H
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "scpbase/atomic.h"
struct error;

/* Pixels types 1, 2, and 4 are too closely packed for easy
   manipulation and are intended only for image IO.  */
typedef enum {
    PIXEL_1,
    PIXEL_2,
    PIXEL_4,
    PIXEL_8,
    PIXEL_16
} pixel_type_t;

struct palette {
    atomic_t refcount;
    uint32_t channels;
    uint32_t size;
    uint16_t data[];
};

/* There are three types of images:
   - Indexed: one plane, palette
   - Packed: one plane, no palette
   - Planar: one plane per channel, no palette
   There is no difference between the data for packed and planar
   formats if the image has only one channel, or if the image
   indexed.  */
struct image {
    atomic_t refcount;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    pixel_type_t type;
    bool planar;
    uint32_t planecount;
    uint32_t rowbytes;
    struct palette *palette;
    void *plane[4];
    uint32_t sigbits[4];
};

struct palette *
palette_create(uint32_t channels, uint32_t size, struct error **err);

static inline void
palette_retain(struct palette *plt)
{
    atomic_inc(&plt->refcount);
}

static inline void
palette_release(struct palette *plt)
{
    if (atomic_dec_and_test(&plt->refcount))
        free(plt);
}

static inline void
palette_release_maybe(struct palette *plt)
{
    if (plt)
        palette_release(plt);
}

struct image *
image_create(uint32_t width, uint32_t height, uint32_t channels,
             pixel_type_t type, bool planar, struct palette *palette,
             struct error **err);

void
image_free_(struct image* img);

static inline void
image_retain(struct image *img)
{
    atomic_inc(&img->refcount);
}

static inline void
image_release(struct image *img)
{
    if (atomic_dec_and_test(&img->refcount))
        image_free_(img);
}

static inline void
image_release_maybe(struct image *img)
{
    if (img)
        image_release(img);
}

enum {
    IMAGE_PLANAR = 1 << 0,
    IMAGE_CHUNKED = 1 << 1,
    IMAGE_PIXEL_TYPE = 1 << 2
};

/* Here, ops is a set of or'd flags above.  */
struct image *
image_convert(struct image *img, unsigned int ops,
              pixel_type_t type, struct error **err);

#endif

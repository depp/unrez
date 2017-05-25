#ifndef SCPIMAGE_PICT_H
#define SCPIMAGE_PICT_H
#include <stdint.h>
struct image;
struct error_handler;

/* Read an image in Macintosh PICT format.  This function is fairly
   limited, it will only extract the bitmap data from an image and
   will not even do that in all cases.  It has only been tested on the
   PICT resources that shipped with the Marathon games.  If you use it
   on PICT files, you will have to skip the first 512 bytes.  */
struct image *
pict_read(void const *data, uint32_t length,
          struct error_handler *err);

#endif

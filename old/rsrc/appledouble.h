#ifndef SCPRSRC_APPLEDOUBLE_H
#define SCPRSRC_APPLEDOUBLE_H
#include <stdint.h>
struct ifile;
struct error;

/* Returns 0 if the file is not an AppleDouble file, 1 if the file is
   an AppleDoubleFile, and <0 if the file is corrupted or if there was
   an error.  If the file is an AppleDouble file, then the offsets and
   lengths of the data and resource forks will be filled in.  */
int
appledouble_parse(struct ifile *file, struct error **err,
                  uint32_t *doffset, uint32_t *dlength,
                  uint32_t *roffset, uint32_t *rlength);

#endif

#ifndef SCPRSRC_MACBINARY_H
#define SCPRSRC_MACBINARY_H
#include <stdint.h>
struct ifile;
struct error;

/* Returns 0 if the file is not a MacBinary file, 1 if the file is a
   MacBinary file, and <0 if there was an error or the file is
   corrupted.  If the file is a valid MacBinary file, then the offsets
   and lengths of the data and resource forks will be filled in.  */
int
macbinary_parse(struct ifile *file, struct error **err,
                uint32_t *doffset, uint32_t *dlength,
                uint32_t *roffset, uint32_t *rlength);

#endif

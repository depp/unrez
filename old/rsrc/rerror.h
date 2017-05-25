#ifndef SCPRSRC_RERROR_H
#define SCPRSRC_RERROR_H

/* Indicates a problem with on-disk resource fork storage, i.e.,
   corrupted MacBinary or AppleDouble data.  Does not include errors
   for native resource fork storage, or for invalid resource fork
   data.  */
extern const struct error_domain ERROR_RSRC;

#endif

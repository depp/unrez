#ifndef SCPRSRC_RESOURCEFORK_H
#define SCPRSRC_RESOURCEFORK_H
/* A Macintosh resource fork can contain an arbitrary stream of bytes.
   However, this is exceptionally rare.  The resource fork of a file
   almost always contains a collection of different resources, each
   identified by a type code and a 16 bit number called the ID.  The
   type code is a four character string which is often treated as a
   32-bit number.

   A typical old Macintosh application will look for a resource just
   using the type code and ID, for example, "PICT" ID 128.  This
   library uses a slightly more complicated method.  Types are
   accessed by index in the order they appear in the file, and
   resources within are accessed by index in the order they appear for
   that type.  So to access "PICT" ID 128, first look up the index for
   the "PICT" type, which might not be present.  Then look up the
   index for "PICT" ID 128, which might also not exist.  The reason
   for this difference is because one expects users of this library to
   want to enumerate all resources rather than look for a specific
   resource.

   These functions load the entire resource fork into memory before
   doing anything.  This isn't actually too bad, the maximum size of a
   resource fork is about 24 MB.  */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct ifile;
struct error_handler;

/* An open resource fork.  */
struct resourcefork {
    unsigned char *data;
    size_t datalen;
    int32_t moff; /* map offset */
    int32_t mlen; /* map size */
    int32_t doff; /* data section offset */
    int32_t dlen; /* data section length */
    struct resourcefork_type *types;
    int32_t tcount;
    uint16_t attr; /* fork attributes */
    int16_t toff; /* type info offset */
    int16_t noff; /* name offset */
};

/* A type in a resource fork.  If the resources field is NULL but the
   resource count is nonzero, then the resources for this type haven't
   been loaded yet.  */
struct resourcefork_type {
    uint8_t code[4];
    struct resourcefork_rsrc *resources;
    int32_t rcount;
    int16_t ref_offset;
};

/* A resource in a resource fork.  The length will be -1 at first
   because the resource's length is stored in a separate location from
   the rest of the information about the resource.  */
struct resourcefork_rsrc {
    int16_t id;
    int16_t name_offset;
    uint8_t attr;
    int32_t offset;
    int32_t length;
};

/* Open a resource fork.  Reads the entire contents into memory, so
   you may close the file after calling this function.  Returns 0 on
   success, nonzero on failure.  */
int
resourcefork_open(struct resourcefork *rfork,
                  struct ifile *file, struct error_handler *err);

/* Close a resource fork and release memory.  */
void
resourcefork_close(struct resourcefork *rfork);

/* Find a specific type in a resource fork, returns -1 if the type is
   not found.  */
int
resourcefork_find_type(struct resourcefork *rfork,
                       uint8_t const *code);

/* Load the resource map for a specific type.  Returns 0 if
   successful, nonzero otherwise.  */
int
resourcefork_load_type(struct resourcefork *rfork,
                       int type_index, struct error_handler *err);

/* Find a resource by its type and ID in a resource fork.  Returns -1
   if the resource is not found.  The type must be loaded first.  */
int
resourcefork_find_rsrc(struct resourcefork *rfork,
                       int type_index, int rsrc_id);

/* Get the location of a resource in a resource fork.  Returns 0 if
   successful, <0 otherwise.  */
int
resourcefork_rsrc_loc(struct resourcefork *rfork,
                      int type_index, int rsrc_index,
                      int32_t *offset, int32_t *length,
                      struct error_handler *err);

/* Get a resource's data.  Returns 0 if successful, nonzero
   otherwise.  */
int
resourcefork_rsrc_read(struct resourcefork *rfork,
                       int type_index, int rsrc_index,
                       void **data, uint32_t *length,
                       struct error_handler *err);

#endif

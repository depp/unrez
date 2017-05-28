/*
 * Copyright 2007-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#ifndef UNREZ_H
#define UNREZ_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * UnRez error codes. Error codes specific to UnRez are always
 * negative. Positive error codes correspond to errno values.
 */
enum {
    /* The file does not have the specified format. */
    kUnrezErrFormat = -1,
    /* The file format is invalid. */
    kUnrezErrInvalid = -2,
    /* The file format contains unsupported features: version too new, etc. */
    kUnrezErrUnsupported = -3,
    /* The resource was not found. */
    kUnrezErrResourceNotFound = -4,
    /* The file does not have a resource fork. */
    kUnrezErrNoResourceFork = -5,
    /* The fork is too large to read into memory. */
    kUnrezErrTooLarge = -6,
    /* The resource fork is too large (which means it's invalid). */
    kUnrezErrResourceForkTooLarge = -7
};

/*
 * unrez_strerror gets a string describing an error code.
 */
int unrez_strerror(int code, char *buf, size_t buflen);

/*
 * An unrez_data is a block of data in memory which it may or may not own. If
 * the structure is zeroed and then data and size are set, then it does not own
 * the data.
 */
struct unrez_data {
    /* The data. */
    const void *data;
    size_t size;
    /* Private fields for freeing the data. */
    int type;
    void *mem;
    size_t memsz;
};

/*
 * unrez_data_destroy frees data owned by the structure.
 */
void unrez_data_destroy(struct unrez_data *d);

/*
  Files from old Macintosh systems can have both a data fork and a resource
  fork. At a filesystem level, both are streams of bytes. The data fork contains
  the normal file contents, and the resource fork uses a special format to hold
  a collection of resources.

  If you want a Mac file to remain intact after being transferred to another
  system (other than another Mac), you have to preserve the resource fork and a
  small amount of additional metadata. There are a few common ways to do this:

  MacBinary encodes the forks and metadata as one stream. Both forks are
  included verbatim, so they can be read almost transparently by a library.
  However, tools that handle MacBinary encoded files will not work unless they
  are aware of the MacBinary format. MacBinary was a very popular format, but it
  was only used for transferring files that would ultimately be used on a Mac,
  such as if you wanted to share files on a website.

  AppleSingle, like MacBinary, encodes both forks of a file and the metadata
  into a single stream, and includes both streams verbatim. Unlike MacBinary, it
  is extensible. However, it was not a very popular format. It is not supported
  by this library.

  AppleDouble encodes a file into two files. The main file contains only the
  data fork. A separate, hidden file contians the resource fork and metadata.
  Its main purpose is to transparently work with files on both Mac systems and
  other systems. This format is used automatically when saving files to tar
  files, network shares, zip files, and flash drives. The AppleDouble file has
  the name of the original file prefixed with "._", so saving "document.txt"
  would result in an additional "._document.txt" file. Then, "document.txt" can
  be edited on a non-Mac system without affecting the resource fork or metadata.

  BinHex is an encoding designed to preserve Mac files over channels that are
  not 8-bit clean. It encodes both forks and metadata into ASCII and includes
  checksums. Neither fork can be read without decoding the file. Like MacBinary,
  it was very popular for transferring files over the internet.

  Resource forks may also be supported by the underlying network share or
  filesystem. Without relying on Apple's "Carbon" interface, you can access
  resource forks on OS X using specially constructed paths and normal unix
  system calls. The same can be done on Linux systems with support for HFS,
  although the paths may be different. NTFS supports "alternate data streams"
  which are ignored by most programs, but in some cases were used by Windows to
  store resource forks created by Macintosh clients using Windows file shares.
*/

/*
 * An unrez_fork represents an open fork of a file.
 */
struct unrez_fork {
    /*
     * The file descriptor for the file containing this fork, or -1 if this fork
     * is not present. Different forks may share the same file descriptor.
     */
    int file;
    /* The offset of the fork within the file. */
    int64_t offset;
    /* The size of the fork. */
    int64_t size;
};

/*
 * unrez_fork_read reads an entire fork of a file into memory.
 */
int unrez_fork_read(const struct unrez_fork *fork, struct unrez_data *d);

/*
 * An unrez_type_t represents the possible ways a resource fork can be accessed
 * from disk.
 */
typedef enum {
    /* No resource fork is present. */
    kUnrezTypeNone,
    /* The file is a MacBinary encoded file. */
    kUnrezTypeMacBinary,
    /* The file is an AppleDouble encoded file. */
    kUnrezTypeAppleDouble,
    /* The file is an AppleSingle encoded file. */
    kUnrezTypeAppleSingle,
    /* The file has forks at the native filesystem level. */
    kUnrezTypeNative,
} unrez_type_t;

/*
 * An unrez_metadata contains parsed metadata for a file.
 */
struct unrez_metadata {
    /*
     * The format which contained the metadata.
     */
    unrez_type_t type;
    /*
     * The original filename, the character encoding for the filename, and the
     * length of the filename. We add an '\0' byte at the end for convenience
     * with C APIs, but there is no guarantee that there are no other '\0' bytes
     * in the filename.
     */
    size_t filename_length;
    int filename_script;
    char *filename;
    /*
     * The finder comment and its length. We add an '\0' byte at the end for
     * convenience with C APIs, but there is no guarantee that there are no
     * other '\0' bytes in the filename.
     */
    size_t comment_length;
    char *comment;
    /*
     * Other file metadata.
     */
    char type_code[4];
    char creator_code[4];
    int finder_flags;
    int vpos;
    int hpos;
    int windowid;
    int protected;
    uint32_t mod_time;
    /*
     * The size and offset of the forks, if the parsed file encodes forks.
     */
    int64_t data_offset;
    int64_t data_size;
    int64_t rsrc_offset;
    int64_t rsrc_size;
};

/*
 * unrez_macbinary_parse parses a MacBinary file, and fills in the metadata
 * structure. Returns 0 on success, or an error code on failure.  The fsize
 * contains the size of the file, or -1 if it is unknown.
 */
int unrez_macbinary_parse(struct unrez_metadata *mdata, int fdes,
                          int64_t fsize);

/*
 * unrez_applefile_parse parses an AppleDouble or AppleSingle file, and fills in
 * the metadata structure. Returns 0 on success, or an error code on failure.
 * The fsize contains the size of the file, or -1 if it is unknown.
 */
int unrez_applefile_parse(struct unrez_metadata *mdata, int fdes,
                          int64_t fsize);

/*
 * An unrez_forkedfile is a file which may have a data fork, resource fork, or
 * both. This does not distinguish between an empty fork and a missing fork,
 * since not all encodings preserve the distinction.
 */
struct unrez_forkedfile {
    /* The file's data fork. */
    struct unrez_fork data;
    /* The file's resource fork. */
    struct unrez_fork rsrc;
    /* Additional metadata for the file. */
    struct unrez_metadata metadata;
};

/*
 * unrez_forkedfile_open opens both forks of a file, if present. The encoding
 * for the data fork and resource fork are determined automatically using
 * heruistics: MacBinary is tried if the filename ends with ".bin", AppleDouble
 * is tried if the filename starts with "._", and finally the native filesystem
 * is used. This order attempts to preserve the user's intent, since MacBinary
 * is the most intentional way to attach a resource fork to a file. Although it
 * should be rare that the same file would have an actual resource fork attached
 * to it in multiple ways, it is easy to imagine a MacBinary file getting an
 * AppleDouble file paired with it if the MacBinary file is copied from a Mac to
 * another system, since AppleDouble is also used to preserve metedata.
 *
 * Returns 0 on success, or a nonzero error code on failure.
 */
int unrez_forkedfile_open(struct unrez_forkedfile *forks, const char *path);

/*
 * unrez_forkedfile_openat is the same as unrez_forkedfile_open, except the path
 * is relative to the directory represented by the file descriptor dirfd, or the
 * working directory if dirfd is AT_FDCWD.
 *
 * Returns 0 on success, or a nonzero error code on failure.
 */
int unrez_forkedfile_openat(struct unrez_forkedfile *forks, int dirfd,
                            const char *path);

/*
 * unrez_forkedfile_close closes a forked file.
 */
void unrez_forkedfile_close(struct unrez_forkedfile *forks);

/*
  A Macintosh resource fork can contain an arbitrary stream of bytes.  However,
  this is exceptionally rare. The resource fork of a file almost always contains
  a collection of different resources, each identified by a type code and a 16
  bit number called the ID. The type code is a four character string which is
  often treated as a 32-bit number.

  A typical old Macintosh application will look for a resource just using the
  type code and ID, for example, "PICT" ID 128. This library uses a slightly
  more complicated method. Types are accessed by index in the order they appear
  in the file, and resources within are accessed by index in the order they
  appear for that type. So to access "PICT" ID 128, first look up the index for
  the "PICT" type, which might not be present. Then look up the index for "PICT"
  ID 128, which might also not exist. The reason for this difference is because
  one expects users of this library to want to enumerate all resources rather
  than look for a specific resource.

  These functions load the entire resource fork into memory before doing
  anything. This isn't actually too bad, the maximum size of a resource fork is
  about 16 MB.
*/

/*
 * An unrez_resourcefork is an open resource fork.
 */
struct unrez_resourcefork {
    /* Resource map section. */
    const unsigned char *map;
    int32_t map_size;
    /* Data section. */
    const unsigned char *data;
    int32_t data_size;
    /* fork attributes */
    uint32_t attr;
    /* type info offset */
    int32_t toff;
    /* name offset */
    int32_t noff;
    /* List of resource types. */
    struct unrez_resourcetype *types;
    int32_t type_count;
    /* Owner of the fork's data. */
    struct unrez_data owner;
};

/*
 * An unrez_resourcetype is a type in an open resource fork. If the resources
 * field is NULL but the resource count is nonzero, then the resources for this
 * type haven't been loaded yet.
 */
struct unrez_resourcetype {
    unsigned char type_code[4];
    struct unrez_resource *resources;
    int32_t count;
    int16_t ref_offset;
};

/*
 * An unrez_resource is a resource in a resource fork. The size will be -1 at
 * first because the resource's size is stored in a separate location from the
 * rest of the information about the resource. Once the resource is loaded, its
 * size will be filled in.
 */
struct unrez_resource {
    int16_t id;
    int16_t name_offset;
    uint8_t attr;
    int32_t offset;
    int32_t size;
};

/*
 * unrez_resourcefork_openmem opens a resource fork from a data buffer in
 * memory. This will not modify the memory or take ownership of it, but it must
 * not be freed while the resource fork is being used. Returns 0 on success, or
 * an error code on failure.
 */
int unrez_resourcefork_openmem(struct unrez_resourcefork *rfork,
                               const void *data, size_t size);

/*
 * unrez_resourcefork_openfork opens a resource fork from a open forked file.
 * This will not modify the forked file or take ownership of it, and the file
 * may be safely closed while the resource fork is still being used. Returns 0
 * on success, or an error code on failure.
 */
int unrez_resourcefork_openfork(struct unrez_resourcefork *rfork,
                                const struct unrez_fork *fork);

/*
 * unrez_resourcefork_open opens a resource fork from the file at the given
 * path. Returns 0 on success, or an error code on failure.
 */
int unrez_resourcefork_open(struct unrez_resourcefork *rfork, const char *path);

/*
 * unrez_resourcefork_open opens a resource fork from the file at the given
 * path. The path is relative to the directory represented by the file
 * descriptor dirfd, or the current directory if dirfd is AT_FDCWD. Returns 0 on
 * success, or an error code on failure.
 */
int unrez_resourcefork_openat(struct unrez_resourcefork *rfork, int dirfd,
                              const char *path);

/*
 * unrez_resourcefork_close closes a resource fork and frees the memory that it
 * owns.
 */
void unrez_resourcefork_close(struct unrez_resourcefork *rfork);

/*
 * unrez_resourcefork_findrsrc finds a resource with the given type code and ID,
 * and gets the data. The returned pointer points into the resource fork's
 * memory. Returns 0 on success, or an error code on failure. This will load the
 * resource type if it is not loaded.
 *
 * This is a wrapper around findtype, loadtype, findid, and getrsrc below.
 */
int unrez_resourcefork_findrsrc(struct unrez_resourcefork *rfork,
                                const unsigned char *type_code, int rsrc_id,
                                const void **data, uint32_t *size);

/*
 * resourcefork_findtype finds a specific type in a resource fork. Returns the
 * index of the type, or -1 if the type is not found. The type must be a
 * four-character code.q
 */
int unrez_resourcefork_findtype(struct unrez_resourcefork *rfork,
                                const unsigned char *type_code);

/*
 * unrez_resourcefork_loadtype loads the resource map for a specific
 * type. Returns 0 if successful, or an error code on failure.
 */
int unrez_resourcefork_loadtype(struct unrez_resourcefork *rfork,
                                int type_index);

/*
 * unrez_resourcefork_findrsrc finds a resource by its type and ID in a resource
 * fork. Returns the resource index, or -1 if the resource is not found. The
 * resource type must be loaded.
 */
int unrez_resourcefork_findid(struct unrez_resourcefork *rfork, int type_index,
                              int rsrc_id);

/*
 * unrez_resourcefork_get_rsrc gets the data for a resource. The returned
 * pointer points into the resource fork's memory. Returns 0 on success, or an
 * error code on failure. This will load the resource type if it is not loaded.
 */
int unrez_resourcefork_getrsrc(struct unrez_resourcefork *rfork, int type_index,
                               int rsrc_index, const void **data,
                               uint32_t *size);

/*
 * unrez_pict_opname gets the name of a picture opcode, or returns NULL if the
 * opcode is reserved, unknown, or out of range.
 */
const char *unrez_pict_opname(int opcode);

/*
 * An unrez_rect is a rectangle in a picture. Coordinates start from the top
 * left.
 */
struct unrez_rect {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
};

/*
 * An unrez_color is a 16-bit RGB color.
 */
struct unrez_color {
    /* According to QuickDraw, "index or other value". Safe to ignore. */
    int16_t v;
    /* Color value. */
    uint16_t r, g, b;
};

/*
 * An unrez_pixdata contains packed pixel data from a picture, as well as the
 * associated color table and blit operation.
 */
struct unrez_pixdata {
    /*
     * Unpacked pixel data, and the size of each unpacked pixel in bits.
     */
    void *data;
    /*
     * QuickDraw PixMap structure. The field names are the same.
     */
    int rowBytes;
    struct unrez_rect bounds;
    int packType;
    int packSize;
    int hRes;
    int vRes;
    int pixelType;
    int pixelSize;
    int cmpCount;
    int cmpSize;
    /*
     * Color palette.
     */
    int ctSize;
    struct unrez_color *ctTable;
    /*
     * Blit operation.
     */
    struct unrez_rect srcRect;
    struct unrez_rect destRect;
    int mode;
};

/*
 * unrez_pixdata_destroy frees memory associated with pixel data.
 */
void unrez_pixdata_destroy(struct unrez_pixdata *pix);

/*
 * unrez_pixdata_16to32 converts 16-bit pixel data to 32-bit pixel data. The
 * packed 5-bit pixel components are expanded to 8 bits, replicating the high
 * bits for the low bits. Returns 0 on success, or a non-zero error code on
 * failure.
 */
int unrez_pixdata_16to32(struct unrez_pixdata *pix);

/*
 * An unrez_pict_callbacks contains callbacks for processing a QuickDraw
 * picture. All callbacks must be set. Callbacks that return an integer should
 * return 0 to continue processing the picture, or nonzero to stop.
 */
struct unrez_pict_callbacks {
    /* Context parameter to pass to callbacks. */
    void *ctx;
    /* Handle the picture header. */
    int (*header)(void *ctx, int version, const struct unrez_rect *frame);
    /* Handle a picture opcode. */
    int (*opcode)(void *ctx, int opcode, const void *data, size_t size);
    /*
     * Handle pixel data in a picture. The pixel data will be destroyed after
     * the callback returns by calling unrez_pixdata_destroy. If you want to
     * keep the pixel data after the function returns, you set the pixel data
     * pointers to NULL so they won't be freed.
     */
    int (*pixels)(void *ctx, int opcode, struct unrez_pixdata *pix);
    /*
     * Handle an error in the picture data. If the error happens outside an
     * opcode, then opcode will be -1. The error message may be NULL, but the
     * error code will always be nonzero.
     */
    void (*error)(void *ctx, int err, int opcode, const char *msg);
};

enum {
    /*
     * Size of the header of a QuickDraw picture, for pictures stored in the
     * data fork. The header should be skipped. This header is not found in
     * pictures stored in the resource fork.
     */
    kUnrezPictHeaderSize = 512
};

/*
 * unrez_pict_decode decodes a QuickDraw picture, passing a stream of opcodes to
 * the supplied callbacks. This is a low-level interface. This function will
 * signal errors through callbacks rather than a return code.
 */
void unrez_pict_decode(const struct unrez_pict_callbacks *cb, const void *data,
                       size_t size);

#ifdef __cplusplus
}
#endif
#endif

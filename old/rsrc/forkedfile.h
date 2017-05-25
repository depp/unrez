#ifndef SCPRSRC_FORKEDFILE_H
#define SCPRSRC_FORKEDFILE_H
/* Files from old Macintosh systems can have both a data fork and a
   resource fork.  At a filesystem level, both are streams of bytes.
   The data fork contains the normal file contents, and the resource
   fork uses a special format to hold a collection of resources.

   If you want a Mac file to remain intact after being transferred to
   another system (other than another Mac), you have to preserve the
   resource fork and a small amount of additional metadata.  There are
   a few common ways to do this:

   MacBinary encodes the forks and metadata as one stream.  Both forks
   are included verbatim, so they can be read almost transparently by
   a library.  However, tools that handle MacBinary encoded files will
   not work unless they are aware of the MacBinary format.  MacBinary
   was a very popular format, but it was only used for transferring
   files that would ultimately be used on a Mac, such as if you wanted
   to share files on a website.

   AppleSingle, like MacBinary, encodes both forks of a file and the
   metadata into a single stream, and includes both streams verbatim.
   Unlike MacBinary, it is extensible.  However, it was not a very
   popular format.  It is not supported by this library.

   AppleDouble encodes a file into two files.  The main file contains
   only the data fork.  A separate, hidden file contians the resource
   fork and metadata.  Its main purpose is to transparently work with
   files on both Mac systems and other systems.  This format is used
   automatically when saving files to tar files, network shares, zip
   files, and flash drives.  The AppleDouble file has the name of the
   original file prefixed with "._", so saving "document.txt" would
   result in an additional "._document.txt" file.  Then,
   "document.txt" can be edited on a non-Mac system without affecting
   the resource fork or metadata.

   BinHex is an encoding designed to preserve Mac files over channels
   that are not 8-bit clean.  It encodes both forks and metadata into
   ASCII and includes checksums.  Neither fork can be read without
   decoding the file.  Like MacBinary, it was very popular for
   transferring files over the internet.

   Resource forks may also be supported by the underlying network
   share or filesystem.  Without relying on Apple's "Carbon"
   interface, you can access resource forks on OS X using specially
   constructed paths and normal unix system calls.  The same can be
   done on Linux systems with support for HFS, although the paths may
   be different.  */
#include <stdint.h>
#include <stdbool.h>
struct ifile;
struct error;

/* Information about a fork of a file.  The "path" field may be NULL,
   which indicates that the fork does not exist.  The "length" may
   also be zero.  If the length is UINT32_MAX, then the whole file
   named is the fork.

   A zero length fork should be treated as equivalent to a missing
   fork.  Some formats cannot distinguish between the two.  */
struct fork {
    char *path;
    uint32_t offset;
    uint32_t length;
};

/* Records the underlying storage mechanism for the given forks.  */
typedef enum {
    FORK_NONE,
    FORK_MACBINARY,
    FORK_APPLEDOUBLE,
    FORK_NATIVE
} fork_format_t;

/* Neither fork is guaranteed to exist, and either or both may have
   zero length.  */
struct forkinfo {
    fork_format_t format;
    struct fork data;
    struct fork rsrc;
};

/* This will try to decode the file first as a MacBinary file, then
   check for an AppleDouble file in the same directory, and then ask
   the OS for the file's resource fork.  Returns 0 if forks were
   parsed successfully.  Otherwise, forkinfo remains uninitialized.

   This order was chosen in order to preserve the user's intent.  A
   MacBinary file indicates that the user was aware that the resource
   fork needed to be preserved and took steps to ensure it.  If a file
   has both a native resource fork and an AppleDouble file, the
   AppleDouble file almost certainly contains older data, probably the
   file's original resource fork.  This last condition should be
   extremely rare.  */
int
fork_parse(struct forkinfo *forkinfo, char const *path,
           struct error **err);

/* Free a forkinfo structure.  */
void
fork_free(struct forkinfo *forkinfo);

/* Returns whether a fork exists and has nonzero length.  */
static inline bool
fork_exists(struct fork *f)
{
    return f->path && f->length;
}

/* Open a file which reads from the given fork.  */
struct ifile *
fork_open(struct fork *f, struct error **err);

#endif

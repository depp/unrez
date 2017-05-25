#include "scprsrc/forkedfile.h"
#include "scprsrc/appledouble.h"
#include "scprsrc/macbinary.h"
#include "scpbase/ifile.h"
#include "scpbase/error.h"
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* These used to be checked only on certain platforms, but I prefer
   just checking for these files on inappropriate platforms rather
   than checking the platform in the preprocessor.  */
static char const FORK_PATH_1[] = "/..namedfork/rsrc";
static char const FORK_PATH_2[] = "/rsrc";
static char const APPLE_DOUBLE_PREFIX[] = "._";

int
fork_parse(struct forkinfo *forkinfo, char const *path,
           struct error **err)
{
    struct ifile *ifile = NULL;
    int result;
    char *dpath = NULL, *rpath = NULL;
    char const *cp;
    size_t plen, rplen, dlen;
    struct error *e = NULL;

    // Make a copy of the pathname
    plen = strlen(path);
    if (plen >= PATH_MAX) {
        error_system(err, ENAMETOOLONG);
        return -1;
    }
    dpath = malloc(plen + 1);
    if (!dpath) {
        error_memory(err);
        return -1;
    }
    memcpy(dpath, path, plen + 1);

    // First see if it is a MacBinary file
    ifile = ifile_open(path, err);
    if (!ifile) {
        free(dpath);
        return -1;
    }
    result = macbinary_parse(
        ifile, err,
        &forkinfo->data.offset, &forkinfo->data.length,
        &forkinfo->rsrc.offset, &forkinfo->rsrc.length);
    ifile_close(ifile);
    ifile = NULL;
    if (result < 0) {
        free(dpath);
        return -1;
    }
    if (result) {
        forkinfo->format = FORK_MACBINARY;
        forkinfo->data.path = dpath;
        forkinfo->rsrc.path = dpath;
        return 0;
    }

    /* We now know that the data fork is just the entire file, so
       we are only looking for the resource fork. */
    forkinfo->data.path = dpath;
    forkinfo->data.offset = 0;
    forkinfo->data.length = UINT32_MAX;

    // Get the path to the AppleDouble file
    rplen = plen + sizeof(FORK_PATH_1) - 1;
    rpath = malloc(rplen + 1);
    if (!rpath) {
        error_memory(err);
        free(dpath);
        return -1;
    }
    for (cp = path + plen; cp != path; --cp)
        if (cp[-1] == '/')
            break;
    dlen = cp - path;
    memcpy(rpath, path, dlen);
    memcpy(rpath + dlen, APPLE_DOUBLE_PREFIX,
           sizeof(APPLE_DOUBLE_PREFIX) - 1);
    memcpy(rpath + dlen + sizeof(APPLE_DOUBLE_PREFIX) - 1,
           path + dlen, plen - dlen + 1);
    
    // Check the AppleDouble file
    ifile = ifile_open(rpath, &e);
    if (ifile) {
        result = appledouble_parse(
            ifile, err, NULL, NULL,
            &forkinfo->rsrc.offset,
            &forkinfo->rsrc.length);
        ifile_close(ifile);
        ifile = NULL;
        if (result < 0) {
            free(dpath);
            free(rpath);
            return -1;
        }
        if (result) {
            forkinfo->format = FORK_APPLEDOUBLE;
            forkinfo->rsrc.path = rpath;
            return 0;
        }
    } else if (e->domain == &ERROR_SYSTEM && e->code == ENOENT) {
        error_free(e);
        e = NULL;
    } else {
        error_move(err, &e);
        free(dpath);
        free(rpath);
        return -1;
    }

    // Check the file's native (fs-supported) resource fork
    memcpy(rpath + dlen, path + dlen, plen - dlen);
    memcpy(rpath + plen, FORK_PATH_1, sizeof(FORK_PATH_1));
    result = access(rpath, F_OK);
    if (result && (errno == ENOENT || errno == ENOTDIR)) {
        memcpy(rpath + plen, FORK_PATH_2, sizeof(FORK_PATH_2));
        result = access(rpath, F_OK);
    }
    if (!result) {
        forkinfo->format = FORK_NATIVE;
        forkinfo->rsrc.path = rpath;
        forkinfo->rsrc.offset = 0;
        forkinfo->rsrc.length = UINT32_MAX;
        return 0;
    } else if (errno == ENOENT || errno == ENOTDIR) {
        // There's no resource fork for this file
        forkinfo->format = FORK_NONE;
        forkinfo->rsrc.path = NULL;
        forkinfo->rsrc.offset = 0;
        forkinfo->rsrc.length = 0;
        free(rpath);
        return 0;
    } else {
        error_system(err, errno);
        free(dpath);
        free(rpath);
        return -1;
    }
}

void
fork_free(struct forkinfo *forkinfo)
{
    char *a = forkinfo->data.path, *b = forkinfo->rsrc.path;
    if (a)
        free(a);
    if (b && b != a)
        free(b);
}

struct ifile *
fork_open(struct fork *f, struct error **err)
{
    if (!f->path) {
        error_system(err, ENOENT);
        return NULL;
    }
    if (f->length == UINT32_MAX)
        return ifile_open(f->path, err);
    return ifile_open_span(f->path, f->offset, f->length, err);
}

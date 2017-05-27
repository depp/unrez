/*
 * Copyright 2008-2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * These could be checked only on certain platforms, but it's simpler to check
 * for these files on inappropriate platforms rather than checking the platform
 * in the preprocessor.
 */
static const char kForkPath1[] = "/..namedfork/rsrc";
static const char kForkPath2[] = "/rsrc";
static const char *const kForkPaths[] = {kForkPath1, kForkPath2};

static const char kAppleDoublePrefix[] = "._";

int unrez_forkedfile_open(struct unrez_forkedfile *forks, const char *path) {
    return unrez_forkedfile_openat(forks, AT_FDCWD, path);
}

int unrez_forkedfile_openat(struct unrez_forkedfile *forks, int dirfd,
                            const char *path) {
    const char *sep;
    char *tmp = NULL;
    size_t len;
    int fd1 = -1, fd2 = -1, i, close_dir = 0, err, r;
    struct stat st1, st2;
    struct unrez_metadata mdata;

    /*
     * Rather than do any significant path manipulation, we open the directory
     * containing the file and use openat() everywhere.
     */
    sep = strrchr(path, '/');
    if (sep != NULL) {
        len = sep + 1 - path;
        tmp = malloc(len + 1);
        if (tmp == NULL) {
            err = errno;
            goto error;
        }
        memcpy(tmp, path, len);
        tmp[len] = '\0';
        dirfd = openat(dirfd, tmp, O_RDONLY | O_CLOEXEC);
        if (dirfd == -1) {
            err = errno;
            goto error;
        }
        close_dir = 1;
        r = fstat(dirfd, &st1);
        if (r == -1) {
            err = errno;
            goto error;
        }
        if (!S_ISDIR(st1.st_mode)) {
            err = ENOTDIR;
            goto error;
        }
        free(tmp);
        tmp = NULL;
        path = sep + 1;
    }

    /* Open the main file, if it exists. It might not exist. */
    fd1 = openat(dirfd, path, O_RDONLY | O_CLOEXEC);
    if (fd1 == -1) {
        err = errno;
        if (err != ENOENT) {
            goto error;
        }
    } else {
        r = fstat(fd1, &st1);
        if (r == -1) {
            err = errno;
            goto error;
        }
        if (!S_ISREG(st1.st_mode)) {
            if (S_ISDIR(st1.st_mode)) {
                err = EISDIR;
            } else {
                err = ENOENT;
            }
            goto error;
        }
    }

    /* Scratch buffer for generating filenames.  */
    len = strlen(path);
    tmp = malloc(len + sizeof(kForkPath1));
    if (tmp == NULL) {
        err = errno;
        goto error;
    }

    if (fd1 != -1) {
        /* Check if the file itself is AppleDouble or AppleSingle. */
        if (len > sizeof(kAppleDoublePrefix) - 1 &&
            memcmp(path, kAppleDoublePrefix, sizeof(kAppleDoublePrefix) - 1) ==
                0) {
            err = unrez_applefile_parse(&mdata, fd1, st1.st_size);
            if (err != 0) {
                if (err != kUnrezErrFormat) {
                    goto error;
                }
            } else {
                if (mdata.type == kUnrezTypeAppleSingle) {
                    forks->data.file = fd1;
                    forks->data.offset = mdata.data_offset;
                    forks->data.size = mdata.data_size;
                } else {
                    memcpy(tmp, path + sizeof(kAppleDoublePrefix) - 1,
                           len + 1 - (sizeof(kAppleDoublePrefix) - 1));
                    fd2 = openat(dirfd, tmp, O_RDONLY);
                    if (fd2 == -1) {
                        err = errno;
                        if (err != ENOENT) {
                            goto error;
                        }
                        forks->data.file = -1;
                        forks->data.offset = 0;
                        forks->data.size = 0;
                    } else {
                        r = fstat(fd2, &st2);
                        if (r == -1) {
                            err = errno;
                            goto error;
                        }
                        if (!S_ISREG(st2.st_mode)) {
                            if (S_ISDIR(st2.st_mode)) {
                                err = EISDIR;
                            } else {
                                err = ENOENT;
                            }
                            goto error;
                        }
                        forks->data.file = fd2;
                        forks->data.offset = 0;
                        forks->data.size = st2.st_size;
                    }
                }
                forks->rsrc.file = fd1;
                forks->rsrc.offset = mdata.rsrc_offset;
                forks->rsrc.size = mdata.rsrc_size;
                forks->metadata = mdata;
                goto success;
            }
        }

        /*
         * Check for MacBinary. MacBinary has particularly weak magic, resulting
         * in false positives, so we only try if the filename matches. This is
         * particularly bad with QuickDraw picture files, which tend to start
         * with a header of 512 zeroes. Parsed as a MacBinary file, the checksum
         * will match. So we use less magic here.
         */
        if (len > 4 && memcmp(path + (len - 4), ".bin", 4) == 0) {
            err = unrez_macbinary_parse(&mdata, fd1, st1.st_size);
            if (err != 0) {
                if (err != kUnrezErrFormat) {
                    goto error;
                }
            } else {
                forks->data.file = fd1;
                forks->data.offset = mdata.data_offset;
                forks->data.size = mdata.data_size;
                forks->rsrc.file = fd1;
                forks->rsrc.offset = mdata.rsrc_offset;
                forks->rsrc.size = mdata.rsrc_size;
                forks->metadata = mdata;
                goto success;
            }
        }

        /* Check for AppleDouble or AppleSingle. */
        err = unrez_applefile_parse(&mdata, fd1, st1.st_size);
        if (err != 0) {
            if (err != kUnrezErrFormat) {
                goto error;
            }
        } else {
            forks->data.file = fd1;
            forks->data.offset = mdata.data_offset;
            forks->data.size = mdata.data_size;
            forks->rsrc.file = fd1;
            forks->rsrc.offset = mdata.rsrc_offset;
            forks->rsrc.size = mdata.rsrc_size;
            forks->metadata = mdata;
            goto success;
        }
    }

    /* Check for a separate AppleDouble. */
    memcpy(tmp, kAppleDoublePrefix, sizeof(kAppleDoublePrefix) - 1);
    memcpy(tmp + sizeof(kAppleDoublePrefix) - 1, path, len + 1);
    fd2 = openat(dirfd, tmp, O_RDONLY | O_CLOEXEC);
    if (fd2 == -1) {
        err = errno;
        if (err != ENOENT) {
            goto error;
        }
    } else {
        r = fstat(fd2, &st2);
        if (r == -1) {
            err = errno;
            goto error;
        }
        if (!S_ISREG(st2.st_mode)) {
            if (S_ISDIR(st2.st_mode)) {
                err = EISDIR;
            } else {
                err = ENOENT;
            }
            goto error;
        }
        err = unrez_applefile_parse(&mdata, fd2, st2.st_size);
        if (err != 0) {
            if (err != kUnrezErrFormat) {
                goto error;
            }
            close(fd2);
            forks->rsrc.file = -1;
            forks->rsrc.offset = 0;
            forks->rsrc.size = 0;
            memset(&forks->metadata, 0, sizeof(forks->metadata));
        } else {
            forks->rsrc.file = fd2;
            forks->rsrc.offset = mdata.rsrc_offset;
            forks->rsrc.size = mdata.rsrc_size;
            forks->metadata = mdata;
        }
        forks->data.file = fd1;
        forks->data.offset = 0;
        forks->data.size = st1.st_size;
        goto success;
    }

    /* Check for native forks. */
    for (i = 0; i < 2; i++) {
        memcpy(tmp, path, len);
        strcpy(tmp + len, kForkPaths[i]);
        fd2 = openat(dirfd, tmp, O_RDONLY | O_CLOEXEC);
        if (fd2 == -1) {
            err = errno;
            if (err != ENOENT && err != ENOTDIR) {
                goto error;
            }
        } else {
            r = fstat(fd2, &st2);
            if (r == -1) {
                err = errno;
                goto error;
            }
            if (fd1 != -1) {
                /* Not sure if this is even possible. */
                forks->data.file = fd1;
                forks->data.offset = 0;
                forks->data.size = st1.st_size;
            } else {
                forks->data.file = -1;
                forks->data.offset = 0;
                forks->data.size = 0;
            }
            forks->rsrc.file = fd2;
            forks->rsrc.offset = 0;
            forks->rsrc.size = st2.st_size;
            memset(&forks->metadata, 0, sizeof(forks->metadata));
            goto success;
        }
    }

    /* No resource fork present. */
    if (fd1 == -1) {
        err = ENOENT;
        goto error;
    }
    forks->data.file = fd1;
    forks->data.offset = 0;
    forks->data.size = st1.st_size;
    forks->rsrc.file = -1;
    forks->rsrc.offset = 0;
    forks->rsrc.size = 0;
    memset(&forks->metadata, 0, sizeof(forks->metadata));
    goto success;

success:
    free(tmp);
    if (close_dir)
        close(dirfd);
    return 0;

error:
    free(tmp);
    if (fd1 != -1)
        close(fd1);
    if (fd2 != -1)
        close(fd2);
    if (close_dir)
        close(dirfd);
    return err;
}

void unrez_forkedfile_close(struct unrez_forkedfile *forks) {
    int data = forks->data.file, rsrc = forks->rsrc.file;
    if (data != -1) {
        close(data);
    }
    if (rsrc != -1 && data != rsrc) {
        close(rsrc);
    }
    free(forks->metadata.comment);
    free(forks->metadata.filename);
}

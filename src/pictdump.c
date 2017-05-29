/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "unrez.h"

#include "defs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sysexits.h>

enum { kToolDump = 1, kTool2Png };

static int tool;

static const unsigned char kPictCode[4] = {'P', 'I', 'C', 'T'};

enum {
    kModeData,
    kModeRsrc,
    kModeRsrcAll,
};

static int opt_id;
static int opt_mode;
static int opt_no_header;
static const char *opt_dir;
static const char *opt_out;

static int error_count;
static int dirfd;
static int has_dir;

static void make_dir(void) {
    int r, err, fd;
    if (has_dir) {
        return;
    }
    r = mkdir(opt_dir, 0777);
    if (r != 0) {
        err = errno;
        if (err != EEXIST) {
            die_errf(EX_CANTCREAT, err, "%s", opt_dir);
        }
    }
    fd = open(opt_dir, O_RDONLY);
    if (fd == -1) {
        die_errf(EX_CANTCREAT, errno, "%s", opt_dir);
    }
    dirfd = fd;
    has_dir = 1;
}

static void opt_parse_id(void *value, const char *option, const char *arg) {
    (void)value;
    (void)option;
    opt_id = parse_id(arg);
    opt_mode = kModeRsrc;
}

static void opt_parse_all(void *value, const char *option, const char *arg) {
    (void)value;
    (void)option;
    (void)arg;
    opt_mode = kModeRsrcAll;
}

static void opt_parse_dir(void *value, const char *option, const char *arg) {
    (void)value;
    (void)option;
    opt_dir = arg;
    opt_out = NULL;
}

static void opt_parse_out(void *value, const char *option, const char *arg) {
    (void)value;
    (void)option;
    opt_dir = NULL;
    opt_out = arg;
}

static const struct option kOptionsDump[] = {
    {"all-picts", NULL, 0, opt_parse_all},
    {"id", NULL, 1, opt_parse_id},
    {"no-header", &opt_no_header, 0, opt_parse_true},
    {0},
};

static const struct option kOptions2Png[] = {
    {"all-picts", NULL, 0, opt_parse_all},
    {"dir", NULL, 1, opt_parse_dir},
    {"id", NULL, 1, opt_parse_id},
    {"no-header", &opt_no_header, 0, opt_parse_true},
    {"out", NULL, 1, opt_parse_out},
    {0},
};

static void pictdump_usage(FILE *fp) {
    fputs("usage: unrez pictdump [<options>] <file>\n", fp);
}

static void pict2png_usage(FILE *fp) {
    fputs("usage: unrez pict2png [<options>] <file>...\n", fp);
}

struct pict2png {
    const char *outfile;
    int success;
    int error;
};

static void cb_error(void *ctx, int err, int opcode, const char *msg) {
    const char *opname;
    char buf[256];
    int r;
    struct pict2png *pp = ctx;
    if (pp != NULL) {
        pp->error = 1;
    }
    error_count++;
    fputs("  error: ", stdout);
    if (opcode >= 0) {
        printf("in op $%04x", opcode);
        opname = unrez_pict_opname(opcode);
        if (opname != NULL) {
            printf(" %s", opname);
        }
        fputs(": ", stdout);
    }
    r = unrez_strerror(err, buf, sizeof(buf));
    if (r == 0) {
        fputs(buf, stdout);
    } else {
        printf("error #%d", err);
    }
    if (msg != NULL) {
        printf(": %s", msg);
    }
    fputc('\n', stdout);
    if (err > 0) {
        exit(EX_OSERR);
    }
}

static int pict2png_header(void *ctx, int version,
                           const struct unrez_rect *frame) {
    (void)ctx;
    (void)version;
    (void)frame;
    return 0;
}

static int pict2png_opcode(void *ctx, int opcode, const void *data,
                           size_t size) {
    (void)ctx;
    (void)opcode;
    (void)data;
    (void)size;
    return 0;
}

static int pict2png_pixels(void *ctx, int opcode, struct unrez_pixdata *pix) {
    struct pict2png *pp = ctx;
    int err;
    (void)opcode;
    if (pix->pixelSize == 16) {
        err = unrez_pixdata_16to32(pix);
        if (err != 0) {
            die_errf(EX_SOFTWARE, err, "16to32");
        }
    }
    write_png(has_dir ? dirfd : AT_FDCWD, pp->outfile, pix);
    pp->success = 1;
    return 0;
}

static const struct unrez_pict_callbacks kCallbacks2Png = {
    NULL, pict2png_header, pict2png_opcode, pict2png_pixels, cb_error,
};

static void pict2png_raw(const char *file, int is_rsrc, int rsrc_id,
                         const void *data, size_t size) {
    const char *base;
    char buf[1024];
    int err;
    struct pict2png pp = {0};
    struct unrez_pict_callbacks cb = kCallbacks2Png;
    cb.ctx = &pp;
    if (opt_out == NULL) {
        make_dir();
        base = strrchr(file, '/');
        if (base == NULL) {
            base = file;
        } else {
            base++;
        }
        if (is_rsrc) {
            err = snprintf(buf, sizeof(buf), "%s.%d.png", base, rsrc_id);
        } else {
            err = snprintf(buf, sizeof(buf), "%s.png", base);
        }
        if (err < 0) {
            die_errf(EX_OSERR, errno, "snprintf");
        }
        if ((size_t)err >= sizeof(buf)) {
            dief(EX_SOFTWARE, "filename too long");
        }
        pp.outfile = buf;
    } else {
        pp.outfile = opt_out;
    }
    printf("writing %s...\n", pp.outfile);
    unrez_pict_decode(&cb, data, size);
    if (!pp.error && !pp.success) {
        error_count++;
        fputs("  error: picture has no bitmap\n", stderr);
    }
}

static int dump_header(void *ctx, int version, const struct unrez_rect *frame) {
    (void)ctx;
    printf("  version = %d\n", version);
    printf("  frame = {top = %d, left = %d, bottom = %d, right = %d}\n",
           frame->top, frame->left, frame->bottom, frame->right);
    return 0;
}

static void show_opcode(int opcode) {
    const char *opname;
    printf("  op $%04x", opcode);
    opname = unrez_pict_opname(opcode);
    if (opname != NULL) {
        printf(" %s", opname);
    }
    fputc('\n', stdout);
}

static int dump_opcode(void *ctx, int opcode, const void *data, size_t size) {
    (void)ctx;
    (void)data;
    (void)size;
    show_opcode(opcode);
    return 0;
}

static int dump_pixels(void *ctx, int opcode, struct unrez_pixdata *pix) {
    (void)ctx;
    show_opcode(opcode);
    printf("    rowBytes = %d\n", pix->rowBytes);
    printf("    bounds = {top = %d, left = %d, bottom = %d, right = %d}\n",
           pix->bounds.top, pix->bounds.left, pix->bounds.bottom,
           pix->bounds.right);
    printf("    packType = %d\n", pix->packType);
    printf("    packSize = %d\n", pix->packSize);
    printf("    hRes = %d\n", pix->hRes);
    printf("    vRes = %d\n", pix->vRes);
    printf("    pixelType = %d\n", pix->pixelType);
    printf("    pixelSize = %d\n", pix->pixelSize);
    printf("    cmpCount = %d\n", pix->cmpCount);
    printf("    cmpSize = %d\n", pix->cmpSize);
    return 0;
}

static const struct unrez_pict_callbacks kCallbacksDump = {
    NULL, dump_header, dump_opcode, dump_pixels, cb_error,
};

static void pictdump_raw(const void *data, size_t size) {
    char ssize[SIZE_WIDTH];
    sprint_size(ssize, sizeof(ssize), size);
    printf("  size = %s\n", ssize);
    unrez_pict_decode(&kCallbacksDump, data, size);
    fputc('\n', stdout);
}

static void pict_data(const char *file) {
    struct unrez_forkedfile forks;
    struct unrez_data fdata;
    int err;
    const void *data;
    size_t size;
    err = unrez_forkedfile_open(&forks, file);
    if (err != 0) {
        die_errf(err > 0 ? EX_NOINPUT : EX_DATAERR, err, "%s", file);
    }
    err = unrez_fork_read(&forks.data, &fdata);
    if (err != 0) {
        die_errf(EX_OSERR, err, "%s", file);
    }
    unrez_forkedfile_close(&forks);
    data = fdata.data;
    size = fdata.size;
    if (!opt_no_header) {
        if (size < kUnrezPictHeaderSize) {
            dief(EX_DATAERR, "%s: missing header", file);
        }
        data = (const char *)data + kUnrezPictHeaderSize;
        size -= kUnrezPictHeaderSize;
    }
    switch (tool) {
    case kToolDump:
        printf("%s data:\n", file);
        pictdump_raw(data, size);
        break;
    case kTool2Png:
        pict2png_raw(file, 0, 0, data, size);
        break;
    }
    unrez_data_destroy(&fdata);
}

static void pict_rsrc1(const char *file, struct unrez_resourcefork *rfork,
                       int type_index, int rsrc_index) {
    struct unrez_resource *rsrc;
    const void *data;
    uint32_t size;
    int err;
    rsrc = &rfork->types[type_index].resources[rsrc_index];
    err =
        unrez_resourcefork_getrsrc(rfork, type_index, rsrc_index, &data, &size);
    if (err != 0) {
        die_errf(err > 0 ? EX_OSERR : EX_DATAERR, err, "%s 'PICT' #%d", file,
                 rsrc->id);
    }
    switch (tool) {
    case kToolDump:
        printf("%s PICT #%d:\n", file, rsrc->id);
        pictdump_raw(data, size);
        break;
    case kTool2Png:
        pict2png_raw(file, 1, rsrc->id, data, size);
        break;
    }
}

static void pict_rsrc(const char *file) {
    struct unrez_resourcefork rfork;
    struct unrez_resourcetype *type;
    int err, type_index, count, rsrc_index;
    err = unrez_resourcefork_open(&rfork, file);
    if (err != 0) {
        die_errf(err > 0 ? EX_NOINPUT : EX_DATAERR, err, "%s", file);
    }
    type_index = unrez_resourcefork_findtype(&rfork, kPictCode);
    if (type_index >= 0) {
        type = &rfork.types[type_index];
        count = type->count;
        err = unrez_resourcefork_loadtype(&rfork, type_index);
        if (err != 0) {
            die_errf(err > 0 ? EX_OSERR : EX_DATAERR, err, "%s", file);
        }
        if (opt_mode == kModeRsrc) {
            rsrc_index = unrez_resourcefork_findid(&rfork, type_index, opt_id);
            if (rsrc_index < 0) {
                dief(EX_NOINPUT, "resource not found: 'PICT' #%d", opt_id);
            }
            pict_rsrc1(file, &rfork, type_index, rsrc_index);
        } else {
            for (rsrc_index = 0; rsrc_index < count; rsrc_index++) {
                pict_rsrc1(file, &rfork, type_index, rsrc_index);
            }
        }
    }
    unrez_resourcefork_close(&rfork);
}

static void pict_exec(int argc, char **argv) {
    int i;
    if (argc < 1) {
        errorf("expected 1 or more arguments");
        exit(EX_USAGE);
    }
    if (opt_mode == kModeData) {
        for (i = 0; i < argc; i++) {
            pict_data(argv[i]);
        }
    } else {
        for (i = 0; i < argc; i++) {
            pict_rsrc(argv[i]);
        }
    }
    if (error_count > 0) {
        errorf("some pictures could not be decoded");
        exit(EX_DATAERR);
    }
}

void pictdump_exec(int argc, char **argv) {
    tool = kToolDump;
    parse_options(kOptionsDump, &argc, &argv);
    pict_exec(argc, argv);
}

void pict2png_exec(int argc, char **argv) {
    tool = kTool2Png;
    parse_options(kOptions2Png, &argc, &argv);
    if (opt_out != NULL) {
        if (argc > 1 || opt_mode == kModeRsrcAll) {
            dief(EX_USAGE, "-out cannot be used with multiple pictures");
        }
    } else if (opt_dir == NULL) {
        dief(EX_USAGE, "either -out or -dir must be specified");
    }
    pict_exec(argc, argv);
}

void pictdump_help(void) {
    pictdump_usage(stdout);
    fputs(
        "Dump opcodes from a QuickDraw picture.\n"
        "\n"
        "options:\n"
        "  -all-picts    dump all PICT resources\n"
        "  -id <id>      dump PICT resource id <id>\n"
        "  -no-header    the picture does not have a 512-byte header\n",
        stdout);
}

void pict2png_help(void) {
    pict2png_usage(stdout);
    fputs(
        "Convert QuickDraw pictures to PNG.\n"
        "\n"
        "options:\n"
        "  -all-picts    dump all PICT resources\n"
        "  -dir <dir>    write PNG files to <dir>\n"
        "  -id <id>      dump PICT resource id <id>\n"
        "  -out <file>   write output to <file> (if only one output)\n"
        "  -no-header    the pictures do not have a 512-byte header\n",
        stdout);
}

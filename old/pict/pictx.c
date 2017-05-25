#define _GNU_SOURCE /* for asprintf */

#include "scpimage/pictx.h"
#include "scpimage/image.h"
#include "scpimage/pict.h"
#include "scpimage/png.h"
#include "scpbase/optparse.h"
#include "scpbase/darray.h"
#include "scpbase/error.h"
#include "scpbase/error_handler.h"
#include "scpbase/ifile.h"
#include "scpbase/ofile.h"
#include "scprsrc/util.h"
#include "scprsrc/forkedfile.h"
#include "scprsrc/resourcefork.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <sysexits.h>

enum {
    OPT_HELP = 256
};

static struct optparser_option const PICTX_OPTIONS[] = {
    { 'D', 'D', "directory", "DIR",
      "extract pictures to DIR" },
    { OPT_HELP, 0, "help", NULL,
      "show help for this command" },
    /* FIXME doesn't work right now
    { 'l', 'l', "list", NULL,
      "list pictures only, don't extract" },
    */
    { 'v', 'v', "verbose", NULL,
      "verbose output" },
    { 0, 0, NULL, NULL, NULL }
};

static char const PICTX_HELP[] =
    "PICT Extract: exctract PICT resources from a file\n"
    "\n";

static char const PICTX_USAGE[] =
    "Usage: %s [options] FILE "
    "[{ ID | MIN_ID:MAX_ID } ...]\n";

static const uint8_t PICT_TYPE[4] = { 'P', 'I', 'C', 'T' };

void
pictx_help(char const *cname)
{
    fputs(PICTX_HELP, stderr);
    fputs(RSRC_HELP, stderr);
    fprintf(stderr, PICTX_USAGE, cname);
    optparser_help(PICTX_OPTIONS);
}

struct pictx_state {
    char const *path;
    char const *dir; /* no trailing slash and not empty */
    char *dirb; /* malloc'd storage for above */
    int16_t (*range)[2];
    size_t rangecount;
    size_t rangealloc;
    bool verbose;
    bool list_only;
    bool all;
    struct error_handler *err;
    struct error_handler *ierr;
    char const *cname;
    struct resourcefork rfork;
    int tidx;
    int16_t *ridx;
    size_t ridxcount;
    int status;
};

static int
pictx_parseopt(struct pictx_state *s, int argc, char **argv)
{
    struct optparser parse;
    int opt;
    int16_t range[2];
    struct error *e = NULL;
    size_t l;
    optparser_create(&parse, PICTX_OPTIONS, argc, argv,
             true, s->err);
    while ((opt = optparser_next(&parse)) >= 0) {
        switch (opt) {
        case 0:
            if (!s->path) {
                s->path = parse.param;
                break;
            }
            s->all = false;
            if (rsrc_idrange_parse(range, parse.param, s->err))
                return EX_USAGE;
            if (ARRAY_GROW(s->range, s->rangecount + 1, &e)) {
                error_handle(s->err, &e);
                return 1;
            }
            memcpy(&s->range[s->rangecount++],
                   &range, sizeof(range));
            break;
        case 'D':
            s->dir = parse.param;
            break;
        case OPT_HELP:
            pictx_help(s->cname);
            return 1;
        case 'l':
            s->list_only = true;
            break;
        case 'v':
            s->verbose = true;
            break;
        default:
            assert(0);
        }
    }
    if (opt != -1)
        return EX_USAGE;
    if (!s->path) {
        error_msg(s->err, "No file specified");
        return EX_USAGE;
    }
    if (s->dir && *s->dir) {
        l = strlen(s->dir);
        if (l > 1 && s->dir[l-1] == '/') {
            s->dirb = malloc(l);
            if (!s->dirb) {
                error_memory(&e);
                error_handle(s->err, &e);
                return 1;
            }
            memcpy(s->dirb, s->dir, l - 1);
            s->dirb[l-1] = '\0';
            s->dir = s->dirb;
        }
    } else
        s->dir = ".";
    return 0;
}

static void
pictx_extract1(struct pictx_state *s, size_t i)
{
    struct error *e = NULL;
    struct error_location rerr, oerr;
    struct image *img;
    struct ofile *ofile;
    char *opath, iloc[16];
    void *data;
    uint32_t length;
    int ridx, rid, r = 0;

    /* Read picture */
    ridx = s->ridx[i];
    rid = s->rfork.types[s->tidx].resources[ridx].id;
    snprintf(iloc, sizeof(iloc), "PICT#%i", rid);
    error_location_create(&rerr, s->err, iloc);
    if (resourcefork_rsrc_read(&s->rfork, s->tidx, ridx,
                               &data, &length, &rerr.err)) {
        s->status = 1;
        return;
    }
    img = pict_read(data, length, &rerr.err);
    if (!img) {
        s->status = 1;
        return;
    }

    /* Print information */
    printf("PICT %6i: %5i x %5i, channels: %i, bits: %i\n",
           rid, img->width, img->height,
           img->channels, img->sigbits[0]);

    /* Write picture as PNG */
    if (asprintf(&opath, "%s/%i.png", s->dir, rid) < 0) {
        error_system(&e, errno);
        error_handle(s->err, &e);
        image_release(img);
        s->status = 1;
        return;
    }
    error_location_create(&oerr, s->err, opath);
    if (!(ofile = ofile_create(opath, &e))) {
        error_handle(&oerr.err, &e);
        r = 1;
    }
    if (!r && !pngf_write(img, ofile, &e)) {
        error_handle(&oerr.err, &e);
        r = 1;
    }
    image_release(img);
    if (ofile_close(ofile, &e)) {
        error_handle(&oerr.err, &e);
        r = 1;
    }
    free(opath);
    if (r)
        s->status = 1;
}

int
pictx_main(int argc, char **argv, char const *cname)
{
    struct error_stderr err;
    struct error_location ierr;
    struct pictx_state s = {
        .all = true,
        .err = &err.err,
        .ierr = &ierr.err,
        .cname = cname
    };
    struct forkinfo ffile;
    struct ifile *filep;
    struct resourcefork_type *type;
    struct error *e = NULL;
    int r, rcount, ridx, pict_id, v;
    size_t i;

    /* Parse options */
    error_stderr_create(&err, cname);
    if ((r = pictx_parseopt(&s, argc, argv)))
        goto end1;

    /* Open input file resource fork */
    error_location_create(&ierr, &err.err, s.path);
    if (fork_parse(&ffile, s.path, &e)) {
        error_handle(&ierr.err, &e);
        r = 1;
        goto end1;
    }
    if (s.verbose)
        rsrc_forkinfo_show(&ffile);
    if (!ffile.rsrc.length) {
        fork_free(&ffile);
        return 0;
    }
    filep = fork_open(&ffile.rsrc, &e);
    fork_free(&ffile);
    if (!filep) {
        error_handle(&ierr.err, &e);
        goto end1;
    }
    r = resourcefork_open(&s.rfork, filep, &ierr.err);
    ifile_close(filep);
    if (r) goto end1;

    /* Find which pictures to extract */
    s.tidx = resourcefork_find_type(&s.rfork, PICT_TYPE);
    if (s.tidx >= 0) {
        if (resourcefork_load_type(&s.rfork, s.tidx, &ierr.err)) {
            r = 1;
            goto end2;
        }
        type = s.rfork.types + s.tidx;
        rcount = type->rcount;
    } else
        rcount = 0;
    if (rcount <= 0) {
        if (s.verbose)
            fputs("File contains no pictures.\n", stdout);
        goto end2;
    }
    s.ridx = malloc(sizeof(*s.ridx) * rcount);
    if (!s.ridx) {
        error_memory(&e);
        error_handle(&err.err, &e);
        r = 1;
        goto end2;
    }
    if (s.all) {
        s.ridxcount = rcount;
        for (ridx = 0; ridx < rcount; ++ridx)
            s.ridx[ridx] = ridx;
    } else {
        s.ridxcount = 0;
        for (ridx = 0; ridx < rcount; ++ridx) {
            pict_id = type->resources[ridx].id;
            if (rsrc_idrange_check(s.range, s.rangecount,
                           pict_id))
                s.ridx[s.ridxcount++] = ridx;
        }
        if (!s.ridxcount) {
            if (s.verbose)
                fputs("All pictures were skipped.\n",
                      stdout);
            goto end3;
        }
    }

    /* Extract pictures */
    v = mkdir(s.dir, 0777);
    if (v && errno != EEXIST) {
        error_system(&e, errno);
        ierr.loc = s.dir;
        error_handle(&ierr.err, &e);
        r = 1;
        goto end3;
    }
    for (i = 0; i < s.ridxcount; ++i)
        pictx_extract1(&s, i);
    r = s.status;

end3:
    free(s.ridx);
end2:
    resourcefork_close(&s.rfork);
end1:
    free(s.range);
    return r;
}

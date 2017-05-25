#include "scprsrc/rcat.h"
#include "scprsrc/util.h"
#include "scprsrc/forkedfile.h"
#include "scprsrc/resourcefork.h"
#include "scpbase/optparse.h"
#include "scpbase/binary.h"
#include "scpbase/ifile.h"
#include "scpbase/ofile.h"
#include "scpbase/error_handler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>

static struct optparser_option const RCAT_OPTIONS[] = {
    { 'h', 0, "help", NULL,
      "show help for this command" },
    { 0, 0, NULL, NULL, NULL }
};

static char const RCAT_HELP[] =
    "%s: print resource contents\n"
    "\n";

static char const RCAT_USAGE[] =
    "Usage: %s FILE TYPE ID\n";

void
rcat_help(char const *cname)
{
    fprintf(stderr, RCAT_HELP, cname);
    fputs(RSRC_HELP, stderr);
    fputs(RSRC_HELP_TYPE, stderr);
    fprintf(stderr, RCAT_USAGE, cname);
    optparser_help(RCAT_OPTIONS);
}

struct rcat_options {
    char const *cname;
    char const *args[3];
    size_t argcount;
    struct error_handler *err;
};

static int
rcat_parseopt(struct rcat_options *options, int argc, char **argv)
{
    struct optparser parse;
    int opt;
    optparser_create(&parse, RCAT_OPTIONS, argc, argv,
                     true, options->err);
    while ((opt = optparser_next(&parse)) >= 0) {
        switch (opt) {
        case 0:
            if (options->argcount == 3)
                goto prusage;
            options->args[options->argcount++] = parse.param;
            break;
        case 'h':
            rcat_help(options->cname);
            return EX_USAGE;
        default:
            assert(0);
        }
    }
    if (opt != -1)
        return EX_USAGE;
    if (options->argcount != 3) {
    prusage:
        fputs(RCAT_USAGE, stderr);
        return EX_USAGE;
    }
    return 0;
}

int
rcat_main(int argc, char **argv, char const *cname)
{
    struct error_stderr err;
    struct error_location errl;
    struct rcat_options options = {
        .cname = cname,
        .argcount = 0,
        .err = &err.err
    };
    struct forkinfo ffile;
    struct ifile *filep;
    struct resourcefork rfork;
    uint8_t type[4];
    int16_t id;
    int typeidx, ridx;
    struct ofile *ofile;
    void *data;
    uint32_t length;
    int r;
    struct error *e = NULL;
    char const *fpath;
    char typen[RSRC_CODE_SIZE];

    error_stderr_create(&err, cname);

    // Parse options
    if ((r = rcat_parseopt(&options, argc, argv)))
        return r;
    fpath = options.args[0];
    if (rsrc_decode_type(type, options.args[1], &err.err))
        return EX_USAGE;
    if (rsrc_id_parse(&id, options.args[2], &err.err))
        return EX_USAGE;
    error_location_create(&errl, &err.err, fpath);
    options.err = &errl.err;

    // Process data
    if (fork_parse(&ffile, fpath, &e)) {
        error_handle(&errl.err, &e);
        return 1;
    }
    filep = fork_open(&ffile.rsrc, &e);
    fork_free(&ffile);
    if (!filep) {
        error_handle(&errl.err, &e);
        return 1;
    }
    if (resourcefork_open(&rfork, filep, &errl.err)) {
        ifile_close(filep);
        return 1;
    }
    ifile_close(filep);
    typeidx = resourcefork_find_type(&rfork, type);
    if (typeidx < 0) {
        resourcefork_close(&rfork);
        rsrc_format(typen, type);
        error_msg(&errl.err, "Resource fork has no such type \"%s\"", typen);
        return 1;
    }
    if (resourcefork_load_type(&rfork, typeidx, &errl.err)) {
        resourcefork_close(&rfork);
        return 1;
    }
    ridx = resourcefork_find_rsrc(&rfork, typeidx, id);
    if (ridx < 0) {
        resourcefork_close(&rfork);
        error_msg(&errl.err,
                  "Resource fork has no such resource %i", (int)id);
        return 1;
    }
    if (resourcefork_rsrc_read(&rfork, typeidx, ridx,
                                &data, &length, &errl.err)) {
        resourcefork_close(&rfork);
        return 1;
    }
    ofile = ofile_create_stdout(&e);
    if (!ofile) {
        resourcefork_close(&rfork);
        error_handle(&err.err, &e);
        return 1;
    }
    ofile_write(ofile, data, length);
    resourcefork_close(&rfork);
    return 0;
}

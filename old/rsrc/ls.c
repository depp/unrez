#include "scprsrc/rls.h"
#include "scprsrc/util.h"
#include "scprsrc/forkedfile.h"
#include "scprsrc/resourcefork.h"
#include "scpbase/optparse.h"
#include "scpbase/darray.h"
#include "scpbase/binary.h"
#include "scpbase/ifile.h"
#include "scpbase/error_handler.h"
#include "scpbase/tool.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

enum {
    OPT_HELP = 256
};

static struct optparser_option const RLS_OPTIONS[] = {
    { 'a', 'a', "all", NULL,
      "list resources of all types" },
    { 'h', 'h', NULL, NULL,
      "print sizes in human readable format" },
    { OPT_HELP, 0, "help", NULL,
      "show help for this command" },
    { 't', 't', "type", "TYPE",
      "list resources of type TYPE" },
    { 'v', 'v', "verbose", NULL,
      "verbose output" },
    { 0, 0, NULL, NULL, NULL }
};

static char const RLS_HELP[] =
    "%s: list the resources in files\n"
    "\n";

static char const RLS_USAGE[] =
    "Usage: %s [options] FILES\n";

void
rls_help(char const *cname)
{
    fprintf(stderr, RLS_HELP, cname);
    fputs(RSRC_HELP, stderr);
    fputs(RSRC_HELP_TYPE, stderr);
    fprintf(stderr, RLS_USAGE, cname);
    optparser_help(RLS_OPTIONS);
}

struct rls_options {
    struct error_handler *err;
    char const *cname;
    char **file;
    size_t filecount;
    size_t filealloc;
    uint8_t (*type)[4];
    size_t typecount;
    size_t typealloc;
    bool verbose;
    bool all;
    bool hsizes;
};

static void
rls_types(struct rls_options *options,
          struct resourcefork *rfork)
{
    int i;
    char scode[RSRC_CODE_SIZE];
    uint8_t const *code;
    (void)options;
    for (i = 0; i < rfork->tcount; ++i) {
        code = rfork->types[i].code;
        rsrc_format(scode, code);
        printf("  type 0x%08x \"%s\"\n",
               read_bu32(code), scode);
    }
}

static int
rls_resources(struct rls_options *options,
              struct resourcefork *rfork)
{
    int ti, ri, r = 0;
    uint32_t ti2;
    char scode[RSRC_CODE_SIZE], sbuf[SIZE_BUFSZ];
    struct resourcefork_type *t;
    int32_t len;
    for (ti = 0; ti < rfork->tcount; ++ti) {
        t = rfork->types + ti;
        if (!options->all) {
            for (ti2 = 0; ti2 < options->typecount; ++ti2) {
                if (!memcmp(options->type[ti2],
                            t->code, 4))
                    break;
            }
            if (ti2 == options->typecount)
                continue;
        }
        rsrc_format(scode, t->code);
        printf("  type 0x%08x \"%s\":\n",
               read_bu32(t->code), scode);
        if (resourcefork_load_type(rfork, ti, options->err)) {
            r = 1;
            continue;
        }
        for (ri = 0; ri < t->rcount; ++ri) {
            if (resourcefork_rsrc_loc(
                    rfork, ti, ri,
                    NULL, &len, options->err)) {
                r = 1;
                continue;
            }
            printf("    %6i: ",
                   (int)t->resources[ri].id);
            if (options->hsizes) {
                format_size(sbuf, len);
                printf("%4s", sbuf);
            } else
                printf("%8i", len);
            putchar('\n');
        }
    }
    return r;
}

static int
rls_file(struct rls_options *options, char const *path)
{
    struct ifile *filep;
    struct forkinfo ffile;
    struct resourcefork rfork;
    int r;
    struct error *e = NULL;
    if (fork_parse(&ffile, path, &e)) {
        error_handle(options->err, &e);
        return 1;
    }
    if (options->verbose)
        rsrc_forkinfo_show(&ffile);
    else
        printf("%s:\n", path);
    filep = fork_open(&ffile.rsrc, &e);
    fork_free(&ffile);
    if (!filep) {
        error_handle(options->err, &e);
        return 1;
    }
    if (resourcefork_open(&rfork, filep, options->err)) {
        ifile_close(filep);
        return 1;
    }
    ifile_close(filep);
    if (!options->all && !options->typecount) {
        r = 0;
        rls_types(options, &rfork);
    } else
        r = rls_resources(options, &rfork);
    resourcefork_close(&rfork);
    return r;
}

static int
rls_parseopt(struct rls_options *options, int argc, char **argv)
{
    struct optparser parse;
    int opt;
    uint8_t code[4];
    struct error *e = NULL;
    optparser_create(&parse, RLS_OPTIONS, argc, argv,
                     true, options->err);
    while ((opt = optparser_next(&parse)) >= 0) {
        switch (opt) {
        case 0:
            if (ARRAY_GROW(options->file, options->filecount + 1, &e)) {
                error_handle(options->err, &e);
                return 1;
            }
            options->file[options->filecount++] = parse.param;
            break;
        case 'a':
            options->all = true;
            break;
        case 'h':
            options->hsizes = true;
            break;
        case OPT_HELP:
            rls_help(options->cname);
            return EX_USAGE;
        case 't':
            if (rsrc_decode_type(code, parse.param, options->err))
                return EX_USAGE;
            if (ARRAY_GROW(options->type, options->typecount + 1, &e)) {
                error_handle(options->err, &e);
                return 1;
            }
            memcpy(&options->type[options->typecount++],
                   code, 4);
            break;
        case 'v':
            options->verbose = true;
            break;
        default:
            assert(0);
        }
    }
    if (opt != -1)
        return EX_USAGE;
    return 0;
}

int
rls_main(int argc, char **argv, char const *cname)
{
    struct error_stderr err;
    struct error_location errl;
    struct rls_options options = {
        .err = &err.err,
        .cname = cname
    };
    size_t i;
    int r, s;
    char const *fpath;
    error_stderr_create(&err, cname);
    r = rls_parseopt(&options, argc, argv);
    if (!r) {
        for (i = 0; i < options.filecount; ++i) {
            fpath = options.file[i];
            error_location_create(&errl, &err.err, fpath);
            options.err = &errl.err;
            if ((s = rls_file(&options, fpath)))
                if (!r) r = s;
        }
    }
    free(options.type);
    free(options.file);
    return r;
}

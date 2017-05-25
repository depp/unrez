#include "scpbase/optparse.h"
#include "scpbase/subcommand.h"
#include "scpbase/error_handler.h"
#include "scprsrc/rcat.h"
#include "scprsrc/rls.h"
#include <stdio.h>
#include <assert.h>
#include <sysexits.h>

static int
rsrc_help(int argc, char **argv, char const *cname);

static const struct optparser_option RSRC_OPTIONS[] = {
    { 'h', 0, "help", NULL, NULL },
    { 0, 0, NULL, NULL, NULL }
};

static char const RSRC_USAGE[] =
    "Usage: rsrc [--help] COMMAND ...\n";

static const struct subcommand RSRC_COMMANDS[] = {
    { "cat", "print a resource",
      rcat_main, rcat_help },
    { "help", "print help for a command",
      rsrc_help, NULL },
    { "ls", "list resources in a file",
      rls_main, rls_help },
    { NULL, NULL, NULL, NULL }
};

static int
rsrc_help(int argc, char **argv, char const *cname)
{
    if (subcommand_help(RSRC_COMMANDS, argc, argv, cname))
        return 1;
    fputs(RSRC_USAGE, stderr);
    subcommand_help_commands(RSRC_COMMANDS);
    return 1;
}

int
main(int argc, char *argv[])
{
    struct optparser parse;
    int opt;
    struct error_stderr err;
    error_stderr_create(&err, "rsrc");
    optparser_create(&parse, RSRC_OPTIONS,
                     argc - 1, argv + 1, true, &err.err);
    while ((opt = optparser_next(&parse)) >= 0) {
        switch (opt) {
        case 0:
            return subcommand_run(
                RSRC_COMMANDS,
                parse.argc + 1, parse.argv - 1,
                "rsrc");
        case 'h':
            return rsrc_help(parse.argc, parse.argv, "rsrc");
        default:
            assert(0);
        }
    }
    if (opt == -1)
        fputs(RSRC_USAGE, stderr);
    return EX_USAGE;
}

/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

static void help_usage(FILE *fp);
static void help_exec(int argc, char **argv);
static void help_help(void);

static void version_exec(int argc, char **argv);
static void version_help(void);

struct command {
    const char *name;
    const char *description;
    void (*exec)(int argc, char **argv);
    void (*help)(void);
};

static const struct command kCommands[] = {
    {"cat", "print resource contents on standard output", cat_exec, cat_help},
    {"help", "print help", help_exec, help_help},
    {"info", "print information about a file and its resource fork", info_exec,
     info_help},
    {"ls", "list resource fork contents", ls_exec, ls_help},
    {"pict2png", "convert a QuickDraw picture to PNG", pict2png_exec,
     pict2png_help},
    {"pictdump", "dump QuickDraw picture opcodes", pictdump_exec,
     pictdump_help},
    /*
    {"resx", "extract resources from a resource fork", resx_exec, resx_help},
    */
    {"version", "print the version", version_exec, version_help},
};

/* cmd_find finds the command with the given name, or NULL if not found. */
static const struct command *cmd_find(const char *name) {
    const struct command *p = kCommands,
                         *e = p + sizeof(kCommands) / sizeof(*kCommands);
    for (; p != e; p++) {
        if (strcmp(p->name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *arg, *opt;
    const struct command *cmd;
    if (argc <= 1) {
        help_usage(stderr);
        exit(EX_USAGE);
    }
    arg = argv[1];
    if (*arg != '-') {
        cmd = cmd_find(arg);
        if (cmd == NULL) {
            dief(EX_USAGE, "unknown command '%s'", arg);
        }
        cmd->exec(argc - 2, argv + 2);
        return 0;
    }
    opt = arg + 1;
    if (*opt == '-') {
        opt++;
    }
    if (strcmp(opt, "help") == 0 || strcmp(opt, "h") == 0) {
        help_exec(argc - 2, argv + 2);
        return 0;
    }
    if (strcmp(opt, "version") == 0) {
        version_exec(argc - 2, argv + 2);
        return 0;
    }
    dief(EX_USAGE, "unknown option '%s'", arg);
    return EX_USAGE;
}

static void help_usage(FILE *fp) {
    const struct command *p, *e;
    fputs(
        "usage: unrez <command> [<args>]\n"
        "\n"
        "commands:\n",
        fp);
    p = kCommands;
    e = p + sizeof(kCommands) / sizeof(*kCommands);
    for (; p != e; p++) {
        fprintf(fp, "  %-10s  %s\n", p->name, p->description);
    }
}

static void help_exec(int argc, char **argv) {
    const char *arg;
    const struct command *cmd;
    if (argc == 0) {
        help_usage(stdout);
        return;
    }
    arg = argv[0];
    if (*arg == '-') {
        dief(EX_USAGE, "unknown option '%s'", arg);
    }
    arg = argv[0];
    cmd = cmd_find(arg);
    if (cmd == NULL) {
        dief(EX_USAGE, "unknown command '%s'", arg);
    }
    cmd->help();
}

static void help_help(void) {
    fputs(
        "usage: unrez help [<command>]\n"
        "Print help for the unrez or an unrez command.\n",
        stdout);
}

static void version_exec(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fputs("unrez version 0.0\n", stdout);
}

static void version_help(void) {
    fputs(
        "usage: unrez version\n"
        "Print the UnRez version.\n",
        stdout);
}

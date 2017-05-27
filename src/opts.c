/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "defs.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

void opt_parse_true(void *value, const char *option, const char *arg) {
    int *ptr = value;
    (void)option;
    (void)arg;
    *ptr = 1;
}

void parse_options(const struct option *opt, int *argc, char ***argv) {
    const struct option *optr;
    char **args = *argv, *arg, *oname, *eq, *param;
    int i = 0, j = 0, n = *argc;
    while (i < n) {
        arg = args[i++];
        if (*arg != '-') {
            args[j++] = arg;
            continue;
        }
        oname = arg + 1;
        if (*oname == '-') {
            oname++;
            if (*oname == '\0') {
                while (i < n) {
                    args[j++] = args[i++];
                }
                break;
            }
        }
        eq = strchr(oname, '=');
        if (eq != NULL) {
            *eq = '\0';
            param = eq + 1;
        } else {
            param = NULL;
        }
        for (optr = opt; optr->name != NULL; optr++) {
            if (strcmp(oname, optr->name) == 0) {
                break;
            }
        }
        if (optr->name == NULL) {
            dief(EX_USAGE, "unknown option '%s'", arg);
        }
        if (optr->has_arg) {
            if (param == NULL) {
                if (i >= n) {
                    dief(EX_USAGE, "missing parameter for '%s'", arg);
                }
                param = args[i++];
            }
        } else {
            if (param != NULL) {
                dief(EX_USAGE, "unexpected parameter for '%s'", arg);
            }
        }
        optr->parse(optr->value, arg, param);
    }
    *argc = j;
}

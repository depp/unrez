/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#include "defs.h"

#include <stdio.h>

void resx_exec(int argc, char **argv) {
    (void)argc;
    (void)argv;
}

void resx_help(void) {
    fputs(
        "usage: unrez resx [<options>] <file>\n"
        "Extract resources from a file's resource fork.\n",
        stdout);
}

/*
 * Copyright 2017 Dietrich Epp.
 *
 * This file is part of UnRez. UnRez is licensed under the terms of the MIT
 * license. For more information, see LICENSE.txt.
 */
#ifndef DEFS_H
#define DEFS_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* Utility Functions */

/*
 * errorf and verrorf print a formatted error message to stderr.
 */
void errorf(const char *msg, ...) __attribute__((format(printf, 1, 2)));
void verrorf(const char *msg, va_list ap);

/*
 * errorf and verrorf print a formatted error message to stderr, with an error
 * code from errno or from unrez.
 */
void error_errf(int errcode, const char *msg, ...)
    __attribute__((format(printf, 2, 3)));
void verror_errf(int errcode, const char *msg, va_list ap);

/*
 * dief and vdief print a formatted error message to stderr and exits with the
 * supplied status code. The status code should be nonzero.
 */
void dief(int status, const char *msg, ...)
    __attribute__((noreturn, format(printf, 2, 3)));
void vdief(int status, const char *msg, va_list ap) __attribute__((noreturn));

/*
 * dief and vdief print a formatted error message to stderr, with an error code
 * from errno or from UnRez, and exits with the supplied status. The status code
 * should be nonzero.
 */
void die_errf(int status, int errcode, const char *msg, ...)
    __attribute__((noreturn, format(printf, 3, 4)));
void vdie_errf(int status, int errcode, const char *msg, va_list ap)
    __attribute__((noreturn));

/* Commands */

void cat_exec(int argc, char **argv);
void cat_help(void);

void info_exec(int argc, char **argv);
void info_help(void);

void ls_exec(int argc, char **argv);
void ls_help(void);

void pict2png_exec(int argc, char **argv);
void pict2png_help(void);

void pictdump_exec(int argc, char **argv);
void pictdump_help(void);

void resx_exec(int argc, char **argv);
void resx_help(void);

/* Argument Parsing */

/*
 * SIZE_WIDTH is the buffer size guaranteed to hold a formatted size.
 */
#define SIZE_WIDTH 24

/*
 * Option to make sprint_size use bytes instead of prefixes.
 */
extern int opt_bytes;

/*
 * sprint_size prints a data size to a string, returning the number of
 * characters that it would have written if the buffer were large enough (like
 * snprintf). The size is rounded to three significant figures, but never more
 * than integer accuracy, and written with SI prefixes, with the unit B for
 * bytes.
 */
int sprint_size(char *buf, size_t bufsize, int64_t size);

/*
 * parse_id parses a string as a resource ID, or prints an error and exits the
 * program.
 */
int parse_id(const char *s);

/*
 * An options the specification for a command-line option flag.
 */
struct option {
    /* The option name, without leading hyphens. */
    const char *name;
    /* Pointer to where the option value is stored, passed to parse(). */
    void *value;
    /*
     * If set, then the option has a mandatory argument. Otherwise, the option
     * may not have an argument.
     */
    int has_arg;
    /*
     * Handle option. If has_arg is set, then arg will be non-NULL and point to
     * the option's argument. Otherwise, arg will be NULL. The name of the
     * option, with leading hyphens, is stored in option.
     */
    void (*parse)(void *value, const char *option, const char *arg);
};

/*
 * opt_parse_true is an option value parser which stores 1 in an int.
 */
void opt_parse_true(void *value, const char *option, const char *arg);

/*
 * parse_options parses command-line options, and modifies argc and argv to only
 * contain the remaining non-option arguments.
 */
void parse_options(const struct option *opt, int *argc, char ***argv);

struct unrez_pixdata;

/*
 * write_png writes pixel data to a PNG file.
 */
void write_png(int dirfd, const char *name, const struct unrez_pixdata *pix);

#endif

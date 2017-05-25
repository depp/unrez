#ifndef SCPRSRC_UTIL_H
#define SCPRSRC_UTIL_H
#include <stdint.h>
#include <stdbool.h>
struct forkinfo;
struct error_handler;

/* A help message common to all tools that operate on resource
   forks.  */
extern char const RSRC_HELP[];

/* A help message common to all tools that operate on resource
   types.  */
extern char const RSRC_HELP_TYPE[];

/* Decode a four character code for a resource type.  Returns true if
   successful.  If there are any problems, an error message will be
   printed to stderr.  */
int
rsrc_decode_type(uint8_t *restrict type,
                 char const *restrict str,
                 struct error_handler *err);

/* The maximum printed size, including nul, of a resource code.  */
#define RSRC_CODE_SIZE 17

/* Format a resource code for printing.  The buffer should contain at
   least RSRC_CODE_SIZE bytes.  */
void
rsrc_format(char *buf, uint8_t const *code);

/* Print out information about a forked file.  */
void
rsrc_forkinfo_show(struct forkinfo *ffile);

/* Parse a resource id.  Return true if successful.  Otherwise, a
   message will be printed to stderr.  */
int
rsrc_id_parse(int16_t *out, char const *str,
              struct error_handler *err);

/* Parse a resource ID range.  The format is either ID or
   MIN_ID:MAX_ID.  This will exit the program with an error if the
   range is ill formed, and will print a warning and return false if
   the numbers are out of range.  If successful, it will return
   true.  */
int
rsrc_idrange_parse(int16_t *out, char const *str,
                   struct error_handler *err);

/* Check to see if a resource ID is in a list of ranges.  */
bool
rsrc_idrange_check(int16_t (*ranges)[2], uint32_t rangecount,
                   int16_t id);

#endif

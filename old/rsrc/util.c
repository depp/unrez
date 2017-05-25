#include "scprsrc/util.h"
#include "scprsrc/forkedfile.h"
#include "scpbase/error_handler.h"
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

char const RSRC_HELP[] =
    "Files may be encoded in MacBinary or AppleDouble format.\n"
    "On certain platforms, native filesystem resource forks are also\n"
    "supported.  The resource fork may be exposed at 'FILE/rsrc' or at\n"
    "'FILE/..namedfork/rsrc'.\n"
    "\n";

char const RSRC_HELP_TYPE[] =
    "Types may be specified as four letter codes such as 'clut' or using\n"
    "hexadecimal, where 'clut' is 636c7574.  Four letter codes may\n"
    "contain hexadecimal escape sequences in the form \\xXX.  It is an\n"
    "error to use non ASCII characters in the command line specification\n"
    "of types, use escape sequences or hexadecimal instead.\n"
    "\n";

static char const
    CODE_SHORT[] = "four character code \"%s\" is too short\n",
    CODE_BADCHAR[] = "four character code contains illegal "
        "character code 0x%02x\n",
    ESCAPE_END[] = "escape sequence ends unexpectedly: \"%s\"",
    ESCAPE_INVALID[] = "unknown escape sequence \"\\%c\"",
    HEX_BAD[] = "invalid hexadecimal number \"%2s\"",
    HEX_INVALID[] = "invalid hexadecimal number \"%s\"",
    HEX_RANGE[] = "type code out of range \"%s\"";

static int
rsrc_hexdecode(char const *restrict p)
{
    int i, val = 0;
    for (i = 0; i < 2; ++i) {
        val <<= 4;
        if (p[i] >= '0' && p[i] <= '9')
            val += p[i] - '0';
        if (p[i] >= 'a' && p[i] <= 'f')
            val += p[i] - 'a' + 10;
        if (p[i] >= 'A' && p[i] <= 'F')
            val += p[i] - 'A' + 10;
        else
            return -1;
    }
    return val;
}

int
rsrc_decode_type(uint8_t *restrict type,
                 char const *restrict str,
                 struct error_handler *err)
{
    int val, i, pos;
    if (str[0] == '0' &&
        (str[1] == 'x' || str[1] == 'X')) {
        for (i = 0; i < 4; ++i) {
            val = rsrc_hexdecode(str + 2*i + 2);
            if (val < 0)
                break;
            type[i] = val;
        }
        if (i == 4 && !str[10])
            return 1;
    }
    for (i = 0, pos = 0; i < 4; ++i) {
        if (!str[pos]) {
            error_msg(err, CODE_SHORT, str);
            return 1;
        }
        if (str[pos] < 0x20 || str[pos] > 0x7e) {
            error_msg(err, CODE_BADCHAR, str[pos]);
            return 1;
        }
        if (str[pos] != '\\') {
            type[i] = str[pos++];
            continue;
        }
        pos++;
        switch (str[pos]) {
        case '\\':
            pos++;
            type[i] = '\\';
            break;
        case 'x':
            pos++;
            if (!str[pos] || !str[pos+1]) {
                error_msg(err, ESCAPE_END, str);
                return 1;
            }
            val = rsrc_hexdecode(str + pos);
            if (val < 0) {
                error_msg(err, HEX_BAD, str + pos);
                return 1;
            }
            type[i] = val;
            pos += 2;
            break;
        case '\0':
            error_msg(err, ESCAPE_END, str);
            return 1;
        default:
            error_msg(err, ESCAPE_INVALID, str[pos]);
            return 1;
        }
    }
    return 0;
}

void
rsrc_format(char *buf, uint8_t const *code)
{
    int i;
    uint8_t c;
    char *optr = buf;
    for (i = 0; i < 4; ++i) {
        c = code[i];
        if (c >= 0x20 && c <= 0x7e)
            *optr++ = c;
        else {
            snprintf(optr, 5, "\\x%02x", c);
            optr += 4;
        }
    }
    *optr = 0;
}

void
rsrc_forkinfo_show(struct forkinfo *ffile)
{
    switch (ffile->format) {
    case FORK_NONE:
        printf("%s: no resource fork found\n",
               ffile->data.path);
        break;
    case FORK_MACBINARY:
        printf("%s: macbinary encoded file\n"
               "    data offset: %u\n"
               "    data length: %u\n"
               "    rsrc offset: %u\n"
               "    rsrc length: %u\n",
               ffile->data.path,
               ffile->data.offset, ffile->data.length,
               ffile->rsrc.offset, ffile->rsrc.length);
        break;
    case FORK_APPLEDOUBLE:
        printf("%s: appledouble encoded file\n"
               "    appledouble path: %s\n"
               "    rsrc offset: %u\n"
               "    rsrc length: %u\n",
               ffile->data.path, ffile->rsrc.path,
               ffile->rsrc.offset, ffile->rsrc.length);
        break;
    case FORK_NATIVE:
        printf("%s: native forked file\n"
               "    resource fork path: %s\n",
               ffile->data.path, ffile->rsrc.path);
        break;
    }
}

int
rsrc_id_parse(int16_t *out, char const *str,
              struct error_handler *err)
{
    char *end;
    long id;
    id = strtol(str, &end, 0);
    if (*end) {
        error_msg(err, "Invalid resource id: %s\n", str);
        return 1;
    }
    if (id < INT16_MIN || id > INT16_MAX) {
        error_msg(err, "Resource id is out of range: %s\n", str);
        return 1;
    }
    *out = id;
    return 0;
}

int
rsrc_idrange_parse(int16_t *out, char const *str,
                   struct error_handler *err)
{
    char *end, *sep;
    long id1, id2, temp, len1;

    sep = strchr(str, ':');
    len1 = sep ? (sep - str) : (long)strlen(str);
    if (sep != str) {
        id1 = strtol(str, &end, 0);
        if (*end && end != sep) {
            error_msg(err, "Invalid resource ID: %*s\n", (int)len1, str);
            return 1;
        }
    } else
        id1 = INT16_MIN;
    if (sep) {
        if (*sep) {
            id2 = strtol(sep + 1, &end, 0);
            if (*end) {
                error_msg(err, "Invalid resource ID: %s\n", sep + 1);
                return 1;
            }
        } else
            id2 = INT16_MAX;
    } else
        id2 = id1;

    if (id2 < id1) {
        warning_msg(err, "Resource ID range is backwards: %s\n", str);
        temp = id1;
        id1 = id2;
        id2 = temp;
    }
    if (id2 < INT16_MIN || id1 > INT16_MAX) {
        error_msg(err, "Resource IDs are out of range: %s\n", str);
        return false;
    }
    if (id1 < INT16_MIN)
        id1 = INT16_MIN;
    if (id2 > INT16_MAX)
        id2 = INT16_MAX;

    out[0] = id1;
    out[1] = id2;
    return 0;
}

bool
rsrc_idrange_check(int16_t (*ranges)[2], uint32_t rangecount,
                   int16_t id)
{
    uint32_t i;
    for (i = 0; i < rangecount; ++i)
        if (ranges[i][0] <= id && id <= ranges[i][1])
            return true;
    return false;
}

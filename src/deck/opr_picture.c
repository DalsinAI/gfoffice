/* Slides as pictures (P9): which writer a name asks for, the file written, and the numbered names of a whole
 * show's pictures. The writers themselves are in ow_picture.c (shared with GFOpenDiag). */
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int ends_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), e = strlen(ext), i;
    if (n <= e) return 0;
    for (i = 0; i < e; i++)
        if (tolower((unsigned char)s[n - e + i]) != tolower((unsigned char)ext[i])) return 0;
    return 1;
}

int opr_picture_format_for(const char *path)
{
    if (path && (ends_ci(path, ".iff") || ends_ci(path, ".ilbm"))) return OPR_PICTURE_ILBM;
    return OPR_PICTURE_PNG;
}

int opr_picture_write(const char *path, int format, const unsigned char *rgb, int width, int height, size_t stride)
{
    size_t n = 0;
    unsigned char *data;
    FILE *f;
    int ok;
    if (!path || !*path) return -1;
    data = format == OPR_PICTURE_ILBM ? opr_ilbm_encode(rgb, width, height, stride, &n)
                                      : opr_png_encode(rgb, width, height, stride, &n);
    if (!data) return -1;
    f = fopen(path, "wb");
    if (!f) { free(data); return -2; }
    ok = fwrite(data, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    free(data);
    return ok ? 0 : -2;
}

/* "Talk.png" and slide 3 of 12 -> "Talk-03.png": the number at least two wide, before the extension. */
int opr_picture_name(char *out, size_t size, const char *path, int index, int count)
{
    const char *dot = NULL, *p;
    int digits = 1, c = count, n;
    if (!out || !size || !path || index < 0) return -1;
    for (p = path; *p; p++) {
        if (*p == '.') dot = p;
        else if (*p == '/' || *p == ':') dot = NULL;
    }
    while (c >= 10) { c /= 10; digits++; }
    if (digits < 2) digits = 2;
    if (dot)
        n = snprintf(out, size, "%.*s-%0*d%s", (int)(dot - path), path, digits, index + 1, dot);
    else
        n = snprintf(out, size, "%s-%0*d", path, digits, index + 1);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

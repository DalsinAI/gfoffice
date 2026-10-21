/*
 * libowf's files and clock over the C library: the PC (where the tests
 * run) and Amiga programs that link libowf in. owf.library has its own over
 * dos.library (library/owf_sys_dos.c).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int owf_sys_read_file(const char *path, unsigned char **data, size_t *length)
{
    FILE *fh = fopen(path, "rb");
    unsigned char *buf;
    long size;

    *data = NULL;
    *length = 0;
    if (!fh)
        return OWF_ERR_IO;
    if (fseek(fh, 0, SEEK_END) != 0 || (size = ftell(fh)) < 0 || fseek(fh, 0, SEEK_SET) != 0) {
        fclose(fh);
        return OWF_ERR_IO;
    }
    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fh);
        return OWF_ERR_MEMORY;
    }
    if (size && fread(buf, 1, (size_t)size, fh) != (size_t)size) {
        free(buf);
        fclose(fh);
        return OWF_ERR_IO;
    }
    fclose(fh);
    buf[size] = 0;
    *data = buf;
    *length = (size_t)size;
    return OWF_OK;
}

int owf_sys_write_file(const char *path, const unsigned char *data, size_t length)
{
    FILE *fh = fopen(path, "wb");
    int result = OWF_OK;

    if (!fh)
        return OWF_ERR_IO;
    if (length && fwrite(data, 1, length, fh) != length)
        result = OWF_ERR_IO;
    if (fclose(fh) != 0)
        result = OWF_ERR_IO;
    return result;
}

int owf_sys_now(owf_time *t)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    if (!tm)
        return 0;
    t->year = tm->tm_year + 1900;
    t->month = tm->tm_mon + 1;
    t->day = tm->tm_mday;
    t->hour = tm->tm_hour;
    t->minute = tm->tm_min;
    t->second = tm->tm_sec;
    return 1;
}

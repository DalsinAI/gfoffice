/*
 * libowf's files and clock over dos.library, for owf.library (which has no
 * C library). The C library's version is ../src/owf_sys.c.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

int owf_sys_read_file(const char *path, unsigned char **data, size_t *length)
{
    BPTR fh;
    LONG size, got;
    unsigned char *buf;

    *data = NULL;
    *length = 0;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh)
        return OWF_ERR_IO;
    /* Seek answers the position it left: the end's, from the second call */
    if (Seek(fh, 0, OFFSET_END) < 0 || (size = Seek(fh, 0, OFFSET_BEGINNING)) < 0) {
        Close(fh);
        return OWF_ERR_IO;
    }
    buf = owf_malloc((size_t)size + 1);
    if (!buf) {
        Close(fh);
        return OWF_ERR_MEMORY;
    }
    got = size ? Read(fh, buf, size) : 0;
    Close(fh);
    if (got != size) {
        owf_free(buf);
        return OWF_ERR_IO;
    }
    buf[size] = 0;
    *data = buf;
    *length = (size_t)size;
    return OWF_OK;
}

int owf_sys_write_file(const char *path, const unsigned char *data, size_t length)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    int result = OWF_OK;

    if (!fh)
        return OWF_ERR_IO;
    if (length && Write(fh, (APTR)data, (LONG)length) != (LONG)length)
        result = OWF_ERR_IO;
    if (!Close(fh))
        result = OWF_ERR_IO;
    return result;
}

/* The Amiga's clock keeps local time: days since 1 January 1978, minutes
 * and ticks (50 a second). */
int owf_sys_now(owf_time *t)
{
    static const unsigned char month_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    struct DateStamp ds;
    LONG days;
    int year = 1978, month = 0;

    DateStamp(&ds);
    days = ds.ds_Days;
    if (days < 0)
        return 0;
    for (;;) {
        int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        int in_year = leap ? 366 : 365;
        if (days < in_year)
            break;
        days -= in_year;
        year++;
    }
    for (;;) {
        int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        int in_month = month_days[month] + (month == 1 && leap);
        if (days < in_month)
            break;
        days -= in_month;
        month++;
    }
    t->year = year;
    t->month = month + 1;
    t->day = (int)days + 1;
    t->hour = (int)(ds.ds_Minute / 60);
    t->minute = (int)(ds.ds_Minute % 60);
    t->second = (int)(ds.ds_Tick / TICKS_PER_SECOND);
    return 1;
}

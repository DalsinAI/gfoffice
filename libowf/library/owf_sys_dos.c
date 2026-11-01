/*
 * libowf's files and clock over dos.library, for owf.library (which has no
 * C library). The C library's version is ../src/owf_sys.c.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

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

/* The length of path's drawer, with its '/' or ':' (0 for a bare name). */
static size_t drawer_length(const char *path)
{
    size_t n = strlen(path);

    while (n && path[n - 1] != '/' && path[n - 1] != ':')
        n--;
    return n;
}

/* 1 when nothing has the name. */
static int name_free(const char *name)
{
    BPTR lock = Lock((CONST_STRPTR)name, SHARED_LOCK);

    if (lock) {
        UnLock(lock);
        return 0;
    }
    return IoErr() == ERROR_OBJECT_NOT_FOUND;
}

/* As owf_sys.c's: a save that fails leaves the file as it was. The bytes go
 * to a short name in the same drawer (owf, the task's address in eight hex
 * digits, two more and .tmp: 17 characters, for a 30-character file
 * system); only when all of it is written and closed does the old file
 * step aside under a name of the same kind (.bak), the new one take its
 * name (Rename doesn't replace a file) and the old one go. If the new one
 * can't take the name, the old one gets it back. A file is replaced only
 * when it could have been written over, as before (not a drawer, not
 * protected from writing or deleting), and the new one keeps its comment
 * and protection bits. */
int owf_sys_write_file(const char *path, const unsigned char *data, size_t length)
{
    static UBYTE counter;
    size_t dir = drawer_length(path);
    struct FileInfoBlock *fib = NULL;
    char *temp, *backup;
    unsigned long id = (unsigned long)FindTask(NULL);
    BPTR fh, lock;
    int result = OWF_ERR_IO, tries, ok;

    if ((lock = Lock((CONST_STRPTR)path, SHARED_LOCK))) {
        fib = AllocDosObject(DOS_FIB, NULL);
        ok = fib && Examine(lock, fib) && fib->fib_DirEntryType < 0 &&
             !(fib->fib_Protection & (FIBF_WRITE | FIBF_DELETE));
        UnLock(lock);
        if (!fib)
            return OWF_ERR_MEMORY;
        if (!ok) {
            FreeDosObject(DOS_FIB, fib);
            return OWF_ERR_IO;
        }
    } else if (IoErr() != ERROR_OBJECT_NOT_FOUND) {
        return OWF_ERR_IO;
    }
    if (!(temp = owf_malloc(2 * (dir + 18)))) {
        if (fib)
            FreeDosObject(DOS_FIB, fib);
        return OWF_ERR_MEMORY;
    }
    backup = temp + dir + 18;
    memcpy(temp, path, dir);
    memcpy(backup, path, dir);
    for (tries = 0; tries < 256; tries++) {
        unsigned n = counter++;
        snprintf(temp + dir, 18, "owf%08lx%02x.tmp", id, n);
        snprintf(backup + dir, 18, "owf%08lx%02x.bak", id, n);
        if (name_free(temp) && name_free(backup))
            break;
    }
    if (tries == 256 || !(fh = Open((CONST_STRPTR)temp, MODE_NEWFILE)))
        goto done;
    ok = !length || Write(fh, (APTR)data, (LONG)length) == (LONG)length;
    if (!Close(fh))
        ok = 0;
    if (!ok) {
        DeleteFile((CONST_STRPTR)temp);
    } else if (!fib) {
        if (Rename((CONST_STRPTR)temp, (CONST_STRPTR)path))
            result = OWF_OK;
        else
            DeleteFile((CONST_STRPTR)temp);
    } else {
        /* a new file is not backed up yet: its archive bit stays clear */
        SetProtection((CONST_STRPTR)temp, fib->fib_Protection & ~FIBF_ARCHIVE);
        if (fib->fib_Comment[0])
            SetComment((CONST_STRPTR)temp, (CONST_STRPTR)fib->fib_Comment);
        if (!Rename((CONST_STRPTR)path, (CONST_STRPTR)backup)) {
            DeleteFile((CONST_STRPTR)temp);
        } else if (!Rename((CONST_STRPTR)temp, (CONST_STRPTR)path)) {
            Rename((CONST_STRPTR)backup, (CONST_STRPTR)path);
            DeleteFile((CONST_STRPTR)temp);
        } else {
            DeleteFile((CONST_STRPTR)backup);
            result = OWF_OK;
        }
    }
done:
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    owf_free(temp);
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

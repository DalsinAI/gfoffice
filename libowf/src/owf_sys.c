/*
 * libowf's files and clock over the C library: the PC (where the tests
 * run) and Amiga programs that link libowf in. owf.library has its own over
 * dos.library (library/owf_sys_dos.c).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* The length of path's drawer, with its '/' or ':' (0 for a bare name). */
static size_t drawer_length(const char *path)
{
    size_t n = strlen(path);

    while (n && path[n - 1] != '/' && path[n - 1] != ':')
        n--;
    return n;
}

static int file_exists(const char *path)
{
    FILE *fh = fopen(path, "rb");

    if (!fh)
        return 0;
    fclose(fh);
    return 1;
}

/* A save that fails must leave the file as it was, so the bytes go to a
 * second file first: a short name in the same drawer (owf, eight hex
 * digits, two more and .tmp, 17 characters, so it fits a 30-character file
 * system). Only when all of it is written and closed does the old file step
 * aside under a name of the same kind (.bak), the new one take its name and
 * the old one go. AmigaDOS's Rename doesn't replace a file (an Amiga C
 * library's rename may not), hence the step aside; if the new one can't
 * take the name, the old one gets it back. The eight digits are where this
 * call's stack is, which no other running task shares. A file is replaced
 * only when it could have been written over, as before: not a drawer, not
 * one that is protected. */
int owf_sys_write_file(const char *path, const unsigned char *data, size_t length)
{
    static unsigned char counter;
    size_t dir = drawer_length(path);
    char *temp, *backup;
    unsigned long id = (unsigned long)(size_t)&dir & 0xffffffffUL;
    FILE *fh;
    int result = OWF_ERR_IO, tries, ok, replace = 0;

    if ((fh = fopen(path, "r+b"))) {
        fclose(fh);
        replace = 1;
    } else if (file_exists(path)) {
        return OWF_ERR_IO;
    }
    if (!(temp = malloc(2 * (dir + 18))))
        return OWF_ERR_MEMORY;
    backup = temp + dir + 18;
    memcpy(temp, path, dir);
    memcpy(backup, path, dir);
    for (tries = 0; tries < 256; tries++) {
        unsigned n = counter++;
        snprintf(temp + dir, 18, "owf%08lx%02x.tmp", id, n);
        snprintf(backup + dir, 18, "owf%08lx%02x.bak", id, n);
        if (!file_exists(temp) && !file_exists(backup))
            break;
    }
    if (tries == 256 || !(fh = fopen(temp, "wb")))
        goto done;
    ok = !length || fwrite(data, 1, length, fh) == length;
    if (fclose(fh) != 0)
        ok = 0;
    if (!ok) {
        remove(temp);
    } else if (!replace) {
        if (rename(temp, path) == 0)
            result = OWF_OK;
        else
            remove(temp);
    } else if (rename(path, backup) != 0) {
        remove(temp);
    } else if (rename(temp, path) != 0) {
        rename(backup, path);
        remove(temp);
    } else {
        remove(backup);
        result = OWF_OK;
    }
done:
    free(temp);
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

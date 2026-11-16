/*
 * GFOpenCalc's portable core: links to other workbooks (Edit Links). The
 * book keeps each other book its formulas use, with the values that book
 * last had (owf_book.extbooks); formulas work from those values. Updating
 * a link reads its file again and takes the values its sheets have now; a
 * file that is not there leaves the last values as they were.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <string.h>

int oc_core_link_count(const oc_core *core)
{
    return core && core->book ? core->book->nextbooks : 0;
}

int oc_core_link(const oc_core *core, int index, const char **name, const char **target, int *sheets)
{
    const owf_extbook *eb;
    if (index < 0 || index >= oc_core_link_count(core))
        return OWF_ERR_UNSUPPORTED;
    eb = &core->book->extbooks[index];
    if (name)
        *name = eb->name;
    if (target)
        *target = eb->target ? eb->target : eb->name;
    if (sheets)
        *sheets = eb->nsheets;
    return OWF_OK;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

static int hex(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* s, "%20" and the like decoded, added at out[*n]; 0 when it does not fit. */
static int put_decoded(char *out, size_t size, size_t *n, const char *s)
{
    for (; *s; s++) {
        char c = *s;
        if (c == '%' && hex(s[1]) >= 0 && hex(s[2]) >= 0) {
            c = (char)(hex(s[1]) * 16 + hex(s[2]));
            if (!c)
                return 0;                /* "%00" would cut the path short */
            s += 2;
        }
        if (*n + 1 >= size)
            return 0;
        out[(*n)++] = c;
    }
    out[*n] = 0;
    return 1;
}

int oc_link_path(const char *target, const char *base, char *out, size_t size)
{
    size_t n = 0;
    const char *p = target;
    if (!target || !*target || !size)
        return 0;
    *out = 0;
    if (!strncmp(p, "file://", 7)) {
        p += 7;
        if (!strncmp(p, "localhost/", 10))
            p += 9;
        if (*p != '/')
            return 0;                    /* "file://server/share/x.xlsx": another computer's file */
        /* "file:///C:/Books/x.xlsx": a drive's path; "file:///home/x.ods": from the root */
        if (p[0] == '/' && ((p[1] >= 'A' && p[1] <= 'Z') || (p[1] >= 'a' && p[1] <= 'z')) && p[2] == ':')
            p++;
        return put_decoded(out, size, &n, p);
    }
    {
        const char *q;
        for (q = p; *q && *q != '/' && *q != ':'; q++)
            ;
        if (*q == ':' && q[1] == '/' && q[2] == '/' && q - p > 1)
            return 0;                    /* "http://...": not a file */
    }
    if (*p == '/' || *p == '\\' || strchr(p, ':') || !base || !*base)
        return put_decoded(out, size, &n, p);   /* "/home/x.ods", "Work:Books/x.ods", "C:\\x.xlsx" */
    n = strlen(base);                           /* beside the book: "Work:Books" + "x.ods" */
    if (n + 2 > size)
        return 0;
    memcpy(out, base, n);
    if (out[n - 1] != '/' && out[n - 1] != ':' && out[n - 1] != '\\')
        out[n++] = '/';
    out[n] = 0;
    return put_decoded(out, size, &n, p);
}

/* A sheet's values now, in place of the last ones: formulas give the values they had, styles and empty
 * cells go. 0 when memory ran out (the sheet keeps the values it had). */
static int take_values(owf_sheet *to, const owf_sheet *from)
{
    owf_cell *cells = NULL;
    int n = 0, i;
    if (from->ncells && !(cells = owf_calloc((size_t)from->ncells, sizeof *cells)))
        return 0;
    for (i = 0; i < from->ncells; i++) {
        const owf_cell *c = &from->cells[i];
        owf_cell *d;
        if (c->type == OWF_VALUE_EMPTY)
            continue;
        d = &cells[n];
        d->row = c->row;
        d->col = c->col;
        d->type = c->type;
        d->number = c->number;
        d->error = c->error;
        if (c->type == OWF_VALUE_TEXT && !(d->text = oc_strdup(c->text ? c->text : ""))) {
            while (n--)
                oc_free_cell(&cells[n]);
            owf_free(cells);
            return 0;
        }
        n++;
    }
    for (i = 0; i < to->ncells; i++)
        oc_free_cell(&to->cells[i]);
    owf_free(to->cells);
    to->cells = cells;
    to->ncells = to->capcells = n;
    return 1;
}

int oc_core_update_link(oc_core *core, int index, const char *path, owf_report *report, int *sheets)
{
    owf_book *src = NULL;
    owf_extbook *eb;
    oc_undo *undo;
    int rc, k, found = 0;
    if (sheets)
        *sheets = 0;
    if (index < 0 || index >= oc_core_link_count(core) || !path || !*path)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    rc = owf_import_book_file(path, NULL, &src, report, NULL);
    if (rc != OWF_OK)
        return rc;                       /* not there, or not a book: the last values stay */
    undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    eb = &core->book->extbooks[index];
    for (k = 0; k < eb->nsheets; k++) {
        int s;
        for (s = 0; s < src->nsheets && !same_name(src->sheets[s].name, eb->sheets[k].name); s++)
            ;
        if (s == src->nsheets)
            continue;                    /* a sheet the file no longer has: its last values stay */
        if (!take_values(&eb->sheets[k], &src->sheets[s])) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        found++;
    }
    owf_book_free(src);
    if (!found) {
        oc_undo_free(undo);
        return rc;
    }
    oc_undo_push(core, undo);
    oc_changed(core);
    if (sheets)
        *sheets = found;
    {
        int r = owf_calc_recalc_all(core->calc);
        return rc != OWF_OK ? rc : r;
    }
}

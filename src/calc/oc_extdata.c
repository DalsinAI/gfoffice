/*
 * GFOpenCalc's portable core: Data > External Data. A range of a sheet is
 * filled from a web page's table (oc_html.c reads the page) or a database's
 * table, view or query (the program's oc_extdata_io reads it: opencalc_extdb.h
 * for SQLite and GFOpenBase's .oadb), and kept with where it came from, so it
 * can be refreshed by hand, as the book opens, or on the program's clock.
 *
 * Each is kept on its sheet in hidden names, as Consolidate keeps its
 * settings: _GF_EXTDATA_n is the range as a reference ('Sheet1'!$B$2:$D$9),
 * which follows rows and columns put in or taken out as any name does, and
 * _GF_EXTDATA_n_D1, _D2... hold its settings as text in pieces of at most 200
 * characters (Excel's formulas take strings of 255 at most). XLSX and ODS keep
 * them with the book's other names.
 *
 * The work is done on a private copy of the book and swapped in as one Undo
 * step (none for a quiet refresh). A refresh writes the new values from the
 * range's top left cell: the range grows or shrinks to them, and what it no
 * longer covers is emptied. It does not write over cells beside it that hold
 * something unless told to (OC_EXTDATA_OVERWRITE); styles stay.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXT_PREFIX "_GF_EXTDATA_"
#define EXT_PIECE 200                    /* the settings' characters in one name */
#define EXT_PIECES 64                    /* names for one's settings, at most */

typedef struct {
    int sheet;                           /* the sheet its names are on */
    unsigned id;                         /* n of _GF_EXTDATA_n */
} ext_entry;

void oc_extdata_init(oc_extdata_spec *spec)
{
    memset(spec, 0, sizeof *spec);
    spec->kind = OC_EXTDATA_WEB;
    spec->headers = 1;
    memcpy(spec->item, "HTML_1", 7);
}

/* ---- the names ---- */

/* n when name is _GF_EXTDATA_n (n 1..), else 0. */
static unsigned range_id(const char *name)
{
    const char *p;
    unsigned long n = 0;
    if (!name || strncmp(name, EXT_PREFIX, sizeof EXT_PREFIX - 1))
        return 0;
    p = name + sizeof EXT_PREFIX - 1;
    if (!*p)
        return 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9' || (n = n * 10 + (unsigned long)(*p - '0')) > 999999UL)
            return 0;
    }
    return (unsigned)n;
}

static int entry_order(const void *a, const void *b)
{
    const ext_entry *x = a, *y = b;
    if (x->sheet != y->sheet)
        return x->sheet < y->sheet ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

/* The book's ranges, sheet by sheet, each sheet's by n: how many (at most OC_EXTDATA_MAX). */
static int entries(const owf_book *book, ext_entry *out)
{
    int i, n = 0;
    for (i = 0; i < book->nnames && n < OC_EXTDATA_MAX; i++) {
        unsigned id = range_id(book->names[i].name);
        if (id && book->names[i].sheet >= 0 && book->names[i].sheet < book->nsheets) {
            out[n].sheet = book->names[i].sheet;
            out[n].id = id;
            n++;
        }
    }
    qsort(out, (size_t)n, sizeof *out, entry_order);
    return n;
}

static int find_name(const owf_book *book, int sheet, const char *name)
{
    int k;
    for (k = 0; k < book->nnames; k++)
        if (book->names[k].sheet == sheet && book->names[k].name && !strcmp(book->names[k].name, name))
            return k;
    return -1;
}

/* The sheet's hidden name set to formula (taken), or added. */
static int set_hidden(owf_book *book, int sheet, const char *name, char *formula)
{
    int k;
    if (!formula)
        return OWF_ERR_MEMORY;
    if ((k = find_name(book, sheet, name)) >= 0) {
        owf_free(book->names[k].formula);
        book->names[k].formula = formula;
        book->names[k].hidden = 1;
        return OWF_OK;
    }
    k = owf_book_add_name(book, name, sheet, formula, 1);
    owf_free(formula);
    return k < 0 ? OWF_ERR_MEMORY : OWF_OK;
}

static void drop_name(owf_book *book, int k)
{
    owf_free(book->names[k].name);
    owf_free(book->names[k].formula);
    memmove(&book->names[k], &book->names[k + 1], (size_t)(book->nnames - k - 1) * sizeof book->names[0]);
    book->nnames--;
}

/* Every name of range id on sheet: the range's and its settings'. */
static void drop_names(owf_book *book, int sheet, unsigned id)
{
    char name[48];
    int k, piece;
    snprintf(name, sizeof name, EXT_PREFIX "%u", id);
    if ((k = find_name(book, sheet, name)) >= 0)
        drop_name(book, k);
    for (piece = 1; piece <= EXT_PIECES; piece++) {
        snprintf(name, sizeof name, EXT_PREFIX "%u_D%d", id, piece);
        if ((k = find_name(book, sheet, name)) < 0)
            break;
        drop_name(book, k);
    }
}

/* ---- the settings as text ---- */

/* s with "%", '"', ";" and control characters as %XX, added at out[*n]; 0 when it does not fit. */
static int put_encoded(char *out, size_t size, size_t *n, const char *s)
{
    static const char hexd[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '%' || c == '"' || c == ';' || c < 0x20) {
            if (*n + 4 >= size)
                return 0;
            out[(*n)++] = '%';
            out[(*n)++] = hexd[c >> 4];
            out[(*n)++] = hexd[c & 15];
        } else {
            if (*n + 2 >= size)
                return 0;
            out[(*n)++] = (char)c;
        }
    }
    out[*n] = 0;
    return 1;
}

static int hexval(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* A field's text from p (up to ";" or the end) decoded into out; the position after it. */
static const char *get_decoded(const char *p, char *out, size_t size)
{
    size_t n = 0;
    while (*p && *p != ';') {
        char c = *p++;
        if (c == '%' && hexval(p[0]) >= 0 && hexval(p[1]) >= 0) {
            c = (char)(hexval(p[0]) * 16 + hexval(p[1]));
            p += 2;
        }
        if (out && n + 1 < size && c)
            out[n++] = c;
    }
    if (out && size)
        out[n] = 0;
    return p;
}

static int settings_text(const oc_extdata_spec *spec, char *out, size_t size)
{
    size_t n;
    int k = snprintf(out, size, "1;k=%d;h=%d;o=%d;t=%u;d=", spec->kind, spec->headers ? 1 : 0,
                     spec->refresh_on_open ? 1 : 0, spec->refresh_seconds);
    if (k < 0 || (size_t)k >= size)
        return 0;
    n = (size_t)k;
    if (!put_encoded(out, size, &n, spec->dest) || n + 4 >= size)
        return 0;
    memcpy(out + n, ";s=", 4);
    n += 3;
    if (!put_encoded(out, size, &n, spec->source) || n + 4 >= size)
        return 0;
    memcpy(out + n, ";i=", 4);
    n += 3;
    return put_encoded(out, size, &n, spec->item);
}

#define SETTINGS_MAX (16 + 3 * (OC_EXTDATA_SOURCE + OC_EXTDATA_ITEM + OC_EXTDATA_REF) + 64)

/* The settings into the sheet's names, in pieces (those left from longer settings dropped). */
static int put_settings(owf_book *book, int sheet, unsigned id, const oc_extdata_spec *spec)
{
    char *text = owf_malloc(SETTINGS_MAX), name[48];
    size_t len, at;
    int piece = 0, k, rc = OWF_OK;
    if (!text)
        return OWF_ERR_MEMORY;
    if (!settings_text(spec, text, SETTINGS_MAX)) {
        owf_free(text);
        return OWF_ERR_UNSUPPORTED;
    }
    len = strlen(text);
    for (at = 0; at < len && rc == OWF_OK; at += EXT_PIECE) {
        size_t take = len - at < EXT_PIECE ? len - at : EXT_PIECE;
        char *f = owf_malloc(take + 3);
        if (!f) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        f[0] = '"';
        memcpy(f + 1, text + at, take);
        f[take + 1] = '"';
        f[take + 2] = 0;
        snprintf(name, sizeof name, EXT_PREFIX "%u_D%d", id, ++piece);
        rc = set_hidden(book, sheet, name, f);
    }
    owf_free(text);
    for (piece++; rc == OWF_OK && piece <= EXT_PIECES; piece++) {
        snprintf(name, sizeof name, EXT_PREFIX "%u_D%d", id, piece);
        if ((k = find_name(book, sheet, name)) < 0)
            break;
        drop_name(book, k);
    }
    return rc;
}

/* The settings kept for range id on sheet (dest as kept): 0 when they do not read. */
static int get_settings(const owf_book *book, int sheet, unsigned id, oc_extdata_spec *spec)
{
    char *text = owf_malloc(SETTINGS_MAX), name[48];
    size_t n = 0;
    int piece, ok = 0;
    const char *p;
    if (!text)
        return 0;
    for (piece = 1; piece <= EXT_PIECES; piece++) {
        int k;
        const char *f;
        size_t len;
        snprintf(name, sizeof name, EXT_PREFIX "%u_D%d", id, piece);
        if ((k = find_name(book, sheet, name)) < 0 || !(f = book->names[k].formula))
            break;
        while (*f == '=' || *f == ' ')
            f++;
        if (*f++ != '"')
            break;
        len = strlen(f);
        if (len && f[len - 1] == '"')
            len--;
        if (n + len + 1 > SETTINGS_MAX)
            break;
        memcpy(text + n, f, len);
        n += len;
    }
    text[n] = 0;
    memset(spec, 0, sizeof *spec);
    if (text[0] == '1' && (text[1] == ';' || !text[1])) {
        ok = 1;
        for (p = text + 1; *p;) {
            if (*p == ';') {
                p++;
                continue;
            }
            if (!strncmp(p, "k=", 2)) spec->kind = atoi(p + 2) == OC_EXTDATA_DATABASE ? OC_EXTDATA_DATABASE : OC_EXTDATA_WEB;
            else if (!strncmp(p, "h=", 2)) spec->headers = atoi(p + 2) != 0;
            else if (!strncmp(p, "o=", 2)) spec->refresh_on_open = atoi(p + 2) != 0;
            else if (!strncmp(p, "t=", 2)) spec->refresh_seconds = (unsigned)strtoul(p + 2, NULL, 10);
            if (!strncmp(p, "d=", 2)) p = get_decoded(p + 2, spec->dest, sizeof spec->dest);
            else if (!strncmp(p, "s=", 2)) p = get_decoded(p + 2, spec->source, sizeof spec->source);
            else if (!strncmp(p, "i=", 2)) p = get_decoded(p + 2, spec->item, sizeof spec->item);
            else p = get_decoded(p, NULL, 0);           /* one a later program wrote: passed over */
        }
    }
    owf_free(text);
    return ok;
}

/* ---- references ---- */

/* 'Sheet'!$B$2:$D$9 (or 'Sheet'!$B$2), into out. */
static int ref_text(const owf_book *book, int sheet, const owf_range *r, int with_dollars, char *out, size_t size)
{
    const char *name = book->sheets[sheet].name;
    char a[16], b[16];
    size_t at = 0, i;
    int n;
    if (size < 8)
        return 0;
    out[at++] = '\'';
    for (i = 0; name[i]; i++) {
        if (at + 4 >= size)
            return 0;
        if (name[i] == '\'')
            out[at++] = '\'';
        out[at++] = name[i];
    }
    out[at++] = '\'';
    out[at++] = '!';
    oc_col_name(r->first_col, a);
    oc_col_name(r->last_col, b);
    if (r->first_row == r->last_row && r->first_col == r->last_col)
        n = with_dollars ? snprintf(out + at, size - at, "$%s$%u", a, r->first_row + 1u)
                         : snprintf(out + at, size - at, "%s%u", a, r->first_row + 1u);
    else
        n = with_dollars ? snprintf(out + at, size - at, "$%s$%u:$%s$%u", a, r->first_row + 1u, b, r->last_row + 1u)
                         : snprintf(out + at, size - at, "%s%u:%s%u", a, r->first_row + 1u, b, r->last_row + 1u);
    return n > 0 && (size_t)n < size - at;
}

/* Range id's range now (its name's reference): 0 when it no longer reads (its cells were taken out). */
static int range_now(const owf_book *book, int sheet, unsigned id, int *s2, owf_range *r)
{
    char name[48];
    int k;
    snprintf(name, sizeof name, EXT_PREFIX "%u", id);
    if ((k = find_name(book, sheet, name)) < 0 || !book->names[k].formula)
        return 0;
    return oc_sheet_range(book, book->names[k].formula, sheet, s2, r) && *s2 >= 0 && *s2 < book->nsheets;
}

/* One's settings, sheet and range as they are now (dest its range's top left cell, on its sheet). */
static int describe(const owf_book *book, const ext_entry *e, int active, oc_extdata_spec *spec, int *sheet,
                    owf_range *range)
{
    owf_range r;
    int s2 = e->sheet;
    if (!get_settings(book, e->sheet, e->id, spec))
        return 0;
    if (range_now(book, e->sheet, e->id, &s2, &r)) {
        owf_range first = r;
        first.last_row = r.first_row;
        first.last_col = r.first_col;
        if (s2 == active) {
            char a[16];
            oc_cell_name(r.first_row, r.first_col, a);
            snprintf(spec->dest, sizeof spec->dest, "%s", a);
        } else {
            ref_text(book, s2, &first, 0, spec->dest, sizeof spec->dest);
        }
    } else {
        int s3;
        if (!oc_sheet_range(book, spec->dest, e->sheet, &s3, &r) || s3 < 0 || s3 >= book->nsheets)
            return 0;
        s2 = s3;
        r.last_row = r.first_row;
        r.last_col = r.first_col;
    }
    if (sheet)
        *sheet = s2;
    if (range)
        *range = r;
    return 1;
}

int oc_core_extdata_count(const oc_core *core)
{
    ext_entry e[OC_EXTDATA_MAX];
    return core && core->book ? entries(core->book, e) : 0;
}

int oc_core_extdata(const oc_core *core, int index, oc_extdata_spec *spec, int *sheet, owf_range *range)
{
    ext_entry e[OC_EXTDATA_MAX];
    oc_extdata_spec dummy;
    int n;
    if (!core || !core->book)
        return OWF_ERR_UNSUPPORTED;
    n = entries(core->book, e);
    if (index < 0 || index >= n)
        return OWF_ERR_UNSUPPORTED;
    return describe(core->book, &e[index], core->sheet, spec ? spec : &dummy, sheet, range) ? OWF_OK
                                                                                            : OWF_ERR_FORMAT;
}

int oc_core_extdata_at(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    ext_entry e[OC_EXTDATA_MAX];
    int n, i;
    if (!core || !core->book)
        return -1;
    n = entries(core->book, e);
    for (i = 0; i < n; i++) {
        int s2;
        owf_range r;
        if (range_now(core->book, e[i].sheet, e[i].id, &s2, &r) && s2 == sheet && row >= r.first_row &&
            row <= r.last_row && col >= r.first_col && col <= r.last_col)
            return i;
    }
    return -1;
}

/* ---- reading a source ---- */

static void say(char *err, size_t errsize, const char *what, const char *detail)
{
    if (err && errsize && !err[0])
        snprintf(err, errsize, what, detail ? detail : "");
}

/* "http://x", "https://x": a scheme other than file: (a drive letter "C:" is none). */
static int is_url(const char *s)
{
    const char *p = s;
    while (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.')
        p++;
    return *p == ':' && p[1] == '/' && p[2] == '/' && p - s > 1 && strncmp(s, "file:", 5);
}

int oc_extdata_fetch_file(void *user, const char *source, char **data, size_t *length, char *err, size_t errsize)
{
    FILE *f;
    long size;
    char *buf;
    (void)user;
    *data = NULL;
    *length = 0;
    if (!source || !*source || is_url(source)) {
        say(err, errsize, "Only files can be read here: %s", source);
        return OWF_ERR_UNSUPPORTED;
    }
    if (!(f = fopen(source, "rb"))) {
        say(err, errsize, "Could not open %s", source);
        return OWF_ERR_IO;
    }
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        say(err, errsize, "Could not read %s", source);
        return OWF_ERR_IO;
    }
    if (size > 64L * 1024 * 1024) {
        fclose(f);
        say(err, errsize, "%s is too big to read (over 64 MB)", source);
        return OWF_ERR_UNSUPPORTED;
    }
    if (!(buf = owf_malloc((size_t)size + 1))) {
        fclose(f);
        return OWF_ERR_MEMORY;
    }
    if (size && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        owf_free(buf);
        say(err, errsize, "Could not read %s", source);
        return OWF_ERR_IO;
    }
    fclose(f);
    buf[size] = 0;
    *data = buf;
    *length = (size_t)size;
    return OWF_OK;
}

/* The source's values into t. */
static int read_source(const oc_extdata_spec *spec, const oc_extdata_io *io, oc_exttable *t, char *err,
                       size_t errsize)
{
    char path[OC_EXTDATA_SOURCE + 512];
    const char *where = spec->source;
    int rc;
    t->rows = t->cols = 0;
    t->cells = NULL;
    if (!spec->source[0]) {
        say(err, errsize, "No source was given%s", NULL);
        return OWF_ERR_FORMAT;
    }
    if (!is_url(spec->source)) {
        if (!oc_link_path(spec->source, io ? io->base : NULL, path, sizeof path)) {
            say(err, errsize, "%s is not a file this computer can read", spec->source);
            return OWF_ERR_UNSUPPORTED;
        }
        where = path;
    }
    if (spec->kind == OC_EXTDATA_DATABASE) {
        if (!io || !io->query) {
            say(err, errsize, "Databases cannot be read here%s", NULL);
            return OWF_ERR_UNSUPPORTED;
        }
        if (!spec->item[0]) {
            say(err, errsize, "No table or query was given%s", NULL);
            return OWF_ERR_FORMAT;
        }
        rc = io->query(io->user, where, spec->item, spec->headers, t, err, errsize);
        if (rc != OWF_OK) {
            oc_exttable_free(t);
            say(err, errsize, "Could not read %s", spec->item);
        }
        return rc;
    }
    {
        char *page = NULL;
        size_t length = 0;
        int (*fetch)(void *, const char *, char **, size_t *, char *, size_t) =
            io && io->fetch ? io->fetch : oc_extdata_fetch_file;
        rc = fetch(io ? io->user : NULL, where, &page, &length, err, errsize);
        if (rc != OWF_OK) {
            owf_free(page);
            say(err, errsize, "Could not read %s", spec->source);
            return rc;
        }
        rc = oc_web_table(page ? page : "", page ? length : 0, spec->item[0] ? spec->item : "1", t);
        owf_free(page);
        if (rc == OWF_ERR_FORMAT)
            say(err, errsize, "The page has no table \"%s\"", spec->item[0] ? spec->item : "1");
        else if (rc == OWF_ERR_UNSUPPORTED)
            say(err, errsize, "The table \"%s\" is too big for a sheet", spec->item);
        return rc;
    }
}

/* ---- putting the values in ---- */

static int inside(const owf_range *r, unsigned row, unsigned col)
{
    return row >= r->first_row && row <= r->last_row && col >= r->first_col && col <= r->last_col;
}

/* A value as one would type it (*in; NULL for an empty one), in buf or, when it is longer, in *big
 * (owf_malloc'd). OWF_ERR_MEMORY when that could not be had. */
static int value_input(const oc_extvalue *v, char *buf, size_t size, char **big, const char **in)
{
    const char *text;
    size_t len;
    *big = NULL;
    *in = NULL;
    switch (v->kind) {
    case OC_EXTV_NUMBER:
        snprintf(buf, size, "%.17g", v->number);
        *in = buf;
        break;
    case OC_EXTV_TEXT:
    case OC_EXTV_INPUT:
        text = v->text ? v->text : "";
        if (!*text)
            break;
        if (v->kind == OC_EXTV_INPUT && text[0] != '=' && text[0] != '\'') {
            *in = text;
            break;
        }
        len = strlen(text);
        if (len + 2 > size) {
            if (!(*big = owf_malloc(len + 2)))
                return OWF_ERR_MEMORY;
            buf = *big;
        }
        buf[0] = '\'';                  /* text as it is */
        memcpy(buf + 1, text, len + 1);
        *in = buf;
        break;
    }
    return OWF_OK;
}

/* Swaps the work book in: one Undo step unless quiet; the range selected when it is on the sheet showing. */
static void commit(oc_core *core, owf_book *work, owf_calc *calc, int quiet, int sheet, const owf_range *select)
{
    owf_book *old_book = core->book;
    owf_calc *old_calc = core->calc;
    oc_undo *undo = NULL;
    int dirty = core->dirty;
    if (!quiet) {
        undo = oc_undo_new(core, OC_UNDO_BOOK);
        if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
            oc_undo_free(undo);
            undo = NULL;
        }
    }
    oc_cf_forget(core);
    oc_charts_forget(core);
    core->book = work;
    core->calc = calc;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    if (undo)
        oc_undo_push(core, undo);
    if (quiet)
        oc_track_hint(core, OC_HINT_QUIET, 0, 0, 0, 0, NULL);
    oc_changed(core);
    if (quiet)
        core->dirty = dirty;             /* refreshed by itself: nothing for the user to save */
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    if (!quiet && select && sheet == core->sheet)
        oc_core_select_range(core, select);
}

/* Puts t at the top left of dest on sheet, emptying old (on old_sheet; old_sheet -1: none) first, and keeps
 * the range and spec as range id (its names moved from names_sheet, -1: none). */
static int put_values(oc_core *core, const oc_extdata_spec *spec, const oc_exttable *t, int sheet,
                      unsigned row, unsigned col, int old_sheet, const owf_range *old, int names_sheet, unsigned id,
                      int flags)
{
    owf_book *work = NULL;
    owf_calc *calc = NULL;
    owf_range ext;
    unsigned rows = t->rows ? t->rows : 1, cols = t->cols ? t->cols : 1, r, c;
    char buf[512], ref[OC_EXTDATA_REF], name[48];
    unsigned *clear = NULL;
    int nclear = 0, i, rc = OWF_OK;
    oc_extdata_spec kept = *spec;
    const owf_sheet *sh;

    if (rows > OC_MAX_ROWS - row)
        rows = OC_MAX_ROWS - row;
    if (cols > OC_MAX_COLS - col)
        cols = OC_MAX_COLS - col;
    ext.first_row = row;
    ext.first_col = col;
    ext.last_row = row + rows - 1;
    ext.last_col = col + cols - 1;

    if (oc_range_locked(core, sheet, &ext) || (old_sheet >= 0 && oc_range_locked(core, old_sheet, old)))
        return OC_PROTECTED;
    sh = &core->book->sheets[sheet];
    if (!(flags & OC_EXTDATA_OVERWRITE))
        for (i = 0; i < sh->ncells; i++) {
            const owf_cell *cell = &sh->cells[i];
            if (inside(&ext, cell->row, cell->col) && !(old_sheet == sheet && inside(old, cell->row, cell->col)) &&
                !oc_cell_empty(cell))
                return OC_EXTDATA_BLOCKED;
        }

    if (!(work = oc_book_copy(core->book)) || !(calc = owf_calc_new(work))) {
        rc = OWF_ERR_MEMORY;
        goto fail;
    }
    /* empty what the range covered and what it will cover (its values, not its styles) */
    {
        int pass;
        for (pass = 0; pass < 2; pass++) {
            int s = pass ? old_sheet : sheet;
            const owf_range *within = pass ? old : &ext;
            const owf_sheet *ws;
            if (s < 0)
                continue;
            ws = &work->sheets[s];
            owf_free(clear);
            clear = NULL;
            if (ws->ncells && !(clear = owf_malloc((size_t)ws->ncells * 2 * sizeof *clear))) {
                rc = OWF_ERR_MEMORY;
                goto fail;
            }
            nclear = 0;
            for (i = 0; i < ws->ncells; i++)
                if (inside(within, ws->cells[i].row, ws->cells[i].col) && !oc_cell_empty(&ws->cells[i])) {
                    clear[nclear * 2] = ws->cells[i].row;
                    clear[nclear * 2 + 1] = ws->cells[i].col;
                    nclear++;
                }
            for (i = 0; i < nclear; i++)
                if ((rc = owf_calc_enter(calc, s, clear[i * 2], clear[i * 2 + 1], "")) != OWF_OK)
                    goto fail;
        }
    }
    for (r = 0; r < rows && r < t->rows; r++)
        for (c = 0; c < cols && c < t->cols; c++) {
            char *big;
            const char *in;
            if ((rc = value_input(&t->cells[(size_t)r * t->cols + c], buf, sizeof buf, &big, &in)) != OWF_OK)
                goto fail;
            if (!in)
                continue;
            rc = owf_calc_enter(calc, sheet, row + r, col + c, in);
            owf_free(big);
            if (rc == OWF_ERR_FORMAT)
                rc = OWF_OK;             /* text kept as it came */
            if (rc != OWF_OK)
                goto fail;
        }
    /* the names: the range, as a reference that follows, and the settings (dest the top left cell) */
    if (names_sheet >= 0 && names_sheet != sheet)
        drop_names(work, names_sheet, id);
    {
        owf_range first = ext;
        first.last_row = first.first_row;
        first.last_col = first.first_col;
        if (!ref_text(work, sheet, &first, 0, kept.dest, sizeof kept.dest) ||
            !ref_text(work, sheet, &ext, 1, ref, sizeof ref)) {
            rc = OWF_ERR_UNSUPPORTED;
            goto fail;
        }
    }
    snprintf(name, sizeof name, EXT_PREFIX "%u", id);
    if ((rc = set_hidden(work, sheet, name, oc_strdup(ref))) != OWF_OK ||
        (rc = put_settings(work, sheet, id, &kept)) != OWF_OK)
        goto fail;
    if ((rc = owf_calc_recalc(calc)) != OWF_OK)
        goto fail;
    owf_free(clear);
    commit(core, work, calc, (flags & OC_EXTDATA_QUIET) != 0, sheet, &ext);
    return OWF_OK;
fail:
    owf_free(clear);
    owf_calc_free(calc);
    owf_book_free(work);
    return rc;
}

/* dest's sheet and top left cell (the active cell when it is empty). */
static int dest_cell(const oc_core *core, const char *dest, int *sheet, unsigned *row, unsigned *col)
{
    owf_range r;
    int s2;
    const char *p = dest;
    while (p && *p == ' ')
        p++;
    if (!p || !*p) {
        *sheet = core->sheet;
        *row = oc_state_const(core)->sel.row;
        *col = oc_state_const(core)->sel.col;
        return 1;
    }
    if (!oc_sheet_range(core->book, p, core->sheet, &s2, &r) || s2 < 0 || s2 >= core->book->nsheets)
        return 0;
    *sheet = s2;
    *row = r.first_row;
    *col = r.first_col;
    return 1;
}

static int spec_ok(const oc_extdata_spec *spec)
{
    return spec && (spec->kind == OC_EXTDATA_WEB || spec->kind == OC_EXTDATA_DATABASE) &&
           memchr(spec->source, 0, sizeof spec->source) && memchr(spec->item, 0, sizeof spec->item) &&
           memchr(spec->dest, 0, sizeof spec->dest);
}

static int start(oc_core *core, int flags, char *err, size_t errsize)
{
    if (err && errsize)
        err[0] = 0;
    if (!core || !core->book)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode) {
        if (flags & OC_EXTDATA_QUIET)
            return OWF_ERR_UNSUPPORTED;  /* not while the user types: the clock tries again */
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    }
    return OWF_OK;
}

int oc_core_extdata_add(oc_core *core, const oc_extdata_spec *spec, const oc_extdata_io *io, int flags, int *index,
                        char *err, size_t errsize)
{
    ext_entry e[OC_EXTDATA_MAX];
    oc_exttable t;
    unsigned row, col, id = 0;
    int sheet, n, i, rc;
    if (index)
        *index = -1;
    if ((rc = start(core, flags, err, errsize)) != OWF_OK)
        return rc;
    if (!spec_ok(spec))
        return OWF_ERR_FORMAT;
    n = entries(core->book, e);
    if (n >= OC_EXTDATA_MAX) {
        say(err, errsize, "A book holds at most 64 external data ranges%s", NULL);
        return OWF_ERR_UNSUPPORTED;
    }
    if (!dest_cell(core, spec->dest, &sheet, &row, &col)) {
        say(err, errsize, "\"%s\" is not a cell", spec->dest);
        return OWF_ERR_FORMAT;
    }
    for (i = 0; i < core->book->nnames; i++) {
        unsigned k = range_id(core->book->names[i].name);
        if (k > id)
            id = k;
    }
    id++;
    if ((rc = read_source(spec, io, &t, err, errsize)) != OWF_OK)
        return rc;
    rc = put_values(core, spec, &t, sheet, row, col, -1, NULL, -1, id, flags);
    oc_exttable_free(&t);
    if (rc == OWF_OK && index) {
        n = entries(core->book, e);
        for (i = 0; i < n; i++)
            if (e[i].id == id)
                *index = i;
    }
    return rc;
}

/* Range index read again with spec (NULL: its own settings). */
static int refresh_one(oc_core *core, int index, const oc_extdata_spec *spec, const oc_extdata_io *io, int flags,
                       char *err, size_t errsize)
{
    ext_entry e[OC_EXTDATA_MAX];
    oc_extdata_spec now;
    oc_exttable t;
    owf_range old;
    unsigned row, col;
    int n, old_sheet, sheet, rc;
    n = entries(core->book, e);
    if (index < 0 || index >= n)
        return OWF_ERR_UNSUPPORTED;
    if (!describe(core->book, &e[index], core->sheet, &now, &old_sheet, &old))
        return OWF_ERR_FORMAT;
    if (!spec) {
        spec = &now;
        sheet = old_sheet;
        row = old.first_row;
        col = old.first_col;
    } else if (!dest_cell(core, spec->dest, &sheet, &row, &col)) {
        say(err, errsize, "\"%s\" is not a cell", spec->dest);
        return OWF_ERR_FORMAT;
    }
    if ((rc = read_source(spec, io, &t, err, errsize)) != OWF_OK)
        return rc;
    rc = put_values(core, spec, &t, sheet, row, col, old_sheet, &old, e[index].sheet, e[index].id, flags);
    oc_exttable_free(&t);
    return rc;
}

int oc_core_extdata_change(oc_core *core, int index, const oc_extdata_spec *spec, const oc_extdata_io *io,
                           int flags, char *err, size_t errsize)
{
    int rc;
    if ((rc = start(core, flags, err, errsize)) != OWF_OK)
        return rc;
    if (!spec_ok(spec))
        return OWF_ERR_FORMAT;
    return refresh_one(core, index, spec, io, flags, err, errsize);
}

int oc_core_extdata_refresh(oc_core *core, int index, const oc_extdata_io *io, int flags, char *err,
                            size_t errsize)
{
    int rc, i, n, first = OWF_OK;
    if ((rc = start(core, flags, err, errsize)) != OWF_OK)
        return rc;
    if (index >= 0)
        return refresh_one(core, index, NULL, io, flags, err, errsize);
    n = oc_core_extdata_count(core);
    for (i = 0; i < n; i++) {
        char *e = first == OWF_OK ? err : NULL;          /* the first error's words */
        rc = refresh_one(core, i, NULL, io, flags, e, e ? errsize : 0);
        if (rc != OWF_OK && first == OWF_OK)
            first = rc;
    }
    return first;
}

int oc_core_extdata_refresh_on_open(oc_core *core, const oc_extdata_io *io)
{
    int i, n, done = 0;
    if (!core || !core->book || core->edit_mode)
        return 0;
    n = oc_core_extdata_count(core);
    for (i = 0; i < n; i++) {
        oc_extdata_spec spec;
        if (oc_core_extdata(core, i, &spec, NULL, NULL) == OWF_OK && spec.refresh_on_open &&
            refresh_one(core, i, NULL, io, OC_EXTDATA_QUIET, NULL, 0) == OWF_OK)
            done++;
    }
    return done;
}

int oc_core_extdata_remove(oc_core *core, int index, int clear)
{
    ext_entry e[OC_EXTDATA_MAX];
    oc_extdata_spec spec;
    owf_book *work;
    owf_calc *calc;
    owf_range r;
    int n, sheet, i, rc;
    if ((rc = start(core, 0, NULL, 0)) != OWF_OK)
        return rc;
    n = entries(core->book, e);
    if (index < 0 || index >= n)
        return OWF_ERR_UNSUPPORTED;
    if (!describe(core->book, &e[index], core->sheet, &spec, &sheet, &r))
        sheet = -1;
    if (clear && sheet >= 0 && oc_range_locked(core, sheet, &r))
        return OC_PROTECTED;
    if (!(work = oc_book_copy(core->book)))
        return OWF_ERR_MEMORY;
    if (!(calc = owf_calc_new(work))) {
        owf_book_free(work);
        return OWF_ERR_MEMORY;
    }
    drop_names(work, e[index].sheet, e[index].id);
    if (clear && sheet >= 0) {
        const owf_sheet *ws = &work->sheets[sheet];
        unsigned *at = ws->ncells ? owf_malloc((size_t)ws->ncells * 2 * sizeof *at) : NULL;
        int nat = 0;
        rc = ws->ncells && !at ? OWF_ERR_MEMORY : OWF_OK;
        for (i = 0; rc == OWF_OK && i < ws->ncells; i++)
            if (inside(&r, ws->cells[i].row, ws->cells[i].col) && !oc_cell_empty(&ws->cells[i])) {
                at[nat * 2] = ws->cells[i].row;
                at[nat * 2 + 1] = ws->cells[i].col;
                nat++;
            }
        for (i = 0; rc == OWF_OK && i < nat; i++)
            rc = owf_calc_enter(calc, sheet, at[i * 2], at[i * 2 + 1], "");
        owf_free(at);
        if (rc == OWF_OK)
            rc = owf_calc_recalc(calc);
        if (rc != OWF_OK) {
            owf_calc_free(calc);
            owf_book_free(work);
            return rc;
        }
    }
    commit(core, work, calc, 0, sheet, NULL);
    return OWF_OK;
}

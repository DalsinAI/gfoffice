/*
 * GFOpenBase's sheets in and out (include/openbase_sheet.h; Design-OpenBase.md
 * section 8): an ODS or XLSX sheet read by libowf and handed to the core's
 * import a row at a time (ob_grid_import), and a table's or a query's rows
 * written as a libowf workbook of one sheet.
 *
 * In its own drawer (src/base/sheet/) so that the core's tests, which build
 * the .c files in src/base alone, don't need libowf. Only owf.h's calls are
 * used, so it builds on owfcalc.library as well as with libowf linked in.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openbase_sheet.h"
#include "owf.h"
#include "owf_open.h"

static int failf(ob_project *p, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
static int failf(ob_project *p, const char *fmt, ...)
{
    char text[OB_ERR_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    return ob_set_error(p, text);
}

/* owfcalc.library, opened by the first call that needs it (OWF=library
 * builds; with libowf linked in there is nothing to open). */
static int lib_open;

static int lib(ob_project *p)
{
    if (!lib_open) lib_open = owf_open();
    if (!lib_open)
        return failf(p, "Sheets need %s %d or newer in LIBS: (part of the GFOffice Suite)", OWF_LIBRARY_NAME,
                     OWF_LIBRARY_VERSION);
    return 1;
}

void ob_sheet_close(void)
{
    if (lib_open) owf_close();
    lib_open = 0;
}

void ob_sheet_opts_init(ob_sheet_opts *o)
{
    memset(o, 0, sizeof *o);
    o->sheet = -1;
    o->header = -1;
}

/* ---- Text ---- */

/* The next character of UTF-8 text at *s (moved past it); '?' for one that
 * is broken. */
static unsigned long utf8_next(const unsigned char **s)
{
    const unsigned char *in = *s;
    unsigned long v = *in;
    int more = v >= 0xf0 ? 3 : v >= 0xe0 ? 2 : v >= 0xc0 ? 1 : 0, k;
    in++;
    if (v >= 0x80 && v < 0xc0) { *s = in; return '?'; }
    if (more) {
        v &= 0x3fUL >> more;
        for (k = 0; k < more && (*in & 0xc0) == 0x80; k++) v = (v << 6) | (*in++ & 0x3f);
        if (k < more) v = '?';
    }
    *s = in;
    return v;
}

/* UTF-8 into ISO-8859-1, cut to fit size. */
static void to_latin1(const char *in, char *out, size_t size)
{
    const unsigned char *s = (const unsigned char *)(in ? in : "");
    size_t o = 0;
    while (*s && o + 1 < size) {
        unsigned long c = utf8_next(&s);
        out[o++] = (char)(c <= 0xff ? c : '?');
    }
    out[o] = 0;
}

/* ISO-8859-1 into UTF-8: malloc'd, NULL when memory runs out. */
static char *to_utf8(const char *in)
{
    const unsigned char *s = (const unsigned char *)in;
    char *out = malloc(strlen(in) * 2 + 1), *o = out;
    if (!out) return NULL;
    for (; *s; s++) {
        if (*s < 0x80) *o++ = (char)*s;
        else { *o++ = (char)(0xc0 | (*s >> 6)); *o++ = (char)(0x80 | (*s & 0x3f)); }
    }
    *o = 0;
    return out;
}

static int same_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

/* ---- Dates: days since 30 December 1899, as the files count them ----
 * The proleptic Gregorian calendar in whole numbers (days from 1 March of
 * year 0 by 400-year eras). */

static long serial_from_date(long y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 719468L + 25569L;
}

static void date_from_serial(long days, long *y, int *m, int *d)
{
    long z = days - 25569L + 719468L, era, doe, yoe, doy, mp;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

/* What a number format shows, from its first section: a date, a time, a date
 * and time, money (a currency sign), or a plain number. */
enum { NF_NUMBER, NF_DATE, NF_TIME, NF_DATETIME, NF_MONEY };

static int numfmt_kind(const char *f)
{
    int date = 0, time = 0, mins = 0, money = 0;
    const char *s;
    if (!f || same_nocase(f, "General")) return NF_NUMBER;
    for (s = f; *s && *s != ';'; s++) {
        int c = tolower((unsigned char)*s);
        if (*s == '"') {
            for (s++; *s && *s != '"'; s++)
                if ((unsigned char)*s == 0xc2 && (unsigned char)s[1] == 0xa3) money = 1;
                else if (*s == '$') money = 1;
            if (!*s) break;
        } else if (*s == '\\' || *s == '_' || *s == '*') {
            if (*s == '\\' && s[1] == '$') money = 1;
            if (!*++s) break;
        } else if (*s == '[') {
            const char *close = strchr(s, ']');
            int lc;
            if (!close) break;
            lc = tolower((unsigned char)s[1]);
            if (s[1] == '$') money = 1;
            else if (lc == 'h' || lc == 'm' || lc == 's') time = 1;
            s = close;
        } else if (c == 'y' || c == 'd') {
            date = 1;
        } else if (c == 'h' || c == 's') {
            time = 1;
        } else if (c == 'm') {
            mins = 1;
        } else if (*s == '$' || ((unsigned char)*s == 0xc2 && (unsigned char)s[1] == 0xa3)
                   || ((unsigned char)*s == 0xe2 && (unsigned char)s[1] == 0x82 && (unsigned char)s[2] == 0xac)) {
            money = 1;
        }
    }
    if (mins && !time) date = 1;        /* "mmm yyyy": months */
    if (date && time) return NF_DATETIME;
    if (date) return NF_DATE;
    if (time) return NF_TIME;
    return money ? NF_MONEY : NF_NUMBER;
}

/* ---- Reading ---- */

typedef struct rd {
    const owf_book *book;
    const owf_sheet *sh;
    unsigned minrow, mincol;
    int at;                 /* the first cell not yet passed */
    char *buf;              /* the texts of the row being read */
    size_t len, cap;
    size_t off[OB_MAX_FIELDS + 1];
    int failed;
} rd;

static int real_cell(const owf_cell *c)
{
    return c->type != OWF_VALUE_EMPTY && !(c->type == OWF_VALUE_TEXT && (!c->text || !c->text[0]));
}

static int put(rd *r, const char *s, size_t n)
{
    if (r->len + n + 1 > r->cap) {
        size_t cap = r->cap ? r->cap : 512;
        char *b;
        while (cap < r->len + n + 1) cap *= 2;
        if (!(b = realloc(r->buf, cap))) { r->failed = 1; return 0; }
        r->buf = b;
        r->cap = cap;
    }
    memcpy(r->buf + r->len, s, n);
    r->len += n;
    r->buf[r->len] = 0;
    return 1;
}

static const char *cell_format(const owf_book *b, const owf_cell *c)
{
    int nf;
    if (c->style <= 0 || c->style >= b->nstyles) return NULL;
    nf = b->styles[c->style].number_format;
    return nf > 0 && nf < b->nnumfmts ? b->numfmts[nf] : NULL;
}

/* The cell's value as the text a field takes, put at the end of r->buf. */
static int put_cell(rd *r, const owf_cell *c)
{
    char t[64];
    switch (c->type) {
    case OWF_VALUE_TEXT: {
        const unsigned char *s = (const unsigned char *)c->text;
        while (*s) {
            unsigned long ch = utf8_next(&s);
            char b = (char)(ch <= 0xff ? ch : '?');
            if (!put(r, &b, 1)) return 0;
        }
        return 1;
    }
    case OWF_VALUE_BOOL:
        return put(r, c->number != 0 ? "Yes" : "No", c->number != 0 ? 3 : 2);
    case OWF_VALUE_ERROR: {
        const char *e = owf_error_name(c->error);
        if (!e) e = "#VALUE!";
        return put(r, e, strlen(e));
    }
    case OWF_VALUE_NUMBER: {
        int kind = numfmt_kind(cell_format(r->book, c));
        double v = c->number;
        if (kind == NF_DATE || kind == NF_TIME || kind == NF_DATETIME) {
            double whole;
            long days, secs, y;
            int m, d;
            if (r->book->date1904) v += 1462;
            if (!(v > -693000.0 && v < 3000000.0)) {
                owf_number_text(c->number, t);
                break;
            }
            whole = floor(v);
            days = (long)whole;
            secs = (long)floor((v - whole) * 86400.0 + 0.5);
            if (secs >= 86400L) { days++; secs -= 86400L; }
            date_from_serial(days, &y, &m, &d);
            if (kind == NF_DATE) snprintf(t, sizeof t, "%04ld-%02d-%02d", y, m, d);
            else if (kind == NF_TIME) snprintf(t, sizeof t, "%02ld:%02ld:%02ld", secs / 3600, secs / 60 % 60, secs % 60);
            else snprintf(t, sizeof t, "%04ld-%02d-%02d %02ld:%02ld:%02ld", y, m, d, secs / 3600, secs / 60 % 60, secs % 60);
        } else if (kind == NF_MONEY && fabs(v) < 1e15) {
            snprintf(t, sizeof t, "%.2f", v);
        } else {
            owf_number_text(v, t);
        }
        break;
    }
    default:
        return 1;
    }
    return put(r, t, strlen(t));
}

static int rd_row(void *user, long row, const char **cells, int max)
{
    rd *r = user;
    const owf_sheet *sh = r->sh;
    unsigned want = r->minrow + (unsigned)row;
    int i, n = 0, k;
    r->len = 0;
    for (i = 0; i < max && i <= OB_MAX_FIELDS; i++) r->off[i] = (size_t)-1;
    while (r->at < sh->ncells && sh->cells[r->at].row < want) r->at++;
    for (k = r->at; k < sh->ncells && sh->cells[k].row == want; k++) {
        const owf_cell *c = &sh->cells[k];
        int col;
        if (!real_cell(c) || c->col < r->mincol) continue;
        col = (int)(c->col - r->mincol);
        if (col + 1 > n) n = col + 1;
        if (col >= max || col > OB_MAX_FIELDS) continue;
        r->off[col] = r->len;
        if (!put_cell(r, c) || !put(r, "", 1)) return -1;     /* the cell's text and its 0 */
    }
    for (i = 0; i < n && i < max && i <= OB_MAX_FIELDS; i++)
        cells[i] = r->off[i] == (size_t)-1 ? NULL : r->buf + r->off[i];
    return n;
}

/* The workbook at path, read by libowf: NULL with the reason. */
static owf_book *load(ob_project *p, const char *path)
{
    owf_book *book = NULL;
    owf_report *notes;
    const owf_book_format *used = NULL;
    if (!lib(p)) return NULL;
    notes = owf_report_new();
    int rc = owf_import_book_file(path, NULL, &book, notes, &used);
    if (notes) owf_report_free(notes);
    /* workbooks only: a text file is CSV's, which the core reads itself */
    if (rc == OWF_OK && book && used && strcmp(used->name, "csv")) return book;
    if (rc == OWF_OK) rc = OWF_ERR_FORMAT;
    if (book) owf_book_free(book);
    switch (rc) {
    case OWF_ERR_IO: failf(p, "%s could not be opened", path); break;
    case OWF_ERR_MEMORY: failf(p, "Not enough memory"); break;
    case OWF_ERR_CORRUPT: failf(p, "%s is damaged: it could not be read as a workbook", path); break;
    default: failf(p, "%s is not a workbook GFOpenBase reads (ODS or XLSX)", path); break;
    }
    return NULL;
}

int ob_sheet_list(ob_project *p, const char *path, char (*names)[OB_SHEET_NAME_MAX], int max)
{
    owf_book *book = load(p, path);
    int i, n;
    if (!book) return -1;
    n = book->nsheets;
    for (i = 0; i < n && i < max; i++) to_latin1(book->sheets[i].name, names[i], OB_SHEET_NAME_MAX);
    owf_book_free(book);
    ob_set_error(p, "");
    return n;
}

/* The sheet o asks for: its index, -1 with the reason. */
static int pick_sheet(ob_project *p, const owf_book *book, const char *path, const ob_sheet_opts *o)
{
    int i;
    if (book->nsheets <= 0) { failf(p, "%s has no sheets", path); return -1; }
    if (o->sheet_name[0]) {
        char name[OB_SHEET_NAME_MAX];
        for (i = 0; i < book->nsheets; i++) {
            to_latin1(book->sheets[i].name, name, sizeof name);
            if (same_nocase(name, o->sheet_name)) return i;
        }
        failf(p, "%s has no sheet named %s", path, o->sheet_name);
        return -1;
    }
    if (o->sheet < 0) return book->active >= 0 && book->active < book->nsheets ? book->active : 0;
    if (o->sheet >= book->nsheets) {
        failf(p, "%s has %d sheet%s: there is no sheet %d", path, book->nsheets, book->nsheets == 1 ? "" : "s",
              o->sheet + 1);
        return -1;
    }
    return o->sheet;
}

/* Reads the sheet and sets up a grid of its rows over r. 0 with the reason. */
static int open_grid(ob_project *p, const char *path, ob_sheet_opts *o, owf_book **bookp, rd *r, ob_grid *g,
                     char *name, size_t size)
{
    const owf_sheet *sh;
    int i, any = 0, which, wide = 0, here = 0;
    unsigned maxrow = 0, start = 0, row = 0;
    memset(r, 0, sizeof *r);
    memset(g, 0, sizeof *g);
    if (!(*bookp = load(p, path))) return 0;
    if ((which = pick_sheet(p, *bookp, path, o)) < 0) return 0;
    sh = &(*bookp)->sheets[which];
    to_latin1(sh->name, name, size);
    snprintf(o->found_sheet, sizeof o->found_sheet, "%s", name);
    r->book = *bookp;
    r->sh = sh;
    /* a title above the table is passed over: leading rows of one cell each,
     * then a gap, then a row of two cells or more, start there */
    {
        unsigned last = 0;
        int rows = 0;
        for (i = 0; i <= sh->ncells; i++) {
            const owf_cell *c = i < sh->ncells ? &sh->cells[i] : NULL;
            if (c && !real_cell(c)) continue;
            if (c && here && c->row == row) { here++; continue; }
            if (here) {                 /* row ended, with `here` cells */
                if (here >= 2) {
                    if (rows && row > last + 1) { start = row; wide = 1; }
                    break;
                }
                last = row;
                rows++;
            }
            if (!c) break;
            row = c->row;
            here = 1;
        }
    }
    for (i = 0; i < sh->ncells; i++) {
        const owf_cell *c = &sh->cells[i];
        if (!real_cell(c) || (wide && c->row < start)) continue;
        if (!any || c->row < r->minrow) r->minrow = c->row;
        if (!any || c->col < r->mincol) r->mincol = c->col;
        if (!any || c->row > maxrow) maxrow = c->row;
        any = 1;
    }
    g->name = name;
    g->nrows = any ? (long)(maxrow - r->minrow) + 1 : 0;
    if (g->nrows > OB_SHEET_MAX_ROWS) g->nrows = OB_SHEET_MAX_ROWS;
    g->first_row = (long)r->minrow + 1;
    g->row = rd_row;
    g->user = r;
    return 1;
}

static void csv_opts_from(ob_csv_opts *c, const ob_sheet_opts *o)
{
    ob_csv_opts_init(c);
    c->header = o->header;
    c->skip_bad = o->skip_bad;
}

static void csv_opts_back(ob_sheet_opts *o, const ob_csv_opts *c)
{
    o->bad = c->bad;
    memcpy(o->report, c->report, sizeof o->report);
    o->report[sizeof o->report - 1] = 0;
    o->found_header = c->found_header;
}

ob_int ob_sheet_import(ob_project *p, const char *table, const char *path, ob_sheet_opts *o)
{
    ob_sheet_opts dflt;
    ob_csv_opts c;
    owf_book *book = NULL;
    rd r;
    ob_grid g;
    char name[OB_SHEET_NAME_MAX];
    ob_int n = -1;
    if (!o) { ob_sheet_opts_init(&dflt); o = &dflt; }
    o->bad = 0;
    o->report[0] = 0;
    o->found_sheet[0] = 0;
    csv_opts_from(&c, o);
    if (open_grid(p, path, o, &book, &r, &g, name, sizeof name)) {
        n = ob_grid_import(p, table, &g, &c);
        if (r.failed && n >= 0) { failf(p, "Not enough memory"); n = -1; }
        csv_opts_back(o, &c);
    }
    free(r.buf);
    if (book) owf_book_free(book);
    return n;
}

int ob_sheet_guess(ob_project *p, const char *path, const char *name, ob_sheet_opts *o, ob_table *out)
{
    ob_sheet_opts dflt;
    ob_csv_opts c;
    owf_book *book = NULL;
    rd r;
    ob_grid g;
    char sname[OB_SHEET_NAME_MAX];
    int good = 0;
    if (!o) { ob_sheet_opts_init(&dflt); o = &dflt; }
    o->found_sheet[0] = 0;
    csv_opts_from(&c, o);
    if (open_grid(p, path, o, &book, &r, &g, sname, sizeof sname)) {
        good = ob_grid_guess(p, &g, name && *name ? name : sname, &c, out);
        if (r.failed && good) good = failf(p, "Not enough memory");
        o->found_header = c.found_header;
    }
    free(r.buf);
    if (book) owf_book_free(book);
    return good;
}

/* ---- Writing ---- */

#define WR_COLS (OB_QUERY_OUT > OB_MAX_FIELDS ? OB_QUERY_OUT : OB_MAX_FIELDS)

typedef struct wr {
    owf_book *book;
    owf_sheet *sh;
    unsigned row;
    int n;                          /* values in a row */
    int types[WR_COLS];             /* ob_type, -1 as SQLite gives it, OB_OBJECT left out */
    int width[WR_COLS];             /* the longest value, in characters */
    int st_head, st_money, st_date, st_time, st_datetime;
    int failed, too_many;
} wr;

static int whole_number(const char *v, double *out)
{
    char *end;
    long long n;
    if (!*v) return 0;
    n = strtoll(v, &end, 10);
    if (*end) return 0;
    *out = (double)n;
    return 1;
}

static int real_number(const char *v, double *out)
{
    char *end;
    double d;
    while (*v == ' ') v++;
    if (!*v || isalpha((unsigned char)*v)) return 0;      /* not "inf" or "nan" */
    d = strtod(v, &end);
    while (*end == ' ') end++;
    if (*end || d != d) return 0;
    *out = d;
    return 1;
}

static int date_part(const char **s, long *serial)
{
    int y, m, d, k;
    if (sscanf(*s, "%4d-%2d-%2d%n", &y, &m, &d, &k) != 3 || k != 10 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    *serial = serial_from_date(y, m, d);
    *s += k;
    return 1;
}

static int time_part(const char **s, long *secs)
{
    int h, m, sec = 0, k = 0;
    if (sscanf(*s, "%2d:%2d:%2d%n", &h, &m, &sec, &k) == 3 && k == 8) {
    } else if (sscanf(*s, "%2d:%2d%n", &h, &m, &k) == 2 && k == 5) {
        sec = 0;
    } else {
        return 0;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59) return 0;
    *secs = h * 3600L + m * 60L + sec;
    *s += k;
    return 1;
}

static void wr_width(wr *w, int col, const char *v)
{
    int len = (int)strlen(v);
    if (col < WR_COLS && len > w->width[col]) w->width[col] = len;
}

/* One value at (w->row, col) as its type writes it. */
static int wr_cell(wr *w, int col, int type, const char *v)
{
    owf_cell *c;
    double d;
    long serial, secs;
    ob_int pence;
    const char *s = v;
    if (!v || !*v) return 1;
    wr_width(w, col, v);
    if (!(c = owf_sheet_cell(w->sh, w->row, (unsigned)col, 1))) return 0;
    switch (type) {
    case OB_COUNTER: case OB_INTEGER: case OB_LOOKUP:
        if (whole_number(v, &d)) { owf_cell_set_number(c, d); return 1; }
        break;
    case OB_NUMBER:
        if (real_number(v, &d)) { owf_cell_set_number(c, d); return 1; }
        break;
    case OB_CURRENCY:
        if (ob_currency_parse(v, &pence)) {
            owf_cell_set_number(c, (double)pence / 100.0);
            c->style = w->st_money;
            return 1;
        }
        break;
    case OB_DATE:
        if (date_part(&s, &serial) && !*s) { owf_cell_set_number(c, (double)serial); c->style = w->st_date; return 1; }
        break;
    case OB_TIME:
        if (time_part(&s, &secs) && !*s) { owf_cell_set_number(c, secs / 86400.0); c->style = w->st_time; return 1; }
        break;
    case OB_DATETIME:
        if (date_part(&s, &serial) && (*s == ' ' || *s == 'T') && (s++, time_part(&s, &secs)) && !*s) {
            owf_cell_set_number(c, (double)serial + secs / 86400.0);
            c->style = w->st_datetime;
            return 1;
        }
        break;
    case OB_YESNO:
        if (same_nocase(v, "Yes") || !strcmp(v, "1")) { owf_cell_set_bool(c, 1); return 1; }
        if (same_nocase(v, "No") || !strcmp(v, "0")) { owf_cell_set_bool(c, 0); return 1; }
        break;
    case -1:
        if (real_number(v, &d)) { owf_cell_set_number(c, d); return 1; }
        break;
    default:
        break;
    }
    /* text, or a value that isn't in its type's form: as it is */
    {
        char *u = to_utf8(v);
        int good = u && owf_cell_set_text(c, u, strlen(u)) == 0;
        free(u);
        return good;
    }
}

static int wr_row(void *user, ob_int key, int n, const char *const *values)
{
    wr *w = user;
    int i, col = 0;
    (void)key;
    if (w->row >= (unsigned long)OB_SHEET_MAX_ROWS) { w->too_many = 1; return 1; }
    for (i = 0; i < n && i < w->n; i++) {
        if (w->types[i] == OB_OBJECT) continue;
        if (!wr_cell(w, col, w->types[i], values[i])) { w->failed = 1; return 1; }
        col++;
    }
    w->row++;
    return 0;
}

static int wr_style(owf_book *b, const char *format, int bold)
{
    owf_cellstyle st;
    owf_cellstyle_init(&st);
    if (bold) st.font.flags |= OWF_BOLD;
    if (format) {
        int nf = owf_book_numfmt(b, format);
        if (nf < 0) return -1;
        st.number_format = nf;
    }
    return owf_book_style(b, &st);
}

/* A new book of one sheet called name (ISO-8859-1), its styles made. */
static int wr_start(ob_project *p, wr *w, const char *name)
{
    char clean[32], *u;
    size_t o = 0;
    const char *s;
    memset(w, 0, sizeof *w);
    /* a name a spreadsheet takes: no []:*?/\, 31 characters at most */
    for (s = name; *s && o + 1 < sizeof clean; s++)
        if (!strchr("[]:*?/\\", *s)) clean[o++] = *s;
    while (o && clean[o - 1] == ' ') o--;
    clean[o] = 0;
    if (!clean[0]) snprintf(clean, sizeof clean, "Sheet1");
    if (!(w->book = owf_book_new()) || !(u = to_utf8(clean))) return failf(p, "Not enough memory");
    w->sh = owf_book_add_sheet(w->book, u);
    free(u);
    if (!w->sh) return failf(p, "Not enough memory");
    w->st_head = wr_style(w->book, NULL, 1);
    w->st_money = wr_style(w->book, "#,##0.00", 0);
    w->st_date = wr_style(w->book, "yyyy-mm-dd", 0);
    w->st_time = wr_style(w->book, "hh:mm:ss", 0);
    w->st_datetime = wr_style(w->book, "yyyy-mm-dd hh:mm:ss", 0);
    if (w->st_head < 0 || w->st_money < 0 || w->st_date < 0 || w->st_time < 0 || w->st_datetime < 0)
        return failf(p, "Not enough memory");
    return 1;
}

static int wr_heading(wr *w, int col, const char *text)
{
    owf_cell *c = owf_sheet_cell(w->sh, 0, (unsigned)col, 1);
    char *u = to_utf8(text);
    int good = c && u && owf_cell_set_text(c, u, strlen(u)) == 0;
    free(u);
    if (good) c->style = w->st_head;
    wr_width(w, col, text);
    return good;
}

/* The format path is written in: its libowf name, NULL with the reason. */
static const char *wr_format(ob_project *p, const char *path, const ob_sheet_opts *o)
{
    const char *f = o && o->format ? o->format : NULL;
    if (!f) {
        const owf_book_format *bf = owf_book_format_for_filename(path);
        f = bf ? bf->name : NULL;
    }
    if (f && (same_nocase(f, "ods") || same_nocase(f, "xlsx"))) return same_nocase(f, "ods") ? "ods" : "xlsx";
    failf(p, "%s: sheets are written as .ods or .xlsx", path);
    return NULL;
}

/* Column widths from the longest values, the header kept in view, written. */
static ob_int wr_finish(ob_project *p, wr *w, const char *path, const char *format, int ncols, ob_int rows)
{
    owf_report *notes;
    int i, rc;
    if (w->failed) { failf(p, "Not enough memory"); return -1; }
    if (w->too_many) { failf(p, "A sheet holds at most %ld rows: filter the rows first", OB_SHEET_MAX_ROWS - 1); return -1; }
    for (i = 0; i < ncols && i < WR_COLS; i++) {
        int chars = w->width[i] < 8 ? 8 : w->width[i] > 60 ? 60 : w->width[i];
        if (owf_sheet_set_columns(w->sh, (unsigned)i, (unsigned)i, (chars + 2) * 115, 0, 0) < 0) {
            failf(p, "Not enough memory");
            return -1;
        }
    }
    w->sh->freeze_rows = 1;
    notes = owf_report_new();
    rc = owf_export_book_file(w->book, path, format, notes);
    if (notes) owf_report_free(notes);
    if (rc == OWF_ERR_MEMORY) { failf(p, "Not enough memory"); return -1; }
    if (rc != OWF_OK) { failf(p, "%s could not be written: is the disk full?", path); return -1; }
    ob_set_error(p, "");
    return rows;
}

ob_int ob_sheet_export(ob_project *p, const char *table, const ob_view *v, const char *path,
                       const ob_sheet_opts *o)
{
    ob_table *t;
    wr *w;
    const char *format;
    int i, ncols = 0;
    ob_int n = -1;
    if (!lib(p)) return -1;
    if (!(t = malloc(sizeof *t))) { failf(p, "Not enough memory"); return -1; }
    if (!(w = calloc(1, sizeof *w))) { free(t); failf(p, "Not enough memory"); return -1; }
    if (!ob_table_get(p, table, t)) {
        if (!ob_error(p)[0]) failf(p, "There is no table %s", table);
        goto out;
    }
    if (v && !ob_view_check(p, table, v)) goto out;
    if (!(format = wr_format(p, path, o))) goto out;
    if (!wr_start(p, w, o && o->sheet_name[0] ? o->sheet_name : t->name)) goto out;
    w->n = t->nfields;
    for (i = 0; i < t->nfields; i++) {
        w->types[i] = (int)t->fields[i].type;
        if (t->fields[i].type == OB_OBJECT) continue;
        if (!wr_heading(w, ncols, t->fields[i].name)) { failf(p, "Not enough memory"); goto out; }
        ncols++;
    }
    w->row = 1;
    n = ob_view_rows(p, table, v, 0, -1, wr_row, w);
    if (n >= 0) n = wr_finish(p, w, path, format, ncols, n);
out:
    if (w->book) owf_book_free(w->book);
    free(w);
    free(t);
    return n;
}

ob_int ob_query_sheet_export(ob_project *p, const ob_query *q, const char *const *params, const char *path,
                             const ob_sheet_opts *o)
{
    ob_qresult *res;
    wr *w;
    const char *format;
    int i, nres, ncols = 0;
    ob_int n = -1;
    if (!lib(p)) return -1;
    if (!(res = malloc(sizeof *res * OB_QUERY_OUT))) { failf(p, "Not enough memory"); return -1; }
    if (!(w = calloc(1, sizeof *w))) { free(res); failf(p, "Not enough memory"); return -1; }
    nres = ob_query_columns(p, q, res, OB_QUERY_OUT);
    /* the answers are checked before the file is made */
    if (nres < 0 || ob_query_total(p, q, params) < 0) goto out;
    if (nres > OB_QUERY_OUT) nres = OB_QUERY_OUT;
    if (!(format = wr_format(p, path, o))) goto out;
    if (!wr_start(p, w, o && o->sheet_name[0] ? o->sheet_name : q->name[0] ? q->name : "Query")) goto out;
    w->n = nres;
    for (i = 0; i < nres; i++) {
        w->types[i] = res[i].type < 0 ? -1 : res[i].type;
        if (res[i].type == OB_OBJECT) continue;
        if (!wr_heading(w, ncols, res[i].heading)) { failf(p, "Not enough memory"); goto out; }
        ncols++;
    }
    w->row = 1;
    n = ob_query_rows(p, q, params, 0, -1, wr_row, w);
    if (n >= 0) n = wr_finish(p, w, path, format, ncols, n);
out:
    if (w->book) owf_book_free(w->book);
    free(w);
    free(res);
    return n;
}

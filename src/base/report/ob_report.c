/*
 * GFOpenBase reports (include/openbase_report.h; Design-OpenBase.md section 7):
 * a table's or a saved query's rows, grouped and totalled, as an owf_doc.
 *
 * The rows come through ob_view_rows (a table, sorted by the group field or
 * the report's sort) or ob_query_rows (a query, in its own order) one at a
 * time: a new group's value closes the last group's table with its totals
 * and starts a heading and a table for the next. Nothing holds every row.
 *
 * In its own drawer (src/base/report/) so that the core's tests, which build
 * the .c files in src/base alone, don't need libowf.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openbase_report.h"

/* A total being worked out: over the values that aren't empty. */
typedef struct acc {
    ob_int count;           /* values that aren't empty */
    ob_int nums;            /* of those, the ones that are numbers */
    int over;               /* isum went past 64 bits: Sum and Average say "too large" */
    ob_int isum, imin, imax;   /* Integer, Currency (pence) */
    double dsum, dmin, dmax;   /* Number */
} acc;

typedef struct rcol {
    int src;                /* the value's index in a row */
    int type;               /* ob_type; -1 a query's expression, taken as a number */
    char heading[OB_LABEL_MAX];
    ob_total total;
    acc group, all;
} rcol;

typedef struct plan {
    ob_project *p;
    const ob_report *r;
    int is_query;
    ob_table *table;        /* the table's design (heap: about 90 KB) */
    ob_query *query;        /* the saved query (heap: about 24 KB) */
    int ncols;
    rcol cols[OB_REPORT_COLUMNS];
    int group_src;          /* -1 no group */
    int group_text;         /* the group's values compare without case (as a text sort does) */
    char group_heading[OB_LABEL_MAX];
    int totals;             /* any column has a total */
    /* building */
    owf_doc *doc;
    int table_id, row;
    ob_int rows;
    char *group_value;      /* this group's value ("" empty), NULL before the first row */
    int failed;             /* out of memory */
} plan;

static int fail(char *err, size_t errsize, const char *fmt, ...)
{
    va_list ap;
    if (err && errsize) {
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static void copy(char *out, size_t size, const char *s)
{
    snprintf(out, size, "%s", s ? s : "");
}

static int same_name(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

static int is_number_type(int type)
{
    return type == OB_INTEGER || type == OB_NUMBER || type == OB_CURRENCY || type == -1;
}

void ob_report_init(ob_report *r, const char *name)
{
    memset(r, 0, sizeof *r);
    copy(r->name, sizeof r->name, name);
}

ob_rcolumn *ob_report_add_column(ob_report *r, const char *field, ob_total total)
{
    ob_rcolumn *c;
    if (r->ncolumns >= OB_REPORT_COLUMNS) return NULL;
    c = &r->columns[r->ncolumns++];
    memset(c, 0, sizeof *c);
    copy(c->field, sizeof c->field, field);
    c->total = total;
    return c;
}

static void plan_free(plan *pl)
{
    free(pl->table);
    free(pl->query);
    free(pl->group_value);
    pl->table = NULL;
    pl->query = NULL;
    pl->group_value = NULL;
}

/* Where a source column named `name` is: its index, type and heading; -1 when
 * there is none. */
static int find_source(plan *pl, const ob_qresult *qcols, int nq, const char *name, int *type,
                       char *heading, size_t hsize)
{
    int i;
    if (pl->is_query) {
        for (i = 0; i < nq; i++)
            if (same_name(qcols[i].heading, name)) {
                *type = qcols[i].type;
                copy(heading, hsize, qcols[i].heading);
                return i;
            }
        return -1;
    }
    if ((i = ob_table_find_field(pl->table, name)) < 0) return -1;
    *type = pl->table->fields[i].type;
    copy(heading, hsize, pl->table->fields[i].label[0] ? pl->table->fields[i].label : pl->table->fields[i].name);
    return i;
}

/* Reads r's source and finds its columns: 0, or -1 with the reason. */
static int plan_make(plan *pl, ob_project *p, const ob_report *r, char *err, size_t errsize)
{
    static const char *const total_names = "Sum, Count, Average, Min or Max (or none)";
    ob_qresult *qcols = NULL;
    const char *source;
    int i, nq = 0, rc = -1;
    memset(pl, 0, sizeof *pl);
    pl->p = p;
    pl->r = r;
    pl->group_src = -1;
    if (!r->table[0] && !r->query[0]) return fail(err, errsize, "%s: needs a table or a query", r->name);
    if (r->table[0] && r->query[0]) return fail(err, errsize, "%s: a table or a query, not both", r->name);
    if (r->ncolumns <= 0) return fail(err, errsize, "%s: no columns", r->name);
    if (r->ncolumns > OB_REPORT_COLUMNS) return fail(err, errsize, "%s: too many columns", r->name);
    pl->is_query = r->query[0] != 0;
    source = pl->is_query ? r->query : r->table;
    if (pl->is_query) {
        if (r->sort[0]) return fail(err, errsize, "%s: a query's rows come in its own order (sort them there)", r->name);
        if (!(pl->query = malloc(sizeof *pl->query)) || !(qcols = malloc(sizeof *qcols * OB_QUERY_OUT))) {
            fail(err, errsize, "Not enough memory");
            goto out;
        }
        if (!ob_query_get(p, r->query, pl->query)) {
            fail(err, errsize, "%s: no query %s", r->name, r->query);
            goto out;
        }
        if ((nq = ob_query_columns(p, pl->query, qcols, OB_QUERY_OUT)) < 0) {
            fail(err, errsize, "%s: %s", r->name, ob_error(p));
            goto out;
        }
        if (nq > OB_QUERY_OUT) nq = OB_QUERY_OUT;
    } else {
        if (!(pl->table = malloc(sizeof *pl->table))) { fail(err, errsize, "Not enough memory"); goto out; }
        if (!ob_table_get(p, r->table, pl->table)) {
            fail(err, errsize, "%s: no table %s", r->name, r->table);
            goto out;
        }
        if (r->sort[0] && ob_table_find_field(pl->table, r->sort) < 0) {
            fail(err, errsize, "%s: sort by %s: %s has no field %s", r->name, r->sort, source, r->sort);
            goto out;
        }
    }
    for (i = 0; i < r->ncolumns; i++) {
        const ob_rcolumn *c = &r->columns[i];
        rcol *rc_ = &pl->cols[i];
        rc_->src = find_source(pl, qcols, nq, c->field, &rc_->type, rc_->heading, sizeof rc_->heading);
        if (rc_->src < 0) {
            fail(err, errsize, "%s: %s has no %s %s", r->name, source, pl->is_query ? "column" : "field", c->field);
            goto out;
        }
        if (c->heading[0]) copy(rc_->heading, sizeof rc_->heading, c->heading);
        if (c->total == OB_TOTAL_GROUP || (unsigned)c->total >= OB_TOTALS) {
            fail(err, errsize, "%s: %s: a column's total is %s", r->name, c->field, total_names);
            goto out;
        }
        if (c->total != OB_TOTAL_NONE && c->total != OB_TOTAL_COUNT && !is_number_type(rc_->type)) {
            fail(err, errsize, "%s: %s needs a number: %s is %s", r->name, ob_total_name(c->total), c->field,
                 ob_type_name((ob_type)rc_->type));
            goto out;
        }
        rc_->total = c->total;
        if (c->total != OB_TOTAL_NONE) pl->totals = 1;
    }
    pl->ncols = r->ncolumns;
    if (r->group[0]) {
        int type;
        pl->group_src = find_source(pl, qcols, nq, r->group, &type, pl->group_heading, sizeof pl->group_heading);
        if (pl->group_src < 0) {
            fail(err, errsize, "%s: group by %s: %s has no %s %s", r->name, r->group, source,
                 pl->is_query ? "column" : "field", r->group);
            goto out;
        }
        pl->group_text = type == OB_TEXT || type == OB_MEMO || type == OB_CHOICE || type == -1;
        /* Groups are made as the rows come, so a query's must come sorted by the group first. A
         * grid query says so in its first sorted column; an SQL query's ORDER BY is the caller's. */
        if (pl->is_query && !pl->query->sql_only) {
            const ob_qcolumn *first = NULL;
            for (i = 0; i < pl->query->ncolumns && !first; i++)
                if (pl->query->columns[i].sort != OB_SORT_NONE) first = &pl->query->columns[i];
            if (!first || !(same_name(first->field, r->group) || (first->heading[0] && same_name(first->heading, r->group))
                            || same_name(first->field, pl->group_heading) ||
                            (first->heading[0] && same_name(first->heading, pl->group_heading)))) {
                fail(err, errsize, "%s: group by %s: sort the query %s by %s first", r->name, r->group, r->query,
                     r->group);
                goto out;
            }
        }
    }
    rc = 0;
out:
    free(qcols);
    if (rc) plan_free(pl);
    return rc;
}

int ob_report_check(ob_project *p, const ob_report *r, char *err, size_t errsize)
{
    plan pl;
    if (plan_make(&pl, p, r, err, errsize)) return -1;
    plan_free(&pl);
    return 0;
}

/* ---- Building the document ---- */

/* ISO-8859-1 to UTF-8, added to para. */
static int add_text(plan *pl, owf_para *para, int bold, const char *latin1)
{
    owf_charfmt cf;
    const unsigned char *s = (const unsigned char *)(latin1 ? latin1 : "");
    size_t n = strlen((const char *)s), k = 0;
    char small[256], *buf = n * 2 < sizeof small ? small : malloc(n * 2 + 1);
    int rc;
    if (!n) return 0;
    if (!buf) { pl->failed = 1; return -1; }
    for (; *s; s++) {
        if (*s < 0x80) {
            buf[k++] = (char)*s;
        } else {
            buf[k++] = (char)(0xC0 | (*s >> 6));
            buf[k++] = (char)(0x80 | (*s & 0x3F));
        }
    }
    owf_charfmt_init(&cf);
    if (bold) cf.flags |= OWF_BOLD;
    rc = owf_para_add_text(para, &cf, buf, k);
    if (buf != small) free(buf);
    if (rc) pl->failed = 1;
    return rc;
}

static owf_para *add_para(plan *pl, owf_story *story, int heading, owf_align align)
{
    owf_parafmt pf;
    owf_para *para;
    owf_parafmt_init(&pf);
    pf.heading = heading;
    pf.align = align;
    if (!(para = owf_story_add(story, &pf))) pl->failed = 1;
    return para;
}

/* A row of the open table: one cell for each column, with text[i] (NULL: empty). */
static void add_row(plan *pl, const char *const *text, int bold)
{
    int i;
    for (i = 0; i < pl->ncols && !pl->failed; i++) {
        owf_para *cell = add_para(pl, &pl->doc->body, 0, OWF_ALIGN_LEFT);
        if (!cell) return;
        cell->table_id = pl->table_id;
        cell->table_row = pl->row;
        cell->table_col = i;
        cell->table_cols = pl->ncols;
        if (text[i]) add_text(pl, cell, bold, text[i]);
    }
    pl->row++;
}

static void start_table(plan *pl)
{
    const char *heads[OB_REPORT_COLUMNS];
    int i;
    pl->table_id++;
    pl->row = 0;
    for (i = 0; i < pl->ncols; i++) heads[i] = pl->cols[i].heading;
    add_row(pl, heads, 1);
}

static void acc_clear(acc *a)
{
    memset(a, 0, sizeof *a);
}

static void acc_add(acc *a, int type, const char *v)
{
    ob_int n;
    double d;
    char *end;
    if (!v || !*v) return;
    a->count++;
    if (type == OB_INTEGER || type == OB_CURRENCY) {
        if (type == OB_CURRENCY) {
            if (!ob_currency_parse(v, &n)) return;
        } else {
            n = strtoll(v, &end, 10);
            if (end == v || *end) return;
        }
        if (!a->nums || n < a->imin) a->imin = n;
        if (!a->nums || n > a->imax) a->imax = n;
        if ((n > 0 && a->isum > LLONG_MAX - n) || (n < 0 && a->isum < LLONG_MIN - n)) a->over = 1;
        else a->isum += n;
        a->nums++;
    } else if (is_number_type(type)) {
        d = strtod(v, &end);
        if (end == v || *end) return;
        if (!a->nums || d < a->dmin) a->dmin = d;
        if (!a->nums || d > a->dmax) a->dmax = d;
        a->dsum += d;
        a->nums++;
    }
}

static void put_number(int type, ob_int i, double d, int as_int, char *out, size_t size)
{
    if (type == OB_CURRENCY) ob_currency_format(i, out, size);
    else if (as_int) snprintf(out, size, "%lld", (long long)i);
    else snprintf(out, size, "%.10g", d);
}

/* "Sum 1,010.50"; "" for no total. A total of no numbers: Sum and Count 0,
 * the others the total's name alone. */
static void acc_text(const rcol *c, const acc *a, char *out, size_t size)
{
    char v[64] = "";
    int whole = c->type == OB_INTEGER || c->type == OB_CURRENCY;
    switch (c->total) {
    case OB_TOTAL_COUNT:
        snprintf(v, sizeof v, "%lld", (long long)a->count);
        break;
    case OB_TOTAL_SUM:
        if (whole && a->over) { copy(v, sizeof v, "too large"); break; }
        put_number(c->type, a->isum, a->dsum, whole, v, sizeof v);
        break;
    case OB_TOTAL_AVERAGE:
        if (!a->nums) break;
        if (whole && a->over) { copy(v, sizeof v, "too large"); break; }
        if (c->type == OB_CURRENCY) ob_currency_format((ob_int)llround((double)a->isum / (double)a->nums), v, sizeof v);
        else put_number(-1, 0, (whole ? (double)a->isum : a->dsum) / (double)a->nums, 0, v, sizeof v);
        break;
    case OB_TOTAL_MIN:
        if (a->nums) put_number(c->type, a->imin, a->dmin, whole, v, sizeof v);
        break;
    case OB_TOTAL_MAX:
        if (a->nums) put_number(c->type, a->imax, a->dmax, whole, v, sizeof v);
        break;
    default:
        out[0] = 0;
        return;
    }
    snprintf(out, size, v[0] ? "%s %s" : "%s", ob_total_name(c->total), v);
}

static void add_totals(plan *pl, int all)
{
    char cells[OB_REPORT_COLUMNS][96];
    const char *text[OB_REPORT_COLUMNS];
    int i;
    if (!pl->totals) return;
    for (i = 0; i < pl->ncols; i++) {
        acc_text(&pl->cols[i], all ? &pl->cols[i].all : &pl->cols[i].group, cells[i], sizeof cells[i]);
        text[i] = cells[i][0] ? cells[i] : NULL;
    }
    add_row(pl, text, 1);
}

static int same_group(const plan *pl, const char *a, const char *b)
{
    return pl->group_text ? same_name(a, b) : !strcmp(a, b);
}

static void start_group(plan *pl, const char *value)
{
    char line[OB_LABEL_MAX + 8];
    owf_para *h;
    int i;
    snprintf(line, sizeof line, "%s: ", pl->group_heading);
    if (!(h = add_para(pl, &pl->doc->body, 2, OWF_ALIGN_LEFT))) return;
    if (add_text(pl, h, 0, line) || add_text(pl, h, 0, value[0] ? value : "(none)")) return;
    for (i = 0; i < pl->ncols; i++) acc_clear(&pl->cols[i].group);
    start_table(pl);
}

static int row_cb(void *user, ob_int key, int n, const char *const *values)
{
    plan *pl = user;
    const char *text[OB_REPORT_COLUMNS];
    int i;
    (void)key;
    if (pl->group_src >= 0) {
        const char *g = pl->group_src < n && values[pl->group_src] ? values[pl->group_src] : "";
        if (!pl->group_value || !same_group(pl, pl->group_value, g)) {
            char *copy_;
            if (pl->group_value) add_totals(pl, 0);
            if (!(copy_ = malloc(strlen(g) + 1))) { pl->failed = 1; return 1; }
            strcpy(copy_, g);
            free(pl->group_value);
            pl->group_value = copy_;
            start_group(pl, g);
        }
    } else if (!pl->rows) {
        start_table(pl);
    }
    for (i = 0; i < pl->ncols; i++) {
        rcol *c = &pl->cols[i];
        const char *v = c->src < n ? values[c->src] : NULL;
        text[i] = v;
        acc_add(&c->group, c->type, v);
        acc_add(&c->all, c->type, v);
    }
    add_row(pl, text, 0);
    pl->rows++;
    return pl->failed;
}

static void add_footer(plan *pl)
{
    owf_para *f = add_para(pl, &pl->doc->footer, 0, OWF_ALIGN_CENTRE);
    owf_charfmt cf;
    if (!f) return;
    owf_charfmt_init(&cf);
    if (add_text(pl, f, 0, "Page ") || owf_para_add_special(f, &cf, OWF_RUN_FIELD, OWF_FIELD_PAGE) ||
        add_text(pl, f, 0, " of ") || owf_para_add_special(f, &cf, OWF_RUN_FIELD, OWF_FIELD_PAGES))
        pl->failed = 1;
}

int ob_report_build(ob_project *p, const ob_report *r, const char *const *params, owf_doc **out,
                    char *err, size_t errsize)
{
    plan pl;
    const char *title = r->title[0] ? r->title : r->name;
    owf_para *h;
    int got;
    *out = NULL;
    if (plan_make(&pl, p, r, err, errsize)) return -1;
    if (!(pl.doc = owf_doc_new())) { plan_free(&pl); return fail(err, errsize, "Not enough memory"); }
    pl.table_id = -1;
    if ((h = add_para(&pl, &pl.doc->body, 1, OWF_ALIGN_LEFT))) add_text(&pl, h, 0, title);
    if (!pl.failed && h && h->nruns && owf_doc_set_title(pl.doc, h->runs[0].text)) pl.failed = 1;
    if (pl.is_query) {
        got = ob_query_rows(p, pl.query, params, 0, -1, row_cb, &pl);
    } else {
        ob_view v;
        ob_view_init(&v);
        copy(v.sort, sizeof v.sort, r->group[0] ? r->group : r->sort);
        v.desc = r->desc;
        got = ob_view_rows(p, r->table, &v, 0, -1, row_cb, &pl);
    }
    if (got < 0 && !pl.failed) {
        fail(err, errsize, "%s: %s", r->name, ob_error(p));
        owf_doc_free(pl.doc);
        plan_free(&pl);
        return -1;
    }
    if (!pl.failed) {
        if (!pl.rows) {
            if ((h = add_para(&pl, &pl.doc->body, 0, OWF_ALIGN_LEFT))) add_text(&pl, h, 0, "No records.");
        } else if (pl.group_src >= 0) {
            add_totals(&pl, 0);
            if (pl.totals && (h = add_para(&pl, &pl.doc->body, 2, OWF_ALIGN_LEFT)) &&
                !add_text(&pl, h, 0, "Report totals")) {
                start_table(&pl);
                add_totals(&pl, 1);
            }
        } else {
            add_totals(&pl, 1);
        }
    }
    if (!pl.failed) add_footer(&pl);
    if (pl.failed) {
        owf_doc_free(pl.doc);
        plan_free(&pl);
        return fail(err, errsize, "Not enough memory");
    }
    *out = pl.doc;
    plan_free(&pl);
    return 0;
}

int ob_report_save(ob_project *p, const ob_report *r, const char *const *params, const char *path,
                   const char *format, owf_report *notes, char *err, size_t errsize)
{
    owf_doc *doc;
    int rc;
    if (ob_report_build(p, r, params, &doc, err, errsize)) return -1;
    rc = owf_export_file(doc, path, format, notes);
    owf_doc_free(doc);
    if (rc == OWF_OK) return 0;
    if (rc == OWF_ERR_FORMAT || rc == OWF_ERR_UNSUPPORTED)
        return fail(err, errsize, "%s: can't save as %s", r->name, format ? format : path);
    if (rc == OWF_ERR_MEMORY) return fail(err, errsize, "Not enough memory");
    return fail(err, errsize, "%s: could not write %s", r->name, path);
}

/* ---- The text a report is kept as ---- */

#define TEXT_MAGIC "OADBREPORT "
#define TEXT_VERSION 1

typedef struct tout {
    char *out;
    size_t size, n;
    int full;
} tout;

static void t_putc(tout *t, char c)
{
    if (t->n + 1 < t->size) t->out[t->n] = c;
    else t->full = 1;
    t->n++;
}

static void t_puts(tout *t, const char *s)
{
    while (*s) t_putc(t, *s++);
}

static void t_value(tout *t, const char *s)
{
    for (; *s; s++) {
        if (*s == '\\' || *s == '|') { t_putc(t, '\\'); t_putc(t, *s); }
        else if (*s == '\n') { t_putc(t, '\\'); t_putc(t, 'n'); }
        else t_putc(t, *s);
    }
}

static void t_setting(tout *t, const char *key, const char *value)
{
    if (!value[0]) return;
    t_puts(t, key);
    t_putc(t, '=');
    t_value(t, value);
    t_putc(t, '\n');
}

int ob_report_to_text(const ob_report *r, char *out, size_t size)
{
    tout t;
    char num[16];
    int i;
    t.out = out;
    t.size = size;
    t.n = 0;
    t.full = size == 0;
    snprintf(num, sizeof num, "%d", TEXT_VERSION);
    t_puts(&t, TEXT_MAGIC);
    t_puts(&t, num);
    t_putc(&t, '\n');
    t_setting(&t, "title", r->title);
    t_setting(&t, "table", r->table);
    t_setting(&t, "query", r->query);
    t_setting(&t, "sort", r->sort);
    if (r->desc) t_setting(&t, "desc", "1");
    t_setting(&t, "group", r->group);
    for (i = 0; i < r->ncolumns && i < OB_REPORT_COLUMNS; i++) {
        const ob_rcolumn *c = &r->columns[i];
        t_puts(&t, "column=");
        t_value(&t, c->field);
        t_putc(&t, '|');
        t_value(&t, c->heading);
        t_putc(&t, '|');
        t_puts(&t, ob_total_name(c->total));
        t_putc(&t, '\n');
    }
    if (t.full || t.n >= size) {
        if (size) out[0] = 0;
        return -1;
    }
    out[t.n] = 0;
    return (int)t.n;
}

/* The value from *s to the end of the line or (bar) an unescaped '|', unescaped into out (cut at size);
 * *s is left on the '|' or the line's end. */
static void t_take(const char **s, int bar, char *out, size_t size)
{
    const char *p = *s;
    size_t k = 0;
    while (*p && *p != '\n' && !(bar && *p == '|')) {
        char c = *p++;
        if (c == '\\' && *p && *p != '\n') {
            c = *p++;
            if (c == 'n') c = '\n';
        }
        if (k + 1 < size) out[k++] = c;
    }
    if (size) out[k] = 0;
    *s = p;
}

static int total_from_name(const char *name, ob_total *t)
{
    int i;
    for (i = 0; i < OB_TOTALS; i++)
        if (i != OB_TOTAL_GROUP && same_name(name, ob_total_name((ob_total)i))) {
            *t = (ob_total)i;
            return 1;
        }
    return 0;
}

int ob_report_from_text(ob_report *r, const char *name, const char *text, char *err, size_t errsize)
{
    const char *s = text ? text : "";
    int line = 1, version;
    ob_report_init(r, name);
    if (strncmp(s, TEXT_MAGIC, strlen(TEXT_MAGIC)))
        return fail(err, errsize, "%s: not a report", name);
    version = atoi(s + strlen(TEXT_MAGIC));
    if (version > TEXT_VERSION)
        return fail(err, errsize, "%s: made by a newer GFOpenBase (report version %d)", name, version);
    if (version < 1) return fail(err, errsize, "%s: not a report", name);
    s = strchr(s, '\n');
    while (s && *s) {
        char key[16];
        size_t k = 0;
        s++;
        line++;
        if (!*s) break;
        if (*s == '\n') continue;
        while (*s && *s != '=' && *s != '\n' && k + 1 < sizeof key) key[k++] = *s++;
        key[k] = 0;
        if (*s != '=') return fail(err, errsize, "%s: line %d: no setting", name, line);
        s++;
        if (!strcmp(key, "column")) {
            ob_rcolumn *c;
            char total[16];
            if (!(c = ob_report_add_column(r, "", OB_TOTAL_NONE)))
                return fail(err, errsize, "%s: line %d: more than %d columns", name, line, OB_REPORT_COLUMNS);
            t_take(&s, 1, c->field, sizeof c->field);
            if (*s == '|') { s++; t_take(&s, 1, c->heading, sizeof c->heading); }
            total[0] = 0;
            if (*s == '|') { s++; t_take(&s, 1, total, sizeof total); }
            if (total[0] && !total_from_name(total, &c->total))
                return fail(err, errsize, "%s: line %d: no total called %s", name, line, total);
        } else {
            char value[OB_VALUE_MAX], *to = NULL;
            size_t size = 0;
            t_take(&s, 0, value, sizeof value);
            if (!strcmp(key, "title")) to = r->title, size = sizeof r->title;
            else if (!strcmp(key, "table")) to = r->table, size = sizeof r->table;
            else if (!strcmp(key, "query")) to = r->query, size = sizeof r->query;
            else if (!strcmp(key, "sort")) to = r->sort, size = sizeof r->sort;
            else if (!strcmp(key, "group")) to = r->group, size = sizeof r->group;
            else if (!strcmp(key, "desc")) r->desc = atoi(value) != 0;
            if (to) copy(to, size, value);
        }
        s = strchr(s, '\n');
    }
    return 0;
}

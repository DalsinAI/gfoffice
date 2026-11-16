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
    acc lv[OB_REPORT_LEVELS], all;   /* this group's of each level, and the report's */
} rcol;

/* A group level. */
typedef struct level {
    const char *field;      /* the report's name for it */
    int src;                /* its value's index in a row */
    int text;               /* its values compare without case (as a text sort does) */
    char heading[OB_LABEL_MAX];
    char *value;            /* this group's value ("" empty), NULL before the first row */
    ob_int rows;            /* in this group */
} level;

/* The bands, for what they may name. */
enum { B_HEAD, B_FOOT, B_PAGE_HEAD, B_PAGE_FOOT, B_GROUP_HEAD, B_GROUP_FOOT };

typedef struct plan {
    ob_project *p;
    const ob_report *r;
    int is_query;
    ob_table *table;        /* the table's design (heap: about 90 KB) */
    ob_query *query;        /* the saved query (heap: about 24 KB) */
    ob_qresult *qcols;      /* the query's columns */
    int nq;
    int ncols;
    rcol cols[OB_REPORT_COLUMNS];
    int nlevels;
    level lv[OB_REPORT_LEVELS];
    int totals;             /* any column has a total */
    /* building */
    owf_doc *doc;
    int table_id, row;
    ob_int rows;
    int top_groups;         /* first-level groups started */
    char *prev;             /* the last row's values, one after another */
    size_t prevsize;
    const char *prevv[OB_MAX_FIELDS > OB_QUERY_OUT ? OB_MAX_FIELDS : OB_QUERY_OUT];
    int nprev;
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
    int i;
    free(pl->table);
    free(pl->query);
    free(pl->qcols);
    free(pl->prev);
    for (i = 0; i < OB_REPORT_LEVELS; i++) {
        free(pl->lv[i].value);
        pl->lv[i].value = NULL;
    }
    pl->table = NULL;
    pl->query = NULL;
    pl->qcols = NULL;
    pl->prev = NULL;
}

/* Where a source column named `name` is: its index, type and heading; -1 when
 * there is none. */
static int find_source(plan *pl, const char *name, int *type, char *heading, size_t hsize)
{
    int i;
    if (pl->is_query) {
        for (i = 0; i < pl->nq; i++)
            if (same_name(pl->qcols[i].heading, name)) {
                *type = pl->qcols[i].type;
                copy(heading, hsize, pl->qcols[i].heading);
                return i;
            }
        return -1;
    }
    if ((i = ob_table_find_field(pl->table, name)) < 0) return -1;
    *type = pl->table->fields[i].type;
    copy(heading, hsize, pl->table->fields[i].label[0] ? pl->table->fields[i].label : pl->table->fields[i].name);
    return i;
}

static const char *source_name(const plan *pl)
{
    return pl->is_query ? pl->r->query : pl->r->table;
}

/* ---- The bands' text ---- */

static const char *band_name(int kind, int level, char *buf, size_t size)
{
    static const char *const names[] = { "report header", "report footer", "page header", "page footer",
                                         "group header", "group footer" };
    if ((kind == B_GROUP_HEAD || kind == B_GROUP_FOOT) && level > 0)
        snprintf(buf, size, "%s %d", names[kind], level + 1);
    else
        copy(buf, size, names[kind]);
    return buf;
}

/* The name in "[name]" at *s (just past the '['): into name, *s past the ']'. 0 when there is no ']'. */
static int take_name(const char **s, char *name, size_t size)
{
    const char *e = strchr(*s, ']');
    size_t n;
    if (!e) return 0;
    n = (size_t)(e - *s);
    if (n >= size) n = size - 1;
    memcpy(name, *s, n);
    name[n] = 0;
    *s = e + 1;
    return 1;
}

/* Whether a band's [names] are ones it may have: 0, or -1 with the reason. */
static int band_check(plan *pl, const char *t, int kind, int level, char *err, size_t errsize)
{
    char name[OB_NAME_MAX], what[24], heading[OB_LABEL_MAX];
    const char *s = t, *start;
    int page = kind == B_PAGE_HEAD || kind == B_PAGE_FOOT;
    int group = kind == B_GROUP_HEAD || kind == B_GROUP_FOOT;
    int type;
    band_name(kind, level, what, sizeof what);
    while ((s = strchr(s, '['))) {
        s++;
        if (*s == '[') { s++; continue; }
        start = s;
        if (!take_name(&s, name, sizeof name)) {
            const char *nl = strchr(start, '\n');
            int n = nl ? (int)(nl - start) : (int)strlen(start);
            return fail(err, errsize, "%s: %s: no ] after [%.*s", pl->r->name, what, n > 40 ? 40 : n, start);
        }
        if (same_name(name, "Title") || same_name(name, "Date")) continue;
        if (same_name(name, "Page") || same_name(name, "Pages")) {
            if (page) continue;
            return fail(err, errsize, "%s: %s: [%s]: only in a page header or footer", pl->r->name, what, name);
        }
        if (same_name(name, "Group") || same_name(name, "Value")) {
            if (group) continue;
            return fail(err, errsize, "%s: %s: [%s]: only in a group's header or footer", pl->r->name, what, name);
        }
        if (same_name(name, "Count")) {
            if (kind == B_FOOT || kind == B_GROUP_FOOT) continue;
            return fail(err, errsize, "%s: %s: [Count]: only in a footer, after the rows", pl->r->name, what);
        }
        if (page) return fail(err, errsize, "%s: %s: [%s]: a page shows no row", pl->r->name, what, name);
        if (!group)
            return fail(err, errsize, "%s: %s: [%s]: a field only in a group's header or footer", pl->r->name, what,
                        name);
        if (find_source(pl, name, &type, heading, sizeof heading) < 0)
            return fail(err, errsize, "%s: %s: %s has no %s %s", pl->r->name, what, source_name(pl),
                        pl->is_query ? "column" : "field", name);
    }
    return 0;
}

/* A query's grid sorts by its levels first, in order: "" or what it must sort by ("Region, then Town"). */
static int col_is(const ob_qcolumn *c, const level *l)
{
    return same_name(c->field, l->field) || (c->heading[0] && same_name(c->heading, l->field)) ||
           same_name(c->field, l->heading) || (c->heading[0] && same_name(c->heading, l->heading));
}

/* Reads r's source and finds its columns, levels and bands' names: 0, or -1 with the reason. */
static int plan_make(plan *pl, ob_project *p, const ob_report *r, char *err, size_t errsize)
{
    static const char *const total_names = "Sum, Count, Average, Min or Max (or none)";
    const char *source, *names[OB_REPORT_LEVELS];
    int i, k, rc = -1;
    memset(pl, 0, sizeof *pl);
    pl->p = p;
    pl->r = r;
    if (!r->table[0] && !r->query[0]) return fail(err, errsize, "%s: needs a table or a query", r->name);
    if (r->table[0] && r->query[0]) return fail(err, errsize, "%s: a table or a query, not both", r->name);
    if (r->ncolumns <= 0) return fail(err, errsize, "%s: no columns", r->name);
    if (r->ncolumns > OB_REPORT_COLUMNS) return fail(err, errsize, "%s: too many columns", r->name);
    if (r->subgroup[1][0] && !r->subgroup[0][0]) return fail(err, errsize, "%s: a third group needs a second", r->name);
    if (r->subgroup[0][0] && !r->group[0]) return fail(err, errsize, "%s: a second group needs a first", r->name);
    pl->is_query = r->query[0] != 0;
    source = pl->is_query ? r->query : r->table;
    if (pl->is_query) {
        if (r->sort[0]) return fail(err, errsize, "%s: a query's rows come in its own order (sort them there)", r->name);
        if (!(pl->query = malloc(sizeof *pl->query)) || !(pl->qcols = malloc(sizeof *pl->qcols * OB_QUERY_OUT))) {
            fail(err, errsize, "Not enough memory");
            goto out;
        }
        if (!ob_query_get(p, r->query, pl->query)) {
            fail(err, errsize, "%s: no query %s", r->name, r->query);
            goto out;
        }
        if ((pl->nq = ob_query_columns(p, pl->query, pl->qcols, OB_QUERY_OUT)) < 0) {
            fail(err, errsize, "%s: %s", r->name, ob_error(p));
            goto out;
        }
        if (pl->nq > OB_QUERY_OUT) pl->nq = OB_QUERY_OUT;
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
        rc_->src = find_source(pl, c->field, &rc_->type, rc_->heading, sizeof rc_->heading);
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
    names[0] = r->group;
    names[1] = r->subgroup[0];
    names[2] = r->subgroup[1];
    for (k = 0; k < OB_REPORT_LEVELS && names[k][0]; k++) {
        level *l = &pl->lv[k];
        int type;
        l->field = names[k];
        l->src = find_source(pl, names[k], &type, l->heading, sizeof l->heading);
        if (l->src < 0) {
            fail(err, errsize, "%s: group by %s: %s has no %s %s", r->name, names[k], source,
                 pl->is_query ? "column" : "field", names[k]);
            goto out;
        }
        l->text = type == OB_TEXT || type == OB_MEMO || type == OB_CHOICE || type == -1;
    }
    pl->nlevels = k;
    /* Groups are made as the rows come, so a query's must come sorted by its levels first, in order. A grid
     * query says so in its sorted columns; an SQL query's ORDER BY is the caller's. */
    if (pl->is_query && !pl->query->sql_only && pl->nlevels) {
        const ob_qcolumn *sorted[OB_REPORT_LEVELS];
        int ns = 0;
        for (i = 0; i < pl->query->ncolumns && ns < pl->nlevels; i++)
            if (pl->query->columns[i].sort != OB_SORT_NONE) sorted[ns++] = &pl->query->columns[i];
        for (k = 0; k < pl->nlevels; k++)
            if (k >= ns || !col_is(sorted[k], &pl->lv[k])) {
                char list[OB_REPORT_LEVELS * (OB_NAME_MAX + 8)] = "";
                for (i = 0; i <= k; i++) {
                    if (i) strcat(list, ", then ");
                    strcat(list, pl->lv[i].field);
                }
                fail(err, errsize, "%s: group by %s: sort the query %s by %s%s first", r->name, pl->lv[k].field,
                     r->query, list, k ? "," : "");
                goto out;
            }
    }
    if (band_check(pl, r->head, B_HEAD, 0, err, errsize) || band_check(pl, r->foot, B_FOOT, 0, err, errsize) ||
        band_check(pl, r->page_head, B_PAGE_HEAD, 0, err, errsize) ||
        band_check(pl, r->page_foot, B_PAGE_FOOT, 0, err, errsize))
        goto out;
    for (k = 0; k < OB_REPORT_LEVELS; k++) {
        if ((r->group_head[k][0] || r->group_foot[k][0]) && k >= pl->nlevels) {
            char what[24];
            fail(err, errsize, "%s: %s: there is no group %d", r->name,
                 band_name(r->group_head[k][0] ? B_GROUP_HEAD : B_GROUP_FOOT, k, what, sizeof what), k + 1);
            goto out;
        }
        if (band_check(pl, r->group_head[k], B_GROUP_HEAD, k, err, errsize) ||
            band_check(pl, r->group_foot[k], B_GROUP_FOOT, k, err, errsize))
            goto out;
    }
    rc = 0;
out:
    if (rc) plan_free(pl);
    return rc;
}

int ob_report_check(ob_project *p, const ob_report *r, char *err, size_t errsize)
{
    plan *pl = malloc(sizeof *pl);      /* about 7 KB: not on an Amiga stack */
    int rc;
    if (!pl) return fail(err, errsize, "Not enough memory");
    rc = plan_make(pl, p, r, err, errsize);
    if (!rc) plan_free(pl);
    free(pl);
    return rc;
}

/* ---- Building the document ---- */

/* ISO-8859-1 to UTF-8, in a buffer of the caller's (out) or the heap; NULL when out of memory. */
static char *to_utf8(const char *latin1, char *out, size_t size, size_t *len)
{
    const unsigned char *s = (const unsigned char *)(latin1 ? latin1 : "");
    size_t n = strlen((const char *)s), k = 0;
    char *buf = n * 2 < size ? out : malloc(n * 2 + 1);
    if (!buf) return NULL;
    for (; *s; s++) {
        if (*s < 0x80) {
            buf[k++] = (char)*s;
        } else {
            buf[k++] = (char)(0xC0 | (*s >> 6));
            buf[k++] = (char)(0x80 | (*s & 0x3F));
        }
    }
    buf[k] = 0;
    *len = k;
    return buf;
}

/* latin1 added to para. */
static int add_text(plan *pl, owf_para *para, int bold, const char *latin1)
{
    owf_charfmt cf;
    char small[256], *buf;
    size_t k;
    int rc;
    if (!latin1 || !*latin1) return 0;
    if (!(buf = to_utf8(latin1, small, sizeof small, &k))) { pl->failed = 1; return -1; }
    owf_charfmt_init(&cf);
    if (bold) cf.flags |= OWF_BOLD;
    rc = owf_para_add_text(para, &cf, buf, k);
    if (buf != small) free(buf);
    if (rc) pl->failed = 1;
    return rc;
}

static int add_field(plan *pl, owf_para *para, owf_field field)
{
    owf_charfmt cf;
    owf_charfmt_init(&cf);
    if (owf_para_add_special(para, &cf, OWF_RUN_FIELD, field)) { pl->failed = 1; return -1; }
    return 0;
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

static const char *title_of(const ob_report *r)
{
    return r->title[0] ? r->title : r->name;
}

/* A band's text into para: its [names] filled in. level: the group level for [Group] and [Value] (-1 none);
 * row (n values): the row for fields; count: [Count]. The names were checked by band_check. */
static void band(plan *pl, owf_para *para, const char *t, int level, const char *const *row, int n, ob_int count)
{
    char lit[OB_BAND_MAX], name[OB_NAME_MAX], heading[OB_LABEL_MAX], num[24];
    size_t k = 0;
    int type, i;
    while (*t && !pl->failed) {
        if (*t != '[' || t[1] == '[') {
            if (*t == '[') t++;
            if (k + 1 < sizeof lit) lit[k++] = *t;
            t++;
            continue;
        }
        t++;
        if (!take_name(&t, name, sizeof name)) break;
        lit[k] = 0;
        add_text(pl, para, 0, lit);
        k = 0;
        if (same_name(name, "Title")) add_text(pl, para, 0, title_of(pl->r));
        else if (same_name(name, "Date")) add_field(pl, para, OWF_FIELD_DATE);
        else if (same_name(name, "Page")) add_field(pl, para, OWF_FIELD_PAGE);
        else if (same_name(name, "Pages")) add_field(pl, para, OWF_FIELD_PAGES);
        else if (same_name(name, "Group") && level >= 0) add_text(pl, para, 0, pl->lv[level].heading);
        else if (same_name(name, "Value") && level >= 0)
            add_text(pl, para, 0, pl->lv[level].value && pl->lv[level].value[0] ? pl->lv[level].value : "(none)");
        else if (same_name(name, "Count")) {
            snprintf(num, sizeof num, "%lld", (long long)count);
            add_text(pl, para, 0, num);
        } else if (row && (i = find_source(pl, name, &type, heading, sizeof heading)) >= 0 && i < n)
            add_text(pl, para, 0, row[i]);
    }
    lit[k] = 0;
    add_text(pl, para, 0, lit);
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

/* A totals row in the open table: level's (-1 the report's). */
static void add_totals(plan *pl, int level)
{
    char cells[OB_REPORT_COLUMNS][96];
    const char *text[OB_REPORT_COLUMNS];
    int i;
    if (!pl->totals) return;
    for (i = 0; i < pl->ncols; i++) {
        acc_text(&pl->cols[i], level < 0 ? &pl->cols[i].all : &pl->cols[i].lv[level], cells[i], sizeof cells[i]);
        text[i] = cells[i][0] ? cells[i] : NULL;
    }
    add_row(pl, text, 1);
}

static int same_group(const level *l, const char *a, const char *b)
{
    return l->text ? same_name(a, b) : !strcmp(a, b);
}

/* A group of level L starts with this row: its header, and a table when it is the last level. */
static void open_level(plan *pl, int L, int n, const char *const *values)
{
    level *l = &pl->lv[L];
    const char *g = l->src < n && values[l->src] ? values[l->src] : "";
    owf_para *h;
    int i;
    free(l->value);
    if (!(l->value = malloc(strlen(g) + 1))) { pl->failed = 1; return; }
    strcpy(l->value, g);
    l->rows = 0;
    for (i = 0; i < pl->ncols; i++) acc_clear(&pl->cols[i].lv[L]);
    if (!(h = add_para(pl, &pl->doc->body, L + 2, OWF_ALIGN_LEFT))) return;
    if (L == 0 && pl->r->new_page && pl->top_groups) h->fmt.page_break_before = 1;
    if (L == 0) pl->top_groups++;
    band(pl, h, pl->r->group_head[L][0] ? pl->r->group_head[L] : "[Group]: [Value]", L, values, n, 0);
    if (L == pl->nlevels - 1) start_table(pl);
}

/* A group of level L ends (the last row was the previous one): its totals, then its footer. */
static void close_level(plan *pl, int L)
{
    level *l = &pl->lv[L];
    owf_para *f;
    if (pl->totals && !(pl->r->no_totals & (1 << L))) {
        if (L < pl->nlevels - 1) {          /* the last level's totals end its table */
            if ((f = add_para(pl, &pl->doc->body, 0, OWF_ALIGN_LEFT)) &&
                !add_text(pl, f, 1, "Totals for ") && !add_text(pl, f, 1, l->heading) && !add_text(pl, f, 1, ": "))
                add_text(pl, f, 1, l->value && l->value[0] ? l->value : "(none)");
            start_table(pl);
        }
        add_totals(pl, L);
    }
    if (pl->r->group_foot[L][0] && (f = add_para(pl, &pl->doc->body, 0, OWF_ALIGN_LEFT)))
        band(pl, f, pl->r->group_foot[L], L, pl->prevv, pl->nprev, l->rows);
}

/* Keeps a copy of the row, for the footers. */
static void keep_row(plan *pl, int n, const char *const *values)
{
    size_t need = 0, at = 0;
    int i;
    if (n > (int)(sizeof pl->prevv / sizeof pl->prevv[0])) n = (int)(sizeof pl->prevv / sizeof pl->prevv[0]);
    for (i = 0; i < n; i++) need += (values[i] ? strlen(values[i]) : 0) + 1;
    if (need > pl->prevsize) {
        char *b = realloc(pl->prev, need);
        if (!b) { pl->failed = 1; return; }
        pl->prev = b;
        pl->prevsize = need;
    }
    for (i = 0; i < n; i++) {
        const char *v = values[i] ? values[i] : "";
        size_t len = strlen(v);
        memcpy(pl->prev + at, v, len + 1);
        pl->prevv[i] = pl->prev + at;
        at += len + 1;
    }
    pl->nprev = n;
}

static int row_cb(void *user, ob_int key, int n, const char *const *values)
{
    plan *pl = user;
    const char *text[OB_REPORT_COLUMNS];
    int i, L;
    (void)key;
    if (pl->nlevels) {
        int changed = pl->nlevels;
        for (L = 0; L < pl->nlevels && changed == pl->nlevels; L++) {
            const level *l = &pl->lv[L];
            const char *g = l->src < n && values[l->src] ? values[l->src] : "";
            if (!l->value || !same_group(l, l->value, g)) changed = L;
        }
        if (changed < pl->nlevels) {
            if (pl->rows)
                for (L = pl->nlevels - 1; L >= changed && !pl->failed; L--) close_level(pl, L);
            for (L = changed; L < pl->nlevels && !pl->failed; L++) open_level(pl, L, n, values);
        }
    } else if (!pl->rows) {
        start_table(pl);
    }
    for (i = 0; i < pl->ncols; i++) {
        rcol *c = &pl->cols[i];
        const char *v = c->src < n ? values[c->src] : NULL;
        text[i] = v;
        for (L = 0; L < pl->nlevels; L++) acc_add(&c->lv[L], c->type, v);
        acc_add(&c->all, c->type, v);
    }
    for (L = 0; L < pl->nlevels; L++) pl->lv[L].rows++;
    add_row(pl, text, 0);
    pl->rows++;
    keep_row(pl, n, values);
    return pl->failed;
}

static void add_page_bands(plan *pl)
{
    owf_para *f;
    if (pl->r->page_head[0] && (f = add_para(pl, &pl->doc->header, 0, OWF_ALIGN_LEFT)))
        band(pl, f, pl->r->page_head, -1, NULL, 0, 0);
    if ((f = add_para(pl, &pl->doc->footer, 0, OWF_ALIGN_CENTRE)))
        band(pl, f, pl->r->page_foot[0] ? pl->r->page_foot : "Page [Page] of [Pages]", -1, NULL, 0, 0);
}

int ob_report_build(ob_project *p, const ob_report *r, const char *const *params, owf_doc **out,
                    char *err, size_t errsize)
{
    plan *pl = malloc(sizeof *pl);      /* about 7 KB: not on an Amiga stack */
    owf_para *h;
    char small[256], *t;
    size_t k;
    int got, L;
    *out = NULL;
    if (!pl) return fail(err, errsize, "Not enough memory");
    if (plan_make(pl, p, r, err, errsize)) { free(pl); return -1; }
    if (!(pl->doc = owf_doc_new())) { plan_free(pl); free(pl); return fail(err, errsize, "Not enough memory"); }
    pl->table_id = -1;
    if ((h = add_para(pl, &pl->doc->body, 1, OWF_ALIGN_LEFT))) band(pl, h, r->head[0] ? r->head : "[Title]", -1, NULL, 0, 0);
    if (!(t = to_utf8(title_of(r), small, sizeof small, &k)) || owf_doc_set_title(pl->doc, t)) pl->failed = 1;
    if (t && t != small) free(t);
    if (!pl->failed) {
        if (pl->is_query) {
            got = ob_query_rows(p, pl->query, params, 0, -1, row_cb, pl);
        } else {
            ob_view v;
            ob_view_init(&v);
            copy(v.sort, sizeof v.sort, pl->nlevels ? r->group : r->sort);
            for (L = 1; L < pl->nlevels; L++) copy(v.then[L - 1], sizeof v.then[L - 1], pl->lv[L].field);
            v.desc = r->desc;
            got = ob_view_rows(p, r->table, &v, 0, -1, row_cb, pl);
        }
        if (got < 0 && !pl->failed) {
            fail(err, errsize, "%s: %s", r->name, ob_error(p));
            owf_doc_free(pl->doc);
            plan_free(pl);
            free(pl);
            return -1;
        }
    }
    if (!pl->failed) {
        if (!pl->rows) {
            if ((h = add_para(pl, &pl->doc->body, 0, OWF_ALIGN_LEFT))) add_text(pl, h, 0, "No records.");
        } else if (pl->nlevels) {
            for (L = pl->nlevels - 1; L >= 0 && !pl->failed; L--) close_level(pl, L);
            if (pl->totals && (h = add_para(pl, &pl->doc->body, 2, OWF_ALIGN_LEFT)) &&
                !add_text(pl, h, 0, "Report totals")) {
                start_table(pl);
                add_totals(pl, -1);
            }
        } else {
            add_totals(pl, -1);
        }
    }
    if (!pl->failed && r->foot[0] && (h = add_para(pl, &pl->doc->body, 0, OWF_ALIGN_LEFT)))
        band(pl, h, r->foot, -1, pl->prevv, pl->nprev, pl->rows);
    if (!pl->failed) add_page_bands(pl);
    if (pl->failed) {
        owf_doc_free(pl->doc);
        plan_free(pl);
        free(pl);
        return fail(err, errsize, "Not enough memory");
    }
    *out = pl->doc;
    plan_free(pl);
    free(pl);
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
#define TEXT_VERSION 2          /* 2 only when a 0.2.0 setting is used */

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

/* Whether r uses a 0.2.0 setting (so its text is version 2). */
static int uses_design2(const ob_report *r)
{
    int i;
    if (r->subgroup[0][0] || r->subgroup[1][0] || r->head[0] || r->foot[0] || r->page_head[0] || r->page_foot[0] ||
        r->no_totals || r->new_page)
        return 1;
    for (i = 0; i < OB_REPORT_LEVELS; i++)
        if (r->group_head[i][0] || r->group_foot[i][0]) return 1;
    return 0;
}

static void t_level(tout *t, const char *key, int level, const char *value)
{
    if (!value[0]) return;
    t_puts(t, key);
    t_putc(t, '=');
    t_putc(t, (char)('1' + level));
    t_putc(t, '|');
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
    snprintf(num, sizeof num, "%d", uses_design2(r) ? 2 : 1);
    t_puts(&t, TEXT_MAGIC);
    t_puts(&t, num);
    t_putc(&t, '\n');
    t_setting(&t, "title", r->title);
    t_setting(&t, "table", r->table);
    t_setting(&t, "query", r->query);
    t_setting(&t, "sort", r->sort);
    if (r->desc) t_setting(&t, "desc", "1");
    t_setting(&t, "group", r->group);
    for (i = 0; i < OB_REPORT_LEVELS - 1; i++) t_setting(&t, "subgroup", r->subgroup[i]);
    t_setting(&t, "head", r->head);
    t_setting(&t, "foot", r->foot);
    t_setting(&t, "pagehead", r->page_head);
    t_setting(&t, "pagefoot", r->page_foot);
    for (i = 0; i < OB_REPORT_LEVELS; i++) {
        t_level(&t, "grouphead", i, r->group_head[i]);
        t_level(&t, "groupfoot", i, r->group_foot[i]);
    }
    if (r->no_totals) {
        snprintf(num, sizeof num, "%d", r->no_totals);
        t_setting(&t, "nototals", num);
    }
    if (r->new_page) t_setting(&t, "newpage", "1");
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
        } else if (!strcmp(key, "grouphead") || !strcmp(key, "groupfoot")) {
            char lv[8];
            int L;
            t_take(&s, 1, lv, sizeof lv);
            L = atoi(lv) - 1;
            if (*s != '|' || L < 0 || L >= OB_REPORT_LEVELS || lv[1])
                return fail(err, errsize, "%s: line %d: no group level %s", name, line, lv);
            s++;
            if (key[5] == 'h') t_take(&s, 0, r->group_head[L], sizeof r->group_head[L]);
            else t_take(&s, 0, r->group_foot[L], sizeof r->group_foot[L]);
        } else if (!strcmp(key, "subgroup")) {
            int L;
            for (L = 0; L < OB_REPORT_LEVELS - 1 && r->subgroup[L][0]; L++) ;
            if (L == OB_REPORT_LEVELS - 1)
                return fail(err, errsize, "%s: line %d: more than %d group levels", name, line, OB_REPORT_LEVELS);
            t_take(&s, 0, r->subgroup[L], sizeof r->subgroup[L]);
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
            else if (!strcmp(key, "head")) to = r->head, size = sizeof r->head;
            else if (!strcmp(key, "foot")) to = r->foot, size = sizeof r->foot;
            else if (!strcmp(key, "pagehead")) to = r->page_head, size = sizeof r->page_head;
            else if (!strcmp(key, "pagefoot")) to = r->page_foot, size = sizeof r->page_foot;
            else if (!strcmp(key, "nototals")) r->no_totals = atoi(value);
            else if (!strcmp(key, "newpage")) r->new_page = atoi(value) != 0;
            if (to) copy(to, size, value);
        }
        s = strchr(s, '\n');
    }
    return 0;
}

/* ---- Reports kept in the project (oadb_reports, through the core) ---- */

int ob_report_store(ob_project *p, const ob_report *r, char *err, size_t errsize)
{
    static char text[OB_REPORT_TEXT_MAX];
    if (ob_report_check(p, r, err, errsize)) return -1;
    if (ob_report_to_text(r, text, sizeof text) < 0) return fail(err, errsize, "%s: too big to keep", r->name);
    if (!ob_report_put_text(p, r->name, text)) return fail(err, errsize, "%s", ob_error(p));
    return 0;
}

int ob_report_load(ob_project *p, const char *name, ob_report *r, char *err, size_t errsize)
{
    static char text[OB_REPORT_TEXT_MAX];
    const char *kept = name;
    int i, n = ob_report_count(p);
    for (i = 0; i < n; i++)          /* the name as it was kept, not as it was asked for */
        if (ob_report_name(p, i) && same_name(ob_report_name(p, i), name)) { kept = ob_report_name(p, i); break; }
    if (!ob_report_get_text(p, name, text, sizeof text)) return fail(err, errsize, "%s", ob_error(p));
    return ob_report_from_text(r, kept, text, err, errsize);
}

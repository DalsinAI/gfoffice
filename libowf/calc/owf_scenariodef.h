/*
 * libowf: the code of scenarios (owf_scenario.h). Included after
 * owf_pivotdef.h, whose cell and sheet readers and column names it uses, by
 * libowf/src/owf_book.c and by GFOpenCalc's src/calc/oc_pivotdef.c when the
 * core runs on owfcalc.library. The includer defines
 * OWF_PIVOT_NUMBER(text, length, double *) as for owf_pivotdef.h. Only
 * owf.h's calls: no C library beyond string work and snprintf.
 *
 * _GF_SCENARIO_k's text, inside quotes: "1;n=Best case;c=;a=1;v=10,20,%3DA1*2"
 * (its name, comment, whether it was shown last, and its cells' inputs in
 * order; %XX for control characters and % " ; , =).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_scenario.h"

#include <stdio.h>
#include <string.h>

int owf_scenario_is_name(const char *name)
{
    return name && !strncmp(name, OWF_SCENARIO_PREFIX, sizeof OWF_SCENARIO_PREFIX - 1);
}

static char *scen_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = owf_malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

static int scen_find(const owf_book *book, int sheet, const char *name)
{
    int i;
    for (i = 0; i < book->nnames; i++)
        if (book->names[i].sheet == sheet && book->names[i].name && !strcmp(book->names[i].name, name))
            return i;
    return -1;
}

int owf_scenario_count(const owf_book *book, int sheet)
{
    char name[40];
    int k;
    if (!book)
        return 0;
    for (k = 0; k < OWF_SCENARIOS; k++) {
        snprintf(name, sizeof name, OWF_SCENARIO_PREFIX "%d", k + 1);
        if (scen_find(book, sheet, name) < 0)
            break;
    }
    return k;
}

unsigned owf_scenario_cells(const owf_scenario *sc)
{
    unsigned long n = 0;
    unsigned i;
    for (i = 0; i < sc->nranges && i < OWF_SCENARIO_RANGES; i++) {
        const owf_range *r = &sc->range[i];
        if (r->last_row < r->first_row || r->last_col < r->first_col)
            return 0;
        n += (unsigned long)(r->last_row - r->first_row + 1u) * (r->last_col - r->first_col + 1u);
        if (n > OWF_SCENARIO_CELLS)
            return OWF_SCENARIO_CELLS + 1u;
    }
    return (unsigned)n;
}

void owf_scenario_free(owf_scenario *sc)
{
    unsigned i;
    if (!sc)
        return;
    if (sc->value)
        for (i = 0; i < sc->ncells; i++)
            owf_free(sc->value[i]);
    owf_free(sc->value);
    sc->value = NULL;
    sc->ncells = 0;
}

static int scen_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* One item at *p up to ; , or the end, unescaped into a new string (or into out when size). */
static char *scen_item(const char **p, char *out, size_t size)
{
    const char *s = *p, *e = s;
    size_t n = 0;
    char *d;
    while (*e && *e != ';' && *e != ',' && *e != '"')
        e++;
    d = size ? out : owf_malloc((size_t)(e - s) + 1u);
    if (!d)
        return NULL;
    if (!size)
        size = (size_t)(e - s) + 1u;
    while (s < e) {
        int c = (unsigned char)*s++;
        if (c == '%' && e - s >= 2 && scen_hex(s[0]) >= 0 && scen_hex(s[1]) >= 0) {
            c = scen_hex(s[0]) * 16 + scen_hex(s[1]);
            s += 2;
        }
        if (n + 1 < size)
            d[n++] = (char)c;
    }
    d[n] = 0;
    *p = e;
    return d;
}

int owf_scenario_get(const owf_book *book, int sheet, int index, owf_scenario *sc)
{
    char name[40];
    const char *p;
    int at, k, ok = 0;
    unsigned want, got = 0;
    if (!book || !sc)
        return 0;
    memset(sc, 0, sizeof *sc);
    if (index < 0 || index >= owf_scenario_count(book, sheet))
        return 0;
    snprintf(name, sizeof name, OWF_SCENARIO_PREFIX "%d", index + 1);
    at = scen_find(book, sheet, name);
    for (k = 0; k < OWF_SCENARIO_RANGES; k++) {
        int i, s;
        snprintf(name, sizeof name, OWF_SCENARIO_PREFIX "%d_R%d", index + 1, k + 1);
        if ((i = scen_find(book, sheet, name)) < 0)
            break;
        if (!book->names[i].formula || !owf_pivot_parse_source(book, book->names[i].formula, &s, &sc->range[k]) ||
            s != sheet)
            return 0;
        sc->nranges++;
    }
    want = owf_scenario_cells(sc);
    if (!sc->nranges || !want || want > OWF_SCENARIO_CELLS || at < 0 || !book->names[at].formula)
        return 0;
    p = book->names[at].formula;
    while (*p == '=' || *p == ' ')
        p++;
    if (*p++ != '"' || *p != '1')
        return 0;
    sc->value = owf_malloc(want * sizeof *sc->value);
    if (!sc->value)
        return 0;
    sc->ncells = 0;
    while (*p && *p != '"') {
        if (*p == ';') {
            p++;
            continue;
        }
        if (!strncmp(p, "n=", 2)) {
            p += 2;
            scen_item(&p, sc->name, sizeof sc->name);
        } else if (!strncmp(p, "c=", 2)) {
            p += 2;
            scen_item(&p, sc->comment, sizeof sc->comment);
        } else if (!strncmp(p, "a=", 2)) {
            sc->active = p[2] == '1';
            p += 2;
            while (*p && *p != ';' && *p != '"')
                p++;
        } else if (!strncmp(p, "v=", 2)) {
            p += 2;
            for (;;) {
                char *v;
                if (got >= want)
                    goto done;
                v = scen_item(&p, NULL, 0);
                if (!v)
                    goto done;
                sc->value[got++] = v;
                sc->ncells = got;
                if (*p != ',')
                    break;
                p++;
            }
        } else {
            while (*p && *p != ';' && *p != '"')   /* a key from a later version */
                p++;
        }
    }
    ok = got == want && sc->name[0];
done:
    if (!ok)
        owf_scenario_free(sc);
    return ok;
}

typedef struct {
    char *s;
    size_t n, cap;
    int failed;
} scen_buf;

static void scen_put(scen_buf *b, const char *s, size_t n)
{
    if (b->failed)
        return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        char *more;
        while (cap < b->n + n + 1)
            cap *= 2;
        more = owf_realloc(b->s, cap);
        if (!more) {
            b->failed = 1;
            return;
        }
        b->s = more;
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

static void scen_escaped(scen_buf *b, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7F || c == '%' || c == '"' || c == ';' || c == ',' || c == '=') {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", c);
            scen_put(b, hex, 3);
        } else {
            scen_put(b, (const char *)&c, 1);
        }
    }
}

static int scen_set(owf_book *book, int sheet, const char *name, char *formula)
{
    int i = scen_find(book, sheet, name);
    if (i >= 0) {
        owf_free(book->names[i].formula);
        book->names[i].formula = formula;
        book->names[i].hidden = 1;
        return OWF_OK;
    }
    i = owf_book_add_name(book, name, sheet, formula, 1);
    owf_free(formula);
    return i < 0 ? OWF_ERR_MEMORY : OWF_OK;
}

static void scen_drop(owf_book *book, int at)
{
    owf_free(book->names[at].name);
    owf_free(book->names[at].formula);
    memmove(&book->names[at], &book->names[at + 1], (size_t)(book->nnames - at - 1) * sizeof book->names[0]);
    book->nnames--;
}

int owf_scenario_put(owf_book *book, int sheet, int index, const owf_scenario *sc)
{
    char name[40], ref[600];
    scen_buf b;
    unsigned i;
    int k, rc, count;
    if (!book || !sc || sheet < 0 || sheet >= book->nsheets || !sc->name[0] || !sc->nranges ||
        sc->nranges > OWF_SCENARIO_RANGES || owf_scenario_cells(sc) != sc->ncells || !sc->ncells ||
        sc->ncells > OWF_SCENARIO_CELLS || !sc->value)
        return OWF_ERR_FORMAT;
    count = owf_scenario_count(book, sheet);
    if (index < 0 || index > count || index >= OWF_SCENARIOS)
        return OWF_ERR_FORMAT;
    memset(&b, 0, sizeof b);
    scen_put(&b, "\"1;n=", 5);
    scen_escaped(&b, sc->name);
    scen_put(&b, ";c=", 3);
    scen_escaped(&b, sc->comment);
    scen_put(&b, sc->active ? ";a=1;v=" : ";a=0;v=", 7);
    for (i = 0; i < sc->ncells; i++) {
        if (i)
            scen_put(&b, ",", 1);
        scen_escaped(&b, sc->value[i] ? sc->value[i] : "");
    }
    scen_put(&b, "\"", 1);
    if (b.failed) {
        owf_free(b.s);
        return OWF_ERR_MEMORY;
    }
    snprintf(name, sizeof name, OWF_SCENARIO_PREFIX "%d", index + 1);
    if ((rc = scen_set(book, sheet, name, b.s)) != OWF_OK)
        return rc;
    for (k = 0; k < OWF_SCENARIO_RANGES; k++) {
        snprintf(name, sizeof name, OWF_SCENARIO_PREFIX "%d_R%d", index + 1, k + 1);
        if ((unsigned)k < sc->nranges) {
            owf_pivotdef d;
            char *copy;
            owf_pivot_init(&d);
            d.source_sheet = sheet;
            d.source = sc->range[k];
            if (!owf_pivot_source_text(book, &d, ref, sizeof ref))
                return OWF_ERR_FORMAT;
            if (!(copy = scen_strdup(ref)))
                return OWF_ERR_MEMORY;
            if ((rc = scen_set(book, sheet, name, copy)) != OWF_OK)
                return rc;
        } else {
            int at = scen_find(book, sheet, name);
            if (at >= 0)
                scen_drop(book, at);
        }
    }
    return OWF_OK;
}

int owf_scenario_remove(owf_book *book, int sheet, int index)
{
    char name[40], prefix[40];
    int count, i, k;
    if (!book)
        return OWF_ERR_FORMAT;
    count = owf_scenario_count(book, sheet);
    if (index < 0 || index >= count)
        return OWF_ERR_FORMAT;
    /* its names go; then every later one's names are numbered one less */
    snprintf(prefix, sizeof prefix, OWF_SCENARIO_PREFIX "%d", index + 1);
    for (i = 0; i < book->nnames;) {
        const char *nm = book->names[i].name;
        size_t n = strlen(prefix);
        if (book->names[i].sheet == sheet && nm && !strncmp(nm, prefix, n) && (!nm[n] || nm[n] == '_'))
            scen_drop(book, i);
        else
            i++;
    }
    for (k = index + 2; k <= count; k++) {
        size_t n;
        snprintf(prefix, sizeof prefix, OWF_SCENARIO_PREFIX "%d", k);
        n = strlen(prefix);
        for (i = 0; i < book->nnames; i++) {
            const char *nm = book->names[i].name;
            char *renamed;
            if (book->names[i].sheet != sheet || !nm || strncmp(nm, prefix, n) || (nm[n] && nm[n] != '_'))
                continue;
            snprintf(name, sizeof name, OWF_SCENARIO_PREFIX "%d%s", k - 1, nm + n);
            if (!(renamed = scen_strdup(name)))
                return OWF_ERR_MEMORY;
            owf_free(book->names[i].name);
            book->names[i].name = renamed;
        }
    }
    return OWF_OK;
}

static int scen_reads_as_other(const char *s)
{
    double v;
    size_t n = strlen(s);
    if (!n)
        return 0;
    if (*s == '=' || *s == '\'' || *s == '+' || *s == '-' || *s == '#')
        return 1;
    if (OWF_PIVOT_NUMBER(s, n, &v))
        return 1;
    return (n == 4 && pivot_same_word("true", s, 4)) || (n == 5 && pivot_same_word("false", s, 5));
}

char *owf_scenario_input(const owf_cell *cell)
{
    char number[48], *out;
    if (!cell || (cell->type == OWF_VALUE_EMPTY && !cell->formula))
        return scen_strdup("");
    if (cell->formula) {
        size_t n = strlen(cell->formula);
        out = owf_malloc(n + 2);
        if (out) {
            out[0] = '=';
            memcpy(out + 1, cell->formula, n + 1);
        }
        return out;
    }
    if (cell->type == OWF_VALUE_NUMBER) {
        owf_number_text(cell->number, number);
        return scen_strdup(number);
    }
    if (cell->type == OWF_VALUE_BOOL)
        return scen_strdup(cell->number != 0.0 ? "TRUE" : "FALSE");
    if (cell->type == OWF_VALUE_TEXT && cell->text) {
        size_t n = strlen(cell->text);
        if (!scen_reads_as_other(cell->text))
            return scen_strdup(cell->text);
        out = owf_malloc(n + 2);
        if (out) {
            out[0] = '\'';
            memcpy(out + 1, cell->text, n + 1);
        }
        return out;
    }
    return scen_strdup("");
}

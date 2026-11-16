/*
 * A mail merge's data source as a table: CSV, semicolon- or tab-separated
 * text (UTF-8, or Latin-1 made UTF-8), its first row the columns' names.
 * GFOpenWrite's mail merge (editor.c, 2.5.0) and GFOpenPublisher's use it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ow_merge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_bytes(const char *s, size_t n)
{
    char *p = (char *)owf_malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int tolower_ascii(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

void ow_merge_table_free(ow_merge_table *t)
{
    int i;
    if (!t) return;
    for (i = 0; i < (t->nrows + 1) * t->ncols; ++i) owf_free(t->cells[i]);
    owf_free(t->cells);
    owf_free(t);
}

static int utf8_ok(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned c = s[i];
        size_t k, more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9;
        if (more == 9 || (more == 1 && c < 0xC2) || i + more >= n + (more ? 0 : 1)) return 0;
        for (k = 1; k <= more; ++k) if ((s[i + k] & 0xC0) != 0x80) return 0;
        i += more + 1;
    }
    return 1;
}

/* The bytes as UTF-8: the same, or Latin-1 made UTF-8 */
char *ow_merge_utf8(const char *data, size_t n, size_t *out_n)
{
    size_t i, o = 0;
    char *out;
    if (utf8_ok((const unsigned char *)data, n)) {
        *out_n = n;
        return copy_bytes(data, n);
    }
    if (!(out = (char *)owf_malloc(2 * n + 1))) return NULL;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c < 0x80) out[o++] = (char)c;
        else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
    *out_n = o;
    return out;
}

/* The separator: the most of comma, semicolon and tab on the first line outside quotes (a comma when none) */
static char merge_separator(const char *s, size_t n)
{
    size_t i;
    int quoted = 0, comma = 0, semi = 0, tab = 0;
    for (i = 0; i < n && (quoted || (s[i] != '\n' && s[i] != '\r')); ++i) {
        if (s[i] == '"') quoted = !quoted;
        else if (!quoted && s[i] == ',') ++comma;
        else if (!quoted && s[i] == ';') ++semi;
        else if (!quoted && s[i] == '\t') ++tab;
    }
    if (semi > comma && semi >= tab) return ';';
    if (tab > comma && tab > semi) return '\t';
    return ',';
}

typedef struct {
    char **cells;
    int ncells, cap;
    int *row_end;                /* each row's cells end here */
    int nrows, caprows;
    char *buf;
    size_t len, capbuf;
    int failed;
} merge_parse;

static void parse_putc(merge_parse *p, char c)
{
    if (p->len + 2 > p->capbuf) {
        size_t want = p->capbuf ? p->capbuf * 2 : 64;
        char *g = (char *)owf_realloc(p->buf, want);
        if (!g) { p->failed = 1; return; }
        p->buf = g;
        p->capbuf = want;
    }
    p->buf[p->len++] = c;
}

static void parse_cell(merge_parse *p)
{
    char *cell;
    if (p->failed) return;
    if (p->ncells == p->cap) {
        int want = p->cap ? p->cap * 2 : 64;
        char **g = (char **)owf_realloc(p->cells, (size_t)want * sizeof *g);
        if (!g) { p->failed = 1; return; }
        p->cells = g;
        p->cap = want;
    }
    cell = p->len ? copy_bytes(p->buf, p->len) : NULL;
    if (p->len && !cell) { p->failed = 1; return; }
    p->cells[p->ncells++] = cell;
    p->len = 0;
}

static void parse_row(merge_parse *p)
{
    if (p->failed) return;
    if (p->nrows == p->caprows) {
        int want = p->caprows ? p->caprows * 2 : 32;
        int *g = (int *)owf_realloc(p->row_end, (size_t)want * sizeof *g);
        if (!g) { p->failed = 1; return; }
        p->row_end = g;
        p->caprows = want;
    }
    p->row_end[p->nrows++] = p->ncells;
}

static void trim_name(char *s)
{
    size_t n = strlen(s), a = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    while (s[a] == ' ' || s[a] == '\t') ++a;
    if (a) memmove(s, s + a, n - a + 1);
}

/* A table from CSV text (UTF-8): NULL when it has no columns or memory runs out (*memory set) */
ow_merge_table *ow_merge_table_parse(const char *s, size_t n, int *memory)
{
    merge_parse p;
    ow_merge_table *t = NULL;
    size_t i = 0;
    char sep;
    int quoted = 0, r, k, start, rows = 0, row_start;
    memset(&p, 0, sizeof p);
    *memory = 0;
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
        s += 3; n -= 3;                              /* a byte-order mark */
    }
    sep = merge_separator(s, n);
    row_start = 0;
    while (i < n && !p.failed) {
        char c = s[i++];
        if (quoted) {
            if (c == '"' && i < n && s[i] == '"') { parse_putc(&p, '"'); ++i; }
            else if (c == '"') quoted = 0;
            else parse_putc(&p, c);
        } else if (c == '"') quoted = 1;
        else if (c == sep) parse_cell(&p);
        else if (c == '\r' || c == '\n') {
            if (c == '\r' && i < n && s[i] == '\n') ++i;
            if (p.len || p.ncells > row_start) { parse_cell(&p); parse_row(&p); }
            row_start = p.ncells;                    /* an empty line is left out */
        } else parse_putc(&p, c);
    }
    if (!p.failed && (p.len || p.ncells > row_start)) { parse_cell(&p); parse_row(&p); }
    if (p.failed) { *memory = 1; goto out; }
    if (!p.nrows || p.row_end[0] == 0) goto out;
    if (!(t = (ow_merge_table *)owf_calloc(1, sizeof *t))) { *memory = 1; goto out; }
    t->ncols = p.row_end[0];
    for (r = 1; r < p.nrows; ++r) {                  /* rows of empty cells are no records */
        for (k = p.row_end[r - 1]; k < p.row_end[r] && !p.cells[k]; ++k) ;
        if (k < p.row_end[r]) ++rows;
    }
    t->nrows = rows;
    t->cells = (char **)owf_calloc((size_t)(rows + 1) * (size_t)t->ncols, sizeof *t->cells);
    if (!t->cells) { owf_free(t); t = NULL; *memory = 1; goto out; }
    for (r = 0, rows = 0; r < p.nrows; ++r) {
        int got;
        start = r ? p.row_end[r - 1] : 0;
        got = p.row_end[r] - start;
        if (r) {
            for (k = start; k < p.row_end[r] && !p.cells[k]; ++k) ;
            if (k == p.row_end[r]) continue;
        }
        for (k = 0; k < got && k < t->ncols; ++k) {  /* cells past the last column are left out */
            t->cells[rows * t->ncols + k] = p.cells[start + k];
            p.cells[start + k] = NULL;
        }
        ++rows;
    }
    for (k = 0; k < t->ncols; ++k) {                 /* a column without a name gets one */
        char made[24], **name = &t->cells[k];
        if (*name) trim_name(*name);
        if (*name && **name) continue;
        snprintf(made, sizeof made, "Column%d", k + 1);
        owf_free(*name);
        if (!(*name = copy_bytes(made, strlen(made)))) { ow_merge_table_free(t); t = NULL; *memory = 1; goto out; }
    }
out:
    for (k = 0; k < p.ncells; ++k) owf_free(p.cells[k]);
    owf_free(p.cells);
    owf_free(p.row_end);
    owf_free(p.buf);
    return t;
}


int ow_merge_column(const ow_merge_table *t, const char *name)
{
    int k;
    if (!t || !name) return -1;
    for (k = 0; k < t->ncols; ++k) if (!strcmp(t->cells[k], name)) return k;
    for (k = 0; k < t->ncols; ++k) {                 /* then without minding case (ASCII) */
        const char *a = t->cells[k], *b = name;
        while (*a && *b && tolower_ascii(*a) == tolower_ascii(*b)) { ++a; ++b; }
        if (!*a && !*b) return k;
    }
    return -1;
}

const char *ow_merge_value(const ow_merge_table *t, int record, const char *column)
{
    int k = ow_merge_column(t, column);
    const char *v;
    if (k < 0 || record < 0 || record >= t->nrows) return NULL;
    v = t->cells[(record + 1) * t->ncols + k];
    return v ? v : "";
}

ow_merge_table *ow_merge_table_from_text(const char *data, size_t length, int *memory)
{
    char *utf8;
    size_t n;
    ow_merge_table *t;
    *memory = 0;
    if (!data) return NULL;
    if (!(utf8 = ow_merge_utf8(data, length, &n))) { *memory = 1; return NULL; }
    t = ow_merge_table_parse(utf8, n, memory);
    owf_free(utf8);
    return t;
}

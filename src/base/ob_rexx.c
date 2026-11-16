/*
 * GFOpenBase's ARexx row commands (core 0.10.0): ADDROW, CHANGEROW,
 * DELETEROW, GETROW and GETFIELDS on a table, as the GFOPENBASE port gets
 * them (amicradle-teams ARexx-RULE.md). They live here rather than in the
 * app so that the host tests reach them. Nothing here asks: the answer, or
 * the reason, goes back as text with an ARexx return code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openbase_core.h"

static int rx_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static const char *rx_skip(const char *s)
{
    while (*s && rx_space(*s)) s++;
    return s;
}
static int rx_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int rx_same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (rx_lower((unsigned char)*a) != rx_lower((unsigned char)*b)) return 0;
    return !*a && !*b;
}

/* s starts with word (any case), then a space, '=' or its end; *rest is what follows */
static int rx_word(const char *s, const char *word, const char **rest)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; i++) if (!s[i] || rx_lower((unsigned char)s[i]) != rx_lower((unsigned char)word[i])) return 0;
    if (s[n] && !rx_space(s[n]) && s[n] != '=') return 0;
    if (rest) *rest = rx_skip(s + n + (s[n] == '='));
    return 1;
}

/* The next argument, "in quotes" ('or these') or one word, into out (cut to size). */
static const char *rx_arg(const char *s, char *out, size_t size)
{
    size_t k = 0;
    s = rx_skip(s);
    if (*s == '"' || *s == '\'') {
        char q = *s++;
        for (; *s && *s != q; s++) if (k + 1 < size) out[k++] = *s;
        if (*s == q) s++;
    } else {
        for (; *s && !rx_space(*s); s++) if (k + 1 < size) out[k++] = *s;
    }
    out[k] = 0;
    return rx_skip(s);
}

/* The same into *w, which moves on past its 0: room for it is the caller's. */
static const char *rx_take(const char *s, char **w)
{
    if (*s == '"' || *s == '\'') {
        char q = *s++;
        for (; *s && *s != q; s++) *(*w)++ = *s;
        if (*s == q) s++;
    } else {
        for (; *s && !rx_space(*s); s++) *(*w)++ = *s;
    }
    *(*w)++ = 0;
    return s;
}

struct rx_get {
    ob_int key;
    int found, field, over;
    char *out;
    size_t size;
};

static int rx_get_cb(void *user, ob_int key, int n, const char *const *values)
{
    struct rx_get *g = (struct rx_get *)user;
    size_t k = 0;
    int i;
    if (key != g->key) return 1;
    g->found = 1;
    for (i = 0; i < n && !g->over; i++) {
        const char *v = values[i] ? values[i] : "";
        size_t l = strlen(v);
        if (g->field >= 0 && i != g->field) continue;
        if (g->field < 0 && i) {
            if (k + 1 >= g->size) { g->over = 1; break; }
            g->out[k++] = '\t';
        }
        if (k + l >= g->size) { g->over = 1; break; }
        memcpy(g->out + k, v, l);
        k += l;
    }
    g->out[g->over ? 0 : k] = 0;
    return 1;
}

/* Whether table has the row key: 1, 0, or -1 with the reason in ob_error(). */
static int rx_have(ob_project *p, const char *table, ob_int key)
{
    char none[1];
    struct rx_get g;
    memset(&g, 0, sizeof g);
    g.key = key;
    g.field = -2;      /* no field wanted */
    g.out = none;
    g.size = sizeof none;
    if (ob_rows(p, table, key - 1, 1, rx_get_cb, &g) < 0) return -1;
    return g.found;
}

int ob_rexx_rows(ob_project *p, const char *table, ob_int current, const char *command,
                 char *out, size_t size, ob_int *row)
{
    enum { RX_ADD, RX_CHANGE, RX_DELETE, RX_GET, RX_FIELDS } cmd;
    static const char *const cmd_names[] = { "ADDROW", "CHANGEROW", "DELETEROW", "GETROW", "GETFIELDS" };
    const char *s, *names[OB_MAX_FIELDS], *vals[OB_MAX_FIELDS];
    ob_table *t = NULL;
    char *scratch = NULL, *w, field[OB_NAME_MAX] = "";
    int n = 0, rc = 0, i;
    ob_int key = current;
    if (row) *row = 0;
    command = rx_skip(command ? command : "");
    if (rx_word(command, "ADDROW", &s)) cmd = RX_ADD;
    else if (rx_word(command, "CHANGEROW", &s)) cmd = RX_CHANGE;
    else if (rx_word(command, "DELETEROW", &s)) cmd = RX_DELETE;
    else if (rx_word(command, "GETROW", &s)) cmd = RX_GET;
    else if (rx_word(command, "GETFIELDS", &s)) cmd = RX_FIELDS;
    else return -1;                                   /* not one of these */
    if (!out || size < 2) return 20;
    out[0] = 0;
    if (!p || !table || !*table) { snprintf(out, size, "No table is shown: TABLE chooses one"); return 5; }
    if (!(t = (ob_table *)malloc(sizeof *t)) || !(scratch = (char *)malloc(2 * strlen(s) + 8))) {
        snprintf(out, size, "Not enough memory");
        rc = 20;
        goto done;
    }
    if (!ob_table_get(p, table, t)) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }

    /* KEY k, FIELD f, and Field=Value pairs ("Field=Value", Field="Value") */
    w = scratch;
    while (*s) {
        const char *r;
        int quoted = *s == '"' || *s == '\'';
        if (!quoted && cmd != RX_ADD && cmd != RX_FIELDS && rx_word(s, "KEY", &r)) {
            char num[32], *end;
            long long k;
            s = rx_arg(r, num, sizeof num);
            k = strtoll(num, &end, 10);
            if (!num[0] || *end || k < 1) { snprintf(out, size, "KEY takes a row's key, not \"%.40s\"", num); rc = 10; goto done; }
            key = (ob_int)k;
            continue;
        }
        if (!quoted && cmd == RX_GET && rx_word(s, "FIELD", &r)) {
            s = rx_arg(r, field, sizeof field);
            continue;
        }
        if (cmd != RX_ADD && cmd != RX_CHANGE) {
            snprintf(out, size, "%s takes %s", cmd_names[cmd],
                     cmd == RX_GET ? "KEY and FIELD" : cmd == RX_DELETE ? "KEY" : "nothing");
            rc = 10;
            goto done;
        }
        {
            char *name = w, *eq;
            const char *val = NULL;
            if (quoted) {
                s = rx_take(s, &w);
                if ((eq = strchr(name, '=')) != NULL) { *eq = 0; val = eq + 1; }
                else if (*s == '=') { val = w; s = rx_take(s + 1, &w); }
            } else {
                while (*s && !rx_space(*s) && *s != '=') *w++ = *s++;
                *w++ = 0;
                if (*s == '=') { val = w; s = rx_take(s + 1, &w); }
            }
            s = rx_skip(s);
            if (!val) { snprintf(out, size, "%.60s needs a value: Field=Value", name); rc = 10; goto done; }
            for (i = 0; i < t->nfields && !rx_same(t->fields[i].name, name); i++) ;
            if (i == t->nfields) { snprintf(out, size, "%.60s has no field %.60s", t->name, name); rc = 10; goto done; }
            if (n >= OB_MAX_FIELDS) { snprintf(out, size, "Too many fields"); rc = 10; goto done; }
            names[n] = t->fields[i].name;
            vals[n] = val;
            n++;
        }
    }

    if (cmd == RX_FIELDS) {
        size_t k = 0;
        for (i = 0; i < t->nfields; i++) {
            size_t l = strlen(t->fields[i].name);
            if (k + l + 4 > size) { snprintf(out, size, "The fields are too many for the answer"); rc = 5; goto done; }
            if (k) out[k++] = ' ';
            out[k++] = '"';
            memcpy(out + k, t->fields[i].name, l);
            k += l;
            out[k++] = '"';
        }
        out[k] = 0;
        goto done;
    }
    if (cmd == RX_ADD) {
        ob_int k = 0;
        if (!n) { snprintf(out, size, "ADDROW takes Field=Value for each field it fills"); rc = 10; goto done; }
        if (!ob_row_add(p, t->name, n, names, vals, &k)) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
        snprintf(out, size, "%lld", (long long)k);
        if (row) *row = k;
        goto done;
    }

    /* the rest are about one row: KEY, or the one chosen */
    if (key < 1) { snprintf(out, size, "No row is chosen: give KEY"); rc = 5; goto done; }
    if (cmd == RX_GET) {
        struct rx_get g;
        memset(&g, 0, sizeof g);
        g.key = key;
        g.field = -1;
        g.out = out;
        g.size = size;
        if (field[0]) {
            for (i = 0; i < t->nfields && !rx_same(t->fields[i].name, field); i++) ;
            if (i == t->nfields) { snprintf(out, size, "%.60s has no field %.60s", t->name, field); rc = 10; goto done; }
            g.field = i;
        }
        if (ob_rows(p, t->name, key - 1, 1, rx_get_cb, &g) < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
        if (!g.found) { snprintf(out, size, "No row %lld in %.60s", (long long)key, t->name); rc = 5; goto done; }
        if (g.over) {
            snprintf(out, size, field[0] ? "The value is too long for the answer" : "The row is too long for the answer: ask for one FIELD");
            rc = 5;
            goto done;
        }
        if (row) *row = key;
        goto done;
    }
    if (cmd == RX_CHANGE && !n) { snprintf(out, size, "CHANGEROW takes Field=Value for each field it changes"); rc = 10; goto done; }
    if ((i = rx_have(p, t->name, key)) <= 0) {
        if (i < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; }
        else { snprintf(out, size, "No row %lld in %.60s", (long long)key, t->name); rc = 5; }
        goto done;
    }
    if (!(cmd == RX_CHANGE ? ob_row_change(p, t->name, key, n, names, vals) : ob_row_delete(p, t->name, key))) {
        snprintf(out, size, "%s", ob_error(p));
        rc = 10;
        goto done;
    }
    snprintf(out, size, "OK");
    if (row) *row = key;
done:
    free(scratch);
    free(t);
    return rc;
}

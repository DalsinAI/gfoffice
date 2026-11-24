/*
 * GFOpenBase's ARexx row commands (core 0.10.0): ADDROW, CHANGEROW,
 * DELETEROW, GETROW and GETFIELDS on a table, and (core 0.18.0) QUERIES,
 * QUERYFIELDS, COUNTQUERY, QUERYROW, RUNQUERY and FINDROW, and (core
 * 0.19.0) QUERY and EXEC with typed SQL, as the
 * GFOPENBASE port gets them (amicradle-teams ARexx-RULE.md). They live here rather than in the
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

/* ---- Saved queries from ARexx (core 0.18.0) ---- */

/* names, each in quotes with a space between, into out; 0 when they don't fit */
static int rx_quoted(char *out, size_t size, size_t *k, const char *name)
{
    size_t l = strlen(name);
    if (*k + l + 4 > size) return 0;
    if (*k) out[(*k)++] = ' ';
    out[(*k)++] = '"';
    memcpy(out + *k, name, l);
    *k += l;
    out[(*k)++] = '"';
    out[*k] = 0;
    return 1;
}

int ob_rexx_queries(ob_project *p, const char *command, char *out, size_t size, int *changed)
{
    enum { RQ_LIST, RQ_FIELDS, RQ_COUNT, RQ_ROW, RQ_RUN } cmd;
    static const char *const cmd_names[] = { "QUERIES", "QUERYFIELDS", "COUNTQUERY", "QUERYROW", "RUNQUERY" };
    const char *s, *answers[OB_QUERY_PARAMS];
    ob_query *q = NULL;
    ob_qresult *cols = NULL;
    char (*asks)[OB_VALUE_MAX] = NULL;
    char *scratch = NULL, *w, name[OB_NAME_MAX] = "", field[OB_NAME_MAX] = "";
    int rc = 0, i, n, na = 0, reads, want;
    long long rownum = 0;
    if (changed) *changed = 0;
    command = rx_skip(command ? command : "");
    if (rx_word(command, "QUERIES", &s)) cmd = RQ_LIST;
    else if (rx_word(command, "QUERYFIELDS", &s)) cmd = RQ_FIELDS;
    else if (rx_word(command, "COUNTQUERY", &s)) cmd = RQ_COUNT;
    else if (rx_word(command, "QUERYROW", &s)) cmd = RQ_ROW;
    else if (rx_word(command, "RUNQUERY", &s)) cmd = RQ_RUN;
    else return -1;                                   /* not one of these */
    if (!out || size < 2) return 20;
    out[0] = 0;
    if (!p) { snprintf(out, size, "No project is open: OPEN opens one"); return 5; }
    if (cmd == RQ_LIST) {
        size_t k = 0;
        if (*s) { snprintf(out, size, "QUERIES takes nothing"); return 10; }
        n = ob_query_count(p);
        if (n < 0) { snprintf(out, size, "%s", ob_error(p)); return 10; }
        for (i = 0; i < n; i++) {
            if (!rx_quoted(out, size, &k, ob_query_name(p, i))) { snprintf(out, size, "The queries are too many for the answer"); return 5; }
        }
        return 0;
    }
    s = rx_arg(s, name, sizeof name);
    if (!name[0]) { snprintf(out, size, "%s takes a saved query's name", cmd_names[cmd]); return 10; }
    q = (ob_query *)malloc(sizeof *q);
    cols = (ob_qresult *)malloc(OB_QUERY_OUT * sizeof *cols);
    asks = (char (*)[OB_VALUE_MAX])malloc(OB_QUERY_PARAMS * sizeof *asks);
    scratch = (char *)malloc(2 * strlen(s) + 8);
    if (!q || !cols || !asks || !scratch) { snprintf(out, size, "Not enough memory"); rc = 20; goto done; }

    /* ROW n, FIELD heading, and an ANSWER for each [question], in order */
    w = scratch;
    while (*s) {
        const char *r;
        if (cmd == RQ_ROW && rx_word(s, "ROW", &r)) {
            char num[32], *end;
            s = rx_arg(r, num, sizeof num);
            rownum = strtoll(num, &end, 10);
            if (!num[0] || *end || rownum < 1) { snprintf(out, size, "ROW takes a row's number from 1, not \"%.40s\"", num); rc = 10; goto done; }
            continue;
        }
        if (cmd == RQ_ROW && rx_word(s, "FIELD", &r)) {
            s = rx_arg(r, field, sizeof field);
            continue;
        }
        if (cmd != RQ_FIELDS && rx_word(s, "ANSWER", &r)) {
            if (na >= OB_QUERY_PARAMS) { snprintf(out, size, "A query asks at most %d questions", OB_QUERY_PARAMS); rc = 10; goto done; }
            answers[na++] = w;
            s = rx_skip(rx_take(r, &w));
            continue;
        }
        snprintf(out, size, "%s takes a name%s", cmd_names[cmd],
                 cmd == RQ_ROW ? ", ROW, FIELD and ANSWER" : cmd == RQ_FIELDS ? "" : " and ANSWER");
        rc = 10;
        goto done;
    }
    if (!ob_query_get(p, name, q)) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
    reads = q->kind == OB_QK_SELECT || q->kind == OB_QK_UNION || q->kind == OB_QK_CROSSTAB;

    if (cmd == RQ_FIELDS) {
        size_t k = 0;
        if (!reads) {
            snprintf(out, size, "%.60s %s: it shows no columns", q->name, q->kind == OB_QK_MAKE ? "makes a table" : "changes rows");
            rc = 10;
            goto done;
        }
        n = ob_query_columns(p, q, cols, OB_QUERY_OUT);
        if (n < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
        for (i = 0; i < n && i < OB_QUERY_OUT; i++) {
            if (!rx_quoted(out, size, &k, cols[i].heading)) { snprintf(out, size, "The fields are too many for the answer"); rc = 5; goto done; }
        }
        goto done;
    }
    /* every question answered, no more and no fewer */
    want = ob_query_params(p, q, asks, OB_QUERY_PARAMS);
    if (want < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
    if (want != na) {
        if (!want) snprintf(out, size, "%.60s asks nothing: leave ANSWER out", q->name);
        else snprintf(out, size, "%.60s asks %d question%s: give an ANSWER for each, in order (%s%.60s)", q->name, want,
                      want == 1 ? "" : "s", want == 1 ? "" : "the first: ", asks[0]);
        rc = 10;
        goto done;
    }
    if (cmd == RQ_COUNT) {
        ob_int t = reads ? ob_query_total(p, q, na ? answers : NULL) : ob_query_preview(p, q, na ? answers : NULL);
        if (t < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
        snprintf(out, size, "%lld", (long long)t);
        goto done;
    }
    if (cmd == RQ_RUN) {
        ob_int t;
        if (reads) {
            snprintf(out, size, "%.60s reads rows: COUNTQUERY counts them and QUERYROW reads one", q->name);
            rc = 10;
            goto done;
        }
        {
            int undoable = 0;
            t = ob_query_run(p, q, na ? answers : NULL, &undoable);
            if (t < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
            if (changed) *changed = 1;
            /* the count; then the word when Undo can't take it back */
            snprintf(out, size, undoable ? "%lld" : "%lld NOUNDO", (long long)t);
        }
        goto done;
    }
    /* QUERYROW */
    {
        struct rx_get g;
        if (!reads) {
            snprintf(out, size, "%.60s changes rows: RUNQUERY runs it and COUNTQUERY counts what it would change", q->name);
            rc = 10;
            goto done;
        }
        if (rownum < 1) { snprintf(out, size, "QUERYROW takes ROW, a row's number from 1"); rc = 10; goto done; }
        memset(&g, 0, sizeof g);
        g.key = (ob_int)(rownum - 1);
        g.field = -1;
        g.out = out;
        g.size = size;
        if (field[0]) {
            n = ob_query_columns(p, q, cols, OB_QUERY_OUT);
            if (n < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
            for (i = 0; i < n && i < OB_QUERY_OUT && !rx_same(cols[i].heading, field); i++) ;
            if (i >= n || i >= OB_QUERY_OUT) { snprintf(out, size, "%.60s has no column %.60s", q->name, field); rc = 10; goto done; }
            g.field = i;
        }
        if (ob_query_rows(p, q, na ? answers : NULL, (ob_int)(rownum - 1), 1, rx_get_cb, &g) < 0) {
            snprintf(out, size, "%s", ob_error(p));
            rc = 10;
            goto done;
        }
        if (!g.found) { snprintf(out, size, "No row %lld in %.60s", rownum, q->name); rc = 5; goto done; }
        if (g.over) {
            snprintf(out, size, field[0] ? "The value is too long for the answer" : "The row is too long for the answer: ask for one FIELD");
            rc = 5;
        }
    }
done:
    free(scratch);
    free(asks);
    free(cols);
    free(q);
    return rc;
}

/* ---- SQL typed from ARexx (core 0.19.0) ---- */

int ob_rexx_sql(ob_project *p, const char *command, char *out, size_t size, int *changed)
{
    const char *s, *answers[OB_QUERY_PARAMS];
    ob_query *q = NULL;
    char (*asks)[OB_VALUE_MAX] = NULL;
    char *scratch = NULL, *w;
    int exec, count = 0, na = 0, rc = 0, want;
    long long rownum = 1;
    size_t len;
    if (changed) *changed = 0;
    command = rx_skip(command ? command : "");
    if (rx_word(command, "QUERY", &s)) exec = 0;
    else if (rx_word(command, "EXEC", &s)) exec = 1;
    else return -1;                                   /* not one of these */
    if (!out || size < 2) return 20;
    out[0] = 0;
    if (!p) { snprintf(out, size, "No project is open: OPEN opens one"); return 5; }
    q = (ob_query *)malloc(sizeof *q);
    asks = (char (*)[OB_VALUE_MAX])malloc(OB_QUERY_PARAMS * sizeof *asks);
    scratch = (char *)malloc(2 * strlen(s) + 8);
    if (!q || !asks || !scratch) { snprintf(out, size, "Not enough memory"); rc = 20; goto done; }

    /* before the SQL: ANSWER v for each value it takes, and for QUERY ROW n or COUNT */
    w = scratch;
    for (;;) {
        const char *r;
        if (rx_word(s, "ANSWER", &r)) {
            if (na >= OB_QUERY_PARAMS) { snprintf(out, size, "SQL takes at most %d values", OB_QUERY_PARAMS); rc = 10; goto done; }
            answers[na++] = w;
            s = rx_skip(rx_take(r, &w));
            continue;
        }
        if (!exec && rx_word(s, "ROW", &r)) {
            char num[32], *end;
            s = rx_arg(r, num, sizeof num);
            rownum = strtoll(num, &end, 10);
            if (!num[0] || *end || rownum < 1) { snprintf(out, size, "ROW takes a row's number from 1, not \"%.40s\"", num); rc = 10; goto done; }
            continue;
        }
        if (!exec && rx_word(s, "COUNT", &r)) { count = 1; s = r; continue; }
        break;
    }
    /* the rest is the SQL; one pair of quotes round all of it comes off */
    len = strlen(s);
    while (len && rx_space(s[len - 1])) len--;
    if (len >= 2 && (s[0] == '"' || s[0] == '\'') && s[len - 1] == s[0]) { s++; len -= 2; }
    if (!len) { snprintf(out, size, "%s takes the SQL to run", exec ? "EXEC" : "QUERY"); rc = 10; goto done; }
    if (len >= OB_SQL_MAX) { snprintf(out, size, "The SQL is too long: %d characters at most", OB_SQL_MAX - 1); rc = 10; goto done; }
    ob_query_init(q, "");
    q->sql_only = 1;
    memcpy(q->sql, s, len);
    q->sql[len] = 0;
    if (exec) {
        const char *t = rx_skip(q->sql);
        if (rx_word(t, "UPDATE", NULL)) q->kind = OB_QK_UPDATE;
        else if (rx_word(t, "INSERT", NULL)) q->kind = OB_QK_APPEND;
        else if (rx_word(t, "DELETE", NULL)) q->kind = OB_QK_DELETE;
        else { snprintf(out, size, "EXEC runs one UPDATE, INSERT or DELETE statement: QUERY reads rows"); rc = 10; goto done; }
    }
    want = ob_query_params(p, q, asks, OB_QUERY_PARAMS);
    if (want < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
    if (want != na) {
        if (!want) snprintf(out, size, "The SQL takes no values: leave ANSWER out");
        else snprintf(out, size, "The SQL takes %d value%s: give an ANSWER for each before it, in order", want, want == 1 ? "" : "s");
        rc = 10;
        goto done;
    }
    if (exec) {
        int undoable = 0;
        ob_int t = ob_query_run(p, q, na ? answers : NULL, &undoable);
        if (t < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
        if (changed) *changed = 1;
        snprintf(out, size, undoable ? "%lld" : "%lld NOUNDO", (long long)t);
    } else if (count) {
        ob_int t = ob_query_total(p, q, na ? answers : NULL);
        if (t < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
        snprintf(out, size, "%lld", (long long)t);
    } else {
        struct rx_get g;
        memset(&g, 0, sizeof g);
        g.key = (ob_int)(rownum - 1);
        g.field = -1;
        g.out = out;
        g.size = size;
        if (ob_query_rows(p, q, na ? answers : NULL, (ob_int)(rownum - 1), 1, rx_get_cb, &g) < 0) {
            snprintf(out, size, "%s", ob_error(p));
            rc = 10;
            goto done;
        }
        if (!g.found) { snprintf(out, size, "No row %lld", rownum); rc = 5; goto done; }
        if (g.over) { snprintf(out, size, "The row is too long for the answer: ask for fewer columns"); rc = 5; }
    }
done:
    free(scratch);
    free(asks);
    free(q);
    return rc;
}

/* ---- FINDROW (core 0.18.0) ---- */

int ob_rexx_find(ob_project *p, const char *table, const ob_view *view, ob_int current, const char *command,
                 char *out, size_t size, ob_int *pos)
{
    const char *s, *val = NULL;
    char *scratch = NULL, *w, *name = NULL;
    ob_int total, from, at;
    int back = 0, first = 0, rc = 0;
    if (pos) *pos = -1;
    command = rx_skip(command ? command : "");
    if (!rx_word(command, "FINDROW", &s)) return -1;
    if (!out || size < 2) return 20;
    out[0] = 0;
    if (!p || !table || !*table) { snprintf(out, size, "No table is shown: TABLE chooses one"); return 5; }
    if (!(scratch = (char *)malloc(2 * strlen(s) + 8))) { snprintf(out, size, "Not enough memory"); return 20; }
    w = scratch;
    while (*s) {
        const char *r;
        int quoted = *s == '"' || *s == '\'';
        if (!quoted && rx_word(s, "BACK", &r) && s[4] != '=') { back = 1; s = r; continue; }
        if (!quoted && rx_word(s, "FIRST", &r) && s[5] != '=') { first = 1; s = r; continue; }
        if (name) { snprintf(out, size, "FINDROW takes one Field=Value, BACK and FIRST"); rc = 10; goto done; }
        name = w;
        if (quoted) {
            char *eq;
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
    }
    if (!name) { snprintf(out, size, "FINDROW takes Field=Value: the field to look in and what to find"); rc = 10; goto done; }
    total = ob_view_total(p, table, view);
    if (total < 0) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
    if (first || current < 0 || current >= total) from = back ? total - 1 : 0;
    else from = back ? current - 1 : current + 1;
    at = from < 0 || from >= total ? -1 : ob_view_find(p, table, view, name, val, from, back);
    if (at == -2) { snprintf(out, size, "%s", ob_error(p)); rc = 10; goto done; }
    if (at < 0) { snprintf(out, size, "No %srow has %.60s in %.60s", first ? "" : back ? "earlier " : "later ", val, name); rc = 5; goto done; }
    if (pos) *pos = at;
    snprintf(out, size, "%lld", (long long)at + 1);
done:
    free(scratch);
    return rc;
}

/* While a form is open, the ARexx commands that would change the table or
 * row it shows, or the rows themselves, are refused (core 0.14.3). */
int ob_rexx_form_refuses(const char *command)
{
    static const char *const moves[] = { "OPEN", "QUIT", "FIRSTROW", "NEXTROW", "PREVROW", "LASTROW",
                                         "ADDROW", "CHANGEROW", "DELETEROW", "RUNQUERY", "FINDROW", "EXEC" };
    const char *rest = "";
    size_t i;
    if (!command) return 0;
    command = rx_skip(command);
    for (i = 0; i < sizeof moves / sizeof moves[0]; i++)
        if (rx_word(command, moves[i], NULL)) return 1;
    return (rx_word(command, "TABLE", &rest) || rx_word(command, "ROW", &rest)) && *rest;
}

/*
 * libowf: the formula engine and the charts a book keeps as Excel's parts
 * (a sheet's drawing, the charts it shows). When rows or columns go in or
 * out, the drawing's anchors (its charts', pictures' and shapes') move with
 * the cells as Excel moves them, and the cells each chart's series name
 * (<c:f>: its values, categories, name) change as a formula's references
 * do; when a sheet is renamed, its new name goes in them. The parts' XML
 * is changed only there: all else in them stays as the file had it.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- XML as text: tags and text one at a time ---- */

enum { X_END, X_START, X_CLOSE, X_TEXT, X_OTHER, X_BAD };

typedef struct {
    const char *s;
    size_t n, i;
} xwalk;

typedef struct {
    int kind;
    size_t a, b;                 /* its bytes, s[a] to s[b - 1] */
    const char *name;            /* a tag's local name (after any prefix) */
    size_t nlen;
    int empty;                   /* <x/> */
} xtok;

static int xnext(xwalk *w, xtok *t)
{
    const char *s = w->s;
    size_t i = w->i, n = w->n;
    memset(t, 0, sizeof *t);
    t->a = i;
    if (i >= n)
        return t->kind = X_END;
    if (s[i] != '<') {                           /* text up to the next tag */
        while (i < n && s[i] != '<')
            i++;
        t->b = w->i = i;
        return t->kind = X_TEXT;
    }
    if (i + 3 < n && !memcmp(s + i, "<!--", 4)) {
        const char *e = NULL;
        size_t k;
        for (k = i + 4; k + 2 < n; k++)
            if (s[k] == '-' && s[k + 1] == '-' && s[k + 2] == '>') {
                e = s + k + 3;
                break;
            }
        if (!e)
            return t->kind = X_BAD;
        t->b = w->i = (size_t)(e - s);
        return t->kind = X_OTHER;
    }
    if (i + 8 < n && !memcmp(s + i, "<![CDATA[", 9)) {
        size_t k;
        for (k = i + 9; k + 2 < n; k++)
            if (s[k] == ']' && s[k + 1] == ']' && s[k + 2] == '>') {
                t->b = w->i = k + 3;
                return t->kind = X_OTHER;
            }
        return t->kind = X_BAD;
    }
    {
        int close = i + 1 < n && s[i + 1] == '/', other = i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!');
        char quote = 0;
        size_t k = i + 1 + (close || other), name;
        name = k;
        while (k < n && !strchr(" \t\r\n/>", s[k]))
            k++;
        t->name = s + name;
        t->nlen = k - name;
        for (; k < n; k++) {                     /* to the tag's '>', past quoted values */
            if (quote) {
                if (s[k] == quote)
                    quote = 0;
            } else if (s[k] == '"' || s[k] == '\'') {
                quote = s[k];
            } else if (s[k] == '>') {
                break;
            }
        }
        if (k >= n || !t->nlen)
            return t->kind = X_BAD;
        t->empty = !close && !other && s[k - 1] == '/';
        t->b = w->i = k + 1;
        {
            const char *colon = memchr(t->name, ':', t->nlen);
            if (colon) {
                t->nlen -= (size_t)(colon + 1 - t->name);
                t->name = colon + 1;
            }
        }
        return t->kind = other ? X_OTHER : close ? X_CLOSE : X_START;
    }
}

static int named(const xtok *t, const char *name)
{
    return t->nlen == strlen(name) && !memcmp(t->name, name, t->nlen);
}

/* An attribute's value in a start tag (its local name, any prefix), into out; 0 when it has none. */
static int tag_attr(const char *s, const xtok *t, const char *name, char *out, size_t size)
{
    size_t k = (size_t)(t->name - s) + t->nlen, len = strlen(name);
    while (k < t->b) {
        size_t a, e;
        char q;
        while (k < t->b && strchr(" \t\r\n", s[k]))
            k++;
        a = k;
        while (k < t->b && s[k] != '=' && !strchr(" \t\r\n/>", s[k]))
            k++;
        e = k;
        if (k >= t->b || s[k] != '=')
            return 0;
        k++;
        if (k >= t->b || (s[k] != '"' && s[k] != '\''))
            return 0;
        q = s[k++];
        {
            size_t v = k, m;
            const char *colon;
            while (k < t->b && s[k] != q)
                k++;
            colon = memchr(s + a, ':', e - a);
            if (colon)
                a = (size_t)(colon + 1 - s);
            if (e - a == len && !memcmp(s + a, name, len)) {
                m = k - v < size - 1 ? k - v : size - 1;
                memcpy(out, s + v, m);
                out[m] = 0;
                return 1;
            }
            k++;
        }
    }
    return 0;
}

/* XML's text with its entities undone, as a new string; NULL when memory ran out. */
static char *unescaped(const char *s, size_t n)
{
    static const struct { const char *e; char c; } ents[5] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' } };
    char *in = malloc(n + 1), *out = NULL;
    owf_buf b;
    size_t i;
    if (!in)
        return NULL;
    memcpy(in, s, n);
    in[n] = 0;
    owf_buf_init(&b);
    for (i = 0; i < n; i++) {
        if (in[i] == '&') {
            int k, done = 0;
            for (k = 0; k < 5 && !done; k++) {
                size_t l = strlen(ents[k].e);
                if (!strncmp(in + i + 1, ents[k].e, l)) {
                    owf_buf_putc(&b, ents[k].c);
                    i += l;
                    done = 1;
                }
            }
            if (!done && in[i + 1] == '#') {     /* a character by its number */
                char *end;
                const char *digits = in + i + (in[i + 2] == 'x' ? 3 : 2);
                unsigned long v = strtoul(digits, &end, in[i + 2] == 'x' ? 16 : 10);
                if (end > digits && *end == ';' && v > 0 && v < 0x110000UL) {
                    owf_buf_put_utf8(&b, v);
                    i = (size_t)(end - in);
                    done = 1;
                }
            }
            if (done)
                continue;
        }
        owf_buf_putc(&b, in[i]);
    }
    owf_buf_putc(&b, 0);
    free(in);
    if (!b.failed && (out = malloc(b.length)) != NULL)
        memcpy(out, b.data, b.length);
    owf_buf_free(&b);
    return out;
}

/* A part's text replaced by b's. 0 when memory ran out. */
static int replace_part(owf_part *p, owf_buf *b)
{
    unsigned char *data;
    if (b->failed)
        return 0;
    if (!(data = malloc(b->length ? b->length : 1)))
        return 0;
    if (b->length)
        memcpy(data, b->data, b->length);
    free(p->data);
    p->data = data;
    p->length = b->length;
    return 1;
}

static owf_part *part_named(owf_book *book, const char *name)
{
    int i;
    for (i = 0; name && i < book->nparts; i++)
        if (!strcmp(book->parts[i].name, name))
            return &book->parts[i];
    return NULL;
}

/* ---- Each chart's references ---- */

/* A chart part's <c:f> texts, each through fix (home its sheet). 1 done (changed or not), 0 memory ran out. */
static int chart_refs(owf_book *book, owf_part *p, int home, calc_chart_fix fix, void *user)
{
    xwalk w;
    xtok t;
    owf_buf b;
    size_t done = 0;
    int changed = 0, ok = 1;
    w.s = (const char *)p->data;
    w.n = p->length;
    w.i = 0;
    owf_buf_init(&b);
    while (xnext(&w, &t) != X_END && t.kind != X_BAD) {
        size_t from, to;
        xtok e;
        char *ref, *fixed;
        if (t.kind != X_START || t.empty || !named(&t, "f"))
            continue;
        from = t.b;                              /* its text, to its end tag */
        if (xnext(&w, &e) == X_TEXT) {
            to = e.b;
            if (xnext(&w, &e) != X_CLOSE)
                continue;
        } else if (e.kind == X_CLOSE) {
            continue;                            /* <c:f></c:f> */
        } else {
            continue;
        }
        if (!(ref = unescaped(w.s + from, to - from))) {
            ok = 0;
            break;
        }
        fixed = fix(user, book, home, ref);
        if (!fixed) {
            free(ref);
            ok = 0;
            break;
        }
        if (strcmp(fixed, ref)) {
            owf_buf_put(&b, w.s + done, from - done);
            owf_buf_puts_xml(&b, fixed);
            done = to;
            changed = 1;
        }
        free(ref);
        free(fixed);
    }
    if (ok && changed) {
        owf_buf_put(&b, w.s + done, w.n - done);
        ok = replace_part(p, &b);
    }
    owf_buf_free(&b);
    return ok;
}

int calc_charts_refs(owf_book *book, calc_chart_fix fix, void *user)
{
    int s;
    for (s = 0; s < book->nsheets; s++) {
        const char *drawing = book->sheets[s].drawing, *slash;
        char dir[512], name[1100];
        const owf_part *rp;
        owf_xml *rels;
        const owf_xml_node *n;
        int e = OWF_OK;
        if (!drawing)
            continue;
        slash = strrchr(drawing, '/');
        snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - drawing + 1) : 0, drawing);
        snprintf(name, sizeof name, "%s_rels/%s.rels", dir, slash ? slash + 1 : drawing);
        if (!(rp = owf_book_part(book, name)))
            continue;
        if (!(rels = owf_xml_parse((const char *)rp->data, rp->length, &e))) {
            if (e == OWF_ERR_MEMORY)
                return 0;
            continue;
        }
        for (n = owf_xml_root(rels)->first; n; n = n->next) {
            const char *type, *target, *mode;
            size_t tl;
            owf_part *cp;
            char part[512];
            if (n->type != OWF_XML_ELEMENT || strcmp(n->name, "Relationship"))
                continue;
            type = owf_xml_attr(n, "", "Type");
            target = owf_xml_attr(n, "", "Target");
            mode = owf_xml_attr(n, "", "TargetMode");
            tl = type ? strlen(type) : 0;
            if (!target || tl < 6 || strcmp(type + tl - 6, "/chart") || (mode && !strcmp(mode, "External")) ||
                !owf_opc_part_name(part, sizeof part, dir, target) || !(cp = part_named(book, part)))
                continue;
            if (!chart_refs(book, cp, s, fix, user)) {
                owf_xml_free(rels);
                return 0;
            }
        }
        owf_xml_free(rels);
    }
    return 1;
}

/* ---- The anchors ---- */

/* Where one edge of an anchor is along the axis: its cell, and the offset into it (EMU), as the text has them. */
typedef struct {
    int found;
    long cell, off;
    size_t cell_a, cell_b, off_a, off_b;         /* their texts' bytes */
} edge;

/* An edge moved: rows (columns) from at on are count further on, or (count < 0) gone. */
static void move_edge(edge *e, long at, long count, long limit)
{
    if (count > 0) {
        if (e->cell >= at)
            e->cell += count;
        if (e->cell >= limit) {
            e->cell = limit - 1;
            e->off = 0;
        }
    } else if (e->cell >= at - count) {
        e->cell += count;
    } else if (e->cell >= at) {                  /* in the deleted ones: the top of what follows them */
        e->cell = at;
        e->off = 0;
    }
}

static void put_number(owf_buf *b, long v)
{
    char t[24];
    snprintf(t, sizeof t, "%ld", v);
    owf_buf_puts(b, t);
}

int calc_charts_shift(owf_book *book, int sheet, int axis, long at, long count, long limit)
{
    owf_sheet *sh = &book->sheets[sheet];
    owf_part *p = sh->drawing ? part_named(book, sh->drawing) : NULL;
    const char *cell_name = axis ? "col" : "row", *off_name = axis ? "colOff" : "rowOff";
    xwalk w;
    xtok t;
    owf_buf b;
    size_t done = 0;
    int changed = 0, depth = 0, ok;
    if (!p || !count)
        return 1;
    w.s = (const char *)p->data;
    w.n = p->length;
    w.i = 0;
    owf_buf_init(&b);
    while (xnext(&w, &t) != X_END && t.kind != X_BAD) {
        edge ends[2];
        char edit[32] = "";
        size_t start = t.a;
        int two, which = -1, d = 0, gone;
        if (t.kind == X_START && !t.empty)
            depth++;
        else if (t.kind == X_CLOSE)
            depth--;
        if (t.kind != X_START || t.empty || depth != 2 || (!named(&t, "twoCellAnchor") && !named(&t, "oneCellAnchor")))
            continue;
        two = named(&t, "twoCellAnchor");
        tag_attr(w.s, &t, "editAs", edit, sizeof edit);
        memset(ends, 0, sizeof ends);
        /* its from and to: the cell and the offset along the axis */
        for (;;) {
            xtok c;
            if (xnext(&w, &c) == X_END || c.kind == X_BAD) {
                owf_buf_free(&b);
                return 1;                        /* damaged: left as it is */
            }
            if (c.kind == X_START && !c.empty) {
                d++;
                if (d == 1)
                    which = named(&c, "from") ? 0 : named(&c, "to") ? 1 : -1;
                else if (d == 2 && which >= 0 && (named(&c, cell_name) || named(&c, off_name))) {
                    int is_cell = named(&c, cell_name);
                    xtok v;
                    if (xnext(&w, &v) == X_TEXT) {
                        char num[32];
                        size_t len = v.b - v.a < sizeof num - 1 ? v.b - v.a : sizeof num - 1;
                        memcpy(num, w.s + v.a, len);
                        num[len] = 0;
                        if (is_cell) {
                            ends[which].cell = strtol(num, NULL, 10);
                            ends[which].cell_a = v.a;
                            ends[which].cell_b = v.b;
                            ends[which].found = 1;
                        } else {
                            ends[which].off = strtol(num, NULL, 10);
                            ends[which].off_a = v.a;
                            ends[which].off_b = v.b;
                        }
                    } else if (v.kind == X_CLOSE) {
                        d--;
                    }
                }
            } else if (c.kind == X_CLOSE) {
                if (d == 0) {
                    t = c;                       /* the anchor's end */
                    break;
                }
                d--;
            }
        }
        depth--;
        if (!ends[0].found || !strcmp(edit, "absolute"))
            continue;
        {
            edge was = ends[0], was_end = ends[1];
            move_edge(&ends[0], at, count, limit);
            if (two && ends[1].found) {
                if (!strcmp(edit, "oneCell")) {  /* moved, not sized: the end as far as the start */
                    ends[1].cell += ends[0].cell - was.cell;
                    if (ends[1].cell >= limit)
                        ends[1].cell = limit - 1;
                    if (ends[1].cell < 0)
                        ends[1].cell = 0;
                } else {
                    move_edge(&ends[1], at, count, limit);
                }
            }
            /* all its rows (columns) deleted, it goes with them as Excel's does */
            gone = count < 0 && two && ends[1].found && strcmp(edit, "oneCell") &&
                   (ends[1].cell < ends[0].cell || (ends[1].cell == ends[0].cell && ends[1].off <= ends[0].off));
            if (!gone && was.cell == ends[0].cell && was.off == ends[0].off && was_end.cell == ends[1].cell &&
                was_end.off == ends[1].off)
                continue;                        /* where it was: its text as it is */
        }
        owf_buf_put(&b, w.s + done, start - done);
        if (gone) {
            done = t.b;
            changed = 1;
            continue;
        }
        {
            /* the anchor's text with its numbers changed, in order */
            size_t marks[8], k = 0, m, from = start;
            long values[4];
            int e;
            for (e = 0; e < 2; e++) {
                if (!ends[e].found)
                    continue;
                marks[k] = ends[e].cell_a;
                marks[k + 1] = ends[e].cell_b;
                values[k / 2] = ends[e].cell;
                k += 2;
                if (ends[e].off_b) {
                    marks[k] = ends[e].off_a;
                    marks[k + 1] = ends[e].off_b;
                    values[k / 2] = ends[e].off;
                    k += 2;
                }
            }
            for (m = 0; m < k; m += 2) {         /* in the text's order (from before to; cell, offset either way) */
                size_t j, best = m;
                for (j = m + 2; j < k; j += 2)
                    if (marks[j] < marks[best])
                        best = j;
                if (best != m) {
                    size_t ta = marks[m], tb = marks[m + 1];
                    long tv = values[m / 2];
                    marks[m] = marks[best];
                    marks[m + 1] = marks[best + 1];
                    values[m / 2] = values[best / 2];
                    marks[best] = ta;
                    marks[best + 1] = tb;
                    values[best / 2] = tv;
                }
            }
            for (m = 0; m < k; m += 2) {
                owf_buf_put(&b, w.s + from, marks[m] - from);
                put_number(&b, values[m / 2]);
                from = marks[m + 1];
            }
            owf_buf_put(&b, w.s + from, t.b - from);
            done = t.b;
            changed = 1;
        }
    }
    ok = 1;
    if (changed) {
        owf_buf_put(&b, w.s + done, w.n - done);
        ok = replace_part(p, &b);
    }
    owf_buf_free(&b);
    return ok;
}

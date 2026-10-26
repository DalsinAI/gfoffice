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

/* An attribute's value's bytes in a start tag (by its local name, any prefix), s[*va] to s[*vb - 1]; 0 when
 * it has none. */
static int attr_span(const char *s, const xtok *t, const char *name, size_t *va, size_t *vb)
{
    size_t k = (size_t)(t->name - s) + t->nlen, len = strlen(name);
    while (k < t->b) {
        size_t a, e, v;
        const char *colon;
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
        v = k;
        while (k < t->b && s[k] != q)
            k++;
        colon = memchr(s + a, ':', e - a);
        if (colon)
            a = (size_t)(colon + 1 - s);
        if (e - a == len && !memcmp(s + a, name, len)) {
            *va = v;
            *vb = k;
            return 1;
        }
        k++;
    }
    return 0;
}

/* An attribute's value, into out; 0 when it has none. */
static int tag_attr(const char *s, const xtok *t, const char *name, char *out, size_t size)
{
    size_t a, b, m;
    if (!attr_span(s, t, name, &a, &b))
        return 0;
    m = b - a < size - 1 ? b - a : size - 1;
    memcpy(out, s + a, m);
    out[m] = 0;
    return 1;
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

/* A number in the text: its value and its bytes. */
typedef struct {
    int found;
    long v;
    size_t a, b;
} num;

enum { A_COL, A_COLOFF, A_ROW, A_ROWOFF };
enum { K_TWO, K_ONE, K_ABSOLUTE };

/* One anchor of a drawing, as its text has it. */
typedef struct {
    int kind;                    /* K_... */
    char edit[16];               /* a two-cell one's editAs: "", "twoCell", "oneCell", "absolute" */
    size_t start, end;           /* its bytes */
    num at[2][4];                /* from and to: col, colOff, row, rowOff */
    num pos[2];                  /* an absolute one's: x, y */
    char rid[64];                /* the chart it shows (graphicFrame, graphic, graphicData, chart: its r:id), or "" */
} anchor;

static void read_num(const char *s, size_t a, size_t b, num *n)
{
    char t[32];
    size_t len = b - a < sizeof t - 1 ? b - a : sizeof t - 1;
    memcpy(t, s + a, len);
    t[len] = 0;
    n->v = strtol(t, NULL, 10);
    n->a = a;
    n->b = b;
    n->found = 1;
}

static int field_of(const xtok *t)
{
    return named(t, "col") ? A_COL : named(t, "colOff") ? A_COLOFF : named(t, "row") ? A_ROW :
           named(t, "rowOff") ? A_ROWOFF : -1;
}

static int is_anchor(const xtok *t)
{
    return named(t, "twoCellAnchor") || named(t, "oneCellAnchor") || named(t, "absoluteAnchor");
}

/* After an anchor's start tag t: the rest of it, read into a. 0 when the text is damaged. */
static int read_anchor(xwalk *w, const xtok *t, anchor *a)
{
    xtok path[4];
    int d = 0;
    memset(a, 0, sizeof *a);
    a->kind = named(t, "twoCellAnchor") ? K_TWO : named(t, "oneCellAnchor") ? K_ONE : K_ABSOLUTE;
    a->start = t->a;
    tag_attr(w->s, t, "editAs", a->edit, sizeof a->edit);
    for (;;) {
        xtok c;
        if (xnext(w, &c) == X_END || c.kind == X_BAD)
            return 0;
        if (c.kind == X_CLOSE) {
            if (!d) {
                a->end = c.b;
                return 1;
            }
            d--;
        } else if (c.kind == X_TEXT) {
            int e = d == 2 ? (named(&path[0], "from") ? 0 : named(&path[0], "to") ? 1 : -1) : -1, f;
            if (e >= 0 && (f = field_of(&path[1])) >= 0)
                read_num(w->s, c.a, c.b, &a->at[e][f]);
        } else if (c.kind == X_START) {
            size_t va, vb;
            if (d == 0 && named(&c, "pos")) {
                if (attr_span(w->s, &c, "x", &va, &vb))
                    read_num(w->s, va, vb, &a->pos[0]);
                if (attr_span(w->s, &c, "y", &va, &vb))
                    read_num(w->s, va, vb, &a->pos[1]);
            }
            if (d == 3 && named(&c, "chart") && named(&path[0], "graphicFrame") && named(&path[1], "graphic") &&
                named(&path[2], "graphicData"))
                tag_attr(w->s, &c, "id", a->rid, sizeof a->rid);
            if (!c.empty) {
                if (d < 4)
                    path[d] = c;
                d++;
            }
        }
    }
}

/* A number to write in place of the text's. */
typedef struct {
    size_t a, b;
    long v;
} change;

static void add_change(change *c, int *n, const num *at, long v)
{
    if (at->found && at->v != v) {
        c[*n].a = at->a;
        c[*n].b = at->b;
        c[*n].v = v;
        (*n)++;
    }
}

/* s[from] to s[to - 1] into b, with the numbers changed (in the text's order, whatever theirs). */
static void put_changed(owf_buf *b, const char *s, size_t from, size_t to, change *c, int n)
{
    int i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && c[j - 1].a > c[j].a; j--) {
            change t = c[j];
            c[j] = c[j - 1];
            c[j - 1] = t;
        }
    for (i = 0; i < n; i++) {
        char t[24];
        owf_buf_put(b, s + from, c[i].a - from);
        snprintf(t, sizeof t, "%ld", c[i].v);
        owf_buf_puts(b, t);
        from = c[i].b;
    }
    owf_buf_put(b, s + from, to - from);
}

/* Where one edge of an anchor is along an axis: its cell, and the offset into it (EMU). */
typedef struct {
    long cell, off;
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

int calc_charts_shift(owf_book *book, int sheet, int axis, long at, long count, long limit)
{
    owf_sheet *sh = &book->sheets[sheet];
    owf_part *p = sh->drawing ? part_named(book, sh->drawing) : NULL;
    int cf = axis ? A_COL : A_ROW, of = axis ? A_COLOFF : A_ROWOFF;
    xwalk w;
    xtok t;
    owf_buf b;
    size_t done = 0;
    int changed = 0, depth = 0, ok = 1;
    if (!p || !count)
        return 1;
    w.s = (const char *)p->data;
    w.n = p->length;
    w.i = 0;
    owf_buf_init(&b);
    while (xnext(&w, &t) != X_END && t.kind != X_BAD) {
        anchor a;
        edge e[2], was[2];
        change c[4];
        int nc = 0, gone, two, k;
        if (t.kind == X_CLOSE)
            depth--;
        if (t.kind != X_START || t.empty)
            continue;
        if (++depth != 2 || !is_anchor(&t))
            continue;
        if (!read_anchor(&w, &t, &a)) {
            owf_buf_free(&b);
            return 1;                            /* damaged: left as it is */
        }
        depth--;
        if (a.kind == K_ABSOLUTE || !a.at[0][cf].found || !strcmp(a.edit, "absolute"))
            continue;
        two = a.kind == K_TWO && a.at[1][cf].found;
        for (k = 0; k < 2; k++) {
            e[k].cell = a.at[k][cf].v;
            e[k].off = a.at[k][of].found ? a.at[k][of].v : 0;
            was[k] = e[k];
        }
        move_edge(&e[0], at, count, limit);
        if (two) {
            if (!strcmp(a.edit, "oneCell")) {    /* moved, not sized: the end as far as the start */
                e[1].cell += e[0].cell - was[0].cell;
                if (e[1].cell >= limit)
                    e[1].cell = limit - 1;
                if (e[1].cell < 0)
                    e[1].cell = 0;
            } else {
                move_edge(&e[1], at, count, limit);
            }
        }
        /* all its rows (columns) deleted, it goes with them as Excel's does */
        gone = count < 0 && two && strcmp(a.edit, "oneCell") &&
               (e[1].cell < e[0].cell || (e[1].cell == e[0].cell && e[1].off <= e[0].off));
        for (k = 0; k < (two ? 2 : 1) && !gone; k++) {
            add_change(c, &nc, &a.at[k][cf], e[k].cell);
            add_change(c, &nc, &a.at[k][of], e[k].off);
        }
        if (!gone && !nc)
            continue;                            /* where it was: its text as it is */
        owf_buf_put(&b, w.s + done, a.start - done);
        if (!gone)
            put_changed(&b, w.s, a.start, a.end, c, nc);
        done = a.end;
        changed = 1;
    }
    if (changed) {
        owf_buf_put(&b, w.s + done, w.n - done);
        ok = replace_part(p, &b);
    }
    owf_buf_free(&b);
    return ok;
}

/* ---- A chart moved ---- */

/* A column's (axis 1) or row's size in EMU, as the sheet has it (hidden: 0). */
static long line_emu(const owf_sheet *s, int axis, unsigned i)
{
    int k;
    long twips;
    if (axis) {
        twips = s->default_width;
        for (k = s->ncols - 1; k >= 0; k--)
            if (i >= s->cols[k].first && i <= s->cols[k].last) {
                twips = s->cols[k].hidden ? 0 : s->cols[k].width > 0 ? s->cols[k].width : s->default_width;
                break;
            }
    } else {
        twips = s->default_height;
        for (k = 0; k < s->nrows; k++)
            if (s->rows[k].row == i) {
                twips = s->rows[k].hidden ? 0 : s->rows[k].height > 0 ? s->rows[k].height : s->default_height;
                break;
            }
    }
    return twips * 635L;
}

/* An edge (a cell and an offset in it) as EMU from the sheet's top (left), and back. */
static double edge_emu(const owf_sheet *s, int axis, long cell, long off)
{
    double e = (double)off;
    long i;
    for (i = 0; i < cell; i++)
        e += (double)line_emu(s, axis, (unsigned)i);
    return e;
}

static void emu_edge(const owf_sheet *s, int axis, double e, edge *out)
{
    long limit = axis ? (long)CALC_MAX_COLS : (long)CALC_MAX_ROWS, i = 0;
    if (e < 0)
        e = 0;
    for (; i < limit - 1; i++) {
        long l = line_emu(s, axis, (unsigned)i);
        if (e < (double)l)
            break;
        e -= (double)l;
    }
    out->cell = i;
    out->off = (long)(e + 0.5);
}

/* Whether a drawing's chart (its r:id) is one owf_sheet_charts reads: its part there, and read as XML. */
static int chart_shown(const owf_book *book, const char *dir, const owf_xml *rels, const char *rid, int *failed)
{
    const owf_xml_node *n;
    for (n = rels && *rid ? owf_xml_root(rels)->first : NULL; n; n = n->next) {
        const char *id, *target, *mode;
        const owf_part *cp;
        char part[512];
        owf_xml *x;
        int e = OWF_OK;
        if (n->type != OWF_XML_ELEMENT || strcmp(n->name, "Relationship"))
            continue;
        id = owf_xml_attr(n, "", "Id");
        target = owf_xml_attr(n, "", "Target");
        mode = owf_xml_attr(n, "", "TargetMode");
        if (!id || !target || strcmp(id, rid) || (mode && !strcmp(mode, "External")))
            continue;
        if (!owf_opc_part_name(part, sizeof part, dir, target) || !(cp = owf_book_part(book, part)))
            return 0;
        if (!(x = owf_xml_parse((const char *)cp->data, cp->length, &e))) {
            if (e == OWF_ERR_MEMORY)
                *failed = 1;
            return 0;
        }
        owf_xml_free(x);
        return 1;
    }
    return 0;
}

#define MAX_SHOWN 64                             /* owf_sheet_charts' most on a sheet */

/* Chart index (as owf_sheet_charts counts them) put with its top left corner at to (a cell and an offset in
 * it, each axis), or (by) moved by dx, dy EMU; its size as it was. */
static int place(owf_book *book, int sheet, int index, int by, const edge *to, long dx, long dy)
{
    owf_sheet *sh;
    owf_part *p;
    owf_xml *rels = NULL;
    const owf_part *rp;
    char dir[512], name[1100];
    const char *slash;
    xwalk w;
    xtok t;
    int depth = 0, n = 0, failed = 0, e = OWF_OK;
    if (sheet < 0 || sheet >= book->nsheets || index < 0 || index >= MAX_SHOWN)
        return OWF_ERR_UNSUPPORTED;
    sh = &book->sheets[sheet];
    if (!sh->drawing || !(p = part_named(book, sh->drawing)))
        return OWF_ERR_UNSUPPORTED;
    slash = strrchr(sh->drawing, '/');
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - sh->drawing + 1) : 0, sh->drawing);
    snprintf(name, sizeof name, "%s_rels/%s.rels", dir, slash ? slash + 1 : sh->drawing);
    if ((rp = owf_book_part(book, name)) != NULL && !(rels = owf_xml_parse((const char *)rp->data, rp->length, &e)) &&
        e == OWF_ERR_MEMORY)
        return OWF_ERR_MEMORY;
    w.s = (const char *)p->data;
    w.n = p->length;
    w.i = 0;
    while (xnext(&w, &t) != X_END && t.kind != X_BAD) {
        anchor a;
        change c[8];
        edge from[2], end[2];
        int nc = 0, k;
        owf_buf b;
        if (t.kind == X_CLOSE)
            depth--;
        if (t.kind != X_START || t.empty)
            continue;
        if (++depth != 2 || !is_anchor(&t))
            continue;
        if (!read_anchor(&w, &t, &a))
            break;
        depth--;
        if (!chart_shown(book, dir, rels, a.rid, &failed)) {
            if (failed)
                break;
            continue;
        }
        if (n++ != index)
            continue;
        /* found: where it starts and ends now, along each axis (k 0 rows, 1 columns) */
        for (k = 0; k < 2; k++) {
            int cf = k ? A_COL : A_ROW, of = k ? A_COLOFF : A_ROWOFF;
            double start, size = 0;
            if (a.kind == K_ABSOLUTE) {
                start = a.pos[k ? 0 : 1].found ? (double)a.pos[k ? 0 : 1].v : 0;
            } else {
                start = edge_emu(sh, k, a.at[0][cf].v, a.at[0][of].v);
                if (a.kind == K_TWO)
                    size = edge_emu(sh, k, a.at[1][cf].v, a.at[1][of].v) - start;
            }
            if (by)
                emu_edge(sh, k, start + (double)(k ? dx : dy), &from[k]);
            else
                from[k] = to[k];
            if (a.kind == K_TWO)
                emu_edge(sh, k, edge_emu(sh, k, from[k].cell, from[k].off) + (size > 0 ? size : 0), &end[k]);
            if (a.kind == K_ABSOLUTE) {
                double at = edge_emu(sh, k, from[k].cell, from[k].off);
                add_change(c, &nc, &a.pos[k ? 0 : 1], (long)(at + 0.5));
            } else {
                add_change(c, &nc, &a.at[0][cf], from[k].cell);
                add_change(c, &nc, &a.at[0][of], from[k].off);
                if (a.kind == K_TWO) {
                    add_change(c, &nc, &a.at[1][cf], end[k].cell);
                    add_change(c, &nc, &a.at[1][of], end[k].off);
                }
            }
        }
        owf_xml_free(rels);
        if (!nc)
            return OWF_OK;
        owf_buf_init(&b);
        owf_buf_put(&b, w.s, a.start);
        put_changed(&b, w.s, a.start, a.end, c, nc);
        owf_buf_put(&b, w.s + a.end, w.n - a.end);
        e = replace_part(p, &b) ? OWF_OK : OWF_ERR_MEMORY;
        owf_buf_free(&b);
        return e;
    }
    owf_xml_free(rels);
    return failed ? OWF_ERR_MEMORY : OWF_ERR_UNSUPPORTED;
}

int calc_chart_place(owf_book *book, int sheet, int index, unsigned row, long row_off, unsigned col, long col_off)
{
    edge to[2];
    to[0].cell = row < CALC_MAX_ROWS ? (long)row : (long)CALC_MAX_ROWS - 1;
    to[0].off = row_off > 0 ? row_off : 0;
    to[1].cell = col < CALC_MAX_COLS ? (long)col : (long)CALC_MAX_COLS - 1;
    to[1].off = col_off > 0 ? col_off : 0;
    return place(book, sheet, index, 0, to, 0, 0);
}

int calc_chart_nudge(owf_book *book, int sheet, int index, long dx, long dy)
{
    return place(book, sheet, index, 1, NULL, dx, dy);
}

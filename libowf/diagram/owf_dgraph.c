/*
 * owf_dgraph.c: a diagram's page as a graph in text, for tools that lay graphs out themselves: a Mermaid
 * flowchart and a Graphviz DOT digraph (owf_diagram.h). Each shape is a node with its label and the nearest
 * shape the format has; a shape with shapes inside it (a container, group or swimlane) is a subgraph (a DOT
 * cluster) holding them; each connector joined at both ends is an edge with its label, its dashes and its
 * arrowheads; fill, line and text colours go across where the format takes them. Places are not kept: the tool
 * lays the graph out again, top to bottom, or left to right when the page is wider than it is tall. Nodes are
 * named n1, n2, ... in the page's order, so any cell id is safe.
 * Written from the formats' published documentation (mermaid.js.org's flowchart syntax, MIT; graphviz.org's
 * DOT language and node shapes), not from any program's code.
 * GFOpenDiag step D6 (amicradle-teams designs/GFOPENDIAG-20261113.md).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <stdlib.h>
#include <string.h>

enum { G_MERMAID, G_DOT };

typedef struct {
    const owf_dpage *page;
    owf_buf out;
    int fmt;
    int *node;                           /* per cell: its node number (1 on), 0 when it isn't a node */
    int *cluster;                        /* per cell: 1 when shapes are inside it */
    int links;                           /* Mermaid: the edges written so far */
} walk;

/* ---- What the page holds ---- */

/* A cell drawn as a shape: a shown vertex not riding on an edge. */
static int is_shape(const owf_dpage *p, const owf_dcell *c)
{
    const owf_dcell *parent;
    if (c->kind != OWF_DCELL_VERTEX || !owf_dcell_shown(p, c))
        return 0;
    parent = c->parent ? owf_dpage_find(p, c->parent) : NULL;
    return !(parent && parent->kind == OWF_DCELL_EDGE);
}

static int index_of(const owf_dpage *p, const char *id)
{
    return id ? owf_dpage_index(p, id) : -1;
}

/* The shape a cell is inside, as an index, or -1 at the top. */
static int container_of(const walk *w, const owf_dcell *c)
{
    int i = index_of(w->page, c->parent);
    return i >= 0 && w->node[i] ? i : -1;
}

/* A label's plain text (owf_malloc'd, "" when none), or NULL when memory ran out. */
static char *text_of(const owf_dcell *c)
{
    char buf[8];
    int html = owf_dstyle_get(c->style, "html", buf, sizeof buf) && !strcmp(buf, "1");
    return owf_dlabel_text(c->value ? c->value : "", html, NULL);
}

/* An edge's label: its own, or else the first label riding on it. NULL when memory ran out. */
static char *edge_text(const owf_dpage *p, const owf_dcell *e)
{
    int i;
    if (e->value && *e->value)
        return text_of(e);
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        if (c->kind == OWF_DCELL_VERTEX && c->parent && !strcmp(c->parent, e->id) && c->value && *c->value &&
            owf_dcell_shown(p, c))
            return text_of(c);
    }
    return text_of(e);
}

static int is_on(const char *style, const char *key)
{
    char v[8];
    return owf_dstyle_get(style, key, v, sizeof v) && !strcmp(v, "1");
}

/* A style's colour as "#rrggbb" in buf, or NULL when it names none (or "none", or one this doesn't read). */
static const char *colour(const char *style, const char *key, char *buf, size_t size)
{
    size_t i;
    if (!owf_dstyle_get(style, key, buf, size) || buf[0] != '#' || strlen(buf) != 7)
        return NULL;
    for (i = 1; i < 7; i++)
        if (!strchr("0123456789abcdefABCDEF", buf[i]))
            return NULL;
    return buf;
}

/* An arrowhead at that end: endArrow is one unless it says none; startArrow only when it names one. */
static int arrow_at(const char *style, const char *key, int def)
{
    char v[24];
    if (!owf_dstyle_get(style, key, v, sizeof v) || !v[0])
        return def;
    return strcmp(v, "none") != 0;
}

/* ---- Mermaid ---- */

/* Text inside a Mermaid "...": quotes, #, < and > as Mermaid's entity codes, a line break as <br>. */
static void mm_text(owf_buf *o, const char *s)
{
    for (; *s; s++) {
        if (*s == '"') owf_buf_puts(o, "#quot;");
        else if (*s == '#') owf_buf_puts(o, "#35;");
        else if (*s == '<') owf_buf_puts(o, "#lt;");
        else if (*s == '>') owf_buf_puts(o, "#gt;");
        else if (*s == '\n') owf_buf_puts(o, "<br>");
        else if ((unsigned char)*s >= 32 || *s == '\t') owf_buf_putc(o, *s == '\t' ? ' ' : *s);
    }
}

/* The brackets for a shape: rectangle, rounded, stadium, circle, rhombus, hexagon, parallelogram, cylinder,
 * subroutine. */
static void mm_brackets(const owf_dpage *p, const owf_dcell *c, const char **open, const char **close)
{
    const char *s = c->style;
    owf_dnum box[4];
    *open = "[";
    *close = "]";
    if (owf_dstyle_is(s, "ellipse")) {
        int round = owf_dcell_box(p, c, box) && box[2] == box[3];
        *open = round ? "((" : "([";
        *close = round ? "))" : "])";
    } else if (owf_dstyle_is(s, "rhombus")) {
        *open = "{"; *close = "}";
    } else if (owf_dstyle_is(s, "hexagon")) {
        *open = "{{"; *close = "}}";
    } else if (owf_dstyle_is(s, "parallelogram")) {
        *open = "[/"; *close = "/]";
    } else if (owf_dstyle_is(s, "cylinder") || owf_dstyle_is(s, "cylinder3")) {
        *open = "[("; *close = ")]";
    } else if (owf_dstyle_is(s, "process")) {
        *open = "[["; *close = "]]";
    } else if (is_on(s, "rounded")) {
        *open = "("; *close = ")";
    }
}

static void indent(owf_buf *o, int depth)
{
    int i;
    for (i = 0; i < depth + 1; i++)
        owf_buf_puts(o, "    ");
}

/* ---- DOT ---- */

/* Text inside a DOT "...": quotes and backslashes escaped, a line break as \n. */
static void dot_text(owf_buf *o, const char *s)
{
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            owf_buf_putc(o, '\\');
            owf_buf_putc(o, *s);
        } else if (*s == '\n')
            owf_buf_puts(o, "\\n");
        else if ((unsigned char)*s >= 32 || *s == '\t')
            owf_buf_putc(o, *s == '\t' ? ' ' : *s);
    }
}

static const char *dot_shape(const owf_dpage *p, const owf_dcell *c)
{
    const char *s = c->style;
    owf_dnum box[4];
    if (owf_dstyle_is(s, "text") || owf_dstyle_is(s, "edgeLabel")) return "plaintext";
    if (owf_dstyle_is(s, "ellipse")) return owf_dcell_box(p, c, box) && box[2] == box[3] ? "circle" : "ellipse";
    if (owf_dstyle_is(s, "rhombus")) return "diamond";
    if (owf_dstyle_is(s, "hexagon")) return "hexagon";
    if (owf_dstyle_is(s, "parallelogram")) return "parallelogram";
    if (owf_dstyle_is(s, "triangle")) return "triangle";
    if (owf_dstyle_is(s, "cylinder") || owf_dstyle_is(s, "cylinder3")) return "cylinder";
    if (owf_dstyle_is(s, "note")) return "note";
    return "box";
}

/* ---- The walk ---- */

/* The shapes inside parent (-1: the top), in the page's order, a container as a subgraph round its own. */
static void nodes(walk *w, int parent, int depth)
{
    const owf_dpage *p = w->page;
    owf_buf *o = &w->out;
    int i;
    for (i = 0; i < p->ncells && !o->failed; i++) {
        const owf_dcell *c = &p->cells[i];
        char *t, fb[16], lb[16], cb[16];
        const char *fill, *line, *font;
        if (!w->node[i] || container_of(w, c) != parent)
            continue;
        if (!(t = text_of(c))) {
            o->failed = 1;
            return;
        }
        fill = colour(c->style, "fillColor", fb, sizeof fb);
        line = colour(c->style, "strokeColor", lb, sizeof lb);
        font = colour(c->style, "fontColor", cb, sizeof cb);
        indent(o, depth);
        if (w->cluster[i]) {
            if (w->fmt == G_MERMAID) {
                owf_buf_printf(o, "subgraph n%d [\"", w->node[i]);
                mm_text(o, *t ? t : " ");
                owf_buf_puts(o, "\"]\n");
            } else {
                owf_buf_printf(o, "subgraph cluster_n%d {\n", w->node[i]);
                indent(o, depth + 1);
                owf_buf_puts(o, "label=\"");
                dot_text(o, t);
                owf_buf_puts(o, "\";\n");
                if (fill || line) {
                    indent(o, depth + 1);
                    if (fill) owf_buf_printf(o, "style=filled; fillcolor=\"%s\"; ", fill);
                    if (line) owf_buf_printf(o, "color=\"%s\"; ", line);
                    owf_buf_puts(o, "\n");
                }
            }
            nodes(w, i, depth + 1);
            indent(o, depth);
            owf_buf_puts(o, w->fmt == G_MERMAID ? "end\n" : "}\n");
        } else if (w->fmt == G_MERMAID) {
            const char *open, *close;
            mm_brackets(p, c, &open, &close);
            owf_buf_printf(o, "n%d%s\"", w->node[i], open);
            mm_text(o, *t ? t : " ");
            owf_buf_printf(o, "\"%s\n", close);
        } else {
            int rounded = is_on(c->style, "rounded") && !strcmp(dot_shape(p, c), "box");
            owf_buf_printf(o, "n%d [shape=%s, label=\"", w->node[i], dot_shape(p, c));
            dot_text(o, t);
            owf_buf_putc(o, '"');
            if (fill || rounded || is_on(c->style, "dashed"))
                owf_buf_printf(o, ", style=\"%s%s%s%s%s\"", fill ? "filled" : "", fill && rounded ? "," : "",
                               rounded ? "rounded" : "", (fill || rounded) && is_on(c->style, "dashed") ? "," : "",
                               is_on(c->style, "dashed") ? "dashed" : "");
            if (fill) owf_buf_printf(o, ", fillcolor=\"%s\"", fill);
            if (line) owf_buf_printf(o, ", color=\"%s\"", line);
            if (font) owf_buf_printf(o, ", fontcolor=\"%s\"", font);
            owf_buf_puts(o, "];\n");
        }
        if (w->fmt == G_MERMAID && (fill || line || font || is_on(c->style, "dashed"))) {
            indent(o, depth);
            owf_buf_printf(o, "style n%d ", w->node[i]);
            {
                int any = 0;
                if (fill) { owf_buf_printf(o, "fill:%s", fill); any = 1; }
                if (line) { owf_buf_printf(o, "%sstroke:%s", any ? "," : "", line); any = 1; }
                if (font) { owf_buf_printf(o, "%scolor:%s", any ? "," : "", font); any = 1; }
                if (is_on(c->style, "dashed")) owf_buf_printf(o, "%sstroke-dasharray:5 5", any ? "," : "");
            }
            owf_buf_puts(o, "\n");
        }
        owf_free(t);
    }
}

/* A DOT edge joins nodes: a container's is drawn from its first shape that isn't a container, and ltail or
 * lhead clips it at the cluster's edge. */
static int inner(const walk *w, int i)
{
    int j, found = 1;
    while (w->cluster[i] && found) {
        found = 0;
        for (j = 0; j < w->page->ncells; j++)
            if (w->node[j] && container_of(w, &w->page->cells[j]) == i) {
                i = j;
                found = 1;
                break;
            }
    }
    return w->node[i];
}

static void edges(walk *w)
{
    const owf_dpage *p = w->page;
    owf_buf *o = &w->out;
    int i;
    for (i = 0; i < p->ncells && !o->failed; i++) {
        const owf_dcell *e = &p->cells[i];
        int s, t, head, tail, dashed;
        char *label, lb[16];
        const char *line;
        if (e->kind != OWF_DCELL_EDGE || !owf_dcell_shown(p, e))
            continue;
        s = index_of(p, e->source);
        t = index_of(p, e->target);
        if (s < 0 || t < 0 || !w->node[s] || !w->node[t])
            continue;                    /* a loose end: no graph edge */
        if (!(label = edge_text(p, e))) {
            o->failed = 1;
            return;
        }
        head = arrow_at(e->style, "endArrow", 1);
        tail = arrow_at(e->style, "startArrow", 0);
        if (w->fmt == G_MERMAID && tail && !head) {   /* Mermaid has no arrow pointing back: turn the edge round */
            int k = s;
            s = t;
            t = k;
            head = 1;
            tail = 0;
        }
        dashed = is_on(e->style, "dashed");
        line = colour(e->style, "strokeColor", lb, sizeof lb);
        if (w->fmt == G_MERMAID) {
            /* a mermaid edge between subgraphs joins them; a node inside one joins as itself */
            owf_buf_printf(o, "    n%d %s%s%s", w->node[s], tail && head ? "<" : "", dashed ? "-.-" : "--",
                           head ? ">" : (dashed ? "" : "-"));
            if (*label) {
                owf_buf_puts(o, "|\"");
                mm_text(o, label);
                owf_buf_puts(o, "\"|");
            }
            owf_buf_printf(o, " n%d\n", w->node[t]);
            if (line)                    /* links are numbered from 0 in the order they come */
                owf_buf_printf(o, "    linkStyle %d stroke:%s\n", w->links, line);
            w->links++;
        } else {
            int attrs = 0;
            /* ltail and lhead need compound=true, set in the head */
            owf_buf_printf(o, "    n%d -> n%d", inner(w, s), inner(w, t));
#define ATTR(...) do { owf_buf_puts(o, attrs++ ? ", " : " ["); owf_buf_printf(o, __VA_ARGS__); } while (0)
            if (*label) {
                ATTR("label=\"");
                dot_text(o, label);
                owf_buf_putc(o, '"');
            }
            if (dashed) ATTR("style=dashed");
            if (head && tail) ATTR("dir=both");
            else if (!head && tail) ATTR("dir=back");
            else if (!head) ATTR("dir=none");
            if (line) ATTR("color=\"%s\"", line);
            if (w->cluster[s]) ATTR("ltail=cluster_n%d", w->node[s]);
            if (w->cluster[t]) ATTR("lhead=cluster_n%d", w->node[t]);
#undef ATTR
            owf_buf_puts(o, attrs ? "];\n" : ";\n");
        }
        owf_free(label);
    }
}

static int graph_write(const owf_diagram *d, int page, int fmt, unsigned char **data, size_t *length)
{
    walk w;
    const owf_dpage *p;
    owf_dnum box[4];
    int i, n = 0, wide;
    size_t len = 0;
    *data = NULL;
    if (length)
        *length = 0;
    if (page < 0 || page >= d->npages)
        return OWF_ERR_FORMAT;
    p = &d->pages[page];
    memset(&w, 0, sizeof w);
    w.page = p;
    w.fmt = fmt;
    owf_buf_init(&w.out);
    w.node = calloc((size_t)(p->ncells ? p->ncells : 1), sizeof *w.node);
    w.cluster = calloc((size_t)(p->ncells ? p->ncells : 1), sizeof *w.cluster);
    if (!w.node || !w.cluster) {
        free(w.node);
        free(w.cluster);
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < p->ncells; i++)
        if (is_shape(p, &p->cells[i]))
            w.node[i] = ++n;
    for (i = 0; i < p->ncells; i++) {
        int c = w.node[i] ? container_of(&w, &p->cells[i]) : -1;
        if (c >= 0)
            w.cluster[c] = 1;
    }
    wide = owf_dpage_bounds(p, box) && box[2] > box[3];
    if (fmt == G_MERMAID) {
        if (p->name && *p->name) {      /* the page's name as the chart's title */
            owf_buf_puts(&w.out, "---\ntitle: \"");
            mm_text(&w.out, p->name);
            owf_buf_puts(&w.out, "\"\n---\n");
        }
        owf_buf_printf(&w.out, "flowchart %s\n", wide ? "LR" : "TD");
    } else {
        owf_buf_puts(&w.out, "digraph \"");
        dot_text(&w.out, p->name && *p->name ? p->name : "Diagram");
        owf_buf_printf(&w.out, "\" {\n    rankdir=%s;\n    compound=true;\n"
                               "    node [fontname=\"Helvetica\"];\n    edge [fontname=\"Helvetica\"];\n",
                       wide ? "LR" : "TB");
    }
    nodes(&w, -1, 0);
    edges(&w);
    if (fmt == G_DOT)
        owf_buf_puts(&w.out, "}\n");
    free(w.node);
    free(w.cluster);
    if (owf_buf_take(&w.out, data, &len) != OWF_OK) {
        *data = NULL;
        return OWF_ERR_MEMORY;
    }
    if (length)
        *length = len;
    return OWF_OK;
}

int owf_dmermaid_write(const owf_diagram *d, int page, unsigned char **data, size_t *length)
{
    return graph_write(d, page, G_MERMAID, data, length);
}

int owf_ddot_write(const owf_diagram *d, int page, unsigned char **data, size_t *length)
{
    return graph_write(d, page, G_DOT, data, length);
}

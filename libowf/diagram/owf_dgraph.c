/*
 * owf_dgraph.c: a diagram's page as a graph in text, for tools that lay graphs out themselves: a Mermaid
 * flowchart, a Graphviz DOT digraph and a GraphML graph (owf_diagram.h). Each shape is a node with its label and the nearest
 * shape the format has; a shape with shapes inside it (a container, group or swimlane) is a subgraph (a DOT
 * cluster) holding them; each connector joined at both ends is an edge with its label, its dashes and its
 * arrowheads; fill, line and text colours go across where the format takes them. Places are not kept: the tool
 * lays the graph out again, top to bottom, or left to right when the page is wider than it is tall. GraphML
 * keeps each shape's place and size too (x, y, width, height in pixels on the page), for a tool that reads
 * them, and a container is a node holding a nested graph. Nodes are named n1, n2, ... in the page's order, so
 * any cell id is safe.
 * Written from the formats' published documentation (mermaid.js.org's flowchart syntax, MIT; graphviz.org's
 * DOT language and node shapes; graphdrawing.org's GraphML primer and schema), not from any program's code.
 * GFOpenDiag step D6 (amicradle-teams designs/GFOPENDIAG-20261113.md).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <stdlib.h>
#include <string.h>

enum { G_MERMAID, G_DOT, G_GRAPHML };

#define MAX_DEPTH 64                     /* containers inside containers, at most; a loop of parents is cut */

typedef struct {
    const owf_dpage *page;
    owf_buf out;
    int fmt;
    int *node;                           /* per cell: its node number (1 on), 0 when it isn't a node */
    int *cluster;                        /* per cell: 1 when shapes are inside it */
    int *up;                             /* per cell: the shape it is inside (an index), or -1 at the top */
    int links;                           /* Mermaid and GraphML: the edges written so far */
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

/* The shape a cell is inside, as an index, or -1 at the top (worked out once, loops cut, in graph_write). */
static int container_of(const walk *w, const owf_dcell *c)
{
    return w->up[c - w->page->cells];
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

/* ---- GraphML ---- */

/* Text inside an XML element or "...": &, <, >, " as entities, a line break as &#10;, other control
 * characters left out (XML 1.0 can't hold them). */
static void gm_text(owf_buf *o, const char *s)
{
    for (; *s; s++) {
        if (*s == '&') owf_buf_puts(o, "&amp;");
        else if (*s == '<') owf_buf_puts(o, "&lt;");
        else if (*s == '>') owf_buf_puts(o, "&gt;");
        else if (*s == '"') owf_buf_puts(o, "&quot;");
        else if (*s == '\n') owf_buf_puts(o, "&#10;");
        else if ((unsigned char)*s >= 32 || *s == '\t') owf_buf_putc(o, *s);
    }
}

/* Hundredths of a pixel as pixels: 12.5, -3, 0.07. */
static void gm_px(owf_buf *o, owf_dnum v)
{
    unsigned long a = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
    owf_buf_printf(o, "%s%lu", v < 0 ? "-" : "", a / 100);
    if (a % 100)
        owf_buf_printf(o, a % 10 ? ".%02lu" : ".%lu", a % 10 ? a % 100 : a % 100 / 10);
}

static void gm_data(owf_buf *o, int depth, const char *key, const char *value)
{
    indent(o, depth + 1);
    owf_buf_printf(o, "<data key=\"%s\">", key);
    gm_text(o, value);
    owf_buf_puts(o, "</data>\n");
}

/* The keys GraphML wants declared before the graph: one name for nodes, edges and the graph where they mean
 * the same. */
static const char gm_head[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\"\n"
    "    xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
    "    xsi:schemaLocation=\"http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd\">\n"
    "    <key id=\"label\" for=\"all\" attr.name=\"label\" attr.type=\"string\"/>\n"
    "    <key id=\"shape\" for=\"node\" attr.name=\"shape\" attr.type=\"string\"/>\n"
    "    <key id=\"x\" for=\"node\" attr.name=\"x\" attr.type=\"double\"/>\n"
    "    <key id=\"y\" for=\"node\" attr.name=\"y\" attr.type=\"double\"/>\n"
    "    <key id=\"width\" for=\"node\" attr.name=\"width\" attr.type=\"double\"/>\n"
    "    <key id=\"height\" for=\"node\" attr.name=\"height\" attr.type=\"double\"/>\n"
    "    <key id=\"fill\" for=\"node\" attr.name=\"fill\" attr.type=\"string\"/>\n"
    "    <key id=\"color\" for=\"all\" attr.name=\"color\" attr.type=\"string\"/>\n"
    "    <key id=\"fontcolor\" for=\"node\" attr.name=\"fontcolor\" attr.type=\"string\"/>\n"
    "    <key id=\"dashed\" for=\"all\" attr.name=\"dashed\" attr.type=\"boolean\"><default>false</default></key>\n"
    "    <key id=\"arrows\" for=\"edge\" attr.name=\"arrows\" attr.type=\"string\"><default>forward</default></key>\n";

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
        if (w->fmt == G_GRAPHML) {
            owf_dnum box[4];
            /* yfiles.foldertype="group": yEd's mark for a node with a graph inside, which yEd and networkx
             * need before they read the nested graph; other readers pass over it */
            owf_buf_printf(o, "<node id=\"n%d\"%s>\n", w->node[i], w->cluster[i] ? " yfiles.foldertype=\"group\"" : "");
            if (*t) gm_data(o, depth, "label", t);
            if (!w->cluster[i]) gm_data(o, depth, "shape", dot_shape(p, c));
            if (owf_dcell_box(p, c, box)) {
                static const char *const keys[4] = { "x", "y", "width", "height" };
                int k;
                for (k = 0; k < 4; k++) {
                    indent(o, depth + 1);
                    owf_buf_printf(o, "<data key=\"%s\">", keys[k]);
                    gm_px(o, box[k]);
                    owf_buf_puts(o, "</data>\n");
                }
            }
            if (fill) gm_data(o, depth, "fill", fill);
            if (line) gm_data(o, depth, "color", line);
            if (font) gm_data(o, depth, "fontcolor", font);
            if (is_on(c->style, "dashed")) gm_data(o, depth, "dashed", "true");
            if (w->cluster[i]) {         /* its shapes as a nested graph, named after it as the primer does */
                indent(o, depth + 1);
                owf_buf_printf(o, "<graph id=\"n%d:\" edgedefault=\"directed\">\n", w->node[i]);
                nodes(w, i, depth + 2);
                indent(o, depth + 1);
                owf_buf_puts(o, "</graph>\n");
            }
            indent(o, depth);
            owf_buf_puts(o, "</node>\n");
        } else if (w->cluster[i]) {
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
    int j, found = 1, depth = 0;
    while (w->cluster[i] && found && depth++ < MAX_DEPTH) {
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
        if (w->fmt == G_GRAPHML) {
            /* in the top graph, which holds both ends however deep they are */
            int bare = !*label && !dashed && !line && head && !tail;
            owf_buf_printf(o, "    <edge id=\"e%d\" source=\"n%d\" target=\"n%d\"%s\n", ++w->links, w->node[s],
                           w->node[t], bare ? "/>" : ">");
            if (!bare) {
                if (*label) gm_data(o, 0, "label", label);
                if (line) gm_data(o, 0, "color", line);
                if (dashed) gm_data(o, 0, "dashed", "true");
                if (!head || tail) gm_data(o, 0, "arrows", head ? "both" : tail ? "back" : "none");
                owf_buf_puts(o, "    </edge>\n");
            }
        } else if (w->fmt == G_MERMAID) {
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
    w.up = calloc((size_t)(p->ncells ? p->ncells : 1), sizeof *w.up);
    if (!w.node || !w.cluster || !w.up) {
        free(w.node);
        free(w.cluster);
        free(w.up);
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < p->ncells; i++)
        if (is_shape(p, &p->cells[i]))
            w.node[i] = ++n;
    for (i = 0; i < p->ncells; i++) {
        int k = index_of(p, p->cells[i].parent);
        w.up[i] = k >= 0 && w.node[k] && k != i ? k : -1;
    }
    for (i = 0; i < p->ncells; i++) {   /* a shape whose parents loop, or go deeper than MAX_DEPTH, goes to the top */
        int j = w.up[i], steps = 0;
        while (j >= 0 && j != i && steps < MAX_DEPTH) {
            j = w.up[j];
            steps++;
        }
        if (j >= 0)
            w.up[i] = -1;
    }
    for (i = 0; i < p->ncells; i++) {
        int c = w.node[i] ? container_of(&w, &p->cells[i]) : -1;
        if (c >= 0)
            w.cluster[c] = 1;
    }
    wide = owf_dpage_bounds(p, box) && box[2] > box[3];
    if (fmt == G_GRAPHML) {
        owf_buf_puts(&w.out, gm_head);
        owf_buf_puts(&w.out, "    <graph id=\"G\" edgedefault=\"directed\">\n");
        if (p->name && *p->name)
            gm_data(&w.out, -1, "label", p->name);   /* in line with the top graph's nodes */
    } else if (fmt == G_MERMAID) {
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
                               "    graph [fontname=\"Helvetica\"];\n    node [fontname=\"Helvetica\"];\n"
                               "    edge [fontname=\"Helvetica\"];\n",
                       wide ? "LR" : "TB");
    }
    nodes(&w, -1, 0);
    edges(&w);
    if (fmt == G_DOT)
        owf_buf_puts(&w.out, "}\n");
    else if (fmt == G_GRAPHML)
        owf_buf_puts(&w.out, "    </graph>\n</graphml>\n");
    free(w.node);
    free(w.cluster);
    free(w.up);
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

int owf_dgraphml_write(const owf_diagram *d, int page, unsigned char **data, size_t *length)
{
    return graph_write(d, page, G_GRAPHML, data, length);
}

/*
 * owf_dexcal.c: a diagram's page as an Excalidraw scene (owf_diagram.h), the JSON file excalidraw.com opens.
 * Each shape is a rectangle, ellipse or diamond at its place and size on the page, with its line and fill
 * colours, dashes and rounded corners, and its label as text bound inside it; a shape drawn as bare text is
 * a text element. Each connector is an arrow along its points (start, bends, end), bound to the shapes it
 * joins, with its arrowheads, colour, dashes and label. Lines are drawn clean (roughness 0) so the scene
 * looks like the page. A container is a rectangle with its label at the top; the shapes in it stay where
 * they are. Not carried: rotation, gradients, pictures and other pages (a scene is one page).
 * Written from Excalidraw's published JSON schema (docs.excalidraw.com, the element types in
 * packages/excalidraw/element/types.ts, MIT), not from any program's code.
 * GFOpenDiag step D6 (amicradle-teams designs/GFOPENDIAG-20261113.md).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <stdlib.h>
#include <string.h>

#define FONT_DEFAULT 12                  /* px, as diagrams.net draws a label with no fontSize */

typedef struct {
    const owf_dpage *p;
    owf_buf out;
    int any;                             /* an element written: the next one starts with a comma */
} scene;

/* ---- Small helpers ---- */

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

static int font_size(const char *style)
{
    char v[16];
    long n;
    if (!owf_dstyle_get(style, "fontSize", v, sizeof v))
        return FONT_DEFAULT;
    n = strtol(v, NULL, 10);
    return n >= 4 && n <= 400 ? (int)n : FONT_DEFAULT;
}

/* How many bytes of well-formed UTF-8 start at s (2 to 4), or 0 when the lead byte at s doesn't start one
 * (a stray continuation, an overlong form, a surrogate, past U+10FFFF, or cut short). */
static int utf8_run(const unsigned char *s)
{
    unsigned char c = s[0], lo = 0x80, hi = 0xBF;
    int n, k;
    if (c >= 0xC2 && c <= 0xDF) n = 2;
    else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        if (c == 0xE0) lo = 0xA0;        /* not overlong */
        if (c == 0xED) hi = 0x9F;        /* not a surrogate */
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;        /* not past U+10FFFF */
    } else
        return 0;
    if (s[1] < lo || s[1] > hi)
        return 0;
    for (k = 2; k < n; k++)
        if ((s[k] & 0xC0) != 0x80)
            return 0;
    return n;
}

/* A JSON string's inside: quotes, backslashes and control characters escaped; well-formed UTF-8 passed as it
 * is, and any other byte from 0x80 up as U+FFFD, so a strict JSON reader takes the file. */
static void js_text(owf_buf *o, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 0x80) {
            int n = utf8_run((const unsigned char *)s);
            if (n) {
                owf_buf_put(o, s, (size_t)n);
                s += n - 1;
            } else
                owf_buf_puts(o, "\xEF\xBF\xBD");
        } else if (c == '"' || c == '\\') {
            owf_buf_putc(o, '\\');
            owf_buf_putc(o, c);
        } else if (c == '\n')
            owf_buf_puts(o, "\\n");
        else if (c == '\t')
            owf_buf_puts(o, "\\t");
        else if (c < 32)
            owf_buf_printf(o, "\\u%04x", c);
        else
            owf_buf_putc(o, c);
    }
}

/* Hundredths of a pixel as a JSON number: 12.5, -3, 0.07. */
static void js_px(owf_buf *o, owf_dnum v)
{
    unsigned long a = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
    owf_buf_printf(o, "%s%lu", v < 0 ? "-" : "", a / 100);
    if (a % 100)
        owf_buf_printf(o, a % 10 ? ".%02lu" : ".%lu", a % 10 ? a % 100 : a % 100 / 10);
}

/* The size text takes, near enough for Excalidraw to place it before it measures it again: widest line by
 * lines, in hundredths. */
#define TEXT_MOST 20000L                 /* characters or lines counted, so the sizes stay in a 32-bit long */

static void text_size(const char *t, int fs, owf_dnum *w, owf_dnum *h)
{
    long widest = 0, now = 0, lines = 1;
    for (; *t; t++) {
        if (*t == '\n') {
            lines++;
            now = 0;
        } else if (((unsigned char)*t & 0xC0) != 0x80 && ++now > widest)
            widest = now;                /* a UTF-8 character counts once */
    }
    if (widest > TEXT_MOST) widest = TEXT_MOST;
    if (lines > TEXT_MOST) lines = TEXT_MOST;
    /* fs is 4 to 400 (font_size): at most 20000 * 400 * 125 = 1e9 */
    *w = (owf_dnum)(widest * fs * 55L);  /* about 0.55 of the size per character */
    *h = (owf_dnum)(lines * fs * 125L);  /* Excalidraw's line height 1.25 */
}

/* A cell drawn as a shape: a shown vertex not riding on an edge. */
static int is_shape(const owf_dpage *p, const owf_dcell *c)
{
    const owf_dcell *parent;
    if (c->kind != OWF_DCELL_VERTEX || !owf_dcell_shown(p, c))
        return 0;
    parent = c->parent ? owf_dpage_find(p, c->parent) : NULL;
    return !(parent && parent->kind == OWF_DCELL_EDGE);
}

static int is_bare_text(const owf_dcell *c)
{
    return owf_dstyle_is(c->style, "text") || owf_dstyle_is(c->style, "edgeLabel");
}

/* 1 when shapes are inside c. */
static int holds_shapes(const owf_dpage *p, const owf_dcell *c)
{
    int i;
    for (i = 0; i < p->ncells; i++)
        if (p->cells[i].parent && !strcmp(p->cells[i].parent, c->id) && &p->cells[i] != c &&
            is_shape(p, &p->cells[i]))
            return 1;
    return 0;
}

static char *label_of(const owf_dcell *c)
{
    char buf[8];
    int html = owf_dstyle_get(c->style, "html", buf, sizeof buf) && !strcmp(buf, "1");
    return owf_dlabel_text(c->value ? c->value : "", html, NULL);
}

/* An edge's label: its own, or else the first label riding on it. NULL when memory ran out. */
static char *edge_label(const owf_dpage *p, const owf_dcell *e)
{
    int i;
    if (e->value && *e->value)
        return label_of(e);
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        if (c->kind == OWF_DCELL_VERTEX && c->parent && !strcmp(c->parent, e->id) && c->value && *c->value &&
            owf_dcell_shown(p, c))
            return label_of(c);
    }
    return label_of(e);
}

/* An edge's points when it is drawn as an arrow (owf_malloc'd, *n of them), else NULL: hidden, joined to a
 * shape that isn't drawn (hidden, or not there), or nowhere to draw it. */
static owf_dpoint *arrow_path(const owf_dpage *p, const owf_dcell *e, int *n)
{
    int src, dst;
    owf_dpoint *pts;
    *n = 0;
    if (e->kind != OWF_DCELL_EDGE || !owf_dcell_shown(p, e))
        return NULL;
    src = e->source ? owf_dpage_index(p, e->source) : -1;
    dst = e->target ? owf_dpage_index(p, e->target) : -1;
    if ((e->source && (src < 0 || !is_shape(p, &p->cells[src]))) || (e->target && (dst < 0 || !is_shape(p, &p->cells[dst]))))
        return NULL;
    if ((pts = owf_dedge_path(p, e, n)) && *n < 2) {
        owf_free(pts);
        pts = NULL;
    }
    return pts;
}

/* ---- Elements ---- */

/* A seed or nonce from the element's number: unsigned so it wraps instead of overflowing, kept to 31 bits. */
static unsigned long seed_of(int index, unsigned long by, char prefix)
{
    return ((unsigned long)index * by + (unsigned char)prefix) & 0x7FFFFFFFUL;
}

/* What every element has; the caller writes its own fields after it, then "}". kind: "rectangle", ... */
static void element(scene *s, const char *kind, char prefix, int index, const owf_dnum box[4], const char *stroke,
                    const char *fill, int dashed, int rounded)
{
    owf_buf *o = &s->out;
    owf_buf_printf(o, "%s\n    {\"id\": \"%c%d\", \"type\": \"%s\", \"x\": ", s->any++ ? "," : "", prefix, index, kind);
    js_px(o, box[0]);
    owf_buf_puts(o, ", \"y\": ");
    js_px(o, box[1]);
    owf_buf_puts(o, ", \"width\": ");
    js_px(o, box[2]);
    owf_buf_puts(o, ", \"height\": ");
    js_px(o, box[3]);
    owf_buf_printf(o, ", \"angle\": 0, \"strokeColor\": \"%s\", \"backgroundColor\": \"%s\", \"fillStyle\": \"solid\", "
                      "\"strokeWidth\": 1, \"strokeStyle\": \"%s\", \"roughness\": 0, \"opacity\": 100, "
                      "\"groupIds\": [], \"frameId\": null, \"roundness\": %s, \"seed\": %lu, \"version\": 1, "
                      "\"versionNonce\": %lu, \"isDeleted\": false, \"updated\": 1, \"link\": null, \"locked\": false",
                   stroke ? stroke : "#1e1e1e", fill ? fill : "transparent", dashed ? "dashed" : "solid",
                   rounded ? "{\"type\": 3}" : "null", seed_of(index, 7919UL, prefix), seed_of(index, 104729UL, prefix));
}

/* Text: inside container (prefix and index; prefix 0 for none) when it is a shape's or arrow's label. box is
 * where it sits; top puts it at the top of the box (a container's title) rather than the middle. */
static void text_element(scene *s, int index, const owf_dnum where[4], const char *t, int fs, const char *colour_,
                         char prefix, int container, int top)
{
    owf_buf *o = &s->out;
    owf_dnum tw, th, box[4];
    text_size(t, fs, &tw, &th);
    box[0] = where[0] + (where[2] - tw) / 2;
    box[1] = top ? where[1] + OWF_DNUM(4) : where[1] + (where[3] - th) / 2;
    box[2] = tw;
    box[3] = th;
    element(s, "text", 't', index, box, colour_, NULL, 0, 0);
    owf_buf_puts(o, ", \"boundElements\": null, \"text\": \"");
    js_text(o, t);
    owf_buf_printf(o, "\", \"originalText\": \"");
    js_text(o, t);
    owf_buf_printf(o, "\", \"fontSize\": %d, \"fontFamily\": 2, \"textAlign\": \"center\", \"verticalAlign\": \"%s\", "
                      "\"lineHeight\": 1.25, \"autoResize\": true, \"containerId\": ",
                   fs, top ? "top" : "middle");
    if (prefix)
        owf_buf_printf(o, "\"%c%d\"}", prefix, container);
    else
        owf_buf_puts(o, "null}");
}

/* boundElements: the label inside it and every arrow joined to it. */
static void bound(scene *s, const owf_dcell *c, int index, int has_text)
{
    const owf_dpage *p = s->p;
    owf_buf *o = &s->out;
    int i, n = 0;
    owf_buf_puts(o, ", \"boundElements\": [");
    if (has_text)
        owf_buf_printf(o, "%s{\"type\": \"text\", \"id\": \"t%d\"}", n++ ? ", " : "", index);
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *e = &p->cells[i];
        owf_dpoint *pts;
        int np;
        if (e->kind != OWF_DCELL_EDGE ||
            !((e->source && !strcmp(e->source, c->id)) || (e->target && !strcmp(e->target, c->id))))
            continue;
        if ((pts = arrow_path(p, e, &np))) {
            owf_buf_printf(o, "%s{\"type\": \"arrow\", \"id\": \"a%d\"}", n++ ? ", " : "", i + 1);
            owf_free(pts);
        }
    }
    owf_buf_puts(o, "]}");
}

static int write_shape(scene *s, int i)
{
    const owf_dpage *p = s->p;
    const owf_dcell *c = &p->cells[i];
    owf_dnum box[4];
    char fb[16], lb[16], cb[16], *t;
    const char *fill, *line, *font, *kind;
    int fs = font_size(c->style), container, bare;
    if (!owf_dcell_box(p, c, box))
        return 1;                        /* nowhere to draw it */
    if (!(t = label_of(c)))
        return 0;
    fill = colour(c->style, "fillColor", fb, sizeof fb);
    line = colour(c->style, "strokeColor", lb, sizeof lb);
    font = colour(c->style, "fontColor", cb, sizeof cb);
    bare = is_bare_text(c);
    if (bare) {                          /* text with no shape round it */
        if (*t)
            text_element(s, i + 1, box, t, fs, font, 0, 0, 0);
        owf_free(t);
        return 1;
    }
    container = holds_shapes(p, c) || owf_dstyle_is(c->style, "swimlane");
    kind = owf_dstyle_is(c->style, "ellipse") ? "ellipse" : owf_dstyle_is(c->style, "rhombus") ? "diamond" : "rectangle";
    element(s, kind, 's', i + 1, box, line, fill, is_on(c->style, "dashed"),
            !strcmp(kind, "rectangle") && is_on(c->style, "rounded"));
    bound(s, c, i + 1, *t != 0);
    if (*t)
        text_element(s, i + 1, box, t, fs, font, 's', i + 1, container);
    owf_free(t);
    return 1;
}

static int write_arrow(scene *s, int i)
{
    const owf_dpage *p = s->p;
    const owf_dcell *e = &p->cells[i];
    owf_buf *o = &s->out;
    owf_dpoint *pts;
    owf_dnum box[4], minx, miny, maxx, maxy;
    int n = 0, k, src, dst;
    char lb[16], sv[24], ev[24], *t;
    const char *line;
    if (!(pts = arrow_path(p, e, &n)))
        return 1;                        /* not drawn (or memory ran out finding where: the scene goes without it) */
    src = e->source ? owf_dpage_index(p, e->source) : -1;
    dst = e->target ? owf_dpage_index(p, e->target) : -1;
    if (!(t = edge_label(p, e))) {
        owf_free(pts);
        return 0;
    }
    minx = maxx = pts[0].x;
    miny = maxy = pts[0].y;
    for (k = 1; k < n; k++) {
        if (pts[k].x < minx) minx = pts[k].x;
        if (pts[k].x > maxx) maxx = pts[k].x;
        if (pts[k].y < miny) miny = pts[k].y;
        if (pts[k].y > maxy) maxy = pts[k].y;
    }
    box[0] = pts[0].x;                   /* an arrow is at its first point; points are from there */
    box[1] = pts[0].y;
    box[2] = maxx - minx;
    box[3] = maxy - miny;
    line = colour(e->style, "strokeColor", lb, sizeof lb);
    element(s, "arrow", 'a', i + 1, box, line, NULL, is_on(e->style, "dashed"), 0);
    owf_buf_puts(o, ", \"points\": [");
    for (k = 0; k < n; k++) {
        owf_buf_puts(o, k ? ", [" : "[");
        js_px(o, pts[k].x - pts[0].x);
        owf_buf_puts(o, ", ");
        js_px(o, pts[k].y - pts[0].y);
        owf_buf_putc(o, ']');
    }
    owf_buf_puts(o, "]");
    owf_buf_puts(o, ", \"startBinding\": ");
    if (src >= 0 && is_shape(p, &p->cells[src]) && !is_bare_text(&p->cells[src]))
        owf_buf_printf(o, "{\"elementId\": \"s%d\", \"focus\": 0, \"gap\": 1}", src + 1);
    else
        owf_buf_puts(o, "null");
    owf_buf_puts(o, ", \"endBinding\": ");
    if (dst >= 0 && is_shape(p, &p->cells[dst]) && !is_bare_text(&p->cells[dst]))
        owf_buf_printf(o, "{\"elementId\": \"s%d\", \"focus\": 0, \"gap\": 1}", dst + 1);
    else
        owf_buf_puts(o, "null");
    /* endArrow is an arrowhead unless it says none; startArrow only when it names one */
    owf_buf_printf(o, ", \"startArrowhead\": %s, \"endArrowhead\": %s, \"elbowed\": false",
                   owf_dstyle_get(e->style, "startArrow", sv, sizeof sv) && sv[0] && strcmp(sv, "none") ? "\"arrow\"" : "null",
                   owf_dstyle_get(e->style, "endArrow", ev, sizeof ev) && ev[0] && !strcmp(ev, "none") ? "null" : "\"arrow\"");
    if (*t) {
        owf_dpoint mid = owf_diag_along(pts, n, 0);
        owf_dnum at[4];
        owf_buf_printf(o, ", \"boundElements\": [{\"type\": \"text\", \"id\": \"t%d\"}]}", i + 1);
        at[0] = mid.x;
        at[1] = mid.y;
        at[2] = at[3] = 0;
        text_element(s, i + 1, at, t, font_size(e->style), NULL, 'a', i + 1, 0);
    } else
        owf_buf_puts(o, ", \"boundElements\": null}");
    owf_free(t);
    owf_free(pts);
    return 1;
}

int owf_dexcal_write(const owf_diagram *d, int page, unsigned char **data, size_t *length)
{
    scene s;
    int i, ok = 1;
    size_t len = 0;
    *data = NULL;
    if (length)
        *length = 0;
    if (page < 0 || page >= d->npages)
        return OWF_ERR_FORMAT;
    memset(&s, 0, sizeof s);
    s.p = &d->pages[page];
    owf_buf_init(&s.out);
    owf_buf_puts(&s.out, "{\n  \"type\": \"excalidraw\",\n  \"version\": 2,\n  \"source\": \"GFOpenDiag\",\n"
                         "  \"elements\": [");
    for (i = 0; i < s.p->ncells && ok && !s.out.failed; i++)       /* shapes first, in the page's order */
        if (is_shape(s.p, &s.p->cells[i]))
            ok = write_shape(&s, i);
    for (i = 0; i < s.p->ncells && ok && !s.out.failed; i++)       /* arrows over them */
        if (s.p->cells[i].kind == OWF_DCELL_EDGE)
            ok = write_arrow(&s, i);
    owf_buf_puts(&s.out, s.any ? "\n  ],\n" : "],\n");
    owf_buf_puts(&s.out, "  \"appState\": {\"viewBackgroundColor\": \"#ffffff\", \"gridSize\": null},\n"
                         "  \"files\": {}\n}\n");
    if (!ok || s.out.failed) {
        owf_buf_free(&s.out);
        return OWF_ERR_MEMORY;
    }
    if (owf_buf_take(&s.out, data, &len) != OWF_OK) {
        *data = NULL;
        return OWF_ERR_MEMORY;
    }
    if (length)
        *length = len;
    return OWF_OK;
}

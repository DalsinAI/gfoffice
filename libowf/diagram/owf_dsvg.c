/*
 * owf_dsvg.c: where cells are on a page, and a page drawn as SVG with the whole diagram inside it
 * (owf_diagram.h). Integer arithmetic only.
 * Written from published descriptions, not from any program's code:
 *  - W3C's SVG 1.1 (second edition) for the elements and attributes drawn;
 *  - diagrams.net's documentation on editable SVG and PNG exports: the mxfile in the SVG root's
 *    content attribute;
 *  - mxGraph's API documentation (Apache 2.0, by name only) for the style keys read (fillColor,
 *    strokeColor, strokeWidth, dashed, rounded, arcSize, fontColor, fontSize, fontStyle, align,
 *    verticalAlign, startSize, endArrow, startArrow, html, visible) and their defaults.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64                     /* cells inside cells, at most, when finding a place */
#define MAX_LINES 64                     /* a label's lines, at most */
#define SVG_NS "http://www.w3.org/2000/svg"

typedef long long wide;                  /* products of two owf_dnums */

/* ---- Where cells are ---- */

static int box_of(const owf_dpage *p, const owf_dcell *c, owf_dnum box[4], int depth)
{
    const owf_dcell *parent;
    if (!(c->geo.flags & OWF_DGEO_HAS) || depth > MAX_DEPTH)
        return 0;
    parent = owf_dpage_find(p, c->parent);
    if (parent && parent->kind == OWF_DCELL_EDGE)
        return 0;                        /* a label along an edge: placed with the edge */
    box[0] = c->geo.x;
    box[1] = c->geo.y;
    box[2] = c->geo.width;
    box[3] = c->geo.height;
    if (parent && parent->kind == OWF_DCELL_VERTEX && !(c->geo.flags & OWF_DGEO_RELATIVE)) {
        owf_dnum pb[4];
        if (box_of(p, parent, pb, depth + 1)) {
            box[0] += pb[0];
            box[1] += pb[1];
        }
    }
    return 1;
}

int owf_dcell_box(const owf_dpage *p, const owf_dcell *c, owf_dnum box[4])
{
    return box_of(p, c, box, 0);
}

/* 0 when the cell or anything it is inside has visible="0". */
int owf_dcell_shown(const owf_dpage *p, const owf_dcell *c)
{
    int depth = 0;
    while (c && depth++ <= MAX_DEPTH) {
        const char *v = owf_dattrs_get(&c->extra, "visible");
        if (v && !strcmp(v, "0"))
            return 0;
        c = owf_dpage_find(p, c->parent);
    }
    return 1;
}

/* x, y of the edge of box on the line from its centre towards (tx, ty). */
static owf_dpoint clip_to_box(const owf_dnum box[4], owf_dnum tx, owf_dnum ty)
{
    owf_dpoint r;
    owf_dnum cx = box[0] + box[2] / 2, cy = box[1] + box[3] / 2;
    wide dx = tx - cx, dy = ty - cy, hw = box[2] / 2, hh = box[3] / 2;
    wide adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    r.x = cx;
    r.y = cy;
    if (!adx && !ady)
        return r;
    /* the side the line leaves by: compare hw/|dx| with hh/|dy| without dividing */
    if (ady * hw <= adx * hh) {
        r.x = (owf_dnum)(cx + (dx < 0 ? -hw : hw));
        r.y = (owf_dnum)(cy + (adx ? dy * hw / adx : 0));
    } else {
        r.x = (owf_dnum)(cx + (ady ? dx * hh / ady : 0));
        r.y = (owf_dnum)(cy + (dy < 0 ? -hh : hh));
    }
    return r;
}

/* A port's place on a box: style keys kx and ky, fractions of its width and height ("0.5"). */
static int port_point(const char *style, const char *kx, const char *ky, const owf_dnum box[4], owf_dpoint *at)
{
    char bx[24], by[24];
    if (!owf_dstyle_get(style, kx, bx, sizeof bx) || !owf_dstyle_get(style, ky, by, sizeof by) || !bx[0] || !by[0])
        return 0;
    at->x = (owf_dnum)(box[0] + (wide)box[2] * owf_dnum_parse(bx) / 100);
    at->y = (owf_dnum)(box[1] + (wide)box[3] * owf_dnum_parse(by) / 100);
    return 1;
}

owf_dpoint *owf_dedge_path(const owf_dpage *p, const owf_dcell *e, int *n)
{
    const owf_dcell *src = owf_dpage_find(p, e->source), *dst = owf_dpage_find(p, e->target);
    const owf_dcell *parent = owf_dpage_find(p, e->parent);
    owf_dnum sb[4], tb[4], pb[4] = { 0, 0, 0, 0 };
    int has_s = src && owf_dcell_box(p, src, sb), has_t = dst && owf_dcell_box(p, dst, tb), i, k = 0;
    owf_dpoint *pts;

    *n = 0;
    if (!has_s && !(e->geo.flags & OWF_DGEO_SOURCE_POINT))
        return NULL;
    if (!has_t && !(e->geo.flags & OWF_DGEO_TARGET_POINT))
        return NULL;
    if (parent && parent->kind == OWF_DCELL_VERTEX)
        owf_dcell_box(p, parent, pb);
    pts = owf_malloc((size_t)(e->geo.npoints + 2) * sizeof *pts);
    if (!pts)
        return NULL;
    k = 1;
    for (i = 0; i < e->geo.npoints; i++) {
        pts[k].x = e->geo.points[i].x + pb[0];
        pts[k].y = e->geo.points[i].y + pb[1];
        k++;
    }
    /* loose ends first, so that a joined end can aim at the point next to it */
    if (!has_s) {
        pts[0].x = e->geo.source_point.x + pb[0];
        pts[0].y = e->geo.source_point.y + pb[1];
    }
    if (!has_t) {
        pts[k].x = e->geo.target_point.x + pb[0];
        pts[k].y = e->geo.target_point.y + pb[1];
    }
    if (has_s && port_point(e->style, "exitX", "exitY", sb, &pts[0]))
        has_s = 0;                       /* placed by its port */
    if (has_t && port_point(e->style, "entryX", "entryY", tb, &pts[k]))
        has_t = 2;                       /* placed by its port; still aims the start */
    if (has_s) {
        owf_dpoint toward;
        if (k > 1)
            toward = pts[1];
        else if (has_t == 2)
            toward = pts[k];
        else if (has_t) {
            toward.x = tb[0] + tb[2] / 2;
            toward.y = tb[1] + tb[3] / 2;
        } else
            toward = pts[k];
        pts[0] = clip_to_box(sb, toward.x, toward.y);
    }
    if (has_t == 1)
        pts[k] = clip_to_box(tb, pts[k - 1].x, pts[k - 1].y);
    *n = k + 1;
    return pts;
}

static void grow(owf_dnum box[4], int *any, owf_dnum x, owf_dnum y, owf_dnum w, owf_dnum h)
{
    if (!*any) {
        box[0] = x;
        box[1] = y;
        box[2] = x + w;
        box[3] = y + h;
        *any = 1;
        return;
    }
    if (x < box[0]) box[0] = x;
    if (y < box[1]) box[1] = y;
    if (x + w > box[2]) box[2] = x + w;
    if (y + h > box[3]) box[3] = y + h;
}

int owf_dpage_bounds(const owf_dpage *p, owf_dnum box[4])
{
    int i, j, any = 0;
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        owf_dnum b[4];
        if (!owf_dcell_shown(p, c))
            continue;
        if (c->kind == OWF_DCELL_VERTEX && owf_dcell_box(p, c, b))
            grow(box, &any, b[0], b[1], b[2], b[3]);
        else if (c->kind == OWF_DCELL_EDGE) {
            int n;
            owf_dpoint *pts = owf_dedge_path(p, c, &n);
            for (j = 0; j < n; j++)
                grow(box, &any, pts[j].x, pts[j].y, 0, 0);
            owf_free(pts);
        }
    }
    if (any) {
        box[2] -= box[0];
        box[3] -= box[1];
    }
    return any;
}

/* ---- Drawing ---- */

/* Integer square root, for lengths along an edge. */
long long owf_diag_isqrt(long long v)
{
    wide r = 0, bit = (wide)1 << 62;
    if (v <= 0)
        return 0;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else
            r >>= 1;
        bit >>= 2;
    }
    return r;
}

static wide seg_len(owf_dpoint a, owf_dpoint b)
{
    wide dx = b.x - a.x, dy = b.y - a.y;
    return owf_diag_isqrt(dx * dx + dy * dy);
}

/* The point at frac (hundredths, -100 the start, 0 the middle, 100 the end) of the way along a path. */
owf_dpoint owf_diag_along(const owf_dpoint *pts, int n, long frac)
{
    wide total = 0, want, at = 0;
    int i;
    if (frac < -100) frac = -100;
    if (frac > 100) frac = 100;
    for (i = 1; i < n; i++)
        total += seg_len(pts[i - 1], pts[i]);
    want = total * (frac + 100) / 200;
    for (i = 1; i < n; i++) {
        wide len = seg_len(pts[i - 1], pts[i]);
        if (at + len >= want && len > 0) {
            owf_dpoint r;
            r.x = (owf_dnum)(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * (want - at) / len);
            r.y = (owf_dnum)(pts[i - 1].y + (pts[i].y - pts[i - 1].y) * (want - at) / len);
            return r;
        }
        at += len;
    }
    return pts[n - 1];
}

static void put_num(owf_buf *b, owf_dnum n)
{
    char num[24];
    owf_dnum_format(n, num, sizeof num);
    owf_buf_puts(b, num);
}

static void put_attr_num(owf_buf *b, const char *name, owf_dnum n)
{
    owf_buf_putc(b, ' ');
    owf_buf_puts(b, name);
    owf_buf_puts(b, "=\"");
    put_num(b, n);
    owf_buf_putc(b, '"');
}

static void put_attr_text(owf_buf *b, const char *name, const char *v)
{
    owf_buf_putc(b, ' ');
    owf_buf_puts(b, name);
    owf_buf_puts(b, "=\"");
    owf_diag_put_value(b, v);
    owf_buf_putc(b, '"');
}

/* A style value, or def when it has none. */
static const char *style_or(const char *style, const char *key, char *buf, size_t size, const char *def)
{
    if (owf_dstyle_get(style, key, buf, size) && buf[0])
        return buf;
    return def;
}

/* A colour as SVG takes it: "#rrggbb" and "none" as they are, draw.io's "default" as def. */
static const char *colour(const char *style, const char *key, char *buf, size_t size, const char *def)
{
    const char *c = style_or(style, key, buf, size, def);
    if (!strcmp(c, "default") || !strcmp(c, "inherit"))
        return def;
    return c;
}

int owf_dstyle_is(const char *style, const char *word)
{
    char v[8];
    char shape[32];
    if (owf_dstyle_get(style, word, v, sizeof v) && !v[0])
        return 1;                        /* "ellipse;" */
    return owf_dstyle_get(style, "shape", shape, sizeof shape) && !strcmp(shape, word);
}

/* stroke, its width and dashes, and fill. */
static void put_paint(owf_buf *b, const char *style, const char *fill_def)
{
    char buf[40];
    owf_dnum width = owf_dnum_parse(style_or(style, "strokeWidth", buf, sizeof buf, "1"));
    put_attr_text(b, "fill", colour(style, "fillColor", buf, sizeof buf, fill_def));
    put_attr_text(b, "stroke", colour(style, "strokeColor", buf, sizeof buf, "#000000"));
    if (width != 100)
        put_attr_num(b, "stroke-width", width);
    if (owf_dstyle_get(style, "dashed", buf, sizeof buf) && !strcmp(buf, "1"))
        owf_buf_puts(b, " stroke-dasharray=\"3 3\"");
}

/* A label's text: an HTML label's tags taken out, its breaks and blocks made new lines, its entities
 * read. Lines go between '\n' into out. */
static void label_text(owf_buf *out, const char *s, int html)
{
    if (!html) {
        owf_buf_puts(out, s);
        return;
    }
    while (*s) {
        if (*s == '<') {
            const char *end = strchr(s, '>'), *name = s + 1;
            size_t len = 0;
            if (!end)
                break;
            if (*name == '/')
                name++;
            while (name + len < end && ((name[len] >= 'a' && name[len] <= 'z') || (name[len] >= 'A' && name[len] <= 'Z')))
                len++;
            if (len == 2 && (!memcmp(name, "br", 2) || !memcmp(name, "BR", 2)))
                owf_buf_putc(out, '\n');
            else if (s[1] != '/' && ((len == 3 && (!memcmp(name, "div", 3) || !memcmp(name, "DIV", 3))) ||
                                     (len == 1 && (*name == 'p' || *name == 'P')) ||
                                     (len == 2 && (!memcmp(name, "li", 2) || !memcmp(name, "LI", 2)))) &&
                     out->length && out->data[out->length - 1] != '\n')
                owf_buf_putc(out, '\n');
            s = end + 1;
        } else if (*s == '&') {
            const char *semi = strchr(s, ';');
            size_t n = semi ? (size_t)(semi - s) : 0;
            if (n == 5 && !memcmp(s, "&nbsp", 5)) owf_buf_putc(out, ' ');
            else if (n == 4 && !memcmp(s, "&amp", 4)) owf_buf_putc(out, '&');
            else if (n == 3 && !memcmp(s, "&lt", 3)) owf_buf_putc(out, '<');
            else if (n == 3 && !memcmp(s, "&gt", 3)) owf_buf_putc(out, '>');
            else if (n == 5 && !memcmp(s, "&quot", 5)) owf_buf_putc(out, '"');
            else if (n > 2 && n < 10 && s[1] == '#') {
                unsigned long c = s[2] == 'x' || s[2] == 'X' ? strtoul(s + 3, NULL, 16) : strtoul(s + 2, NULL, 10);
                if (c == 160)
                    c = ' ';
                if (c && c < 0x80)
                    owf_buf_putc(out, (int)c);
                else if (c && c < 0x800) {
                    owf_buf_putc(out, (int)(0xc0 | (c >> 6)));
                    owf_buf_putc(out, (int)(0x80 | (c & 63)));
                } else if (c && c < 0x110000) {
                    if (c >= 0x10000) {
                        owf_buf_putc(out, (int)(0xf0 | (c >> 18)));
                        owf_buf_putc(out, (int)(0x80 | ((c >> 12) & 63)));
                    } else
                        owf_buf_putc(out, (int)(0xe0 | (c >> 12)));
                    owf_buf_putc(out, (int)(0x80 | ((c >> 6) & 63)));
                    owf_buf_putc(out, (int)(0x80 | (c & 63)));
                }
            } else {
                owf_buf_putc(out, '&');
                s++;
                continue;
            }
            s = semi + 1;
        } else
            owf_buf_putc(out, *s++);
    }
}

char *owf_dlabel_text(const char *value, int html, size_t *length)
{
    owf_buf b;
    owf_buf_init(&b);
    label_text(&b, value ? value : "", html);
    owf_buf_putc(&b, 0);
    if (b.failed) {
        owf_buf_free(&b);
        return NULL;
    }
    if (length)
        *length = b.length - 1;
    return (char *)b.data;
}

/* A label at (x, y): its anchor ("start", "middle", "end") across, and down from y as valign says
 * ("top": its first line's top at y; "middle": centred on y; "bottom": its last line's foot at y). */
static void put_label(owf_buf *b, const char *value, const char *style, owf_dnum x, owf_dnum y,
                      const char *anchor, const char *valign, const char *bg_def)
{
    char bgb[40];
    const char *bg = colour(style, "labelBackgroundColor", bgb, sizeof bgb, bg_def);
    char buf[40];
    owf_buf text;
    owf_dnum size = owf_dnum_parse(style_or(style, "fontSize", buf, sizeof buf, "12")), line, top;
    long fstyle = owf_dnum_parse(style_or(style, "fontStyle", buf, sizeof buf, "0")) / 100;
    const char *lines[MAX_LINES];
    size_t starts[MAX_LINES], lens[MAX_LINES];
    int nlines = 0, i, html = owf_dstyle_get(style, "html", buf, sizeof buf) && !strcmp(buf, "1");
    size_t at, from = 0;

    if (!value || !*value)
        return;
    if (size <= 0)
        size = 1200;
    line = size * 6 / 5;
    owf_buf_init(&text);
    label_text(&text, value, html);
    if (text.failed || !text.length) {
        b->failed |= text.failed;
        owf_buf_free(&text);
        return;
    }
    for (at = 0; at <= text.length && nlines < MAX_LINES; at++)
        if (at == text.length || text.data[at] == '\n') {
            starts[nlines] = from;
            lens[nlines] = at - from;
            lines[nlines] = (const char *)text.data;
            nlines++;
            from = at + 1;
        }
    if (!strcmp(valign, "top"))
        top = y;
    else if (!strcmp(valign, "bottom"))
        top = y - line * nlines;
    else
        top = y - line * nlines / 2;
    if (strcmp(bg, "none")) {
        /* behind the text: as wide as its longest line at about 0.55 em a letter (no font metrics here) */
        size_t widest = 0, k, chars;
        owf_dnum w, left;
        for (i = 0; i < nlines; i++) {
            for (k = chars = 0; k < lens[i]; k++)
                chars += ((unsigned char)lines[i][starts[i] + k] & 0xc0) != 0x80;
            if (chars > widest)
                widest = chars;
        }
        w = (owf_dnum)((wide)widest * size * 55 / 100) + 200;
        left = !strcmp(anchor, "start") ? x - 100 : !strcmp(anchor, "end") ? x - w + 100 : x - w / 2;
        owf_buf_puts(b, "<rect");
        put_attr_num(b, "x", left);
        put_attr_num(b, "y", top);
        put_attr_num(b, "width", w);
        put_attr_num(b, "height", line * nlines);
        put_attr_text(b, "fill", bg);
        owf_buf_puts(b, " stroke=\"none\" />\n");
    }
    owf_buf_puts(b, "<text");
    put_attr_num(b, "x", x);
    put_attr_text(b, "text-anchor", anchor);
    put_attr_text(b, "font-family", style_or(style, "fontFamily", buf, sizeof buf, "Helvetica"));
    put_attr_num(b, "font-size", size);
    put_attr_text(b, "fill", colour(style, "fontColor", buf, sizeof buf, "#000000"));
    if (fstyle & 1)
        owf_buf_puts(b, " font-weight=\"bold\"");
    if (fstyle & 2)
        owf_buf_puts(b, " font-style=\"italic\"");
    if (fstyle & 4)
        owf_buf_puts(b, " text-decoration=\"underline\"");
    owf_buf_putc(b, '>');
    for (i = 0; i < nlines; i++) {
        /* each line's baseline: its top plus the font's size less a fifth (descent) and half the gap */
        owf_buf_puts(b, "<tspan");
        put_attr_num(b, "x", x);
        put_attr_num(b, "y", top + line * i + size * 9 / 10);
        owf_buf_putc(b, '>');
        owf_buf_put_xml(b, lines[i] + starts[i], lens[i]);
        owf_buf_puts(b, "</tspan>");
    }
    owf_buf_puts(b, "</text>\n");
    owf_buf_free(&text);
}

/* A label inside a box, placed as align and verticalAlign say. */
static void put_box_label(owf_buf *b, const owf_dcell *c, const owf_dnum box[4], owf_dnum header)
{
    char ab[16], vb[16];
    const char *align = style_or(c->style, "align", ab, sizeof ab, "center");
    const char *valign = header ? "middle" : style_or(c->style, "verticalAlign", vb, sizeof vb, "middle");
    owf_dnum x, y, pad = 200;
    const char *anchor = "middle";
    if (!strcmp(align, "left")) {
        x = box[0] + pad;
        anchor = "start";
    } else if (!strcmp(align, "right")) {
        x = box[0] + box[2] - pad;
        anchor = "end";
    } else
        x = box[0] + box[2] / 2;
    if (header)
        y = box[1] + header / 2;
    else if (!strcmp(valign, "top"))
        y = box[1] + pad;
    else if (!strcmp(valign, "bottom"))
        y = box[1] + box[3] - pad;
    else
        y = box[1] + box[3] / 2;
    put_label(b, c->value, c->style, x, y, anchor, valign, "none");
}

static void put_vertex(owf_buf *b, const owf_dpage *p, const owf_dcell *c)
{
    owf_dnum box[4], header = 0;
    char buf[40];
    const char *style = c->style;
    int bare = owf_dstyle_is(style, "text") || owf_dstyle_is(style, "edgeLabel");

    if (!owf_dcell_box(p, c, box))
        return;
    if (bare)
        ;
    else if (owf_dstyle_is(style, "ellipse")) {
        owf_buf_puts(b, "<ellipse");
        put_attr_num(b, "cx", box[0] + box[2] / 2);
        put_attr_num(b, "cy", box[1] + box[3] / 2);
        put_attr_num(b, "rx", box[2] / 2);
        put_attr_num(b, "ry", box[3] / 2);
        put_paint(b, style, "#ffffff");
        owf_buf_puts(b, " />\n");
    } else if (owf_dstyle_is(style, "rhombus")) {
        owf_buf_puts(b, "<path d=\"M ");
        put_num(b, box[0] + box[2] / 2); owf_buf_putc(b, ' '); put_num(b, box[1]);
        owf_buf_puts(b, " L "); put_num(b, box[0] + box[2]); owf_buf_putc(b, ' '); put_num(b, box[1] + box[3] / 2);
        owf_buf_puts(b, " L "); put_num(b, box[0] + box[2] / 2); owf_buf_putc(b, ' '); put_num(b, box[1] + box[3]);
        owf_buf_puts(b, " L "); put_num(b, box[0]); owf_buf_putc(b, ' '); put_num(b, box[1] + box[3] / 2);
        owf_buf_puts(b, " Z\"");
        put_paint(b, style, "#ffffff");
        owf_buf_puts(b, " />\n");
    } else {
        owf_buf_puts(b, "<rect");
        put_attr_num(b, "x", box[0]);
        put_attr_num(b, "y", box[1]);
        put_attr_num(b, "width", box[2]);
        put_attr_num(b, "height", box[3]);
        if (owf_dstyle_get(style, "rounded", buf, sizeof buf) && !strcmp(buf, "1")) {
            owf_dnum arc = owf_dnum_parse(style_or(style, "arcSize", buf, sizeof buf, "15"));
            owf_dnum small = box[2] < box[3] ? box[2] : box[3];
            owf_dnum r = (owf_dnum)((wide)small * arc / 10000 / 2);
            put_attr_num(b, "rx", r);
            put_attr_num(b, "ry", r);
        }
        put_paint(b, style, "#ffffff");
        owf_buf_puts(b, " />\n");
        if (owf_dstyle_is(style, "swimlane")) {
            header = owf_dnum_parse(style_or(style, "startSize", buf, sizeof buf, "23"));
            if (header > box[3])
                header = box[3];
            owf_buf_puts(b, "<path d=\"M ");
            put_num(b, box[0]); owf_buf_putc(b, ' '); put_num(b, box[1] + header);
            owf_buf_puts(b, " L "); put_num(b, box[0] + box[2]); owf_buf_putc(b, ' '); put_num(b, box[1] + header);
            owf_buf_puts(b, "\" fill=\"none\"");
            put_attr_text(b, "stroke", colour(style, "strokeColor", buf, sizeof buf, "#000000"));
            owf_buf_puts(b, " />\n");
        }
    }
    put_box_label(b, c, box, header);
}

/* An arrowhead at tip, pointing from the point before it. */
static void put_arrow(owf_buf *b, owf_dpoint from, owf_dpoint tip, const char *stroke)
{
    wide dx = tip.x - from.x, dy = tip.y - from.y, len = owf_diag_isqrt(dx * dx + dy * dy);
    wide ux, uy, size = 800, half = 300;
    owf_dnum bx, by;
    if (!len)
        return;
    ux = dx * 1000 / len;                /* the direction in thousandths */
    uy = dy * 1000 / len;
    bx = (owf_dnum)(tip.x - ux * size / 1000);
    by = (owf_dnum)(tip.y - uy * size / 1000);
    owf_buf_puts(b, "<path d=\"M ");
    put_num(b, tip.x); owf_buf_putc(b, ' '); put_num(b, tip.y);
    owf_buf_puts(b, " L "); put_num(b, (owf_dnum)(bx - uy * half / 1000));
    owf_buf_putc(b, ' '); put_num(b, (owf_dnum)(by + ux * half / 1000));
    owf_buf_puts(b, " L "); put_num(b, (owf_dnum)(bx + uy * half / 1000));
    owf_buf_putc(b, ' '); put_num(b, (owf_dnum)(by - ux * half / 1000));
    owf_buf_puts(b, " Z\"");
    put_attr_text(b, "fill", stroke);
    put_attr_text(b, "stroke", stroke);
    owf_buf_puts(b, " />\n");
}

static int wants_arrow(const char *style, const char *key, const char *def)
{
    char buf[24];
    const char *v = style_or(style, key, buf, sizeof buf, def);
    return strcmp(v, "none") != 0;
}

static void put_edge(owf_buf *b, const owf_dpage *p, const owf_dcell *e)
{
    char buf[40];
    int n, i;
    owf_dpoint *pts = owf_dedge_path(p, e, &n);
    const char *stroke;
    if (!pts)
        return;
    owf_buf_puts(b, "<path d=\"");
    for (i = 0; i < n; i++) {
        owf_buf_puts(b, i ? " L " : "M ");
        put_num(b, pts[i].x);
        owf_buf_putc(b, ' ');
        put_num(b, pts[i].y);
    }
    owf_buf_putc(b, '"');
    owf_buf_puts(b, " fill=\"none\"");     /* an edge has no fill */
    {
        char wb[24], db[8];
        owf_dnum width = owf_dnum_parse(style_or(e->style, "strokeWidth", wb, sizeof wb, "1"));
        put_attr_text(b, "stroke", colour(e->style, "strokeColor", buf, sizeof buf, "#000000"));
        if (width != 100)
            put_attr_num(b, "stroke-width", width);
        if (owf_dstyle_get(e->style, "dashed", db, sizeof db) && !strcmp(db, "1"))
            owf_buf_puts(b, " stroke-dasharray=\"3 3\"");
    }
    owf_buf_puts(b, " />\n");
    stroke = colour(e->style, "strokeColor", buf, sizeof buf, "#000000");
    if (strcmp(stroke, "none")) {
        if (wants_arrow(e->style, "endArrow", "classic"))
            put_arrow(b, pts[n - 2], pts[n - 1], stroke);
        if (wants_arrow(e->style, "startArrow", "none"))
            put_arrow(b, pts[1], pts[0], stroke);
    }
    if (e->value && *e->value) {
        owf_dpoint at = owf_diag_along(pts, n, 0);
        put_label(b, e->value, e->style, at.x + e->geo.offset.x, at.y + e->geo.offset.y, "middle", "middle", "#ffffff");
    }
    /* labels placed along it: geometry x is how far, -1 to 1; the offset moves them off the line */
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *l = &p->cells[i];
        if (l->kind == OWF_DCELL_VERTEX && l->parent && !strcmp(l->parent, e->id) && owf_dcell_shown(p, l)) {
            owf_dpoint at = owf_diag_along(pts, n, (long)l->geo.x);
            put_label(b, l->value, l->style, at.x + l->geo.offset.x, at.y + l->geo.offset.y, "middle", "middle", "#ffffff");
        }
    }
    owf_free(pts);
}

int owf_dsvg_write(const owf_diagram *d, int page, unsigned char **data, size_t *length)
{
    const owf_dpage *p;
    owf_dnum box[4] = { 0, 0, 0, 0 };
    unsigned char *mx = NULL;
    size_t mxn = 0;
    owf_buf b;
    int i, r;

    if (page < 0 || page >= d->npages)
        return OWF_ERR_FORMAT;
    p = &d->pages[page];
    r = owf_drawio_write(d, 0, &mx, &mxn);
    if (r != OWF_OK)
        return r;
    owf_dpage_bounds(p, box);
    box[0] -= 100;                       /* a pixel round it, for the lines' width */
    box[1] -= 100;
    box[2] += 200;
    box[3] += 200;
    owf_buf_init(&b);
    owf_buf_puts(&b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"" SVG_NS "\" version=\"1.1\"");
    owf_buf_puts(&b, " width=\"");
    put_num(&b, box[2]);
    owf_buf_puts(&b, "px\" height=\"");
    put_num(&b, box[3]);
    owf_buf_puts(&b, "px\" viewBox=\"");
    put_num(&b, box[0]); owf_buf_putc(&b, ' ');
    put_num(&b, box[1]); owf_buf_putc(&b, ' ');
    put_num(&b, box[2]); owf_buf_putc(&b, ' ');
    put_num(&b, box[3]);
    owf_buf_puts(&b, "\" content=\"");
    while (mxn && (mx[mxn - 1] == '\n'))
        mx[--mxn] = 0;
    owf_diag_put_value(&b, (const char *)mx);
    owf_free(mx);
    owf_buf_puts(&b, "\">\n<g>\n");
    /* in file order, which is drawing order; edges and shapes alike */
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        const owf_dcell *parent = owf_dpage_find(p, c->parent);
        if (!owf_dcell_shown(p, c))
            continue;
        if (c->kind == OWF_DCELL_EDGE)
            put_edge(&b, p, c);
        else if (c->kind == OWF_DCELL_VERTEX && !(parent && parent->kind == OWF_DCELL_EDGE))
            put_vertex(&b, p, c);
    }
    owf_buf_puts(&b, "</g>\n</svg>\n");
    return owf_buf_take(&b, data, length);
}

int owf_dsvg_read(const unsigned char *data, size_t length, owf_diagram *d)
{
    int err, r;
    owf_xml *x = owf_xml_parse((const char *)data, length, &err);
    const owf_xml_node *root;
    const char *content;
    if (!x)
        return err == OWF_ERR_MEMORY ? err : OWF_ERR_FORMAT;
    root = owf_xml_root(x);
    content = owf_xml_is(root, SVG_NS, "svg") ? owf_xml_attr(root, "", "content") : NULL;
    if (!content || content[0] != '<')
        r = OWF_ERR_FORMAT;
    else
        r = owf_drawio_read((const unsigned char *)content, strlen(content), d);
    owf_xml_free(x);
    return r;
}

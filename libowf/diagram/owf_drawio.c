/*
 * owf_drawio.c: the .drawio file (owf_diagram.h): an mxfile of diagram pages, each an mxGraphModel in
 * plain XML or compressed. Integer arithmetic only.
 * Written from published descriptions of the format, not from any program's code:
 *  - mxGraph's API documentation (mxCodec, mxGraphModel, mxCell, mxGeometry, mxPoint; Apache 2.0) for
 *    the element and attribute names and that defaults (0, empty) are left out;
 *  - diagrams.net's documentation on extracting a diagram's XML: a compressed page is base64 (RFC 4648)
 *    of raw DEFLATE (RFC 1951) of the XML after encodeURIComponent (ECMA-262; RFC 3986's unreserved set
 *    with ! ' ( ) *).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"
#include "owf_inflate.h"
#ifdef OWF_HAVE_ZLIB
#include "owf_zlib.h"
#endif

#include <stdlib.h>
#include <string.h>

#define MAX_CELLS 1000000                /* in one page: more is a damaged file */
#define MAX_PAGE_XML (64UL * 1024 * 1024) /* a compressed page's XML, at most */

/* ---- base64, URI encoding ---- */

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_value(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

/* Decodes base64 text (spaces and line breaks skipped) into owf_malloc'd bytes. */
static int b64_decode(const char *s, size_t n, unsigned char **out, size_t *out_n)
{
    unsigned char *o = owf_malloc(n / 4 * 3 + 3);
    unsigned long acc = 0;
    size_t i, k = 0;
    int bits = 0, pad = 0;
    if (!o)
        return OWF_ERR_MEMORY;
    for (i = 0; i < n; i++) {
        int c = (unsigned char)s[i], v;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (c == '=') {
            pad = 1;
            continue;
        }
        v = b64_value(c);
        if (v < 0 || pad) {
            owf_free(o);
            return OWF_ERR_CORRUPT;
        }
        acc = (acc << 6) | (unsigned long)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o[k++] = (unsigned char)(acc >> bits);
        }
    }
    *out = o;
    *out_n = k;
    return OWF_OK;
}

static void b64_encode(owf_buf *b, const unsigned char *in, size_t n)
{
    size_t i;
    for (i = 0; i + 2 < n; i += 3) {
        unsigned long v = ((unsigned long)in[i] << 16) | ((unsigned long)in[i + 1] << 8) | in[i + 2];
        owf_buf_putc(b, b64[v >> 18]);
        owf_buf_putc(b, b64[(v >> 12) & 63]);
        owf_buf_putc(b, b64[(v >> 6) & 63]);
        owf_buf_putc(b, b64[v & 63]);
    }
    if (n - i == 1) {
        unsigned long v = (unsigned long)in[i] << 16;
        owf_buf_putc(b, b64[v >> 18]);
        owf_buf_putc(b, b64[(v >> 12) & 63]);
        owf_buf_puts(b, "==");
    } else if (n - i == 2) {
        unsigned long v = ((unsigned long)in[i] << 16) | ((unsigned long)in[i + 1] << 8);
        owf_buf_putc(b, b64[v >> 18]);
        owf_buf_putc(b, b64[(v >> 12) & 63]);
        owf_buf_putc(b, b64[(v >> 6) & 63]);
        owf_buf_putc(b, '=');
    }
}

static int hex_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* decodeURIComponent, in place; the new length. */
size_t owf_diag_uri_decode(char *s, size_t n)
{
    size_t i, k = 0;
    for (i = 0; i < n; i++) {
        int hi, lo;
        if (s[i] == '%' && i + 2 < n && (hi = hex_value((unsigned char)s[i + 1])) >= 0 &&
            (lo = hex_value((unsigned char)s[i + 2])) >= 0) {
            s[k++] = (char)(hi * 16 + lo);
            i += 2;
        } else
            s[k++] = s[i];
    }
    return k;
}

void owf_diag_uri_encode(owf_buf *b, const unsigned char *s, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0; i < n; i++) {
        int c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            (c && strchr("-_.!~*'()", c)))
            owf_buf_putc(b, c);
        else {
            owf_buf_putc(b, '%');
            owf_buf_putc(b, hex[c >> 4]);
            owf_buf_putc(b, hex[c & 15]);
        }
    }
}

/* ---- Reading ---- */

static int set_attrs(owf_dattrs *a, const owf_xml_node *n, const char *const *skip)
{
    int i, k;
    for (i = 0; i < n->nattrs; i++) {
        const owf_xml_attribute *at = &n->attrs[i];
        int skipped = 0;
        if (at->ns[0])
            continue;
        for (k = 0; skip && skip[k]; k++)
            if (!strcmp(at->name, skip[k]))
                skipped = 1;
        if (!skipped && owf_dattrs_set(a, at->name, at->value) != OWF_OK)
            return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

static int read_point(const owf_xml_node *n, owf_dpoint *pt)
{
    pt->x = owf_dnum_parse(owf_xml_attr(n, "", "x"));
    pt->y = owf_dnum_parse(owf_xml_attr(n, "", "y"));
    return OWF_OK;
}

static int read_geometry(owf_dgeometry *g, const owf_xml_node *n)
{
    static const char *const known[] = { "x", "y", "width", "height", "relative", "as", NULL };
    const owf_xml_node *c;
    const char *rel = owf_xml_attr(n, "", "relative");
    g->flags |= OWF_DGEO_HAS;
    g->x = owf_dnum_parse(owf_xml_attr(n, "", "x"));
    g->y = owf_dnum_parse(owf_xml_attr(n, "", "y"));
    g->width = owf_dnum_parse(owf_xml_attr(n, "", "width"));
    g->height = owf_dnum_parse(owf_xml_attr(n, "", "height"));
    if (rel && !strcmp(rel, "1"))
        g->flags |= OWF_DGEO_RELATIVE;
    if (set_attrs(&g->extra, n, known) != OWF_OK)
        return OWF_ERR_MEMORY;
    for (c = n->first; c; c = c->next) {
        const char *as = owf_xml_attr(c, "", "as");
        if (c->type != OWF_XML_ELEMENT || !as)
            continue;
        if (owf_xml_is(c, "", "mxPoint")) {
            if (!strcmp(as, "sourcePoint")) {
                read_point(c, &g->source_point);
                g->flags |= OWF_DGEO_SOURCE_POINT;
            } else if (!strcmp(as, "targetPoint")) {
                read_point(c, &g->target_point);
                g->flags |= OWF_DGEO_TARGET_POINT;
            } else if (!strcmp(as, "offset")) {
                read_point(c, &g->offset);
                g->flags |= OWF_DGEO_OFFSET;
            }
        } else if (owf_xml_is(c, "", "Array") && !strcmp(as, "points")) {
            const owf_xml_node *pn;
            g->flags |= OWF_DGEO_POINTS;
            for (pn = owf_xml_child(c, "", "mxPoint"); pn; pn = owf_xml_next(pn, "", "mxPoint")) {
                owf_dpoint pt;
                read_point(pn, &pt);
                if (owf_dgeo_add_point(g, pt.x, pt.y) != OWF_OK)
                    return OWF_ERR_MEMORY;
            }
        } else if (owf_xml_is(c, "", "mxRectangle") && !strcmp(as, "alternateBounds")) {
            g->flags |= OWF_DGEO_ALTERNATE;
            g->alternate[0] = owf_dnum_parse(owf_xml_attr(c, "", "x"));
            g->alternate[1] = owf_dnum_parse(owf_xml_attr(c, "", "y"));
            g->alternate[2] = owf_dnum_parse(owf_xml_attr(c, "", "width"));
            g->alternate[3] = owf_dnum_parse(owf_xml_attr(c, "", "height"));
        }
    }
    return OWF_OK;
}

/* One mxCell, or a UserObject or object holding one. */
static int read_cell(owf_dpage *p, const owf_xml_node *n)
{
    static const char *const known[] = { "id", "parent", "value", "style", "vertex", "edge", "source",
                                         "target", NULL };
    static const char *const wrap_known[] = { "id", "label", NULL };
    const owf_xml_node *wrap = NULL, *cell = n, *g;
    const char *id, *v;
    char fresh[24];
    owf_dcell *c;
    int kind = OWF_DCELL_PLAIN;

    if (!owf_xml_is(n, "", "mxCell")) {
        wrap = n;
        cell = owf_xml_child(n, "", "mxCell");
    }
    if (p->ncells >= MAX_CELLS)
        return OWF_ERR_CORRUPT;
    id = owf_xml_attr(wrap ? wrap : cell, "", "id");
    if (!id || !*id || owf_dpage_index(p, id) >= 0) {
        owf_dpage_new_id(p, fresh, sizeof fresh);
        id = fresh;
    }
    if ((v = owf_xml_attr(cell, "", "vertex")) && !strcmp(v, "1"))
        kind = OWF_DCELL_VERTEX;
    else if ((v = owf_xml_attr(cell, "", "edge")) && !strcmp(v, "1"))
        kind = OWF_DCELL_EDGE;
    c = owf_dpage_add(p, id, owf_xml_attr(cell, "", "parent"), kind);
    if (!c)
        return OWF_ERR_MEMORY;
    if (wrap) {
        c->wrapper = owf_xml_is(wrap, "", "UserObject") ? OWF_DWRAP_USEROBJECT : OWF_DWRAP_OBJECT;
        if (owf_dcell_set(&c->value, owf_xml_attr(wrap, "", "label")) != OWF_OK ||
            set_attrs(&c->data, wrap, wrap_known) != OWF_OK)
            return OWF_ERR_MEMORY;
    } else if (owf_dcell_set(&c->value, owf_xml_attr(cell, "", "value")) != OWF_OK)
        return OWF_ERR_MEMORY;
    if (!cell)
        return OWF_OK;
    if (owf_dcell_set(&c->style, owf_xml_attr(cell, "", "style")) != OWF_OK ||
        owf_dcell_set(&c->source, owf_xml_attr(cell, "", "source")) != OWF_OK ||
        owf_dcell_set(&c->target, owf_xml_attr(cell, "", "target")) != OWF_OK ||
        set_attrs(&c->extra, cell, known) != OWF_OK)
        return OWF_ERR_MEMORY;
    /* a vertex="0" or edge="0" is not a default: keep it */
    if ((v = owf_xml_attr(cell, "", "vertex")) && strcmp(v, "1") && owf_dattrs_set(&c->extra, "vertex", v))
        return OWF_ERR_MEMORY;
    if ((v = owf_xml_attr(cell, "", "edge")) && strcmp(v, "1") && owf_dattrs_set(&c->extra, "edge", v))
        return OWF_ERR_MEMORY;
    for (g = owf_xml_child(cell, "", "mxGeometry"); g; g = owf_xml_next(g, "", "mxGeometry")) {
        const char *as = owf_xml_attr(g, "", "as");
        if (!as || !strcmp(as, "geometry"))
            return read_geometry(&c->geo, g);
    }
    return OWF_OK;
}

static int read_model(owf_dpage *p, const owf_xml_node *model)
{
    const owf_xml_node *root = owf_xml_child(model, "", "root"), *n;
    int r;
    if (set_attrs(&p->model, model, NULL) != OWF_OK)
        return OWF_ERR_MEMORY;
    if (!root)
        return OWF_OK;
    for (n = root->first; n; n = n->next) {
        if (n->type != OWF_XML_ELEMENT)
            continue;
        if (owf_xml_is(n, "", "mxCell") || owf_xml_is(n, "", "UserObject") || owf_xml_is(n, "", "object"))
            if ((r = read_cell(p, n)) != OWF_OK)
                return r;
    }
    return OWF_OK;
}

/* Raw DEFLATE of unknown size: tried with more room until it fits or is shown damaged. */
int owf_diag_inflate_any(const unsigned char *in, size_t n, unsigned char **out, size_t *out_n)
{
    size_t limit = n > (MAX_PAGE_XML - 1024) / 1032 ? MAX_PAGE_XML : n * 1032 + 1024;
    size_t room = n * 4 + 1024;
    for (;;) {
        unsigned char *o;
        size_t made = 0;
        int r;
        if (room > limit)
            room = limit;
        o = owf_malloc(room + 1);
        if (!o)
            return OWF_ERR_MEMORY;
        r = owf_inflate_upto(o, room, in, n, &made);
        if (r == OWF_OK) {
            o[made] = 0;
            *out = o;
            *out_n = made;
            return OWF_OK;
        }
        owf_free(o);
        if (r != OWF_ERR_CORRUPT || room >= limit)
            return r;
        room *= 4;
    }
}

/* A compressed page's text into its mxGraphModel. */
static int read_compressed(owf_dpage *p, const owf_xml_node *diagram)
{
    owf_buf text;
    const owf_xml_node *t;
    unsigned char *raw = NULL, *xmltext = NULL;
    size_t raw_n = 0, xml_n = 0;
    owf_xml *x;
    int r, err;

    owf_buf_init(&text);
    for (t = diagram->first; t; t = t->next)
        if (t->type == OWF_XML_TEXT)
            owf_buf_put(&text, t->name, t->length);
    if (text.failed) {
        owf_buf_free(&text);
        return OWF_ERR_MEMORY;
    }
    r = b64_decode(text.data ? (const char *)text.data : "", text.length, &raw, &raw_n);
    owf_buf_free(&text);
    if (r != OWF_OK)
        return r;
    if (!raw_n) {
        owf_free(raw);
        return OWF_OK;                   /* an empty page */
    }
    r = owf_diag_inflate_any(raw, raw_n, &xmltext, &xml_n);
    owf_free(raw);
    if (r != OWF_OK)
        return r;
    xml_n = owf_diag_uri_decode((char *)xmltext, xml_n);
    x = owf_xml_parse((const char *)xmltext, xml_n, &err);
    owf_free(xmltext);
    if (!x)
        return err;
    p->compressed = 1;
    if (!owf_xml_is(owf_xml_root(x), "", "mxGraphModel"))
        r = OWF_ERR_CORRUPT;
    else
        r = read_model(p, owf_xml_root(x));
    owf_xml_free(x);
    return r;
}

int owf_drawio_read(const unsigned char *data, size_t length, owf_diagram *d)
{
    int err, r = OWF_OK;
    owf_xml *x = owf_xml_parse((const char *)data, length, &err);
    const owf_xml_node *root, *n;

    if (!x)
        return err == OWF_ERR_MEMORY ? err : OWF_ERR_FORMAT;
    root = owf_xml_root(x);
    if (owf_xml_is(root, "", "mxGraphModel")) {
        owf_dpage *p = owf_diagram_bare_page(d);
        r = p ? read_model(p, root) : OWF_ERR_MEMORY;
    } else if (owf_xml_is(root, "", "mxfile")) {
        r = set_attrs(&d->file, root, NULL);
        for (n = owf_xml_child(root, "", "diagram"); n && r == OWF_OK; n = owf_xml_next(n, "", "diagram")) {
            owf_dpage *p = owf_diagram_bare_page(d);
            const owf_xml_node *model = owf_xml_child(n, "", "mxGraphModel");
            if (!p) {
                r = OWF_ERR_MEMORY;
                break;
            }
            if (owf_dcell_set(&p->id, owf_xml_attr(n, "", "id")) != OWF_OK ||
                owf_dcell_set(&p->name, owf_xml_attr(n, "", "name")) != OWF_OK)
                r = OWF_ERR_MEMORY;
            else if (model)
                r = read_model(p, model);
            else
                r = read_compressed(p, n);
        }
    } else
        r = OWF_ERR_FORMAT;
    owf_xml_free(x);
    return r;
}

/* ---- Writing ---- */

/* An attribute value: XML's escapes, and line breaks and tabs as references so they stay. */
void owf_diag_put_value(owf_buf *b, const char *s)
{
    const char *start = s;
    for (; *s; s++) {
        const char *with = NULL;
        switch (*s) {
        case '&': with = "&amp;"; break;
        case '<': with = "&lt;"; break;
        case '>': with = "&gt;"; break;
        case '"': with = "&quot;"; break;
        case '\n': with = "&#xa;"; break;
        case '\r': with = "&#xd;"; break;
        case '\t': with = "&#x9;"; break;
        default: break;
        }
        if (with) {
            owf_buf_put(b, start, (size_t)(s - start));
            owf_buf_puts(b, with);
            start = s + 1;
        }
    }
    owf_buf_put(b, start, (size_t)(s - start));
}

static void put_attr(owf_buf *b, const char *name, const char *value)
{
    if (!value)
        return;
    owf_buf_putc(b, ' ');
    owf_buf_puts(b, name);
    owf_buf_puts(b, "=\"");
    owf_diag_put_value(b, value);
    owf_buf_putc(b, '"');
}

static void put_attrs(owf_buf *b, const owf_dattrs *a)
{
    int i;
    for (i = 0; i < a->n; i++)
        put_attr(b, a->v[i].name, a->v[i].value);
}

/* A number, left out when it is 0 (mxCodec's default). */
static void put_num(owf_buf *b, const char *name, owf_dnum n)
{
    char num[24];
    if (!n)
        return;
    owf_dnum_format(n, num, sizeof num);
    put_attr(b, name, num);
}

/* Spaces for a level when pretty, nothing when not. */
static void indent(owf_buf *b, int pretty, int level)
{
    if (!pretty)
        return;
    owf_buf_putc(b, '\n');
    while (level-- > 0)
        owf_buf_puts(b, "  ");
}

static void put_point(owf_buf *b, int pretty, int level, const owf_dpoint *pt, const char *as)
{
    indent(b, pretty, level);
    owf_buf_puts(b, "<mxPoint");
    put_num(b, "x", pt->x);
    put_num(b, "y", pt->y);
    put_attr(b, "as", as);
    owf_buf_puts(b, " />");
}

static void put_geometry(owf_buf *b, int pretty, int level, const owf_dgeometry *g)
{
    int i, inner = g->flags & (OWF_DGEO_SOURCE_POINT | OWF_DGEO_TARGET_POINT | OWF_DGEO_OFFSET |
                               OWF_DGEO_ALTERNATE | OWF_DGEO_POINTS);
    indent(b, pretty, level);
    owf_buf_puts(b, "<mxGeometry");
    put_num(b, "x", g->x);
    put_num(b, "y", g->y);
    put_num(b, "width", g->width);
    put_num(b, "height", g->height);
    if (g->flags & OWF_DGEO_RELATIVE)
        put_attr(b, "relative", "1");
    put_attrs(b, &g->extra);
    put_attr(b, "as", "geometry");
    if (!inner) {
        owf_buf_puts(b, " />");
        return;
    }
    owf_buf_putc(b, '>');
    if (g->flags & OWF_DGEO_ALTERNATE) {
        indent(b, pretty, level + 1);
        owf_buf_puts(b, "<mxRectangle");
        put_num(b, "x", g->alternate[0]);
        put_num(b, "y", g->alternate[1]);
        put_num(b, "width", g->alternate[2]);
        put_num(b, "height", g->alternate[3]);
        owf_buf_puts(b, " as=\"alternateBounds\" />");
    }
    if (g->flags & OWF_DGEO_SOURCE_POINT)
        put_point(b, pretty, level + 1, &g->source_point, "sourcePoint");
    if (g->flags & OWF_DGEO_TARGET_POINT)
        put_point(b, pretty, level + 1, &g->target_point, "targetPoint");
    if (g->flags & OWF_DGEO_POINTS) {
        indent(b, pretty, level + 1);
        if (!g->npoints)
            owf_buf_puts(b, "<Array as=\"points\" />");
        else {
            owf_buf_puts(b, "<Array as=\"points\">");
            for (i = 0; i < g->npoints; i++)
                put_point(b, pretty, level + 2, &g->points[i], NULL);
            indent(b, pretty, level + 1);
            owf_buf_puts(b, "</Array>");
        }
    }
    if (g->flags & OWF_DGEO_OFFSET)
        put_point(b, pretty, level + 1, &g->offset, "offset");
    indent(b, pretty, level);
    owf_buf_puts(b, "</mxGeometry>");
}

static void put_cell(owf_buf *b, int pretty, int level, const owf_dcell *c)
{
    const char *wrap = c->wrapper == OWF_DWRAP_USEROBJECT ? "UserObject" :
                       c->wrapper == OWF_DWRAP_OBJECT ? "object" : NULL;
    if (wrap) {
        indent(b, pretty, level);
        owf_buf_putc(b, '<');
        owf_buf_puts(b, wrap);
        put_attr(b, "label", c->value ? c->value : "");
        put_attrs(b, &c->data);
        put_attr(b, "id", c->id);
        owf_buf_putc(b, '>');
        level++;
    }
    indent(b, pretty, level);
    owf_buf_puts(b, "<mxCell");
    if (!wrap) {
        put_attr(b, "id", c->id);
        put_attr(b, "value", c->value);
    }
    put_attr(b, "style", c->style);
    if (c->kind == OWF_DCELL_VERTEX)
        put_attr(b, "vertex", "1");
    else if (c->kind == OWF_DCELL_EDGE)
        put_attr(b, "edge", "1");
    put_attrs(b, &c->extra);
    put_attr(b, "parent", c->parent);
    put_attr(b, "source", c->source);
    put_attr(b, "target", c->target);
    if (c->geo.flags & OWF_DGEO_HAS) {
        owf_buf_putc(b, '>');
        put_geometry(b, pretty, level + 1, &c->geo);
        indent(b, pretty, level);
        owf_buf_puts(b, "</mxCell>");
    } else
        owf_buf_puts(b, " />");
    if (wrap) {
        indent(b, pretty, level - 1);
        owf_buf_puts(b, "</");
        owf_buf_puts(b, wrap);
        owf_buf_putc(b, '>');
    }
}

static void put_model(owf_buf *b, int pretty, int level, const owf_dpage *p)
{
    int i;
    indent(b, pretty, level);
    owf_buf_puts(b, "<mxGraphModel");
    put_attrs(b, &p->model);
    owf_buf_putc(b, '>');
    indent(b, pretty, level + 1);
    owf_buf_puts(b, "<root>");
    for (i = 0; i < p->ncells; i++)
        put_cell(b, pretty, level + 2, &p->cells[i]);
    indent(b, pretty, level + 1);
    owf_buf_puts(b, "</root>");
    indent(b, pretty, level);
    owf_buf_puts(b, "</mxGraphModel>");
}

/* Raw DEFLATE of n bytes: zlib's when it is there, stored blocks when not. */
static unsigned char *deflate_raw(const unsigned char *in, size_t n, size_t *out_n)
{
    size_t blocks = n / 65535 + 1, at = 0, k = 0, b;
    unsigned char *z;
#ifdef OWF_HAVE_ZLIB
    {
        z_stream zs;
        uLong bound;
        unsigned char *packed = NULL;
        memset(&zs, 0, sizeof zs);
        if (deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK) {
            bound = deflateBound(&zs, (uLong)n);
            if (bound >= (uLong)n && bound - (uLong)n <= (uLong)n / 2 + 4096)
                packed = owf_malloc(bound);
            if (packed) {
                zs.next_in = (Bytef *)in;
                zs.avail_in = (uInt)n;
                zs.next_out = packed;
                zs.avail_out = (uInt)bound;
                if (deflate(&zs, Z_FINISH) == Z_STREAM_END) {
                    *out_n = zs.total_out;
                    deflateEnd(&zs);
                    return packed;
                }
                owf_free(packed);
            }
            deflateEnd(&zs);
        }
    }
#endif
    z = owf_malloc(n + blocks * 5 + 1);
    if (!z)
        return NULL;
    for (b = 0; b < blocks; b++) {
        size_t m = n - at > 65535 ? 65535 : n - at;
        z[k++] = (unsigned char)(b + 1 == blocks);
        z[k++] = (unsigned char)m;
        z[k++] = (unsigned char)(m >> 8);
        z[k++] = (unsigned char)~m;
        z[k++] = (unsigned char)(~m >> 8);
        memcpy(z + k, in + at, m);
        k += m;
        at += m;
    }
    *out_n = k;
    return z;
}

static void put_compressed(owf_buf *b, const owf_dpage *p)
{
    owf_buf xml, uri;
    unsigned char *z;
    size_t zn = 0;
    owf_buf_init(&xml);
    owf_buf_init(&uri);
    put_model(&xml, 0, 0, p);
    if (xml.failed) {
        b->failed = 1;
        owf_buf_free(&xml);
        return;
    }
    owf_diag_uri_encode(&uri, xml.data, xml.length);
    owf_buf_free(&xml);
    if (uri.failed) {
        b->failed = 1;
        owf_buf_free(&uri);
        return;
    }
    z = deflate_raw(uri.data, uri.length, &zn);
    owf_buf_free(&uri);
    if (!z) {
        b->failed = 1;
        return;
    }
    b64_encode(b, z, zn);
    owf_free(z);
}

int owf_drawio_write(const owf_diagram *d, int compressed, unsigned char **data, size_t *length)
{
    owf_buf b;
    int i;
    owf_buf_init(&b);
    owf_buf_puts(&b, "<mxfile");
    if (d->file.n)
        put_attrs(&b, &d->file);
    else
        put_attr(&b, "host", "GFOpenDiag");
    owf_buf_putc(&b, '>');
    for (i = 0; i < d->npages; i++) {
        const owf_dpage *p = &d->pages[i];
        indent(&b, 1, 1);
        owf_buf_puts(&b, "<diagram");
        put_attr(&b, "id", p->id);
        put_attr(&b, "name", p->name);
        owf_buf_putc(&b, '>');
        if (compressed)
            put_compressed(&b, p);
        else {
            put_model(&b, 1, 2, p);
            indent(&b, 1, 1);
        }
        owf_buf_puts(&b, "</diagram>");
    }
    indent(&b, 1, 0);
    owf_buf_puts(&b, "</mxfile>\n");
    return owf_buf_take(&b, data, length);
}

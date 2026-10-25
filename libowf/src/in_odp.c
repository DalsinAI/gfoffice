/*
 * libowf: ODP in (OpenDocument Presentation, OASIS ODF 1.2), zipped (.odp,
 * .otp) or as one flat XML file (.fodp), into a deck (owf_deck.h).
 *
 * content.xml holds office:presentation, a draw:page per slide naming its
 * master page and layout; styles.xml holds the master pages
 * (style:master-page), their presentation styles (the title's, the
 * outline levels', the notes') and the layouts
 * (style:presentation-page-layout). Objects are draw:frame elements (text
 * boxes, pictures, tables, embedded objects) and shapes (draw:rect,
 * draw:ellipse, draw:line, draw:polygon, draw:custom-shape); groups are
 * draw:g. Notes are presentation:notes in each page; transitions are
 * attributes of the page's drawing-page style.
 *
 * Text goes through the ODT reader's paragraph code (in_odt.c), so text
 * boxes read as ODT's body does. Each run keeps only what differs from its
 * level's look (the master's title, outline level, notes or other text), so
 * that a master's look stays the master's.
 *
 * What the model doesn't edit (other shapes, embedded charts and objects,
 * animations, rare transitions) is kept as the file's XML, with the styles,
 * drawing definitions and package members it refers to, and written back.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_deck_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_OFFICE "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
#define NS_STYLE "urn:oasis:names:tc:opendocument:xmlns:style:1.0"
#define NS_TEXT "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
#define NS_TABLE "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
#define NS_DRAW "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
#define NS_FO "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
#define NS_XLINK "http://www.w3.org/1999/xlink"
#define NS_DC "http://purl.org/dc/elements/1.1/"
#define NS_SVG "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
#define NS_PRES "urn:oasis:names:tc:opendocument:xmlns:presentation:1.0"
#define NS_SMIL "urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0"
#define NS_ANIM "urn:oasis:names:tc:opendocument:xmlns:animation:1.0"
#define NS_XML "http://www.w3.org/XML/1998/namespace"
#define NS_MANIFEST "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"
#define NS_LOEXT "urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0"

#define MAX_CHAIN 16
#define TABLE_MAX_COLS 64

typedef struct {
    const char *name, *family, *parent;
    const owf_xml_node *node;
    int where;                       /* OWF_ODF_COMMON, _STYLES_AUTO, _CONTENT_AUTO */
} gstyle;

typedef struct {
    const char *href;
    int index;
} pic;

typedef struct {
    owf_deck *deck;
    owf_report *report;
    const owf_unzip *zip;            /* NULL for flat XML */
    owf_odf_text *text;
    gstyle *styles;
    int nstyles, capstyles;
    const owf_xml_node *defaults[16];   /* style:default-style elements */
    int ndefaults;
    const owf_xml_node *common, *autos_styles, *autos_content, *masters_node, *presentation;
    const owf_xml_node *manifest;
    const char *mnames[OWF_DECK_MAX_MASTERS];   /* each master's style:name */
    const char *lnames[OWF_DECK_MAX_LAYOUTS];   /* each layout's style:name */
    pic *pics;
    int npics, cappics;
    struct { const owf_xml_node *node; owf_kept_kind kind; } *keptnodes;
    int nkeptnodes, capkeptnodes;
    owf_buf scratch;
    int failed;
    int depth;
} odp;

/* ---- Small helpers ---- */

static int is(const owf_xml_node *n, const char *ns, const char *name)
{
    return n && n->type == OWF_XML_ELEMENT && owf_xml_is(n, ns, name);
}

static const char *attr(const owf_xml_node *n, const char *ns, const char *name)
{
    return n ? owf_xml_attr(n, ns, name) : NULL;
}

static int same(const char *a, const char *b)
{
    return a && b && !strcmp(a, b);
}

static int length_attr(const owf_xml_node *n, const char *ns, const char *name, int *twips)
{
    return owf_parse_length(attr(n, ns, name), twips);
}

/* "#rrggbb" as OWF_SHADE; 0 when it is not one. */
static unsigned long shade(const char *v)
{
    unsigned long c;
    return owf_parse_colour(v, &c) ? OWF_SHADE(c) : 0;
}

/* "45%" as 45; def when it is not a percentage. */
static int percent(const char *v, int def)
{
    int n = 0, digits = 0;
    if (!v)
        return def;
    while (*v >= '0' && *v <= '9' && n < 100000) {
        n = n * 10 + (*v++ - '0');
        digits++;
    }
    if (*v == '.')
        for (v++; *v >= '0' && *v <= '9'; v++)
            ;
    return digits && *v == '%' ? n : def;
}

/* ISO 8601 durations as ODF writes them ("PT00H00M05S", "PT5.5S"), in
 * milliseconds; -1 when it is not one. */
static long duration_ms(const char *v)
{
    long total = 0, n = 0, frac = 0, scale = 0;
    int in_time = 0, digits = 0;
    if (!v || *v != 'P')
        return -1;
    for (v++; *v; v++) {
        if (*v >= '0' && *v <= '9') {
            if (scale) {
                if (scale <= 100) {
                    frac += (*v - '0') * (100 / scale);
                    scale *= 10;
                }
            } else if (n < 1000000) {
                n = n * 10 + (*v - '0');
            }
            digits = 1;
        } else if (*v == '.' || *v == ',') {
            scale = 1;
        } else if (*v == 'T') {
            in_time = 1;
        } else {
            long unit = *v == 'H' && in_time ? 3600000L : *v == 'M' && in_time ? 60000L : *v == 'S' ? 1000L
                      : *v == 'D' ? 86400000L : 0;
            if (!unit)
                return -1;
            if (n > 100000)
                n = 100000;
            total += n * unit + (unit == 1000 ? frac * 10 : 0);
            n = frac = scale = 0;
        }
    }
    return digits ? total : -1;
}

/* "id12" as 12, else 0. */
static long id_number(const char *v)
{
    long n = 0;
    if (!v || v[0] != 'i' || v[1] != 'd' || !v[2])
        return 0;
    for (v += 2; *v; v++) {
        if (*v < '0' || *v > '9' || n > 100000000L)
            return 0;
        n = n * 10 + (*v - '0');
    }
    return n;
}

static void text_of(const owf_xml_node *n, owf_buf *out, int depth)
{
    const owf_xml_node *c;
    if (!n || depth > OWF_DECK_MAX_DEPTH)
        return;
    for (c = n->first; c; c = c->next) {
        if (c->type == OWF_XML_TEXT)
            owf_buf_put(out, c->name, c->length);
        else if (is(c, NS_TEXT, "s"))
            owf_buf_putc(out, ' ');
        else if (is(c, NS_TEXT, "line-break"))
            owf_buf_putc(out, '\n');
        else if (is(c, NS_TEXT, "p") && out->length)
            owf_buf_putc(out, '\n'), text_of(c, out, depth + 1);
        else
            text_of(c, out, depth + 1);
    }
}

/* A node's text as a new string (malloc), or NULL when there is none. */
static char *node_text(odp *o, const owf_xml_node *n)
{
    char *s = NULL;
    o->scratch.length = 0;
    text_of(n, &o->scratch, 0);
    if (o->scratch.failed)
        o->failed = 1;
    else if (o->scratch.length)
        s = owf_deck_strdup((const char *)o->scratch.data, o->scratch.length);
    return s;
}

/* ---- Rotations without floating point ---- */

/* sin(degrees) * 16384, 0 to 90 */
static const short sine[91] = {
    0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406, 3686, 3964, 4240,
    4516, 4790, 5063, 5334, 5604, 5872, 6138, 6402, 6664, 6924, 7182, 7438, 7692, 7943, 8192,
    8438, 8682, 8923, 9162, 9397, 9630, 9860, 10087, 10311, 10531, 10749, 10963, 11174, 11381,
    11585, 11786, 11982, 12176, 12365, 12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741,
    13894, 14044, 14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191, 15296, 15396,
    15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083, 16135, 16182, 16225, 16262,
    16294, 16322, 16344, 16362, 16374, 16382, 16384
};

/* sin of tenths of a degree, * 16384 */
long owf_deck_sin(int tenths)
{
    long t = tenths % 3600, s;
    int quadrant, d, r;
    if (t < 0)
        t += 3600;
    quadrant = (int)(t / 900);
    t %= 900;
    if (quadrant & 1)
        t = 900 - t;
    d = (int)(t / 10);
    r = (int)(t % 10);
    s = sine[d] + (d < 90 ? (sine[d + 1] - sine[d]) * r / 10 : 0);
    return quadrant >= 2 ? -s : s;
}

long owf_deck_cos(int tenths)
{
    return owf_deck_sin(tenths + 900);
}

/* Radians ("0.5236", "-1.5708") as tenths of a degree. */
static int radians_tenths(const char *v, const char **end)
{
    long whole = 0, micro = 0, scale = 100000;
    int negative = 0;
    long long t;
    while (*v == ' ')
        v++;
    if (*v == '-') {
        negative = 1;
        v++;
    } else if (*v == '+') {
        v++;
    }
    while (*v >= '0' && *v <= '9') {
        if (whole < 100000)
            whole = whole * 10 + (*v - '0');
        v++;
    }
    if (*v == '.')
        for (v++; *v >= '0' && *v <= '9'; v++) {
            micro += (*v - '0') * scale;
            scale /= 10;
        }
    if (*v == 'e' || *v == 'E') {   /* a tiny number written as 1.2e-17: none */
        for (v++; *v == '-' || *v == '+' || (*v >= '0' && *v <= '9'); v++)
            ;
        whole = micro = 0;
    }
    if (end)
        *end = v;
    t = ((long long)whole * 1000000 + micro) * 1800;
    t = (t + 1570796) / 3141593;
    t %= 3600;
    return (int)(negative ? -t : t);
}

/* ---- Styles ---- */

static void add_styles(odp *o, const owf_xml_node *container, int where)
{
    const owf_xml_node *n;
    for (n = container ? container->first : NULL; n; n = n->next) {
        if (is(n, NS_STYLE, "style")) {
            gstyle *s;
            if (o->nstyles == o->capstyles) {
                int want = o->capstyles ? o->capstyles * 2 : 64;
                gstyle *p = realloc(o->styles, (size_t)want * sizeof *p);
                if (!p) {
                    o->failed = 1;
                    return;
                }
                o->styles = p;
                o->capstyles = want;
            }
            s = &o->styles[o->nstyles++];
            s->name = attr(n, NS_STYLE, "name");
            s->family = attr(n, NS_STYLE, "family");
            s->parent = attr(n, NS_STYLE, "parent-style-name");
            s->node = n;
            s->where = where;
            if (!s->name || !s->family)
                o->nstyles--;
        } else if (is(n, NS_STYLE, "default-style") && o->ndefaults < 16) {
            o->defaults[o->ndefaults++] = n;
        }
    }
}

static const gstyle *find_style(const odp *o, const char *name, const char *family, int where)
{
    int i, pass;
    if (!name)
        return NULL;
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < o->nstyles; i++) {
            const gstyle *s = &o->styles[i];
            if ((pass == 0 && s->where != where) || (pass == 1 && s->where != OWF_ODF_COMMON))
                continue;
            if (same(s->name, name) && (!family || same(s->family, family)))
                return s;
        }
    return NULL;
}

static const owf_xml_node *default_style(const odp *o, const char *family)
{
    int i;
    for (i = 0; i < o->ndefaults; i++)
        if (same(attr(o->defaults[i], NS_STYLE, "family"), family))
            return o->defaults[i];
    return NULL;
}

/* The style and its parents, the style first. */
static int chain(const odp *o, const gstyle *s, const gstyle **out)
{
    int n = 0;
    while (s && n < MAX_CHAIN) {
        int i;
        for (i = 0; i < n; i++)
            if (out[i] == s)
                return n;      /* a loop */
        out[n++] = s;
        s = s->parent ? find_style(o, s->parent, s->family, OWF_ODF_COMMON) : NULL;
    }
    return n;
}

/* A property through the style's parents and its family's default. */
static const char *prop(const odp *o, const gstyle *s, const char *element, const char *ns, const char *name)
{
    const gstyle *c[MAX_CHAIN];
    const owf_xml_node *d;
    const char *v;
    int i, n = chain(o, s, c);
    for (i = 0; i < n; i++)
        if ((v = attr(owf_xml_child(c[i]->node, NS_STYLE, element), ns, name)))
            return v;
    d = s ? default_style(o, s->family) : NULL;
    return d ? attr(owf_xml_child(d, NS_STYLE, element), ns, name) : NULL;
}

/* A text:list-style inside a style's graphic properties (or its parents'). */
static const owf_xml_node *style_list(const odp *o, const gstyle *s)
{
    const gstyle *c[MAX_CHAIN];
    int i, n = chain(o, s, c);
    for (i = 0; i < n; i++) {
        const owf_xml_node *g = owf_xml_child(c[i]->node, NS_STYLE, "graphic-properties");
        const owf_xml_node *l = owf_xml_child(g, NS_TEXT, "list-style");
        if (l)
            return l;
    }
    return NULL;
}

/* Text and paragraph properties onto pf and cf: the family's default and
 * the parents first, or (own) the style's own only. */
static void apply_text(odp *o, const gstyle *s, owf_parafmt *pf, owf_charfmt *cf, int own)
{
    const gstyle *c[MAX_CHAIN];
    int i, n;
    if (!s)
        return;
    n = own ? 1 : chain(o, s, c);
    if (own)
        c[0] = s;
    if (!own) {
        const owf_xml_node *d = default_style(o, s->family);
        if (d) {
            owf_paraprops pp;
            owf_charprops cp;
            memset(&pp, 0, sizeof pp);
            memset(&cp, 0, sizeof cp);
            owf_odf_para_props(owf_xml_child(d, NS_STYLE, "paragraph-properties"), &pp);
            owf_odf_text_props(o->text, owf_xml_child(d, NS_STYLE, "text-properties"), &cp);
            if (pf)
                owf_paraprops_apply(pf, &pp);
            if (cf)
                owf_charprops_apply(cf, &cp);
        }
    }
    for (i = n - 1; i >= 0; i--) {
        owf_paraprops pp;
        owf_charprops cp;
        memset(&pp, 0, sizeof pp);
        memset(&cp, 0, sizeof cp);
        owf_odf_para_props(owf_xml_child(c[i]->node, NS_STYLE, "paragraph-properties"), &pp);
        owf_odf_text_props(o->text, owf_xml_child(c[i]->node, NS_STYLE, "text-properties"), &cp);
        if (pf)
            owf_paraprops_apply(pf, &pp);
        if (cf)
            owf_charprops_apply(cf, &cp);
    }
}

/* ---- Lists ---- */

static unsigned long first_char(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!s || !*s)
        return 0;
    return owf_utf8_next(&p, p + strlen(s));
}

static void copy_affix(char *to, size_t size, const char *from)
{
    size_t n = from ? strlen(from) : 0;
    if (n >= size)
        n = size - 1;
    if (n)
        memcpy(to, from, n);
    to[n] = 0;
}

/* A text:list-style's levels onto lv (9 levels). */
static void read_list(odp *o, const owf_xml_node *list, owf_textlevel *lv)
{
    const owf_xml_node *l;
    for (l = list ? list->first : NULL; l; l = l->next) {
        const char *level = attr(l, NS_TEXT, "level");
        int k = level ? atoi(level) - 1 : -1, v;
        owf_textlevel *t;
        const owf_xml_node *props, *align, *tp;
        if (l->type != OWF_XML_ELEMENT || k < 0 || k >= OWF_TEXT_LEVELS)
            continue;
        t = &lv[k];
        if (is(l, NS_TEXT, "list-level-style-bullet")) {
            t->bullet = first_char(attr(l, NS_TEXT, "bullet-char"));
            if (!t->bullet)
                t->bullet = 0x2022;
            if (t->bullet == ' ')
                t->bullet = 0;            /* a blank bullet: none */
            t->bullet_size = percent(attr(l, NS_TEXT, "bullet-relative-size"), 0);
        } else if (is(l, NS_TEXT, "list-level-style-number")) {
            const char *f = attr(l, NS_STYLE, "num-format");
            const char *start = attr(l, NS_TEXT, "start-value");
            if (f && (!strcmp(f, "a") || !strcmp(f, "A") || !strcmp(f, "i") || !strcmp(f, "I") || !strcmp(f, "1")))
                t->bullet = OWF_BULLET_NUMBER | (unsigned char)f[0];
            else
                t->bullet = f && *f ? OWF_BULLET_NUMBER | '1' : 0;
            copy_affix(t->prefix, sizeof t->prefix, attr(l, NS_STYLE, "num-prefix"));
            copy_affix(t->suffix, sizeof t->suffix, attr(l, NS_STYLE, "num-suffix"));
            t->start = start ? atoi(start) : 0;
            if (t->start < 0 || t->start > 100000)
                t->start = 0;
            t->bullet_size = percent(attr(l, NS_TEXT, "bullet-relative-size"), 0);
        } else if (is(l, NS_TEXT, "list-level-style-image")) {
            t->bullet = 0x2022;
            owf_report_add(o->report, OWF_NOTE_APPROX, "Picture bullets are shown as round bullets");
        } else {
            continue;
        }
        if (t->bullet_size > 400)
            t->bullet_size = 0;
        tp = owf_xml_child(l, NS_STYLE, "text-properties");
        if (tp) {
            owf_charprops cp;
            memset(&cp, 0, sizeof cp);
            owf_odf_text_props(o->text, tp, &cp);
            if (cp.mask & OWF_CP_FONT)
                t->bullet_font = cp.fmt.font;
            if ((cp.mask & OWF_CP_COLOUR) && cp.fmt.colour != OWF_COLOUR_AUTO)
                t->bullet_colour = OWF_SHADE(cp.fmt.colour);
            if (!t->bullet_size && (v = percent(attr(tp, NS_FO, "font-size"), 0)) > 0 && v <= 400)
                t->bullet_size = v;
        }
        props = owf_xml_child(l, NS_STYLE, "list-level-properties");
        align = owf_xml_child(props, NS_STYLE, "list-level-label-alignment");
        if (align) {
            int left = 0, first = 0;
            if (length_attr(align, NS_FO, "margin-left", &left))
                t->para.indent_left = left;
            if (length_attr(align, NS_FO, "text-indent", &first))
                t->para.indent_first = first;
        } else if (props) {
            int before = 0, width = 0;
            int has_before = length_attr(props, NS_TEXT, "space-before", &before);
            int has_width = length_attr(props, NS_TEXT, "min-label-width", &width);
            if (has_before || has_width) {
                t->para.indent_left = before + width;
                t->para.indent_first = -width;
            }
        }
    }
}

/* ---- Kept XML ---- */

static const struct { const char *uri, *prefix; } known_ns[] = {
    { NS_OFFICE, "office" }, { NS_STYLE, "style" }, { NS_TEXT, "text" }, { NS_TABLE, "table" },
    { NS_DRAW, "draw" }, { NS_FO, "fo" }, { NS_XLINK, "xlink" }, { NS_DC, "dc" },
    { "urn:oasis:names:tc:opendocument:xmlns:meta:1.0", "meta" },
    { "urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0", "number" }, { NS_PRES, "presentation" },
    { NS_SVG, "svg" }, { "urn:oasis:names:tc:opendocument:xmlns:chart:1.0", "chart" },
    { "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0", "dr3d" }, { "http://www.w3.org/1998/Math/MathML", "math" },
    { "urn:oasis:names:tc:opendocument:xmlns:form:1.0", "form" },
    { "urn:oasis:names:tc:opendocument:xmlns:script:1.0", "script" }, { NS_SMIL, "smil" }, { NS_ANIM, "anim" },
    { "http://openoffice.org/2004/office", "ooo" }, { "http://openoffice.org/2004/writer", "ooow" },
    { "http://openoffice.org/2004/calc", "oooc" }, { "http://www.w3.org/2001/xml-events", "dom" },
    { "http://www.w3.org/2002/xforms", "xforms" }, { "http://www.w3.org/2001/XMLSchema", "xsd" },
    { "http://www.w3.org/2001/XMLSchema-instance", "xsi" }, { "http://www.w3.org/1999/xhtml", "xhtml" },
    { "http://purl.org/dc/terms/", "dcterms" },
    { "urn:oasis:names:tc:opendocument:xmlns:of:1.2", "of" },
    { "http://www.w3.org/2003/g/data-view#", "grddl" },
    { "http://openoffice.org/2009/office", "officeooo" }, { "http://openoffice.org/2010/drawing", "drawooo" },
    { NS_LOEXT, "loext" }, { "urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0", "calcext" },
    { "urn:org:documentfoundation:names:experimental:office:xmlns:field:1.0", "field" },
    { "http://www.w3.org/TR/css3-text/", "css3t" },
};

typedef struct {
    const char *uris[32];
    int n;
} ns_set;

/* The prefix for a namespace URI; unknown ones get "ns1"... in order. */
static const char *prefix_of(const ns_set *u, const char *uri, char *buf)
{
    size_t i;
    int k;
    if (!uri || !*uri)
        return NULL;
    if (!strcmp(uri, NS_XML))
        return "xml";
    for (i = 0; i < sizeof known_ns / sizeof known_ns[0]; i++)
        if (!strcmp(known_ns[i].uri, uri))
            return known_ns[i].prefix;
    for (k = 0; k < u->n; k++)
        if (!strcmp(u->uris[k], uri)) {
            snprintf(buf, 16, "ns%d", k + 1);
            return buf;
        }
    return "ns0";
}

static void note_ns(ns_set *u, const char *uri)
{
    int k;
    if (!uri || !*uri || !strcmp(uri, NS_XML))
        return;
    for (k = 0; k < u->n; k++)
        if (!strcmp(u->uris[k], uri))
            return;
    if (u->n < 32)
        u->uris[u->n++] = uri;
}

static int collect_ns(const owf_xml_node *n, ns_set *u, int depth)
{
    const owf_xml_node *c;
    int i;
    if (depth > 200)
        return 0;
    note_ns(u, n->ns);
    for (i = 0; i < n->nattrs; i++)
        note_ns(u, n->attrs[i].ns);
    for (c = n->first; c; c = c->next)
        if (c->type == OWF_XML_ELEMENT && !collect_ns(c, u, depth + 1))
            return 0;
    return 1;
}

static void put_escaped(owf_buf *out, const char *s, size_t n, int in_attr)
{
    size_t i, start = 0;
    for (i = 0; i < n; i++) {
        const char *rep = NULL;
        char c = s[i];
        if (c == '&') rep = "&amp;";
        else if (c == '<') rep = "&lt;";
        else if (c == '>') rep = "&gt;";
        else if (in_attr && c == '"') rep = "&quot;";
        else if (in_attr && c == '\t') rep = "&#9;";
        else if (in_attr && c == '\n') rep = "&#10;";
        else if (c == '\r') rep = "&#13;";
        if (rep) {
            owf_buf_put(out, s + start, i - start);
            owf_buf_puts(out, rep);
            start = i + 1;
        }
    }
    owf_buf_put(out, s + start, n - start);
}

static void put_qname(owf_buf *out, const ns_set *u, const char *ns, const char *name)
{
    char buf[16];
    const char *p = prefix_of(u, ns, buf);
    if (p) {
        owf_buf_puts(out, p);
        owf_buf_putc(out, ':');
    }
    owf_buf_puts(out, name);
}

static void put_node(owf_buf *out, const owf_xml_node *n, const ns_set *u, int root)
{
    const owf_xml_node *c;
    int i;
    if (n->type == OWF_XML_TEXT) {
        put_escaped(out, n->name, n->length, 0);
        return;
    }
    owf_buf_putc(out, '<');
    put_qname(out, u, n->ns, n->name);
    if (root) {
        /* every namespace the element and its children use, so the kept
           XML stands on its own wherever it is written */
        for (i = 0; i < u->n; i++) {
            char buf[16];
            owf_buf_printf(out, " xmlns:%s=\"", prefix_of(u, u->uris[i], buf));
            put_escaped(out, u->uris[i], strlen(u->uris[i]), 1);
            owf_buf_putc(out, '"');
        }
    }
    for (i = 0; i < n->nattrs; i++) {
        owf_buf_putc(out, ' ');
        put_qname(out, u, n->attrs[i].ns, n->attrs[i].name);
        owf_buf_puts(out, "=\"");
        put_escaped(out, n->attrs[i].value, strlen(n->attrs[i].value), 1);
        owf_buf_putc(out, '"');
    }
    if (!n->first) {
        owf_buf_puts(out, "/>");
        return;
    }
    owf_buf_putc(out, '>');
    for (c = n->first; c; c = c->next)
        put_node(out, c, u, 0);
    owf_buf_puts(out, "</");
    put_qname(out, u, n->ns, n->name);
    owf_buf_putc(out, '>');
}

/* An element as kept XML: its index in deck->kept, or -1. */
static int keep_xml(odp *o, owf_kept_kind kind, const owf_xml_node *n)
{
    ns_set u;
    int k;
    memset(&u, 0, sizeof u);
    if (!collect_ns(n, &u, 0)) {
        owf_report_add(o->report, OWF_NOTE_LOST, "An object nested too deeply was left out");
        return -1;
    }
    o->scratch.length = 0;
    put_node(&o->scratch, n, &u, 1);
    if (o->scratch.failed) {
        o->failed = 1;
        return -1;
    }
    k = owf_deck_keep(o->deck, kind, (const char *)o->scratch.data, o->scratch.length);
    if (k < 0)
        owf_report_add(o->report, OWF_NOTE_LOST, "Objects beyond what OpenPresentation keeps (8 MB) were left out");
    else
        o->deck->kept_format = OWF_KEPT_ODP;
    return k;
}

/* 1 when this element was kept as this kind already; else it is noted. */
static int already_kept(odp *o, owf_kept_kind kind, const owf_xml_node *n)
{
    int i;
    for (i = 0; i < o->nkeptnodes; i++)
        if (o->keptnodes[i].node == n && o->keptnodes[i].kind == kind)
            return 1;
    if (o->nkeptnodes == o->capkeptnodes) {
        int want = o->capkeptnodes ? o->capkeptnodes * 2 : 32;
        void *p = realloc(o->keptnodes, (size_t)want * sizeof *o->keptnodes);
        if (!p) {
            o->failed = 1;
            return 1;
        }
        o->keptnodes = p;
        o->capkeptnodes = want;
    }
    o->keptnodes[o->nkeptnodes].node = n;
    o->keptnodes[o->nkeptnodes].kind = kind;
    o->nkeptnodes++;
    return 0;
}

static const char *const generated_presentation[] = {
    "title", "subtitle", "notes", "background", "backgroundobjects", "outline1", "outline2", "outline3",
    "outline4", "outline5", "outline6", "outline7", "outline8", "outline9"
};

/* 1 when the writer makes a common style of this name itself. */
static int generated_style(const odp *o, const char *name, const char *family)
{
    int m;
    size_t k;
    if (same(family, "graphic") && (same(name, "standard") || same(name, "objectwithoutfill")))
        return 1;
    if (!same(family, "presentation"))
        return 0;
    for (m = 0; m < o->deck->nmasters; m++) {
        size_t n = o->mnames[m] ? strlen(o->mnames[m]) : 0;
        if (!n || strncmp(name, o->mnames[m], n) || name[n] != '-')
            continue;
        for (k = 0; k < sizeof generated_presentation / sizeof generated_presentation[0]; k++)
            if (!strcmp(name + n + 1, generated_presentation[k]))
                return 1;
    }
    return 0;
}

static void keep_refs(odp *o, const owf_xml_node *n, int where, int depth);

/* The automatic style (or list or number style) of that name, else the
 * common one, kept with what it refers to. */
static void keep_style_named(odp *o, const char *name, int where, int depth)
{
    const owf_xml_node *containers[2], *c;
    int k;
    containers[0] = where == OWF_ODF_STYLES_AUTO ? o->autos_styles : o->autos_content;
    containers[1] = o->common;
    for (k = 0; k < 2; k++)
        for (c = containers[k] ? containers[k]->first : NULL; c; c = c->next) {
            /* a master's objects are in styles.xml, so are the styles they use */
            owf_kept_kind kind = k ? OWF_KEPT_DEFINITION
                               : where == OWF_ODF_STYLES_AUTO ? OWF_KEPT_MASTER_STYLE : OWF_KEPT_STYLE;
            if (c->type != OWF_XML_ELEMENT || !same(attr(c, NS_STYLE, "name"), name))
                continue;
            if (k && generated_style(o, name, attr(c, NS_STYLE, "family")))
                return;
            if (already_kept(o, kind, c))
                return;
            if (keep_xml(o, kind, c) >= 0)
                keep_refs(o, c, k ? OWF_ODF_COMMON : where, depth + 1);
            return;
        }
}

static void keep_definition_named(odp *o, const char *name)
{
    const owf_xml_node *c;
    for (c = o->common ? o->common->first : NULL; c; c = c->next)
        if (c->type == OWF_XML_ELEMENT && c->ns && !strcmp(c->ns, NS_DRAW) && same(attr(c, NS_DRAW, "name"), name)) {
            if (!already_kept(o, OWF_KEPT_DEFINITION, c) && keep_xml(o, OWF_KEPT_DEFINITION, c) >= 0)
                keep_refs(o, c, OWF_ODF_COMMON, 1);
            return;
        }
}

static const char *manifest_type(const odp *o, const char *path)
{
    const owf_xml_node *e;
    for (e = o->manifest ? o->manifest->first : NULL; e; e = e->next)
        if (is(e, NS_MANIFEST, "file-entry") && same(attr(e, NS_MANIFEST, "full-path"), path))
            return attr(e, NS_MANIFEST, "media-type");
    return NULL;
}

/* A package member (or a folder of them: "Object 1") kept for kept XML. */
static void keep_part(odp *o, const char *href)
{
    char path[256], folder[260];
    const owf_xml_node *e;
    size_t n;
    if (!o->zip || !href)
        return;
    if (href[0] == '.' && href[1] == '/')
        href += 2;
    if (!*href || href[0] == '/' || href[0] == '#' || strstr(href, "..") || strchr(href, ':') ||
        strlen(href) >= sizeof path)
        return;
    strcpy(path, href);
    n = strlen(path);
    if (n && path[n - 1] == '/')
        path[--n] = 0;
    if (owf_unzip_has(o->zip, path)) {
        unsigned char *data;
        size_t length;
        if (owf_unzip_read(o->zip, path, &data, &length) == OWF_OK) {
            if (owf_deck_add_part(o->deck, path, manifest_type(o, path), data, length) < 0)
                owf_report_add(o->report, OWF_NOTE_LOST, "Objects beyond what OpenPresentation keeps (8 MB) were left out");
            free(data);
        }
        return;
    }
    /* a folder: its own entry, then each member the manifest lists */
    snprintf(folder, sizeof folder, "%s/", path);
    if (!manifest_type(o, folder))
        return;
    owf_deck_add_part(o->deck, folder, manifest_type(o, folder), NULL, 0);
    for (e = o->manifest ? o->manifest->first : NULL; e; e = e->next) {
        const char *p = attr(e, NS_MANIFEST, "full-path");
        unsigned char *data;
        size_t length;
        if (!is(e, NS_MANIFEST, "file-entry") || !p || strncmp(p, folder, strlen(folder)) || !p[strlen(folder)])
            continue;
        if (p[strlen(p) - 1] == '/') {
            owf_deck_add_part(o->deck, p, attr(e, NS_MANIFEST, "media-type"), NULL, 0);
            continue;
        }
        if (owf_unzip_read(o->zip, p, &data, &length) == OWF_OK) {
            if (owf_deck_add_part(o->deck, p, attr(e, NS_MANIFEST, "media-type"), data, length) < 0)
                owf_report_add(o->report, OWF_NOTE_LOST, "Objects beyond what OpenPresentation keeps (8 MB) were left out");
            free(data);
        }
    }
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !strcmp(s + a - b, tail);
}

/* Styles, drawing definitions and package members a kept element names. */
static void keep_refs(odp *o, const owf_xml_node *n, int where, int depth)
{
    const owf_xml_node *c;
    int i;
    if (depth > 64)
        return;
    for (i = 0; i < n->nattrs; i++) {
        const owf_xml_attribute *a = &n->attrs[i];
        if (!a->ns || !a->value || !*a->value)
            continue;
        if (ends_with(a->name, "style-name") && strcmp(a->name, "master-page-name"))
            keep_style_named(o, a->value, where, depth);
        else if (!strcmp(a->ns, NS_DRAW) &&
                 (!strcmp(a->name, "fill-image-name") || !strcmp(a->name, "fill-gradient-name") ||
                  !strcmp(a->name, "fill-hatch-name") || !strcmp(a->name, "marker-start") ||
                  !strcmp(a->name, "marker-end") || !strcmp(a->name, "stroke-dash") ||
                  !strcmp(a->name, "opacity-name")))
            keep_definition_named(o, a->value);
        else if (!strcmp(a->ns, NS_XLINK) && !strcmp(a->name, "href"))
            keep_part(o, a->value);
    }
    for (c = n->first; c; c = c->next)
        if (c->type == OWF_XML_ELEMENT)
            keep_refs(o, c, where, depth + 1);
}

/* An object kept whole, with what it refers to. */
static int keep_object(odp *o, const owf_xml_node *n, int where)
{
    int k = keep_xml(o, OWF_KEPT_OBJECT, n);
    if (k >= 0)
        keep_refs(o, n, where, 0);
    return k;
}

/* ---- Pictures ---- */

static const char *sniff_mime(const unsigned char *d, size_t n, const char *name)
{
    const char *e = name ? strrchr(name, '.') : NULL;
    if (n >= 8 && !memcmp(d, "\211PNG", 4)) return "image/png";
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return "image/jpeg";
    if (n >= 6 && !memcmp(d, "GIF8", 4)) return "image/gif";
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return "image/bmp";
    if (n >= 12 && !memcmp(d, "FORM", 4) && !memcmp(d + 8, "ILBM", 4)) return "image/x-ilbm";
    if (n >= 4 && d[0] == 0xD7 && d[1] == 0xCD && d[2] == 0xC6 && d[3] == 0x9A) return "image/x-wmf";
    if (n >= 44 && !memcmp(d + 40, " EMF", 4)) return "image/x-emf";
    if (e && (!strcmp(e, ".svg") || !strcmp(e, ".SVG"))) return "image/svg+xml";
    if (n >= 5 && (!memcmp(d, "<svg", 4) || !memcmp(d, "<?xml", 5))) return "image/svg+xml";
    return "application/octet-stream";
}

static int drawable(const char *mime)
{
    return mime && (!strcmp(mime, "image/png") || !strcmp(mime, "image/jpeg"));
}

static int base64_value(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* office:binary-data's base64 into out. */
static void base64_decode(const owf_xml_node *n, owf_buf *out)
{
    const owf_xml_node *c;
    unsigned long acc = 0;
    int bits = 0;
    for (c = n->first; c; c = c->next) {
        size_t i;
        if (c->type != OWF_XML_TEXT)
            continue;
        for (i = 0; i < c->length; i++) {
            int v = base64_value((unsigned char)c->name[i]);
            if (v < 0)
                continue;
            acc = (acc << 6) | (unsigned long)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                owf_buf_putc(out, (int)((acc >> bits) & 0xFF));
            }
        }
    }
}

/* A draw:image's picture into res: its index, or -1. */
static int load_picture(odp *o, const owf_xml_node *img, int w, int h, const char *alt)
{
    const char *href = attr(img, NS_XLINK, "href");
    const owf_xml_node *bin = owf_xml_child(img, NS_OFFICE, "binary-data");
    unsigned char *data = NULL;
    size_t length = 0;
    const char *mime;
    int i, index;

    if (href && o->zip) {
        if (href[0] == '.' && href[1] == '/')
            href += 2;
        for (i = 0; i < o->npics; i++)
            if (!strcmp(o->pics[i].href, href))
                return o->pics[i].index;
        if (owf_unzip_read(o->zip, href, &data, &length) != OWF_OK) {
            owf_report_add(o->report, OWF_NOTE_LOST, "A picture that is not in the file was left out");
            return -1;
        }
    } else if (bin) {
        owf_buf b;
        owf_buf_init(&b);
        base64_decode(bin, &b);
        if (b.failed || !b.length) {
            owf_buf_free(&b);
            return -1;
        }
        data = b.data;
        length = b.length;
    } else {
        if (href)
            owf_report_add(o->report, OWF_NOTE_LOST, "A linked picture was left out");
        return -1;
    }
    mime = o->zip && href ? manifest_type(o, href) : NULL;
    if (!mime || !*mime || !strncmp(mime, "application/", 12))
        mime = sniff_mime(data, length, href);
    index = owf_doc_add_image(o->deck->res, href ? href : "Pictures/picture", mime, data, length, w, h, alt);
    free(data);
    if (index < 0) {
        o->failed = 1;
        return -1;
    }
    if (href && o->zip) {
        if (o->npics == o->cappics) {
            int want = o->cappics ? o->cappics * 2 : 16;
            pic *p = realloc(o->pics, (size_t)want * sizeof *p);
            if (!p) {
                o->failed = 1;
                return index;
            }
            o->pics = p;
            o->cappics = want;
        }
        o->pics[o->npics].href = href;
        o->pics[o->npics].index = index;
        o->npics++;
    }
    return index;
}

/* The best of a frame's draw:image elements: one the PDF draws, if any. */
static const owf_xml_node *best_image(odp *o, const owf_xml_node *frame)
{
    const owf_xml_node *c, *best = NULL;
    int nimages = 0;
    for (c = frame->first; c; c = c->next) {
        const char *href;
        if (!is(c, NS_DRAW, "image"))
            continue;
        nimages++;
        href = attr(c, NS_XLINK, "href");
        if (!best)
            best = c;
        if (href && o->zip) {
            const char *mime = manifest_type(o, href[0] == '.' && href[1] == '/' ? href + 2 : href);
            const char *e = strrchr(href, '.');
            if (drawable(mime) || (e && (!strcmp(e, ".png") || !strcmp(e, ".jpg") || !strcmp(e, ".jpeg")))) {
                best = c;
                break;
            }
        }
    }
    if (nimages > 1)
        owf_report_add(o->report, OWF_NOTE_APPROX, "A picture kept in several versions keeps one of them");
    return best;
}

/* fo:clip's "rect(top, right, bottom, left)" in the picture's own size, as
 * thousandths of it. */
static void read_crop(odp *o, owf_obj *obj, const char *clip)
{
    const owf_image *im;
    int v[4], i, natural_w, natural_h;
    const char *p;
    char part[32];
    if (!clip || strncmp(clip, "rect(", 5) || obj->image < 0)
        return;
    im = &o->deck->res->images[obj->image];
    if (!owf_deck_image_size(im, &natural_w, &natural_h))
        return;
    p = clip + 5;
    for (i = 0; i < 4; i++) {
        size_t n = 0;
        while (*p == ' ' || *p == ',')
            p++;
        while (*p && *p != ',' && *p != ' ' && *p != ')' && n < sizeof part - 1)
            part[n++] = *p++;
        part[n] = 0;
        if (!strcmp(part, "auto"))
            v[i] = 0;
        else if (!owf_parse_length(part, &v[i]))
            return;
    }
    /* top, right, bottom, left into left, top, right, bottom */
    obj->crop[0] = (int)(((long)v[3] * 1000 + natural_w / 2) / natural_w);
    obj->crop[1] = (int)(((long)v[0] * 1000 + natural_h / 2) / natural_h);
    obj->crop[2] = (int)(((long)v[1] * 1000 + natural_w / 2) / natural_w);
    obj->crop[3] = (int)(((long)v[2] * 1000 + natural_h / 2) / natural_h);
    for (i = 0; i < 4; i++)
        if (obj->crop[i] < 0 || obj->crop[i] > 1000)
            obj->crop[i] = 0;
}

/* ---- Objects ---- */

/* The style an object names: presentation:style-name, else draw:style-name. */
static const gstyle *object_style(odp *o, const owf_xml_node *n, int where)
{
    const char *p = attr(n, NS_PRES, "style-name");
    const gstyle *s = p ? find_style(o, p, "presentation", where) : NULL;
    if (!s)
        s = find_style(o, attr(n, NS_DRAW, "style-name"), "graphic", where);
    return s;
}

static owf_placeholder placeholder_kind(const char *c)
{
    if (!c) return OWF_PH_NONE;
    if (!strcmp(c, "title")) return OWF_PH_TITLE;
    if (!strcmp(c, "subtitle")) return OWF_PH_SUBTITLE;
    if (!strcmp(c, "outline")) return OWF_PH_BODY;
    if (!strcmp(c, "text")) return OWF_PH_TEXT;
    if (!strcmp(c, "graphic")) return OWF_PH_PICTURE;
    if (!strcmp(c, "object") || !strcmp(c, "orgchart")) return OWF_PH_OBJECT;
    if (!strcmp(c, "chart")) return OWF_PH_CHART;
    if (!strcmp(c, "table")) return OWF_PH_TABLE;
    if (!strcmp(c, "notes")) return OWF_PH_NOTES;
    if (!strcmp(c, "page")) return OWF_PH_SLIDE_IMAGE;
    if (!strcmp(c, "date-time")) return OWF_PH_DATE;
    if (!strcmp(c, "footer")) return OWF_PH_FOOTER;
    if (!strcmp(c, "page-number")) return OWF_PH_NUMBER;
    if (!strcmp(c, "header")) return OWF_PH_HEADER;
    return OWF_PH_NONE;
}

/* svg:x, y, width, height, or draw:transform's rotate and translate. */
static void read_box(odp *o, owf_obj *obj, const owf_xml_node *n)
{
    const char *t = attr(n, NS_DRAW, "transform");
    int has_w = length_attr(n, NS_SVG, "width", &obj->width);
    int has_h = length_attr(n, NS_SVG, "height", &obj->height);
    length_attr(n, NS_SVG, "x", &obj->x);
    length_attr(n, NS_SVG, "y", &obj->y);
    if (obj->placeholder != OWF_PH_NONE && !has_w && !has_h)
        obj->flags |= OWF_OBJ_INHERIT_BOX;
    if (t) {
        const char *p = t;
        int tx = 0, ty = 0, rot = 0, ok = 1;
        while (*p) {
            while (*p == ' ' || *p == ',')
                p++;
            if (!strncmp(p, "rotate", 6)) {
                p += 6;
                while (*p == ' ' || *p == '(')
                    p++;
                rot += radians_tenths(p, &p);
                while (*p && *p != ')')
                    p++;
                if (*p)
                    p++;
            } else if (!strncmp(p, "translate", 9)) {
                char a[32], b[32];
                size_t k;
                p += 9;
                while (*p == ' ' || *p == '(')
                    p++;
                for (k = 0; *p && *p != ' ' && *p != ',' && *p != ')' && k < sizeof a - 1;)
                    a[k++] = *p++;
                a[k] = 0;
                while (*p == ' ' || *p == ',')
                    p++;
                for (k = 0; *p && *p != ' ' && *p != ')' && k < sizeof b - 1;)
                    b[k++] = *p++;
                b[k] = 0;
                if (!owf_parse_length(a, &tx) || !owf_parse_length(b, &ty))
                    ok = 0;
                while (*p && *p != ')')
                    p++;
                if (*p)
                    p++;
            } else if (*p) {
                ok = 0;                   /* scale, skew, matrix */
                break;
            }
        }
        if (!ok) {
            owf_report_add(o->report, OWF_NOTE_APPROX, "Skewed or scaled objects are drawn in their own box");
        } else {
            /* the unrotated box: about the same centre */
            long c = owf_deck_cos(rot), s = owf_deck_sin(rot);
            long cx = ((long)obj->width * c + (long)obj->height * s) / 2;
            long cy = (-(long)obj->width * s + (long)obj->height * c) / 2;
            obj->x = (int)(tx + (cx + 8192) / 16384 - obj->width / 2);
            obj->y = (int)(ty + (cy + 8192) / 16384 - obj->height / 2);
            obj->rotation = rot;
            if (rot)
                owf_report_add(o->report, OWF_NOTE_APPROX, "Rotated objects are drawn unrotated (their rotation is kept)");
        }
    }
}

static void read_ids(odp *o, owf_obj *obj, const owf_xml_node *n)
{
    const char *id = attr(n, NS_XML, "id");
    const char *name = attr(n, NS_DRAW, "name");
    const owf_xml_node *title = owf_xml_child(n, NS_SVG, "title"), *desc = owf_xml_child(n, NS_SVG, "desc");
    if (!id)
        id = attr(n, NS_DRAW, "id");
    if (id) {
        obj->id = id_number(id);
        if (!obj->id && owf_deck_set_text(&obj->xml_id, id) < 0)
            o->failed = 1;
    }
    if (name && owf_deck_set_text(&obj->name, name) < 0)
        o->failed = 1;
    if (desc || title) {
        obj->alt = node_text(o, desc ? desc : title);
    }
    if (same(attr(n, NS_DRAW, "display"), "none"))
        obj->flags |= OWF_OBJ_HIDDEN;
}

/* Fill, line, insets, alignment and fitting from the object's style. */
static void read_look(odp *o, owf_obj *obj, const gstyle *st, int is_shape)
{
    const char *fill = prop(o, st, "graphic-properties", NS_DRAW, "fill");
    const char *stroke = prop(o, st, "graphic-properties", NS_DRAW, "stroke");
    const char *v;
    int pad;

    if (fill && !strcmp(fill, "solid"))
        obj->look.fill = shade(prop(o, st, "graphic-properties", NS_DRAW, "fill-color"));
    else if (fill && (!strcmp(fill, "gradient") || !strcmp(fill, "hatch") || !strcmp(fill, "bitmap"))) {
        obj->look.fill = shade(prop(o, st, "graphic-properties", NS_DRAW, "fill-color"));
        owf_report_add(o->report, OWF_NOTE_APPROX, "Gradient, hatched and picture fills are drawn in one colour");
    } else if (!fill && is_shape) {
        obj->look.fill = shade(prop(o, st, "graphic-properties", NS_DRAW, "fill-color"));
    }
    if (stroke && strcmp(stroke, "none")) {
        obj->look.line = shade(prop(o, st, "graphic-properties", NS_SVG, "stroke-color"));
        if (!obj->look.line)
            obj->look.line = OWF_SHADE(0);
        obj->look.line_style = !strcmp(stroke, "dash") ? OWF_LINE_DASHED : OWF_LINE_THIN;
    } else if (!stroke && is_shape) {
        obj->look.line = OWF_SHADE(0);
        obj->look.line_style = OWF_LINE_THIN;
    }
    if (obj->look.line)
        owf_parse_length(prop(o, st, "graphic-properties", NS_SVG, "stroke-width"), &obj->look.line_width);
    if ((v = prop(o, st, "graphic-properties", NS_DRAW, "marker-start")) && *v)
        obj->look.arrows |= OWF_ARROW_START;
    if ((v = prop(o, st, "graphic-properties", NS_DRAW, "marker-end")) && *v)
        obj->look.arrows |= OWF_ARROW_END;
    if (owf_parse_length(prop(o, st, "graphic-properties", NS_FO, "padding"), &pad))
        obj->inset[0] = obj->inset[1] = obj->inset[2] = obj->inset[3] = pad;
    owf_parse_length(prop(o, st, "graphic-properties", NS_FO, "padding-left"), &obj->inset[0]);
    owf_parse_length(prop(o, st, "graphic-properties", NS_FO, "padding-top"), &obj->inset[1]);
    owf_parse_length(prop(o, st, "graphic-properties", NS_FO, "padding-right"), &obj->inset[2]);
    owf_parse_length(prop(o, st, "graphic-properties", NS_FO, "padding-bottom"), &obj->inset[3]);
    if ((v = prop(o, st, "graphic-properties", NS_DRAW, "textarea-vertical-align"))) {
        obj->valign = !strcmp(v, "middle") ? OWF_VALIGN_CENTRE : !strcmp(v, "bottom") ? OWF_VALIGN_BOTTOM
                                                                                   : OWF_VALIGN_TOP;
    } else if (is_shape) {
        obj->valign = OWF_VALIGN_CENTRE;
    }
    v = prop(o, st, "graphic-properties", NS_DRAW, "fit-to-size");
    if (same(v, "shrink-to-fit") || same(v, "true") || same(v, "all") ||
        same(prop(o, st, "graphic-properties", NS_STYLE, "shrink-to-fit"), "true"))
        obj->autofit = OWF_AUTOFIT_SHRINK;
    else if (same(prop(o, st, "graphic-properties", NS_DRAW, "auto-grow-height"), "true") && !is_shape &&
             obj->placeholder == OWF_PH_NONE)
        obj->autofit = OWF_AUTOFIT_GROW;
}

/* The look of a list level for this object: its own list's, else its
 * master's levels. */
static const owf_textlevel *level_look(const owf_master *m, const owf_obj *obj, int level)
{
    if (obj->list && level > 0)
        return &obj->list[level - 1];
    switch (obj->placeholder) {
    case OWF_PH_TITLE: return &m->title;
    case OWF_PH_SUBTITLE: return &m->subtitle;
    case OWF_PH_NOTES: return &m->notes;
    case OWF_PH_BODY:
    case OWF_PH_TEXT:
    case OWF_PH_OBJECT:
        return &m->body[level > 0 ? level - 1 : 0];
    default: return &m->other;
    }
}

static const owf_xml_node *o_autos(const odp *o, int styles);

typedef struct {
    odp *o;
    owf_obj *obj;
    owf_story *story;
    const owf_master *m;
    const gstyle *st;        /* the object's style */
    const gstyle *pstyle;    /* its draw:text-style-name paragraph style, or NULL */
    int which;
    owf_builder b;
    int keep_levels;
} textctx;

/* One paragraph, its runs relative to its level's look. */
static void text_para(textctx *t, const owf_xml_node *p, int level)
{
    odp *o = t->o;
    const owf_textlevel *lv = level_look(t->m, t->obj, level);
    owf_parafmt pf = lv->para;
    owf_charfmt cf = lv->chars;
    int before = t->story->nparas, j, k;

    if (t->st) {
        if (same(t->st->family, "graphic"))
            apply_text(o, t->st, &pf, &cf, 0);
        else
            apply_text(o, t->st, &pf, &cf, 1);
    }
    if (t->pstyle)
        apply_text(o, t->pstyle, &pf, &cf, 1);
    if (owf_odf_text_paragraph(o->text, &t->b, p, t->which, &pf, &cf) != OWF_OK)
        o->failed = 1;
    for (k = before; k < t->story->nparas; k++) {
        owf_para *para = &t->story->paras[k];
        for (j = 0; j < para->nruns; j++) {
            owf_charfmt *f = &para->runs[j].fmt;
            int font = f->font >= 0 ? f->font : cf.font;
            int size = f->size > 0 ? f->size : cf.size;
            f->font = font == lv->chars.font ? -1 : font;
            f->size = size == lv->chars.size ? 0 : size;
            if (f->colour == lv->chars.colour)
                f->colour = OWF_COLOUR_AUTO;
        }
        if (level > 0) {
            /* a list paragraph's indents are its list level's */
            para->fmt.indent_left = lv->para.indent_left;
            para->fmt.indent_first = lv->para.indent_first;
        }
        if (t->keep_levels && level > 0 && owf_obj_set_level(t->obj, k, level) < 0)
            o->failed = 1;
    }
}

static void text_blocks(textctx *t, const owf_xml_node *n, int level, int depth)
{
    const owf_xml_node *c;
    if (depth > OWF_DECK_MAX_DEPTH)
        return;
    for (c = n->first; c; c = c->next) {
        if (c->type != OWF_XML_ELEMENT)
            continue;
        if (is(c, NS_TEXT, "p") || is(c, NS_TEXT, "h")) {
            text_para(t, c, level);
        } else if (is(c, NS_TEXT, "list")) {
            const char *name = attr(c, NS_TEXT, "style-name");
            const owf_xml_node *item;
            int next = level < OWF_TEXT_LEVELS ? level + 1 : OWF_TEXT_LEVELS;
            if (name && !t->obj->list && t->keep_levels) {
                const owf_xml_node *ls = NULL, *e;
                const owf_xml_node *where = t->which == OWF_ODF_STYLES_AUTO ? o_autos(t->o, 1) : o_autos(t->o, 0);
                for (e = where ? where->first : NULL; e && !ls; e = e->next)
                    if (is(e, NS_TEXT, "list-style") && same(attr(e, NS_STYLE, "name"), name))
                        ls = e;
                for (e = t->o->common ? t->o->common->first : NULL; e && !ls; e = e->next)
                    if (is(e, NS_TEXT, "list-style") && same(attr(e, NS_STYLE, "name"), name))
                        ls = e;
                if (ls) {
                    int i;
                    t->obj->list = malloc(OWF_TEXT_LEVELS * sizeof *t->obj->list);
                    if (!t->obj->list) {
                        t->o->failed = 1;
                    } else {
                        /* the master's levels, the list's bullets and indents over them */
                        owf_obj plain = *t->obj;
                        plain.list = NULL;
                        for (i = 0; i < OWF_TEXT_LEVELS; i++)
                            t->obj->list[i] = *level_look(t->m, &plain, i + 1);
                        read_list(t->o, ls, t->obj->list);
                    }
                }
            }
            for (item = c->first; item; item = item->next) {
                if (is(item, NS_TEXT, "list-item"))
                    text_blocks(t, item, next, depth + 1);
                else if (is(item, NS_TEXT, "list-header"))
                    text_blocks(t, item, level, depth + 1);
            }
        } else if (is(c, NS_TABLE, "table")) {
            owf_report_add(t->o->report, OWF_NOTE_LOST, "A table inside a text box was left out");
        } else {
            text_blocks(t, c, level, depth + 1);
        }
    }
}

static const owf_xml_node *o_autos(const odp *o, int styles)
{
    return styles ? o->autos_styles : o->autos_content;
}

/* A text box's (or a shape's) paragraphs into the object's text. */
static void read_text(odp *o, owf_obj *obj, const owf_xml_node *box, const owf_xml_node *shape, const gstyle *st,
                      const owf_master *m, int which)
{
    textctx t;
    memset(&t, 0, sizeof t);     /* the builder's formats are compared whole, padding too */
    t.pstyle = find_style(o, attr(shape, NS_DRAW, "text-style-name"), "paragraph", which);
    t.o = o;
    t.obj = obj;
    t.story = &obj->text;
    t.m = m;
    t.st = st;
    t.which = which;
    t.keep_levels = 1;
    owf_builder_init(&t.b, o->deck->res, &obj->text);
    text_blocks(&t, box, 0, 0);
    if (owf_builder_done(&t.b) != OWF_OK)
        o->failed = 1;
}

/* A table's cells, as native table paragraphs, with column widths. */
static void read_table(odp *o, owf_obj *obj, const owf_xml_node *tbl, const owf_master *m, int which)
{
    const owf_xml_node *groups[8], *g, *row, *cell;
    int widths[TABLE_MAX_COLS], ncols = 0, ngroups = 0, nrows = 0, i, merged = 0;
    textctx t;

    memset(&t, 0, sizeof t);

    obj->kind = OWF_OBJ_TABLE;
    /* columns, inside their groups too */
    groups[ngroups++] = tbl;
    for (i = 0; i < ngroups; i++)
        for (g = groups[i]->first; g; g = g->next) {
            if (is(g, NS_TABLE, "table-column")) {
                const char *rep = attr(g, NS_TABLE, "number-columns-repeated");
                int n = rep ? atoi(rep) : 1, w = 0, k;
                const gstyle *cs = find_style(o, attr(g, NS_TABLE, "style-name"), "table-column", which);
                owf_parse_length(prop(o, cs, "table-column-properties", NS_STYLE, "column-width"), &w);
                for (k = 0; k < n && ncols < TABLE_MAX_COLS; k++)
                    widths[ncols++] = w;
            } else if ((is(g, NS_TABLE, "table-columns") || is(g, NS_TABLE, "table-header-columns") ||
                        is(g, NS_TABLE, "table-column-group")) && ngroups < 8) {
                groups[ngroups++] = g;
            }
        }
    t.o = o;
    t.obj = obj;
    t.story = &obj->text;
    t.m = m;
    t.st = NULL;
    t.pstyle = NULL;
    t.which = which;
    t.keep_levels = 0;
    owf_builder_init(&t.b, o->deck->res, &obj->text);
    /* rows, inside their groups too */
    ngroups = 0;
    groups[ngroups++] = tbl;
    for (i = 0; i < ngroups; i++)
        for (row = groups[i]->first; row; row = row->next) {
            int col = 0;
            if ((is(row, NS_TABLE, "table-rows") || is(row, NS_TABLE, "table-header-rows") ||
                 is(row, NS_TABLE, "table-row-group")) && ngroups < 8) {
                groups[ngroups++] = row;
                continue;
            }
            if (!is(row, NS_TABLE, "table-row"))
                continue;
            for (cell = row->first; cell; cell = cell->next) {
                const gstyle *cs;
                const char *span;
                unsigned long fill;
                int before = obj->text.nparas, k;
                if (is(cell, NS_TABLE, "covered-table-cell")) {
                    col++;
                    merged = 1;
                    continue;
                }
                if (!is(cell, NS_TABLE, "table-cell") || col >= TABLE_MAX_COLS)
                    continue;
                if ((span = attr(cell, NS_TABLE, "number-columns-spanned")) && atoi(span) > 1)
                    merged = 1;
                if ((span = attr(cell, NS_TABLE, "number-rows-spanned")) && atoi(span) > 1)
                    merged = 1;
                cs = find_style(o, attr(cell, NS_TABLE, "style-name"), "table-cell", which);
                t.st = cs;
                text_blocks(&t, cell, 0, 0);
                if (t.b.para || t.b.text.length)
                    owf_builder_end_para(&t.b);
                if (obj->text.nparas == before) {   /* an empty cell is still a cell */
                    owf_parafmt pf = m->other.para;
                    t.b.parafmt = pf;
                    owf_builder_end_para(&t.b);
                }
                fill = 0;
                if (same(prop(o, cs, "graphic-properties", NS_DRAW, "fill"), "solid"))
                    fill = shade(prop(o, cs, "graphic-properties", NS_DRAW, "fill-color"));
                if (!fill)
                    fill = shade(prop(o, cs, "table-cell-properties", NS_FO, "background-color"));
                for (k = before; k < obj->text.nparas; k++) {
                    owf_para *p = &obj->text.paras[k];
                    p->table_id = 0;
                    p->table_row = nrows;
                    p->table_col = col;
                    p->cell_shading = fill;
                }
                col++;
            }
            if (col > ncols)
                ncols = col < TABLE_MAX_COLS ? col : TABLE_MAX_COLS;
            nrows++;
        }
    if (owf_builder_done(&t.b) != OWF_OK)
        o->failed = 1;
    for (i = 0; i < obj->text.nparas; i++)
        obj->text.paras[i].table_cols = ncols;
    if (merged)
        owf_report_add(o->report, OWF_NOTE_APPROX, "Merged table cells are shown apart");
    obj->ncols = ncols;
    if (ncols) {
        int any = 0;
        for (i = 0; i < ncols; i++)
            any |= i < TABLE_MAX_COLS && widths[i] > 0;
        if (any) {
            obj->col_widths = calloc((size_t)ncols, sizeof *obj->col_widths);
            if (!obj->col_widths)
                o->failed = 1;
            else
                for (i = 0; i < ncols; i++)
                    obj->col_widths[i] = widths[i] > 0 ? widths[i] : 0;
        }
    }
}

static owf_obj *new_obj(odp *o, owf_obj **objs, int *n, int *cap)
{
    owf_obj *obj;
    int i;
    if (*n >= OWF_DECK_MAX_OBJS) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Objects beyond %d on a slide were left out", OWF_DECK_MAX_OBJS);
        return NULL;
    }
    if (*n == *cap) {
        int want = *cap ? *cap * 2 : 8;
        owf_obj *p = realloc(*objs, (size_t)want * sizeof *p);
        if (!p) {
            o->failed = 1;
            return NULL;
        }
        *objs = p;
        *cap = want;
    }
    obj = &(*objs)[(*n)++];
    owf_obj_init(obj, OWF_OBJ_TEXT);
    (void)i;
    return obj;
}

typedef struct {
    owf_obj **objs;
    int *n, *cap;
    const owf_master *m;
    int which;               /* where the styles are */
    int on_master;
} objctx;

static const char *custom_type(const owf_xml_node *n)
{
    return attr(owf_xml_child(n, NS_DRAW, "enhanced-geometry"), NS_DRAW, "type");
}

static int read_object(odp *o, objctx *c, const owf_xml_node *n, int parent, int depth);

static void count_placeholder(objctx *c, owf_obj *obj)
{
    int i, index = 0;
    for (i = 0; i < *c->n - 1; i++)
        if ((*c->objs)[i].placeholder == obj->placeholder)
            index++;
    obj->ph_index = index;
}

/* The polygon's points (svg:points in svg:viewBox units) inside its box. */
static void read_points(odp *o, owf_obj *obj, const owf_xml_node *n)
{
    const char *pts = attr(n, NS_DRAW, "points"), *vb = attr(n, NS_SVG, "viewBox");
    long box[4] = { 0, 0, 0, 0 };
    int count = 0, cap = 0, *xy = NULL;
    const char *p;
    int k;
    if (!pts)
        return;
    if (vb && sscanf(vb, "%ld %ld %ld %ld", &box[0], &box[1], &box[2], &box[3]) != 4)
        box[2] = box[3] = 0;
    for (p = pts; *p && count < 4096;) {
        long x, y;
        char *e;
        while (*p == ' ' || *p == ',')
            p++;
        if (!*p)
            break;
        x = strtol(p, &e, 10);
        if (e == p || *e != ',')
            break;
        p = e + 1;
        y = strtol(p, &e, 10);
        if (e == p)
            break;
        p = e;
        if (count == cap) {
            int want = cap ? cap * 2 : 16;
            int *q = realloc(xy, (size_t)want * 2 * sizeof *q);
            if (!q) {
                free(xy);
                o->failed = 1;
                return;
            }
            xy = q;
            cap = want;
        }
        if (box[2] > 0 && box[3] > 0) {
            x = (x - box[0]) * obj->width / box[2];
            y = (y - box[1]) * obj->height / box[3];
        }
        xy[count * 2] = (int)x;
        xy[count * 2 + 1] = (int)y;
        count++;
    }
    for (k = 0; k < count * 2; k++)
        if (xy[k] < -100000 || xy[k] > 100000)
            xy[k] = 0;
    obj->points = xy;
    obj->npoints = count;
}

static int read_object(odp *o, objctx *c, const owf_xml_node *n, int parent, int depth)
{
    owf_obj *obj;
    const gstyle *st;
    const char *what = NULL;
    int index;

    if (n->type != OWF_XML_ELEMENT || !n->ns || depth > OWF_DECK_MAX_DEPTH)
        return 0;
    if (strcmp(n->ns, NS_DRAW) && strcmp(n->ns, "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0"))
        return 0;
    if (is(n, NS_DRAW, "layer-set"))
        return 0;
    obj = new_obj(o, c->objs, c->n, c->cap);
    if (!obj)
        return 0;
    index = *c->n - 1;
    obj->parent = parent;
    obj->placeholder = placeholder_kind(attr(n, NS_PRES, "class"));
    if (obj->placeholder != OWF_PH_NONE)
        count_placeholder(c, obj);
    if (same(attr(n, NS_PRES, "placeholder"), "true"))
        obj->flags |= OWF_OBJ_EMPTY;
    st = object_style(o, n, c->which);
    read_ids(o, obj, n);
    read_box(o, obj, n);

    if (is(n, NS_DRAW, "frame")) {
        const owf_xml_node *box = owf_xml_child(n, NS_DRAW, "text-box");
        const owf_xml_node *img = best_image(o, n);
        const owf_xml_node *tbl = owf_xml_child(n, NS_TABLE, "table");
        const owf_xml_node *ob = owf_xml_child(n, NS_DRAW, "object");
        if (!ob)
            ob = owf_xml_child(n, NS_DRAW, "object-ole");
        if (!ob)
            ob = owf_xml_child(n, NS_DRAW, "plugin");
        if (!ob)
            ob = owf_xml_child(n, NS_DRAW, "applet");
        if (!ob)
            ob = owf_xml_child(n, NS_DRAW, "floating-frame");
        read_look(o, obj, st, 0);
        if (ob) {
            obj->kind = OWF_OBJ_OTHER;
            what = is(ob, NS_DRAW, "plugin") ? "Sound and video" : "Embedded charts and objects";
            if (img)       /* its replacement picture, to show */
                obj->image = load_picture(o, img, obj->width, obj->height, obj->alt);
        } else if (tbl) {
            read_table(o, obj, tbl, c->m, c->which);
        } else if (img) {
            obj->kind = OWF_OBJ_PICTURE;
            obj->image = load_picture(o, img, obj->width, obj->height, obj->alt);
            read_crop(o, obj, prop(o, st, "graphic-properties", NS_FO, "clip"));
        } else if (box) {
            obj->kind = OWF_OBJ_TEXT;
            read_text(o, obj, box, n, st, c->m, c->which);
        } else {
            obj->kind = OWF_OBJ_TEXT;
        }
    } else if (is(n, NS_DRAW, "rect") || is(n, NS_DRAW, "ellipse") || is(n, NS_DRAW, "circle") ||
               is(n, NS_DRAW, "line") || is(n, NS_DRAW, "polygon") || is(n, NS_DRAW, "custom-shape")) {
        const char *kind = attr(n, NS_DRAW, "kind");
        obj->kind = OWF_OBJ_SHAPE;
        if (is(n, NS_DRAW, "rect")) {
            obj->shape = length_attr(n, NS_DRAW, "corner-radius", &obj->radius) && obj->radius > 0
                         ? OWF_SHAPE_ROUNDRECT : OWF_SHAPE_RECT;
        } else if (is(n, NS_DRAW, "ellipse") || is(n, NS_DRAW, "circle")) {
            obj->shape = OWF_SHAPE_ELLIPSE;
            if (kind && strcmp(kind, "full")) {
                obj->kind = OWF_OBJ_OTHER;
                what = "Arcs and pie segments";
            }
        } else if (is(n, NS_DRAW, "line")) {
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            obj->shape = OWF_SHAPE_LINE;
            length_attr(n, NS_SVG, "x1", &x1);
            length_attr(n, NS_SVG, "y1", &y1);
            length_attr(n, NS_SVG, "x2", &x2);
            length_attr(n, NS_SVG, "y2", &y2);
            obj->x = x1 < x2 ? x1 : x2;
            obj->y = y1 < y2 ? y1 : y2;
            obj->width = x1 < x2 ? x2 - x1 : x1 - x2;
            obj->height = y1 < y2 ? y2 - y1 : y1 - y2;
            if (x2 < x1)
                obj->flags |= OWF_OBJ_FLIP_H;
            if (y2 < y1)
                obj->flags |= OWF_OBJ_FLIP_V;
        } else if (is(n, NS_DRAW, "polygon")) {
            obj->shape = OWF_SHAPE_POLYGON;
            read_points(o, obj, n);
        } else {
            const char *type = custom_type(n);
            const owf_xml_node *geo = owf_xml_child(n, NS_DRAW, "enhanced-geometry");
            if (same(type, "rectangle") || same(type, "ooxml-rect")) obj->shape = OWF_SHAPE_RECT;
            else if (same(type, "round-rectangle") || same(type, "ooxml-roundRect")) obj->shape = OWF_SHAPE_ROUNDRECT;
            else if (same(type, "ellipse") || same(type, "ooxml-ellipse")) obj->shape = OWF_SHAPE_ELLIPSE;
            else if (same(type, "isosceles-triangle") || same(type, "ooxml-triangle")) obj->shape = OWF_SHAPE_TRIANGLE;
            else if (same(type, "diamond") || same(type, "ooxml-diamond")) obj->shape = OWF_SHAPE_DIAMOND;
            else if (same(type, "right-arrow") || same(type, "ooxml-rightArrow")) obj->shape = OWF_SHAPE_ARROW;
            else if (same(type, "left-arrow") || same(type, "ooxml-leftArrow")) {
                obj->shape = OWF_SHAPE_ARROW;
                obj->flags |= OWF_OBJ_FLIP_H;
            } else {
                obj->kind = OWF_OBJ_OTHER;
                what = "Shapes other than rectangles, ellipses, triangles, diamonds and arrows";
            }
            if (same(attr(geo, NS_DRAW, "mirror-horizontal"), "true"))
                obj->flags ^= OWF_OBJ_FLIP_H;
            if (same(attr(geo, NS_DRAW, "mirror-vertical"), "true"))
                obj->flags ^= OWF_OBJ_FLIP_V;
            if (obj->shape == OWF_SHAPE_ROUNDRECT && obj->kind == OWF_OBJ_SHAPE) {
                /* the corner: a sixth of the shorter side unless its modifier says */
                const char *mod = attr(geo, NS_DRAW, "modifiers");
                long m = mod ? strtol(mod, NULL, 10) : 3600;
                int shorter = obj->width < obj->height ? obj->width : obj->height;
                if (m <= 0 || m > 10800)
                    m = 3600;
                obj->radius = (int)((long)shorter * m / 21600);
            }
        }
        read_look(o, obj, st, obj->shape != OWF_SHAPE_LINE);
        if (obj->shape == OWF_SHAPE_LINE)
            obj->look.fill = 0;
        read_text(o, obj, n, n, st, c->m, c->which);
    } else if (is(n, NS_DRAW, "g")) {
        const owf_xml_node *child;
        int i, first = 1;
        obj->kind = OWF_OBJ_GROUP;
        for (child = n->first; child; child = child->next)
            read_object(o, c, child, index, depth + 1);
        /* its box: around its objects' */
        obj = &(*c->objs)[index];
        for (i = index + 1; i < *c->n; i++) {
            const owf_obj *k = &(*c->objs)[i];
            int x2, y2;
            if (k->parent != index)
                continue;
            if (first) {
                obj->x = k->x;
                obj->y = k->y;
                obj->width = k->width;
                obj->height = k->height;
                first = 0;
                continue;
            }
            x2 = obj->x + obj->width > k->x + k->width ? obj->x + obj->width : k->x + k->width;
            y2 = obj->y + obj->height > k->y + k->height ? obj->y + obj->height : k->y + k->height;
            if (k->x < obj->x) obj->x = k->x;
            if (k->y < obj->y) obj->y = k->y;
            obj->width = x2 - obj->x;
            obj->height = y2 - obj->y;
        }
        return 1;
    } else {
        obj->kind = OWF_OBJ_OTHER;
        read_look(o, obj, st, 1);
        what = is(n, NS_DRAW, "connector") ? "Connectors"
             : is(n, NS_DRAW, "polyline") || is(n, NS_DRAW, "path") ? "Freeform lines and curves"
             : "Objects OpenPresentation doesn't draw yet";
        if (is(n, NS_DRAW, "connector") || is(n, NS_DRAW, "measure")) {
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            length_attr(n, NS_SVG, "x1", &x1);
            length_attr(n, NS_SVG, "y1", &y1);
            length_attr(n, NS_SVG, "x2", &x2);
            length_attr(n, NS_SVG, "y2", &y2);
            obj->x = x1 < x2 ? x1 : x2;
            obj->y = y1 < y2 ? y1 : y2;
            obj->width = x1 < x2 ? x2 - x1 : x1 - x2;
            obj->height = y1 < y2 ? y2 - y1 : y1 - y2;
        }
    }
    obj = &(*c->objs)[index];
    if (obj->kind == OWF_OBJ_OTHER) {
        obj->keep = keep_object(o, n, c->which);
        if (what)
            owf_report_add(o->report, OWF_NOTE_APPROX, "%s are kept and shown as their box", what);
    }
    if (obj->width < 0) obj->width = 0;
    if (obj->height < 0) obj->height = 0;
    return 1;
}

/* ---- Pages ---- */

/* A drawing-page style's background onto *colour and *image. */
static void read_background(odp *o, const gstyle *st, unsigned long *colour, int *image)
{
    const char *fill = prop(o, st, "drawing-page-properties", NS_DRAW, "fill");
    if (!fill)
        return;
    if (!strcmp(fill, "solid")) {
        *colour = shade(prop(o, st, "drawing-page-properties", NS_DRAW, "fill-color"));
    } else if (!strcmp(fill, "bitmap")) {
        const char *name = prop(o, st, "drawing-page-properties", NS_DRAW, "fill-image-name");
        const owf_xml_node *c;
        for (c = o->common ? o->common->first : NULL; c; c = c->next)
            if (is(c, NS_DRAW, "fill-image") && same(attr(c, NS_DRAW, "name"), name)) {
                *image = load_picture(o, c, o->deck->width, o->deck->height, NULL);
                break;
            }
    } else if (!strcmp(fill, "gradient")) {
        const char *name = prop(o, st, "drawing-page-properties", NS_DRAW, "fill-gradient-name");
        const owf_xml_node *c;
        for (c = o->common ? o->common->first : NULL; c; c = c->next)
            if (is(c, NS_DRAW, "gradient") && same(attr(c, NS_DRAW, "name"), name)) {
                *colour = shade(attr(c, NS_DRAW, "start-color"));
                break;
            }
        owf_report_add(o->report, OWF_NOTE_APPROX, "Gradient backgrounds are drawn in one colour");
    } else if (!strcmp(fill, "hatch")) {
        *colour = shade(prop(o, st, "drawing-page-properties", NS_DRAW, "fill-color"));
        owf_report_add(o->report, OWF_NOTE_APPROX, "Hatched backgrounds are drawn in one colour");
    }
}

/* The transition, from smil:type and the like. */
static void read_transition(odp *o, owf_slide *s, const gstyle *st)
{
    const char *type = prop(o, st, "drawing-page-properties", NS_SMIL, "type");
    const char *sub = prop(o, st, "drawing-page-properties", NS_SMIL, "subtype");
    const char *dir = prop(o, st, "drawing-page-properties", NS_SMIL, "direction");
    const char *colour = prop(o, st, "drawing-page-properties", NS_SMIL, "fadeColor");
    const char *style = prop(o, st, "drawing-page-properties", NS_PRES, "transition-style");
    const char *speed = prop(o, st, "drawing-page-properties", NS_PRES, "transition-speed");
    const char *how = prop(o, st, "drawing-page-properties", NS_PRES, "transition-type");
    owf_transition *t = &s->transition;
    int reverse = same(dir, "reverse");
    long ms;

    t->kind = OWF_TRANS_NONE;
    if (type) {
        if (!strcmp(type, "fade")) {
            t->kind = (same(sub, "fadeToColor") || same(sub, "fadeOverColor")) &&
                      (!colour || shade(colour) == OWF_SHADE(0)) ? OWF_TRANS_FADE_BLACK : OWF_TRANS_FADE;
            if ((same(sub, "fadeToColor") || same(sub, "fadeOverColor")) && colour && shade(colour) != OWF_SHADE(0))
                t->kind = OWF_TRANS_OTHER;
        } else if (!strcmp(type, "barWipe")) {
            t->kind = OWF_TRANS_WIPE;
            t->direction = same(sub, "topToBottom") ? (reverse ? OWF_DIR_DOWN : OWF_DIR_UP)
                                                     : (reverse ? OWF_DIR_RIGHT : OWF_DIR_LEFT);
        } else if (!strcmp(type, "pushWipe") || !strcmp(type, "slideWipe")) {
            t->kind = !strcmp(type, "pushWipe") ? OWF_TRANS_PUSH : reverse ? OWF_TRANS_UNCOVER : OWF_TRANS_COVER;
            t->direction = same(sub, "fromRight") ? OWF_DIR_RIGHT : same(sub, "fromTop") ? OWF_DIR_UP
                         : same(sub, "fromBottom") ? OWF_DIR_DOWN : OWF_DIR_LEFT;
            if (sub && !same(sub, "fromLeft") && !same(sub, "fromRight") && !same(sub, "fromTop") &&
                !same(sub, "fromBottom"))
                t->kind = OWF_TRANS_OTHER;
        } else if (!strcmp(type, "barnDoorWipe")) {
            t->kind = OWF_TRANS_SPLIT;
            t->direction = same(sub, "horizontal") ? OWF_DIR_UP : OWF_DIR_LEFT;
        } else if (!strcmp(type, "blindsWipe")) {
            t->kind = OWF_TRANS_BLINDS;
            t->direction = same(sub, "horizontal") ? OWF_DIR_UP : OWF_DIR_LEFT;
        } else if (!strcmp(type, "dissolve")) {
            t->kind = OWF_TRANS_DISSOLVE;
        } else {
            t->kind = OWF_TRANS_OTHER;
        }
    } else if (style && strcmp(style, "none")) {
        t->kind = OWF_TRANS_OTHER;
    }
    if (t->kind == OWF_TRANS_OTHER) {
        /* the attributes as they were, for save */
        static const struct { const char *ns, *prefix, *name; } keep[] = {
            { NS_SMIL, "smil", "type" }, { NS_SMIL, "smil", "subtype" }, { NS_SMIL, "smil", "direction" },
            { NS_SMIL, "smil", "fadeColor" }, { NS_PRES, "presentation", "transition-style" },
        };
        size_t k;
        o->scratch.length = 0;
        for (k = 0; k < sizeof keep / sizeof keep[0]; k++) {
            const char *v = prop(o, st, "drawing-page-properties", keep[k].ns, keep[k].name);
            if (!v)
                continue;
            owf_buf_printf(&o->scratch, " %s:%s=\"", keep[k].prefix, keep[k].name);
            put_escaped(&o->scratch, v, strlen(v), 1);
            owf_buf_putc(&o->scratch, '"');
        }
        if (!o->scratch.failed && o->scratch.length)
            t->keep = owf_deck_keep(o->deck, OWF_KEPT_ATTRIBUTES, (const char *)o->scratch.data, o->scratch.length);
        owf_report_add(o->report, OWF_NOTE_APPROX, "Transitions OpenPresentation doesn't show yet are kept, and shown as a cut");
    }
    t->duration = same(speed, "slow") ? 2000 : same(speed, "fast") ? 500 : same(speed, "medium") ? 1000 : 0;
    t->advance_after = -1;
    if (same(how, "automatic")) {
        ms = duration_ms(prop(o, st, "drawing-page-properties", NS_PRES, "duration"));
        t->advance_after = ms >= 0 && ms < 86400000L ? (int)ms : 0;
    }
}

static const owf_xml_node *find_decl(const odp *o, const char *element, const char *name)
{
    const owf_xml_node *c;
    if (!name)
        return NULL;
    for (c = o->presentation ? o->presentation->first : NULL; c; c = c->next)
        if (is(c, NS_PRES, element) && same(attr(c, NS_PRES, "name"), name))
            return c;
    return NULL;
}

static int master_index(const odp *o, const char *name)
{
    int i;
    for (i = 0; i < o->deck->nmasters; i++)
        if (same(o->mnames[i], name))
            return i;
    return 0;
}

static int layout_index(const odp *o, const char *name)
{
    int i;
    for (i = 0; name && i < o->deck->nlayouts; i++)
        if (same(o->lnames[i], name))
            return i;
    return -1;
}

static void read_notes(odp *o, owf_slide *s, const owf_xml_node *notes, const owf_master *m)
{
    const owf_xml_node *f;
    int found = 0;
    for (f = notes ? notes->first : NULL; f; f = f->next) {
        const owf_xml_node *box;
        owf_obj tmp;
        textctx t;
        memset(&t, 0, sizeof t);
        if (!is(f, NS_DRAW, "frame") || !same(attr(f, NS_PRES, "class"), "notes") ||
            !(box = owf_xml_child(f, NS_DRAW, "text-box")))
            continue;
        if (found++) {
            owf_report_add(o->report, OWF_NOTE_LOST, "A second notes box on a slide was left out");
            continue;
        }
        owf_obj_init(&tmp, OWF_OBJ_TEXT);
        tmp.placeholder = OWF_PH_NOTES;
        t.o = o;
        t.obj = &tmp;
        t.story = &s->notes;
        t.m = m;
        t.st = object_style(o, f, OWF_ODF_CONTENT_AUTO);
        t.pstyle = find_style(o, attr(f, NS_DRAW, "text-style-name"), "paragraph", OWF_ODF_CONTENT_AUTO);
        t.which = OWF_ODF_CONTENT_AUTO;
        t.keep_levels = 0;
        owf_builder_init(&t.b, o->deck->res, &s->notes);
        text_blocks(&t, box, 0, 0);
        if (owf_builder_done(&t.b) != OWF_OK)
            o->failed = 1;
        free(tmp.list);
    }
}

static void read_page(odp *o, const owf_xml_node *page)
{
    owf_slide *s;
    const gstyle *st;
    const owf_master *m;
    const owf_xml_node *c, *d;
    objctx oc;
    const char *v;

    if (o->deck->nslides >= OWF_DECK_MAX_SLIDES) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Slides beyond %d were left out", OWF_DECK_MAX_SLIDES);
        return;
    }
    s = owf_deck_add_slide(o->deck, -1, -1);
    if (!s) {
        o->failed = 1;
        return;
    }
    s->master = master_index(o, attr(page, NS_DRAW, "master-page-name"));
    s->layout = layout_index(o, attr(page, NS_PRES, "presentation-page-layout-name"));
    m = &o->deck->masters[s->master];
    if ((v = attr(page, NS_DRAW, "name")) && owf_deck_set_text(&s->name, v) < 0)
        o->failed = 1;
    st = find_style(o, attr(page, NS_DRAW, "style-name"), "drawing-page", OWF_ODF_CONTENT_AUTO);
    read_background(o, st, &s->background, &s->background_image);
    read_transition(o, s, st);
    s->hidden = same(prop(o, st, "drawing-page-properties", NS_PRES, "visibility"), "hidden");
    if (!same(prop(o, st, "drawing-page-properties", NS_PRES, "display-footer"), "false") &&
        attr(page, NS_PRES, "use-footer-name"))
        s->show |= OWF_SHOW_FOOTER;
    if (same(prop(o, st, "drawing-page-properties", NS_PRES, "display-page-number"), "true"))
        s->show |= OWF_SHOW_NUMBER;
    if (!same(prop(o, st, "drawing-page-properties", NS_PRES, "display-date-time"), "false") &&
        attr(page, NS_PRES, "use-date-time-name"))
        s->show |= OWF_SHOW_DATE;
    if ((d = find_decl(o, "footer-decl", attr(page, NS_PRES, "use-footer-name"))))
        s->footer = node_text(o, d);
    if ((d = find_decl(o, "date-time-decl", attr(page, NS_PRES, "use-date-time-name"))) &&
        same(attr(d, NS_PRES, "source"), "fixed"))
        s->date_text = node_text(o, d);
    if (!s->footer)
        s->show &= ~OWF_SHOW_FOOTER;

    oc.objs = &s->objs;
    oc.n = &s->nobjs;
    oc.cap = &s->capobjs;
    oc.m = m;
    oc.which = OWF_ODF_CONTENT_AUTO;
    oc.on_master = 0;
    for (c = page->first; c && !o->failed; c = c->next) {
        if (is(c, NS_PRES, "notes")) {
            read_notes(o, s, c, m);
        } else if (is(c, NS_ANIM, "par") || is(c, NS_ANIM, "seq")) {
            s->keep_timing = keep_xml(o, OWF_KEPT_TIMING, c);
            owf_report_add(o->report, OWF_NOTE_APPROX, "Animations are kept, not played");
        } else if (is(c, NS_OFFICE, "forms")) {
            /* nothing to show */
        } else {
            read_object(o, &oc, c, -1, 0);
        }
    }
}

/* ---- Masters, layouts and the page ---- */

static void page_size(odp *o, const char *layout_name, int *w, int *h)
{
    const owf_xml_node *c;
    for (c = o->autos_styles ? o->autos_styles->first : NULL; c; c = c->next) {
        const owf_xml_node *p;
        if (!is(c, NS_STYLE, "page-layout") || !same(attr(c, NS_STYLE, "name"), layout_name))
            continue;
        p = owf_xml_child(c, NS_STYLE, "page-layout-properties");
        if (length_attr(p, NS_FO, "page-width", w) && *w < 144)
            *w = 144;
        if (length_attr(p, NS_FO, "page-height", h) && *h < 144)
            *h = 144;
        if (*w > 2000000)
            *w = 2000000;
        if (*h > 2000000)
            *h = 2000000;
        return;
    }
}

/* The master's text levels from its presentation styles ("Default-title",
 * "Default-outline1"...). */
static void read_master_levels(odp *o, owf_master *m, const char *name, int font)
{
    char sname[200];
    const gstyle *st, *outline1;
    const owf_xml_node *list;
    int i;

    owf_deck_default_levels(m, font);
    /* other text: the graphic default and "standard" */
    st = find_style(o, "standard", "graphic", OWF_ODF_COMMON);
    if (st)
        apply_text(o, st, &m->other.para, &m->other.chars, 0);
    else {
        const owf_xml_node *d = default_style(o, "graphic");
        if (d) {
            owf_charprops cp;
            memset(&cp, 0, sizeof cp);
            owf_odf_text_props(o->text, owf_xml_child(d, NS_STYLE, "text-properties"), &cp);
            owf_charprops_apply(&m->other.chars, &cp);
        }
    }
#define LEVEL(suffix, lv) \
    snprintf(sname, sizeof sname, "%s-%s", name, suffix); \
    if ((st = find_style(o, sname, "presentation", OWF_ODF_COMMON))) \
        apply_text(o, st, &(lv)->para, &(lv)->chars, 0);
    LEVEL("title", &m->title)
    LEVEL("subtitle", &m->subtitle)
    LEVEL("notes", &m->notes)
    for (i = 0; i < OWF_TEXT_LEVELS; i++) {
        char suffix[24];
        snprintf(suffix, sizeof suffix, "outline%d", i + 1);
        LEVEL(suffix, &m->body[i])
    }
#undef LEVEL
    /* the outline's bullets and indents: its list's, over the levels */
    snprintf(sname, sizeof sname, "%s-outline1", name);
    outline1 = find_style(o, sname, "presentation", OWF_ODF_COMMON);
    list = style_list(o, outline1);
    if (list)
        read_list(o, list, m->body);
}

static void read_master(odp *o, const owf_xml_node *mp)
{
    const char *name = attr(mp, NS_STYLE, "name"), *display = attr(mp, NS_STYLE, "display-name");
    owf_master *m;
    const gstyle *st;
    objctx oc;
    const owf_xml_node *c;
    int index;

    if (o->deck->nmasters >= OWF_DECK_MAX_MASTERS) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Masters beyond %d were left out", OWF_DECK_MAX_MASTERS);
        return;
    }
    m = owf_deck_add_master(o->deck, display ? display : name ? name : "Default");
    if (!m) {
        o->failed = 1;
        return;
    }
    index = o->deck->nmasters - 1;
    o->mnames[index] = name ? name : "Default";
    if (index == 0)
        page_size(o, attr(mp, NS_STYLE, "page-layout-name"), &o->deck->width, &o->deck->height);
    read_master_levels(o, m, o->mnames[index], o->deck->res->base.font);
    m->background = OWF_SHADE(0xFFFFFF);
    st = find_style(o, attr(mp, NS_DRAW, "style-name"), "drawing-page", OWF_ODF_STYLES_AUTO);
    read_background(o, st, &m->background, &m->background_image);
    oc.objs = &m->objs;
    oc.n = &m->nobjs;
    oc.cap = &m->capobjs;
    oc.m = m;
    oc.which = OWF_ODF_STYLES_AUTO;
    oc.on_master = 1;
    for (c = mp->first; c && !o->failed; c = c->next) {
        if (is(c, NS_PRES, "notes")) {
            if (index == 0) {
                int w = o->deck->notes_page.width, h = o->deck->notes_page.height;
                page_size(o, attr(c, NS_STYLE, "page-layout-name"), &w, &h);
                o->deck->notes_page.width = w;
                o->deck->notes_page.height = h;
            }
        } else {
            read_object(o, &oc, c, -1, 0);
            m = &o->deck->masters[index];
        }
    }
}

static void read_layouts(odp *o)
{
    const owf_xml_node *c, *p;
    for (c = o->common ? o->common->first : NULL; c && !o->failed; c = c->next) {
        const char *name = attr(c, NS_STYLE, "name"), *display = attr(c, NS_STYLE, "display-name");
        owf_slide_layout *l;
        if (!is(c, NS_STYLE, "presentation-page-layout") || !name)
            continue;
        if (o->deck->nlayouts >= OWF_DECK_MAX_LAYOUTS)
            break;
        l = owf_deck_add_layout(o->deck, display ? display : name, -1);
        if (!l) {
            o->failed = 1;
            return;
        }
        o->lnames[o->deck->nlayouts - 1] = name;
        for (p = c->first; p; p = p->next) {
            owf_obj *obj;
            int i, index = 0;
            if (!is(p, NS_PRES, "placeholder"))
                continue;
            obj = owf_layout_add(l, OWF_OBJ_TEXT);
            if (!obj) {
                o->failed = 1;
                return;
            }
            obj->placeholder = placeholder_kind(attr(p, NS_PRES, "object"));
            if (obj->placeholder == OWF_PH_NONE) {
                l->nobjs--;
                continue;
            }
            for (i = 0; i < l->nobjs - 1; i++)
                if (l->objs[i].placeholder == obj->placeholder)
                    index++;
            obj->ph_index = index;
            length_attr(p, NS_SVG, "x", &obj->x);
            length_attr(p, NS_SVG, "y", &obj->y);
            length_attr(p, NS_SVG, "width", &obj->width);
            length_attr(p, NS_SVG, "height", &obj->height);
        }
    }
}

/* ---- Reading ---- */

static int read_deck(odp *o, const owf_xml_node *content_root, const owf_xml_node *styles_root,
                     const owf_xml_node *meta_root)
{
    const owf_xml_node *body, *c, *t;
    int font;

    o->common = owf_xml_child(styles_root, NS_OFFICE, "styles");
    o->autos_styles = owf_xml_child(styles_root, NS_OFFICE, "automatic-styles");
    o->autos_content = owf_xml_child(content_root, NS_OFFICE, "automatic-styles");
    o->masters_node = owf_xml_child(styles_root, NS_OFFICE, "master-styles");
    body = owf_xml_child(content_root, NS_OFFICE, "body");
    o->presentation = owf_xml_child(body, NS_OFFICE, "presentation");
    if (!o->presentation)
        return OWF_ERR_FORMAT;

    owf_odf_text_faces(o->text, owf_xml_child(styles_root, NS_OFFICE, "font-face-decls"));
    if (content_root != styles_root)
        owf_odf_text_faces(o->text, owf_xml_child(content_root, NS_OFFICE, "font-face-decls"));
    owf_odf_text_styles(o->text, o->common, OWF_ODF_COMMON);
    owf_odf_text_styles(o->text, o->autos_styles, OWF_ODF_STYLES_AUTO);
    owf_odf_text_styles(o->text, o->autos_content, OWF_ODF_CONTENT_AUTO);
    if (content_root == styles_root)
        owf_odf_text_styles(o->text, o->autos_content, OWF_ODF_STYLES_AUTO);
    add_styles(o, o->common, OWF_ODF_COMMON);
    add_styles(o, o->autos_styles, OWF_ODF_STYLES_AUTO);
    if (content_root != styles_root)
        add_styles(o, o->autos_content, OWF_ODF_CONTENT_AUTO);
    if (o->failed)
        return OWF_ERR_MEMORY;

    /* the default text: the graphic default style's font and size */
    {
        owf_charfmt cf;
        const owf_xml_node *d = default_style(o, "graphic");
        owf_charfmt_init(&cf);
        cf.size = 18 * OWF_TWIPS_PER_POINT;
        if (d) {
            owf_charprops cp;
            memset(&cp, 0, sizeof cp);
            owf_odf_text_props(o->text, owf_xml_child(d, NS_STYLE, "text-properties"), &cp);
            owf_charprops_apply(&cf, &cp);
        }
        font = cf.font >= 0 ? cf.font : owf_doc_font(o->deck->res, "Liberation Sans", OWF_FONT_SANS);
        if (font < 0)
            return OWF_ERR_MEMORY;
        o->deck->res->base.font = font;
        o->deck->res->base.size = cf.size;
    }

    /* the graphic defaults kept objects' styles inherit */
    {
        const gstyle *std = find_style(o, "standard", "graphic", OWF_ODF_COMMON);
        const owf_xml_node *gp = std ? owf_xml_child(std->node, NS_STYLE, "graphic-properties") : NULL;
        /* none to keep when they are what the writer writes without them */
        int plain = gp && !gp->first && gp->nattrs == 2 && same(attr(gp, NS_DRAW, "stroke"), "none") &&
                    same(attr(gp, NS_DRAW, "fill"), "none");
        if (gp && !plain && keep_xml(o, OWF_KEPT_GRAPHIC, gp) >= 0)
            keep_refs(o, gp, OWF_ODF_COMMON, 1);
    }
    read_layouts(o);
    for (c = o->masters_node ? o->masters_node->first : NULL; c && !o->failed; c = c->next)
        if (is(c, NS_STYLE, "master-page"))
            read_master(o, c);
    if (!o->deck->nmasters) {
        /* a presentation without a master: one plain master */
        owf_master *m = owf_deck_add_master(o->deck, "Default");
        if (!m)
            return OWF_ERR_MEMORY;
        o->mnames[0] = "Default";
        owf_deck_default_levels(m, font);
        m->background = OWF_SHADE(0xFFFFFF);
    }
    for (c = o->presentation->first; c && !o->failed; c = c->next)
        if (is(c, NS_DRAW, "page"))
            read_page(o, c);
        else if (is(c, NS_PRES, "settings")) {
            if (keep_xml(o, OWF_KEPT_SETTINGS, c) >= 0 && owf_xml_child(c, NS_PRES, "show"))
                owf_report_add(o->report, OWF_NOTE_APPROX, "Custom shows are kept, not shown");
        }

    if (meta_root && (t = owf_xml_child(owf_xml_child(meta_root, NS_OFFICE, "meta"), NS_DC, "title"))) {
        char *title = node_text(o, t);
        if (title) {
            if (owf_doc_set_title(o->deck->res, title) < 0)
                o->failed = 1;
            free(title);
        }
    }
    return o->failed || o->scratch.failed ? OWF_ERR_MEMORY : OWF_OK;
}

static int parse_part(const owf_unzip *zip, const char *name, owf_xml **xml, int required)
{
    unsigned char *data;
    size_t length;
    int result = owf_unzip_read(zip, name, &data, &length), error;

    *xml = NULL;
    if (result != OWF_OK)
        return required ? (result == OWF_ERR_FORMAT ? OWF_ERR_CORRUPT : result) : OWF_OK;
    *xml = owf_xml_parse((const char *)data, length, &error);
    free(data);
    return *xml ? OWF_OK : (required ? error : OWF_OK);
}

static odp *new_reader(owf_deck *deck, owf_report *report, const owf_unzip *zip)
{
    odp *o = calloc(1, sizeof *o);
    if (!o)
        return NULL;
    o->deck = deck;
    o->report = report;
    o->zip = zip;
    owf_buf_init(&o->scratch);
    o->text = owf_odf_text_new(deck->res, report, zip);
    if (!o->text) {
        free(o);
        return NULL;
    }
    return o;
}

static void free_reader(odp *o)
{
    owf_odf_text_free(o->text);
    owf_buf_free(&o->scratch);
    free(o->styles);
    free(o->pics);
    free(o->keptnodes);
    free(o);
}

static int import_odp(const unsigned char *data, size_t length, owf_deck *deck, owf_report *report)
{
    owf_unzip zip;
    owf_xml *content = NULL, *styles = NULL, *meta = NULL, *manifest = NULL;
    odp *o;
    int result;

    if (!owf_unzip_open(&zip, data, length))
        return OWF_ERR_FORMAT;
    result = parse_part(&zip, "content.xml", &content, 1);
    if (result == OWF_OK)
        result = parse_part(&zip, "styles.xml", &styles, 0);
    if (result == OWF_OK)
        result = parse_part(&zip, "meta.xml", &meta, 0);
    if (result == OWF_OK)
        result = parse_part(&zip, "META-INF/manifest.xml", &manifest, 0);
    if (result == OWF_OK) {
        o = new_reader(deck, report, &zip);
        if (!o) {
            result = OWF_ERR_MEMORY;
        } else {
            o->manifest = manifest ? owf_xml_root(manifest) : NULL;
            result = read_deck(o, owf_xml_root(content), styles ? owf_xml_root(styles) : owf_xml_root(content),
                               meta ? owf_xml_root(meta) : NULL);
            free_reader(o);
        }
    }
    owf_xml_free(content);
    owf_xml_free(styles);
    owf_xml_free(meta);
    owf_xml_free(manifest);
    return result;
}

static int import_fodp(const unsigned char *data, size_t length, owf_deck *deck, owf_report *report)
{
    owf_xml *xml;
    odp *o;
    int error, result;

    xml = owf_xml_parse((const char *)data, length, &error);
    if (!xml)
        return error;
    if (!owf_xml_is(owf_xml_root(xml), NS_OFFICE, "document")) {
        owf_xml_free(xml);
        return OWF_ERR_FORMAT;
    }
    o = new_reader(deck, report, NULL);
    if (!o) {
        owf_xml_free(xml);
        return OWF_ERR_MEMORY;
    }
    result = read_deck(o, owf_xml_root(xml), owf_xml_root(xml), owf_xml_root(xml));
    free_reader(o);
    owf_xml_free(xml);
    return result;
}

static int detect_odp(const unsigned char *data, size_t length)
{
    return owf_odf_kind(data, length) == OWF_PACKAGE_SLIDES ? 100 : 0;
}

static int detect_fodp(const unsigned char *data, size_t length)
{
    static const char mime[] = "application/vnd.oasis.opendocument.presentation";
    size_t n = length < 4096 ? length : 4096, i, j;
    for (i = 0; i + 16 < n; i++)
        if (data[i] == '<' && !memcmp(data + i, "<office:document", 16) &&
            (data[i + 16] == ' ' || data[i + 16] == '\n' || data[i + 16] == '\r' || data[i + 16] == '\t')) {
            for (j = i; j + sizeof mime - 1 < n; j++)
                if (!memcmp(data + j, mime, sizeof mime - 1))
                    return 90;
            return 0;
        }
    return 0;
}

const owf_deck_format owf_deck_format_odp = {
    "odp", "OpenDocument Presentation", "odp otp", detect_odp, import_odp, owf_export_odp
};

const owf_deck_format owf_deck_format_fodp = {
    "fodp", "OpenDocument Presentation, flat XML", "fodp", detect_fodp, import_fodp, owf_export_fodp
};

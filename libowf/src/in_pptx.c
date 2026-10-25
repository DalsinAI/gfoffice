/*
 * libowf: PPTX in (Office Open XML PresentationML, ECMA-376 Part 1, and
 * DrawingML for the shapes and text), .pptx, .potx and .ppsx, into a deck
 * (owf_deck.h). Written from the published standard; no other program's
 * code was read.
 *
 * ppt/presentation.xml gives the slide size (p:sldSz, in EMU: 914,400 an
 * inch, 635 a twip) and the slides in order; each slide, layout, master,
 * theme and notes page is a part of its own, joined by relationships.
 * Shapes are p:sp (placeholders name their kind in p:nvPr/p:ph), pictures
 * p:pic, lines p:cxnSp, groups p:grpSp, tables a:tbl in a p:graphicFrame.
 *
 * Text looks are inherited, lowest first: the presentation's default text
 * style (or the master's title, body or other text style, for a
 * placeholder), the master's placeholder of that kind, the layout's, the
 * shape's own list style, then the paragraph's and the run's own. Each
 * paragraph is worked out here, whole; each run then keeps only what
 * differs from its level's look (owf_deck_para_level), as the ODP reader's
 * do. Bulleted paragraphs are list paragraphs at their level; where their
 * look is not the master's body level, the object gets a list of its own.
 * Theme colours are worked out to fixed colours, and the report says so.
 *
 * What the model doesn't hold (charts, SmartArt, embedded objects, a
 * layout's own pictures, animations) is left out or shown as a box, and the
 * report says which. Nothing is kept for saving as PPTX yet: OpenPresentation
 * saves what it read as ODP.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_deck_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_A "http://schemas.openxmlformats.org/drawingml/2006/main"
#define NS_P "http://schemas.openxmlformats.org/presentationml/2006/main"
#define NS_R "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define NS_A_STRICT "http://purl.oclc.org/ooxml/drawingml/main"
#define NS_P_STRICT "http://purl.oclc.org/ooxml/presentationml/main"
#define NS_R_STRICT "http://purl.oclc.org/ooxml/officeDocument/relationships"
#define NS_MC "http://schemas.openxmlformats.org/markup-compatibility/2006"
#define NS_DC "http://purl.org/dc/elements/1.1/"

#define EMU_PER_TWIP 635
#define MAX_COORD 2000000            /* twips: about 35 metres, as the ODP reader */
#define MAX_PARTS 512                /* layouts and masters read, all told */
#define NAME_SIZE 200
#define TABLE_MAX_COLS 64

/* ---- Small helpers ---- */

static int ns_a(const char *ns)
{
    return ns && (!strcmp(ns, NS_A) || !strcmp(ns, NS_A_STRICT));
}

static int ns_p(const char *ns)
{
    return ns && (!strcmp(ns, NS_P) || !strcmp(ns, NS_P_STRICT));
}

/* A DrawingML (a:) or PresentationML (p:) element of that name. */
static int isa(const owf_xml_node *n, const char *name)
{
    return n && n->type == OWF_XML_ELEMENT && ns_a(n->ns) && !strcmp(n->name, name);
}

static int isp(const owf_xml_node *n, const char *name)
{
    return n && n->type == OWF_XML_ELEMENT && ns_p(n->ns) && !strcmp(n->name, name);
}

static const owf_xml_node *achild(const owf_xml_node *n, const char *name)
{
    const owf_xml_node *c;
    for (c = n ? n->first : NULL; c; c = c->next)
        if (isa(c, name))
            return c;
    return NULL;
}

static const owf_xml_node *pchild(const owf_xml_node *n, const char *name)
{
    const owf_xml_node *c;
    for (c = n ? n->first : NULL; c; c = c->next)
        if (isp(c, name))
            return c;
    return NULL;
}

/* An attribute by its local name, whatever its namespace (PresentationML's
 * own attributes have none). */
static const char *att(const owf_xml_node *n, const char *name)
{
    int i;
    if (!n)
        return NULL;
    for (i = 0; i < n->nattrs; i++)
        if (!strcmp(n->attrs[i].name, name))
            return n->attrs[i].value;
    return NULL;
}

/* r:id, r:embed, r:link: the relationships namespace's, either flavour. */
static const char *ratt(const owf_xml_node *n, const char *name)
{
    const char *v;
    if (!n)
        return NULL;
    v = owf_xml_attr(n, NS_R, name);
    return v ? v : owf_xml_attr(n, NS_R_STRICT, name);
}

static int same(const char *a, const char *b)
{
    return a && b && !strcmp(a, b);
}

static int clamp(long long v, long long lo, long long hi)
{
    return (int)(v < lo ? lo : v > hi ? hi : v);
}

/* A whole number as the file writes it, kept within +-2^30 (EMU: more than
 * MAX_COORD) so that sums and products of two stay in 64 bits; def when
 * there is none. */
static long long number(const char *s, long long def)
{
    long long v = 0;
    int neg = 0, digits = 0;
    if (!s)
        return def;
    while (*s == ' ')
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    for (; *s >= '0' && *s <= '9'; s++) {
        if (v < (1LL << 30))
            v = v * 10 + (*s - '0');
        digits++;
    }
    if (!digits)
        return def;
    if (v > (1LL << 30))
        v = 1LL << 30;
    return neg ? -v : v;
}

static int number_att(const owf_xml_node *n, const char *name, long long *out)
{
    const char *v = att(n, name);
    if (!v)
        return 0;
    while (*v == ' ' || *v == '-' || *v == '+')
        v++;
    if (*v < '0' || *v > '9')
        return 0;
    *out = number(att(n, name), 0);
    return 1;
}

static int emu_to_twips(long long emu)
{
    return clamp(emu >= 0 ? (emu + EMU_PER_TWIP / 2) / EMU_PER_TWIP : (emu - EMU_PER_TWIP / 2) / EMU_PER_TWIP,
                 -MAX_COORD, MAX_COORD);
}

/* The text of an element: its own and its children's. */
static void text_of(const owf_xml_node *n, owf_buf *out, int depth)
{
    const owf_xml_node *c;
    if (depth > OWF_DECK_MAX_DEPTH)
        return;
    for (c = n ? n->first : NULL; c; c = c->next) {
        if (c->type == OWF_XML_TEXT)
            owf_buf_put(out, c->name, c->length);
        else
            text_of(c, out, depth + 1);
    }
}

/* ---- Parts and relationships ---- */

typedef struct {
    const char *id, *type, *target;
    int external;
} rel;

typedef struct {
    owf_xml *xml;
    rel *items;
    int count;
    char dir[NAME_SIZE];         /* the source part's folder: "ppt/slides/" */
} rels;

typedef struct {
    const owf_unzip *zip;
    owf_report *report;
    int failed;
} reader;

static owf_xml *read_xml(const owf_unzip *zip, const char *name, int *error)
{
    unsigned char *data;
    size_t length;
    owf_xml *xml;
    int e;
    if (!error)
        error = &e;
    *error = owf_unzip_read(zip, name, &data, &length);
    if (*error != OWF_OK)
        return NULL;
    xml = owf_xml_parse((const char *)data, length, error);
    free(data);
    return xml;
}

/* A part's relationships: "ppt/slides/_rels/slide1.xml.rels" for
 * "ppt/slides/slide1.xml". None is not an error. */
static void read_rels(const owf_unzip *zip, const char *part, rels *r)
{
    const owf_xml_node *n;
    const char *slash = strrchr(part, '/');
    char name[NAME_SIZE + 16];
    int count = 0;

    memset(r, 0, sizeof *r);
    snprintf(r->dir, sizeof r->dir, "%.*s", slash ? (int)(slash - part + 1) : 0, part);
    snprintf(name, sizeof name, "%s_rels/%s.rels", r->dir, slash ? slash + 1 : part);
    r->xml = read_xml(zip, name, NULL);
    if (!r->xml)
        return;
    for (n = owf_xml_root(r->xml)->first; n; n = n->next)
        if (n->type == OWF_XML_ELEMENT)
            count++;
    r->items = calloc((size_t)count + 1, sizeof *r->items);
    if (!r->items)
        return;
    for (n = owf_xml_root(r->xml)->first; n; n = n->next) {
        rel *it = &r->items[r->count];
        if (n->type != OWF_XML_ELEMENT || strcmp(n->name, "Relationship"))
            continue;
        it->id = att(n, "Id");
        it->type = att(n, "Type");
        it->target = att(n, "Target");
        it->external = same(att(n, "TargetMode"), "External");
        if (it->id && it->type && it->target)
            r->count++;
    }
}

static void free_rels(rels *r)
{
    free(r->items);
    owf_xml_free(r->xml);
    memset(r, 0, sizeof *r);
}

static const rel *rel_id(const rels *r, const char *id)
{
    int i;
    for (i = 0; id && i < r->count; i++)
        if (!strcmp(r->items[i].id, id))
            return &r->items[i];
    return NULL;
}

/* The first relationship whose type ends in "/kind". */
static const rel *rel_type(const rels *r, const char *kind)
{
    size_t k = strlen(kind);
    int i;
    for (i = 0; i < r->count; i++) {
        size_t n = strlen(r->items[i].type);
        if (n > k && r->items[i].type[n - k - 1] == '/' && !strcmp(r->items[i].type + n - k, kind))
            return &r->items[i];
    }
    return NULL;
}

/* The part a relationship points to, from the package's root. */
static int rel_part(const rels *r, const rel *rl, char *out, size_t size)
{
    if (!rl || rl->external)
        return 0;
    return owf_opc_part_name(out, size, r->dir, rl->target);
}

/* ---- Colours ----
 * A theme names twelve colours; a master's colour map says which of them
 * bg1, tx1, bg2 and tx2 are. Colours are worked out to RGB here, their
 * changes (tint, shade, lumMod, lumOff) in whole numbers: no floating
 * point. */

enum { C_DK1, C_LT1, C_DK2, C_LT2, C_ACC1, C_ACC2, C_ACC3, C_ACC4, C_ACC5, C_ACC6, C_HLINK, C_FOLHLINK, C_COUNT };
static const char *const scheme_names[C_COUNT] = {
    "dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5", "accent6", "hlink", "folHlink"
};

typedef struct {
    unsigned long scheme[C_COUNT];   /* 0xRRGGBB */
    int major, minor;                /* the theme's fonts: into res->fonts */
    int line_width[3];               /* the line styles' widths, twips */
} theme;

typedef struct {
    const theme *th;
    int map[4];                      /* bg1, tx1, bg2, tx2: into scheme */
    unsigned long ph;                /* phClr: a style reference's colour */
    int has_ph;
} colours;

static int scheme_index(const char *name)
{
    int i;
    for (i = 0; name && i < C_COUNT; i++)
        if (!strcmp(name, scheme_names[i]))
            return i;
    return -1;
}

/* A master's p:clrMap (or a slide's a:overrideClrMapping). */
static void read_map(const owf_xml_node *n, int map[4])
{
    static const char *const keys[4] = { "bg1", "tx1", "bg2", "tx2" };
    static const int defaults[4] = { C_LT1, C_DK1, C_LT2, C_DK2 };
    int i;
    for (i = 0; i < 4; i++) {
        int k = scheme_index(att(n, keys[i]));
        map[i] = k >= 0 ? k : defaults[i];
    }
}

static int hex6(const char *s, unsigned long *rgb)
{
    unsigned long v = 0;
    int i;
    if (!s)
        return 0;
    for (i = 0; i < 6; i++) {
        int c = s[i], d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            return 0;
        v = v << 4 | (unsigned long)d;
    }
    *rgb = v;
    return 1;
}

/* Hue in thousandths of a degree, saturation and lightness in thousandths
 * of a percent (100000 is all), as DrawingML measures them. */
static void rgb_hsl(unsigned long rgb, long long *h, long long *s, long long *l)
{
    long long r = (long long)(rgb >> 16 & 255), g = (long long)(rgb >> 8 & 255), b = (long long)(rgb & 255);
    long long mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    long long d = mx - mn;
    *l = (mx + mn) * 100000 / 510;
    if (!d) {
        *h = *s = 0;
        return;
    }
    *s = *l > 50000 ? d * 100000 / (510 - mx - mn) : d * 100000 / (mx + mn);
    if (mx == r)
        *h = ((g - b) * 60000 / d + 360000) % 360000;
    else if (mx == g)
        *h = (b - r) * 60000 / d + 120000;
    else
        *h = (r - g) * 60000 / d + 240000;
}

static long long hue_part(long long p, long long q, long long t)
{
    t = (t % 360000 + 360000) % 360000;
    if (t < 60000) return p + (q - p) * t / 60000;
    if (t < 180000) return q;
    if (t < 240000) return p + (q - p) * (240000 - t) / 60000;
    return p;
}

static unsigned long hsl_rgb(long long h, long long s, long long l)
{
    long long q, p, r, g, b;
    if (s <= 0) {
        r = g = b = l;
    } else {
        q = l < 50000 ? l * (100000 + s) / 100000 : l + s - l * s / 100000;
        p = 2 * l - q;
        r = hue_part(p, q, h + 120000);
        g = hue_part(p, q, h);
        b = hue_part(p, q, h - 120000);
    }
    r = (r * 255 + 50000) / 100000;
    g = (g * 255 + 50000) / 100000;
    b = (b * 255 + 50000) / 100000;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (unsigned long)(r << 16 | g << 8 | b);
}

/* The colour's changes, in the order the file gives them. */
static unsigned long modify(const owf_xml_node *c, unsigned long rgb)
{
    const owf_xml_node *m;
    for (m = c->first; m; m = m->next) {
        long long v;
        if (m->type != OWF_XML_ELEMENT || !ns_a(m->ns) || !number_att(m, "val", &v))
            continue;
        if (v < 0) v = 0;
        if (v > 10000000) v = 10000000;
        if (!strcmp(m->name, "tint") || !strcmp(m->name, "shade")) {
            /* toward white (a tint) or black (a shade): v of the colour */
            int tint = !strcmp(m->name, "tint"), k;
            unsigned long out = 0;
            if (v > 100000) v = 100000;
            for (k = 16; k >= 0; k -= 8) {
                long long ch = (long long)(rgb >> k & 255);
                ch = tint ? 255 - (255 - ch) * v / 100000 : ch * v / 100000;
                out |= (unsigned long)ch << k;
            }
            rgb = out;
        } else if (!strcmp(m->name, "lumMod") || !strcmp(m->name, "lumOff") || !strcmp(m->name, "satMod")) {
            long long h, s, l;
            rgb_hsl(rgb, &h, &s, &l);
            if (!strcmp(m->name, "lumMod")) l = l * v / 100000;
            else if (!strcmp(m->name, "lumOff")) l += v;
            else s = s * v / 100000;
            l = l < 0 ? 0 : l > 100000 ? 100000 : l;
            s = s < 0 ? 0 : s > 100000 ? 100000 : s;
            rgb = hsl_rgb(h, s, l);
        }
    }
    return rgb;
}

static const struct { const char *name; unsigned long rgb; } preset_colours[] = {
    { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 },
    { "blue", 0x0000FF }, { "yellow", 0xFFFF00 }, { "gray", 0x808080 }, { "grey", 0x808080 },
    { "darkGray", 0xA9A9A9 }, { "lightGray", 0xD3D3D3 }, { "orange", 0xFFA500 }, { "navy", 0x000080 },
    { "silver", 0xC0C0C0 }, { "maroon", 0x800000 }, { "purple", 0x800080 }, { "lime", 0x00FF00 },
};

/* One colour element (a:srgbClr, a:schemeClr...), its changes applied. */
static int one_colour(const colours *cc, const owf_xml_node *c, unsigned long *out)
{
    unsigned long rgb = 0;
    int found = 0;
    if (!c || c->type != OWF_XML_ELEMENT || !ns_a(c->ns))
        return 0;
    {
        if (!strcmp(c->name, "srgbClr")) {
            found = hex6(att(c, "val"), &rgb);
        } else if (!strcmp(c->name, "sysClr")) {
            found = hex6(att(c, "lastClr"), &rgb);
            if (!found) {
                rgb = same(att(c, "val"), "window") ? 0xFFFFFF : 0;
                found = 1;
            }
        } else if (!strcmp(c->name, "schemeClr")) {
            const char *v = att(c, "val");
            int k = -1;
            if (same(v, "phClr")) {
                if (cc->has_ph) {
                    rgb = cc->ph;
                    found = 1;
                }
            } else if (cc->th) {
                if (same(v, "bg1")) k = cc->map[0];
                else if (same(v, "tx1")) k = cc->map[1];
                else if (same(v, "bg2")) k = cc->map[2];
                else if (same(v, "tx2")) k = cc->map[3];
                else k = scheme_index(v);
                if (k >= 0) {
                    rgb = cc->th->scheme[k];
                    found = 1;
                }
            }
        } else if (!strcmp(c->name, "prstClr")) {
            size_t i;
            for (i = 0; i < sizeof preset_colours / sizeof preset_colours[0]; i++)
                if (same(att(c, "val"), preset_colours[i].name)) {
                    rgb = preset_colours[i].rgb;
                    found = 1;
                }
        } else if (!strcmp(c->name, "scrgbClr")) {
            long long r = number(att(c, "r"), 0), g = number(att(c, "g"), 0), b = number(att(c, "b"), 0);
            r = r < 0 ? 0 : r > 100000 ? 255 : r * 255 / 100000;
            g = g < 0 ? 0 : g > 100000 ? 255 : g * 255 / 100000;
            b = b < 0 ? 0 : b > 100000 ? 255 : b * 255 / 100000;
            rgb = (unsigned long)(r << 16 | g << 8 | b);
            found = 1;
        } else if (!strcmp(c->name, "hslClr")) {
            long long hue = number(att(c, "hue"), 0) / 60, sat = number(att(c, "sat"), 0), lum = number(att(c, "lum"), 0);
            rgb = hsl_rgb(hue < 0 ? 0 : hue % 360000, sat < 0 ? 0 : sat > 100000 ? 100000 : sat,
                          lum < 0 ? 0 : lum > 100000 ? 100000 : lum);
            found = 1;
        }
        if (found)
            rgb = modify(c, rgb);
    }
    if (found)
        *out = rgb & 0xFFFFFFUL;
    return found;
}

/* A colour element, or the first one inside n (a:solidFill, a:buClr, a
 * style reference). 0 when there is none. */
static int colour_of(const colours *cc, const owf_xml_node *n, unsigned long *out)
{
    const owf_xml_node *c;
    if (!n)
        return 0;
    if (one_colour(cc, n, out))
        return 1;
    for (c = n->first; c; c = c->next)
        if (one_colour(cc, c, out))
            return 1;
    return 0;
}

/* A fill: a:solidFill, a:gradFill (its first stop), a:pattFill (its
 * foreground), a:noFill. 1 with *fill (OWF_SHADE, 0 for none) when n holds
 * one. */
static int fill_of(reader *x, const colours *cc, const owf_xml_node *n, unsigned long *fill)
{
    const owf_xml_node *f;
    unsigned long rgb;
    for (f = n ? n->first : NULL; f; f = f->next) {
        if (isa(f, "noFill")) {
            *fill = 0;
            return 1;
        }
        if (isa(f, "solidFill")) {
            *fill = colour_of(cc, f, &rgb) ? OWF_SHADE(rgb) : 0;
            return 1;
        }
        if (isa(f, "gradFill")) {
            const owf_xml_node *gs = achild(achild(f, "gsLst"), "gs");
            *fill = gs && colour_of(cc, gs, &rgb) ? OWF_SHADE(rgb) : 0;
            owf_report_add(x->report, OWF_NOTE_APPROX, "Gradient and pattern fills are drawn in one colour");
            return 1;
        }
        if (isa(f, "pattFill")) {
            *fill = colour_of(cc, achild(f, "fgClr"), &rgb) ? OWF_SHADE(rgb) : 0;
            owf_report_add(x->report, OWF_NOTE_APPROX, "Gradient and pattern fills are drawn in one colour");
            return 1;
        }
        if (isa(f, "blipFill")) {
            *fill = 0;
            owf_report_add(x->report, OWF_NOTE_LOST, "Picture fills of shapes were left out");
            return 1;
        }
        if (isa(f, "grpFill")) {
            *fill = 0;
            return 1;
        }
    }
    return 0;
}

/* The theme part: its colours, fonts and line widths. */
static void read_theme(reader *x, owf_deck *deck, const char *part, theme *th)
{
    static const unsigned long office[C_COUNT] = {
        0x000000, 0xFFFFFF, 0x1F497D, 0xEEECE1, 0x4F81BD, 0xC0504D, 0x9BBB59, 0x8064A2, 0x4BACC6, 0xF79646,
        0x0000FF, 0x800080
    };
    owf_xml *xml;
    const owf_xml_node *el, *c;
    colours none;
    int i;

    memcpy(th->scheme, office, sizeof office);
    th->line_width[0] = 15;
    th->line_width[1] = 40;
    th->line_width[2] = 60;
    memset(&none, 0, sizeof none);
    xml = part ? read_xml(x->zip, part, NULL) : NULL;
    el = xml ? achild(owf_xml_root(xml), "themeElements") : NULL;
    for (c = achild(el, "clrScheme") ? achild(el, "clrScheme")->first : NULL; c; c = c->next) {
        unsigned long rgb;
        int k;
        if (c->type != OWF_XML_ELEMENT || (k = scheme_index(c->name)) < 0)
            continue;
        if (colour_of(&none, c, &rgb))
            th->scheme[k] = rgb;
    }
    {
        const owf_xml_node *fs = achild(el, "fontScheme");
        const char *major = att(achild(achild(fs, "majorFont"), "latin"), "typeface");
        const char *minor = att(achild(achild(fs, "minorFont"), "latin"), "typeface");
        th->major = owf_doc_font(deck->res, major && *major ? major : "Calibri Light", OWF_FONT_ANY);
        th->minor = owf_doc_font(deck->res, minor && *minor ? minor : "Calibri", OWF_FONT_ANY);
        if (th->major < 0 || th->minor < 0)
            x->failed = 1;
    }
    i = 0;
    for (c = achild(achild(el, "fmtScheme"), "lnStyleLst") ? achild(achild(el, "fmtScheme"), "lnStyleLst")->first : NULL;
         c && i < 3; c = c->next)
        if (isa(c, "ln")) {
            long long w;
            if (number_att(c, "w", &w))
                th->line_width[i] = clamp(w / EMU_PER_TWIP, 1, 2000);
            i++;
        }
    owf_xml_free(xml);
}

/* ---- Text properties ----
 * What a level of a list style (a:lvl1pPr ... a:lvl9pPr), a paragraph's
 * a:pPr or a run's a:rPr says, each part only when it says it, so that
 * they can be laid over one another down the inheritance. */

enum {
    TP_MARL = 1 << 0, TP_INDENT = 1 << 1, TP_ALGN = 1 << 2, TP_BEFORE = 1 << 3, TP_AFTER = 1 << 4,
    TP_LINE = 1 << 5, TP_BULLET = 1 << 6, TP_BUFONT = 1 << 7, TP_BUSIZE = 1 << 8, TP_BUCOLOUR = 1 << 9,
    TP_SIZE = 1 << 10, TP_BOLD = 1 << 11, TP_ITALIC = 1 << 12, TP_UNDERLINE = 1 << 13, TP_STRIKE = 1 << 14,
    TP_COLOUR = 1 << 15, TP_FONT = 1 << 16, TP_BASELINE = 1 << 17
};
enum { BU_NONE, BU_CHAR, BU_NUMBER };

typedef struct {
    unsigned long has;
    int marl, indent;            /* twips */
    owf_align align;
    int before, after;           /* twips, or (with _pct) thousandths of a percent of the size */
    int before_pct, after_pct;
    int line;                    /* percent */
    int bullet;                  /* BU_... */
    unsigned long bu_char;
    char bu_scheme[24];
    int bu_start;
    int bu_font;                 /* into res->fonts */
    int bu_size;                 /* percent */
    unsigned long bu_colour;     /* 0xRRGGBB */
    int size;                    /* twips */
    int bold, italic, underline, strike, baseline;
    unsigned long colour;        /* 0xRRGGBB */
    int font;
} tprops;

typedef struct {
    reader *x;
    owf_deck *deck;
    const theme *th;
    colours cc;
} textctx;

static int font_named(textctx *t, const char *name)
{
    int f;
    if (!name || !*name)
        return -1;
    if (name[0] == '+' && t->th) {
        /* the theme's: +mj-lt, +mn-lt (and their ea and cs, read as latin) */
        return !strncmp(name, "+mj", 3) ? t->th->major : t->th->minor;
    }
    f = owf_doc_font(t->deck->res, name, OWF_FONT_ANY);
    if (f < 0)
        t->x->failed = 1;
    return f;
}

static void read_rpr(textctx *t, const owf_xml_node *r, tprops *p)
{
    long long v;
    const char *s;
    unsigned long rgb;
    const owf_xml_node *f;
    if (!r)
        return;
    if (number_att(r, "sz", &v) && v >= 100 && v <= 400000) {
        p->size = (int)(v / 5);                      /* hundredths of a point */
        p->has |= TP_SIZE;
    }
    if ((s = att(r, "b"))) {
        p->bold = same(s, "1") || same(s, "true");
        p->has |= TP_BOLD;
    }
    if ((s = att(r, "i"))) {
        p->italic = same(s, "1") || same(s, "true");
        p->has |= TP_ITALIC;
    }
    if ((s = att(r, "u"))) {
        p->underline = !same(s, "none");
        p->has |= TP_UNDERLINE;
    }
    if ((s = att(r, "strike"))) {
        p->strike = !same(s, "noStrike");
        p->has |= TP_STRIKE;
    }
    if (number_att(r, "baseline", &v)) {
        p->baseline = v > 0 ? 1 : v < 0 ? -1 : 0;
        p->has |= TP_BASELINE;
    }
    for (f = r->first; f; f = f->next) {
        if (isa(f, "solidFill") && colour_of(&t->cc, f, &rgb)) {
            p->colour = rgb;
            p->has |= TP_COLOUR;
        } else if (isa(f, "gradFill")) {
            const owf_xml_node *gs = achild(achild(f, "gsLst"), "gs");
            if (gs && colour_of(&t->cc, gs, &rgb)) {
                p->colour = rgb;
                p->has |= TP_COLOUR;
            }
        } else if (isa(f, "latin")) {
            int font = font_named(t, att(f, "typeface"));
            if (font >= 0) {
                p->font = font;
                p->has |= TP_FONT;
            }
        }
    }
}

static int spacing(const owf_xml_node *n, int *twips, int *pct)
{
    long long v;
    const owf_xml_node *c;
    if ((c = achild(n, "spcPts")) && number_att(c, "val", &v)) {
        *twips = clamp(v / 5, 0, 100000);
        *pct = -1;
        return 1;
    }
    if ((c = achild(n, "spcPct")) && number_att(c, "val", &v)) {
        *pct = clamp(v, 0, 10000000);
        *twips = 0;
        return 1;
    }
    return 0;
}

static void read_ppr(textctx *t, const owf_xml_node *n, tprops *p)
{
    long long v;
    const char *s;
    const owf_xml_node *c;
    unsigned long rgb;
    if (!n)
        return;
    if (number_att(n, "marL", &v)) {
        p->marl = clamp(emu_to_twips(v), 0, 100000);
        p->has |= TP_MARL;
    }
    if (number_att(n, "indent", &v)) {
        p->indent = clamp(emu_to_twips(v), -100000, 100000);
        p->has |= TP_INDENT;
    }
    if ((s = att(n, "algn"))) {
        p->align = same(s, "ctr") ? OWF_ALIGN_CENTRE : same(s, "r") ? OWF_ALIGN_RIGHT
                 : same(s, "just") || same(s, "dist") || same(s, "justLow") || same(s, "thaiDist") ? OWF_ALIGN_JUSTIFY
                 : OWF_ALIGN_LEFT;
        p->has |= TP_ALGN;
    }
    for (c = n->first; c; c = c->next) {
        if (c->type != OWF_XML_ELEMENT || !ns_a(c->ns))
            continue;
        if (!strcmp(c->name, "spcBef")) {
            if (spacing(c, &p->before, &p->before_pct))
                p->has |= TP_BEFORE;
        } else if (!strcmp(c->name, "spcAft")) {
            if (spacing(c, &p->after, &p->after_pct))
                p->has |= TP_AFTER;
        } else if (!strcmp(c->name, "lnSpc")) {
            const owf_xml_node *pc = achild(c, "spcPct");
            if (pc && number_att(pc, "val", &v)) {
                p->line = clamp(v / 1000, 50, 1000);
                p->has |= TP_LINE;
            } else if (achild(c, "spcPts")) {
                p->line = 100;               /* exact spacing: as single */
                p->has |= TP_LINE;
            }
        } else if (!strcmp(c->name, "buNone")) {
            p->bullet = BU_NONE;
            p->has |= TP_BULLET;
        } else if (!strcmp(c->name, "buChar")) {
            const char *ch = att(c, "char");
            if (ch && *ch) {
                const unsigned char *q = (const unsigned char *)ch;
                p->bu_char = owf_utf8_next(&q, q + strlen(ch));
                p->bullet = BU_CHAR;
                p->has |= TP_BULLET;
            }
        } else if (!strcmp(c->name, "buAutoNum")) {
            snprintf(p->bu_scheme, sizeof p->bu_scheme, "%s", att(c, "type") ? att(c, "type") : "arabicPeriod");
            p->bu_start = number_att(c, "startAt", &v) ? clamp(v, 1, 32767) : 1;
            p->bullet = BU_NUMBER;
            p->has |= TP_BULLET;
        } else if (!strcmp(c->name, "buFont")) {
            int font = font_named(t, att(c, "typeface"));
            if (font >= 0) {
                p->bu_font = font;
                p->has |= TP_BUFONT;
            }
        } else if (!strcmp(c->name, "buFontTx")) {
            p->has &= ~(unsigned long)TP_BUFONT;
        } else if (!strcmp(c->name, "buSzPct")) {
            if (number_att(c, "val", &v)) {
                p->bu_size = clamp(v / 1000, 25, 400);
                p->has |= TP_BUSIZE;
            }
        } else if (!strcmp(c->name, "buSzTx")) {
            p->has &= ~(unsigned long)TP_BUSIZE;
        } else if (!strcmp(c->name, "buClr")) {
            if (colour_of(&t->cc, c, &rgb)) {
                p->bu_colour = rgb;
                p->has |= TP_BUCOLOUR;
            }
        } else if (!strcmp(c->name, "buClrTx")) {
            p->has &= ~(unsigned long)TP_BUCOLOUR;
        } else if (!strcmp(c->name, "defRPr")) {
            read_rpr(t, c, p);
        }
    }
}

/* What src says, over dst. */
static void lay_over(tprops *dst, const tprops *src)
{
    unsigned long h = src->has;
    if (h & TP_MARL) dst->marl = src->marl;
    if (h & TP_INDENT) dst->indent = src->indent;
    if (h & TP_ALGN) dst->align = src->align;
    if (h & TP_BEFORE) { dst->before = src->before; dst->before_pct = src->before_pct; }
    if (h & TP_AFTER) { dst->after = src->after; dst->after_pct = src->after_pct; }
    if (h & TP_LINE) dst->line = src->line;
    if (h & TP_BULLET) {
        dst->bullet = src->bullet;
        dst->bu_char = src->bu_char;
        memcpy(dst->bu_scheme, src->bu_scheme, sizeof dst->bu_scheme);
        dst->bu_start = src->bu_start;
    }
    if (h & TP_BUFONT) dst->bu_font = src->bu_font;
    if (h & TP_BUSIZE) dst->bu_size = src->bu_size;
    if (h & TP_BUCOLOUR) dst->bu_colour = src->bu_colour;
    if (h & TP_SIZE) dst->size = src->size;
    if (h & TP_BOLD) dst->bold = src->bold;
    if (h & TP_ITALIC) dst->italic = src->italic;
    if (h & TP_UNDERLINE) dst->underline = src->underline;
    if (h & TP_STRIKE) dst->strike = src->strike;
    if (h & TP_BASELINE) dst->baseline = src->baseline;
    if (h & TP_COLOUR) dst->colour = src->colour;
    if (h & TP_FONT) dst->font = src->font;
    dst->has |= h;
}

/* A list style's level: a:lvl1pPr ... a:lvl9pPr (lvl from 0). */
static const owf_xml_node *level_node(const owf_xml_node *style, int lvl)
{
    char name[16];
    if (!style)
        return NULL;
    snprintf(name, sizeof name, "lvl%dpPr", lvl + 1);
    return achild(style, name);
}

/* The look a paragraph's level takes, down a chain of list styles (the
 * presentation's or master's, a master's placeholder's, a layout's, the
 * shape's), with the shape's style reference (its font colour and face)
 * laid over before the shape's own list style. */
#define CHAIN_MAX 6
typedef struct {
    const owf_xml_node *styles[CHAIN_MAX];
    int nstyles;
    int ref_at;                  /* where the shape's reference goes in: before styles[ref_at] */
    tprops ref;                  /* the reference's look (has 0: none) */
    tprops extra;                /* laid over last (a table cell's style), has 0: none */
} chain;

static void chain_level(textctx *t, const chain *ch, int lvl, tprops *out)
{
    int i;
    memset(out, 0, sizeof *out);
    out->size = 18 * OWF_TWIPS_PER_POINT;
    out->line = 100;
    out->bu_size = 100;
    out->font = t->th ? t->th->minor : -1;
    out->bu_font = -1;
    for (i = 0; i < ch->nstyles; i++) {
        tprops p;
        if (i == ch->ref_at)
            lay_over(out, &ch->ref);
        memset(&p, 0, sizeof p);
        read_ppr(t, level_node(ch->styles[i], lvl), &p);
        lay_over(out, &p);
    }
    if (ch->ref_at >= ch->nstyles)
        lay_over(out, &ch->ref);
    lay_over(out, &ch->extra);
}

/* tprops as the model's look of a level. */
static void to_level(textctx *t, const tprops *p, owf_textlevel *lv)
{
    owf_textlevel_init(lv);
    lv->chars.font = p->font >= 0 ? p->font : t->th ? t->th->minor : -1;
    lv->chars.size = p->size > 0 ? p->size : 18 * OWF_TWIPS_PER_POINT;
    lv->chars.flags = (p->bold ? OWF_BOLD : 0) | (p->italic ? OWF_ITALIC : 0) | (p->underline ? OWF_UNDERLINE : 0) |
                      (p->strike ? OWF_STRIKE : 0);
    lv->chars.colour = p->has & TP_COLOUR ? p->colour : 0x000000;
    lv->para.align = p->has & TP_ALGN ? p->align : OWF_ALIGN_LEFT;
    lv->para.indent_left = p->marl;
    lv->para.indent_first = p->indent;
    lv->para.space_before = p->before_pct >= 0 ? (int)((long long)lv->chars.size * p->before_pct / 100000) : p->before;
    lv->para.space_after = p->after_pct >= 0 ? (int)((long long)lv->chars.size * p->after_pct / 100000) : p->after;
    if (!(p->has & TP_BEFORE)) lv->para.space_before = 0;
    if (!(p->has & TP_AFTER)) lv->para.space_after = 0;
    lv->para.line_spacing = p->line > 0 ? p->line : 100;
    if (p->bullet == BU_CHAR) {
        lv->bullet = p->bu_char;
    } else if (p->bullet == BU_NUMBER) {
        const char *s = p->bu_scheme;
        char f = !strncmp(s, "alphaLc", 7) ? 'a' : !strncmp(s, "alphaUc", 7) ? 'A'
               : !strncmp(s, "romanLc", 7) ? 'i' : !strncmp(s, "romanUc", 7) ? 'I' : '1';
        lv->bullet = OWF_BULLET_NUMBER | (unsigned char)f;
        lv->start = p->bu_start > 0 ? p->bu_start : 1;
        if (strstr(s, "ParenBoth")) {
            strcpy(lv->prefix, "(");
            strcpy(lv->suffix, ")");
        } else if (strstr(s, "ParenR")) {
            strcpy(lv->suffix, ")");
        } else if (strstr(s, "Period")) {
            strcpy(lv->suffix, ".");
        } else if (strstr(s, "Minus")) {
            strcpy(lv->prefix, "- ");
            strcpy(lv->suffix, " -");
        }
    }
    if (lv->bullet) {
        lv->bullet_font = p->has & TP_BUFONT ? p->bu_font : -1;
        lv->bullet_size = p->has & TP_BUSIZE && p->bu_size != 100 ? p->bu_size : 0;
        lv->bullet_colour = p->has & TP_BUCOLOUR ? OWF_SHADE(p->bu_colour) : 0;
    }
}

/* The same look, as the model compares it. */
static int same_level(const owf_textlevel *a, const owf_textlevel *b)
{
    return a->bullet == b->bullet && a->bullet_font == b->bullet_font && a->bullet_size == b->bullet_size &&
           a->bullet_colour == b->bullet_colour && a->start == b->start && !strcmp(a->prefix, b->prefix) &&
           !strcmp(a->suffix, b->suffix) && a->para.indent_left == b->para.indent_left &&
           a->para.indent_first == b->para.indent_first && a->chars.size == b->chars.size &&
           a->chars.font == b->chars.font && a->chars.colour == b->chars.colour && a->chars.flags == b->chars.flags &&
           a->para.align == b->para.align;
}

/* ---- The reader ---- */

/* A master or layout part, read once. */
typedef struct {
    char name[NAME_SIZE];
    owf_xml *xml;
    rels rels;
    const owf_xml_node *root, *tree;     /* p:sldMaster or p:sldLayout, its p:spTree */
    int index;                           /* the deck's master or layout */
    int master;                          /* a layout's master: into pptx.masters */
    theme th;                            /* a master's */
    int map[4];                          /* a master's colour map */
} part;

typedef struct {
    int index;
} pic;

typedef struct {
    reader r;
    owf_deck *deck;
    const owf_xml_node *default_text;    /* the presentation's p:defaultTextStyle */
    part *masters;
    int nmasters;
    part *layouts;
    int nlayouts;
    pic *pics;
    int npics, cappics;
    char (*picnames)[NAME_SIZE];
} pptx;

/* Placeholder kinds as PresentationML names them, and where their text
 * looks start: 0 the master's title style, 1 its body style, 2 its other
 * style. */
static owf_placeholder ph_kind(const char *type, int *style)
{
    int st = 1;
    owf_placeholder k = OWF_PH_BODY;
    if (!type || same(type, "obj") || same(type, "body")) k = OWF_PH_BODY;
    else if (same(type, "title") || same(type, "ctrTitle")) { k = OWF_PH_TITLE; st = 0; }
    else if (same(type, "subTitle")) k = OWF_PH_SUBTITLE;
    else if (same(type, "dt")) { k = OWF_PH_DATE; st = 2; }
    else if (same(type, "ftr")) { k = OWF_PH_FOOTER; st = 2; }
    else if (same(type, "sldNum")) { k = OWF_PH_NUMBER; st = 2; }
    else if (same(type, "hdr")) { k = OWF_PH_HEADER; st = 2; }
    else if (same(type, "pic")) k = OWF_PH_PICTURE;
    else if (same(type, "tbl")) k = OWF_PH_TABLE;
    else if (same(type, "chart")) k = OWF_PH_CHART;
    else if (same(type, "clipArt") || same(type, "media") || same(type, "dgm")) k = OWF_PH_OBJECT;
    else if (same(type, "sldImg")) { k = OWF_PH_SLIDE_IMAGE; st = 2; }
    if (style)
        *style = st;
    return k;
}

/* The placeholder a shape is, from its p:nvSpPr (p:nvPicPr...): its
 * p:ph, or NULL. */
static const owf_xml_node *ph_of(const owf_xml_node *shape)
{
    const owf_xml_node *c;
    for (c = shape ? shape->first : NULL; c; c = c->next)
        if (c->type == OWF_XML_ELEMENT && ns_p(c->ns) && strlen(c->name) > 2 && !strcmp(c->name + strlen(c->name) - 2, "Pr") &&
            !strncmp(c->name, "nv", 2))
            return pchild(pchild(c, "nvPr"), "ph");
    return NULL;
}

/* The master's or layout's placeholder a slide's matches: by its idx, else
 * by its kind (a master's by kind alone: ctrTitle is its title, subTitle and
 * content its body). */
static const owf_xml_node *find_ph(const owf_xml_node *tree, const owf_xml_node *ph, int master)
{
    const owf_xml_node *c, *by_type = NULL;
    const char *type = att(ph, "type"), *idx = att(ph, "idx");
    int style;
    owf_placeholder kind = ph_kind(type, &style);
    if (!tree)
        return NULL;
    for (c = tree->first; c; c = c->next) {
        const owf_xml_node *p = ph_of(c);
        const char *t2;
        if (!p)
            continue;
        t2 = att(p, "type");
        if (!master && idx && same(att(p, "idx"), idx) && !same(idx, "0"))
            return c;
        if (master) {
            int s2;
            owf_placeholder k2 = ph_kind(t2, &s2);
            if (k2 == kind || (style == 1 && s2 == 1 && (k2 == OWF_PH_BODY || kind == OWF_PH_BODY)) ||
                (kind == OWF_PH_SUBTITLE && k2 == OWF_PH_BODY)) {
                if (k2 == kind || !by_type)
                    by_type = c;
                if (k2 == kind)
                    break;
            }
        } else if (!by_type && (same(t2, type) || (!type && !t2))) {
            by_type = c;
        }
    }
    return by_type;
}

/* A shape's p:spPr (p:grpSpPr), and its box, if it has one. */
static const owf_xml_node *sp_pr(const owf_xml_node *shape)
{
    const owf_xml_node *c = pchild(shape, "spPr");
    return c ? c : pchild(shape, "grpSpPr");
}

static int xfrm_box(const owf_xml_node *xfrm, long long *x, long long *y, long long *w, long long *h)
{
    const owf_xml_node *off = achild(xfrm, "off"), *ext = achild(xfrm, "ext");
    if (!off || !ext)
        return 0;
    *x = number(att(off, "x"), 0);
    *y = number(att(off, "y"), 0);
    *w = number(att(ext, "cx"), 0);
    *h = number(att(ext, "cy"), 0);
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
    return 1;
}

/* A text body's a:bodyPr settings, as they are inherited: insets, anchor,
 * fitting. */
typedef struct {
    int has_inset[4], inset[4];  /* left, top, right, bottom: twips */
    int has_anchor, anchor;      /* OWF_VALIGN_... */
    int has_fit, fit;            /* OWF_AUTOFIT_... */
    int vertical;
} body_look;

static void read_body(const owf_xml_node *bp, body_look *b)
{
    static const char *const ins[4] = { "lIns", "tIns", "rIns", "bIns" };
    long long v;
    const char *s;
    int i;
    if (!bp)
        return;
    for (i = 0; i < 4; i++)
        if (number_att(bp, ins[i], &v)) {
            b->inset[i] = clamp(emu_to_twips(v), 0, 20000);
            b->has_inset[i] = 1;
        }
    if ((s = att(bp, "anchor"))) {
        b->anchor = same(s, "ctr") ? OWF_VALIGN_CENTRE : same(s, "b") ? OWF_VALIGN_BOTTOM : OWF_VALIGN_TOP;
        b->has_anchor = 1;
    }
    if (achild(bp, "normAutofit")) { b->fit = OWF_AUTOFIT_SHRINK; b->has_fit = 1; }
    else if (achild(bp, "spAutoFit")) { b->fit = OWF_AUTOFIT_GROW; b->has_fit = 1; }
    else if (achild(bp, "noAutofit")) { b->fit = OWF_AUTOFIT_NONE; b->has_fit = 1; }
    if ((s = att(bp, "vert")) && !same(s, "horz"))
        b->vertical = 1;
}

static const owf_xml_node *body_of(const owf_xml_node *shape)
{
    return achild(pchild(shape, "txBody"), "bodyPr");
}

static const owf_xml_node *lst_of(const owf_xml_node *shape)
{
    return achild(pchild(shape, "txBody"), "lstStyle");
}

static void apply_body(owf_obj *obj, const body_look *b)
{
    int i;
    /* DrawingML's defaults: 0.1 inch left and right, 0.05 inch top and bottom */
    static const int defaults[4] = { 144, 72, 144, 72 };
    for (i = 0; i < 4; i++)
        obj->inset[i] = b->has_inset[i] ? b->inset[i] : defaults[i];
    obj->valign = b->has_anchor ? (unsigned char)b->anchor : OWF_VALIGN_TOP;
    obj->autofit = b->has_fit ? (unsigned char)b->fit : OWF_AUTOFIT_NONE;
}

/* The text style a placeholder's text starts from: the master's title,
 * body or other style; the presentation's default for other text. */
static const owf_xml_node *base_style(const pptx *x, const part *master, int style)
{
    const owf_xml_node *ts = master ? pchild(master->root, "txStyles") : NULL;
    const owf_xml_node *s = NULL;
    if (style == 0) s = pchild(ts, "titleStyle");
    else if (style == 1) s = pchild(ts, "bodyStyle");
    else if (style == 2) s = pchild(ts, "otherStyle");
    return s ? s : x->default_text;
}

/* ---- Pictures ---- */

static const char *sniff_mime(const unsigned char *d, size_t n, const char *name)
{
    const char *e = name ? strrchr(name, '.') : NULL;
    if (n >= 8 && !memcmp(d, "\211PNG", 4)) return "image/png";
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return "image/jpeg";
    if (n >= 6 && !memcmp(d, "GIF8", 4)) return "image/gif";
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return "image/bmp";
    if (n >= 4 && d[0] == 0xD7 && d[1] == 0xCD && d[2] == 0xC6 && d[3] == 0x9A) return "image/x-wmf";
    if (n >= 44 && !memcmp(d + 40, " EMF", 4)) return "image/x-emf";
    if (n >= 4 && (!memcmp(d, "II*", 3) || !memcmp(d, "MM\0*", 4))) return "image/tiff";
    if (e && (!strcmp(e, ".svg") || !strcmp(e, ".SVG"))) return "image/svg+xml";
    return "application/octet-stream";
}

/* A picture a relationship points to, into res: its index, or -1. The
 * same part twice is one picture. */
static int load_picture(pptx *x, const rels *r, const char *id, int w, int h, const char *alt)
{
    const rel *rl = rel_id(r, id);
    char name[NAME_SIZE];
    unsigned char *data;
    size_t length;
    int i, index;
    const char *mime;

    if (!rl)
        return -1;
    if (rl->external) {
        owf_report_add(x->r.report, OWF_NOTE_LOST, "A linked picture was left out");
        return -1;
    }
    if (!rel_part(r, rl, name, sizeof name))
        return -1;
    for (i = 0; i < x->npics; i++)
        if (!strcmp(x->picnames[i], name))
            return x->pics[i].index;
    if (owf_unzip_read(x->r.zip, name, &data, &length) != OWF_OK) {
        owf_report_add(x->r.report, OWF_NOTE_LOST, "A picture that is not in the file was left out");
        return -1;
    }
    mime = sniff_mime(data, length, name);
    if (strcmp(mime, "image/png") && strcmp(mime, "image/jpeg"))
        owf_report_add(x->r.report, OWF_NOTE_APPROX, "Pictures other than PNG and JPEG are kept, and drawn through datatypes only");
    index = owf_doc_add_image(x->deck->res, name, mime, data, length, w, h, alt);
    free(data);
    if (index < 0) {
        x->r.failed = 1;
        return -1;
    }
    if (x->npics == x->cappics) {
        int want = x->cappics ? x->cappics * 2 : 16;
        pic *p = realloc(x->pics, (size_t)want * sizeof *p);
        char (*n)[NAME_SIZE] = realloc(x->picnames, (size_t)want * sizeof *n);
        if (p) x->pics = p;
        if (n) x->picnames = n;
        if (!p || !n) {
            x->r.failed = 1;
            return index;
        }
        x->cappics = want;
    }
    x->pics[x->npics].index = index;
    snprintf(x->picnames[x->npics], NAME_SIZE, "%s", name);
    x->npics++;
    return index;
}

/* ---- Text ---- */

typedef struct {
    pptx *x;
    textctx t;
    const rels *rels;            /* the part's, for links */
    chain ch;
    const owf_master *m;         /* the slide's master in the deck */
    int body_levels;             /* a body placeholder: its levels are the master's body levels */
    int listable;                /* bulleted paragraphs become list paragraphs */
} textjob;

/* Text with its tabs as tab runs; a link's text as link text. */
static void add_text(textjob *j, owf_para *p, const owf_charfmt *cf, const char *s, size_t n, const char *href)
{
    size_t start = 0, i;
    for (i = 0; i <= n; i++) {
        if (i < n && s[i] != '\t')
            continue;
        if (i > start) {
            int ok = href ? owf_para_add_link_text(p, cf, s + start, i - start, href)
                          : owf_para_add_text(p, cf, s + start, i - start);
            if (ok < 0)
                j->x->r.failed = 1;
        }
        if (i < n && owf_para_add_special(p, cf, OWF_RUN_TAB, OWF_FIELD_PAGE) < 0)
            j->x->r.failed = 1;
        start = i + 1;
    }
}

static void to_charfmt(const tprops *r, owf_charfmt *cf, const owf_textlevel *lv)
{
    owf_charfmt_init(cf);
    cf->font = r->font;
    cf->size = r->size > 0 ? r->size : 18 * OWF_TWIPS_PER_POINT;
    cf->flags = (r->bold ? OWF_BOLD : 0) | (r->italic ? OWF_ITALIC : 0) | (r->underline ? OWF_UNDERLINE : 0) |
                (r->strike ? OWF_STRIKE : 0) | (r->baseline > 0 ? OWF_SUPER : r->baseline < 0 ? OWF_SUB : 0);
    cf->colour = r->has & TP_COLOUR ? r->colour : 0x000000;
    /* only what differs from the level's look */
    if (lv) {
        if (cf->font == lv->chars.font) cf->font = -1;
        if (cf->size == lv->chars.size) cf->size = 0;
        if (cf->colour == lv->chars.colour) cf->colour = OWF_COLOUR_AUTO;
    }
}

/* One run, field or line break of a paragraph. */
static void read_run(textjob *j, owf_para *p, const owf_xml_node *c, const tprops *para, const owf_textlevel *lv)
{
    tprops r = *para;
    owf_charfmt cf;
    owf_buf text;
    const owf_xml_node *rpr = achild(c, "rPr"), *t;
    const char *href = NULL;

    read_rpr(&j->t, rpr, &r);
    to_charfmt(&r, &cf, lv);
    if (isa(c, "br")) {
        if (owf_para_add_special(p, &cf, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE) < 0)
            j->x->r.failed = 1;
        return;
    }
    if (isa(c, "fld")) {
        const char *type = att(c, "type");
        if (same(type, "slidenum") || (type && !strncmp(type, "datetime", 8))) {
            if (owf_para_add_special(p, &cf, OWF_RUN_FIELD, same(type, "slidenum") ? OWF_FIELD_PAGE : OWF_FIELD_DATE) < 0)
                j->x->r.failed = 1;
            return;
        }
    }
    if (rpr && j->rels) {
        const rel *rl = rel_id(j->rels, ratt(achild(rpr, "hlinkClick"), "id"));
        if (rl && rl->external)
            href = rl->target;
    }
    owf_buf_init(&text);
    for (t = c->first; t; t = t->next)
        if (isa(t, "t"))
            text_of(t, &text, 0);
    if (text.failed)
        j->x->r.failed = 1;
    else if (text.length)
        add_text(j, p, &cf, (const char *)text.data, text.length, href);
    owf_buf_free(&text);
}

/* The look a list paragraph at lvl takes: the object's own list when its
 * look isn't the master's level. */
static void list_level(textjob *j, owf_obj *obj, int lvl, const owf_textlevel *tl)
{
    int i;
    if (!obj->list && j->body_levels && j->m && same_level(&j->m->body[lvl], tl))
        return;
    if (!obj->list) {
        obj->list = malloc(OWF_TEXT_LEVELS * sizeof *obj->list);
        if (!obj->list) {
            j->x->r.failed = 1;
            return;
        }
        for (i = 0; i < OWF_TEXT_LEVELS; i++)
            obj->list[i] = j->m ? j->m->body[i] : *tl;
    }
    obj->list[lvl] = *tl;
}

/* A text body's paragraphs, into story (an object's text: obj; notes and
 * table cells: obj for the level look only, not listable). */
static void read_paras(textjob *j, const owf_xml_node *txBody, owf_obj *obj, owf_story *story)
{
    const owf_xml_node *p, *c;
    for (p = txBody ? txBody->first : NULL; p && !j->x->r.failed; p = p->next) {
        const owf_xml_node *ppr;
        tprops L, own;
        owf_textlevel tl;
        const owf_textlevel *lv;
        owf_para *para;
        long long v;
        int lvl = 0, level = 0, index;

        if (!isa(p, "p"))
            continue;
        if (story->nparas >= 100000)
            break;
        ppr = achild(p, "pPr");
        if (ppr && number_att(ppr, "lvl", &v))
            lvl = clamp(v, 0, OWF_TEXT_LEVELS - 1);
        chain_level(&j->t, &j->ch, lvl, &L);
        memset(&own, 0, sizeof own);
        read_ppr(&j->t, ppr, &own);
        lay_over(&L, &own);
        to_level(&j->t, &L, &tl);
        para = owf_story_add(story, &tl.para);
        if (!para) {
            j->x->r.failed = 1;
            return;
        }
        index = story->nparas - 1;
        if (obj && j->listable && tl.bullet) {
            level = lvl + 1;
            list_level(j, obj, lvl, &tl);
            if (owf_obj_set_level(obj, index, level) < 0)
                j->x->r.failed = 1;
        }
        if (obj && j->listable)
            lv = owf_deck_para_level(j->x->deck, j->m, obj, index);
        else if (!obj && j->m)
            lv = &j->m->notes;       /* notes */
        else
            lv = NULL;               /* table cells: every run says its whole look */
        for (c = p->first; c && !j->x->r.failed; c = c->next)
            if (isa(c, "r") || isa(c, "br") || isa(c, "fld"))
                read_run(j, &story->paras[index], c, &L, lv);
    }
}

/* 1 when a text body holds any text. */
static int has_text(const owf_xml_node *txBody)
{
    const owf_xml_node *p, *r, *t;
    for (p = txBody ? txBody->first : NULL; p; p = p->next)
        for (r = isa(p, "p") ? p->first : NULL; r; r = r->next)
            for (t = isa(r, "r") || isa(r, "fld") ? r->first : NULL; t; t = t->next)
                if (isa(t, "t") && t->first)
                    return 1;
    return 0;
}

/* ---- Objects ---- */

/* A group's frame: its children's coordinates (chOff, chExt) onto its box
 * (off, ext). */
typedef struct {
    long long ox, oy, cx, cy, chx, chy, chcx, chcy;
} xform;

typedef struct {
    pptx *x;
    const rels *rels;
    const owf_xml_node *layout_tree, *master_tree;
    const part *master;          /* the master part */
    int deck_master;             /* its index in the deck */
    colours cc;
    owf_obj **objs;
    int *n, *cap;
    int slide;                   /* the deck's slide, -1 on a master */
    xform t[OWF_DECK_MAX_DEPTH + 1];
    int nt;
} objctx;

static const owf_master *ctx_master(const objctx *c)
{
    return &c->x->deck->masters[c->deck_master];
}

static void map_box(const objctx *c, long long *x, long long *y, long long *w, long long *h)
{
    int i;
    for (i = c->nt - 1; i >= 0; i--) {
        const xform *t = &c->t[i];
        if (t->chcx > 0) {
            *x = t->ox + (*x - t->chx) * t->cx / t->chcx;
            *w = *w * t->cx / t->chcx;
        }
        if (t->chcy > 0) {
            *y = t->oy + (*y - t->chy) * t->cy / t->chcy;
            *h = *h * t->cy / t->chcy;
        }
        if (*x < -(1LL << 30)) *x = -(1LL << 30);
        if (*x > (1LL << 30)) *x = 1LL << 30;
        if (*y < -(1LL << 30)) *y = -(1LL << 30);
        if (*y > (1LL << 30)) *y = 1LL << 30;
        if (*w > (1LL << 30)) *w = 1LL << 30;
        if (*h > (1LL << 30)) *h = 1LL << 30;
    }
}

static owf_obj *new_obj(objctx *c, owf_obj_kind kind, int parent)
{
    owf_obj *obj;
    if (*c->n >= OWF_DECK_MAX_OBJS) {
        owf_report_add(c->x->r.report, OWF_NOTE_LOST, "Objects beyond %d on a slide were left out", OWF_DECK_MAX_OBJS);
        return NULL;
    }
    if (*c->n == *c->cap) {
        int want = *c->cap ? *c->cap * 2 : 8;
        owf_obj *p = realloc(*c->objs, (size_t)want * sizeof *p);
        if (!p) {
            c->x->r.failed = 1;
            return NULL;
        }
        *c->objs = p;
        *c->cap = want;
    }
    obj = &(*c->objs)[(*c->n)++];
    owf_obj_init(obj, kind);
    obj->parent = parent;
    return obj;
}

/* The box, rotation and flips of a shape (from the layout's or master's
 * placeholder when it has none of its own), group frames applied. */
static void read_box(objctx *c, owf_obj *obj, const owf_xml_node *xfrm, const owf_xml_node *lph,
                     const owf_xml_node *mph)
{
    long long x = 0, y = 0, w = 0, h = 0, rot;
    int has = xfrm_box(xfrm, &x, &y, &w, &h);
    if (!has && lph)
        has = xfrm_box(achild(sp_pr(lph), "xfrm"), &x, &y, &w, &h);
    if (!has && mph)
        has = xfrm_box(achild(sp_pr(mph), "xfrm"), &x, &y, &w, &h);
    map_box(c, &x, &y, &w, &h);
    obj->x = emu_to_twips(x);
    obj->y = emu_to_twips(y);
    obj->width = clamp(emu_to_twips(w), 0, MAX_COORD);
    obj->height = clamp(emu_to_twips(h), 0, MAX_COORD);
    if (xfrm && number_att(xfrm, "rot", &rot) && rot % 21600000) {
        /* sixty-thousandths of a degree clockwise; the model's tenths anticlockwise */
        long long tenths = (rot / 6000) % 3600;
        obj->rotation = (int)((3600 - tenths) % 3600);
        if (obj->rotation)
            owf_report_add(c->x->r.report, OWF_NOTE_APPROX, "Rotated objects are drawn unrotated (their rotation is kept)");
    }
    if (xfrm && (same(att(xfrm, "flipH"), "1") || same(att(xfrm, "flipH"), "true")))
        obj->flags |= OWF_OBJ_FLIP_H;
    if (xfrm && (same(att(xfrm, "flipV"), "1") || same(att(xfrm, "flipV"), "true")))
        obj->flags |= OWF_OBJ_FLIP_V;
}

static void read_ids(objctx *c, owf_obj *obj, const owf_xml_node *shape)
{
    const owf_xml_node *nv, *cnv = NULL;
    long long id;
    for (nv = shape->first; nv && !cnv; nv = nv->next)
        if (nv->type == OWF_XML_ELEMENT && ns_p(nv->ns) && !strncmp(nv->name, "nv", 2))
            cnv = pchild(nv, "cNvPr");
    if (!cnv)
        return;
    if (number_att(cnv, "id", &id) && id > 0 && id < 0x7FFFFFFFLL)
        obj->id = (long)id;
    if (att(cnv, "name") && *att(cnv, "name") && owf_deck_set_text(&obj->name, att(cnv, "name")) < 0)
        c->x->r.failed = 1;
    if (att(cnv, "descr") && *att(cnv, "descr") && owf_deck_set_text(&obj->alt, att(cnv, "descr")) < 0)
        c->x->r.failed = 1;
    if (same(att(cnv, "hidden"), "1") || same(att(cnv, "hidden"), "true"))
        obj->flags |= OWF_OBJ_HIDDEN;
}

/* The fill and line of a shape: its own, else its placeholders', else its
 * style's references to the theme. */
static void read_look(objctx *c, owf_obj *obj, const owf_xml_node *shape, const owf_xml_node *lph,
                      const owf_xml_node *mph, int line_only)
{
    const owf_xml_node *spPr = sp_pr(shape), *style = pchild(shape, "style"), *ln = NULL;
    const owf_xml_node *from[3];
    unsigned long rgb, fill = 0;
    long long v;
    int i, has_fill = 0, has_line = 0, has_width = 0;

    from[0] = spPr;
    from[1] = lph ? sp_pr(lph) : NULL;
    from[2] = mph ? sp_pr(mph) : NULL;
    for (i = 0; i < 3 && !has_fill && !line_only; i++)
        if (from[i])
            has_fill = fill_of(&c->x->r, &c->cc, from[i], &fill);
    if (!has_fill && !line_only && style) {
        const owf_xml_node *fr = achild(style, "fillRef");
        if (fr && number(att(fr, "idx"), 0) > 0 && colour_of(&c->cc, fr, &rgb)) {
            fill = OWF_SHADE(rgb);
            if (number(att(fr, "idx"), 0) > 1)
                owf_report_add(c->x->r.report, OWF_NOTE_APPROX, "Gradient and pattern fills are drawn in one colour");
        }
    }
    obj->look.fill = line_only ? 0 : fill;
    for (i = 0; i < 3 && !ln; i++)
        ln = achild(from[i], "ln");
    if (ln) {
        const owf_xml_node *d = achild(ln, "prstDash");
        if (achild(ln, "noFill")) {
            has_line = 1;
            obj->look.line = 0;
        } else if (achild(ln, "solidFill") && colour_of(&c->cc, achild(ln, "solidFill"), &rgb)) {
            has_line = 1;
            obj->look.line = OWF_SHADE(rgb);
        }
        if (number_att(ln, "w", &v)) {
            obj->look.line_width = clamp(v / EMU_PER_TWIP, 1, 2000);
            has_width = 1;
        }
        obj->look.line_style = d && !same(att(d, "val"), "solid") ? OWF_LINE_DASHED : OWF_LINE_THIN;
        if (achild(ln, "headEnd") && !same(att(achild(ln, "headEnd"), "type"), "none"))
            obj->look.arrows |= OWF_ARROW_START;
        if (achild(ln, "tailEnd") && !same(att(achild(ln, "tailEnd"), "type"), "none"))
            obj->look.arrows |= OWF_ARROW_END;
    }
    if (!has_line && style) {
        const owf_xml_node *lr = achild(style, "lnRef");
        long long idx = number(att(lr, "idx"), 0);
        if (lr && idx > 0 && colour_of(&c->cc, lr, &rgb)) {
            obj->look.line = OWF_SHADE(rgb);
            if (!has_width && c->master)
                obj->look.line_width = c->master->th.line_width[idx > 3 ? 2 : idx - 1];
            obj->look.line_style = OWF_LINE_THIN;
        }
    }
}

/* A preset geometry as the model's shape: 1, or 0 for one it doesn't have
 * (drawn as a rectangle). */
static int preset_shape(owf_obj *obj, const char *prst)
{
    int w = obj->width, h = obj->height, shorter = w < h ? w : h;
    static const struct { const char *name; owf_shape shape; } map[] = {
        { "rect", OWF_SHAPE_RECT }, { "roundRect", OWF_SHAPE_ROUNDRECT }, { "ellipse", OWF_SHAPE_ELLIPSE },
        { "triangle", OWF_SHAPE_TRIANGLE }, { "diamond", OWF_SHAPE_DIAMOND }, { "rightArrow", OWF_SHAPE_ARROW },
        { "leftArrow", OWF_SHAPE_ARROW }, { "line", OWF_SHAPE_LINE }, { "straightConnector1", OWF_SHAPE_LINE },
    };
    size_t i;
    if (!prst)
        return 0;
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!strcmp(prst, map[i].name)) {
            obj->shape = map[i].shape;
            if (!strcmp(prst, "leftArrow"))
                obj->flags ^= OWF_OBJ_FLIP_H;
            if (obj->shape == OWF_SHAPE_ROUNDRECT)
                obj->radius = (int)((long long)shorter * 16667 / 100000);
            return 1;
        }
    /* a few more as polygons: corners in the box */
    {
        int pts[16], n = 0, k;
        if (!strcmp(prst, "rtTriangle")) {
            int p[] = { 0, 0, 0, h, w, h };
            memcpy(pts, p, sizeof p);
            n = 3;
        } else if (!strcmp(prst, "parallelogram")) {
            int a = shorter / 4, p[] = { a, 0, w, 0, w - a, h, 0, h };
            memcpy(pts, p, sizeof p);
            n = 4;
        } else if (!strcmp(prst, "trapezoid")) {
            int a = shorter / 4, p[] = { a, 0, w - a, 0, w, h, 0, h };
            memcpy(pts, p, sizeof p);
            n = 4;
        } else if (!strcmp(prst, "hexagon")) {
            int a = shorter / 4, p[] = { a, 0, w - a, 0, w, h / 2, w - a, h, a, h, 0, h / 2 };
            memcpy(pts, p, sizeof p);
            n = 6;
        } else if (!strcmp(prst, "octagon")) {
            int a = (int)((long long)shorter * 29289 / 100000);
            int p[] = { a, 0, w - a, 0, w, a, w, h - a, w - a, h, a, h, 0, h - a, 0, a };
            memcpy(pts, p, sizeof p);
            n = 8;
        } else if (!strcmp(prst, "pentagon") || !strcmp(prst, "homePlate")) {
            int p1[] = { w / 2, 0, w, h * 38 / 100, w * 81 / 100, h, w * 19 / 100, h, 0, h * 38 / 100 };
            int p2[] = { 0, 0, w - h / 2, 0, w, h / 2, w - h / 2, h, 0, h };
            memcpy(pts, !strcmp(prst, "pentagon") ? p1 : p2, sizeof p1);
            n = 5;
        }
        if (!n)
            return 0;
        obj->points = malloc((size_t)n * 2 * sizeof *obj->points);
        if (!obj->points)
            return 0;
        for (k = 0; k < n * 2; k++)
            obj->points[k] = pts[k];
        obj->npoints = n;
        obj->shape = OWF_SHAPE_POLYGON;
        return 1;
    }
}

/* The text job for a shape's text: its chain of list styles and its
 * style reference's font. */
static void text_job(objctx *c, textjob *j, const owf_xml_node *shape, const owf_xml_node *ph,
                     const owf_xml_node *lph, const owf_xml_node *mph)
{
    const owf_xml_node *style = pchild(shape, "style");
    memset(j, 0, sizeof *j);
    j->x = c->x;
    j->t.x = &c->x->r;
    j->t.deck = c->x->deck;
    j->t.th = c->master ? &c->master->th : NULL;
    j->t.cc = c->cc;
    j->rels = c->rels;
    j->m = ctx_master(c);
    j->listable = 1;
    if (ph) {
        int st;
        ph_kind(att(ph, "type"), &st);
        j->ch.styles[j->ch.nstyles++] = base_style(c->x, c->master, st);
        if (mph) j->ch.styles[j->ch.nstyles++] = lst_of(mph);
        if (lph) j->ch.styles[j->ch.nstyles++] = lst_of(lph);
    } else {
        j->ch.styles[j->ch.nstyles++] = c->x->default_text;
    }
    j->ch.ref_at = j->ch.nstyles;
    j->ch.styles[j->ch.nstyles++] = lst_of(shape);
    if (style) {
        const owf_xml_node *fr = achild(style, "fontRef");
        unsigned long rgb;
        if (fr && colour_of(&c->cc, fr, &rgb)) {
            j->ch.ref.colour = rgb;
            j->ch.ref.has |= TP_COLOUR;
        }
        if (fr && j->t.th && (same(att(fr, "idx"), "major") || same(att(fr, "idx"), "minor"))) {
            j->ch.ref.font = same(att(fr, "idx"), "major") ? j->t.th->major : j->t.th->minor;
            j->ch.ref.has |= TP_FONT;
        }
    }
}

static void read_shape_text(objctx *c, int index, const owf_xml_node *shape, const owf_xml_node *ph,
                            const owf_xml_node *lph, const owf_xml_node *mph)
{
    textjob j;
    body_look b;
    owf_obj *obj = &(*c->objs)[index];
    memset(&b, 0, sizeof b);
    if (mph) read_body(body_of(mph), &b);
    if (lph) read_body(body_of(lph), &b);
    read_body(body_of(shape), &b);
    apply_body(obj, &b);
    if (b.vertical)
        owf_report_add(c->x->r.report, OWF_NOTE_APPROX, "Vertical text is drawn across");
    if (!pchild(shape, "txBody"))
        return;
    text_job(c, &j, shape, ph, lph, mph);
    obj = &(*c->objs)[index];
    j.body_levels = obj->placeholder == OWF_PH_BODY;
    read_paras(&j, pchild(shape, "txBody"), obj, &obj->text);
}

/* A footer, date or slide number placeholder on a slide: what the slide
 * shows in its master's. */
static void slide_field(objctx *c, owf_placeholder kind, const owf_xml_node *shape)
{
    owf_slide *s = &c->x->deck->slides[c->slide];
    const owf_xml_node *body = pchild(shape, "txBody"), *p, *r;
    owf_buf text;
    int field = 0;
    owf_buf_init(&text);
    for (p = body ? body->first : NULL; p; p = p->next)
        for (r = isa(p, "p") ? p->first : NULL; r; r = r->next) {
            if (isa(r, "fld"))
                field = 1;
            else if (isa(r, "r"))
                text_of(achild(r, "t"), &text, 0);
        }
    owf_buf_putc(&text, 0);
    if (text.failed) {
        c->x->r.failed = 1;
        owf_buf_free(&text);
        return;
    }
    if (kind == OWF_PH_FOOTER && text.data[0]) {
        s->show |= OWF_SHOW_FOOTER;
        if (owf_deck_set_text(&s->footer, (const char *)text.data) < 0)
            c->x->r.failed = 1;
    } else if (kind == OWF_PH_NUMBER) {
        s->show |= OWF_SHOW_NUMBER;
    } else if (kind == OWF_PH_DATE) {
        s->show |= OWF_SHOW_DATE;
        if (!field && text.data[0] && owf_deck_set_text(&s->date_text, (const char *)text.data) < 0)
            c->x->r.failed = 1;
    }
    owf_buf_free(&text);
}

static void read_tree(objctx *c, const owf_xml_node *tree, int parent, int depth);

static void read_sp(objctx *c, const owf_xml_node *n, int parent)
{
    const owf_xml_node *ph = ph_of(n), *lph = NULL, *mph = NULL, *spPr = pchild(n, "spPr");
    const owf_xml_node *geom = achild(spPr, "prstGeom"), *nv = pchild(n, "nvSpPr");
    owf_placeholder kind = OWF_PH_NONE;
    owf_obj *obj;
    int index, text_box = same(att(pchild(nv, "cNvSpPr"), "txBox"), "1");

    if (ph) {
        kind = ph_kind(att(ph, "type"), NULL);
        if (c->slide >= 0) {
            lph = find_ph(c->layout_tree, ph, 0);
            mph = find_ph(c->master_tree, lph && ph_of(lph) ? ph_of(lph) : ph, 1);
            if (kind == OWF_PH_DATE || kind == OWF_PH_FOOTER || kind == OWF_PH_NUMBER || kind == OWF_PH_HEADER) {
                slide_field(c, kind, n);
                return;
            }
        }
    }
    obj = new_obj(c, ph || text_box ? OWF_OBJ_TEXT : OWF_OBJ_SHAPE, parent);
    if (!obj)
        return;
    index = *c->n - 1;
    obj->placeholder = kind;
    if (ph) {
        int i, k = 0;
        for (i = 0; i < index; i++)
            if ((*c->objs)[i].placeholder == kind)
                k++;
        obj->ph_index = k;
        if (c->slide < 0 || !has_text(pchild(n, "txBody")))
            if (kind == OWF_PH_TITLE || kind == OWF_PH_SUBTITLE || kind == OWF_PH_BODY || kind == OWF_PH_OBJECT ||
                kind == OWF_PH_PICTURE || kind == OWF_PH_TABLE || kind == OWF_PH_CHART)
                obj->flags |= OWF_OBJ_EMPTY;
    }
    read_ids(c, obj, n);
    read_box(c, obj, achild(spPr, "xfrm"), lph, mph);
    if (obj->kind == OWF_OBJ_SHAPE) {
        const char *prst = att(geom, "prst");
        if (!geom || !preset_shape(obj, prst)) {
            obj->shape = OWF_SHAPE_RECT;
            if (achild(spPr, "custGeom"))
                owf_report_add(c->x->r.report, OWF_NOTE_APPROX, "Freeform shapes are drawn as rectangles");
            else if (prst)
                owf_report_add(c->x->r.report, OWF_NOTE_APPROX,
                               "Shapes other than rectangles, ellipses, triangles, diamonds, arrows and simple polygons are drawn as rectangles");
        }
    }
    read_look(c, obj, n, lph, mph, obj->kind == OWF_OBJ_SHAPE && obj->shape == OWF_SHAPE_LINE);
    obj = &(*c->objs)[index];
    if (obj->kind == OWF_OBJ_SHAPE && obj->shape == OWF_SHAPE_LINE)
        obj->look.fill = 0;
    read_shape_text(c, index, n, ph, lph, mph);
}

static void read_pic(objctx *c, const owf_xml_node *n, int parent)
{
    const owf_xml_node *ph = ph_of(n), *lph = NULL, *mph = NULL, *blipFill = pchild(n, "blipFill");
    const owf_xml_node *blip = achild(blipFill, "blip"), *src = achild(blipFill, "srcRect");
    owf_obj *obj;
    int index;
    if (!blipFill)
        blipFill = achild(n, "blipFill"), blip = achild(blipFill, "blip"), src = achild(blipFill, "srcRect");
    if (ph && c->slide >= 0) {
        lph = find_ph(c->layout_tree, ph, 0);
        mph = find_ph(c->master_tree, lph && ph_of(lph) ? ph_of(lph) : ph, 1);
    }
    obj = new_obj(c, OWF_OBJ_PICTURE, parent);
    if (!obj)
        return;
    index = *c->n - 1;
    if (ph)
        obj->placeholder = OWF_PH_PICTURE;
    read_ids(c, obj, n);
    read_box(c, obj, achild(pchild(n, "spPr"), "xfrm"), lph, mph);
    read_look(c, obj, n, NULL, NULL, 1);
    obj = &(*c->objs)[index];
    obj->look.fill = 0;
    if (blip) {
        int image = load_picture(c->x, c->rels, ratt(blip, "embed"), obj->width, obj->height, obj->alt);
        obj = &(*c->objs)[index];
        obj->image = image;
        if (!ratt(blip, "embed") && ratt(blip, "link"))
            owf_report_add(c->x->r.report, OWF_NOTE_LOST, "A linked picture was left out");
    }
    if (src) {
        static const char *const sides[4] = { "l", "t", "r", "b" };
        int k;
        long long v;
        for (k = 0; k < 4; k++)
            if (number_att(src, sides[k], &v))
                obj->crop[k] = clamp(v / 100, 0, 999);
    }
}

static void read_cxn(objctx *c, const owf_xml_node *n, int parent)
{
    owf_obj *obj = new_obj(c, OWF_OBJ_SHAPE, parent);
    int index;
    if (!obj)
        return;
    index = *c->n - 1;
    obj->shape = OWF_SHAPE_LINE;
    read_ids(c, obj, n);
    read_box(c, obj, achild(pchild(n, "spPr"), "xfrm"), NULL, NULL);
    read_look(c, obj, n, NULL, NULL, 1);
    obj = &(*c->objs)[index];
    obj->look.fill = 0;
    if (!OWF_SHADE_SET(obj->look.line))
        obj->look.line = OWF_SHADE(0);
    {
        const char *prst = att(achild(pchild(n, "spPr"), "prstGeom"), "prst");
        if (prst && strcmp(prst, "line") && strcmp(prst, "straightConnector1"))
            owf_report_add(c->x->r.report, OWF_NOTE_APPROX, "Bent and curved connectors are drawn straight");
    }
}

static void read_group(objctx *c, const owf_xml_node *n, int parent, int depth)
{
    const owf_xml_node *xfrm = achild(pchild(n, "grpSpPr"), "xfrm");
    owf_obj *obj = new_obj(c, OWF_OBJ_GROUP, parent);
    long long x, y, w, h;
    int index;
    if (!obj)
        return;
    index = *c->n - 1;
    read_ids(c, obj, n);
    read_box(c, obj, xfrm, NULL, NULL);
    if (c->nt < OWF_DECK_MAX_DEPTH && xfrm_box(xfrm, &x, &y, &w, &h)) {
        xform *t = &c->t[c->nt];
        const owf_xml_node *choff = achild(xfrm, "chOff"), *chext = achild(xfrm, "chExt");
        t->ox = x;
        t->oy = y;
        t->cx = w;
        t->cy = h;
        t->chx = number(att(choff, "x"), x);
        t->chy = number(att(choff, "y"), y);
        t->chcx = number(att(chext, "cx"), w);
        t->chcy = number(att(chext, "cy"), h);
        c->nt++;
        read_tree(c, n, index, depth + 1);
        c->nt--;
    } else {
        read_tree(c, n, index, depth + 1);
    }
}

/* A tint of a colour toward white, thousandths of a percent of the colour. */
static unsigned long tint(unsigned long rgb, long long v)
{
    unsigned long out = 0;
    int k;
    for (k = 16; k >= 0; k -= 8) {
        long long ch = (long long)(rgb >> k & 255);
        out |= (unsigned long)(255 - (255 - ch) * v / 100000) << k;
    }
    return out;
}

/* A table (a:tbl) as the model's native table: cells as paragraphs, the
 * column widths. A table style is drawn as PowerPoint's default one
 * (Medium Style 2, in the first accent colour): a header row in the
 * colour with white bold text, the other rows in bands of its tints. */
static void read_table(objctx *c, int index, const owf_xml_node *tbl)
{
    const owf_xml_node *tblPr = achild(tbl, "tblPr"), *grid = achild(tbl, "tblGrid"), *g, *tr, *tc;
    int widths[TABLE_MAX_COLS], ncols = 0, nrows = 0, merged = 0, i;
    int styled = achild(tblPr, "tableStyleId") != NULL;
    int first_row = same(att(tblPr, "firstRow"), "1"), bands = same(att(tblPr, "bandRow"), "1");
    unsigned long accent = c->master ? c->master->th.scheme[C_ACC1] : 0x4F81BD;
    unsigned long white = c->master ? c->master->th.scheme[C_LT1] : 0xFFFFFF;
    owf_obj *obj = &(*c->objs)[index];

    obj->kind = OWF_OBJ_TABLE;
    for (g = grid ? grid->first : NULL; g; g = g->next)
        if (isa(g, "gridCol") && ncols < TABLE_MAX_COLS)
            widths[ncols++] = clamp(emu_to_twips(number(att(g, "w"), 0)), 0, MAX_COORD);
    for (tr = tbl->first; tr && !c->x->r.failed; tr = tr->next) {
        int col = 0;
        if (!isa(tr, "tr"))
            continue;
        for (tc = tr->first; tc && !c->x->r.failed; tc = tc->next) {
            const owf_xml_node *tcPr;
            textjob j;
            unsigned long fill = 0;
            int before, k, header;
            if (!isa(tc, "tc") || col >= TABLE_MAX_COLS)
                continue;
            if (att(tc, "gridSpan") || att(tc, "rowSpan") || att(tc, "hMerge") || att(tc, "vMerge"))
                merged = 1;
            tcPr = achild(tc, "tcPr");
            header = styled && first_row && nrows == 0;
            if (!fill_of(&c->x->r, &c->cc, tcPr, &fill) && styled) {
                if (header)
                    fill = OWF_SHADE(accent);
                else
                    fill = OWF_SHADE(tint(accent, bands && (nrows - (first_row ? 1 : 0)) % 2 == 0 ? 40000 : 20000));
            }
            memset(&j, 0, sizeof j);
            j.x = c->x;
            j.t.x = &c->x->r;
            j.t.deck = c->x->deck;
            j.t.th = c->master ? &c->master->th : NULL;
            j.t.cc = c->cc;
            j.rels = c->rels;
            j.m = ctx_master(c);
            j.ch.styles[j.ch.nstyles++] = c->x->default_text;
            j.ch.ref_at = 1;
            if (header) {
                j.ch.extra.bold = 1;
                j.ch.extra.colour = white;
                j.ch.extra.has = TP_BOLD | TP_COLOUR;
            }
            obj = &(*c->objs)[index];
            before = obj->text.nparas;
            read_paras(&j, achild(tc, "txBody"), obj, &obj->text);
            obj = &(*c->objs)[index];
            if (obj->text.nparas == before && !owf_story_add(&obj->text, NULL)) {
                c->x->r.failed = 1;
                return;
            }
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
    obj = &(*c->objs)[index];
    for (i = 0; i < obj->text.nparas; i++)
        obj->text.paras[i].table_cols = ncols;
    obj->ncols = ncols;
    if (ncols) {
        obj->col_widths = calloc((size_t)ncols, sizeof *obj->col_widths);
        if (!obj->col_widths)
            c->x->r.failed = 1;
        else
            for (i = 0; i < ncols; i++)
                obj->col_widths[i] = i < TABLE_MAX_COLS && grid ? widths[i] : 0;
    }
    obj->inset[0] = obj->inset[1] = obj->inset[2] = obj->inset[3] = 0;
    if (merged)
        owf_report_add(c->x->r.report, OWF_NOTE_APPROX, "Merged table cells are shown apart");
    if (styled)
        owf_report_add(c->x->r.report, OWF_NOTE_APPROX, "Table styles are drawn as PowerPoint's default one");
}

static void read_frame(objctx *c, const owf_xml_node *n, int parent)
{
    const owf_xml_node *data = achild(achild(n, "graphic"), "graphicData");
    const char *uri = att(data, "uri");
    owf_obj *obj = new_obj(c, OWF_OBJ_OTHER, parent);
    int index;
    if (!obj)
        return;
    index = *c->n - 1;
    read_ids(c, obj, n);
    read_box(c, obj, pchild(n, "xfrm"), NULL, NULL);
    if (uri && strstr(uri, "/table") && achild(data, "tbl")) {
        read_table(c, index, achild(data, "tbl"));
    } else {
        owf_report_add(c->x->r.report, OWF_NOTE_LOST,
                       uri && strstr(uri, "/chart") ? "Charts are shown as their box"
                       : uri && strstr(uri, "/diagram") ? "SmartArt is shown as its box"
                       : "Embedded objects are shown as their box");
    }
}

/* mc:AlternateContent: the fallback's objects, which every reader knows,
 * else the first choice's. */
static const owf_xml_node *alternate(const owf_xml_node *n)
{
    const owf_xml_node *c, *choice = NULL;
    for (c = n->first; c; c = c->next) {
        if (c->type != OWF_XML_ELEMENT || !c->ns || strcmp(c->ns, NS_MC))
            continue;
        if (!strcmp(c->name, "Fallback"))
            return c;
        if (!strcmp(c->name, "Choice") && !choice)
            choice = c;
    }
    return choice;
}

static void read_tree(objctx *c, const owf_xml_node *tree, int parent, int depth)
{
    const owf_xml_node *n;
    if (!tree || depth > OWF_DECK_MAX_DEPTH)
        return;
    for (n = tree->first; n && !c->x->r.failed; n = n->next) {
        if (n->type != OWF_XML_ELEMENT)
            continue;
        if (isp(n, "sp"))
            read_sp(c, n, parent);
        else if (isp(n, "pic"))
            read_pic(c, n, parent);
        else if (isp(n, "cxnSp"))
            read_cxn(c, n, parent);
        else if (isp(n, "grpSp"))
            read_group(c, n, parent, depth);
        else if (isp(n, "graphicFrame"))
            read_frame(c, n, parent);
        else if (n->ns && !strcmp(n->ns, NS_MC) && !strcmp(n->name, "AlternateContent"))
            read_tree(c, alternate(n), parent, depth + 1);
        else if (isp(n, "contentPart"))
            owf_report_add(c->x->r.report, OWF_NOTE_LOST, "Ink was left out");
    }
}

/* ---- Slides ---- */

/* A p:cSld's p:bg onto *colour and *image; 0 when it has none. */
static int read_background(pptx *x, const colours *cc, const rels *r, const owf_xml_node *cSld,
                           unsigned long *colour, int *image)
{
    const owf_xml_node *bg = pchild(cSld, "bg"), *pr, *ref;
    unsigned long rgb, fill;
    if (!bg)
        return 0;
    if ((pr = pchild(bg, "bgPr"))) {
        const owf_xml_node *blip = achild(achild(pr, "blipFill"), "blip");
        if (blip) {
            *image = load_picture(x, r, ratt(blip, "embed"), x->deck->width, x->deck->height, NULL);
            return 1;
        }
        if (fill_of(&x->r, cc, pr, &fill)) {
            *colour = fill ? fill : OWF_SHADE(0xFFFFFF);
            return 1;
        }
    }
    if ((ref = pchild(bg, "bgRef")) && colour_of(cc, ref, &rgb)) {
        *colour = OWF_SHADE(rgb);
        if (number(att(ref, "idx"), 0) > 1001)
            owf_report_add(x->r.report, OWF_NOTE_APPROX, "Gradient and pattern fills are drawn in one colour");
        return 1;
    }
    return 0;
}

static void read_transition(const owf_xml_node *slide, owf_slide *s)
{
    const owf_xml_node *t = pchild(slide, "transition"), *k;
    owf_transition *tr = &s->transition;
    long long v;
    const char *spd, *dir;
    if (!t) {
        const owf_xml_node *c;
        for (c = slide->first; c && !t; c = c->next)
            if (c->type == OWF_XML_ELEMENT && c->ns && !strcmp(c->ns, NS_MC) && !strcmp(c->name, "AlternateContent"))
                t = pchild(alternate(c), "transition");
    }
    if (!t)
        return;
    spd = att(t, "spd");
    tr->duration = same(spd, "slow") ? 2000 : same(spd, "fast") ? 500 : 1000;
    if (number_att(t, "dur", &v) && v > 0 && v < 600000)
        tr->duration = (int)v;
    if (number_att(t, "advTm", &v) && v >= 0 && v < 86400000)
        tr->advance_after = (int)v;
    for (k = t->first; k && k->type != OWF_XML_ELEMENT; k = k->next)
        ;
    if (!k)
        return;
    dir = att(k, "dir");
    /* PowerPoint names where the slide moves to; the model where it comes from */
    if (isp(k, "push") || isp(k, "wipe") || isp(k, "cover") || isp(k, "pull"))
        tr->direction = same(dir, "u") ? OWF_DIR_DOWN : same(dir, "d") ? OWF_DIR_UP : same(dir, "r") ? OWF_DIR_LEFT
                      : OWF_DIR_RIGHT;
    if (isp(k, "fade")) tr->kind = same(att(k, "thruBlk"), "1") || same(att(k, "thruBlk"), "true")
                                   ? OWF_TRANS_FADE_BLACK : OWF_TRANS_FADE;
    else if (isp(k, "push")) tr->kind = OWF_TRANS_PUSH;
    else if (isp(k, "wipe")) tr->kind = OWF_TRANS_WIPE;
    else if (isp(k, "cover")) tr->kind = OWF_TRANS_COVER;
    else if (isp(k, "pull")) tr->kind = OWF_TRANS_UNCOVER;
    else if (isp(k, "split")) {
        tr->kind = OWF_TRANS_SPLIT;
        tr->direction = same(att(k, "orient"), "vert") ? OWF_DIR_LEFT : OWF_DIR_UP;
    } else if (isp(k, "blinds")) {
        tr->kind = OWF_TRANS_BLINDS;
        tr->direction = same(dir, "vert") ? OWF_DIR_LEFT : OWF_DIR_UP;
    } else if (isp(k, "dissolve")) tr->kind = OWF_TRANS_DISSOLVE;
    else if (isp(k, "cut")) tr->kind = OWF_TRANS_NONE;
    else tr->kind = OWF_TRANS_OTHER;
}

/* ---- Masters and layouts ---- */

static void level_from(pptx *x, const part *mp, const owf_xml_node *base, const owf_xml_node *own, int lvl,
                       owf_textlevel *lv)
{
    textctx t;
    chain ch;
    tprops p;
    memset(&t, 0, sizeof t);
    memset(&ch, 0, sizeof ch);
    t.x = &x->r;
    t.deck = x->deck;
    t.th = &mp->th;
    t.cc.th = &mp->th;
    memcpy(t.cc.map, mp->map, sizeof t.cc.map);
    ch.styles[ch.nstyles++] = base;
    ch.styles[ch.nstyles++] = own;
    ch.ref_at = ch.nstyles;
    chain_level(&t, &ch, lvl, &p);
    to_level(&t, &p, lv);
}

/* The master part, read once: its theme, colour map, text levels,
 * background and art. Its index into x->masters, or -1. */
static int get_master(pptx *x, const char *name)
{
    part *mp;
    owf_master *m;
    const owf_xml_node *cSld;
    const rel *rl;
    char theme_name[NAME_SIZE];
    objctx c;
    int i, index;

    for (i = 0; i < x->nmasters; i++)
        if (!strcmp(x->masters[i].name, name))
            return i;
    if (x->nmasters >= OWF_DECK_MAX_MASTERS) {
        owf_report_add(x->r.report, OWF_NOTE_LOST, "Masters beyond %d were left out", OWF_DECK_MAX_MASTERS);
        return x->nmasters ? 0 : -1;
    }
    mp = &x->masters[x->nmasters];
    memset(mp, 0, sizeof *mp);
    snprintf(mp->name, sizeof mp->name, "%s", name);
    mp->xml = read_xml(x->r.zip, name, NULL);
    mp->root = mp->xml ? owf_xml_root(mp->xml) : NULL;
    if (!mp->root || !isp(mp->root, "sldMaster")) {
        owf_xml_free(mp->xml);
        return -1;
    }
    x->nmasters++;
    read_rels(x->r.zip, name, &mp->rels);
    cSld = pchild(mp->root, "cSld");
    mp->tree = pchild(cSld, "spTree");
    rl = rel_type(&mp->rels, "theme");
    read_theme(&x->r, x->deck, rel_part(&mp->rels, rl, theme_name, sizeof theme_name) ? theme_name : NULL, &mp->th);
    read_map(pchild(mp->root, "clrMap"), mp->map);

    m = owf_deck_add_master(x->deck, att(cSld, "name") && *att(cSld, "name") ? att(cSld, "name") : "Master");
    if (!m) {
        x->r.failed = 1;
        return -1;
    }
    index = x->deck->nmasters - 1;
    mp->index = index;
    /* its text levels: its title, body and other styles, under its placeholders' own */
    {
        const owf_xml_node *ts = pchild(mp->root, "txStyles");
        const owf_xml_node *title_ph = NULL, *body_ph = NULL, *n;
        for (n = mp->tree ? mp->tree->first : NULL; n; n = n->next) {
            const owf_xml_node *ph = ph_of(n);
            int st;
            if (!ph)
                continue;
            ph_kind(att(ph, "type"), &st);
            if (st == 0 && !title_ph) title_ph = n;
            else if (st == 1 && !body_ph) body_ph = n;
        }
        level_from(x, mp, pchild(ts, "titleStyle") ? pchild(ts, "titleStyle") : x->default_text, lst_of(title_ph), 0,
                   &m->title);
        for (i = 0; i < OWF_TEXT_LEVELS; i++)
            level_from(x, mp, pchild(ts, "bodyStyle") ? pchild(ts, "bodyStyle") : x->default_text, lst_of(body_ph), i,
                       &m->body[i]);
        level_from(x, mp, pchild(ts, "otherStyle") ? pchild(ts, "otherStyle") : x->default_text, NULL, 0, &m->other);
        /* a subtitle: the first body level, centred, without its bullet */
        m->subtitle = m->body[0];
        m->subtitle.bullet = 0;
        m->subtitle.bullet_font = -1;
        m->subtitle.bullet_size = 0;
        m->subtitle.bullet_colour = 0;
        m->subtitle.para.indent_left = m->subtitle.para.indent_first = 0;
        m->subtitle.para.align = OWF_ALIGN_CENTRE;
        owf_textlevel_init(&m->notes);
        m->notes.chars.font = mp->th.minor;
        m->notes.chars.size = 12 * OWF_TWIPS_PER_POINT;
        m->notes.chars.colour = 0x000000;
    }
    /* its background and art */
    memset(&c, 0, sizeof c);
    c.x = x;
    c.rels = &mp->rels;
    c.master = mp;
    c.deck_master = index;
    c.cc.th = &mp->th;
    memcpy(c.cc.map, mp->map, sizeof c.cc.map);
    c.slide = -1;
    m->background = OWF_SHADE(mp->th.scheme[mp->map[0]]);
    read_background(x, &c.cc, &mp->rels, cSld, &x->deck->masters[index].background,
                    &x->deck->masters[index].background_image);
    m = &x->deck->masters[index];
    c.objs = &m->objs;
    c.n = &m->nobjs;
    c.cap = &m->capobjs;
    read_tree(&c, mp->tree, -1, 0);
    return x->nmasters - 1;
}

/* The layout part, read once, with its master: its placeholders' boxes. Its
 * index into x->layouts, or -1. */
static int get_layout(pptx *x, const char *name)
{
    part *lp;
    owf_slide_layout *l;
    const owf_xml_node *cSld, *n;
    const rel *rl;
    char master_name[NAME_SIZE];
    int i, master, art = 0;

    for (i = 0; i < x->nlayouts; i++)
        if (!strcmp(x->layouts[i].name, name))
            return i;
    if (x->nlayouts >= OWF_DECK_MAX_LAYOUTS)
        return -1;
    lp = &x->layouts[x->nlayouts];
    memset(lp, 0, sizeof *lp);
    snprintf(lp->name, sizeof lp->name, "%s", name);
    lp->xml = read_xml(x->r.zip, name, NULL);
    lp->root = lp->xml ? owf_xml_root(lp->xml) : NULL;
    if (!lp->root || !isp(lp->root, "sldLayout")) {
        owf_xml_free(lp->xml);
        return -1;
    }
    read_rels(x->r.zip, name, &lp->rels);
    rl = rel_type(&lp->rels, "slideMaster");
    master = rel_part(&lp->rels, rl, master_name, sizeof master_name) ? get_master(x, master_name) : -1;
    if (master < 0) {
        free_rels(&lp->rels);
        owf_xml_free(lp->xml);
        return -1;
    }
    x->nlayouts++;
    lp->master = master;
    cSld = pchild(lp->root, "cSld");
    lp->tree = pchild(cSld, "spTree");
    l = owf_deck_add_layout(x->deck, att(cSld, "name") && *att(cSld, "name") ? att(cSld, "name") : "Layout",
                            x->masters[master].index);
    if (!l) {
        x->r.failed = 1;
        return -1;
    }
    lp->index = x->deck->nlayouts - 1;
    for (n = lp->tree ? lp->tree->first : NULL; n && !x->r.failed; n = n->next) {
        const owf_xml_node *ph = ph_of(n), *mph;
        owf_obj *obj;
        long long bx = 0, by = 0, bw = 0, bh = 0;
        int k = 0, j;
        if (!ph) {
            if (isp(n, "sp") || isp(n, "pic") || isp(n, "grpSp") || isp(n, "cxnSp") || isp(n, "graphicFrame"))
                art = 1;
            continue;
        }
        obj = owf_layout_add(&x->deck->layouts[lp->index], OWF_OBJ_TEXT);
        if (!obj) {
            x->r.failed = 1;
            break;
        }
        l = &x->deck->layouts[lp->index];
        obj->placeholder = ph_kind(att(ph, "type"), NULL);
        for (j = 0; j < l->nobjs - 1; j++)
            if (l->objs[j].placeholder == obj->placeholder)
                k++;
        obj->ph_index = k;
        mph = find_ph(x->masters[master].tree, ph, 1);
        if (xfrm_box(achild(sp_pr(n), "xfrm"), &bx, &by, &bw, &bh) ||
            (mph && xfrm_box(achild(sp_pr(mph), "xfrm"), &bx, &by, &bw, &bh))) {
            obj->x = emu_to_twips(bx);
            obj->y = emu_to_twips(by);
            obj->width = clamp(emu_to_twips(bw), 0, MAX_COORD);
            obj->height = clamp(emu_to_twips(bh), 0, MAX_COORD);
        }
    }
    if (art)
        owf_report_add(x->r.report, OWF_NOTE_LOST, "A layout's own pictures and shapes are not shown");
    return x->nlayouts - 1;
}

/* A slide's notes page: its body placeholder's text. */
static void read_notes(pptx *x, const rels *r, int slide, const part *mp, const colours *cc)
{
    const rel *rl = rel_type(r, "notesSlide");
    char name[NAME_SIZE];
    owf_xml *xml;
    rels nr;
    const owf_xml_node *tree, *n;
    if (!rel_part(r, rl, name, sizeof name) || !(xml = read_xml(x->r.zip, name, NULL)))
        return;
    read_rels(x->r.zip, name, &nr);
    tree = pchild(pchild(owf_xml_root(xml), "cSld"), "spTree");
    for (n = tree ? tree->first : NULL; n; n = n->next) {
        const owf_xml_node *ph = ph_of(n);
        textjob j;
        if (!isp(n, "sp") || !ph || !same(att(ph, "type"), "body"))
            continue;
        memset(&j, 0, sizeof j);
        j.x = x;
        j.t.x = &x->r;
        j.t.deck = x->deck;
        j.t.th = &mp->th;
        j.t.cc = *cc;
        j.rels = &nr;
        j.m = &x->deck->masters[mp->index];
        j.ch.styles[j.ch.nstyles++] = x->default_text;
        j.ch.ref_at = 1;
        j.ch.extra.size = 12 * OWF_TWIPS_PER_POINT;
        j.ch.extra.has = TP_SIZE;
        read_paras(&j, pchild(n, "txBody"), NULL, &x->deck->slides[slide].notes);
        break;
    }
    free_rels(&nr);
    owf_xml_free(xml);
}

static void read_slide(pptx *x, const char *name)
{
    owf_xml *xml;
    const owf_xml_node *root, *cSld;
    const rel *rl;
    rels r;
    char layout_name[NAME_SIZE];
    int layout = -1, index;
    part *mp, *lp = NULL;
    owf_slide *s;
    objctx c;

    if (x->deck->nslides >= OWF_DECK_MAX_SLIDES) {
        owf_report_add(x->r.report, OWF_NOTE_LOST, "Slides beyond %d were left out", OWF_DECK_MAX_SLIDES);
        return;
    }
    xml = read_xml(x->r.zip, name, NULL);
    root = xml ? owf_xml_root(xml) : NULL;
    if (!root || !isp(root, "sld")) {
        owf_report_add(x->r.report, OWF_NOTE_LOST, "A slide that could not be read was left out");
        owf_xml_free(xml);
        return;
    }
    read_rels(x->r.zip, name, &r);
    rl = rel_type(&r, "slideLayout");
    if (rel_part(&r, rl, layout_name, sizeof layout_name))
        layout = get_layout(x, layout_name);
    if (layout >= 0) {
        lp = &x->layouts[layout];
        mp = &x->masters[lp->master];
    } else if (x->nmasters) {
        mp = &x->masters[0];
    } else {
        owf_report_add(x->r.report, OWF_NOTE_LOST, "A slide without a layout or master was left out");
        free_rels(&r);
        owf_xml_free(xml);
        return;
    }
    s = owf_deck_add_slide(x->deck, -1, -1);
    if (!s) {
        x->r.failed = 1;
        free_rels(&r);
        owf_xml_free(xml);
        return;
    }
    index = x->deck->nslides - 1;
    s->layout = lp ? lp->index : -1;
    s->master = mp->index;
    cSld = pchild(root, "cSld");
    if (att(cSld, "name") && *att(cSld, "name") && owf_deck_set_text(&s->name, att(cSld, "name")) < 0)
        x->r.failed = 1;
    s->hidden = same(att(root, "show"), "0") || same(att(root, "show"), "false");

    memset(&c, 0, sizeof c);
    c.x = x;
    c.rels = &r;
    c.layout_tree = lp ? lp->tree : NULL;
    c.master_tree = mp->tree;
    c.master = mp;
    c.deck_master = mp->index;
    c.cc.th = &mp->th;
    memcpy(c.cc.map, mp->map, sizeof c.cc.map);
    {
        const owf_xml_node *ov = achild(pchild(root, "clrMapOvr"), "overrideClrMapping");
        if (ov)
            read_map(ov, c.cc.map);
    }
    c.slide = index;
    if (!read_background(x, &c.cc, &r, cSld, &x->deck->slides[index].background,
                         &x->deck->slides[index].background_image) && lp)
        read_background(x, &c.cc, &lp->rels, pchild(lp->root, "cSld"), &x->deck->slides[index].background,
                        &x->deck->slides[index].background_image);
    s = &x->deck->slides[index];
    c.objs = &s->objs;
    c.n = &s->nobjs;
    c.cap = &s->capobjs;
    read_tree(&c, pchild(cSld, "spTree"), -1, 0);
    s = &x->deck->slides[index];
    read_transition(root, s);
    if (s->transition.kind == OWF_TRANS_OTHER)
        owf_report_add(x->r.report, OWF_NOTE_APPROX, "Transitions OpenPresentation doesn't show yet are shown as a cut");
    if (pchild(root, "timing"))
        owf_report_add(x->r.report, OWF_NOTE_LOST, "Animations were left out");
    read_notes(x, &r, index, mp, &c.cc);
    free_rels(&r);
    owf_xml_free(xml);
}

/* ---- Reading ---- */

/* The package's title: docProps/core.xml's dc:title. */
static void read_title(pptx *x, const rels *package)
{
    const rel *rl = rel_type(package, "core-properties");
    char name[NAME_SIZE];
    owf_xml *xml;
    const owf_xml_node *t;
    if (!rl || !rel_part(package, rl, name, sizeof name))
        snprintf(name, sizeof name, "docProps/core.xml");
    xml = read_xml(x->r.zip, name, NULL);
    t = xml ? owf_xml_child(owf_xml_root(xml), NS_DC, "title") : NULL;
    if (t) {
        owf_buf b;
        owf_buf_init(&b);
        text_of(t, &b, 0);
        owf_buf_putc(&b, 0);
        if (b.failed || (b.data[0] && owf_doc_set_title(x->deck->res, (const char *)b.data) < 0))
            x->r.failed = 1;
        owf_buf_free(&b);
    }
    owf_xml_free(xml);
}

static int import_pptx(const unsigned char *data, size_t length, owf_deck *deck, owf_report *report)
{
    owf_unzip zip;
    owf_xml *pres = NULL;
    const owf_xml_node *root, *c;
    rels package, prels;
    char main_name[NAME_SIZE] = "ppt/presentation.xml", name[NAME_SIZE];
    pptx *x;
    int result = OWF_OK, i, error;

    if (!owf_unzip_open(&zip, data, length))
        return OWF_ERR_FORMAT;
    x = calloc(1, sizeof *x);
    if (!x)
        return OWF_ERR_MEMORY;
    x->masters = calloc(OWF_DECK_MAX_MASTERS, sizeof *x->masters);
    x->layouts = calloc(OWF_DECK_MAX_LAYOUTS, sizeof *x->layouts);
    if (!x->masters || !x->layouts) {
        free(x->masters);
        free(x->layouts);
        free(x);
        return OWF_ERR_MEMORY;
    }
    x->r.zip = &zip;
    x->r.report = report;
    x->deck = deck;

    /* the main part, by the package's relationship */
    read_rels(&zip, "", &package);
    {
        const rel *rl = rel_type(&package, "officeDocument");
        if (rl)
            owf_opc_part_name(main_name, sizeof main_name, "", rl->target);
    }
    pres = read_xml(&zip, main_name, &error);
    root = pres ? owf_xml_root(pres) : NULL;
    if (!root || !isp(root, "presentation")) {
        result = pres ? OWF_ERR_FORMAT : error == OWF_ERR_FORMAT ? OWF_ERR_CORRUPT : error;
        goto done;
    }
    read_rels(&zip, main_name, &prels);
    x->default_text = pchild(root, "defaultTextStyle");
    {
        const owf_xml_node *sz = pchild(root, "sldSz"), *nsz = pchild(root, "notesSz");
        long long cx, cy;
        if (sz && number_att(sz, "cx", &cx) && number_att(sz, "cy", &cy)) {
            deck->width = clamp(emu_to_twips(cx), 144, MAX_COORD);
            deck->height = clamp(emu_to_twips(cy), 144, MAX_COORD);
        }
        if (nsz && number_att(nsz, "cx", &cx) && number_att(nsz, "cy", &cy)) {
            deck->notes_page.width = clamp(emu_to_twips(cx), 144, MAX_COORD);
            deck->notes_page.height = clamp(emu_to_twips(cy), 144, MAX_COORD);
        }
    }
    /* the masters in their order, then the slides */
    for (c = pchild(root, "sldMasterIdLst") ? pchild(root, "sldMasterIdLst")->first : NULL; c && !x->r.failed;
         c = c->next)
        if (isp(c, "sldMasterId") && rel_part(&prels, rel_id(&prels, ratt(c, "id")), name, sizeof name))
            get_master(x, name);
    for (c = pchild(root, "sldIdLst") ? pchild(root, "sldIdLst")->first : NULL; c && !x->r.failed; c = c->next)
        if (isp(c, "sldId")) {
            if (rel_part(&prels, rel_id(&prels, ratt(c, "id")), name, sizeof name))
                read_slide(x, name);
            else
                owf_report_add(report, OWF_NOTE_LOST, "A slide that is not in the file was left out");
        }
    if (!deck->nmasters && !x->r.failed) {
        /* no master at all: a plain one, so the deck can be shown and saved */
        owf_master *m = owf_deck_add_master(deck, "Default");
        int font = owf_doc_font(deck->res, "Liberation Sans", OWF_FONT_SANS);
        if (!m || font < 0) {
            x->r.failed = 1;
        } else {
            owf_deck_default_levels(m, font);
            m->background = OWF_SHADE(0xFFFFFF);
        }
    }
    if (x->nmasters) {
        deck->res->base.font = x->masters[0].th.minor;
        deck->res->base.size = 18 * OWF_TWIPS_PER_POINT;
    } else if (deck->nmasters) {
        deck->res->base.font = deck->masters[0].other.chars.font;
    }
    read_title(x, &package);
    owf_report_add(report, OWF_NOTE_APPROX, "Theme colours are read as fixed colours");
    free_rels(&prels);
    result = x->r.failed ? OWF_ERR_MEMORY : OWF_OK;
done:
    for (i = 0; i < x->nmasters; i++) {
        free_rels(&x->masters[i].rels);
        owf_xml_free(x->masters[i].xml);
    }
    for (i = 0; i < x->nlayouts; i++) {
        free_rels(&x->layouts[i].rels);
        owf_xml_free(x->layouts[i].xml);
    }
    free_rels(&package);
    owf_xml_free(pres);
    free(x->masters);
    free(x->layouts);
    free(x->pics);
    free(x->picnames);
    free(x);
    return result;
}

static int detect_pptx(const unsigned char *data, size_t length)
{
    return length >= 4 && data[0] == 'P' && data[1] == 'K' && owf_ooxml_kind(data, length) == OWF_PACKAGE_SLIDES
           ? 100 : 0;
}

const owf_deck_format owf_deck_format_pptx = {
    "pptx", "PowerPoint presentation (Office Open XML)", "pptx potx ppsx pptm", detect_pptx, import_pptx, NULL
};

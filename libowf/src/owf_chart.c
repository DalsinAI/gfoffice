/*
 * libowf: the charts on a sheet, read from the parts an .xlsx's drawing
 * was kept with (owf_sheet.drawing, owf_book.parts): for a program to
 * show them. Each chart's place (the drawing's anchor: from a cell to
 * another, from a cell with a size, or from the sheet's corner), its
 * kind (columns, bars, lines, areas, a pie; the first group of a chart
 * that has several), title, legend, value axis, and its series: their
 * names, the cells their values and categories come from, and the values
 * and categories the file last had (the caches Excel writes).
 *
 * Small parts, read as trees. Damaged ones give fewer charts, never an
 * error; only memory running out is one.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POINTS 4096                  /* a series' points read */
#define MAX_SERIES 64
#define MAX_CHARTS 64                    /* a sheet's */
#define EMU_PER_TWIP 635L

/* The namespaces, transitional and strict. */
static const char *const ns_xdr[2] = { "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing",
                                       "http://purl.oclc.org/ooxml/drawingml/spreadsheetDrawing" };
static const char *const ns_a[2] = { "http://schemas.openxmlformats.org/drawingml/2006/main",
                                     "http://purl.oclc.org/ooxml/drawingml/main" };
static const char *const ns_c[2] = { "http://schemas.openxmlformats.org/drawingml/2006/chart",
                                     "http://purl.oclc.org/ooxml/drawingml/chart" };
static const char *const ns_r[2] = { "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
                                     "http://purl.oclc.org/ooxml/officeDocument/relationships" };

static int in_ns(const owf_xml_node *n, const char *const *ns)
{
    return n && n->type == OWF_XML_ELEMENT && (!strcmp(n->ns, ns[0]) || !strcmp(n->ns, ns[1]));
}

static int is(const owf_xml_node *n, const char *const *ns, const char *name)
{
    return in_ns(n, ns) && !strcmp(n->name, name);
}

static const owf_xml_node *child(const owf_xml_node *n, const char *const *ns, const char *name)
{
    const owf_xml_node *c;
    for (c = n ? n->first : NULL; c; c = c->next)
        if (is(c, ns, name))
            return c;
    return NULL;
}

static const owf_xml_node *next(const owf_xml_node *n, const char *const *ns, const char *name)
{
    for (n = n ? n->next : NULL; n; n = n->next)
        if (is(n, ns, name))
            return n;
    return NULL;
}

static const char *val(const owf_xml_node *n)
{
    return n ? owf_xml_attr(n, "", "val") : NULL;
}

/* An element's text (its text children), at most size - 1 bytes. */
static void text_of(const owf_xml_node *n, char *out, size_t size)
{
    const owf_xml_node *c;
    size_t k = 0;
    for (c = n ? n->first : NULL; c; c = c->next)
        if (c->type == OWF_XML_TEXT) {
            size_t m = c->length < size - 1 - k ? c->length : size - 1 - k;
            memcpy(out + k, c->name, m);
            k += m;
        }
    out[k] = 0;
}

static long number_of(const owf_xml_node *n, long def)
{
    char buf[32], *end;
    long v;
    text_of(n, buf, sizeof buf);
    v = strtol(buf, &end, 10);
    return end == buf ? def : v;
}

static long attr_long(const owf_xml_node *n, const char *name, long def)
{
    const char *s = n ? owf_xml_attr(n, "", name) : NULL;
    char *end;
    long v;
    if (!s)
        return def;
    v = strtol(s, &end, 10);
    return end == s ? def : v;
}

static int attr_double(const owf_xml_node *n, double *d)
{
    const char *s = val(n);
    return s && owf_number_parse(s, strlen(s), d);
}

/* All of an element's text below it (a rich title's runs), into a buffer; 0 when memory ran out. */
static void all_text(const owf_xml_node *n, owf_buf *b)
{
    const owf_xml_node *c;
    for (c = n ? n->first : NULL; c; c = c->next) {
        if (c->type == OWF_XML_TEXT) {
            if (is(n, ns_a, "t") || is(n, ns_c, "v"))
                owf_buf_put(b, c->name, c->length);
        } else if (c->type == OWF_XML_ELEMENT) {
            if (is(c, ns_a, "p") && b->length)
                owf_buf_putc(b, ' ');            /* a paragraph after another */
            all_text(c, b);
        }
    }
}

static char *dup_buf(owf_buf *b, int *failed)
{
    char *s;
    if (b->failed) {
        *failed = 1;
        return NULL;
    }
    s = malloc(b->length + 1);
    if (!s) {
        *failed = 1;
        return NULL;
    }
    memcpy(s, b->data ? (const char *)b->data : "", b->length);
    s[b->length] = 0;
    return s;
}

static char *dup_str(const char *t, int *failed)
{
    char *s;
    if (!t)
        return NULL;
    s = malloc(strlen(t) + 1);
    if (!s) {
        *failed = 1;
        return NULL;
    }
    strcpy(s, t);
    return s;
}

/* A colour from an element holding a:srgbClr or a:schemeClr (an accent, by Office's theme), as
 * OWF_SHADE(rgb); 0 when none. */
static unsigned long colour_in(const owf_xml_node *fill)
{
    static const unsigned long accents[6] = { 0x4472C4UL, 0xED7D31UL, 0xA5A5A5UL, 0xFFC000UL, 0x5B9BD5UL, 0x70AD47UL };
    const owf_xml_node *c;
    unsigned long rgb;
    for (c = fill ? fill->first : NULL; c; c = c->next) {
        const char *v = val(c);
        if (!v)
            continue;
        if (is(c, ns_a, "srgbClr") && owf_parse_colour(v, &rgb))
            return OWF_SHADE(rgb);
        if (is(c, ns_a, "schemeClr")) {
            if (!strncmp(v, "accent", 6) && v[6] >= '1' && v[6] <= '6' && !v[7])
                return OWF_SHADE(accents[v[6] - '1']);
            if (!strcmp(v, "tx1") || !strcmp(v, "dk1"))
                return OWF_SHADE(0);
            if (!strcmp(v, "bg1") || !strcmp(v, "lt1"))
                return OWF_SHADE(0xFFFFFFUL);
        }
    }
    return 0;
}

/* A series' own colour: its shape's fill, or its line's for lines. */
static unsigned long series_colour(const owf_xml_node *ser, int kind)
{
    const owf_xml_node *sp = child(ser, ns_c, "spPr");
    unsigned long c = 0;
    if (kind == OWF_CHART_LINE)
        c = colour_in(child(child(sp, ns_a, "ln"), ns_a, "solidFill"));
    if (!c)
        c = colour_in(child(sp, ns_a, "solidFill"));
    if (!c && kind != OWF_CHART_LINE)
        c = colour_in(child(child(sp, ns_a, "ln"), ns_a, "solidFill"));
    return c;
}

/* A reference's formula and its cache (strRef, numRef, or a literal) in a series' tx, cat or val:
 * the formula (or NULL) and the cache's element (strCache, numCache, strLit, numLit), or NULL. */
static const owf_xml_node *data_source(const owf_xml_node *n, const owf_xml_node **f)
{
    static const char *const refs[3] = { "strRef", "numRef", "multiLvlStrRef" };
    static const char *const caches[3] = { "strCache", "numCache", "multiLvlStrCache" };
    const owf_xml_node *r;
    int i;
    *f = NULL;
    for (i = 0; i < 3; i++)
        if ((r = child(n, ns_c, refs[i])) != NULL) {
            const owf_xml_node *cache = child(r, ns_c, caches[i]);
            *f = child(r, ns_c, "f");
            if (i == 2 && cache)                 /* several levels: the innermost (the first lvl) */
                cache = child(cache, ns_c, "lvl");
            return cache;
        }
    if ((r = child(n, ns_c, "strLit")) != NULL || (r = child(n, ns_c, "numLit")) != NULL)
        return r;
    return NULL;
}

static int point_count(const owf_xml_node *cache)
{
    const owf_xml_node *p;
    long n = attr_long(child(cache, ns_c, "ptCount"), "val", 0), idx;
    for (p = child(cache, ns_c, "pt"); p; p = next(p, ns_c, "pt"))
        if ((idx = attr_long(p, "idx", -1)) >= n && idx < MAX_POINTS)
            n = idx + 1;
    return n < 0 ? 0 : n > MAX_POINTS ? MAX_POINTS : (int)n;
}

static int marker_of(const char *v)
{
    static const char *const names[] = { "auto", "none", "circle", "square", "diamond", "triangle", "x", "star", "plus",
                                         "dash", "dot" };
    size_t i;
    for (i = 0; v && i < sizeof names / sizeof *names; i++)
        if (!strcmp(v, names[i]))
            return (int)i;
    return v && !strcmp(v, "picture") ? OWF_MARKER_SQUARE : OWF_MARKER_AUTO;
}

/* A number format the file sets (numFmt, not linked to the cells'), or NULL. */
static char *format_of(const owf_xml_node *numfmt, int *failed)
{
    const char *code = numfmt ? owf_xml_attr(numfmt, "", "formatCode") : NULL, *linked;
    if (!code || !*code)
        return NULL;
    linked = owf_xml_attr(numfmt, "", "sourceLinked");
    if (linked && (!strcmp(linked, "1") || !strcmp(linked, "true")))
        return NULL;
    return dup_str(code, failed);
}

static int flag_set(const owf_xml_node *n, const char *name)
{
    const char *v = val(child(n, ns_c, name));
    return child(n, ns_c, name) && (!v || !strcmp(v, "1") || !strcmp(v, "true"));
}

/* Data labels (dLbls): what they show, where, in which format. A series' own over its group's. */
static void read_labels(owf_chart_series *s, const owf_xml_node *d, int *failed)
{
    static const char *const pos[] = { "", "ctr", "inEnd", "inBase", "outEnd", "t", "b", "l", "r", "bestFit" };
    const char *v;
    size_t i;
    if (!d)
        return;
    if (flag_set(d, "delete")) {
        s->labels = 0;
        return;
    }
    s->labels = (flag_set(d, "showVal") ? OWF_LABEL_VALUE : 0) | (flag_set(d, "showPercent") ? OWF_LABEL_PERCENT : 0) |
                (flag_set(d, "showCatName") ? OWF_LABEL_CATEGORY : 0) | (flag_set(d, "showSerName") ? OWF_LABEL_SERIES : 0);
    s->label_pos = OWF_LABEL_AUTO;
    if ((v = val(child(d, ns_c, "dLblPos"))) != NULL)
        for (i = 1; i < sizeof pos / sizeof *pos; i++)
            if (!strcmp(v, pos[i]))
                s->label_pos = (int)i;
    free(s->label_format);
    s->label_format = format_of(child(d, ns_c, "numFmt"), failed);
}

static int read_series(owf_chart_series *s, const owf_xml_node *ser, int kind, int number, int *failed)
{
    const owf_xml_node *f, *cat_cache, *val_cache, *name_cache, *p, *m, *ln;
    char buf[256];
    int n, scatter = kind == OWF_CHART_SCATTER;
    long most = -1;
    owf_buf b;

    s->index = (int)attr_long(child(ser, ns_c, "idx"), "val", number);
    if (s->index < 0)
        s->index = number;
    s->colour = series_colour(ser, kind);
    ln = child(child(ser, ns_c, "spPr"), ns_a, "ln");
    s->line = !child(ln, ns_a, "noFill");
    m = child(ser, ns_c, "marker");
    s->marker = marker_of(val(child(m, ns_c, "symbol")));
    s->marker_size = (int)attr_long(child(m, ns_c, "size"), "val", 0);
    if (s->marker_size < 0 || s->marker_size > 72)
        s->marker_size = 0;
    /* its name: a reference's cache, or the text itself */
    name_cache = data_source(child(ser, ns_c, "tx"), &f);
    if (f) {
        text_of(f, buf, sizeof buf);
        s->name_cell = dup_str(buf, failed);
    }
    owf_buf_init(&b);
    if (name_cache)
        all_text(child(name_cache, ns_c, "pt"), &b);
    else if (child(child(ser, ns_c, "tx"), ns_c, "v"))
        all_text(child(ser, ns_c, "tx"), &b);
    if (b.length || b.failed)
        s->name = dup_buf(&b, failed);
    owf_buf_free(&b);
    /* its values and categories (a scatter's y and x) */
    val_cache = data_source(child(ser, ns_c, scatter ? "yVal" : "val"), &f);
    if (f) {
        text_of(f, buf, sizeof buf);
        s->values = dup_str(buf, failed);
    }
    cat_cache = data_source(child(ser, ns_c, scatter ? "xVal" : "cat"), &f);
    if (f) {
        text_of(f, buf, sizeof buf);
        if (scatter)
            s->x_values = dup_str(buf, failed);
        else
            s->categories = dup_str(buf, failed);
    }
    if (child(val_cache, ns_c, "formatCode")) {
        text_of(child(val_cache, ns_c, "formatCode"), buf, sizeof buf);
        if (*buf && strcmp(buf, "General"))
            s->format = dup_str(buf, failed);
    }
    read_labels(s, child(ser, ns_c, "dLbls"), failed);
    n = point_count(val_cache);
    if (point_count(cat_cache) > n)
        n = point_count(cat_cache);
    for (p = child(ser, ns_c, "dPt"); p; p = next(p, ns_c, "dPt"))   /* points of their own colour */
        if ((most = attr_long(child(p, ns_c, "idx"), "val", -1)) >= n && most < MAX_POINTS)
            n = (int)most + 1;
    if (n && !*failed) {
        s->points = calloc((size_t)n, sizeof *s->points);
        if (!s->points) {
            *failed = 1;
            return 0;
        }
        s->npoints = n;
    }
    for (p = child(val_cache, ns_c, "pt"); p && !*failed; p = next(p, ns_c, "pt")) {
        long idx = attr_long(p, "idx", -1);
        double d;
        text_of(child(p, ns_c, "v"), buf, sizeof buf);
        if (idx >= 0 && idx < n && owf_number_parse(buf, strlen(buf), &d)) {
            s->points[idx].value = d;
            s->points[idx].has_value = 1;
        }
    }
    for (p = child(cat_cache, ns_c, "pt"); p && !*failed; p = next(p, ns_c, "pt")) {
        long idx = attr_long(p, "idx", -1);
        double d;
        if (idx >= 0 && idx < n && !s->points[idx].label) {
            text_of(child(p, ns_c, "v"), buf, sizeof buf);
            if (scatter && is(cat_cache, ns_c, "numCache") && owf_number_parse(buf, strlen(buf), &d)) {
                s->points[idx].x = d;            /* a number, not a category */
                s->points[idx].has_x = 1;
            } else {
                s->points[idx].label = dup_str(buf, failed);
            }
        }
    }
    for (p = child(ser, ns_c, "dPt"); p && !*failed; p = next(p, ns_c, "dPt")) {
        long idx = attr_long(child(p, ns_c, "idx"), "val", -1);
        if (idx >= 0 && idx < n)
            s->points[idx].colour = colour_in(child(child(p, ns_c, "spPr"), ns_a, "solidFill"));
    }
    return !*failed;
}

/* The chart part's first group of series (barChart, lineChart...) into c. */
static void read_chart(owf_chart *c, const owf_xml_node *space, int *failed)
{
    static const struct { const char *name; int kind; } groups[] = {
        { "barChart", OWF_CHART_COLUMN }, { "bar3DChart", OWF_CHART_COLUMN }, { "lineChart", OWF_CHART_LINE },
        { "line3DChart", OWF_CHART_LINE }, { "areaChart", OWF_CHART_AREA }, { "area3DChart", OWF_CHART_AREA },
        { "pieChart", OWF_CHART_PIE }, { "pie3DChart", OWF_CHART_PIE }, { "doughnutChart", OWF_CHART_PIE },
        { "ofPieChart", OWF_CHART_PIE }, { "scatterChart", OWF_CHART_SCATTER }, { "radarChart", OWF_CHART_OTHER },
        { "bubbleChart", OWF_CHART_OTHER }, { "stockChart", OWF_CHART_OTHER }, { "surfaceChart", OWF_CHART_OTHER },
        { "surface3DChart", OWF_CHART_OTHER }
    };
    const owf_xml_node *chart = child(space, ns_c, "chart"), *plot = child(chart, ns_c, "plotArea"), *g = NULL, *n, *ser;
    const owf_xml_node *title = child(chart, ns_c, "title"), *legend = child(chart, ns_c, "legend"), *ax;
    const char *v;
    int count = 0;
    size_t i;

    for (n = plot ? plot->first : NULL; n && !g; n = n->next)
        for (i = 0; i < sizeof groups / sizeof *groups; i++)
            if (is(n, ns_c, groups[i].name)) {
                g = n;
                c->kind = groups[i].kind;
                break;
            }
    c->gap = 150;
    if (g) {
        if (c->kind == OWF_CHART_COLUMN && (v = val(child(g, ns_c, "barDir"))) != NULL && !strcmp(v, "bar"))
            c->kind = OWF_CHART_BAR;
        v = val(child(g, ns_c, "grouping"));
        c->grouping = !v ? OWF_CHART_CLUSTERED : !strcmp(v, "stacked") ? OWF_CHART_STACKED :
                      !strcmp(v, "percentStacked") ? OWF_CHART_PERCENT : OWF_CHART_CLUSTERED;
        if (child(g, ns_c, "gapWidth"))
            c->gap = (int)attr_long(child(g, ns_c, "gapWidth"), "val", 150);
        if (c->gap < 0 || c->gap > 500)
            c->gap = 150;
        for (ser = child(g, ns_c, "ser"); ser && count < MAX_SERIES; ser = next(ser, ns_c, "ser"))
            count++;
        if (count) {
            c->series = calloc((size_t)count, sizeof *c->series);
            if (!c->series) {
                *failed = 1;
                return;
            }
        }
        for (ser = child(g, ns_c, "ser"); ser && c->nseries < count && !*failed; ser = next(ser, ns_c, "ser")) {
            owf_chart_series *s = &c->series[c->nseries];
            read_labels(s, child(g, ns_c, "dLbls"), failed);       /* the group's, then the series' own */
            read_series(s, ser, c->kind, c->nseries, failed);
            c->nseries++;
        }
        /* pies: the hole, the first slice's angle, a colour each */
        if (is(g, ns_c, "doughnutChart")) {
            c->hole = (int)attr_long(child(g, ns_c, "holeSize"), "val", 50);
            if (c->hole < 10 || c->hole > 90)
                c->hole = c->hole < 10 ? 10 : 90;
        }
        c->first_angle = (int)(attr_long(child(g, ns_c, "firstSliceAng"), "val", 0) % 360);
        if (c->first_angle < 0)
            c->first_angle += 360;
        if (child(g, ns_c, "varyColors"))
            c->vary = !(v = val(child(g, ns_c, "varyColors"))) || !strcmp(v, "1") || !strcmp(v, "true");
        else
            c->vary = c->kind == OWF_CHART_PIE;
        /* lines' markers (a line chart's c:marker 0: none), a scatter's style (lines only, markers only) */
        for (count = 0; count < c->nseries; count++) {
            owf_chart_series *s = &c->series[count];
            if (c->kind == OWF_CHART_LINE && (v = val(child(g, ns_c, "marker"))) != NULL &&
                (!strcmp(v, "0") || !strcmp(v, "false")) && s->marker == OWF_MARKER_AUTO)
                s->marker = OWF_MARKER_NONE;
            if (c->kind == OWF_CHART_SCATTER && (v = val(child(g, ns_c, "scatterStyle"))) != NULL) {
                if ((!strcmp(v, "line") || !strcmp(v, "smooth")) && s->marker == OWF_MARKER_AUTO)
                    s->marker = OWF_MARKER_NONE;
                if (!strcmp(v, "marker") || !strcmp(v, "none"))
                    s->line = 0;
            }
        }
    }
    /* the title: its text, a reference's cache, or (none given) the only series' name */
    if (title && !*failed) {
        owf_buf b;
        const owf_xml_node *f, *tx = child(title, ns_c, "tx"), *cache = data_source(tx, &f);
        owf_buf_init(&b);
        if (cache)
            all_text(cache, &b);
        else
            all_text(child(tx, ns_c, "rich"), &b);
        if (b.length || b.failed)
            c->title = dup_buf(&b, failed);
        else if (c->nseries == 1 && c->series[0].name)
            c->title = dup_str(c->series[0].name, failed);
        else if (!tx)
            c->title = dup_str("Chart Title", failed);
        owf_buf_free(&b);
    }
    if (legend) {
        v = val(child(legend, ns_c, "legendPos"));
        c->legend = !v || !strcmp(v, "r") || !strcmp(v, "tr") ? 'r' : !strcmp(v, "l") ? 'l' : !strcmp(v, "t") ? 't' : 'b';
    }
    /* the value axis: its gridlines and its limits; a scatter's two, x the group's first axis (by its id) */
    v = val(child(g, ns_c, "axId"));
    for (ax = child(plot, ns_c, "valAx"), count = 0; ax; ax = next(ax, ns_c, "valAx"), count++) {
        const owf_xml_node *sc = child(ax, ns_c, "scaling");
        const char *pos = val(child(ax, ns_c, "axPos")), *id = val(child(ax, ns_c, "axId"));
        int x_axis = c->kind == OWF_CHART_SCATTER &&
                     (v && id ? !strcmp(v, id) : pos ? !strcmp(pos, "b") || !strcmp(pos, "t") : count == 0);
        if (x_axis) {
            c->x_format = format_of(child(ax, ns_c, "numFmt"), failed);
            c->x_gridlines = child(ax, ns_c, "majorGridlines") != NULL;
            c->has_xmin = attr_double(child(sc, ns_c, "min"), &c->xmin);
            c->has_xmax = attr_double(child(sc, ns_c, "max"), &c->xmax);
        } else if (c->kind == OWF_CHART_SCATTER || count == 0) {          /* others: the first, the primary */
            free(c->value_format);
            c->value_format = format_of(child(ax, ns_c, "numFmt"), failed);
            c->gridlines = child(ax, ns_c, "majorGridlines") != NULL;
            c->has_min = attr_double(child(sc, ns_c, "min"), &c->min);
            c->has_max = attr_double(child(sc, ns_c, "max"), &c->max);
        }
    }
}

/* A column's (row's) width in twips, as the grid shows it. */
static long line_twips(const owf_sheet *s, int axis, unsigned i)
{
    int k;
    if (axis) {
        for (k = s->ncols - 1; k >= 0; k--)
            if (i >= s->cols[k].first && i <= s->cols[k].last)
                return s->cols[k].hidden ? 0 : s->cols[k].width > 0 ? s->cols[k].width : s->default_width;
        return s->default_width;
    }
    for (k = 0; k < s->nrows; k++)
        if (s->rows[k].row == i)
            return s->rows[k].hidden ? 0 : s->rows[k].height > 0 ? s->rows[k].height : s->default_height;
    return s->default_height;
}

/* An absolute anchor's offset from the sheet's corner (EMU) as a cell and an offset in it. */
static void cell_at(const owf_sheet *s, int axis, long emu, unsigned *index, long *off)
{
    unsigned i = 0, last = axis ? 16383U : 1048575U;
    long t;
    if (emu < 0)
        emu = 0;
    while (i < last && i < 200000U && emu >= (t = line_twips(s, axis, i) * EMU_PER_TWIP)) {
        emu -= t;
        i++;
    }
    *index = i;
    *off = emu;
}

static void read_marker(const owf_xml_node *m, unsigned *col, long *col_off, unsigned *row, long *row_off)
{
    long v = number_of(child(m, ns_xdr, "col"), 0);
    *col = v < 0 ? 0 : v > 16383 ? 16383U : (unsigned)v;
    v = number_of(child(m, ns_xdr, "row"), 0);
    *row = v < 0 ? 0 : v > 1048575L ? 1048575U : (unsigned)v;
    *col_off = number_of(child(m, ns_xdr, "colOff"), 0);
    *row_off = number_of(child(m, ns_xdr, "rowOff"), 0);
    if (*col_off < 0)
        *col_off = 0;
    if (*row_off < 0)
        *row_off = 0;
}

/* The relationships' part of a part, parsed, or NULL. */
static owf_xml *rels_of(const owf_book *book, const char *part, char *dir, size_t dir_size, int *failed)
{
    const char *slash = strrchr(part, '/');
    char name[1100];
    const owf_part *p;
    owf_xml *xml;
    int e = OWF_OK;
    snprintf(dir, dir_size, "%.*s", slash ? (int)(slash - part + 1) : 0, part);
    snprintf(name, sizeof name, "%s_rels/%s.rels", dir, slash ? slash + 1 : part);
    if (!(p = owf_book_part(book, name)))
        return NULL;
    xml = owf_xml_parse((const char *)p->data, p->length, &e);
    if (!xml && e == OWF_ERR_MEMORY)
        *failed = 1;
    return xml;
}

/* The part a relationship id points to, from a part's parsed relationships. */
static const owf_part *rel_target(const owf_book *book, const owf_xml *rels, const char *dir, const char *id)
{
    const owf_xml_node *n;
    char name[512];
    for (n = rels && id ? owf_xml_root(rels)->first : NULL; n; n = n->next) {
        const char *rid, *target, *mode;
        if (n->type != OWF_XML_ELEMENT || strcmp(n->name, "Relationship"))
            continue;
        rid = owf_xml_attr(n, "", "Id");
        target = owf_xml_attr(n, "", "Target");
        mode = owf_xml_attr(n, "", "TargetMode");
        if (rid && target && !strcmp(rid, id) && !(mode && !strcmp(mode, "External")) &&
            owf_opc_part_name(name, sizeof name, dir, target))
            return owf_book_part(book, name);
    }
    return NULL;
}

void owf_charts_free(owf_chart *charts, int count)
{
    int i, k, p;
    for (i = 0; charts && i < count; i++) {
        for (k = 0; k < charts[i].nseries; k++) {
            owf_chart_series *s = &charts[i].series[k];
            for (p = 0; p < s->npoints; p++)
                free(s->points[p].label);
            free(s->points);
            free(s->name);
            free(s->name_cell);
            free(s->label_format);
            free(s->format);
            free(s->x_values);
            free(s->values);
            free(s->categories);
        }
        free(charts[i].series);
        free(charts[i].title);
        free(charts[i].value_format);
        free(charts[i].x_format);
    }
    free(charts);
}

int owf_sheet_charts(const owf_book *book, int sheet, owf_chart **charts, int *count)
{
    const owf_sheet *s;
    const owf_part *dp;
    owf_xml *drawing = NULL, *rels = NULL;
    const owf_xml_node *a;
    owf_chart *out = NULL;
    char dir[512];
    int n = 0, cap = 0, failed = 0, e = OWF_OK;

    *charts = NULL;
    *count = 0;
    if (!book || sheet < 0 || sheet >= book->nsheets)
        return OWF_OK;
    s = &book->sheets[sheet];
    if (!s->drawing || !(dp = owf_book_part(book, s->drawing)))
        return OWF_OK;
    drawing = owf_xml_parse((const char *)dp->data, dp->length, &e);
    if (!drawing)
        return e == OWF_ERR_MEMORY ? OWF_ERR_MEMORY : OWF_OK;
    rels = rels_of(book, s->drawing, dir, sizeof dir, &failed);
    for (a = owf_xml_root(drawing)->first; a && !failed && n < MAX_CHARTS; a = a->next) {
        const owf_xml_node *frame, *ref, *from;
        const owf_part *cp;
        owf_xml *cx;
        owf_chart *c;
        int kind;
        if (is(a, ns_xdr, "twoCellAnchor"))
            kind = 0;
        else if (is(a, ns_xdr, "oneCellAnchor"))
            kind = 1;
        else if (is(a, ns_xdr, "absoluteAnchor"))
            kind = 2;
        else
            continue;
        frame = child(a, ns_xdr, "graphicFrame");
        ref = child(child(child(frame, ns_a, "graphic"), ns_a, "graphicData"), ns_c, "chart");
        if (!ref)
            continue;                            /* a picture, a shape, a group */
        cp = rel_target(book, rels, dir, owf_xml_attr(ref, ns_r[0], "id") ? owf_xml_attr(ref, ns_r[0], "id")
                                                                          : owf_xml_attr(ref, ns_r[1], "id"));
        if (!cp)
            continue;
        cx = owf_xml_parse((const char *)cp->data, cp->length, &e);
        if (!cx) {
            if (e == OWF_ERR_MEMORY)
                failed = 1;
            continue;
        }
        if (n == cap) {
            owf_chart *p = realloc(out, (size_t)(cap ? cap * 2 : 4) * sizeof *out);
            if (!p) {
                failed = 1;
                owf_xml_free(cx);
                break;
            }
            out = p;
            cap = cap ? cap * 2 : 4;
        }
        c = &out[n++];
        memset(c, 0, sizeof *c);
        from = child(a, ns_xdr, "from");
        if (kind == 2) {
            const owf_xml_node *pos = child(a, ns_xdr, "pos");
            cell_at(s, 1, attr_long(pos, "x", 0), &c->from_col, &c->from_col_off);
            cell_at(s, 0, attr_long(pos, "y", 0), &c->from_row, &c->from_row_off);
        } else {
            read_marker(from, &c->from_col, &c->from_col_off, &c->from_row, &c->from_row_off);
        }
        if (kind == 0) {
            read_marker(child(a, ns_xdr, "to"), &c->to_col, &c->to_col_off, &c->to_row, &c->to_row_off);
            if (c->to_row < c->from_row || (c->to_row == c->from_row && c->to_row_off < c->from_row_off) ||
                c->to_col < c->from_col || (c->to_col == c->from_col && c->to_col_off < c->from_col_off)) {
                c->to_row = c->from_row;         /* a damaged anchor: nothing to show */
                c->to_col = c->from_col;
                c->to_row_off = c->from_row_off;
                c->to_col_off = c->from_col_off;
            }
        } else {
            const owf_xml_node *ext = child(a, ns_xdr, "ext");
            c->sized = 1;
            c->width = attr_long(ext, "cx", 0);
            c->height = attr_long(ext, "cy", 0);
            if (c->width < 0)
                c->width = 0;
            if (c->height < 0)
                c->height = 0;
        }
        read_chart(c, owf_xml_root(cx), &failed);
        owf_xml_free(cx);
    }
    owf_xml_free(rels);
    owf_xml_free(drawing);
    if (failed) {
        owf_charts_free(out, n);
        return OWF_ERR_MEMORY;
    }
    *charts = out;
    *count = n;
    return OWF_OK;
}

/* ---- Writing: a chart as an .xlsx's chart part (DrawingML), as read above ---- */

static void put_text(owf_buf *b, const char *s)
{
    for (; s && *s; s++)
        switch (*s) {
        case '&': owf_buf_puts(b, "&amp;"); break;
        case '<': owf_buf_puts(b, "&lt;"); break;
        case '>': owf_buf_puts(b, "&gt;"); break;
        case '"': owf_buf_puts(b, "&quot;"); break;
        default: owf_buf_putc(b, *s);
        }
}

static void put_rgb(owf_buf *b, unsigned long shade)
{
    owf_buf_printf(b, "<a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", shade & 0xFFFFFFUL);
}

/* A series' tx, cat, val (xVal, yVal): its reference and what it last held, as a cache. */
static void put_source(owf_buf *b, const char *element, const char *ref, const owf_chart_series *s, int which,
                       const char *format)
{
    int i, n = 0, numbers = which != 0 && which != 1;     /* 0 the name, 1 the categories, 2 the values, 3 x values */
    char num[40];
    for (i = 0; i < s->npoints; i++)
        if (which == 1 ? s->points[i].label != NULL : which == 2 ? s->points[i].has_value : which == 3 && s->points[i].has_x)
            n = i + 1;
    if (!ref && (which == 0 ? !s->name : !n))
        return;
    owf_buf_printf(b, "<c:%s>", element);
    if (ref) {
        owf_buf_printf(b, "<c:%s><c:f>", numbers ? "numRef" : "strRef");
        put_text(b, ref);
        owf_buf_printf(b, "</c:f><c:%s>", numbers ? "numCache" : "strCache");
    } else {
        owf_buf_printf(b, "<c:%s>", numbers ? "numLit" : "strLit");
    }
    if (numbers && format && *format) {
        owf_buf_puts(b, "<c:formatCode>");
        put_text(b, format);
        owf_buf_puts(b, "</c:formatCode>");
    }
    if (which == 0) {
        owf_buf_puts(b, "<c:ptCount val=\"1\"/>");
        if (s->name) {
            owf_buf_puts(b, "<c:pt idx=\"0\"><c:v>");
            put_text(b, s->name);
            owf_buf_puts(b, "</c:v></c:pt>");
        }
    } else {
        owf_buf_printf(b, "<c:ptCount val=\"%d\"/>", n);
        for (i = 0; i < n; i++) {
            const owf_chart_point *p = &s->points[i];
            if (which == 1 && p->label) {
                owf_buf_printf(b, "<c:pt idx=\"%d\"><c:v>", i);
                put_text(b, p->label);
                owf_buf_puts(b, "</c:v></c:pt>");
            } else if ((which == 2 && p->has_value) || (which == 3 && p->has_x)) {
                owf_number_exact(which == 2 ? p->value : p->x, num);
                owf_buf_printf(b, "<c:pt idx=\"%d\"><c:v>%s</c:v></c:pt>", i, num);
            }
        }
    }
    owf_buf_printf(b, "</c:%s>", ref ? (numbers ? "numCache" : "strCache") : (numbers ? "numLit" : "strLit"));
    if (ref)
        owf_buf_printf(b, "</c:%s>", numbers ? "numRef" : "strRef");
    owf_buf_printf(b, "</c:%s>", element);
}

static void put_labels(owf_buf *b, const owf_chart_series *s)
{
    static const char *const pos[] = { "", "ctr", "inEnd", "inBase", "outEnd", "t", "b", "l", "r", "bestFit" };
    if (!s->labels)
        return;
    owf_buf_puts(b, "<c:dLbls>");
    if (s->label_format) {
        owf_buf_puts(b, "<c:numFmt formatCode=\"");
        put_text(b, s->label_format);
        owf_buf_puts(b, "\" sourceLinked=\"0\"/>");
    }
    if (s->label_pos > 0 && s->label_pos <= OWF_LABEL_BEST_FIT)
        owf_buf_printf(b, "<c:dLblPos val=\"%s\"/>", pos[s->label_pos]);
    owf_buf_printf(b, "<c:showLegendKey val=\"0\"/><c:showVal val=\"%d\"/><c:showCatName val=\"%d\"/><c:showSerName val=\"%d\"/>"
                      "<c:showPercent val=\"%d\"/><c:showBubbleSize val=\"0\"/></c:dLbls>",
                   !!(s->labels & OWF_LABEL_VALUE), !!(s->labels & OWF_LABEL_CATEGORY), !!(s->labels & OWF_LABEL_SERIES),
                   !!(s->labels & OWF_LABEL_PERCENT));
}

static void put_series(owf_buf *b, const owf_chart *c, const owf_chart_series *s, int number)
{
    static const char *const markers[] = { "auto", "none", "circle", "square", "diamond", "triangle", "x", "star", "plus",
                                           "dash", "dot" };
    int lines = c->kind == OWF_CHART_LINE || c->kind == OWF_CHART_SCATTER, i;
    owf_buf_printf(b, "<c:ser><c:idx val=\"%d\"/><c:order val=\"%d\"/>", s->index, number);
    if (s->name_cell || s->name) {
        if (s->name_cell) {
            put_source(b, "tx", s->name_cell, s, 0, NULL);
        } else {
            owf_buf_puts(b, "<c:tx><c:v>");
            put_text(b, s->name);
            owf_buf_puts(b, "</c:v></c:tx>");
        }
    }
    if (s->colour || (lines && !s->line)) {
        owf_buf_puts(b, "<c:spPr>");
        if (s->colour && !lines)
            put_rgb(b, s->colour);
        if (lines) {
            owf_buf_puts(b, "<a:ln>");
            if (!s->line)
                owf_buf_puts(b, "<a:noFill/>");
            else
                put_rgb(b, s->colour);
            owf_buf_puts(b, "</a:ln>");
        }
        owf_buf_puts(b, "</c:spPr>");
    }
    if (lines && (s->marker != OWF_MARKER_AUTO || s->marker_size)) {
        owf_buf_printf(b, "<c:marker><c:symbol val=\"%s\"/>", markers[s->marker >= 0 && s->marker <= OWF_MARKER_DOT ? s->marker : 0]);
        if (s->marker_size)
            owf_buf_printf(b, "<c:size val=\"%d\"/>", s->marker_size);
        owf_buf_puts(b, "</c:marker>");
    }
    for (i = 0; i < s->npoints; i++)
        if (OWF_SHADE_SET(s->points[i].colour)) {
            owf_buf_printf(b, "<c:dPt><c:idx val=\"%d\"/>%s<c:spPr>", i, c->kind == OWF_CHART_PIE ? "<c:bubble3D val=\"0\"/>" : "");
            put_rgb(b, s->points[i].colour);
            owf_buf_puts(b, "</c:spPr></c:dPt>");
        }
    put_labels(b, s);
    if (c->kind == OWF_CHART_SCATTER) {
        put_source(b, "xVal", s->x_values, s, 3, NULL);
        put_source(b, "yVal", s->values, s, 2, s->format);
    } else {
        put_source(b, "cat", s->categories, s, 1, NULL);
        put_source(b, "val", s->values, s, 2, s->format);
    }
    if (lines)
        owf_buf_puts(b, "<c:smooth val=\"0\"/>");
    owf_buf_puts(b, "</c:ser>");
}

static void put_value_axis(owf_buf *b, int id, int cross, const char *pos, int gridlines, int has_min, double min, int has_max,
                           double max, const char *format)
{
    char num[40];
    owf_buf_printf(b, "<c:valAx><c:axId val=\"%d\"/><c:scaling><c:orientation val=\"minMax\"/>", id);
    if (has_max) {
        owf_number_exact(max, num);
        owf_buf_printf(b, "<c:max val=\"%s\"/>", num);
    }
    if (has_min) {
        owf_number_exact(min, num);
        owf_buf_printf(b, "<c:min val=\"%s\"/>", num);
    }
    owf_buf_printf(b, "</c:scaling><c:delete val=\"0\"/><c:axPos val=\"%s\"/>", pos);
    if (gridlines)
        owf_buf_puts(b, "<c:majorGridlines/>");
    if (format) {
        owf_buf_puts(b, "<c:numFmt formatCode=\"");
        put_text(b, format);
        owf_buf_puts(b, "\" sourceLinked=\"0\"/>");
    } else {
        owf_buf_puts(b, "<c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>");
    }
    owf_buf_printf(b, "<c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>"
                      "<c:crossAx val=\"%d\"/><c:crosses val=\"autoZero\"/>", cross);
    owf_buf_puts(b, "</c:valAx>");
}

int owf_chart_part_xml(const owf_chart *c, owf_buf *b)
{
    static const char *const groupings[] = { "clustered", "stacked", "percentStacked" };
    int i, pie = c->kind == OWF_CHART_PIE, scatter = c->kind == OWF_CHART_SCATTER;
    const char *g = c->kind == OWF_CHART_LINE ? "lineChart" : c->kind == OWF_CHART_AREA ? "areaChart" :
                    pie ? (c->hole ? "doughnutChart" : "pieChart") : scatter ? "scatterChart" : "barChart";
    int grouping = c->grouping >= 0 && c->grouping <= OWF_CHART_PERCENT ? c->grouping : 0;
    owf_buf_puts(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                    "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
                    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><c:chart>");
    if (c->title) {
        owf_buf_puts(b, "<c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>");
        put_text(b, c->title);
        owf_buf_puts(b, "</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title><c:autoTitleDeleted val=\"0\"/>");
    } else {
        owf_buf_puts(b, "<c:autoTitleDeleted val=\"1\"/>");
    }
    owf_buf_printf(b, "<c:plotArea><c:layout/><c:%s>", g);
    if (!strcmp(g, "barChart"))
        owf_buf_printf(b, "<c:barDir val=\"%s\"/>", c->kind == OWF_CHART_BAR ? "bar" : "col");
    if (scatter)
        owf_buf_printf(b, "<c:scatterStyle val=\"lineMarker\"/>");
    else if (!pie)
        owf_buf_printf(b, "<c:grouping val=\"%s\"/>", c->kind == OWF_CHART_COLUMN || c->kind == OWF_CHART_BAR || grouping ?
                                                      groupings[grouping] : "standard");
    owf_buf_printf(b, "<c:varyColors val=\"%d\"/>", c->vary ? 1 : 0);
    for (i = 0; i < c->nseries; i++)
        put_series(b, c, &c->series[i], i);
    if (!strcmp(g, "barChart")) {
        owf_buf_printf(b, "<c:gapWidth val=\"%d\"/>", c->gap > 0 ? c->gap : 150);
        if (grouping)
            owf_buf_puts(b, "<c:overlap val=\"100\"/>");
    }
    if (c->kind == OWF_CHART_LINE)
        owf_buf_puts(b, "<c:marker val=\"1\"/>");
    if (pie) {
        owf_buf_printf(b, "<c:firstSliceAng val=\"%d\"/>", c->first_angle);
        if (c->hole)
            owf_buf_printf(b, "<c:holeSize val=\"%d\"/>", c->hole);
    } else {
        owf_buf_puts(b, "<c:axId val=\"500000001\"/><c:axId val=\"500000002\"/>");
    }
    owf_buf_printf(b, "</c:%s>", g);
    if (scatter) {                                   /* x the group's first axis, along the bottom */
        put_value_axis(b, 500000001, 500000002, "b", c->x_gridlines, c->has_xmin, c->xmin, c->has_xmax, c->xmax, c->x_format);
        put_value_axis(b, 500000002, 500000001, "l", c->gridlines, c->has_min, c->min, c->has_max, c->max, c->value_format);
    } else if (!pie) {
        int bar = c->kind == OWF_CHART_BAR;
        owf_buf_printf(b, "<c:catAx><c:axId val=\"500000001\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
                          "<c:delete val=\"0\"/><c:axPos val=\"%s\"/><c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>"
                          "<c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/><c:tickLblPos val=\"nextTo\"/>"
                          "<c:crossAx val=\"500000002\"/><c:crosses val=\"autoZero\"/><c:auto val=\"1\"/>"
                          "<c:lblAlgn val=\"ctr\"/><c:lblOffset val=\"100\"/></c:catAx>", bar ? "l" : "b");
        put_value_axis(b, 500000002, 500000001, bar ? "b" : "l", c->gridlines, c->has_min, c->min, c->has_max, c->max,
                       c->value_format);
    }
    owf_buf_puts(b, "</c:plotArea>");
    if (c->legend)
        owf_buf_printf(b, "<c:legend><c:legendPos val=\"%c\"/><c:overlay val=\"0\"/></c:legend>", c->legend);
    owf_buf_puts(b, "<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart></c:chartSpace>");
    return b->failed ? OWF_ERR_MEMORY : OWF_OK;
}

int owf_chart_drawing_xml(const owf_chart *charts, int n, owf_buf *b)
{
    int i;
    owf_buf_puts(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                    "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
                    "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                    "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">");
    for (i = 0; i < n; i++) {
        const owf_chart *c = &charts[i];
        owf_buf_printf(b, "<xdr:%s><xdr:from><xdr:col>%u</xdr:col><xdr:colOff>%ld</xdr:colOff><xdr:row>%u</xdr:row>"
                          "<xdr:rowOff>%ld</xdr:rowOff></xdr:from>",
                       c->sized ? "oneCellAnchor" : "twoCellAnchor", c->from_col, c->from_col_off, c->from_row, c->from_row_off);
        if (c->sized)
            owf_buf_printf(b, "<xdr:ext cx=\"%ld\" cy=\"%ld\"/>", c->width, c->height);
        else
            owf_buf_printf(b, "<xdr:to><xdr:col>%u</xdr:col><xdr:colOff>%ld</xdr:colOff><xdr:row>%u</xdr:row>"
                              "<xdr:rowOff>%ld</xdr:rowOff></xdr:to>",
                           c->to_col, c->to_col_off, c->to_row, c->to_row_off);
        owf_buf_printf(b, "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"%d\" name=\"Chart %d\"/>"
                          "<xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr><xdr:xfrm><a:off x=\"0\" y=\"0\"/>"
                          "<a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm><a:graphic>"
                          "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
                          "<c:chart r:id=\"rId%d\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/></xdr:%s>",
                       i + 2, i + 1, i + 1, c->sized ? "oneCellAnchor" : "twoCellAnchor");
    }
    owf_buf_puts(b, "</xdr:wsDr>");
    return b->failed ? OWF_ERR_MEMORY : OWF_OK;
}

/* A sheet's charts kept as an .xlsx's drawing would keep them: a drawing part (xl/drawings/drawing<k>.xml, the
 * first k free), its relationships and a chart part each (xl/charts/chart<j>.xml), the sheet's drawing named. */
int owf_sheet_keep_charts(owf_book *book, int sheet, const owf_chart *charts, int n)
{
    static const char *const ct_drawing = "application/vnd.openxmlformats-officedocument.drawing+xml",
                             *ct_chart = "application/vnd.openxmlformats-officedocument.drawingml.chart+xml",
                             *ct_rels = "application/vnd.openxmlformats-package.relationships+xml";
    char drawing[64], name[128];
    owf_buf b, rels;
    int k = 1, j = 1, i, r = OWF_OK;
    if (!n || sheet < 0 || sheet >= book->nsheets || book->sheets[sheet].drawing)
        return OWF_OK;
    do
        snprintf(drawing, sizeof drawing, "xl/drawings/drawing%d.xml", k++);
    while (owf_book_part(book, drawing));
    owf_buf_init(&b);
    owf_buf_init(&rels);
    owf_buf_puts(&rels, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (i = 0; i < n && r == OWF_OK; i++) {
        do
            snprintf(name, sizeof name, "xl/charts/chart%d.xml", j++);
        while (owf_book_part(book, name));
        b.length = 0;
        if ((r = owf_chart_part_xml(&charts[i], &b)) != OWF_OK)
            break;
        if (owf_book_keep_part(book, name, ct_chart, b.data, b.length) < 0) {
            r = OWF_ERR_MEMORY;
            break;
        }
        owf_buf_printf(&rels, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                              "relationships/chart\" Target=\"../charts/%s\"/>", i + 1, name + 10);
    }
    owf_buf_puts(&rels, "</Relationships>");
    b.length = 0;
    if (r == OWF_OK && (rels.failed || (r = owf_chart_drawing_xml(charts, n, &b)) != OWF_OK))
        r = OWF_ERR_MEMORY;
    if (r == OWF_OK) {
        snprintf(name, sizeof name, "xl/drawings/_rels/%s.rels", drawing + 12);
        if (owf_book_keep_part(book, drawing, ct_drawing, b.data, b.length) < 0 ||
            owf_book_keep_part(book, name, ct_rels, rels.data, rels.length) < 0 ||
            !(book->sheets[sheet].drawing = malloc(strlen(drawing) + 1)))
            r = OWF_ERR_MEMORY;
        else
            strcpy(book->sheets[sheet].drawing, drawing);
    }
    owf_buf_free(&b);
    owf_buf_free(&rels);
    return r;
}

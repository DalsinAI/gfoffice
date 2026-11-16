/*
 * libowf: a slide's chart as OpenDocument keeps it (Presentation gap 2): an
 * embedded chart object's content.xml (office:chart), its data in its own
 * table (table:table "local-table") and its series pointing at that table's
 * cells, as LibreOffice Impress writes one. Read into an owf_chart with the
 * points the table holds, and written back so. A sheet's charts (ODS) point
 * at the book's cells and are in_ods.c's and out_ods.c's; this is the
 * deck's, with no book.
 *
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_OFFICE "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
#define NS_STYLE "urn:oasis:names:tc:opendocument:xmlns:style:1.0"
#define NS_TEXT "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
#define NS_TABLE "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
#define NS_DRAW "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
#define NS_CHART "urn:oasis:names:tc:opendocument:xmlns:chart:1.0"
#define NS_SVG "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
#define NS_FO "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
#define MAX_ROWS 4096
#define MAX_SERIES 64

/* ---- Reading ---- */

static char *dup_n(const char *s, size_t n, int *failed)
{
    char *t = malloc(n + 1);
    if (!t) {
        *failed = 1;
        return NULL;
    }
    memcpy(t, s, n);
    t[n] = 0;
    return t;
}

/* An element's text, its descendants' joined (text:p, text:span...), into b. */
static void text_of(const owf_xml_node *n, owf_buf *b, int depth)
{
    const owf_xml_node *c;
    if (!n || depth > 16)
        return;
    for (c = n->first; c; c = c->next)
        if (c->type == OWF_XML_TEXT)
            owf_buf_put(b, c->name, c->length);
        else if (owf_xml_is(c, NS_TEXT, "s"))
            owf_buf_putc(b, ' ');
        else
            text_of(c, b, depth + 1);
}

static char *text_dup(const owf_xml_node *n, int *failed)
{
    owf_buf b;
    char *s = NULL;
    owf_buf_init(&b);
    text_of(n, &b, 0);
    if (b.failed)
        *failed = 1;
    else if (b.length)
        s = dup_n((const char *)b.data, b.length, failed);
    owf_buf_free(&b);
    return s;
}

/* A cell address's column and row (from 1): "local-table.$B$2", ".B2", "B2". 0 when it is none. */
static const char *cell_of(const char *s, int *col, int *row)
{
    int c = 0, r = 0;
    const char *dot;
    if (!s)
        return NULL;
    while (*s == ' ')
        s++;
    if ((dot = strchr(s, '.')) && (!strchr(s, ':') || dot < strchr(s, ':')))
        s = dot + 1;
    if (*s == '$') s++;
    while (*s >= 'A' && *s <= 'Z' && c < 18279)
        c = c * 26 + (*s++ - 'A' + 1);
    if (*s == '$') s++;
    while (*s >= '0' && *s <= '9' && r < 1048577)
        r = r * 10 + (*s++ - '0');
    if (!c || !r)
        return NULL;
    *col = c;
    *row = r;
    return s;
}

/* A range "t.$B$2:.$B$4" (or one cell): its corners. 0 when it is none. */
static int range_of(const char *s, int *c0, int *r0, int *c1, int *r1)
{
    const char *e = cell_of(s, c0, r0);
    if (!e)
        return 0;
    *c1 = *c0;
    *r1 = *r0;
    if (*e == ':' && !cell_of(e + 1, c1, r1))
        return 0;
    if (*c1 < *c0 || *r1 < *r0)
        return 0;
    return 1;
}

/* The local table's cells, by row and column from 1 (the header rows first), repeats worked out. */
typedef struct {
    const owf_xml_node **cells;      /* rows * cols, NULL: empty */
    int rows, cols;
} grid;

static void grid_row(grid *g, const owf_xml_node *row, int r)
{
    const owf_xml_node *c;
    int col = 0;
    for (c = row->first; c && col < g->cols; c = c->next) {
        const char *rep;
        int n = 1, k;
        if (!owf_xml_is(c, NS_TABLE, "table-cell") && !owf_xml_is(c, NS_TABLE, "covered-table-cell"))
            continue;
        if ((rep = owf_xml_attr(c, NS_TABLE, "number-columns-repeated")) && atoi(rep) > 1)
            n = atoi(rep);
        for (k = 0; k < n && col < g->cols; k++, col++)
            g->cells[r * g->cols + col] = c;
    }
}

static void grid_rows(grid *g, const owf_xml_node *parent, int *r, int depth)
{
    const owf_xml_node *n;
    if (!parent || depth > 4)
        return;
    for (n = parent->first; n && *r < g->rows; n = n->next) {
        if (owf_xml_is(n, NS_TABLE, "table-row")) {
            const char *rep = owf_xml_attr(n, NS_TABLE, "number-rows-repeated");
            int times = rep && atoi(rep) > 1 ? atoi(rep) : 1, k;
            for (k = 0; k < times && *r < g->rows; k++)
                grid_row(g, n, (*r)++);
        } else if (owf_xml_is(n, NS_TABLE, "table-header-rows") || owf_xml_is(n, NS_TABLE, "table-rows") ||
                   owf_xml_is(n, NS_TABLE, "table-row-group")) {
            grid_rows(g, n, r, depth + 1);
        }
    }
}

static int grid_make(grid *g, const owf_xml_node *table, int rows, int cols)
{
    int r = 0;
    memset(g, 0, sizeof *g);
    if (rows <= 0 || cols <= 0 || rows > MAX_ROWS + 1 || cols > MAX_SERIES + 1)
        return 1;
    if (!(g->cells = calloc((size_t)rows * (size_t)cols, sizeof *g->cells)))
        return 0;
    g->rows = rows;
    g->cols = cols;
    grid_rows(g, table, &r, 0);
    return 1;
}

static const owf_xml_node *grid_at(const grid *g, int col, int row)
{
    if (col < 1 || row < 1 || col > g->cols || row > g->rows)
        return NULL;
    return g->cells[(row - 1) * g->cols + (col - 1)];
}

static int cell_number(const owf_xml_node *c, double *v)
{
    const char *t = c ? owf_xml_attr(c, NS_OFFICE, "value-type") : NULL, *s = c ? owf_xml_attr(c, NS_OFFICE, "value") : NULL;
    if (!t || !s || (strcmp(t, "float") && strcmp(t, "percentage") && strcmp(t, "currency")))
        return 0;
    return owf_number_parse(s, strlen(s), v);
}

/* A style's chart-properties in the document's automatic styles, or NULL. */
static const owf_xml_node *chart_props(const owf_xml_node *autos, const char *name, const char *which)
{
    const owf_xml_node *s;
    if (!autos || !name)
        return NULL;
    for (s = owf_xml_child(autos, NS_STYLE, "style"); s; s = owf_xml_next(s, NS_STYLE, "style")) {
        const char *n = owf_xml_attr(s, NS_STYLE, "name");
        if (n && !strcmp(n, name))
            return owf_xml_child(s, NS_STYLE, which);
    }
    return NULL;
}

static int is_true(const char *s)
{
    return s && !strcmp(s, "true");
}

static int kind_of(const char *cls, int vertical, int *hole)
{
    if (cls && !strncmp(cls, "chart:", 6))
        cls += 6;
    *hole = 0;
    if (!cls) return OWF_CHART_OTHER;
    if (!strcmp(cls, "bar")) return vertical ? OWF_CHART_BAR : OWF_CHART_COLUMN;
    if (!strcmp(cls, "line")) return OWF_CHART_LINE;
    if (!strcmp(cls, "area")) return OWF_CHART_AREA;
    if (!strcmp(cls, "circle")) return OWF_CHART_PIE;
    if (!strcmp(cls, "ring")) { *hole = 50; return OWF_CHART_PIE; }
    if (!strcmp(cls, "scatter")) return OWF_CHART_SCATTER;
    return OWF_CHART_OTHER;
}

/* The points of one series: its values down column vc, rows r0..r1; its categories (or x) from column cc. */
static void read_points(owf_chart_series *s, const grid *g, int vc, int r0, int r1, int cc, int scatter, int *failed)
{
    int n = r1 - r0 + 1, k;
    if (n <= 0 || n > MAX_ROWS)
        return;
    if (!(s->points = calloc((size_t)n, sizeof *s->points))) {
        *failed = 1;
        return;
    }
    s->npoints = n;
    for (k = 0; k < n; k++) {
        owf_chart_point *p = &s->points[k];
        const owf_xml_node *a = cc ? grid_at(g, cc, r0 + k) : NULL;
        p->value = 0;
        p->has_value = cell_number(grid_at(g, vc, r0 + k), &p->value);
        if (a && scatter)
            p->has_x = cell_number(a, &p->x);
        else if (a)
            p->label = text_dup(a, failed);
    }
}

int owf_chart_from_odf(const char *xml, size_t length, owf_chart *c)
{
    owf_xml *x;
    const owf_xml_node *root, *autos, *chart, *plot, *t, *n, *table;
    const char *cls, *cats = NULL;
    int e = OWF_OK, failed = 0, vertical, hole, scatter, ns = 0, cc = 0;
    grid g;
    if (!c)
        return OWF_ERR_FORMAT;
    memset(c, 0, sizeof *c);
    if (!xml || !length)
        return OWF_ERR_FORMAT;
    if (!(x = owf_xml_parse(xml, length, &e)))
        return e == OWF_ERR_MEMORY ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    root = owf_xml_root(x);
    autos = owf_xml_child(root, NS_OFFICE, "automatic-styles");
    chart = owf_xml_child(owf_xml_child(owf_xml_child(root, NS_OFFICE, "body"), NS_OFFICE, "chart"), NS_CHART, "chart");
    plot = owf_xml_child(chart, NS_CHART, "plot-area");
    table = owf_xml_child(chart, NS_TABLE, "table");
    if (!chart || !plot) {
        owf_xml_free(x);
        return OWF_ERR_FORMAT;
    }
    {
        const owf_xml_node *pp = chart_props(autos, owf_xml_attr(plot, NS_CHART, "style-name"), "chart-properties");
        vertical = is_true(pp ? owf_xml_attr(pp, NS_CHART, "vertical") : NULL);
        c->grouping = is_true(pp ? owf_xml_attr(pp, NS_CHART, "percentage") : NULL) ? OWF_CHART_PERCENT
                    : is_true(pp ? owf_xml_attr(pp, NS_CHART, "stacked") : NULL) ? OWF_CHART_STACKED : OWF_CHART_CLUSTERED;
    }
    cls = owf_xml_attr(chart, NS_CHART, "class");
    c->kind = kind_of(cls, vertical, &hole);
    c->hole = hole;
    scatter = c->kind == OWF_CHART_SCATTER;
    if ((t = owf_xml_child(chart, NS_CHART, "title")))
        c->title = text_dup(t, &failed);
    if ((t = owf_xml_child(chart, NS_CHART, "legend"))) {
        const char *pos = owf_xml_attr(t, NS_CHART, "legend-position");
        c->legend = !pos || !strcmp(pos, "end") ? 'r' : !strcmp(pos, "start") ? 'l' : !strcmp(pos, "top") ? 't'
                  : !strcmp(pos, "bottom") ? 'b' : 'r';
    }
    for (n = owf_xml_child(plot, NS_CHART, "axis"); n; n = owf_xml_next(n, NS_CHART, "axis")) {
        const char *dim = owf_xml_attr(n, NS_CHART, "dimension");
        const owf_xml_node *k = owf_xml_child(n, NS_CHART, "categories");
        if (dim && !strcmp(dim, "x") && k)
            cats = owf_xml_attr(k, NS_TABLE, "cell-range-address");
        if (dim && !strcmp(dim, "x") && (t = owf_xml_child(n, NS_CHART, "title")))
            c->category_title = text_dup(t, &failed);
        if (dim && !strcmp(dim, "y") && (t = owf_xml_child(n, NS_CHART, "title")) && !c->value_title)
            c->value_title = text_dup(t, &failed);
        if (dim && !strcmp(dim, "y") && owf_xml_child(n, NS_CHART, "grid"))
            c->gridlines = 1;
    }
    for (n = owf_xml_child(plot, NS_CHART, "series"); n; n = owf_xml_next(n, NS_CHART, "series"))
        ns++;
    if (ns > MAX_SERIES)
        ns = MAX_SERIES;
    if (ns && !(c->series = calloc((size_t)ns, sizeof *c->series)))
        failed = 1;
    if (!failed && table) {
        /* the grid spans every address used */
        int rows = 1, cols = 1, a, b, a1, b1;
        if (cats && range_of(cats, &a, &b, &a1, &b1)) {
            cc = a;
            if (a1 > cols) cols = a1;
            if (b1 > rows) rows = b1;
        }
        for (n = owf_xml_child(plot, NS_CHART, "series"); n; n = owf_xml_next(n, NS_CHART, "series")) {
            const owf_xml_node *d = owf_xml_child(n, NS_CHART, "domain");
            const char *refs[3];
            int i;
            refs[0] = owf_xml_attr(n, NS_CHART, "values-cell-range-address");
            refs[1] = owf_xml_attr(n, NS_CHART, "label-cell-address");
            refs[2] = d ? owf_xml_attr(d, NS_TABLE, "cell-range-address") : NULL;
            for (i = 0; i < 3; i++)
                if (refs[i] && range_of(refs[i], &a, &b, &a1, &b1)) {
                    if (a1 > cols) cols = a1;
                    if (b1 > rows) rows = b1;
                }
        }
        if (rows > MAX_ROWS + 1) rows = MAX_ROWS + 1;
        if (cols > MAX_SERIES + 1) cols = MAX_SERIES + 1;
        if (!grid_make(&g, table, rows, cols))
            failed = 1;
    } else {
        memset(&g, 0, sizeof g);
    }
    for (n = owf_xml_child(plot, NS_CHART, "series"); n && !failed && c->nseries < ns; n = owf_xml_next(n, NS_CHART, "series")) {
        owf_chart_series *s = &c->series[c->nseries];
        const owf_xml_node *d = owf_xml_child(n, NS_CHART, "domain"), *sp;
        const char *v = owf_xml_attr(n, NS_CHART, "values-cell-range-address");
        const char *label = owf_xml_attr(n, NS_CHART, "label-cell-address");
        int vc, r0, vc1, r1, xc = cc, a, b;
        s->index = c->nseries;
        s->line = 1;
        if (label && cell_of(label, &a, &b))
            s->name = text_dup(grid_at(&g, a, b), &failed);
        if (scatter && d && range_of(owf_xml_attr(d, NS_TABLE, "cell-range-address"), &a, &b, &vc1, &r1))
            xc = a;
        if (v && range_of(v, &vc, &r0, &vc1, &r1) && g.cells)
            read_points(s, &g, vc, r0, r1, xc, scatter, &failed);
        sp = chart_props(autos, owf_xml_attr(n, NS_CHART, "style-name"), "graphic-properties");
        if (sp) {
            unsigned long rgb;
            const char *fill = owf_xml_attr(sp, NS_DRAW, "fill-color");
            if (!fill) fill = owf_xml_attr(sp, NS_SVG, "stroke-color");
            if (fill && owf_parse_colour(fill, &rgb))
                s->colour = OWF_SHADE(rgb);
        }
        c->nseries++;
    }
    free(g.cells);
    owf_xml_free(x);
    if (failed) {
        owf_chart_clear(c);
        return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

/* ---- Writing ---- */

static void col_letters(char *out, int col)        /* from 1: "A" */
{
    char t[8];
    int n = 0, i;
    for (; col > 0 && n < 7; col = (col - 1) / 26)
        t[n++] = (char)('A' + (col - 1) % 26);
    for (i = 0; i < n; i++)
        out[i] = t[n - 1 - i];
    out[n] = 0;
}

static void put_range(owf_buf *b, const char *attr, int col, int r0, int r1)
{
    char l[8];
    col_letters(l, col);
    if (r1 > r0)
        owf_buf_printf(b, " %s=\"local-table.$%s$%d:.$%s$%d\"", attr, l, r0, l, r1);
    else
        owf_buf_printf(b, " %s=\"local-table.$%s$%d\"", attr, l, r0);
}

static void put_cm(owf_buf *b, const char *name, int twips)
{
    long hundredths = ((long)twips * 254L + 720L) / 1440L;        /* 1440 twips an inch, 2.54 cm */
    owf_buf_printf(b, " %s=\"%ld.%02ldcm\"", name, hundredths / 100, hundredths % 100);
}

static void put_string_cell(owf_buf *b, const char *s)
{
    owf_buf_puts(b, "<table:table-cell office:value-type=\"string\"><text:p>");
    owf_buf_puts_xml(b, s);
    owf_buf_puts(b, "</text:p></table:table-cell>");
}

static void put_number_cell(owf_buf *b, double v)
{
    char num[40];
    owf_number_exact(v, num);
    owf_buf_printf(b, "<table:table-cell office:value-type=\"float\" office:value=\"%s\"><text:p>%s</text:p>"
                      "</table:table-cell>", num, num);
}

static void put_title(owf_buf *b, const char *element, const char *text)
{
    if (!text || !*text)
        return;
    owf_buf_printf(b, "<chart:%s><text:p>", element);
    owf_buf_puts_xml(b, text);
    owf_buf_printf(b, "</text:p></chart:%s>", element);
}

int owf_chart_odf_xml(const owf_chart *c, int width, int height, owf_buf *b)
{
    static const char *const legends[] = { "end", "start", "top", "bottom" };
    int i, r, rows = 0, scatter, pie, cats = 0;
    const char *cls;
    if (!c)
        return OWF_ERR_FORMAT;
    scatter = c->kind == OWF_CHART_SCATTER;
    pie = c->kind == OWF_CHART_PIE;
    cls = c->kind == OWF_CHART_LINE ? "chart:line" : c->kind == OWF_CHART_AREA ? "chart:area"
        : pie ? (c->hole ? "chart:ring" : "chart:circle") : scatter ? "chart:scatter" : "chart:bar";
    for (i = 0; i < c->nseries; i++)
        if (c->series[i].npoints > rows)
            rows = c->series[i].npoints;
    if (rows > MAX_ROWS)
        rows = MAX_ROWS;
    for (r = 0; r < rows && !cats; r++)
        for (i = 0; i < c->nseries; i++)
            if (r < c->series[i].npoints && (scatter ? c->series[i].points[r].has_x : c->series[i].points[r].label != NULL))
                cats = 1;

    owf_buf_puts(b, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content "
                    "xmlns:office=\"" NS_OFFICE "\" xmlns:style=\"" NS_STYLE "\" xmlns:text=\"" NS_TEXT "\" "
                    "xmlns:table=\"" NS_TABLE "\" xmlns:draw=\"" NS_DRAW "\" xmlns:chart=\"" NS_CHART "\" "
                    "xmlns:svg=\"" NS_SVG "\" xmlns:fo=\"" NS_FO "\" office:version=\"1.2\"><office:automatic-styles>");
    owf_buf_printf(b, "<style:style style:name=\"plot\" style:family=\"chart\"><style:chart-properties chart:vertical=\"%s\"",
                   c->kind == OWF_CHART_BAR ? "true" : "false");
    if (c->grouping == OWF_CHART_STACKED) owf_buf_puts(b, " chart:stacked=\"true\"");
    if (c->grouping == OWF_CHART_PERCENT) owf_buf_puts(b, " chart:percentage=\"true\"");
    owf_buf_puts(b, "/></style:style>");
    for (i = 0; i < c->nseries; i++)
        if (OWF_SHADE_SET(c->series[i].colour)) {
            unsigned long rgb = OWF_SHADE_RGB(c->series[i].colour);
            owf_buf_printf(b, "<style:style style:name=\"series%d\" style:family=\"chart\"><style:graphic-properties "
                              "draw:fill=\"solid\" draw:fill-color=\"#%06lx\" svg:stroke-color=\"#%06lx\"/></style:style>",
                           i + 1, rgb, rgb);
        }
    owf_buf_puts(b, "</office:automatic-styles><office:body><office:chart><chart:chart");
    put_cm(b, "svg:width", width > 0 ? width : 7200);
    put_cm(b, "svg:height", height > 0 ? height : 4320);
    owf_buf_printf(b, " chart:class=\"%s\">", cls);
    put_title(b, "title", c->title);
    if (c->legend)
        owf_buf_printf(b, "<chart:legend chart:legend-position=\"%s\"/>",
                       legends[c->legend == 'l' ? 1 : c->legend == 't' ? 2 : c->legend == 'b' ? 3 : 0]);
    owf_buf_puts(b, "<chart:plot-area chart:style-name=\"plot\" chart:data-source-has-labels=\"both\">");
    {                                   /* a pie's too: its categories name its slices */
        owf_buf_puts(b, "<chart:axis chart:dimension=\"x\" chart:name=\"primary-x\">");
        put_title(b, "title", c->category_title);
        if (cats && rows && !scatter) {
            owf_buf_puts(b, "<chart:categories");
            put_range(b, "table:cell-range-address", 1, 2, rows + 1);
            owf_buf_puts(b, "/>");
        }
        owf_buf_puts(b, "</chart:axis><chart:axis chart:dimension=\"y\" chart:name=\"primary-y\">");
        put_title(b, "title", c->value_title);
        if (c->gridlines)
            owf_buf_puts(b, "<chart:grid chart:class=\"major\"/>");
        owf_buf_puts(b, "</chart:axis>");
    }
    for (i = 0; i < c->nseries; i++) {
        owf_buf_puts(b, "<chart:series");
        if (OWF_SHADE_SET(c->series[i].colour))
            owf_buf_printf(b, " chart:style-name=\"series%d\"", i + 1);
        if (rows)
            put_range(b, "chart:values-cell-range-address", i + 2, 2, rows + 1);
        if (c->series[i].name)
            put_range(b, "chart:label-cell-address", i + 2, 1, 1);
        owf_buf_printf(b, " chart:class=\"%s\">", cls);
        if (scatter && cats && rows) {
            owf_buf_puts(b, "<chart:domain");
            put_range(b, "table:cell-range-address", 1, 2, rows + 1);
            owf_buf_puts(b, "/>");
        }
        owf_buf_puts(b, "</chart:series>");
    }
    owf_buf_puts(b, "</chart:plot-area>");

    /* the data: a header row of the series' names, then a row a point, its category (or x) in column A */
    owf_buf_printf(b, "<table:table table:name=\"local-table\"><table:table-header-columns><table:table-column/>"
                      "</table:table-header-columns><table:table-columns><table:table-column "
                      "table:number-columns-repeated=\"%d\"/></table:table-columns>"
                      "<table:table-header-rows><table:table-row><table:table-cell/>", c->nseries > 0 ? c->nseries : 1);
    for (i = 0; i < c->nseries; i++)
        if (c->series[i].name)
            put_string_cell(b, c->series[i].name);
        else
            owf_buf_puts(b, "<table:table-cell/>");
    owf_buf_puts(b, "</table:table-row></table:table-header-rows><table:table-rows>");
    for (r = 0; r < rows; r++) {
        const owf_chart_point *a = NULL;
        owf_buf_puts(b, "<table:table-row>");
        for (i = 0; i < c->nseries && !a; i++)
            if (r < c->series[i].npoints && (scatter ? c->series[i].points[r].has_x : c->series[i].points[r].label != NULL))
                a = &c->series[i].points[r];
        if (a && scatter)
            put_number_cell(b, a->x);
        else if (a)
            put_string_cell(b, a->label);
        else
            owf_buf_puts(b, "<table:table-cell/>");
        for (i = 0; i < c->nseries; i++)
            if (r < c->series[i].npoints && c->series[i].points[r].has_value)
                put_number_cell(b, c->series[i].points[r].value);
            else
                owf_buf_puts(b, "<table:table-cell/>");
        owf_buf_puts(b, "</table:table-row>");
    }
    owf_buf_puts(b, "</table:table-rows></table:table></chart:chart></office:chart></office:body>"
                    "</office:document-content>\n");
    return b->failed ? OWF_ERR_MEMORY : OWF_OK;
}

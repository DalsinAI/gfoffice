/*
 * libowf: the charts on a sheet (owf_sheet_charts, owf_sheet_keep_charts): where an .xlsx's drawing anchors
 * each one, read from and kept as the parts it was kept with (owf_sheet.drawing, owf_book.parts). The chart
 * part itself, the chart's kind, series and look, is read and written by owf_chart.c, with no book, so a
 * deck's charts use it too.
 *
 * Small parts, read as trees. Damaged ones give fewer charts, never an
 * error; only memory running out is one.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        owf_chart_read_space(c, owf_xml_root(cx), &failed);
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

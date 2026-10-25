/*
 * OpenCalc's core: the charts on the sheet showing, drawn as Excel draws a
 * plain one. Read once from the sheet's kept drawing (owf_sheet_charts),
 * their values each time from the cells their series come from, so a
 * chart follows what is typed; from the values the file last had when the
 * cells cannot be found (another workbook, a name). Columns and bars
 * (clustered, stacked, 100%), lines (and areas, as lines), pies and
 * doughnuts, scatters (lines and markers); the other kinds as their box
 * and title with a word that they are not drawn yet.
 *
 * All of it through the drawer (oc_gfx.h): lines, filled polygons and
 * text, so another can stand in for the core's own.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_gfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POINTS 4096                  /* categories drawn */
#define TITLE_TWIPS 280                  /* 14 points, Excel's chart title */
#define LABEL_TWIPS 180                  /* 9 points, its axes' and legend's */
#define RGB_CHART_TEXT 0x595959UL
#define RGB_CHART_LINE 0xD9D9D9UL        /* the chart's frame, the gridlines */
#define RGB_AXIS_LINE 0xBFBFBFUL

static const unsigned long accents[6] = { 0x4472C4UL, 0xED7D31UL, 0xA5A5A5UL, 0xFFC000UL, 0x5B9BD5UL, 0x70AD47UL };

/* ---- The charts of the sheet showing, read once ---- */

void oc_charts_forget(oc_core *core)
{
    owf_charts_free(core->charts, core->ncharts);
    core->charts = NULL;
    core->ncharts = 0;
    free(core->charts_of);
    core->charts_of = NULL;
}

const owf_chart *oc_core_charts(oc_core *core, int *count)
{
    const owf_sheet *s = oc_sheet(core);
    *count = 0;
    if (!s || !s->drawing)
        return NULL;
    if (!core->charts_of || strcmp(core->charts_of, s->drawing)) {
        oc_charts_forget(core);
        if (!(core->charts_of = oc_strdup(s->drawing)))
            return NULL;
        if (owf_sheet_charts(core->book, oc_core_sheet(core), &core->charts, &core->ncharts) != OWF_OK) {
            core->charts = NULL;
            core->ncharts = 0;
            free(core->charts_of);       /* tried again next time */
            core->charts_of = NULL;
        }
    }
    *count = core->ncharts;
    return core->charts;
}

/* ---- The values: from the cells, else as the file last had them ---- */

/* "Chart!$B$1:$B$4" or "'My sheet'!B1:B4" as a sheet and a range; 0 when it is not one of this book's. */
static int chart_ref(const owf_book *book, const char *ref, int *sheet, owf_range *r)
{
    char name[256], range[64];
    size_t n = 0, k = 0;
    const char *p = ref, *bang;
    int i;
    if (!ref)
        return 0;
    if (*p == '\'') {
        for (p++; *p && n < sizeof name - 1; p++) {
            if (*p == '\'') {
                if (p[1] != '\'')
                    break;
                p++;
            }
            name[n++] = *p;
        }
        if (*p != '\'' || p[1] != '!')
            return 0;
        bang = p + 1;
    } else {
        if (!(bang = strchr(p, '!')) || (size_t)(bang - p) >= sizeof name)
            return 0;
        n = (size_t)(bang - p);
        memcpy(name, p, n);
    }
    name[n] = 0;
    for (p = bang + 1; *p && k < sizeof range - 1; p++)
        if (*p != '$')
            range[k++] = *p;
    range[k] = 0;
    if (*p || !oc_parse_range(range, r))
        return 0;
    for (i = 0; i < book->nsheets; i++) {
        const char *a = book->sheets[i].name, *b = name;
        for (; *a && *b; a++, b++) {
            int ca = (unsigned char)*a, cb = (unsigned char)*b;
            if (ca >= 'A' && ca <= 'Z')
                ca += 'a' - 'A';
            if (cb >= 'A' && cb <= 'Z')
                cb += 'a' - 'A';
            if (ca != cb)
                break;
        }
        if (!*a && !*b) {
            *sheet = i;
            return 1;
        }
    }
    return 0;
}

/* A one-row or one-column range's cells, in order: how many (0 when it is neither). */
static int range_cells(const owf_range *r)
{
    unsigned long n;
    if (r->first_row == r->last_row)
        n = (unsigned long)r->last_col - r->first_col + 1;
    else if (r->first_col == r->last_col)
        n = (unsigned long)r->last_row - r->first_row + 1;
    else
        return 0;
    return n > MAX_POINTS ? MAX_POINTS : (int)n;
}

static void range_at(const owf_range *r, int k, unsigned *row, unsigned *col)
{
    *row = r->first_row == r->last_row ? r->first_row : r->first_row + (unsigned)k;
    *col = r->first_row == r->last_row ? r->first_col + (unsigned)k : r->first_col;
}

typedef struct {
    int n, ns;                   /* categories, series */
    double *v;                   /* ns x n */
    double *x;                   /* a scatter's x, ns x n */
    unsigned char *has;
    char **labels;               /* n */
    char **names;                /* ns */
    const char **fmts;           /* ns: each series' values' number format (its first value cell's, else the
                                    file's), NULL General */
    const char *xfmt;            /* a scatter's x values' (the first series') */
} chartdata;

/* Values the axis can hold: none past 1e200 (and not a number: the least). */
static double bounded(double v)
{
    if (!(v > -1e200))
        return -1e200;
    return v < 1e200 ? v : 1e200;
}

static void data_free(chartdata *cd)
{
    int i;
    for (i = 0; cd->labels && i < cd->n; i++)
        free(cd->labels[i]);
    for (i = 0; cd->names && i < cd->ns; i++)
        free(cd->names[i]);
    free(cd->labels);
    free(cd->names);
    free(cd->fmts);
    free(cd->v);
    free(cd->x);
    free(cd->has);
}

/* A cell's number format (NULL: General). */
static const char *cell_format(const oc_core *core, int sheet, const owf_cell *c, unsigned col)
{
    const owf_book *b = core->book;
    int st = c->style;
    (void)col;
    if (st <= 0 && sheet >= 0 && sheet < b->nsheets)
        st = oc_col_style(&b->sheets[sheet], c->col);
    if (st > 0 && st < b->nstyles && b->styles[st].number_format > 0 && b->styles[st].number_format < b->nnumfmts)
        return b->numfmts[b->styles[st].number_format];
    return NULL;
}

/* A cell's text as the grid shows it (General numbers to 11 characters). */
static char *cell_label(const oc_core *core, const owf_cell *c, unsigned col)
{
    char buf[128];
    unsigned long colour;
    const owf_cellstyle *st;
    const char *fmt = NULL;
    if (oc_cell_empty(c))
        return oc_strdup("");
    st = oc_cell_style(core, c, col);
    if (st && st->number_format > 0 && st->number_format < core->book->nnumfmts)
        fmt = core->book->numfmts[st->number_format];
    owf_calc_format_value(fmt, c, core->book->date1904, 11, buf, sizeof buf, &colour);
    buf[strcspn(buf, "\r\n")] = 0;
    return oc_strdup(buf);
}

static int read_data(const oc_core *core, const owf_chart *ch, chartdata *cd)
{
    const owf_book *book = core->book;
    int s, k, n = 0, sheet;
    owf_range r;
    char num[24];

    memset(cd, 0, sizeof *cd);
    cd->ns = ch->nseries;
    for (s = 0; s < ch->nseries; s++) {
        int m = chart_ref(book, ch->series[s].values, &sheet, &r) ? range_cells(&r) : 0;
        if (!m)
            m = ch->series[s].npoints;
        if (m > n)
            n = m;
    }
    if (ch->kind == OWF_CHART_PIE && ch->hole == 0 && ch->nseries > 1)
        cd->ns = 1;                      /* a pie shows its first series */
    cd->n = n > MAX_POINTS ? MAX_POINTS : n;
    if (cd->ns <= 0 || cd->ns > 64) {
        cd->ns = 0;
        return 1;
    }
    {
        size_t ns = (size_t)cd->ns & 127, np = (size_t)(cd->n > 0 ? cd->n : 1) & 8191;
        cd->names = calloc(ns, sizeof *cd->names);
        cd->v = calloc(ns * np, sizeof *cd->v);
        cd->x = calloc(ns * np, sizeof *cd->x);
        cd->fmts = calloc(ns, sizeof *cd->fmts);
        cd->has = calloc(ns * np, 1);
        cd->labels = calloc(np, sizeof *cd->labels);
    }
    if (!cd->names || !cd->v || !cd->x || !cd->has || !cd->labels || !cd->fmts)
        return 0;
    for (s = 0; s < cd->ns; s++) {
        const owf_chart_series *se = &ch->series[s];
        size_t at = (size_t)s * (size_t)cd->n;
        if (chart_ref(book, se->name_cell, &sheet, &r) && r.first_row == r.last_row && r.first_col == r.last_col &&
            !oc_cell_empty(oc_find(core, sheet, r.first_row, r.first_col)))
            cd->names[s] = cell_label(core, oc_find(core, sheet, r.first_row, r.first_col), r.first_col);
        else if (se->name && *se->name)
            cd->names[s] = oc_strdup(se->name);
        else {
            snprintf(num, sizeof num, "Series%d", se->index + 1);
            cd->names[s] = oc_strdup(num);
        }
        if (!cd->names[s])
            return 0;
        if (chart_ref(book, se->values, &sheet, &r) && range_cells(&r)) {
            int m = range_cells(&r);
            for (k = 0; k < m && k < cd->n; k++) {
                unsigned row, col;
                const owf_cell *c;
                range_at(&r, k, &row, &col);
                c = oc_find(core, sheet, row, col);
                if (!c || c->type == OWF_VALUE_EMPTY || c->type == OWF_VALUE_ERROR)
                    continue;            /* a gap (Excel's #N/A and blanks) */
                cd->v[at + k] = c->type == OWF_VALUE_NUMBER ? bounded(c->number) : 0;
                if (!cd->has[at + k] && !cd->fmts[s] && c->type == OWF_VALUE_NUMBER)
                    cd->fmts[s] = cell_format(core, sheet, c, col);
                cd->has[at + k] = 1;
            }
            if (!cd->fmts[s])
                cd->fmts[s] = se->format;
        } else {
            cd->fmts[s] = se->format;
            for (k = 0; k < se->npoints && k < cd->n; k++)
                if (se->points[k].has_value) {
                    cd->v[at + k] = bounded(se->points[k].value);
                    cd->has[at + k] = 1;
                }
        }
        /* a scatter's x: from its cells (any text among them: 1, 2, 3..., as Excel does), else as the file had
           them, else 1, 2, 3... */
        for (k = 0; k < cd->n; k++)
            cd->x[at + k] = k + 1;
        if (ch->kind == OWF_CHART_SCATTER) {
            int m, words = 0;
            if (chart_ref(book, se->x_values, &sheet, &r) && (m = range_cells(&r)) > 0) {
                for (k = 0; k < m && k < cd->n; k++) {
                    unsigned row, col;
                    const owf_cell *c;
                    range_at(&r, k, &row, &col);
                    c = oc_find(core, sheet, row, col);
                    if (c && c->type == OWF_VALUE_NUMBER) {
                        cd->x[at + k] = bounded(c->number);
                        if (!cd->xfmt && s == 0)
                            cd->xfmt = cell_format(core, sheet, c, col);
                    }
                    else if (c && (c->type == OWF_VALUE_TEXT || c->type == OWF_VALUE_BOOL))
                        words = 1;
                }
                if (words)
                    for (k = 0; k < cd->n; k++)
                        cd->x[at + k] = k + 1;
            } else {
                for (k = 0; k < se->npoints && k < cd->n; k++)
                    if (se->points[k].has_x)
                        cd->x[at + k] = bounded(se->points[k].x);
            }
        }
    }
    /* the categories: the first series' cells, else its labels as the file had them, else 1, 2, 3... */
    for (s = 0; s < ch->nseries; s++)
        if (chart_ref(book, ch->series[s].categories, &sheet, &r) && range_cells(&r))
            break;
    for (k = 0; k < cd->n; k++) {
        if (s < ch->nseries && k < range_cells(&r)) {
            unsigned row, col;
            range_at(&r, k, &row, &col);
            cd->labels[k] = cell_label(core, oc_find(core, sheet, row, col), col);
        } else {
            const char *l = NULL;
            int t;
            for (t = 0; t < ch->nseries && !l; t++)
                if (k < ch->series[t].npoints)
                    l = ch->series[t].points[k].label;
            if (!l) {
                snprintf(num, sizeof num, "%d", k + 1);
                l = num;
            }
            cd->labels[k] = oc_strdup(l);
        }
        if (!cd->labels[k])
            return 0;
    }
    return 1;
}


/* ---- Colours ---- */

/* Office's accents in turn; past six, each again darker, then lighter, as Excel goes on. */
static unsigned long accent_rgb(int i)
{
    unsigned long c = accents[(i % 6 + 6) % 6], r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
    int level = (i < 0 ? 0 : i / 6) % 3;
    if (level == 1) {
        r = r * 6 / 10;
        g = g * 6 / 10;
        b = b * 6 / 10;
    } else if (level == 2) {
        r += (255 - r) * 4 / 10;
        g += (255 - g) * 4 / 10;
        b += (255 - b) * 4 / 10;
    }
    return r << 16 | g << 8 | b;
}

static unsigned long series_rgb(const owf_chart_series *s)
{
    return OWF_SHADE_SET(s->colour) ? OWF_SHADE_RGB(s->colour) : accent_rgb(s->index);
}

/* A point's colour: its own, else its series', else (colours by point) the accents in turn. */
static unsigned long point_rgb(const owf_chart *ch, int s, int k)
{
    const owf_chart_series *se = &ch->series[s];
    if (k < se->npoints && OWF_SHADE_SET(se->points[k].colour))
        return OWF_SHADE_RGB(se->points[k].colour);
    return ch->vary ? accent_rgb(k) : series_rgb(se);
}

/* ---- Drawing, through the drawer (oc_gfx.h) only ---- */

typedef struct {
    oc_core *core;
    oc_gfx *g;
    const owf_chart *ch;
    chartdata cd;
    int pad, th, lh, swatch;
    int x0, y0, x1, y1;          /* what is left for the plot */
} render;

static void text(render *r, const char *s, int x, int y, int w, int h, int align, int twips, unsigned flags)
{
    r->g->text(r->g, s, x, y, w, h, align, twips, flags, RGB_CHART_TEXT);
}

static int width_of(render *r, const char *s)
{
    return r->g->text_width(r->g, s, LABEL_TWIPS, 0);
}

/* A marker (OWF_MARKER_...) of size pixels around x, y. */
static void marker(oc_gfx *g, int kind, int x, int y, int size, unsigned long rgb)
{
    int h = size / 2 > 1 ? size / 2 : 1, xy[24], i;
    switch (kind) {
    case OWF_MARKER_NONE:
        return;
    case OWF_MARKER_SQUARE:
        oc_gfx_box(g, x - h, y - h, 2 * h + 1, 2 * h + 1, rgb);
        return;
    case OWF_MARKER_DIAMOND:
        xy[0] = x;
        xy[1] = y - h - 1;
        xy[2] = x + h + 1;
        xy[3] = y;
        xy[4] = x;
        xy[5] = y + h + 1;
        xy[6] = x - h - 1;
        xy[7] = y;
        g->polygon(g, xy, 4, rgb);
        return;
    case OWF_MARKER_TRIANGLE:
        xy[0] = x;
        xy[1] = y - h - 1;
        xy[2] = x + h + 1;
        xy[3] = y + h + 1;
        xy[4] = x - h - 1;
        xy[5] = y + h + 1;
        g->polygon(g, xy, 3, rgb);
        return;
    case OWF_MARKER_X:
        g->line(g, x - h, y - h, x + h, y + h, 1, rgb);
        g->line(g, x - h, y + h, x + h, y - h, 1, rgb);
        return;
    case OWF_MARKER_PLUS:
        g->line(g, x - h, y, x + h, y, 1, rgb);
        g->line(g, x, y - h, x, y + h, 1, rgb);
        return;
    case OWF_MARKER_STAR:
        g->line(g, x - h, y - h, x + h, y + h, 1, rgb);
        g->line(g, x - h, y + h, x + h, y - h, 1, rgb);
        g->line(g, x, y - h, x, y + h, 1, rgb);
        return;
    case OWF_MARKER_DASH:
        oc_gfx_box(g, x - h, y - 1, 2 * h + 1, 2, rgb);
        return;
    case OWF_MARKER_DOT:
        oc_gfx_box(g, x - 1, y - 1, 2, 2, rgb);
        return;
    }
    for (i = 0; i < 12; i++) {                   /* a circle (AUTO too), as twelve sides */
        double a = i * 3.14159265358979 / 6;
        xy[2 * i] = x + (int)floor((h + 0.5) * cos(a) + 0.5);
        xy[2 * i + 1] = y + (int)floor((h + 0.5) * sin(a) + 0.5);
    }
    g->polygon(g, xy, 12, rgb);
}

static int marker_px(const oc_core *core, const owf_chart_series *s)
{
    int pt = s->marker_size > 0 ? s->marker_size : 5, px = pt * 4 * core->zoom / 300;   /* 96/72 */
    return px < 3 ? 3 : px;
}

/* The legend, at the side the file says: names and their colours (markers for scatters); what is left of the
 * plot's room shrinks by it. */
static void legend(render *r, char **names, const unsigned long *rgb, const int *marks, int n)
{
    const owf_chart *ch = r->ch;
    int s, widest = 0, total = 0, lx, ly, lh = r->lh, sw = r->swatch;
    if (!ch->legend || n <= 0)
        return;
    for (s = 0; s < n; s++) {
        int tw = width_of(r, names[s]);
        if (tw > widest)
            widest = tw;
        total += sw + 4 + tw + r->pad;
    }
    if (ch->legend == 'r' || ch->legend == 'l') {
        int lw = sw + 4 + widest;
        if (lw > (r->x1 - r->x0) / 3)
            lw = (r->x1 - r->x0) / 3;
        lx = ch->legend == 'r' ? r->x1 - lw : r->x0;
        ly = (r->y0 + r->y1 - n * lh) / 2;
        if (ly < r->y0)
            ly = r->y0;
        for (s = 0; s < n && ly + lh <= r->y1; s++, ly += lh) {
            if (marks && marks[s] != OWF_MARKER_NONE)
                marker(r->g, marks[s], lx + sw / 2, ly + lh / 2, sw, rgb[s]);
            else
                oc_gfx_box(r->g, lx, ly + (lh - sw) / 2, sw, sw, rgb[s]);
            text(r, names[s], lx + sw + 4, ly, lw - sw - 4, lh, OC_ALIGN_LEFT, LABEL_TWIPS, 0);
        }
        if (ch->legend == 'r')
            r->x1 -= lw + r->pad;
        else
            r->x0 += lw + r->pad;
    } else {
        lx = r->x0 + (r->x1 - r->x0 - total + r->pad) / 2;
        if (lx < r->x0)
            lx = r->x0;
        ly = ch->legend == 't' ? r->y0 : r->y1 - lh;
        for (s = 0; s < n; s++) {
            int tw = width_of(r, names[s]);
            if (marks && marks[s] != OWF_MARKER_NONE)
                marker(r->g, marks[s], lx + sw / 2, ly + lh / 2, sw, rgb[s]);
            else
                oc_gfx_box(r->g, lx, ly + (lh - sw) / 2, sw, sw, rgb[s]);
            text(r, names[s], lx + sw + 4, ly, tw + 1, lh, OC_ALIGN_LEFT, LABEL_TWIPS, 0);
            lx += sw + 4 + tw + r->pad;
        }
        if (ch->legend == 't')
            r->y0 += lh + r->pad / 2;
        else
            r->y1 -= lh + r->pad / 2;
    }
}

/* ---- Value axes ---- */

/* From lo to hi in steps of 1, 2 or 5 times a power of ten, about `want` of them. 0 is taken in unless the
 * values all lie within a sixth of their largest (as Excel does); the file's limits are kept. */
static void scale(int has_min, double min, int has_max, double max, double lo, double hi, int want, double *from,
                  double *to, double *step)
{
    double range, raw, mag = 1, s;
    if (lo > 0 && !((hi - lo) < hi / 6))
        lo = 0;
    if (hi < 0 && !((hi - lo) < -lo / 6))
        hi = 0;
    if (has_min)
        lo = bounded(min);
    if (has_max)
        hi = bounded(max);
    if (hi <= lo)
        hi = lo + 1;
    range = hi - lo;
    raw = range / (want > 1 ? want : 1);
    while (mag * 10 <= raw && mag < 1e300)
        mag *= 10;
    while (mag > raw && mag > 1e-300)
        mag /= 10;
    s = mag;
    if (s < raw)
        s = mag * 2;
    if (s < raw)
        s = mag * 5;
    if (s < raw)
        s = mag * 10;
    *step = s;
    *from = has_min ? lo : floor(lo / s + 1e-9) * s;
    *to = has_max ? hi : ceil(hi / s - 1e-9) * s;
    if (*to <= *from)
        *to = *from + s;
}

/* A number in a format (NULL: General), as a cell shows it, on one line. */
static void number_text(const oc_core *core, double v, const char *fmt, char *out, size_t size)
{
    owf_cell c;
    unsigned long colour;
    memset(&c, 0, sizeof c);
    c.type = OWF_VALUE_NUMBER;
    c.number = v;
    owf_calc_format_value(fmt, &c, core->book->date1904, 15, out, size, &colour);
    out[strcspn(out, "\r\n")] = 0;
}

/* The value axis's format: the file's, else the first series' values' (as Excel links it to them); 100%: 0%. */
static const char *axis_format(const render *r)
{
    if (r->ch->grouping == OWF_CHART_PERCENT && r->ch->kind != OWF_CHART_SCATTER)
        return "0%";
    if (r->ch->value_format)
        return r->ch->value_format;
    return r->cd.ns > 0 ? r->cd.fmts[0] : NULL;
}

/* The widest of an axis's labels. */
static int labels_width(render *r, double from, double to, double step, const char *fmt)
{
    char buf[64];
    double v;
    int ticks, most = 0, t;
    for (v = from, ticks = 0; v <= to + step / 2 && ticks < 64; v += step, ticks++) {
        number_text(r->core, v, fmt, buf, sizeof buf);
        if ((t = width_of(r, buf)) > most)
            most = t;
    }
    return most;
}

/* An axis's gridlines (when grid) and labels: across (vertical lines, labels below) or up (horizontal lines,
 * labels to the left, right-aligned in lw); pos maps a value to its pixel. */
#define POS(v) (across ? a0 + (int)floor(((v) - from) * (a1 - a0) / (to - from) + 0.5) \
                       : a1 - (int)floor(((v) - from) * (a1 - a0) / (to - from) + 0.5))
static void axis(render *r, int across, int a0, int a1, int c0, int c1, double from, double to, double step, int grid,
                 const char *fmt, int lw)
{
    char buf[64];
    double v;
    int ticks;
    for (v = from, ticks = 0; v <= to + step / 2 && ticks < 64; v += step, ticks++) {
        int at = POS(v);
        number_text(r->core, fabs(v) < step / 1e6 ? 0 : v, fmt, buf, sizeof buf);
        if (across) {
            if (grid)
                r->g->line(r->g, at, c0, at, c1, 1, RGB_CHART_LINE);
            text(r, buf, at - 40 * r->core->zoom / 100, c1 + 2, 80 * r->core->zoom / 100, r->lh, OC_ALIGN_CENTRE,
                 LABEL_TWIPS, 0);
        } else {
            if (grid)
                r->g->line(r->g, c0, at, c1 - 1, at, 1, RGB_CHART_LINE);
            text(r, buf, c0 - 4 - lw, at - r->lh / 2, lw, r->lh, OC_ALIGN_RIGHT, LABEL_TWIPS, 0);
        }
    }
}

/* ---- Data labels ---- */

/* A point's label: its series' name, category, value (in the labels' format, else its values') and share,
 * as the series says, joined by ", " as Excel joins them. */
static void label_text(render *r, int s, int k, double v, double share, const char *category, char *out, size_t size)
{
    const owf_chart_series *se = &r->ch->series[s];
    char part[64];
    const char *parts[4];
    int n = 0, i;
    size_t at = 0;
    if (se->labels & OWF_LABEL_SERIES)
        parts[n++] = r->cd.names[s];
    if (se->labels & OWF_LABEL_CATEGORY)
        parts[n++] = category ? category : k < r->cd.n ? r->cd.labels[k] : "";
    if (se->labels & OWF_LABEL_VALUE) {
        number_text(r->core, v, se->label_format ? se->label_format : r->cd.fmts[s], part, sizeof part);
        parts[n++] = part;
    }
    out[0] = 0;
    for (i = 0; i < n && at + 1 < size; i++) {
        size_t m = strlen(parts[i]);
        if (i && at + 2 < size) {
            memcpy(out + at, ", ", 2);
            at += 2;
        }
        if (m > size - 1 - at)
            m = size - 1 - at;
        memcpy(out + at, parts[i], m);
        at += m;
        out[at] = 0;
    }
    if ((se->labels & OWF_LABEL_PERCENT) && at + 2 < size) {
        number_text(r->core, share, "0%", part, sizeof part);
        if (at) {
            memcpy(out + at, ", ", 2);
            at += 2;
        }
        out[at] = 0;
        strncat(out, part, size - 1 - at);
    }
}

/* A label's text with its middle at cy: centred on x, or starting (OC_ALIGN_LEFT) or ending there. */
static void put_label(render *r, const char *s, int x, int cy, int align)
{
    int w = width_of(r, s) + 2, lh = r->lh;
    if (!*s)
        return;
    x = align == OC_ALIGN_LEFT ? x : align == OC_ALIGN_RIGHT ? x - w : x - w / 2;
    text(r, s, x, cy - lh / 2, w, lh, OC_ALIGN_CENTRE, LABEL_TWIPS, 0);
}

/* A line's or a scatter's point's label, beside its marker (m pixels) where the series says: right unless. */
static void point_label(render *r, const char *s, int x, int y, int m, int pos)
{
    int lh = r->lh;
    switch (pos) {
    case OWF_LABEL_LEFT: put_label(r, s, x - m / 2 - 3, y, OC_ALIGN_RIGHT); break;
    case OWF_LABEL_ABOVE: put_label(r, s, x, y - m / 2 - 1 - lh / 2, OC_ALIGN_CENTRE); break;
    case OWF_LABEL_BELOW: put_label(r, s, x, y + m / 2 + 1 + lh / 2, OC_ALIGN_CENTRE); break;
    case OWF_LABEL_CENTRE: put_label(r, s, x, y, OC_ALIGN_CENTRE); break;
    default: put_label(r, s, x + m / 2 + 3, y, OC_ALIGN_LEFT); break;
    }
}

/* ---- Columns, bars and lines over categories ---- */

static int categories(render *r)
{
    const owf_chart *ch = r->ch;
    chartdata *cd = &r->cd;
    oc_gfx *g = r->g;
    oc_core *core = r->core;
    int s, k, horizontal = ch->kind == OWF_CHART_BAR, lh = r->lh, t;
    int percent = ch->grouping == OWF_CHART_PERCENT, stacked = ch->grouping != OWF_CHART_CLUSTERED;
    int along, want, vw, cwid = 0, n = cd->n ? cd->n : 1, every = 1, slot, ax0, ax1, ay0, ay1, a0, a1, across;
    double lo = 0, hi = 0, from, to, step, v, sw, bw, gap = ch->gap / 100.0, val0;
    unsigned long *rgb = malloc((size_t)(cd->ns ? cd->ns : 1) * sizeof *rgb);

    if (!rgb)
        return 0;
    for (s = 0; s < cd->ns; s++)
        rgb[s] = series_rgb(&ch->series[s]);
    legend(r, cd->names, rgb, NULL, cd->ns);
    free(rgb);

    /* the values' range: each point's, or each category's sums when stacked; percentages for 100% */
    for (k = 0; k < cd->n; k++) {
        double pos = 0, neg = 0, all = 0;
        for (s = 0; s < cd->ns; s++) {
            size_t at = (size_t)s * (size_t)cd->n + (size_t)k;
            if (!cd->has[at])
                continue;
            v = cd->v[at];
            if (!stacked) {
                if (v < lo || (k == 0 && s == 0))
                    lo = v;
                if (v > hi || (k == 0 && s == 0))
                    hi = v;
            } else if (v >= 0) {
                pos += v;
            } else {
                neg += v;
            }
            all += fabs(v);
        }
        if (percent && all > 0) {
            pos /= all;
            neg /= all;
        }
        if (stacked) {
            if (neg < lo)
                lo = neg;
            if (pos > hi)
                hi = pos;
        }
    }
    if (stacked)                                 /* sums run from 0 */
        lo = lo > 0 ? 0 : lo;
    along = horizontal ? r->x1 - r->x0 : r->y1 - r->y0;
    want = along / (horizontal ? 60 * core->zoom / 100 + 1 : lh * 3 / 2 + 1);
    scale(ch->has_min, ch->min, ch->has_max, ch->max, lo, hi, want < 2 ? 2 : want > 10 ? 10 : want, &from, &to, &step);
    vw = labels_width(r, from, to, step, axis_format(r));
    for (k = 0; k < cd->n; k++)
        if ((t = width_of(r, cd->labels[k])) > cwid)
            cwid = t;
    if (horizontal) {
        if (cwid > (r->x1 - r->x0) / 3)
            cwid = (r->x1 - r->x0) / 3;
        ax0 = r->x0 + cwid + 4;
        ax1 = r->x1 - vw / 2;
        ay0 = r->y0;
        ay1 = r->y1 - lh - 2;
    } else {
        ax0 = r->x0 + vw + 4;
        ax1 = r->x1;
        ay0 = r->y0 + lh / 2;
        ay1 = r->y1 - lh - 2;
    }
    if (ax1 - ax0 < 8 || ay1 - ay0 < 8)
        return 1;
    across = horizontal;
    a0 = horizontal ? ax0 : ay0;
    a1 = horizontal ? ax1 : ay1;
    if (horizontal)
        axis(r, 1, ax0, ax1, ay0, ay1, from, to, step, ch->gridlines, axis_format(r), 0);
    else
        axis(r, 0, ay0, ay1, ax0, ax1, from, to, step, ch->gridlines, axis_format(r), vw);
    /* the category axis, along 0 (or the scale's end nearest it) */
    val0 = from > 0 ? from : to < 0 ? to : 0;
    if (horizontal)
        g->line(g, POS(val0), ay0, POS(val0), ay1, 1, RGB_AXIS_LINE);
    else
        g->line(g, ax0, POS(val0), ax1 - 1, POS(val0), 1, RGB_AXIS_LINE);
    /* the categories' labels: as many as fit */
    sw = (double)(horizontal ? ay1 - ay0 : ax1 - ax0) / n;
    if (!horizontal && sw > 0 && cwid + 4 > sw)
        every = (int)((cwid + 4) / sw) + 1;
    if (horizontal && sw > 0 && lh > sw)
        every = (int)(lh / sw) + 1;
    for (k = 0; k < cd->n; k += every) {
        if (horizontal) {                        /* the first category at the bottom, as Excel draws it */
            int cyy = ay1 - (int)((k + 0.5) * sw) - lh / 2;
            text(r, cd->labels[k], r->x0, cyy, cwid, lh, OC_ALIGN_RIGHT, LABEL_TWIPS, 0);
        } else {
            int cxx = ax0 + (int)(k * sw);
            slot = (int)(sw * every);
            text(r, cd->labels[k], cxx + (int)(sw / 2) - slot / 2, ay1 + 2, slot, lh, OC_ALIGN_CENTRE, LABEL_TWIPS, 0);
        }
    }
    /* the data */
    if (ch->kind == OWF_CHART_COLUMN || ch->kind == OWF_CHART_BAR) {
        int pass;
        bw = stacked ? sw / (1 + gap) : sw / (cd->ns + gap);
        for (pass = 0; pass < 2; pass++)         /* the bars, then their labels over them all */
        for (k = 0; k < cd->n; k++) {
            double all = 0, pos = 0, neg = 0, start = k * sw + (sw - (stacked ? bw : bw * cd->ns)) / 2;
            for (s = 0; s < cd->ns; s++)
                if (cd->has[(size_t)s * cd->n + k])
                    all += fabs(cd->v[(size_t)s * cd->n + k]);
            for (s = 0; s < cd->ns; s++) {
                size_t at = (size_t)s * (size_t)cd->n + (size_t)k;
                double base, top, a, b;
                int i0, i1, v0, v1;
                if (!cd->has[at])
                    continue;
                v = cd->v[at];
                if (percent)
                    v = all > 0 ? v / all : 0;
                if (stacked) {
                    base = v >= 0 ? pos : neg;
                    top = base + v;
                    if (v >= 0)
                        pos = top;
                    else
                        neg = top;
                } else {
                    base = val0;
                    top = v;
                }
                top = top < from ? from : top > to ? to : top;
                base = base < from ? from : base > to ? to : base;
                a = start + (stacked ? 0 : s * bw);
                b = a + bw;
                i0 = (int)floor(a + 0.5);
                i1 = (int)floor(b + 0.5);
                v0 = POS(base);
                v1 = POS(top);
                if (pass == 0) {
                    if (horizontal)
                        oc_gfx_box(g, v0 < v1 ? v0 : v1, ay1 - i1, abs(v1 - v0), i1 - i0, point_rgb(ch, s, k));
                    else
                        oc_gfx_box(g, ax0 + i0, v0 < v1 ? v0 : v1, i1 - i0, abs(v1 - v0), point_rgb(ch, s, k));
                } else if (ch->series[s].labels) {
                    /* where: at its end outside it (clustered) or in its middle (stacked), unless the file says */
                    char buf[160];
                    int pos = ch->series[s].label_pos, mid = (i0 + i1) / 2, up = horizontal ? v1 >= v0 : v1 <= v0;
                    int end = v1, startp = v0, lh2 = lh / 2 + 1;
                    label_text(r, s, k, cd->v[at], all > 0 ? fabs(cd->v[at]) / all : 0, NULL, buf, sizeof buf);
                    if (pos == OWF_LABEL_AUTO || pos == OWF_LABEL_BEST_FIT)
                        pos = stacked ? OWF_LABEL_CENTRE : OWF_LABEL_OUTSIDE_END;
                    if (horizontal) {
                        int cyy = ay1 - mid, dir = up ? 1 : -1;
                        if (pos == OWF_LABEL_OUTSIDE_END)
                            put_label(r, buf, end + dir * 3, cyy, up ? OC_ALIGN_LEFT : OC_ALIGN_RIGHT);
                        else if (pos == OWF_LABEL_INSIDE_END)
                            put_label(r, buf, end - dir * 3, cyy, up ? OC_ALIGN_RIGHT : OC_ALIGN_LEFT);
                        else if (pos == OWF_LABEL_INSIDE_BASE)
                            put_label(r, buf, startp + dir * 3, cyy, up ? OC_ALIGN_LEFT : OC_ALIGN_RIGHT);
                        else
                            put_label(r, buf, (end + startp) / 2, cyy, OC_ALIGN_CENTRE);
                    } else {
                        int cxx = ax0 + mid, dir = up ? -1 : 1;
                        if (pos == OWF_LABEL_OUTSIDE_END)
                            put_label(r, buf, cxx, end + dir * lh2, OC_ALIGN_CENTRE);
                        else if (pos == OWF_LABEL_INSIDE_END)
                            put_label(r, buf, cxx, end - dir * lh2, OC_ALIGN_CENTRE);
                        else if (pos == OWF_LABEL_INSIDE_BASE)
                            put_label(r, buf, cxx, startp + dir * lh2, OC_ALIGN_CENTRE);
                        else
                            put_label(r, buf, cxx, (end + startp) / 2, OC_ALIGN_CENTRE);
                    }
                }
            }
        }
    } else {                                     /* lines (and areas, as their lines) through the categories' middles */
        int lt = 3 * core->zoom / 100 > 1 ? 3 * core->zoom / 100 : 1;
        double *sum = calloc((size_t)n, sizeof *sum), *all = calloc((size_t)n, sizeof *all);
        if (!sum || !all) {
            free(sum);
            free(all);
            return 0;
        }
        for (k = 0; k < cd->n; k++)
            for (s = 0; s < cd->ns; s++)
                if (cd->has[(size_t)s * cd->n + k])
                    all[k] += fabs(cd->v[(size_t)s * cd->n + k]);
        int pass;
        for (pass = 0; pass < 3; pass++) {       /* the lines, their markers, their labels */
            memset(sum, 0, (size_t)n * sizeof *sum);
            for (s = 0; s < cd->ns; s++) {
                const owf_chart_series *se = &ch->series[s];
                int has_last = 0, lx = 0, lyy = 0, mark = ch->kind == OWF_CHART_LINE ? se->marker : OWF_MARKER_NONE;
                int mpx = mark == OWF_MARKER_NONE ? 0 : marker_px(core, se);
                for (k = 0; k < cd->n; k++) {
                    size_t at = (size_t)s * (size_t)cd->n + (size_t)k;
                    int xx, yy;
                    if (!cd->has[at]) {
                        has_last = 0;            /* a gap */
                        continue;
                    }
                    v = cd->v[at];
                    if (percent)
                        v = all[k] > 0 ? v / all[k] : 0;
                    if (stacked) {
                        sum[k] += v;
                        v = sum[k];
                    }
                    v = v < from ? from : v > to ? to : v;
                    xx = ax0 + (int)floor((k + 0.5) * sw + 0.5);
                    yy = POS(v);
                    if (pass == 0) {
                        if (has_last)
                            g->line(g, lx, lyy, xx, yy, lt, series_rgb(se));
                        else if (mark == OWF_MARKER_NONE && (k + 1 >= cd->n || !cd->has[at + 1]))   /* alone: a dot */
                            oc_gfx_box(g, xx - lt, yy - lt, 2 * lt + 1, 2 * lt + 1, series_rgb(se));
                    } else if (pass == 1 && mark != OWF_MARKER_NONE) {
                        marker(g, mark, xx, yy, mpx, point_rgb(ch, s, k));
                    } else if (pass == 2 && se->labels) {
                        char buf[160];
                        label_text(r, s, k, cd->v[at], all[k] > 0 ? fabs(cd->v[at]) / all[k] : 0, NULL, buf, sizeof buf);
                        point_label(r, buf, xx, yy, mpx ? mpx : lt, se->label_pos);
                    }
                    lx = xx;
                    lyy = yy;
                    has_last = 1;
                }
            }
        }
        free(sum);
        free(all);
    }
    (void)across;
    return 1;
}

/* ---- Scatters: x and y both values ---- */

static int scatter(render *r)
{
    const owf_chart *ch = r->ch;
    chartdata *cd = &r->cd;
    oc_gfx *g = r->g;
    oc_core *core = r->core;
    int s, k, lh = r->lh, any = 0, want, vw, a0, a1, across, ax0, ax1, ay0, ay1, lt;
    double xlo = 0, xhi = 0, ylo = 0, yhi = 0, xfrom, xto, xstep, yfrom, yto, ystep, from, to;
    unsigned long *rgb = malloc((size_t)(cd->ns ? cd->ns : 1) * sizeof *rgb);
    int *marks = malloc((size_t)(cd->ns ? cd->ns : 1) * sizeof *marks);

    if (!rgb || !marks) {
        free(rgb);
        free(marks);
        return 0;
    }
    for (s = 0; s < cd->ns; s++) {
        rgb[s] = series_rgb(&ch->series[s]);
        marks[s] = ch->series[s].marker;
    }
    legend(r, cd->names, rgb, marks, cd->ns);
    free(rgb);
    free(marks);
    for (s = 0; s < cd->ns; s++)
        for (k = 0; k < cd->n; k++) {
            size_t at = (size_t)s * (size_t)cd->n + (size_t)k;
            if (!cd->has[at])
                continue;
            if (!any || cd->x[at] < xlo)
                xlo = cd->x[at];
            if (!any || cd->x[at] > xhi)
                xhi = cd->x[at];
            if (!any || cd->v[at] < ylo)
                ylo = cd->v[at];
            if (!any || cd->v[at] > yhi)
                yhi = cd->v[at];
            any = 1;
        }
    want = (r->y1 - r->y0) / (lh * 3 / 2 + 1);
    scale(ch->has_min, ch->min, ch->has_max, ch->max, ylo, yhi, want < 2 ? 2 : want > 10 ? 10 : want, &yfrom, &yto, &ystep);
    vw = labels_width(r, yfrom, yto, ystep, axis_format(r));
    ax0 = r->x0 + vw + 4;
    ax1 = r->x1;
    want = (ax1 - ax0) / (60 * core->zoom / 100 + 1);
    scale(ch->has_xmin, ch->xmin, ch->has_xmax, ch->xmax, xlo, xhi, want < 2 ? 2 : want > 10 ? 10 : want, &xfrom, &xto, &xstep);
    ax1 -= labels_width(r, xfrom, xto, xstep, ch->x_format ? ch->x_format : cd->xfmt) / 2;
    ay0 = r->y0 + lh / 2;
    ay1 = r->y1 - lh - 2;
    if (ax1 - ax0 < 8 || ay1 - ay0 < 8)
        return 1;
    axis(r, 1, ax0, ax1, ay0, ay1, xfrom, xto, xstep, ch->x_gridlines, ch->x_format ? ch->x_format : cd->xfmt, 0);
    axis(r, 0, ay0, ay1, ax0, ax1, yfrom, yto, ystep, ch->gridlines, axis_format(r), vw);
    across = 0;                                  /* the x axis along y's 0 (or its end nearest it) */
    a0 = ay0;
    a1 = ay1;
    from = yfrom;
    to = yto;
    {
        double val0 = yfrom > 0 ? yfrom : yto < 0 ? yto : 0;
        g->line(g, ax0, POS(val0), ax1 - 1, POS(val0), 1, RGB_AXIS_LINE);
    }
    lt = 3 * core->zoom / 100 > 1 ? 3 * core->zoom / 100 : 1;
    for (s = 0; s < cd->ns; s++) {
        const owf_chart_series *se = &ch->series[s];
        int has_last = 0, lx = 0, ly = 0, px = marker_px(core, se);
        for (k = 0; k < cd->n; k++) {
            size_t at = (size_t)s * (size_t)cd->n + (size_t)k;
            double xv, yv;
            int xx, yy;
            if (!cd->has[at]) {
                has_last = 0;
                continue;
            }
            xv = cd->x[at] < xfrom ? xfrom : cd->x[at] > xto ? xto : cd->x[at];
            yv = cd->v[at] < yfrom ? yfrom : cd->v[at] > yto ? yto : cd->v[at];
            xx = ax0 + (int)floor((xv - xfrom) * (ax1 - ax0) / (xto - xfrom) + 0.5);
            yy = ay1 - (int)floor((yv - yfrom) * (ay1 - ay0) / (yto - yfrom) + 0.5);
            if (has_last && se->line)
                g->line(g, lx, ly, xx, yy, lt, series_rgb(se));
            lx = xx;
            ly = yy;
            has_last = 1;
        }
        for (k = 0; k < cd->n; k++) {            /* the markers over the lines */
            size_t at = (size_t)s * (size_t)cd->n + (size_t)k;
            double xv, yv;
            if (!cd->has[at] || se->marker == OWF_MARKER_NONE)
                continue;
            xv = cd->x[at] < xfrom ? xfrom : cd->x[at] > xto ? xto : cd->x[at];
            yv = cd->v[at] < yfrom ? yfrom : cd->v[at] > yto ? yto : cd->v[at];
            marker(g, se->marker, ax0 + (int)floor((xv - xfrom) * (ax1 - ax0) / (xto - xfrom) + 0.5),
                   ay1 - (int)floor((yv - yfrom) * (ay1 - ay0) / (yto - yfrom) + 0.5), px, point_rgb(ch, s, k));
        }
    }
    for (s = 0; s < cd->ns; s++) {               /* the labels over everything: a scatter's category is its x */
        const owf_chart_series *se = &ch->series[s];
        int px = se->marker == OWF_MARKER_NONE ? lt : marker_px(core, se);
        if (!se->labels)
            continue;
        for (k = 0; k < cd->n; k++) {
            size_t at = (size_t)s * (size_t)cd->n + (size_t)k;
            double xv, yv;
            char buf[160], xs[64];
            if (!cd->has[at])
                continue;
            xv = cd->x[at] < xfrom ? xfrom : cd->x[at] > xto ? xto : cd->x[at];
            yv = cd->v[at] < yfrom ? yfrom : cd->v[at] > yto ? yto : cd->v[at];
            number_text(core, cd->x[at], ch->x_format ? ch->x_format : cd->xfmt, xs, sizeof xs);
            label_text(r, s, k, cd->v[at], 0, xs, buf, sizeof buf);
            point_label(r, buf, ax0 + (int)floor((xv - xfrom) * (ax1 - ax0) / (xto - xfrom) + 0.5),
                        ay1 - (int)floor((yv - yfrom) * (ay1 - ay0) / (yto - yfrom) + 0.5), px, se->label_pos);
        }
    }
    (void)across;
    (void)a0;
    (void)a1;
    (void)from;
    (void)to;
    return 1;
}
#undef POS

/* ---- Pies and doughnuts ---- */

#define PI 3.14159265358979

/* A point on a circle around cx, cy: degrees clockwise from 12 o'clock. */
static void on_circle(double cx, double cy, double radius, double deg, int *x, int *y)
{
    double a = deg * PI / 180;
    *x = (int)floor(cx + radius * sin(a) + 0.5);
    *y = (int)floor(cy - radius * cos(a) + 0.5);
}

/* A slice from a0 to a1 degrees between radii inner (0: to the centre) and outer. */
static void slice(oc_gfx *g, double cx, double cy, double inner, double outer, double a0, double a1, unsigned long rgb)
{
    int xy[2 * 128], n = 0, steps = (int)ceil((a1 - a0) / 6), i, most = inner > 0 ? 60 : 120;
    if (steps < 1)
        steps = 1;
    if (steps > most)
        steps = most;
    for (i = 0; i <= steps; i++, n++)
        on_circle(cx, cy, outer, a0 + (a1 - a0) * i / steps, &xy[2 * n], &xy[2 * n + 1]);
    if (inner > 0)
        for (i = steps; i >= 0; i--, n++)
            on_circle(cx, cy, inner, a0 + (a1 - a0) * i / steps, &xy[2 * n], &xy[2 * n + 1]);
    else {
        xy[2 * n] = (int)floor(cx + 0.5);
        xy[2 * n + 1] = (int)floor(cy + 0.5);
        n++;
    }
    g->polygon(g, xy, n, rgb);
}

static int pie(render *r)
{
    const owf_chart *ch = r->ch;
    chartdata *cd = &r->cd;
    oc_gfx *g = r->g;
    int s, k, rings = ch->hole > 0 ? cd->ns : 1, n = cd->n, white = r->core->zoom >= 150 ? 2 : 1;
    double cx, cy, radius, hole, band;
    unsigned long *rgb;
    char **names;

    if (rings < 1)
        return 1;
    /* the legend: the categories in their colours (the series' when the points are not each their own) */
    rgb = malloc((size_t)(n > cd->ns ? n : cd->ns > 0 ? cd->ns : 1) * sizeof *rgb);
    if (!rgb)
        return 0;
    if (ch->vary || !cd->ns) {
        for (k = 0; k < n; k++)
            rgb[k] = point_rgb(ch, 0, k);
        names = cd->labels;
    } else {
        for (s = 0; s < cd->ns; s++)
            rgb[s] = series_rgb(&ch->series[s]);
        names = cd->names;
        n = cd->ns;
    }
    legend(r, names, rgb, NULL, n);
    free(rgb);
    cx = (r->x0 + r->x1) / 2.0;
    cy = (r->y0 + r->y1) / 2.0;
    radius = ((r->x1 - r->x0 < r->y1 - r->y0 ? r->x1 - r->x0 : r->y1 - r->y0) / 2.0) * 0.95;
    if (radius < 4)
        return 1;
    hole = radius * ch->hole / 100.0;
    band = (radius - hole) / rings;
    for (s = 0; s < rings; s++) {                /* a doughnut's first series innermost */
        double total = 0, at = ch->first_angle, inner = hole + band * s, outer = inner + band;
        for (k = 0; k < cd->n; k++)
            if (cd->has[(size_t)s * cd->n + k])
                total += fabs(cd->v[(size_t)s * cd->n + k]);
        if (total <= 0)
            continue;
        for (k = 0; k < cd->n; k++) {
            size_t i = (size_t)s * (size_t)cd->n + (size_t)k;
            double sweep;
            if (!cd->has[i] || cd->v[i] == 0)
                continue;
            sweep = fabs(cd->v[i]) / total * 360;
            slice(g, cx, cy, inner, outer, at, at + sweep, point_rgb(ch, s, k));
            at += sweep;
        }
        /* white lines between the slices, as Excel draws their edges */
        at = ch->first_angle;
        for (k = 0; k < cd->n && total > 0; k++) {
            size_t i = (size_t)s * (size_t)cd->n + (size_t)k;
            int x0, y0, x1, y1;
            if (!cd->has[i] || cd->v[i] == 0)
                continue;
            on_circle(cx, cy, inner, at, &x0, &y0);
            on_circle(cx, cy, outer, at, &x1, &y1);
            if (fabs(cd->v[i]) < total)
                g->line(g, x0, y0, x1, y1, white, 0xFFFFFFUL);
            at += fabs(cd->v[i]) / total * 360;
        }
        if (s > 0) {                             /* and between a doughnut's rings */
            int px, py, qx, qy;
            on_circle(cx, cy, inner, 0, &px, &py);
            for (k = 1; k <= 60; k++, px = qx, py = qy) {
                on_circle(cx, cy, inner, k * 6, &qx, &qy);
                g->line(g, px, py, qx, qy, white, 0xFFFFFFUL);
            }
        }
    }
    /* the labels: in each slice (a doughnut's ring's middle), or outside the pie, as the series says */
    for (s = 0; s < rings; s++) {
        const owf_chart_series *se = &ch->series[s];
        double total = 0, at = ch->first_angle, inner = hole + band * s, outer = inner + band;
        if (!se->labels)
            continue;
        for (k = 0; k < cd->n; k++)
            if (cd->has[(size_t)s * cd->n + k])
                total += fabs(cd->v[(size_t)s * cd->n + k]);
        for (k = 0; k < cd->n && total > 0; k++) {
            size_t i = (size_t)s * (size_t)cd->n + (size_t)k;
            double sweep, mid, rad;
            int lx, ly, pos = se->label_pos;
            char buf[160];
            if (!cd->has[i] || cd->v[i] == 0)
                continue;
            sweep = fabs(cd->v[i]) / total * 360;
            mid = at + sweep / 2;
            at += sweep;
            rad = pos == OWF_LABEL_OUTSIDE_END ? outer + r->lh * 0.8
                : ch->hole > 0 ? (inner + outer) / 2
                : pos == OWF_LABEL_INSIDE_END ? outer * 0.8 : pos == OWF_LABEL_INSIDE_BASE ? outer * 0.35 : outer * 0.62;
            on_circle(cx, cy, rad, mid, &lx, &ly);
            label_text(r, s, k, cd->v[i], fabs(cd->v[i]) / total, NULL, buf, sizeof buf);
            put_label(r, buf, lx, ly, pos != OWF_LABEL_OUTSIDE_END ? OC_ALIGN_CENTRE : lx >= cx ? OC_ALIGN_LEFT : OC_ALIGN_RIGHT);
        }
    }
    return 1;
}

/* ---- A chart ---- */

int oc_chart_render(oc_core *core, oc_gfx *g, const owf_chart *ch, int x, int y, int w, int h)
{
    render r;
    int ok;

    memset(&r, 0, sizeof r);
    r.core = core;
    r.g = g;
    r.ch = ch;
    r.pad = 8 * core->zoom / 100 < 4 ? 4 : 8 * core->zoom / 100;
    r.th = oc_line_px(core, TITLE_TWIPS);
    r.lh = oc_line_px(core, LABEL_TWIPS);
    r.swatch = r.lh * 3 / 5;

    /* the chart's area: white, in a light grey frame */
    oc_gfx_box(g, x, y, w, h, 0xFFFFFFUL);
    g->line(g, x, y, x + w - 1, y, 1, RGB_CHART_LINE);
    g->line(g, x, y + h - 1, x + w - 1, y + h - 1, 1, RGB_CHART_LINE);
    g->line(g, x, y, x, y + h - 1, 1, RGB_CHART_LINE);
    g->line(g, x + w - 1, y, x + w - 1, y + h - 1, 1, RGB_CHART_LINE);
    r.x0 = x + r.pad;
    r.x1 = x + w - r.pad;
    r.y0 = y + r.pad;
    r.y1 = y + h - r.pad;
    if (ch->title && *ch->title) {
        text(&r, ch->title, r.x0, r.y0, r.x1 - r.x0, r.th, OC_ALIGN_CENTRE, TITLE_TWIPS, OWF_BOLD);
        r.y0 += r.th + r.pad / 2;
    }
    if (ch->kind == OWF_CHART_OTHER || !ch->nseries) {
        text(&r, !ch->nseries ? "This chart has no data" : "This kind of chart is not drawn yet", r.x0, (r.y0 + r.y1 - r.lh) / 2,
             r.x1 - r.x0, r.lh, OC_ALIGN_CENTRE, LABEL_TWIPS, 0);
        return !g->failed;
    }
    if (!read_data(core, ch, &r.cd)) {
        data_free(&r.cd);
        return 0;
    }
    if (ch->kind == OWF_CHART_PIE)
        ok = pie(&r);
    else if (ch->kind == OWF_CHART_SCATTER)
        ok = scatter(&r);
    else
        ok = categories(&r);
    data_free(&r.cd);
    return ok && !g->failed;
}

int oc_chart_draw(oc_core *core, oc_draw_list *list, const owf_chart *ch, int x, int y, int w, int h, int cx, int cy, int cw,
                  int chh)
{
    oc_gfx_items gi;
    int l = x > cx ? x : cx, t = y > cy ? y : cy, r = x + w < cx + cw ? x + w : cx + cw, b = y + h < cy + chh ? y + h : cy + chh;
    if (r <= l || b <= t)
        return 1;
    oc_gfx_items_init(&gi, core, list, l, t, r - l, b - t);
    return oc_chart_render(core, &gi.g, ch, x, y, w, h);
}

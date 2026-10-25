/*
 * OpenCalc's core: the charts on the sheet showing, drawn as Excel draws a
 * plain one. Read once from the sheet's kept drawing (owf_sheet_charts),
 * their values each time from the cells their series come from, so a
 * chart follows what is typed; from the values the file last had when the
 * cells cannot be found (another workbook, a name). Columns and bars
 * (clustered, stacked, 100%), lines (and areas, as lines); the other kinds
 * as their box and title with a word that they are not drawn yet.
 *
 * Everything is fills, 1-pixel lines and text, cut here to the pane the
 * chart shows in, so the window only paints them.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

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
    unsigned char *has;
    char **labels;               /* n */
    char **names;                /* ns */
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
    free(cd->v);
    free(cd->has);
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
    cd->n = n > MAX_POINTS ? MAX_POINTS : n;
    if (cd->ns <= 0 || cd->ns > 64) {
        cd->ns = 0;
        return 1;
    }
    {
        size_t ns = (size_t)cd->ns & 127, np = (size_t)(cd->n > 0 ? cd->n : 1) & 8191;
        cd->names = calloc(ns, sizeof *cd->names);
        cd->v = calloc(ns * np, sizeof *cd->v);
        cd->has = calloc(ns * np, 1);
        cd->labels = calloc(np, sizeof *cd->labels);
    }
    if (!cd->names || !cd->v || !cd->has || !cd->labels)
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
                cd->has[at + k] = 1;
            }
        } else {
            for (k = 0; k < se->npoints && k < cd->n; k++)
                if (se->points[k].has_value) {
                    cd->v[at + k] = bounded(se->points[k].value);
                    cd->has[at + k] = 1;
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

/* ---- Fills, lines and text, cut to the clip box ---- */

typedef struct {
    oc_core *core;
    oc_draw_list *list;
    int cx, cy, cw, ch;
    int ok;
    owf_cellstyle style;         /* for measuring: the default font at a size */
} pen;

static void box(pen *p, int x, int y, int w, int h, unsigned long rgb)
{
    int x1 = x + w, y1 = y + h;
    oc_draw_item *it;
    if (x < p->cx)
        x = p->cx;
    if (y < p->cy)
        y = p->cy;
    if (x1 > p->cx + p->cw)
        x1 = p->cx + p->cw;
    if (y1 > p->cy + p->ch)
        y1 = p->cy + p->ch;
    if (x1 <= x || y1 <= y || !p->ok)
        return;
    if (!(it = oc_draw_add(p->list, OC_DRAW_FILL, OC_ROLE_CHART))) {
        p->ok = 0;
        return;
    }
    it->x = x;
    it->y = y;
    it->w = x1 - x;
    it->h = y1 - y;
    it->rgb = rgb;
}

/* Which sides of the clip box a point is beyond. */
static int outcode(const pen *p, double x, double y)
{
    return (x < p->cx) | (x > p->cx + p->cw - 1) << 1 | (y < p->cy) << 2 | (y > p->cy + p->ch - 1) << 3;
}

/* A 1-pixel line, cut to the clip box (Cohen and Sutherland). */
static void seg(pen *p, int ax, int ay, int bx, int by, unsigned long rgb)
{
    double x0 = ax, y0 = ay, x1 = bx, y1 = by;
    int c0 = outcode(p, x0, y0), c1 = outcode(p, x1, y1), tries = 0;
    oc_draw_item *it;
    while ((c0 | c1) && tries++ < 8) {
        int c = c0 ? c0 : c1;
        double x, y, xmin = p->cx, xmax = p->cx + p->cw - 1, ymin = p->cy, ymax = p->cy + p->ch - 1;
        if (c0 & c1)
            return;
        if (c & 8) {
            x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0);
            y = ymax;
        } else if (c & 4) {
            x = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0);
            y = ymin;
        } else if (c & 2) {
            y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0);
            x = xmax;
        } else {
            y = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0);
            x = xmin;
        }
        if (c == c0) {
            x0 = x;
            y0 = y;
            c0 = outcode(p, x0, y0);
        } else {
            x1 = x;
            y1 = y;
            c1 = outcode(p, x1, y1);
        }
    }
    if ((c0 | c1) || !p->ok)
        return;
    if (!(it = oc_draw_add(p->list, OC_DRAW_SEGMENT, OC_ROLE_CHART))) {
        p->ok = 0;
        return;
    }
    it->x = (int)floor(x0 + 0.5);
    it->y = (int)floor(y0 + 0.5);
    it->w = (int)floor(x1 + 0.5) - it->x;
    it->h = (int)floor(y1 + 0.5) - it->y;
    it->rgb = rgb;
}

/* A line t pixels thick: t lines side by side. */
static void thick(pen *p, int x0, int y0, int x1, int y1, int t, unsigned long rgb)
{
    int k, flat = abs(x1 - x0) >= abs(y1 - y0);
    for (k = 0; k < t; k++) {
        int o = k - (t - 1) / 2;
        if (flat)
            seg(p, x0, y0 + o, x1, y1 + o, rgb);
        else
            seg(p, x0 + o, y0, x1 + o, y1, rgb);
    }
}

static int measure(pen *p, const char *s, int twips, unsigned flags)
{
    p->style.font.size = twips;
    p->style.font.flags = flags;
    return oc_measure(p->core, s, strlen(s), &p->style);
}

/* Text in the box x,y,w,h (one line), aligned in it; only when its line is wholly in the clip box. */
static void text(pen *p, const char *s, int x, int y, int w, int h, int align, int twips, unsigned flags)
{
    oc_draw_item *it;
    int tw, l = x > p->cx ? x : p->cx, r = x + w < p->cx + p->cw ? x + w : p->cx + p->cw;
    if (!*s || !p->ok || y < p->cy || y + h > p->cy + p->ch || r <= l)
        return;
    tw = measure(p, s, twips, flags);
    if (!(it = oc_draw_add(p->list, OC_DRAW_TEXT, OC_ROLE_TEXT)) || !oc_draw_add_text(p->list, it, s, strlen(s))) {
        p->ok = 0;
        return;
    }
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->clip_x = l;
    it->clip_y = y;
    it->clip_w = r - l;
    it->clip_h = h;
    it->align = align;
    it->valign = OWF_VALIGN_CENTRE;
    it->text_x = align == OC_ALIGN_CENTRE ? x + (w - tw) / 2 : align == OC_ALIGN_RIGHT ? x + w - tw : x;
    it->flags = flags;
    it->size = twips;
    it->rgb = RGB_CHART_TEXT;
}

/* ---- The value axis ---- */

/* From lo to hi in steps of 1, 2 or 5 times a power of ten, about `want` of them, taking in 0 when the
 * values are all on one side of it (as Excel does), unless the file sets the limits. */
static void scale(const owf_chart *ch, double lo, double hi, int want, double *from, double *to, double *step)
{
    double range, raw, mag = 1, s;
    if (lo > 0)
        lo = 0;
    if (hi < 0)
        hi = 0;
    if (ch->has_min)
        lo = bounded(ch->min);
    if (ch->has_max)
        hi = bounded(ch->max);
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
    *from = ch->has_min ? lo : floor(lo / s + 1e-9) * s;
    *to = ch->has_max ? hi : ceil(hi / s - 1e-9) * s;
    if (*to <= *from)
        *to = *from + s;
}

static void number_label(const oc_core *core, double v, int percent, char *out, size_t size)
{
    owf_cell c;
    unsigned long colour;
    memset(&c, 0, sizeof c);
    c.type = OWF_VALUE_NUMBER;
    c.number = v;
    owf_calc_format_value(percent ? "0%" : NULL, &c, core->book->date1904, 11, out, size, &colour);
}

/* ---- A chart ---- */

static unsigned long series_rgb(const owf_chart_series *s)
{
    return OWF_SHADE_SET(s->colour) ? OWF_SHADE_RGB(s->colour) : accents[(s->index % 6 + 6) % 6];
}

int oc_chart_draw(oc_core *core, oc_draw_list *list, const owf_chart *ch, int x, int y, int w, int h, int cx, int cy, int cw,
                  int chh)
{
    pen p;
    chartdata cd;
    int pad = 8 * core->zoom / 100, th = oc_line_px(core, TITLE_TWIPS), lh = oc_line_px(core, LABEL_TWIPS);
    int px0, px1, py0, py1, s, k, horizontal = ch->kind == OWF_CHART_BAR, swatch, ok;
    int percent = ch->grouping == OWF_CHART_PERCENT, stacked = ch->grouping != OWF_CHART_CLUSTERED;
    double lo = 0, hi = 0, from, to, step, v;
    char buf[64];

    memset(&p, 0, sizeof p);
    p.core = core;
    p.list = list;
    p.ok = 1;
    p.style.font.font = -1;
    p.cx = x > cx ? x : cx;
    p.cy = y > cy ? y : cy;
    p.cw = (x + w < cx + cw ? x + w : cx + cw) - p.cx;
    p.ch = (y + h < cy + chh ? y + h : cy + chh) - p.cy;
    if (p.cw <= 0 || p.ch <= 0)
        return 1;
    if (pad < 4)
        pad = 4;
    swatch = lh * 3 / 5;

    /* the chart's area: white, in a light grey frame */
    box(&p, x, y, w, h, 0xFFFFFFUL);
    seg(&p, x, y, x + w - 1, y, RGB_CHART_LINE);
    seg(&p, x, y + h - 1, x + w - 1, y + h - 1, RGB_CHART_LINE);
    seg(&p, x, y, x, y + h - 1, RGB_CHART_LINE);
    seg(&p, x + w - 1, y, x + w - 1, y + h - 1, RGB_CHART_LINE);
    px0 = x + pad;
    px1 = x + w - pad;
    py0 = y + pad;
    py1 = y + h - pad;
    if (ch->title && *ch->title) {
        text(&p, ch->title, px0, py0, px1 - px0, th, OC_ALIGN_CENTRE, TITLE_TWIPS, OWF_BOLD);
        py0 += th + pad / 2;
    }
    if (ch->kind == OWF_CHART_OTHER || ch->kind == OWF_CHART_PIE || !ch->nseries) {
        const char *what = !ch->nseries ? "This chart has no data" : ch->kind == OWF_CHART_PIE ? "Pie charts are not drawn yet"
                                                                                              : "This kind of chart is not drawn yet";
        text(&p, what, px0, (py0 + py1 - lh) / 2, px1 - px0, lh, OC_ALIGN_CENTRE, LABEL_TWIPS, 0);
        return p.ok;
    }
    if (!read_data(core, ch, &cd)) {
        data_free(&cd);
        return 0;
    }

    /* the legend: each series' colour and name, at the side the file says */
    if (ch->legend && cd.ns) {
        int widest = 0, total = 0, lx, ly;
        for (s = 0; s < cd.ns; s++) {
            int tw = measure(&p, cd.names[s], LABEL_TWIPS, 0);
            if (tw > widest)
                widest = tw;
            total += swatch + 4 + tw + pad;
        }
        if (ch->legend == 'r' || ch->legend == 'l') {
            int lw = swatch + 4 + widest;
            if (lw > (px1 - px0) / 3)
                lw = (px1 - px0) / 3;
            lx = ch->legend == 'r' ? px1 - lw : px0;
            ly = (py0 + py1 - cd.ns * lh) / 2;
            for (s = 0; s < cd.ns; s++, ly += lh) {
                box(&p, lx, ly + (lh - swatch) / 2, swatch, swatch, series_rgb(&ch->series[s]));
                text(&p, cd.names[s], lx + swatch + 4, ly, lw - swatch - 4, lh, OC_ALIGN_LEFT, LABEL_TWIPS, 0);
            }
            if (ch->legend == 'r')
                px1 -= lw + pad;
            else
                px0 += lw + pad;
        } else {
            lx = px0 + (px1 - px0 - total + pad) / 2;
            if (lx < px0)
                lx = px0;
            ly = ch->legend == 't' ? py0 : py1 - lh;
            for (s = 0; s < cd.ns; s++) {
                int tw = measure(&p, cd.names[s], LABEL_TWIPS, 0);
                box(&p, lx, ly + (lh - swatch) / 2, swatch, swatch, series_rgb(&ch->series[s]));
                text(&p, cd.names[s], lx + swatch + 4, ly, tw + 1, lh, OC_ALIGN_LEFT, LABEL_TWIPS, 0);
                lx += swatch + 4 + tw + pad;
            }
            if (ch->legend == 't')
                py0 += lh + pad / 2;
            else
                py1 -= lh + pad / 2;
        }
    }

    /* the values' range: each point's, or each category's sums when stacked; percentages for 100% */
    for (k = 0; k < cd.n; k++) {
        double pos = 0, neg = 0, all = 0;
        for (s = 0; s < cd.ns; s++) {
            size_t at = (size_t)s * (size_t)cd.n + (size_t)k;
            if (!cd.has[at])
                continue;
            v = cd.v[at];
            if (!stacked) {
                if (v < lo)
                    lo = v;
                if (v > hi)
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

    {
        /* the axes' room: the values' labels and the categories' */
        int along = horizontal ? px1 - px0 : py1 - py0, want = along / (horizontal ? 60 * core->zoom / 100 + 1 : lh * 3 / 2 + 1);
        int vw = 0, cwid = 0, n = cd.n ? cd.n : 1, every = 1, slot, ax0, ax1, ay0, ay1, t, ticks;
        double sw, bw, gap = ch->gap / 100.0, val0;
        scale(ch, lo, hi, want < 2 ? 2 : want > 10 ? 10 : want, &from, &to, &step);
        for (v = from, ticks = 0; v <= to + step / 2 && ticks < 64; v += step, ticks++) {
            number_label(core, v, percent, buf, sizeof buf);
            t = measure(&p, buf, LABEL_TWIPS, 0);
            if (t > vw)
                vw = t;
        }
        for (k = 0; k < cd.n; k++) {
            t = measure(&p, cd.labels[k], LABEL_TWIPS, 0);
            if (t > cwid)
                cwid = t;
        }
        if (horizontal) {
            if (cwid > (px1 - px0) / 3)
                cwid = (px1 - px0) / 3;
            ax0 = px0 + cwid + 4;
            ax1 = px1 - vw / 2;
            ay0 = py0;
            ay1 = py1 - lh - 2;
        } else {
            ax0 = px0 + vw + 4;
            ax1 = px1;
            ay0 = py0 + lh / 2;
            ay1 = py1 - lh - 2;
        }
        if (ax1 - ax0 < 8 || ay1 - ay0 < 8) {
            data_free(&cd);
            return p.ok;
        }
#define VPOS(val) (horizontal ? ax0 + (int)floor(((val) - from) * (ax1 - ax0) / (to - from) + 0.5) \
                              : ay1 - (int)floor(((val) - from) * (ay1 - ay0) / (to - from) + 0.5))
        /* gridlines and the values' labels */
        for (v = from, ticks = 0; v <= to + step / 2 && ticks < 64; v += step, ticks++) {
            int at = VPOS(v);
            number_label(core, fabs(v) < step / 1e6 ? 0 : v, percent, buf, sizeof buf);
            if (horizontal) {
                if (ch->gridlines)
                    seg(&p, at, ay0, at, ay1, RGB_CHART_LINE);
                text(&p, buf, at - 40 * core->zoom / 100, ay1 + 2, 80 * core->zoom / 100, lh, OC_ALIGN_CENTRE, LABEL_TWIPS, 0);
            } else {
                if (ch->gridlines)
                    seg(&p, ax0, at, ax1 - 1, at, RGB_CHART_LINE);
                text(&p, buf, px0, at - lh / 2, vw, lh, OC_ALIGN_RIGHT, LABEL_TWIPS, 0);
            }
        }
        /* the category axis, along 0 (or the scale's end nearest it) */
        val0 = from > 0 ? from : to < 0 ? to : 0;
        if (horizontal)
            seg(&p, VPOS(val0), ay0, VPOS(val0), ay1, RGB_AXIS_LINE);
        else
            seg(&p, ax0, VPOS(val0), ax1 - 1, VPOS(val0), RGB_AXIS_LINE);
        /* the categories' labels: as many as fit */
        sw = (double)(horizontal ? ay1 - ay0 : ax1 - ax0) / n;
        if (!horizontal && sw > 0 && cwid + 4 > sw)
            every = (int)((cwid + 4) / sw) + 1;
        if (horizontal && sw > 0 && lh > sw)
            every = (int)(lh / sw) + 1;
        for (k = 0; k < cd.n; k += every) {
            if (horizontal) {                    /* the first category at the bottom, as Excel draws it */
                int cyy = ay1 - (int)((k + 0.5) * sw) - lh / 2;
                text(&p, cd.labels[k], px0, cyy, cwid, lh, OC_ALIGN_RIGHT, LABEL_TWIPS, 0);
            } else {
                int cxx = ax0 + (int)(k * sw);
                slot = (int)(sw * every);
                text(&p, cd.labels[k], cxx + (int)(sw / 2) - slot / 2, ay1 + 2, slot, lh, OC_ALIGN_CENTRE, LABEL_TWIPS, 0);
            }
        }
        /* the data */
        if (ch->kind == OWF_CHART_COLUMN || ch->kind == OWF_CHART_BAR) {
            bw = stacked ? sw / (1 + gap) : sw / (cd.ns + gap);
            for (k = 0; k < cd.n; k++) {
                double all = 0, pos = 0, neg = 0, start = k * sw + (sw - (stacked ? bw : bw * cd.ns)) / 2;
                for (s = 0; s < cd.ns; s++)
                    if (cd.has[(size_t)s * cd.n + k])
                        all += fabs(cd.v[(size_t)s * cd.n + k]);
                for (s = 0; s < cd.ns; s++) {
                    size_t at = (size_t)s * (size_t)cd.n + (size_t)k;
                    double base, top, a, b;
                    int i0, i1, v0, v1;
                    if (!cd.has[at])
                        continue;
                    v = cd.v[at];
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
                    if (top < from)
                        top = from;
                    if (top > to)
                        top = to;
                    if (base < from)
                        base = from;
                    if (base > to)
                        base = to;
                    a = start + (stacked ? 0 : s * bw);
                    b = a + bw;
                    i0 = (int)floor(a + 0.5);
                    i1 = (int)floor(b + 0.5);
                    v0 = VPOS(base);
                    v1 = VPOS(top);
                    if (horizontal)
                        box(&p, v0 < v1 ? v0 : v1, ay1 - i1, abs(v1 - v0), i1 - i0, series_rgb(&ch->series[s]));
                    else
                        box(&p, ax0 + i0, v0 < v1 ? v0 : v1, i1 - i0, abs(v1 - v0), series_rgb(&ch->series[s]));
                }
            }
        } else {                                 /* lines (and areas, as their lines) through the categories' middles */
            int lt = 3 * core->zoom / 100 > 1 ? 3 * core->zoom / 100 : 1;
            double *sum = calloc((size_t)(cd.n ? cd.n : 1), sizeof *sum), *all = calloc((size_t)(cd.n ? cd.n : 1), sizeof *all);
            if (!sum || !all) {
                free(sum);
                free(all);
                data_free(&cd);
                return 0;
            }
            for (k = 0; k < cd.n; k++)
                for (s = 0; s < cd.ns; s++)
                    if (cd.has[(size_t)s * cd.n + k])
                        all[k] += fabs(cd.v[(size_t)s * cd.n + k]);
            for (s = 0; s < cd.ns; s++) {
                int has_last = 0, lx = 0, lyy = 0;
                for (k = 0; k < cd.n; k++) {
                    size_t at = (size_t)s * (size_t)cd.n + (size_t)k;
                    int xx, yy;
                    if (!cd.has[at]) {
                        has_last = 0;            /* a gap */
                        continue;
                    }
                    v = cd.v[at];
                    if (percent)
                        v = all[k] > 0 ? v / all[k] : 0;
                    if (stacked) {
                        sum[k] += v;
                        v = sum[k];
                    }
                    if (v < from)
                        v = from;
                    if (v > to)
                        v = to;
                    xx = ax0 + (int)floor((k + 0.5) * sw + 0.5);
                    yy = VPOS(v);
                    if (has_last)
                        thick(&p, lx, lyy, xx, yy, lt, series_rgb(&ch->series[s]));
                    else if (k + 1 >= cd.n || !cd.has[at + 1])    /* a point alone: a dot */
                        box(&p, xx - lt, yy - lt, 2 * lt + 1, 2 * lt + 1, series_rgb(&ch->series[s]));
                    lx = xx;
                    lyy = yy;
                    has_last = 1;
                }
            }
            free(sum);
            free(all);
        }
#undef VPOS
    }
    ok = p.ok;
    data_free(&cd);
    return ok;
}

/*
 * opj_view.c: GFOpenProject's Gantt view (openproject_core.h): the task table on the left, a two-tier
 * timescale and the bars on the right, link lines with arrows, milestones as diamonds, summary bars and
 * the critical path in red. Everything is drawn with GFOpenLayout's drawing calls (gfopenlayout_draw.h):
 * no graphics.library. Lines are one-pixel fills, so the view needs nothing more than fills and text.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_core.h"

#include <stdio.h>
#include <string.h>

#define SPLIT 4                          /* the bar between the table and the chart */
#define DAY 1440L
#define OUT 6                            /* how far a link's line leaves a bar before it turns */

static long floor_div(long a, long b)
{
    long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int opj_zoom_day_width(int zoom)
{
    static const int width[OPJ_NZOOMS] = { 24, 6, 2, 1 };
    return zoom >= 0 && zoom < OPJ_NZOOMS ? width[zoom] : width[0];
}

static int sheet_width(const opj_view *v)
{
    int c, w = 0;
    if (v->sheet_width > 0) return v->sheet_width;
    for (c = 0; c < OPJ_NCOLS; c++) w += v->columns[c] > 0 ? v->columns[c] : 0;
    return w;
}

void opj_view_init(opj_view *v, const opj_doc *doc, int width, int height, int row_height, int zoom)
{
    static const int widths[OPJ_NCOLS] = { 32, 180, 56, 96, 96, 84, 0, 0, 0, 0, 0, 0, 0 };
    long day;
    memset(v, 0, sizeof *v);
    v->width = width;
    v->height = height;
    v->row_height = row_height > 4 ? row_height : 16;
    memcpy(v->columns, widths, sizeof widths);
    v->zoom = zoom >= 0 && zoom < OPJ_NZOOMS ? zoom : OPJ_ZOOM_DAYS;
    day = floor_div(doc && doc->plan ? doc->plan->start : 0, DAY);
    day -= (owf_ptime_weekday(day * DAY) + 6) % 7;                 /* back to the Monday */
    v->origin = day * DAY;
    v->selected = -1;
}

int opj_chart_left(const opj_view *v)
{
    return sheet_width(v) + SPLIT;
}

long opj_view_x(const opj_view *v, owf_ptime t)
{
    long dt = t - v->origin, days = floor_div(dt, DAY), dw = opj_zoom_day_width(v->zoom);
    if (days > 4000000L) days = 4000000L;                          /* far off the view: kept from overflowing */
    if (days < -4000000L) days = -4000000L;
    return opj_chart_left(v) + days * dw + (dt - floor_div(dt, DAY) * DAY) * dw / DAY;
}

owf_ptime opj_view_time(const opj_view *v, long x)
{
    long dx = x - opj_chart_left(v), dw = opj_zoom_day_width(v->zoom), days = floor_div(dx, dw);
    return v->origin + days * DAY + (dx - days * dw) * DAY / dw;
}

int opj_row_y(const opj_view *v, int row)
{
    return 2 * v->row_height + (row - v->first_row) * v->row_height;
}

int opj_rows_shown(const opj_view *v)
{
    int room = v->height - 2 * v->row_height;
    return room > 0 ? (room + v->row_height - 1) / v->row_height : 0;
}

/* ---- Bars and lines ---- */

int opj_bar_of(const opj_view *v, const opj_doc *doc, int row, opj_bar *bar)
{
    const owf_ptask *t;
    int y, rh = v->row_height;
    long x0, x1;
    if (!doc || row < 0 || row >= doc->plan->ntasks) return 0;
    t = &doc->plan->tasks[row];
    y = opj_row_y(v, row);
    x0 = opj_view_x(v, t->early_start);
    x1 = opj_view_x(v, t->early_finish);
    memset(bar, 0, sizeof *bar);
    bar->critical = t->critical;
    if (t->summary) {
        bar->kind = OPJ_BAR_SUMMARY;
        bar->bar.x = (int)x0;
        bar->bar.w = x1 > x0 ? (int)(x1 - x0) : 1;
        bar->bar.y = y + rh / 4;
        bar->bar.h = rh / 2;
    } else if (t->milestone) {
        int s = (rh / 2) | 1;
        bar->kind = OPJ_BAR_MILESTONE;
        bar->bar.x = (int)x1 - s / 2;
        bar->bar.y = y + (rh - s) / 2;
        bar->bar.w = bar->bar.h = s;
    } else {
        bar->kind = OPJ_BAR_TASK;
        bar->bar.x = (int)x0;
        bar->bar.w = x1 > x0 ? (int)(x1 - x0) : 1;
        bar->bar.y = y + rh / 4;
        bar->bar.h = rh / 2;
        if (t->percent > 0) {
            int third = bar->bar.h / 3;
            bar->done.x = bar->bar.x;
            bar->done.w = (int)((long)bar->bar.w * t->percent / 100);
            if (bar->done.w < 1) bar->done.w = 1;
            bar->done.y = bar->bar.y + third;
            bar->done.h = bar->bar.h - 2 * third;
        }
    }
    return 1;
}

int opj_link_path(const opj_view *v, const opj_doc *doc, int row, int link, opj_path *path)
{
    const owf_plink *l;
    opj_bar from, to;
    int from_finish, into_start, x1, y1, x2, y2, sx, rh = v->row_height;
    if (!doc || row < 0 || row >= doc->plan->ntasks || link < 0 || link >= doc->plan->tasks[row].nlinks) return 0;
    l = &doc->plan->tasks[row].links[link];
    if (!opj_bar_of(v, doc, l->pred, &from) || !opj_bar_of(v, doc, row, &to)) return 0;
    from_finish = l->type == OWF_LINK_FS || l->type == OWF_LINK_FF;
    into_start = l->type == OWF_LINK_FS || l->type == OWF_LINK_SS;
    x1 = from_finish ? from.bar.x + from.bar.w : from.bar.x - 1;
    y1 = opj_row_y(v, l->pred) + rh / 2;
    x2 = into_start ? to.bar.x - 1 : to.bar.x + to.bar.w;
    y2 = opj_row_y(v, row) + rh / 2;
    sx = from_finish ? x1 + OUT : x1 - OUT;
    path->x[0] = x1;
    path->y[0] = y1;
    path->x[1] = sx;
    path->y[1] = y1;
    if ((into_start && sx <= x2 - 3) || (!into_start && sx >= x2 + 3)) {
        path->x[2] = sx;                 /* room to come straight in: down (or up), then across */
        path->y[2] = y2;
        path->x[3] = x2;
        path->y[3] = y2;
        path->n = 4;
    } else {
        int ex = into_start ? x2 - OUT : x2 + OUT;
        int ym = y2 > y1 ? opj_row_y(v, row) : opj_row_y(v, row) + rh;   /* along the edge of the row */
        path->x[2] = sx;
        path->y[2] = ym;
        path->x[3] = ex;
        path->y[3] = ym;
        path->x[4] = ex;
        path->y[4] = y2;
        path->x[5] = x2;
        path->y[5] = y2;
        path->n = 6;
    }
    return 1;
}

/* ---- Drawing ---- */

static void fill(oldraw *d, int x, int y, int w, int h, oldraw_colour c)
{
    oldraw_rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    oldraw_fill(d, &r, c);
}

static void line(oldraw *d, int x0, int y0, int x1, int y1, oldraw_colour c)
{
    if (y0 == y1) fill(d, x0 < x1 ? x0 : x1, y0, (x0 < x1 ? x1 - x0 : x0 - x1) + 1, 1, c);
    else if (x0 == x1) fill(d, x0, y0 < y1 ? y0 : y1, 1, (y0 < y1 ? y1 - y0 : y0 - y1) + 1, c);
}

static void draw_path(oldraw *d, const opj_path *p, oldraw_colour c)
{
    int i, k, dir;
    for (i = 1; i < p->n; i++) line(d, p->x[i - 1], p->y[i - 1], p->x[i], p->y[i], c);
    /* The arrow at the tip: a small triangle pointing along the last stretch. */
    dir = p->x[p->n - 1] >= p->x[p->n - 2] ? 1 : -1;
    for (k = 0; k < 4; k++) fill(d, p->x[p->n - 1] - dir * k, p->y[p->n - 1] - k, 1, 2 * k + 1, c);
}

static void draw_bar(oldraw *d, const opj_bar *b)
{
    oldraw_colour c = b->critical ? OPJ_COLOUR_CRITICAL : OPJ_COLOUR_BAR;
    int k;
    switch (b->kind) {
    case OPJ_BAR_TASK:
        oldraw_fill(d, &b->bar, c);
        if (b->done.w) oldraw_fill(d, &b->done, OPJ_COLOUR_DONE);
        break;
    case OPJ_BAR_SUMMARY: {
        int band = b->bar.h / 2, tab = b->bar.h - band, w = b->bar.w;
        fill(d, b->bar.x, b->bar.y, w, band, OPJ_COLOUR_SUMMARY);
        for (k = 0; k < tab; k++) {      /* the tabs hanging at each end */
            int tw = tab - k < w / 2 ? tab - k : w / 2;
            if (tw < 1) tw = 1;
            fill(d, b->bar.x, b->bar.y + band + k, tw, 1, OPJ_COLOUR_SUMMARY);
            fill(d, b->bar.x + w - tw, b->bar.y + band + k, tw, 1, OPJ_COLOUR_SUMMARY);
        }
        break;
    }
    default: {                           /* a diamond, one row of pixels at a time */
        int half = b->bar.w / 2, cx = b->bar.x + half, cy = b->bar.y + half;
        for (k = 0; k <= half; k++) {
            fill(d, cx - (half - k), cy - k, 2 * (half - k) + 1, 1, c == OPJ_COLOUR_BAR ? OPJ_COLOUR_SUMMARY : c);
            if (k) fill(d, cx - (half - k), cy + k, 2 * (half - k) + 1, 1, c == OPJ_COLOUR_BAR ? OPJ_COLOUR_SUMMARY : c);
        }
        break;
    }
    }
}

/* ---- The timescale ---- */

enum { UNIT_DAY, UNIT_WEEK, UNIT_MONTH, UNIT_QUARTER, UNIT_YEAR };
static const char month_names[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char day_letters[7][2] = { "S", "M", "T", "W", "T", "F", "S" };

/* The start of the unit holding t. */
static owf_ptime unit_start(owf_ptime t, int unit)
{
    int y, mo, d;
    long day = floor_div(t, DAY);
    switch (unit) {
    case UNIT_DAY: return day * DAY;
    case UNIT_WEEK: return (day - (owf_ptime_weekday(day * DAY) + 6) % 7) * DAY;
    default: break;
    }
    owf_ptime_split(day * DAY, &y, &mo, &d, NULL, NULL);
    if (unit == UNIT_QUARTER) mo = (mo - 1) / 3 * 3 + 1;
    if (unit == UNIT_YEAR) mo = 1;
    return owf_ptime_make(y, mo, 1, 0, 0);
}

static owf_ptime unit_next(owf_ptime t, int unit)
{
    int y, mo, d;
    switch (unit) {
    case UNIT_DAY: return t + DAY;
    case UNIT_WEEK: return t + 7 * DAY;
    default: break;
    }
    owf_ptime_split(t, &y, &mo, &d, NULL, NULL);
    mo += unit == UNIT_MONTH ? 1 : unit == UNIT_QUARTER ? 3 : 12;
    while (mo > 12) mo -= 12, y++;
    return owf_ptime_make(y, mo, 1, 0, 0);
}

static void unit_label(owf_ptime t, int unit, int top, char *out)
{
    int y, mo, d;
    owf_ptime_split(t, &y, &mo, &d, NULL, NULL);
    switch (unit) {
    case UNIT_DAY: strcpy(out, day_letters[owf_ptime_weekday(t)]); break;
    case UNIT_WEEK:
        if (top) sprintf(out, "%d %s '%02d", d, month_names[mo - 1], y % 100);
        else sprintf(out, "%d", d);
        break;
    case UNIT_MONTH:
        if (top) sprintf(out, "%s %d", month_names[mo - 1], y);
        else strcpy(out, month_names[mo - 1]);
        break;
    case UNIT_QUARTER:
        if (top) sprintf(out, "Q%d %d", (mo - 1) / 3 + 1, y);
        else sprintf(out, "Q%d", (mo - 1) / 3 + 1);
        break;
    default: sprintf(out, "%d", y); break;
    }
}

static void draw_tier(oldraw *d, const opj_view *v, const oldraw_rect *area, int unit, int top, int y)
{
    owf_ptime t = unit_start(opj_view_time(v, area->x > opj_chart_left(v) ? area->x : opj_chart_left(v)), unit);
    int guard;
    for (guard = 0; guard < 4000; guard++) {
        owf_ptime next = unit_next(t, unit);
        long x0 = opj_view_x(v, t), x1 = opj_view_x(v, next);
        oldraw_rect cell;
        char label[24];
        if (x0 >= area->x + area->w || x0 >= v->width) break;
        cell.x = (int)x0;
        cell.y = y;
        cell.w = (int)(x1 - x0);
        cell.h = v->row_height;
        oldraw_frame(d, &cell, 0);
        unit_label(t, unit, top, label);
        cell.x += 2;
        cell.w -= 4;
        if (cell.w > 0) oldraw_text_box(d, &cell, label, (size_t)-1, &v->font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0,
                                        top ? OLDRAW_LEFT : OLDRAW_CENTRE);
        t = next;
    }
}

static void draw_timescale(oldraw *d, const opj_view *v, const oldraw_rect *area)
{
    static const int tiers[OPJ_NZOOMS][2] = {
        { UNIT_WEEK, UNIT_DAY }, { UNIT_MONTH, UNIT_WEEK }, { UNIT_QUARTER, UNIT_MONTH }, { UNIT_YEAR, UNIT_QUARTER },
    };
    int z = v->zoom >= 0 && v->zoom < OPJ_NZOOMS ? v->zoom : 0;
    fill(d, opj_chart_left(v), 0, v->width - opj_chart_left(v), 2 * v->row_height, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
    draw_tier(d, v, area, tiers[z][0], 1, 0);
    draw_tier(d, v, area, tiers[z][1], 0, v->row_height);
}

/* Days off on the project's calendar, shaded down the chart (when a day is wide enough to see). */
static void draw_nonworking(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area)
{
    const owf_pcal *cal = &doc->plan->calendars[doc->plan->calendar];
    int left = area->x > opj_chart_left(v) ? area->x : opj_chart_left(v), right = area->x + area->w, top = 2 * v->row_height;
    long day;
    if (opj_zoom_day_width(v->zoom) < 6) return;
    for (day = floor_div(opj_view_time(v, left), DAY); ; day++) {
        long x0 = opj_view_x(v, day * DAY), x1 = opj_view_x(v, (day + 1) * DAY);
        if (x0 >= right) break;
        if (!owf_pcal_day(cal, day)->nspans) fill(d, (int)x0, top, (int)(x1 - x0), v->height - top, OPJ_COLOUR_NONWORKING);
    }
}

static void draw_sheet_titles(oldraw *d, const opj_view *v)
{
    int c, x = 0;
    for (c = 0; c < OPJ_NCOLS; c++) {
        oldraw_rect r;
        if (v->columns[c] <= 0) continue;
        r.x = x;
        r.y = 0;
        r.w = v->columns[c];
        r.h = 2 * v->row_height;
        fill(d, r.x, r.y, r.w, r.h, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        oldraw_frame(d, &r, 0);
        r.x += 3;
        r.w -= 6;
        if (r.w > 0) oldraw_text_box(d, &r, opj_column_title(c), (size_t)-1, &v->font, OLDRAW_PEN(OLDRAW_PEN_TEXT),
                                     OLDRAW_BOLD, OLDRAW_CENTRE);
        x += v->columns[c];
    }
}

int opj_baseline_bar(const opj_view *v, const opj_doc *doc, int row, oldraw_rect *bar)
{
    const owf_ptask *t;
    long x0, x1;
    int rh;
    if (!v || !bar || !v->show_baseline || !doc || row < 0 || row >= doc->plan->ntasks) return 0;
    t = &doc->plan->tasks[row];
    if (!t->baselined) return 0;
    rh = v->row_height;
    x0 = opj_view_x(v, t->baseline_start);
    x1 = opj_view_x(v, t->baseline_finish);
    bar->x = (int)x0;
    bar->w = x1 > x0 ? (int)(x1 - x0) : 1;
    bar->y = opj_row_y(v, row) + rh * 3 / 4 + 1;
    bar->h = rh / 6 > 1 ? rh / 6 : 2;
    return 1;
}

#define LABEL_GAP 6
#define LABEL_WIDTH 240

int opj_label_box(const opj_view *v, const opj_doc *doc, int row, oldraw_rect *box)
{
    opj_bar b;
    int i;
    if (!v || !box || !opj_bar_of(v, doc, row, &b)) return 0;
    for (i = 0; i < doc->plan->nassigns; i++)
        if (doc->plan->assigns[i].task == row) break;
    if (i == doc->plan->nassigns) return 0;
    box->x = b.bar.x + b.bar.w + LABEL_GAP;
    box->y = opj_row_y(v, row);
    box->w = LABEL_WIDTH;
    box->h = v->row_height;
    return 1;
}

static void draw_sheet_row(oldraw *d, const opj_view *v, const opj_doc *doc, int row)
{
    const owf_ptask *t = &doc->plan->tasks[row];
    int c, x = 0, y = opj_row_y(v, row), sw = sheet_width(v);
    int selected = row == v->selected;
    oldraw_colour ink = OLDRAW_PEN(selected ? OLDRAW_PEN_FILLTEXT : OLDRAW_PEN_TEXT);
    fill(d, 0, y, sw, v->row_height, OLDRAW_PEN(selected ? OLDRAW_PEN_FILL : OLDRAW_PEN_BACKGROUND));
    for (c = 0; c < OPJ_NCOLS; c++) {
        char text[256];
        oldraw_rect r;
        int indent = 0;
        if (v->columns[c] <= 0) continue;
        if (c == OPJ_COL_NAME) indent = (t->level - 1) * (v->row_height * 3 / 4);
        opj_cell_text(doc, row, c, text, sizeof text);
        r.x = x + 3 + indent;
        r.y = y;
        r.w = v->columns[c] - 6 - indent;
        r.h = v->row_height;
        if (r.w > 0)
            oldraw_text_box(d, &r, text, (size_t)-1, &v->font, ink, c == OPJ_COL_NAME && t->summary ? OLDRAW_BOLD : 0,
                            c == OPJ_COL_ID || c == OPJ_COL_PERCENT ? OLDRAW_RIGHT : OLDRAW_LEFT);
        x += v->columns[c];
        fill(d, x - 1, y, 1, v->row_height, OPJ_COLOUR_GRID);
    }
    fill(d, 0, y + v->row_height - 1, sw, 1, OPJ_COLOUR_GRID);
}

static int crosses(const oldraw_rect *a, int x, int y, int w, int h)
{
    return x < a->x + a->w && x + w > a->x && y < a->y + a->h && y + h > a->y;
}

void opj_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area)
{
    oldraw_rect all;
    int row, last, i, j, sw = sheet_width(v), cl = opj_chart_left(v), rh = v->row_height;
    if (!d || !v || !doc) return;
    if (!area) {
        all.x = all.y = 0;
        all.w = v->width;
        all.h = v->height;
        area = &all;
    }
    oldraw_clip(d, area);
    last = v->first_row + opj_rows_shown(v);
    if (last > doc->plan->ntasks) last = doc->plan->ntasks;

    /* The chart: background, days off, bars, then the links over them, then the timescale on top. */
    if (crosses(area, cl, 0, v->width - cl, v->height)) {
        fill(d, cl, 2 * rh, v->width - cl, v->height - 2 * rh, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        draw_nonworking(d, v, doc, area);
        if (v->selected >= v->first_row && v->selected < last)
            fill(d, cl, opj_row_y(v, v->selected), v->width - cl, rh, OPJ_COLOUR_NONWORKING);
        for (row = v->first_row; row < last; row++) {
            opj_bar b;
            if (opj_bar_of(v, doc, row, &b) && crosses(area, b.bar.x, b.bar.y, b.bar.w, b.bar.h)) draw_bar(d, &b);
        }
        for (row = v->first_row; row < last; row++) {            /* baselines under them */
            oldraw_rect r;
            if (opj_baseline_bar(v, doc, row, &r) && crosses(area, r.x, r.y, r.w, r.h))
                fill(d, r.x, r.y, r.w, r.h, OPJ_COLOUR_BASELINE);
        }
        for (row = v->first_row; row < last; row++) {            /* resource names beside the bars */
            oldraw_rect r;
            char text[256];
            if (!opj_label_box(v, doc, row, &r) || !crosses(area, r.x, r.y, r.w, r.h)) continue;
            owf_presource_names_text(doc->plan, row, text, sizeof text);
            oldraw_text_box(d, &r, text, (size_t)-1, &v->font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT);
        }
        for (i = 0; i < doc->plan->ntasks; i++)
            for (j = 0; j < doc->plan->tasks[i].nlinks; j++) {
                opj_path p;
                int k, x0, x1, y0, y1;
                if (!opj_link_path(v, doc, i, j, &p)) continue;
                x0 = x1 = p.x[0];
                y0 = y1 = p.y[0];
                for (k = 1; k < p.n; k++) {
                    if (p.x[k] < x0) x0 = p.x[k];
                    if (p.x[k] > x1) x1 = p.x[k];
                    if (p.y[k] < y0) y0 = p.y[k];
                    if (p.y[k] > y1) y1 = p.y[k];
                }
                if (crosses(area, x0 - 4, y0 - 4, x1 - x0 + 9, y1 - y0 + 9)) draw_path(d, &p, OPJ_COLOUR_LINK);
            }
        draw_timescale(d, v, area);
    }
    /* The bar between the two halves. */
    if (crosses(area, sw, 0, SPLIT, v->height)) {
        oldraw_rect r;
        r.x = sw;
        r.y = 0;
        r.w = SPLIT;
        r.h = v->height;
        fill(d, r.x, r.y, r.w, r.h, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        oldraw_frame(d, &r, 0);
    }
    /* The table. */
    if (crosses(area, 0, 0, sw, v->height)) {
        fill(d, 0, 2 * rh, sw, v->height - 2 * rh, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        for (row = v->first_row; row < last; row++)
            if (crosses(area, 0, opj_row_y(v, row), sw, rh)) draw_sheet_row(d, v, doc, row);
        draw_sheet_titles(d, v);
    }
    oldraw_clip(d, NULL);
}

/* ---- Hit tests ---- */

void opj_hit_test(const opj_view *v, const opj_doc *doc, int x, int y, opj_hit *hit)
{
    int sw = sheet_width(v), cl = opj_chart_left(v), rh = v->row_height, row;
    memset(hit, 0, sizeof *hit);
    hit->row = hit->column = -1;
    if (x < 0 || y < 0 || x >= v->width || y >= v->height || !doc) return;
    if (x >= sw && x < cl) {
        hit->what = OPJ_HIT_SPLIT;
        return;
    }
    if (x < sw) {
        int c, cx = 0;
        for (c = 0; c < OPJ_NCOLS; c++) {
            if (v->columns[c] <= 0) continue;
            if (x < cx + v->columns[c]) break;
            cx += v->columns[c];
        }
        if (c == OPJ_NCOLS) return;
        hit->column = c;
    } else {
        hit->time = opj_view_time(v, x);
    }
    if (y < 2 * rh) {
        hit->what = x < sw ? OPJ_HIT_COLUMN_TITLE : OPJ_HIT_TIMESCALE;
        return;
    }
    row = v->first_row + (y - 2 * rh) / rh;
    if (row >= doc->plan->ntasks) row = -1;
    hit->row = row;
    if (x < sw) {
        hit->what = row >= 0 ? OPJ_HIT_CELL : OPJ_HIT_NONE;
        return;
    }
    hit->what = OPJ_HIT_CHART;
    if (row >= 0) {
        opj_bar b;
        if (opj_bar_of(v, doc, row, &b) && x >= b.bar.x - 2 && x < b.bar.x + b.bar.w + 2 && y >= b.bar.y &&
            y < b.bar.y + b.bar.h) {
            hit->what = OPJ_HIT_BAR;
            if (b.kind == OPJ_BAR_TASK && x >= b.bar.x + b.bar.w - 3) hit->part = 1;
            else if (b.done.w && x < b.done.x + b.done.w && y >= b.done.y && y < b.done.y + b.done.h) hit->part = 2;
        }
    }
}

/* ---- Printing ---- */

int opj_print_layout(opj_print *pr, const opj_view *proto, const opj_doc *doc, int page_width, int page_height)
{
    int rh, dw, first_chart, more_chart, ntasks;
    long days, end, i;
    if (!pr || !proto || !doc) return 0;
    memset(pr, 0, sizeof *pr);
    pr->proto = *proto;
    pr->proto.selected = -1;
    pr->proto.first_row = 0;
    pr->page_width = page_width;
    pr->page_height = page_height;
    rh = proto->row_height;
    dw = opj_zoom_day_width(proto->zoom);
    pr->rows_per_page = (page_height - 3 * rh) / rh;           /* the timescale's two rows and the footer */
    first_chart = page_width - opj_chart_left(proto);
    more_chart = page_width - SPLIT;
    pr->first_days = first_chart / dw;
    pr->more_days = more_chart / dw;
    if (pr->rows_per_page < 1 || pr->first_days < 1 || pr->more_days < 1) return 0;
    ntasks = doc->plan->ntasks;
    pr->down = ntasks > 0 ? (ntasks + pr->rows_per_page - 1) / pr->rows_per_page : 1;
    end = doc->plan->start;
    for (i = 0; i < ntasks; i++)
        if (doc->plan->tasks[i].early_finish > end) end = doc->plan->tasks[i].early_finish;
    days = floor_div(end - proto->origin, DAY) + 2;             /* to a day past the finish */
    pr->across = 1;
    if (days > pr->first_days) {
        long more = (days - pr->first_days + pr->more_days - 1) / pr->more_days;
        pr->across += more > 1000 ? 1000 : (int)more;
    }
    if ((long)pr->across * pr->down > 100000L) pr->across = (int)(100000L / pr->down);
    return pr->across * pr->down;
}

int opj_print_page_view(const opj_print *pr, int page, opj_view *v)
{
    int down, across, c;
    if (!pr || !v || page < 0 || page >= pr->across * pr->down) return 0;
    across = page / pr->down;
    down = page % pr->down;
    *v = pr->proto;
    v->width = pr->page_width;
    v->height = 2 * v->row_height + pr->rows_per_page * v->row_height;
    v->first_row = down * pr->rows_per_page;
    if (across > 0) {
        for (c = 0; c < OPJ_NCOLS; c++) v->columns[c] = 0;
        v->sheet_width = 0;
        v->origin = pr->proto.origin + (pr->first_days + (long)(across - 1) * pr->more_days) * DAY;
    }
    return 1;
}

void opj_print_draw(oldraw *d, const opj_print *pr, const opj_doc *doc, int page)
{
    opj_view v;
    oldraw_rect r;
    char text[64];
    if (!d || !doc || !opj_print_page_view(pr, page, &v)) return;
    r.x = r.y = 0;
    r.w = pr->page_width;
    r.h = pr->page_height;
    oldraw_clip(d, &r);
    fill(d, 0, 0, r.w, r.h, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
    oldraw_clip(d, NULL);
    opj_draw(d, &v, doc, NULL);
    r.y = pr->page_height - v.row_height;
    r.h = v.row_height;
    r.x = 4;
    r.w = pr->page_width / 2 - 8;
    oldraw_text_box(d, &r, doc->plan->title ? doc->plan->title : "", (size_t)-1, &v.font,
                    OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT);
    sprintf(text, "Page %d of %d", page + 1, pr->across * pr->down);
    r.x = pr->page_width / 2;
    oldraw_text_box(d, &r, text, (size_t)-1, &v.font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_RIGHT);
}

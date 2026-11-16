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
    long day;
    int c;
    memset(v, 0, sizeof *v);
    v->width = width;
    v->height = height;
    v->row_height = row_height > 4 ? row_height : 16;
    for (c = OPJ_COL_ID; c <= OPJ_COL_PREDECESSORS; c++) v->columns[c] = opj_column_width(c);
    v->zoom = zoom >= 0 && zoom < OPJ_NZOOMS ? zoom : OPJ_ZOOM_DAYS;
    day = floor_div(doc && doc->plan ? doc->plan->start : 0, DAY);
    day -= (owf_ptime_weekday(day * DAY) + 6) % 7;                 /* back to the Monday */
    v->origin = day * DAY;
    v->selected = -1;
}

int opj_column_width(int column)
{
    static const int widths[OPJ_NCOLS] = { 32, 180, 56, 96, 96, 84, 64, 48, 140, 96, 96, 72, 72, 140, 96, 160,
                                                   96, 96, 96, 96, 80, 80, 80, 80, 80, 96 };
    return column >= 0 && column < OPJ_NCOLS ? widths[column] : 0;
}

int opj_view_column_shown(const opj_view *v, int column)
{
    return v && column >= 0 && column < OPJ_NCOLS && v->columns[column] > 0;
}

int opj_view_show_column(opj_view *v, int column, int show)
{
    int c, others = 0;
    if (!v || column < 0 || column >= OPJ_NCOLS) return 0;
    if (show) {
        if (v->columns[column] <= 0) v->columns[column] = opj_column_width(column);
        return 1;
    }
    for (c = 0; c < OPJ_NCOLS; c++)
        if (c != column && v->columns[c] > 0) others++;
    if (!others && v->columns[column] > 0) return 0;              /* the table keeps one column */
    v->columns[column] = 0;
    return 1;
}

int opj_view_near_column(const opj_view *v, int column)
{
    int c;
    if (!v) return -1;
    if (column < 0) column = 0;
    if (column >= OPJ_NCOLS) column = OPJ_NCOLS - 1;
    for (c = column; c < OPJ_NCOLS; c++)
        if (v->columns[c] > 0) return c;
    for (c = column - 1; c >= 0; c--)
        if (v->columns[c] > 0) return c;
    return -1;
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

/* The view's lines when it has some worked out, else NULL (a line is a task). */
static const opj_lines *lines_of(const opj_view *v)
{
    return v && v->lines && v->lines->line_of ? v->lines : NULL;
}

/* A task row's line; -1 hidden (or out of the lines' range). */
static int view_line(const opj_view *v, int row)
{
    const opj_lines *l = lines_of(v);
    if (!l) return row;
    return row >= 0 && row < l->ntasks ? l->line_of[row] : -1;
}

#define HIDDEN_Y (-1000000)              /* a hidden task's row: far above the view */

int opj_row_y(const opj_view *v, int row)
{
    int line = view_line(v, row);
    if (line < 0 && lines_of(v)) return HIDDEN_Y;
    return 2 * v->row_height + (line - v->first_row) * v->row_height;
}

int opj_view_lines(const opj_view *v, const opj_doc *doc)
{
    const opj_lines *l = lines_of(v);
    if (!doc || !doc->plan) return 0;
    return l ? l->n : doc->plan->ntasks;
}

int opj_view_line_task(const opj_view *v, const opj_doc *doc, int line)
{
    const opj_lines *l = lines_of(v);
    int task;
    if (!doc || !doc->plan || line < 0 || line >= opj_view_lines(v, doc)) return -1;
    task = l ? l->task[line] : line;
    return task < doc->plan->ntasks ? task : -1;
}

int opj_view_task_line(const opj_view *v, const opj_doc *doc, int task)
{
    if (!doc || !doc->plan || task < 0 || task >= doc->plan->ntasks) return -1;
    return view_line(v, task);
}

int opj_rows_shown(const opj_view *v)
{
    int room = v->height - 2 * v->row_height;
    return room > 0 ? (room + v->row_height - 1) / v->row_height : 0;
}

/* ---- Bars and lines ---- */

/* The Tracking Gantt's halves: the bar from TRACK_TOP down, the baseline under it, each track_h high. */
#define TRACK_TOP 2
static int track_h(int rh)
{
    int h = (rh - 2 * TRACK_TOP - 1) / 2;
    return h > 1 ? h : 1;
}

int opj_bar_of(const opj_view *v, const opj_doc *doc, int row, opj_bar *bar)
{
    const owf_ptask *t;
    int y, rh = v->row_height;
    long x0, x1;
    if (!doc || row < 0 || row >= doc->plan->ntasks || view_line(v, row) < 0) return 0;
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
        bar->bar.y = v->tracking ? y + TRACK_TOP : y + rh / 4;
        bar->bar.h = v->tracking ? track_h(rh) : rh / 2;
        if (t->percent > 0) {
            int third = v->tracking ? 0 : bar->bar.h / 3;   /* Tracking Gantt: the done part is solid */
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
    if (!v || !bar || !(v->show_baseline || v->tracking) || !doc || row < 0 || row >= doc->plan->ntasks ||
        view_line(v, row) < 0)
        return 0;
    t = &doc->plan->tasks[row];
    if (!t->baselined) return 0;
    rh = v->row_height;
    x0 = opj_view_x(v, t->baseline_start);
    x1 = opj_view_x(v, t->baseline_finish);
    bar->x = (int)x0;
    bar->w = x1 > x0 ? (int)(x1 - x0) : 1;
    if (v->tracking) {                           /* the lower half, as high as the bar */
        bar->y = opj_row_y(v, row) + TRACK_TOP + track_h(rh) + 1;
        bar->h = track_h(rh);
        return 1;
    }
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
    for (i = 0; i < doc->plan->nassigns && !v->tracking; i++)
        if (doc->plan->assigns[i].task == row) break;
    if (!v->tracking && i == doc->plan->nassigns) return 0;
    box->x = b.bar.x + b.bar.w + LABEL_GAP;
    box->y = opj_row_y(v, row);
    box->w = LABEL_WIDTH;
    box->h = v->row_height;
    return 1;
}

void opj_label_text(const opj_view *v, const opj_doc *doc, int row, char *out, size_t size)
{
    if (!out || !size) return;
    out[0] = 0;
    if (!v || !doc || row < 0 || row >= doc->plan->ntasks) return;
    if (v->tracking) snprintf(out, size, "%d%%", doc->plan->tasks[row].percent);
    else owf_presource_names_text(doc->plan, row, out, size);
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

/* A group's heading line across the table: shaded, its column and value in bold. */
static void draw_heading_row(oldraw *d, const opj_view *v, const opj_doc *doc, int line)
{
    int y = 2 * v->row_height + (line - v->first_row) * v->row_height, sw = sheet_width(v);
    char text[320];
    oldraw_rect r;
    fill(d, 0, y, sw, v->row_height, OPJ_COLOUR_HEADING);
    opj_lines_heading_text(v->lines, doc, line, text, sizeof text);
    r.x = 3;
    r.y = y;
    r.w = sw - 6;
    r.h = v->row_height;
    if (r.w > 0)
        oldraw_text_box(d, &r, text, (size_t)-1, &v->font, OLDRAW_PEN(OLDRAW_PEN_TEXT), OLDRAW_BOLD, OLDRAW_LEFT);
    fill(d, 0, y + v->row_height - 1, sw, 1, OPJ_COLOUR_GRID);
}

static int crosses(const oldraw_rect *a, int x, int y, int w, int h)
{
    return x < a->x + a->w && x + w > a->x && y < a->y + a->h && y + h > a->y;
}

void opj_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area)
{
    oldraw_rect all;
    int row, line, last, i, j, sw = sheet_width(v), cl = opj_chart_left(v), rh = v->row_height, sel;
    if (!d || !v || !doc) return;
    if (!area) {
        all.x = all.y = 0;
        all.w = v->width;
        all.h = v->height;
        area = &all;
    }
    oldraw_clip(d, area);
    last = v->first_row + opj_rows_shown(v);
    if (last > opj_view_lines(v, doc)) last = opj_view_lines(v, doc);
    sel = opj_view_task_line(v, doc, v->selected);

    /* The chart: background, days off, bars, then the links over them, then the timescale on top. */
    if (crosses(area, cl, 0, v->width - cl, v->height)) {
        fill(d, cl, 2 * rh, v->width - cl, v->height - 2 * rh, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        draw_nonworking(d, v, doc, area);
        for (line = v->first_row; line < last; line++)          /* the groups' headings, shaded across */
            if (opj_view_line_task(v, doc, line) < 0)
                fill(d, cl, 2 * rh + (line - v->first_row) * rh, v->width - cl, rh, OPJ_COLOUR_HEADING);
        if (sel >= v->first_row && sel < last)
            fill(d, cl, opj_row_y(v, v->selected), v->width - cl, rh, OPJ_COLOUR_NONWORKING);
        for (line = v->first_row; line < last; line++) {
            opj_bar b;
            row = opj_view_line_task(v, doc, line);
            if (opj_bar_of(v, doc, row, &b) && crosses(area, b.bar.x, b.bar.y, b.bar.w, b.bar.h)) draw_bar(d, &b);
        }
        for (line = v->first_row; line < last; line++) {          /* baselines under them */
            oldraw_rect r;
            row = opj_view_line_task(v, doc, line);
            if (opj_baseline_bar(v, doc, row, &r) && crosses(area, r.x, r.y, r.w, r.h))
                fill(d, r.x, r.y, r.w, r.h, OPJ_COLOUR_BASELINE);
        }
        for (line = v->first_row; line < last; line++) {          /* resource names beside the bars */
            oldraw_rect r;
            char text[256];
            row = opj_view_line_task(v, doc, line);
            if (!opj_label_box(v, doc, row, &r) || !crosses(area, r.x, r.y, r.w, r.h)) continue;
            opj_label_text(v, doc, row, text, sizeof text);
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
        for (line = v->first_row; line < last; line++) {
            if (!crosses(area, 0, 2 * rh + (line - v->first_row) * rh, sw, rh)) continue;
            row = opj_view_line_task(v, doc, line);
            if (row >= 0) draw_sheet_row(d, v, doc, row);
            else draw_heading_row(d, v, doc, line);
        }
        draw_sheet_titles(d, v);
    }
    oldraw_clip(d, NULL);
}

/* ---- The Resource Usage view ---- */

#define USAGE_SHEET (OPJ_USAGE_ID_W + OPJ_USAGE_NAME_W + OPJ_USAGE_WORK_W)
#define OPJ_COLOUR_RESOURCE_ROW OLDRAW_RGB(0xF0F0F0)      /* a resource's own row, behind its work */

int opj_usage_rows(const opj_doc *doc)
{
    const owf_project *p;
    int i, n;
    if (!doc || !doc->plan) return 0;
    p = doc->plan;
    n = p->nresources;
    for (i = 0; i < p->nassigns; i++)
        if (p->assigns[i].resource >= 0 && p->assigns[i].resource < p->nresources) n++;
    return n;
}

int opj_usage_row(const opj_doc *doc, int row, int *resource, int *assign)
{
    const owf_project *p;
    int r, i;
    if (!doc || !doc->plan || row < 0) return 0;
    p = doc->plan;
    for (r = 0; r < p->nresources; r++) {
        if (row == 0) {
            if (resource) *resource = r;
            if (assign) *assign = -1;
            return 1;
        }
        row--;
        for (i = 0; i < p->nassigns; i++) {
            if (p->assigns[i].resource != r) continue;
            if (row == 0) {
                if (resource) *resource = r;
                if (assign) *assign = i;
                return 1;
            }
            row--;
        }
    }
    return 0;
}

int opj_task_usage_rows(const opj_doc *doc)
{
    const owf_project *p;
    int i, n;
    if (!doc || !doc->plan) return 0;
    p = doc->plan;
    n = p->ntasks;
    for (i = 0; i < p->nassigns; i++)
        if (p->assigns[i].task >= 0 && p->assigns[i].task < p->ntasks) n++;
    return n;
}

int opj_task_usage_row(const opj_doc *doc, int row, int *task, int *assign)
{
    const owf_project *p;
    int t, i;
    if (!doc || !doc->plan || row < 0) return 0;
    p = doc->plan;
    for (t = 0; t < p->ntasks; t++) {
        if (row == 0) {
            if (task) *task = t;
            if (assign) *assign = -1;
            return 1;
        }
        row--;
        for (i = 0; i < p->nassigns; i++) {
            if (p->assigns[i].task != t) continue;
            if (row == 0) {
                if (task) *task = t;
                if (assign) *assign = i;
                return 1;
            }
            row--;
        }
    }
    return 0;
}

const char *opj_task_usage_column_title(int column)
{
    static const char *const titles[OPJ_NUCOLS] = { "ID", "Task Name", "Work" };
    return column >= 0 && column < OPJ_NUCOLS ? titles[column] : "";
}

const char *opj_usage_column_title(int column)
{
    static const char *const titles[OPJ_NUCOLS] = { "ID", "Resource Name", "Work" };
    return column >= 0 && column < OPJ_NUCOLS ? titles[column] : "";
}

int opj_edit_for_key(int key, int shift)
{
    switch (key) {
    case 13: return OPJ_EDIT_CELL;
    case 127: return OPJ_EDIT_DELETE;
    case 9: return shift ? OPJ_EDIT_OUTDENT : OPJ_EDIT_INDENT;
    default: return OPJ_EDIT_NONE;
    }
}

const char *opj_edit_refused(int usage, int edit)
{
    if (!usage || edit < 0 || edit >= OPJ_NEDITS) return NULL;
    if (usage == OPJ_VIEW_TASK_USAGE) return "Task Usage is read only: Gantt Chart (G) edits tasks";
    if (usage == OPJ_VIEW_GRAPH)
        return "The Resource Graph is read only: Gantt Chart (G) edits tasks, Resources... edits resources";
    return "Resource Usage is read only: Gantt Chart (G) edits tasks, Resources... edits resources";
}

/* A cell of resource r's row (a -1) or of its assignment a's. */
static void usage_cell(const opj_doc *doc, int r, int a, int column, char *out, size_t size)
{
    static const int rcols[OPJ_NUCOLS] = { OPJ_RCOL_ID, OPJ_RCOL_NAME, OPJ_RCOL_WORK };
    char text[16];
    const owf_passign *as;
    const char *name;
    if (a < 0) {
        opj_resource_cell_text(doc, r, rcols[column], out, size);
        return;
    }
    as = &doc->plan->assigns[a];
    if (column == OPJ_UCOL_NAME) {
        name = as->task >= 0 && as->task < doc->plan->ntasks && doc->plan->tasks[as->task].name
                   ? doc->plan->tasks[as->task].name : "";
        snprintf(out, size, "%s", name);
    } else if (column == OPJ_UCOL_WORK && doc->plan->resources[r].type == OWF_RES_WORK) {
        opj_work_text(as->work, text);
        snprintf(out, size, "%s", text);
    }
}

/* A cell of task t's row (a -1) or of its assignment a's: its ID, name and work, or "", the resource's
 * name and the assignment's work. */
static void task_usage_cell(const opj_doc *doc, int t, int a, int column, char *out, size_t size)
{
    const owf_project *p = doc->plan;
    char text[16];
    if (a < 0) {
        if (column == OPJ_UCOL_ID) snprintf(out, size, "%d", t + 1);
        else if (column == OPJ_UCOL_NAME) snprintf(out, size, "%s", p->tasks[t].name ? p->tasks[t].name : "");
        else {
            opj_work_text(p->tasks[t].work, text);
            snprintf(out, size, "%s", text);
        }
        return;
    }
    if (column == OPJ_UCOL_NAME) {
        int r = p->assigns[a].resource;
        snprintf(out, size, "%s", r >= 0 && r < p->nresources && p->resources[r].name ? p->resources[r].name : "");
    } else if (column == OPJ_UCOL_WORK && p->assigns[a].resource >= 0 && p->assigns[a].resource < p->nresources &&
               p->resources[p->assigns[a].resource].type == OWF_RES_WORK) {
        opj_work_text(p->assigns[a].work, text);
        snprintf(out, size, "%s", text);
    }
}

void opj_task_usage_cell_text(const opj_doc *doc, int row, int column, char *out, size_t size)
{
    int t, a;
    if (!out || !size) return;
    out[0] = 0;
    if (column < 0 || column >= OPJ_NUCOLS || !opj_task_usage_row(doc, row, &t, &a)) return;
    task_usage_cell(doc, t, a, column, out, size);
}

void opj_usage_cell_text(const opj_doc *doc, int row, int column, char *out, size_t size)
{
    int r, a;
    if (!out || !size) return;
    out[0] = 0;
    if (column < 0 || column >= OPJ_NUCOLS || !opj_usage_row(doc, row, &r, &a)) return;
    usage_cell(doc, r, a, column, out, size);
}

void opj_usage_view(const opj_view *v, opj_view *usage)
{
    int c;
    *usage = *v;
    usage->lines = NULL;                         /* its rows are its own */
    for (c = 0; c < OPJ_NCOLS; c++) usage->columns[c] = 0;
    usage->sheet_width = USAGE_SHEET;
}

static void draw_usage_titles(oldraw *d, const opj_view *u, int tasks)
{
    static const int widths[OPJ_NUCOLS] = { OPJ_USAGE_ID_W, OPJ_USAGE_NAME_W, OPJ_USAGE_WORK_W };
    int c, x = 0;
    for (c = 0; c < OPJ_NUCOLS; c++) {
        oldraw_rect r;
        r.x = x;
        r.y = 0;
        r.w = widths[c];
        r.h = 2 * u->row_height;
        fill(d, r.x, r.y, r.w, r.h, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        oldraw_frame(d, &r, 0);
        r.x += 3;
        r.w -= 6;
        oldraw_text_box(d, &r, tasks ? opj_task_usage_column_title(c) : opj_usage_column_title(c), (size_t)-1, &u->font, OLDRAW_PEN(OLDRAW_PEN_TEXT),
                        OLDRAW_BOLD, OLDRAW_CENTRE);
        x += widths[c];
    }
}

/* owner: the resource (tasks 0) or the task (tasks 1) the row is, or whose assignment it is. */
static void draw_usage_row(oldraw *d, const opj_view *u, const opj_doc *doc, int row, int owner, int assign, int tasks)
{
    static const int widths[OPJ_NUCOLS] = { OPJ_USAGE_ID_W, OPJ_USAGE_NAME_W, OPJ_USAGE_WORK_W };
    int c, x = 0, y = opj_row_y(u, row), selected = row == u->selected;
    oldraw_colour ink = OLDRAW_PEN(selected ? OLDRAW_PEN_FILLTEXT : OLDRAW_PEN_TEXT);
    fill(d, 0, y, USAGE_SHEET, u->row_height, OLDRAW_PEN(selected ? OLDRAW_PEN_FILL : OLDRAW_PEN_BACKGROUND));
    for (c = 0; c < OPJ_NUCOLS; c++) {
        char text[256];
        oldraw_rect r;
        int indent = c == OPJ_UCOL_NAME ? (assign >= 0 ? u->row_height * 3 / 4 : 0) +
                     (tasks ? (doc->plan->tasks[owner].level - 1) * (u->row_height * 3 / 4) : 0) : 0;
        text[0] = 0;
        if (tasks) task_usage_cell(doc, owner, assign, c, text, sizeof text);
        else usage_cell(doc, owner, assign, c, text, sizeof text);
        r.x = x + 3 + indent;
        r.y = y;
        r.w = widths[c] - 6 - indent;
        r.h = u->row_height;
        if (r.w > 0)
            oldraw_text_box(d, &r, text, (size_t)-1, &u->font, ink,
                            assign < 0 && c == OPJ_UCOL_NAME && (!tasks || doc->plan->tasks[owner].summary) ? OLDRAW_BOLD : 0,
                            c == OPJ_UCOL_NAME ? OLDRAW_LEFT : OLDRAW_RIGHT);
        x += widths[c];
        fill(d, x - 1, y, 1, u->row_height, OPJ_COLOUR_GRID);
    }
    fill(d, 0, y + u->row_height - 1, USAGE_SHEET, 1, OPJ_COLOUR_GRID);
}

/* The work in each period across a row of the chart, right-aligned in its cell. */
static void draw_usage_work(oldraw *d, const opj_view *u, const opj_doc *doc, const oldraw_rect *area, int row,
                            int owner, int assign, int tasks)
{
    int cl = opj_chart_left(u), y = opj_row_y(u, row), right = area->x + area->w, guard;
    owf_ptime t = opj_period_start(u->zoom, opj_view_time(u, area->x > cl ? area->x : cl));
    if (assign < 0) fill(d, cl, y, u->width - cl, u->row_height, OPJ_COLOUR_RESOURCE_ROW);
    for (guard = 0; guard < 4000; guard++) {
        owf_ptime next = opj_period_next(u->zoom, t);
        long x0 = opj_view_x(u, t), x1 = opj_view_x(u, next);
        oldraw_rect cell;
        char text[24];
        int over = 0;
        if (x0 >= right || x0 >= u->width) break;
        fill(d, (int)x1 - 1, y, 1, u->row_height, OPJ_COLOUR_GRID);
        if (tasks) opj_task_usage_text(doc, owner, assign, t, next, text, sizeof text);
        else opj_usage_text(doc, owner, assign, t, next, text, sizeof text, &over);
        cell.x = (int)x0 + 2;
        cell.y = y;
        cell.w = (int)(x1 - x0) - 5;
        cell.h = u->row_height;
        if (text[0] && cell.w > 0)
            oldraw_text_box(d, &cell, text, (size_t)-1, &u->font, over ? OPJ_COLOUR_CRITICAL : OLDRAW_PEN(OLDRAW_PEN_TEXT),
                            assign < 0 ? OLDRAW_BOLD : 0, OLDRAW_RIGHT);
        t = next;
    }
    fill(d, cl, y + u->row_height - 1, u->width - cl, 1, OPJ_COLOUR_GRID);
}

static void usage_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area, int tasks)
{
    opj_view u;
    oldraw_rect all;
    int row, last, cl, rh, rows;
    if (!d || !v || !doc) return;
    opj_usage_view(v, &u);
    cl = opj_chart_left(&u);
    rh = u.row_height;
    if (!area) {
        all.x = all.y = 0;
        all.w = u.width;
        all.h = u.height;
        area = &all;
    }
    oldraw_clip(d, area);
    last = u.first_row + opj_rows_shown(&u);
    rows = tasks ? opj_task_usage_rows(doc) : opj_usage_rows(doc);
    if (last > rows) last = rows;
    if (crosses(area, cl, 0, u.width - cl, u.height)) {
        fill(d, cl, 2 * rh, u.width - cl, u.height - 2 * rh, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        for (row = u.first_row; row < last; row++) {
            int r, a;
            if (crosses(area, cl, opj_row_y(&u, row), u.width - cl, rh) &&
                (tasks ? opj_task_usage_row(doc, row, &r, &a) : opj_usage_row(doc, row, &r, &a)))
                draw_usage_work(d, &u, doc, area, row, r, a, tasks);
        }
        draw_timescale(d, &u, area);
    }
    if (crosses(area, USAGE_SHEET, 0, SPLIT, u.height)) {
        oldraw_rect r;
        r.x = USAGE_SHEET;
        r.y = 0;
        r.w = SPLIT;
        r.h = u.height;
        fill(d, r.x, r.y, r.w, r.h, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        oldraw_frame(d, &r, 0);
    }
    if (crosses(area, 0, 0, USAGE_SHEET, u.height)) {
        fill(d, 0, 2 * rh, USAGE_SHEET, u.height - 2 * rh, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        for (row = u.first_row; row < last; row++) {
            int r, a;
            if (crosses(area, 0, opj_row_y(&u, row), USAGE_SHEET, rh) &&
                (tasks ? opj_task_usage_row(doc, row, &r, &a) : opj_usage_row(doc, row, &r, &a)))
                draw_usage_row(d, &u, doc, row, r, a, tasks);
        }
        draw_usage_titles(d, &u, tasks);
    }
    oldraw_clip(d, NULL);
}

void opj_usage_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area)
{
    usage_draw(d, v, doc, area, 0);
}

void opj_task_usage_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area)
{
    usage_draw(d, v, doc, area, 1);
}

/* ---- The Resource Graph ---- */

static long max_units_of(const opj_doc *doc, int resource)
{
    return doc && resource >= 0 && resource < doc->plan->nresources ? doc->plan->resources[resource].max_units : 0;
}

/* The highest percent in the periods the view shows; 0 when none. */
static long graph_peak(const opj_view *u, const opj_doc *doc, int resource)
{
    owf_ptime t = opj_period_start(u->zoom, opj_view_time(u, opj_chart_left(u)));
    long peak = 0;
    int guard;
    for (guard = 0; guard < 4000 && opj_view_x(u, t) < u->width; guard++) {
        owf_ptime next = opj_period_next(u->zoom, t);
        long pc = opj_graph_percent(doc, resource, t, next);
        if (pc > peak) peak = pc;
        t = next;
    }
    return peak;
}

long opj_graph_scale(const opj_view *v, const opj_doc *doc, int resource)
{
    opj_view u;
    long top;
    if (!v || !doc) return 100;
    opj_usage_view(v, &u);
    top = graph_peak(&u, doc, resource);
    if (max_units_of(doc, resource) > top) top = max_units_of(doc, resource);
    if (top < 100) top = 100;
    if (top > 100000) top = 100000;
    return (top + 49) / 50 * 50;
}

/* The plot's top (scale percent) and bottom (0%) y. */
static void plot_span(const opj_view *u, int *top, int *bottom)
{
    *top = 3 * u->row_height;
    *bottom = u->height - u->row_height;
    if (*bottom < *top + 1) *bottom = *top + 1;
}

static int percent_y(const opj_view *u, long pc, long scale)
{
    int top, bottom;
    plot_span(u, &top, &bottom);
    if (pc > scale) pc = scale;
    return bottom - (int)((long)(bottom - top) * pc / (scale > 0 ? scale : 100));
}

int opj_graph_bar(const opj_view *v, const opj_doc *doc, int resource, owf_ptime period, long scale,
                  oldraw_rect *within, oldraw_rect *over)
{
    opj_view u;
    owf_ptime start, next;
    long pc, mu, x0, x1;
    int bottom, top, ymax;
    if (!v || !doc || !within || !over) return 0;
    opj_usage_view(v, &u);
    start = opj_period_start(u.zoom, period);
    next = opj_period_next(u.zoom, start);
    if ((pc = opj_graph_percent(doc, resource, start, next)) <= 0) return 0;
    mu = max_units_of(doc, resource);
    x0 = opj_view_x(&u, start);
    x1 = opj_view_x(&u, next);
    plot_span(&u, &top, &bottom);
    within->x = over->x = (int)x0 + (x1 - x0 > 4 ? 1 : 0);
    within->w = over->w = (int)(x1 - x0) - (x1 - x0 > 4 ? 2 : 0);
    ymax = percent_y(&u, pc < mu ? pc : mu, scale);
    within->y = ymax;
    within->h = bottom - ymax;
    over->y = percent_y(&u, pc, scale);
    over->h = pc > mu ? ymax - over->y : 0;
    if (over->h <= 0) over->h = 0;
    return 1;
}

void opj_graph_draw(oldraw *d, const opj_view *v, const opj_doc *doc, int resource, const oldraw_rect *area)
{
    opj_view u;
    oldraw_rect all, r;
    char text[300], pc[24];
    long scale, mu;
    int cl, rh, top, bottom, guard;
    if (!d || !v || !doc) return;
    opj_usage_view(v, &u);
    cl = opj_chart_left(&u);
    rh = u.row_height;
    if (!area) {
        all.x = all.y = 0;
        all.w = u.width;
        all.h = u.height;
        area = &all;
    }
    oldraw_clip(d, area);
    scale = opj_graph_scale(v, doc, resource);
    mu = max_units_of(doc, resource);
    plot_span(&u, &top, &bottom);
    if (crosses(area, cl, 0, u.width - cl, u.height)) {
        owf_ptime t = opj_period_start(u.zoom, opj_view_time(&u, area->x > cl ? area->x : cl));
        fill(d, cl, 2 * rh, u.width - cl, u.height - 2 * rh, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        for (guard = 0; guard < 4000; guard++) {
            oldraw_rect in, ov;
            owf_ptime next = opj_period_next(u.zoom, t);
            long x0 = opj_view_x(&u, t), x1 = opj_view_x(&u, next);
            if (x0 >= area->x + area->w || x0 >= u.width) break;
            fill(d, (int)x1 - 1, 2 * rh, 1, u.height - 2 * rh, OPJ_COLOUR_GRID);
            if (opj_graph_bar(v, doc, resource, t, scale, &in, &ov)) {
                if (in.h) oldraw_fill(d, &in, OPJ_COLOUR_BAR);
                if (ov.h) oldraw_fill(d, &ov, OPJ_COLOUR_CRITICAL);
                if (x1 - x0 >= 24) {                     /* its percent above it, where there is room */
                    r.x = (int)x0 + 1;
                    r.w = (int)(x1 - x0) - 2;
                    r.h = rh;
                    r.y = (ov.h ? ov.y : in.y) - rh;
                    if (r.y < 2 * rh) r.y = 2 * rh;
                    snprintf(pc, sizeof pc, "%ld%%", opj_graph_percent(doc, resource, t, next));
                    oldraw_text_box(d, &r, pc, (size_t)-1, &u.font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_CENTRE);
                }
            }
            t = next;
        }
        if (mu > 0 && mu <= scale) fill(d, cl, percent_y(&u, mu, scale), u.width - cl, 1, OPJ_COLOUR_SUMMARY);
        fill(d, cl, bottom, u.width - cl, 1, OPJ_COLOUR_SUMMARY);
        draw_timescale(d, &u, area);
    }
    if (crosses(area, USAGE_SHEET, 0, SPLIT, u.height)) {
        r.x = USAGE_SHEET;
        r.y = 0;
        r.w = SPLIT;
        r.h = u.height;
        fill(d, r.x, r.y, r.w, r.h, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        oldraw_frame(d, &r, 0);
    }
    if (crosses(area, 0, 0, USAGE_SHEET, u.height)) {
        int i;
        fill(d, 0, 0, USAGE_SHEET, u.height, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        for (i = 0; i < 5; i++) {
            text[0] = 0;
            if (resource < 0 || resource >= doc->plan->nresources) {
                if (i == 2) snprintf(text, sizeof text, "No resource: Resources... adds them");
            } else if (i == 2) {
                snprintf(text, sizeof text, "%s", doc->plan->resources[resource].name ? doc->plan->resources[resource].name : "");
            } else if (i == 3) {
                snprintf(text, sizeof text, "Peak: %ld%%", graph_peak(&u, doc, resource));
            } else if (i == 4) {
                snprintf(text, sizeof text, "Max Units: %ld%%", mu);
            }
            if (!text[0]) continue;
            r.x = 6;
            r.y = i * rh;
            r.w = USAGE_SHEET - 12;
            r.h = rh;
            oldraw_text_box(d, &r, text, (size_t)-1, &u.font, OLDRAW_PEN(OLDRAW_PEN_TEXT), i == 2 ? OLDRAW_BOLD : 0,
                            OLDRAW_LEFT);
        }
        r.x = 6;
        r.y = top - rh / 2;
        r.w = USAGE_SHEET - 12;
        r.h = rh;
        snprintf(text, sizeof text, "%ld%%", scale);
        oldraw_text_box(d, &r, text, (size_t)-1, &u.font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_RIGHT);
        r.y = bottom - rh / 2;
        oldraw_text_box(d, &r, "0%", (size_t)-1, &u.font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_RIGHT);
    }
    oldraw_clip(d, NULL);
}

/* ---- Hit tests ---- */

void opj_hit_test(const opj_view *v, const opj_doc *doc, int x, int y, opj_hit *hit)
{
    int sw = sheet_width(v), cl = opj_chart_left(v), rh = v->row_height, row;
    memset(hit, 0, sizeof *hit);
    hit->row = hit->column = hit->line = -1;
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
    hit->line = v->first_row + (y - 2 * rh) / rh;
    if (hit->line >= opj_view_lines(v, doc)) hit->line = -1;
    row = opj_view_line_task(v, doc, hit->line);
    hit->row = row;
    if (hit->line >= 0 && row < 0) {
        hit->what = OPJ_HIT_HEADING;
        return;
    }
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
    if (!pr || !proto || !doc || proto->row_height < 1) return 0;   /* a view not set up by opj_view_init */
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
    ntasks = opj_view_lines(proto, doc);                       /* the lines: the view's filter and groups */
    pr->down = ntasks > 0 ? (ntasks + pr->rows_per_page - 1) / pr->rows_per_page : 1;
    end = doc->plan->start;
    for (i = 0; i < doc->plan->ntasks; i++)
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

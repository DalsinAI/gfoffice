/*
 * opj_drag.c: dragging on GFOpenProject's Gantt chart, as MS Project's mouse does it. A bar's middle moves
 * the task (it gets Start No Earlier Than at the day it is dropped on), its right end stretches it (its
 * duration becomes the working time to the end of the day dropped on), its done part or its left end sets
 * % Complete, and a bar dragged up or down onto another task's bar links the two, finish to start. Each
 * drop is one edit and one Undo step. Moves and stretches go by whole days at every zoom. No Amiga
 * headers: the window passes the pointer's place, and this works out the edit.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "openproject_core.h"

#define DAY (24L * 60)
#define EDGE 3                                   /* pixels at a bar's left end that set % Complete */

static long floor_div(long a, long b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static const owf_pcal *calendar_of(const owf_project *p, const owf_ptask *t)
{
    int c = t->calendar >= 0 && t->calendar < p->ncalendars ? t->calendar : p->calendar;
    return &p->calendars[c];
}

int opj_drag_begin(const opj_view *v, const opj_doc *doc, const opj_hit *hit, int x, opj_drag *d)
{
    opj_bar b;
    if (!v || !doc || !hit || !d) return 0;
    memset(d, 0, sizeof *d);
    d->row = -1;
    if (hit->what != OPJ_HIT_BAR || hit->row < 0 || hit->row >= doc->plan->ntasks) return 0;
    if (!opj_bar_of(v, doc, hit->row, &b)) return 0;
    d->row = hit->row;
    d->x0 = x;
    if (b.kind == OPJ_BAR_TASK && (hit->part == 2 || x < b.bar.x + EDGE)) d->kind = OPJ_DRAG_PROGRESS;
    else if (b.kind == OPJ_BAR_TASK && hit->part == 1) d->kind = OPJ_DRAG_STRETCH;
    else d->kind = OPJ_DRAG_MOVE;            /* a summary or milestone moves only by linking (below) */
    return 1;
}

/* The whole days from the press to x (rounded to the nearest). */
static long days_moved(const opj_view *v, const opj_drag *d, int x)
{
    long dt = (long)(opj_view_time(v, x) - opj_view_time(v, d->x0));
    return floor_div(dt + DAY / 2, DAY);
}

/* The row whose bar is under x, y, when it is another task's (a link's target); -1 none. */
static int link_target(const opj_view *v, const opj_doc *doc, const opj_drag *d, int x, int y)
{
    opj_hit h;
    opj_hit_test(v, doc, x, y, &h);
    return h.what == OPJ_HIT_BAR && h.row >= 0 && h.row != d->row ? h.row : -1;
}

/* What the drop would do: OPJ_DRAG_ kind (OPJ_DRAG_LINK when it links), with the new start, the minutes of
 * the new duration, the new percent or the link's successor in *value; -1 when it would do nothing. */
static int plan_drop(const opj_view *v, const opj_doc *doc, const opj_drag *d, int x, int y, long *value)
{
    const owf_project *p = doc->plan;
    const owf_ptask *t;
    long days;
    int target;
    if (d->row < 0 || d->row >= p->ntasks) return -1;
    t = &p->tasks[d->row];
    if ((target = link_target(v, doc, d, x, y)) >= 0) {
        *value = target;
        return OPJ_DRAG_LINK;
    }
    if (opj_row_y(v, d->row) > y || y >= opj_row_y(v, d->row) + v->row_height) return -1;   /* off its row */
    switch (d->kind) {
    case OPJ_DRAG_MOVE:
        if (t->summary || t->milestone) return -1;
        if (!(days = days_moved(v, d, x))) return -1;
        *value = owf_pcal_next_start(calendar_of(p, t), floor_div(t->early_start, DAY) * DAY + days * DAY);
        return OPJ_DRAG_MOVE;
    case OPJ_DRAG_STRETCH: {
        owf_ptime end;
        long minutes;
        const owf_pday *h;
        long day;
        if (!(days = days_moved(v, d, x))) return -1;
        day = floor_div(t->early_finish - 1, DAY) + days;           /* the day the finish is dropped on */
        h = owf_pcal_day(calendar_of(p, t), day);
        end = day * DAY + (h && h->nspans ? h->to[h->nspans - 1] : 24 * 60);
        if (end <= t->early_start) return -1;
        minutes = t->elapsed ? (long)(end - t->early_start) : owf_pcal_work(calendar_of(p, t), t->early_start, end);
        if (minutes <= 0 || minutes == t->duration) return -1;
        *value = minutes;
        return OPJ_DRAG_STRETCH;
    }
    case OPJ_DRAG_PROGRESS: {
        opj_bar b;
        long pc;
        if (!opj_bar_of(v, doc, d->row, &b) || b.bar.w <= 0) return -1;
        pc = ((long)(x - b.bar.x) * 100 + b.bar.w / 2) / b.bar.w;
        if (pc < 0) pc = 0;
        if (pc > 100) pc = 100;
        if (pc == t->percent) return -1;
        *value = pc;
        return OPJ_DRAG_PROGRESS;
    }
    default:
        return -1;
    }
}

int opj_drag_text(const opj_view *v, const opj_doc *doc, const opj_drag *d, int x, int y, char *out, size_t size)
{
    long value = 0;
    char text[40];
    int kind;
    if (!out || !size) return 0;
    out[0] = 0;
    if (!v || !doc || !d) return 0;
    switch ((kind = plan_drop(v, doc, d, x, y, &value))) {
    case OPJ_DRAG_MOVE:
        opj_date_text((owf_ptime)value, 0, text);
        snprintf(out, size, "Start: %s", text);
        break;
    case OPJ_DRAG_STRETCH:
        owf_pduration_text(doc->plan, value, doc->plan->tasks[d->row].elapsed, 0, text);
        snprintf(out, size, "Duration: %s", text);
        break;
    case OPJ_DRAG_PROGRESS: snprintf(out, size, "%% Complete: %ld%%", value); break;
    case OPJ_DRAG_LINK: snprintf(out, size, "Link %d to %ld, finish to start", d->row + 1, value + 1); break;
    default: return 0;
    }
    return 1;
}

int opj_drag_end(opj_doc *doc, const opj_view *v, const opj_drag *d, int x, int y)
{
    long value = 0;
    char text[40];
    if (!v || !doc || !d) return OWF_ERR_FORMAT;
    switch (plan_drop(v, doc, d, x, y, &value)) {
    case OPJ_DRAG_MOVE: {
        static const int cols[2] = { OPJ_COL_CONSTRAINT_TYPE, OPJ_COL_CONSTRAINT_DATE };
        char date[40];
        const char *texts[2];
        int y0, mo, da, h, mi;
        owf_ptime_split((owf_ptime)value, &y0, &mo, &da, &h, &mi);
        snprintf(date, sizeof date, "%02d/%02d/%04d %02d:%02d", da, mo, y0, h, mi);
        texts[0] = "Start No Earlier Than";
        texts[1] = date;
        return opj_set_cells(doc, d->row, cols, texts, 2, NULL);
    }
    case OPJ_DRAG_STRETCH: return opj_set_duration(doc, d->row, value);
    case OPJ_DRAG_PROGRESS:
        snprintf(text, sizeof text, "%ld", value);
        return opj_set_cell(doc, d->row, OPJ_COL_PERCENT, text);
    case OPJ_DRAG_LINK: return opj_link_tasks(doc, d->row, (int)value);
    default: return OWF_ERR_FORMAT;
    }
}

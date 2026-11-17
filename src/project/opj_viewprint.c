/*
 * GFOpenProject: the views other than the Gantt chart laid out on pages
 * (printing, PDF and reports, step 3 part 3): Resource Usage and Task Usage
 * go down by rows and across by whole periods of the timescale, with their
 * table on every page; the Network Diagram goes down by whole lines of boxes
 * and across by whole columns, so no box is cut by a page's edge. Every page
 * has a footer row: the plan's title, the view's name, "Page n of m".
 * opj_pdf.c writes the pages as a PDF. Integer arithmetic only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_core.h"

#include <stdio.h>
#include <string.h>

#define DAY 1440L
#define MAX_ACROSS 1000

const char *opj_viewprint_name(int kind)
{
    static const char *const names[OPJ_NPRINTVIEWS] = { "Gantt Chart", "Resource Usage", "Task Usage", "Network Diagram" };
    return kind >= 0 && kind < OPJ_NPRINTVIEWS ? names[kind] : NULL;
}

int opj_viewprint_kind(const char *word)
{
    static const char *const words[OPJ_NPRINTVIEWS] = { "gantt", "usage", "taskusage", "network" };
    char w[16];
    size_t n = 0;
    int k;
    for (; word && *word && n + 1 < sizeof w; word++)
        if (*word != ' ' && *word != '_' && *word != '-') w[n++] = (char)(*word >= 'A' && *word <= 'Z' ? *word + 32 : *word);
    w[n] = 0;
    if (word && *word) return -1;
    if (!strcmp(w, "resourceusage")) return OPJ_PRINT_USAGE;
    if (!strcmp(w, "tracking")) return OPJ_PRINT_GANTT;        /* the Tracking Gantt is the Gantt view's */
    for (k = 0; k < OPJ_NPRINTVIEWS; k++)
        if (!strcmp(w, words[k])) return k;
    return -1;
}

/* The origin of the page across after the one whose chart starts at origin: the start of the period its
 * right edge is in, so a period cut by the edge is whole on the next page; always later than origin. */
static owf_ptime next_origin(const opj_viewprint *vp, owf_ptime origin)
{
    opj_view v = vp->proto, u;
    owf_ptime t;
    v.width = vp->page_width;
    v.origin = origin;
    opj_usage_view(&v, &u);
    t = opj_period_start(u.zoom, opj_view_time(&u, u.width));
    if (t <= origin) t = opj_period_next(u.zoom, opj_period_start(u.zoom, origin));
    if (t <= origin) t = origin + DAY;
    return t;
}

/* A box's size (they are all one size); 0 when the diagram has no box. */
static int first_box(const opj_viewprint *vp, const opj_network *net, oldraw_rect *b)
{
    int i;
    for (i = 0; net && i < net->ntasks; i++)
        if (opj_network_box(&vp->proto, net, i, b)) return 1;
    return 0;
}

int opj_viewprint_layout(opj_viewprint *vp, int kind, const opj_view *proto, const opj_doc *doc, const opj_network *net,
                         int page_width, int page_height)
{
    int rh;
    if (!vp || !proto || !doc || !doc->plan || proto->row_height < 1) return 0;
    if (kind != OPJ_PRINT_USAGE && kind != OPJ_PRINT_TASK_USAGE && kind != OPJ_PRINT_NETWORK) return 0;
    memset(vp, 0, sizeof *vp);
    vp->kind = kind;
    vp->proto = *proto;
    vp->proto.selected = -1;
    vp->proto.first_row = 0;
    vp->proto.scroll_x = vp->proto.scroll_y = 0;
    vp->proto.lines = NULL;
    vp->page_width = page_width;
    vp->page_height = page_height;
    rh = proto->row_height;
    if (kind == OPJ_PRINT_NETWORK) {
        oldraw_rect b;
        int cols, lines;
        if (!net || !first_box(vp, net, &b)) return 0;
        vp->step_x = b.w + OPJ_NET_GAP_X;
        vp->step_y = b.h + OPJ_NET_GAP_Y;
        cols = (page_width - 2 * OPJ_NET_MARGIN + OPJ_NET_GAP_X) / vp->step_x;
        lines = (page_height - rh - 2 * OPJ_NET_MARGIN + OPJ_NET_GAP_Y) / vp->step_y;
        if (cols < 1 || lines < 1) return 0;
        vp->step_x *= cols;
        vp->step_y *= lines;
        vp->rows_per_page = lines;
        vp->across = (net->columns + cols - 1) / cols;
        vp->down = (net->lines + lines - 1) / lines;
        if (vp->across < 1) vp->across = 1;
        if (vp->down < 1) vp->down = 1;
    } else {
        opj_view u;
        owf_ptime end = doc->plan->start, o;
        int rows = kind == OPJ_PRINT_TASK_USAGE ? opj_task_usage_rows(doc) : opj_usage_rows(doc), i;
        vp->rows_per_page = (page_height - 3 * rh) / rh;       /* the timescale's two rows and the footer */
        vp->proto.width = page_width;
        opj_usage_view(&vp->proto, &u);
        if (vp->rows_per_page < 1 || page_width - opj_chart_left(&u) < opj_zoom_day_width(u.zoom)) return 0;
        vp->down = rows > 0 ? (rows + vp->rows_per_page - 1) / vp->rows_per_page : 1;
        for (i = 0; i < doc->plan->ntasks; i++)
            if (doc->plan->tasks[i].early_finish > end) end = doc->plan->tasks[i].early_finish;
        vp->end = end;
        vp->across = 1;
        for (o = next_origin(vp, vp->proto.origin); o < end && vp->across < MAX_ACROSS; o = next_origin(vp, o)) vp->across++;
    }
    if ((long)vp->across * vp->down > 100000L) vp->across = (int)(100000L / vp->down);
    return vp->across * vp->down;
}

int opj_viewprint_page_view(const opj_viewprint *vp, int page, opj_view *v)
{
    int down, across, i;
    if (!vp || !v || page < 0 || page >= vp->across * vp->down) return 0;
    across = page / vp->down;
    down = page % vp->down;
    *v = vp->proto;
    v->width = vp->page_width;
    if (vp->kind == OPJ_PRINT_NETWORK) {
        v->height = vp->page_height - v->row_height;
        v->scroll_x = across * vp->step_x;
        v->scroll_y = down * vp->step_y;
    } else {
        v->height = 2 * v->row_height + vp->rows_per_page * v->row_height;
        v->first_row = down * vp->rows_per_page;
        for (i = 0; i < across; i++) v->origin = next_origin(vp, v->origin);
    }
    return 1;
}

void opj_viewprint_draw(oldraw *d, const opj_viewprint *vp, const opj_doc *doc, const opj_network *net, int page)
{
    opj_view v;
    oldraw_rect r;
    char text[64];
    if (!d || !doc || !opj_viewprint_page_view(vp, page, &v)) return;
    r.x = r.y = 0;
    r.w = vp->page_width;
    r.h = vp->page_height;
    oldraw_clip(d, &r);
    oldraw_fill(d, &r, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
    oldraw_clip(d, NULL);
    if (vp->kind == OPJ_PRINT_NETWORK) {
        if (!net) return;
        /* Only this page's whole columns and lines: the next page's first boxes would show cut at the edges.
         * The cut is half way through the gap, where only links run. */
        if (OPJ_NET_MARGIN + vp->step_x - OPJ_NET_GAP_X / 2 < r.w) r.w = OPJ_NET_MARGIN + vp->step_x - OPJ_NET_GAP_X / 2;
        r.h = v.height;                          /* the footer's row stays clear of boxes and lines */
        if (OPJ_NET_MARGIN + vp->step_y - OPJ_NET_GAP_Y / 2 < r.h) r.h = OPJ_NET_MARGIN + vp->step_y - OPJ_NET_GAP_Y / 2;
        opj_network_draw(d, &v, net, doc, &r);
    } else if (vp->kind == OPJ_PRINT_TASK_USAGE) {
        opj_task_usage_draw(d, &v, doc, NULL);
    } else {
        opj_usage_draw(d, &v, doc, NULL);
    }
    oldraw_clip(d, NULL);
    r.y = vp->page_height - v.row_height;
    r.h = v.row_height;
    r.x = 4;
    r.w = vp->page_width / 3 - 8;
    oldraw_text_box(d, &r, doc->plan->title ? doc->plan->title : "", (size_t)-1, &v.font,
                    OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT);
    r.x = vp->page_width / 3;
    oldraw_text_box(d, &r, opj_viewprint_name(vp->kind), (size_t)-1, &v.font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0,
                    OLDRAW_CENTRE);
    sprintf(text, "Page %d of %d", page + 1, vp->across * vp->down);
    r.x = vp->page_width - r.w - 4;
    oldraw_text_box(d, &r, text, (size_t)-1, &v.font, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_RIGHT);
}

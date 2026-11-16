/*
 * opj_network.c: GFOpenProject's Network Diagram view, as MS Project draws it. Every task that isn't a
 * summary is a box (its name; ID and duration; start; finish), the boxes stand in columns by how many links
 * lead to them, and each link is a line with an arrow from its predecessor's box to its successor's. A
 * critical task's box is red; a task under way is struck through once, a finished one crossed. No Amiga
 * headers: it draws through oldraw.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openproject_core.h"

/* ---- Laying the boxes out ---- */

int opj_network_layout(const opj_doc *doc, opj_network *net)
{
    const owf_project *p;
    int n, i, k, pass, changed, *count;
    if (!net) return OWF_ERR_FORMAT;
    memset(net, 0, sizeof *net);
    if (!doc || !doc->plan) return OWF_ERR_FORMAT;
    p = doc->plan;
    n = p->ntasks;
    net->ntasks = n;
    if (!n) return OWF_OK;
    net->column = malloc((size_t)n * sizeof *net->column);
    net->line = malloc((size_t)n * sizeof *net->line);
    count = calloc((size_t)n + 1, sizeof *count);
    if (!net->column || !net->line || !count) {
        free(count);
        opj_network_free(net);
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < n; i++) net->column[i] = p->tasks[i].summary ? -1 : 0;
    /* A box's column is one past its furthest predecessor's. n passes at most, so a loop of links (which
     * the scheduler reports) can't keep it going. */
    for (pass = 0, changed = 1; changed && pass < n; pass++) {
        changed = 0;
        for (i = 0; i < n; i++) {
            const owf_ptask *t = &p->tasks[i];
            if (net->column[i] < 0) continue;
            for (k = 0; k < t->nlinks; k++) {
                int pr = t->links[k].pred;
                if (pr < 0 || pr >= n || pr == i || net->column[pr] < 0) continue;
                if (net->column[pr] + 1 > net->column[i] && net->column[pr] + 1 < n) {
                    net->column[i] = net->column[pr] + 1;
                    changed = 1;
                }
            }
        }
    }
    /* In each column, the boxes go down in the sheet's order. */
    for (i = 0; i < n; i++) {
        int c = net->column[i];
        if (c < 0) { net->line[i] = -1; continue; }
        net->line[i] = count[c]++;
        if (c + 1 > net->columns) net->columns = c + 1;
        if (count[c] > net->lines) net->lines = count[c];
    }
    free(count);
    return OWF_OK;
}

void opj_network_free(opj_network *net)
{
    if (!net) return;
    free(net->column);
    free(net->line);
    memset(net, 0, sizeof *net);
}

static int box_h(const opj_view *v) { return 4 * v->row_height + 4; }

int opj_network_box(const opj_view *v, const opj_network *net, int row, oldraw_rect *box)
{
    if (!v || !net || !box || row < 0 || row >= net->ntasks || !net->column || net->column[row] < 0) return 0;
    box->x = OPJ_NET_MARGIN + net->column[row] * (OPJ_NET_BOX_W + OPJ_NET_GAP_X) - v->scroll_x;
    box->y = OPJ_NET_MARGIN + net->line[row] * (box_h(v) + OPJ_NET_GAP_Y) - v->scroll_y;
    box->w = OPJ_NET_BOX_W;
    box->h = box_h(v);
    return 1;
}

void opj_network_size(const opj_view *v, const opj_network *net, int *width, int *height)
{
    int w = 0, h = 0;
    if (v && net && net->columns) {
        w = 2 * OPJ_NET_MARGIN + net->columns * (OPJ_NET_BOX_W + OPJ_NET_GAP_X) - OPJ_NET_GAP_X;
        h = 2 * OPJ_NET_MARGIN + net->lines * (box_h(v) + OPJ_NET_GAP_Y) - OPJ_NET_GAP_Y;
    }
    if (width) *width = w;
    if (height) *height = h;
}

int opj_network_hit(const opj_view *v, const opj_network *net, int x, int y)
{
    oldraw_rect b;
    int i;
    if (!v || !net || x < 0 || y < 0 || x >= v->width || y >= v->height) return -1;
    for (i = 0; i < net->ntasks; i++)
        if (opj_network_box(v, net, i, &b) && x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
    return -1;
}

/* Keeps a scroll in [0, the diagram's size less the view's]. */
static void clamp_scroll(opj_view *v, const opj_network *net)
{
    int w, h;
    opj_network_size(v, net, &w, &h);
    if (v->scroll_x > w - v->width) v->scroll_x = w - v->width;
    if (v->scroll_y > h - v->height) v->scroll_y = h - v->height;
    if (v->scroll_x < 0) v->scroll_x = 0;
    if (v->scroll_y < 0) v->scroll_y = 0;
}

void opj_network_scroll(opj_view *v, const opj_network *net, int dx, int dy)
{
    if (!v || !net) return;
    v->scroll_x += dx;
    v->scroll_y += dy;
    clamp_scroll(v, net);
}

void opj_network_reveal(opj_view *v, const opj_network *net, int row)
{
    oldraw_rect b;
    if (!v || !net || !opj_network_box(v, net, row, &b)) return;
    if (b.x + b.w + OPJ_NET_MARGIN > v->width) v->scroll_x += b.x + b.w + OPJ_NET_MARGIN - v->width;
    if (b.y + b.h + OPJ_NET_MARGIN > v->height) v->scroll_y += b.y + b.h + OPJ_NET_MARGIN - v->height;
    opj_network_box(v, net, row, &b);
    if (b.x - OPJ_NET_MARGIN < 0) v->scroll_x += b.x - OPJ_NET_MARGIN;
    if (b.y - OPJ_NET_MARGIN < 0) v->scroll_y += b.y - OPJ_NET_MARGIN;
    clamp_scroll(v, net);
}

int opj_network_link_path(const opj_view *v, const opj_network *net, const opj_doc *doc, int row, int link,
                          opj_path *path)
{
    oldraw_rect a, b;
    int pred, x1, y1, x2, y2, xm;
    if (!v || !net || !doc || !path || row < 0 || row >= doc->plan->ntasks || row >= net->ntasks) return 0;
    if (link < 0 || link >= doc->plan->tasks[row].nlinks) return 0;
    pred = doc->plan->tasks[row].links[link].pred;
    if (pred < 0 || pred >= net->ntasks || net->column[pred] < 0 || net->column[row] <= net->column[pred]) return 0;
    if (!opj_network_box(v, net, pred, &a) || !opj_network_box(v, net, row, &b)) return 0;
    x1 = a.x + a.w;
    y1 = a.y + a.h / 2;
    x2 = b.x - 1;
    y2 = b.y + b.h / 2;
    if (y1 == y2) {
        path->n = 2;
        path->x[0] = x1, path->y[0] = y1;
        path->x[1] = x2, path->y[1] = y2;
        return 1;
    }
    xm = x2 - OPJ_NET_GAP_X / 2;                 /* down or up in the gap before the successor's column */
    path->n = 4;
    path->x[0] = x1, path->y[0] = y1;
    path->x[1] = xm, path->y[1] = y1;
    path->x[2] = xm, path->y[2] = y2;
    path->x[3] = x2, path->y[3] = y2;
    return 1;
}

void opj_network_line_text(const opj_doc *doc, int row, int line, char *out, size_t size)
{
    char a[40], b[40];
    const owf_ptask *t;
    if (!out || !size) return;
    out[0] = 0;
    if (!doc || row < 0 || row >= doc->plan->ntasks) return;
    t = &doc->plan->tasks[row];
    switch (line) {
    case 0: snprintf(out, size, "%s", t->name ? t->name : ""); break;
    case 1:
        owf_pduration_text(doc->plan, t->duration, t->elapsed, t->estimated, b);
        snprintf(out, size, "ID: %d  Dur: %s", row + 1, b);
        break;
    case 2:
        opj_date_text(t->early_start, 0, a);
        snprintf(out, size, "Start: %s", a);
        break;
    case 3:
        opj_date_text(t->early_finish, 0, a);
        snprintf(out, size, "Finish: %s", a);
        break;
    default: break;
    }
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
    int i, k;
    for (i = 1; i < p->n; i++) line(d, p->x[i - 1], p->y[i - 1], p->x[i], p->y[i], c);
    for (k = 0; k < 4; k++) fill(d, p->x[p->n - 1] - k, p->y[p->n - 1] - k, 1, 2 * k + 1, c);   /* the arrow */
}

/* A diagonal from (x, y) across w by h, down to the right (down < 0: up to the right). */
static void diagonal(oldraw *d, int x, int y, int w, int h, int down, oldraw_colour c)
{
    int i;
    for (i = 0; i < w; i++) {
        int yy = (int)((long)i * (h - 1) / (w > 1 ? w - 1 : 1));
        fill(d, x + i, down > 0 ? y + yy : y + h - 1 - yy, 1, 1, c);
    }
}

static void draw_box(oldraw *d, const opj_view *v, const opj_doc *doc, int row, const oldraw_rect *b)
{
    const owf_ptask *t = &doc->plan->tasks[row];
    oldraw_colour edge = t->critical ? OPJ_COLOUR_CRITICAL : OPJ_COLOUR_BAR;
    oldraw_colour ink = OLDRAW_PEN(row == v->selected ? OLDRAW_PEN_FILLTEXT : OLDRAW_PEN_TEXT);
    char text[300];
    oldraw_rect r;
    int i, k;
    fill(d, b->x, b->y, b->w, b->h, OLDRAW_PEN(row == v->selected ? OLDRAW_PEN_FILL : OLDRAW_PEN_BACKGROUND));
    for (k = 0; k < (t->milestone ? 1 : 2); k++) {   /* a milestone's edge is thin, a task's two pixels */
        line(d, b->x + k, b->y + k, b->x + b->w - 1 - k, b->y + k, edge);
        line(d, b->x + k, b->y + b->h - 1 - k, b->x + b->w - 1 - k, b->y + b->h - 1 - k, edge);
        line(d, b->x + k, b->y + k, b->x + k, b->y + b->h - 1 - k, edge);
        line(d, b->x + b->w - 1 - k, b->y + k, b->x + b->w - 1 - k, b->y + b->h - 1 - k, edge);
    }
    if (t->milestone) {                          /* and doubled inside */
        line(d, b->x + 3, b->y + 3, b->x + b->w - 4, b->y + 3, edge);
        line(d, b->x + 3, b->y + b->h - 4, b->x + b->w - 4, b->y + b->h - 4, edge);
        line(d, b->x + 3, b->y + 3, b->x + 3, b->y + b->h - 4, edge);
        line(d, b->x + b->w - 4, b->y + 3, b->x + b->w - 4, b->y + b->h - 4, edge);
    }
    if (t->percent > 0) diagonal(d, b->x + 2, b->y + 2, b->w - 4, b->h - 4, -1, OPJ_COLOUR_GRID);
    if (t->percent >= 100) diagonal(d, b->x + 2, b->y + 2, b->w - 4, b->h - 4, 1, OPJ_COLOUR_GRID);
    for (i = 0; i < 4; i++) {
        opj_network_line_text(doc, row, i, text, sizeof text);
        r.x = b->x + 6;
        r.y = b->y + 2 + i * v->row_height;
        r.w = b->w - 12;
        r.h = v->row_height;
        oldraw_text_box(d, &r, text, (size_t)-1, &v->font, ink, i == 0 ? OLDRAW_BOLD : 0,
                        i == 0 ? OLDRAW_CENTRE : OLDRAW_LEFT);
    }
}

static int crosses(const oldraw_rect *a, int x, int y, int w, int h)
{
    return x < a->x + a->w && x + w > a->x && y < a->y + a->h && y + h > a->y;
}

void opj_network_draw(oldraw *d, const opj_view *v, const opj_network *net, const opj_doc *doc,
                      const oldraw_rect *area)
{
    oldraw_rect all, b;
    opj_path path;
    int i, k, pass;
    if (!d || !v || !net || !doc) return;
    if (!area) {
        all.x = all.y = 0;
        all.w = v->width;
        all.h = v->height;
        area = &all;
    }
    oldraw_clip(d, area);
    fill(d, area->x, area->y, area->w, area->h, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
    for (pass = 0; pass < 2; pass++)            /* the critical links last, so they show where lines share */
        for (i = 0; i < doc->plan->ntasks && i < net->ntasks; i++)
            for (k = 0; k < doc->plan->tasks[i].nlinks; k++) {
                const owf_ptask *t = &doc->plan->tasks[i];
                int critical = t->critical && t->links[k].pred >= 0 && t->links[k].pred < doc->plan->ntasks &&
                               doc->plan->tasks[t->links[k].pred].critical;
                if (critical == pass && opj_network_link_path(v, net, doc, i, k, &path))
                    draw_path(d, &path, critical ? OPJ_COLOUR_CRITICAL : OPJ_COLOUR_LINK);
            }
    for (i = 0; i < doc->plan->ntasks && i < net->ntasks; i++)
        if (opj_network_box(v, net, i, &b) && crosses(area, b.x, b.y, b.w, b.h)) draw_box(d, v, doc, i, &b);
    oldraw_clip(d, NULL);
}

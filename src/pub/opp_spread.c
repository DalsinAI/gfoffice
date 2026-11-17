/*
 * GFOpenPressmaster: facing pages, two to a spread (include/openpressmaster_spread.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpressmaster_spread.h"

#include <string.h>

int opp_spread_facing(const owf_deck *deck)
{
    int i, k;
    if (!deck || !deck->masters) return 0;
    for (i = 0; i < deck->nmasters; i++)
        for (k = i + 1; k < deck->nmasters; k++)
            if (deck->masters[i].name && deck->masters[k].name && deck->masters[i].name[0] &&
                strcmp(deck->masters[i].name, deck->masters[k].name) == 0)
                return 1;
    return 0;
}

int opp_spread_side(int page)
{
    return page > 0 && page % 2 ? OPP_SPREAD_LEFT : OPP_SPREAD_RIGHT;
}

int opp_spread_partner(int npages, int page)
{
    int other;
    if (page <= 0 || page >= npages) return -1;
    other = page % 2 ? page + 1 : page - 1;
    return other < npages ? other : -1;
}

int opp_spread_first(int npages, int page)
{
    if (page < 0 || page >= npages) return -1;
    return page > 0 && page % 2 == 0 ? page - 1 : page;
}

int opp_spread_place(int view_width, int view_height, int page_width, int page_height, opp_spread_box *left,
                     opp_spread_box *right)
{
    opp_spread_box l, r;
    long w, h;
    memset(&l, 0, sizeof l);
    memset(&r, 0, sizeof r);
    if (left) *left = l;
    if (right) *right = r;
    if (view_width < 1 || view_height < 1 || page_width < 1 || page_height < 1) return 0;
    w = view_width / 2;
    h = w * page_height / page_width;
    if (h > view_height) {
        h = view_height;
        w = h * page_width / page_height;
    }
    if (w < 8 || h < 8) return 0;
    l.w = r.w = (int)w;
    l.h = r.h = (int)h;
    l.x = (int)((view_width - 2 * w) / 2);
    r.x = l.x + (int)w;
    l.y = r.y = (int)((view_height - h) / 2);
    if (left) *left = l;
    if (right) *right = r;
    return (int)w;
}

static int inside(const opp_spread_box *b, int x, int y)
{
    return b && b->w > 0 && b->h > 0 && x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

int opp_spread_hit(const opp_spread_box *left, const opp_spread_box *right, int x, int y)
{
    if (inside(left, x, y)) return OPP_SPREAD_LEFT;
    if (inside(right, x, y)) return OPP_SPREAD_RIGHT;
    return -1;
}

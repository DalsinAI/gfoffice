/*
 * libowf: the print core (owf_print.h; docs/suite/owf-shared-core.md). Was src/deck/opr_pdf.c's own
 * bleed, crop marks and booklet order; that file now calls these through its opr_ names.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_print.h"

#include <stddef.h>

int owf_print_bleed(int bleed)
{
    if (bleed <= 0) return 0;
    return bleed > OWF_PRINT_MAX_BLEED ? OWF_PRINT_MAX_BLEED : bleed;
}

int owf_print_mark_gap(int bleed)
{
    int b = owf_print_bleed(bleed);
    return b > OWF_PRINT_MARK_GAP ? b : OWF_PRINT_MARK_GAP;
}

int owf_print_margin(int bleed, int crop_marks)
{
    if (crop_marks) return owf_print_mark_gap(bleed) + OWF_PRINT_MARK_LENGTH + OWF_PRINT_MARK_ROOM;
    return owf_print_bleed(bleed);
}

int owf_print_crop_lines(int m, int w, int h, int gap, int *xy)
{
    int corner, n = 0;
    if (!xy) return -1;
    for (corner = 0; corner < 4; corner++) {
        int x = m + (corner & 1 ? w : 0), y = m + (corner & 2 ? h : 0);
        int sx = corner & 1 ? 1 : -1, sy = corner & 2 ? 1 : -1;
        xy[n++] = x + sx * gap;                                   /* across, level with the edge */
        xy[n++] = y;
        xy[n++] = x + sx * (gap + OWF_PRINT_MARK_LENGTH);
        xy[n++] = y;
        xy[n++] = x;                                              /* down, in line with the edge */
        xy[n++] = y + sy * gap;
        xy[n++] = x;
        xy[n++] = y + sy * (gap + OWF_PRINT_MARK_LENGTH);
    }
    return OWF_PRINT_CROP_LINES;
}

unsigned long owf_print_bleed_colour(const owf_deck *deck, int page)
{
    const owf_master *m;
    unsigned long bg;
    if (!deck || page < 0 || page >= deck->nslides) return 0;
    m = owf_deck_slide_master(deck, page);
    bg = deck->slides[page].background;
    if (!OWF_SHADE_SET(bg) && deck->slides[page].background_image < 0 && m) bg = m->background;
    if (!OWF_SHADE_SET(bg) || OWF_SHADE_RGB(bg) == 0xFFFFFFUL) return 0;
    return bg;
}

int owf_print_booklet_sides(int pages)
{
    return pages > 0 ? (pages + 3) / 4 * 2 : 0;
}

int owf_print_booklet_side(int pages, int side, int *left, int *right)
{
    int total = (pages + 3) / 4 * 4, sheet = side / 2, l, r;
    if (pages <= 0 || side < 0 || side >= total / 2) return -1;
    if (side % 2 == 0) { l = total - 1 - 2 * sheet; r = 2 * sheet; }   /* the front: the last page and the first */
    else { l = 2 * sheet + 1; r = total - 2 - 2 * sheet; }             /* the back: the second and the next to last */
    if (left) *left = l < pages ? l : -1;
    if (right) *right = r < pages ? r : -1;
    return 0;
}

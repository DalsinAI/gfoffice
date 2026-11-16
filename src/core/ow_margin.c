/*
 * GFOpenWrite's comment notes in the page's right margin (2.0.40): placed from the editor's comments and the
 * layout, apart from ow_layout.c, which GFOpenPresentation and GFOpenPublisher link without the editor.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ow_layout.h"

/* 2.0.40: the comment notes in a page's margin */
int ow_layout_margin_notes(const ow_layout *l, ow_editor *editor, int page, const ol_scale *s,
                           int line_height, ow_margin_note *notes, int max)
{
    const ol_page *pg;
    ow_comment_info info;
    int n, k, x0, x1, bottom = -100000, count = 0;
    if (!l || !editor || !s || line_height <= 0) return 0;
    pg = ow_layout_page(l);
    x0 = ol_scale_x(s, pg->width - pg->margin_right) + 6;
    x1 = ol_scale_x(s, pg->width) - 4;
    if (x1 - x0 < 24) return 0;
    n = ow_editor_comment_count(editor);
    for (k = 0; k < n; ++k) {
        int at_page, x, w, h, top, y;
        ol_rect at;
        if (!ow_editor_comment_info(editor, k, &info) || !info.shown || !ow_layout_comment_place(l, info.end, &at_page, &at) ||
            at_page != page)
            continue;
        x = x0 + (info.reply_to >= 0 ? 8 : 0);
        w = x1 - x;
        if (w < 16) continue;
        h = 2 * line_height + 4;
        top = ol_scale_y(s, at.y);
        y = top > bottom + 3 ? top : bottom + 3;
        bottom = y + h;
        if (notes && count < max) {
            ow_margin_note *m = &notes[count];
            m->index = k;
            m->x = x;
            m->y = y;
            m->width = w;
            m->height = h;
            m->margin_x = x0;
            m->place_x = ol_scale_x(s, at.x);
            m->place_y = top;
            m->place_height = ol_scale_length(s, at.height);
        }
        ++count;
    }
    return count;
}

int ow_margin_note_at(const ow_margin_note *notes, int n, int x, int y)
{
    int i;
    for (i = 0; notes && i < n; ++i)
        if (x >= notes[i].x && x < notes[i].x + notes[i].width && y >= notes[i].y && y < notes[i].y + notes[i].height)
            return notes[i].index;
    return -1;
}

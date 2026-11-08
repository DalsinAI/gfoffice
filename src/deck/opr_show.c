/*
 * GFOpenPresentation's portable slide-show navigation and timing. The Amiga
 * player and later ARexx commands share this state machine, so hidden slides,
 * looping and automatic advance do not acquire separate UI implementations.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <limits.h>
#include <string.h>

static int valid(const opr_show *show)
{
    return show && show->deck && show->deck->nslides > 0 &&
           show->slide >= 0 && show->slide < show->deck->nslides;
}

static int visible_forward(const owf_deck *deck, int from)
{
    int i;
    if (!deck) return -1;
    if (from < 0) from = 0;
    for (i = from; i < deck->nslides; i++)
        if (!deck->slides[i].hidden)
            return i;
    return -1;
}

static int visible_backward(const owf_deck *deck, int from)
{
    int i;
    if (!deck) return -1;
    if (from >= deck->nslides) from = deck->nslides - 1;
    for (i = from; i >= 0; i--)
        if (!deck->slides[i].hidden)
            return i;
    return -1;
}

static int move_to(opr_show *show, int slide)
{
    int changed;
    if (!show || !show->deck || slide < 0 || slide >= show->deck->nslides)
        return -1;
    changed = slide != show->slide;
    show->slide = slide;
    show->elapsed_ms = 0;
    return changed;
}

int opr_show_begin(opr_show *show, const owf_deck *deck, int from, int loop)
{
    int slide;
    if (!show) return -1;
    memset(show, 0, sizeof *show);
    if (!deck || deck->nslides <= 0)
        return -1;
    if (from < 0) from = 0;
    if (from >= deck->nslides) from = deck->nslides - 1;
    slide = visible_forward(deck, from);
    if (slide < 0)
        slide = visible_forward(deck, 0);
    if (slide < 0)
        return -1;
    show->deck = deck;
    show->slide = slide;
    show->loop = loop != 0;
    return 0;
}

int opr_show_next(opr_show *show)
{
    int slide;
    if (!valid(show)) return -1;
    slide = visible_forward(show->deck, show->slide + 1);
    if (slide < 0 && show->loop)
        slide = visible_forward(show->deck, 0);
    if (slide < 0)
        return -1;
    return move_to(show, slide);
}

int opr_show_previous(opr_show *show)
{
    int slide;
    if (!valid(show)) return -1;
    slide = visible_backward(show->deck, show->slide - 1);
    if (slide < 0 && show->loop)
        slide = visible_backward(show->deck, show->deck->nslides - 1);
    if (slide < 0)
        return 0;
    return move_to(show, slide);
}

int opr_show_first(opr_show *show)
{
    int slide;
    if (!valid(show)) return -1;
    slide = visible_forward(show->deck, 0);
    return slide < 0 ? -1 : move_to(show, slide);
}

int opr_show_last(opr_show *show)
{
    int slide;
    if (!valid(show)) return -1;
    slide = visible_backward(show->deck, show->deck->nslides - 1);
    return slide < 0 ? -1 : move_to(show, slide);
}

int opr_show_goto(opr_show *show, int slide)
{
    return valid(show) ? move_to(show, slide) : -1;
}

int opr_show_next_hidden(opr_show *show)
{
    int i;
    if (!valid(show)) return -1;
    for (i = show->slide + 1; i < show->deck->nslides; i++)
        if (show->deck->slides[i].hidden)
            return move_to(show, i);
    if (show->loop)
        for (i = 0; i < show->slide; i++)
            if (show->deck->slides[i].hidden)
                return move_to(show, i);
    return 0;
}

int opr_show_tick(opr_show *show, unsigned long milliseconds)
{
    int after;
    if (!valid(show)) return -1;
    after = show->deck->slides[show->slide].transition.advance_after;
    if (after < 0)
        return 0;
    if (milliseconds > ULONG_MAX - show->elapsed_ms)
        show->elapsed_ms = ULONG_MAX;
    else
        show->elapsed_ms += milliseconds;
    if (show->elapsed_ms < (unsigned long)after)
        return 0;
    return opr_show_next(show);
}

int opr_transition_frame_at(owf_transition_kind kind, int direction, int width, int height,
                            int step, int steps, opr_transition_frame *frame)
{
    int amount;
    if (!frame || width <= 0 || height <= 0 || step < 1 || steps < 1 || step > steps ||
        direction < OWF_DIR_LEFT || direction > OWF_DIR_DOWN ||
        (kind != OWF_TRANS_WIPE && kind != OWF_TRANS_PUSH))
        return -1;
    memset(frame, 0, sizeof *frame);
    if (direction == OWF_DIR_LEFT || direction == OWF_DIR_RIGHT) {
        amount = (int)((long)width * step / steps);
        if (kind == OWF_TRANS_WIPE) {
            frame->have_new = amount > 0;
            frame->new_part.source_x = frame->new_part.dest_x = direction == OWF_DIR_RIGHT ? width - amount : 0;
            frame->new_part.width = amount;
            frame->new_part.height = height;
        } else if (direction == OWF_DIR_LEFT) {
            frame->have_new = amount > 0;
            frame->new_part.source_x = width - amount;
            frame->new_part.width = amount;
            frame->new_part.height = height;
            frame->have_old = amount < width;
            frame->old_part.dest_x = amount;
            frame->old_part.width = width - amount;
            frame->old_part.height = height;
        } else {
            frame->have_old = amount < width;
            frame->old_part.source_x = amount;
            frame->old_part.width = width - amount;
            frame->old_part.height = height;
            frame->have_new = amount > 0;
            frame->new_part.dest_x = width - amount;
            frame->new_part.width = amount;
            frame->new_part.height = height;
        }
    } else {
        amount = (int)((long)height * step / steps);
        if (kind == OWF_TRANS_WIPE) {
            frame->have_new = amount > 0;
            frame->new_part.source_y = frame->new_part.dest_y = direction == OWF_DIR_DOWN ? height - amount : 0;
            frame->new_part.width = width;
            frame->new_part.height = amount;
        } else if (direction == OWF_DIR_UP) {
            frame->have_new = amount > 0;
            frame->new_part.source_y = height - amount;
            frame->new_part.width = width;
            frame->new_part.height = amount;
            frame->have_old = amount < height;
            frame->old_part.dest_y = amount;
            frame->old_part.width = width;
            frame->old_part.height = height - amount;
        } else {
            frame->have_old = amount < height;
            frame->old_part.source_y = amount;
            frame->old_part.width = width;
            frame->old_part.height = height - amount;
            frame->have_new = amount > 0;
            frame->new_part.dest_y = height - amount;
            frame->new_part.width = width;
            frame->new_part.height = amount;
        }
    }
    return 0;
}

int opr_transition_tile_count(int width, int height)
{
    int cols, rows;
    if (width <= 0 || height <= 0 || width > INT_MAX - 15 || height > INT_MAX - 15)
        return -1;
    cols = (width + 15) / 16;
    rows = (height + 15) / 16;
    if (rows > INT_MAX / cols)
        return -1;
    return cols * rows;
}

static int transition_gcd(int a, int b)
{
    while (b) { int r = a % b; a = b; b = r; }
    return a;
}

int opr_transition_tile_at(int width, int height, int index, opr_blit *tile)
{
    int total = opr_transition_tile_count(width, height), cols, multiplier = 37, shuffled;
    if (!tile || total <= 0 || index < 0 || index >= total)
        return -1;
    cols = (width + 15) / 16;
    while (transition_gcd(multiplier, total) != 1) multiplier += 2;
    shuffled = (index * multiplier + 17) % total;
    memset(tile, 0, sizeof *tile);
    tile->source_x = tile->dest_x = shuffled % cols * 16;
    tile->source_y = tile->dest_y = shuffled / cols * 16;
    tile->width = tile->source_x + 16 <= width ? 16 : width - tile->source_x;
    tile->height = tile->source_y + 16 <= height ? 16 : height - tile->source_y;
    return 0;
}

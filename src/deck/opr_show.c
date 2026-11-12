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
    show->steps = opr_anim_steps(show->deck, slide);
    show->step = 0;
    return changed;
}

/* A slide reached going back shows as it was left: every step played. */
static int move_back_to(opr_show *show, int slide)
{
    int rc = move_to(show, slide);
    if (rc >= 0)
        show->step = show->steps;
    return rc;
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
    show->steps = opr_anim_steps(deck, slide);
    return 0;
}

int opr_show_next(opr_show *show)
{
    int slide;
    if (!valid(show)) return -1;
    if (show->step < show->steps) {
        show->step++;
        show->elapsed_ms = 0;
        return 2;
    }
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
    if (show->step > 0) {
        show->step--;
        show->elapsed_ms = 0;
        return 2;
    }
    slide = visible_backward(show->deck, show->slide - 1);
    if (slide < 0 && show->loop)
        slide = visible_backward(show->deck, show->deck->nslides - 1);
    if (slide < 0)
        return 0;
    return move_back_to(show, slide);
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
    if (after < 0 || show->step < show->steps)
        return 0;
    if (milliseconds > ULONG_MAX - show->elapsed_ms)
        show->elapsed_ms = ULONG_MAX;
    else
        show->elapsed_ms += milliseconds;
    if (show->elapsed_ms < (unsigned long)after)
        return 0;
    return opr_show_next(show);
}

static void transition_part(opr_blit *part, int sx, int sy, int dx, int dy, int width, int height)
{
    part->source_x = sx;
    part->source_y = sy;
    part->dest_x = dx;
    part->dest_y = dy;
    part->width = width;
    part->height = height;
}

int opr_transition_plan_at(owf_transition_kind kind, int direction, int width, int height,
                           int step, int steps, opr_transition_plan *plan)
{
    int amount, i;
    if (!plan || width <= 0 || height <= 0 || step < 1 || steps < 1 || step > steps ||
        direction < OWF_DIR_LEFT || direction > OWF_DIR_DOWN || kind < OWF_TRANS_WIPE ||
        kind > OWF_TRANS_BLINDS)
        return -1;
    memset(plan, 0, sizeof *plan);
    if (direction == OWF_DIR_LEFT || direction == OWF_DIR_RIGHT) {
        amount = (int)((long)width * step / steps);
        if (kind == OWF_TRANS_WIPE) {
            if (amount > 0)
                transition_part(&plan->new_parts[plan->new_count++], direction == OWF_DIR_RIGHT ? width - amount : 0,
                                0, direction == OWF_DIR_RIGHT ? width - amount : 0, 0, amount, height);
        } else if (kind == OWF_TRANS_PUSH || kind == OWF_TRANS_COVER) {
            if (kind == OWF_TRANS_PUSH && amount < width) {
                if (direction == OWF_DIR_LEFT)
                    transition_part(&plan->old_parts[plan->old_count++], 0, 0, amount, 0, width - amount, height);
                else
                    transition_part(&plan->old_parts[plan->old_count++], amount, 0, 0, 0, width - amount, height);
            }
            if (amount > 0) {
                if (direction == OWF_DIR_LEFT)
                    transition_part(&plan->new_parts[plan->new_count++], width - amount, 0, 0, 0, amount, height);
                else
                    transition_part(&plan->new_parts[plan->new_count++], 0, 0, width - amount, 0, amount, height);
            }
        } else if (kind == OWF_TRANS_UNCOVER) {
            plan->base_new = 1;
            if (amount < width) {
                if (direction == OWF_DIR_LEFT)
                    transition_part(&plan->old_parts[plan->old_count++], 0, 0, amount, 0, width - amount, height);
                else
                    transition_part(&plan->old_parts[plan->old_count++], amount, 0, 0, 0, width - amount, height);
            }
        } else if (kind == OWF_TRANS_SPLIT) {
            int start = (width - amount) / 2;
            if (amount > 0)
                transition_part(&plan->new_parts[plan->new_count++], start, 0, start, 0, amount, height);
        } else { /* vertical blinds */
            for (i = 0; i < OPR_TRANSITION_MAX_PARTS; i++) {
                int start = width * i / OPR_TRANSITION_MAX_PARTS;
                int end = width * (i + 1) / OPR_TRANSITION_MAX_PARTS;
                int reveal = (end - start) * step / steps;
                int x = direction == OWF_DIR_RIGHT ? end - reveal : start;
                if (reveal > 0)
                    transition_part(&plan->new_parts[plan->new_count++], x, 0, x, 0, reveal, height);
            }
        }
    } else {
        amount = (int)((long)height * step / steps);
        if (kind == OWF_TRANS_WIPE) {
            if (amount > 0)
                transition_part(&plan->new_parts[plan->new_count++], 0,
                                direction == OWF_DIR_DOWN ? height - amount : 0, 0,
                                direction == OWF_DIR_DOWN ? height - amount : 0, width, amount);
        } else if (kind == OWF_TRANS_PUSH || kind == OWF_TRANS_COVER) {
            if (kind == OWF_TRANS_PUSH && amount < height) {
                if (direction == OWF_DIR_UP)
                    transition_part(&plan->old_parts[plan->old_count++], 0, 0, 0, amount, width, height - amount);
                else
                    transition_part(&plan->old_parts[plan->old_count++], 0, amount, 0, 0, width, height - amount);
            }
            if (amount > 0) {
                if (direction == OWF_DIR_UP)
                    transition_part(&plan->new_parts[plan->new_count++], 0, height - amount, 0, 0, width, amount);
                else
                    transition_part(&plan->new_parts[plan->new_count++], 0, 0, 0, height - amount, width, amount);
            }
        } else if (kind == OWF_TRANS_UNCOVER) {
            plan->base_new = 1;
            if (amount < height) {
                if (direction == OWF_DIR_UP)
                    transition_part(&plan->old_parts[plan->old_count++], 0, 0, 0, amount, width, height - amount);
                else
                    transition_part(&plan->old_parts[plan->old_count++], 0, amount, 0, 0, width, height - amount);
            }
        } else if (kind == OWF_TRANS_SPLIT) {
            int start = (height - amount) / 2;
            if (amount > 0)
                transition_part(&plan->new_parts[plan->new_count++], 0, start, 0, start, width, amount);
        } else { /* horizontal blinds */
            for (i = 0; i < OPR_TRANSITION_MAX_PARTS; i++) {
                int start = height * i / OPR_TRANSITION_MAX_PARTS;
                int end = height * (i + 1) / OPR_TRANSITION_MAX_PARTS;
                int reveal = (end - start) * step / steps;
                int y = direction == OWF_DIR_DOWN ? end - reveal : start;
                if (reveal > 0)
                    transition_part(&plan->new_parts[plan->new_count++], 0, y, 0, y, width, reveal);
            }
        }
    }
    return 0;
}

int opr_transition_frame_at(owf_transition_kind kind, int direction, int width, int height,
                            int step, int steps, opr_transition_frame *frame)
{
    opr_transition_plan plan;
    if (!frame || (kind != OWF_TRANS_WIPE && kind != OWF_TRANS_PUSH) ||
        opr_transition_plan_at(kind, direction, width, height, step, steps, &plan) < 0)
        return -1;
    memset(frame, 0, sizeof *frame);
    if (plan.old_count) {
        frame->have_old = 1;
        frame->old_part = plan.old_parts[0];
    }
    if (plan.new_count) {
        frame->have_new = 1;
        frame->new_part = plan.new_parts[0];
    }
    return 0;
}

int opr_transition_pattern_at(int step, int steps, unsigned short pattern[4])
{
    static const unsigned char order[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    int level, x, y;
    if (!pattern || step < 1 || steps < 1 || step > steps)
        return -1;
    level = (16 * step + steps - 1) / steps;
    for (y = 0; y < 4; y++) {
        unsigned short bits = 0;
        for (x = 0; x < 16; x++)
            if (order[y][x & 3] < level)
                bits |= (unsigned short)(1u << (15 - x));
        pattern[y] = bits;
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

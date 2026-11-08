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

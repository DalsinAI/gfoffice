/*
 * GFOpenPresentation's core: speaker notes (openpresentation_core.h, "Notes
 * pages and handouts"): the notes' text as one string, and setting it as
 * one Undo step. The notes page's geometry and its layout are in
 * opr_layout.c, which OWConvert's PDF links without the editing core.
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdlib.h>
#include <string.h>

char *opr_notes_get(const owf_deck *deck, int slide)
{
    const owf_obj *o;
    size_t total = 1, at = 0;
    char *out;
    int i;
    if (!deck || slide < 0 || slide >= deck->nslides)
        return NULL;
    o = &deck->slides[slide].notes;
    for (i = 0; i < o->text.nparas; i++) {
        size_t n;
        char *t = opr_para_text(&o->text.paras[i], &n);
        if (!t)
            return NULL;
        total += n + 1;
        owf_free(t);
    }
    out = owf_malloc(total);
    if (!out)
        return NULL;
    for (i = 0; i < o->text.nparas; i++) {
        size_t n;
        char *t = opr_para_text(&o->text.paras[i], &n);
        if (!t) {
            owf_free(out);
            return NULL;
        }
        if (i)
            out[at++] = '\n';
        memcpy(out + at, t, n);
        at += n;
        owf_free(t);
    }
    out[at] = 0;
    return out;
}

int opr_notes_set(opr_undo *u, owf_deck *deck, int slide, const char *utf8)
{
    if (!deck || slide < 0 || slide >= deck->nslides || !utf8)
        return -1;
    if (u && opr_undo_push(u, deck, slide, OPR_NOTES) < 0)
        return -1;
    /* run out of memory part way: the step put back what was there */
    return opr_text_set(deck, slide, OPR_NOTES, utf8) < 0 ? -1 : 0;
}

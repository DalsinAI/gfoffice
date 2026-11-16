/*
 * GFOpenPresentation's core: spelling through a deck (see
 * openpresentation_core.h, "Spelling"). Each slide's typeable objects in
 * order, then its notes, checked with GFOpenWrite's dictionary and word
 * rules (src/core/ow_spell_dict.c). A placeholder still showing its prompt
 * is not checked: the prompt is not the author's text.
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "ow_spell_dict.h"

#include <string.h>

static const owf_obj *spell_object(const owf_deck *deck, int slide, int obj)
{
    const owf_slide *s = &deck->slides[slide];
    if (obj == OPR_NOTES) return &s->notes;
    return obj >= 0 && obj < s->nobjs ? &s->objs[obj] : NULL;
}

/* -1, 0 or 1 as (slide, obj, para, offset) comes before, at or after the other. */
static int spell_compare(int slide, int obj, int para, size_t offset,
                         int slide2, int obj2, int para2, size_t offset2)
{
    if (slide != slide2) return slide < slide2 ? -1 : 1;
    if (obj != obj2) return obj < obj2 ? -1 : 1;
    if (para != para2) return para < para2 ? -1 : 1;
    if (offset != offset2) return offset < offset2 ? -1 : 1;
    return 0;
}

int opr_spell_words(ow_spell *spell, const char *text, size_t length,
                    opr_spell_range *ranges, int max_ranges)
{
    size_t from = 0, a, b;
    int n = 0;
    if (!spell || !text) return 0;
    while (from < length && ow_spell_scan(spell, text, from, length, &a, &b)) {
        if (n < max_ranges && ranges) {
            ranges[n].start = a;
            ranges[n].end = b;
        }
        n++;
        from = b;
    }
    return n;
}

int opr_spell_next(const owf_deck *deck, ow_spell *spell, int start_slide, int start_obj,
                   opr_pos start, opr_find_result *result, char *word, size_t word_size)
{
    opr_find_result first, after;
    int have_first = 0, have_after = 0, slide, k;
    if (!deck || !spell || !result) return -1;
    if (start_slide < 0 || start_slide >= deck->nslides) {
        start_slide = 0;
        start_obj = 0;
        start.para = 0;
        start.offset = 0;
    }
    for (slide = 0; slide < deck->nslides && !have_after; ++slide) {
        const owf_slide *s = &deck->slides[slide];
        /* the objects in order, then the notes (OPR_NOTES is above any object) */
        for (k = 0; k <= s->nobjs && !have_after; ++k) {
            int obj = k < s->nobjs ? k : OPR_NOTES, para;
            const owf_obj *o = spell_object(deck, slide, obj);
            if (!o || (o->flags & OWF_OBJ_EMPTY) || !opr_obj_editable(deck, slide, obj)) continue;
            for (para = 0; para < o->text.nparas && !have_after; ++para) {
                size_t n, from = 0, a, b;
                char *text = opr_para_text(&o->text.paras[para], &n);
                if (!text) return -1;
                while (from < n && ow_spell_scan(spell, text, from, n, &a, &b)) {
                    opr_find_result found;
                    found.slide = slide;
                    found.obj = obj;
                    found.selection.anchor.para = found.selection.focus.para = para;
                    found.selection.anchor.offset = a;
                    found.selection.focus.offset = b;
                    if (!have_first) {
                        first = found;
                        have_first = 1;
                        if (word && word_size) {
                            size_t m = b - a < word_size - 1 ? b - a : word_size - 1;
                            memcpy(word, text + a, m);
                            word[m] = 0;
                        }
                    }
                    if (spell_compare(slide, obj, para, a, start_slide, start_obj, start.para, start.offset) >= 0) {
                        after = found;
                        have_after = 1;
                        if (word && word_size) {
                            size_t m = b - a < word_size - 1 ? b - a : word_size - 1;
                            memcpy(word, text + a, m);
                            word[m] = 0;
                        }
                        break;
                    }
                    from = b;
                }
                owf_free(text);
            }
        }
    }
    if (have_after) *result = after;
    else if (have_first) *result = first;
    else return 0;
    return 1;
}

int opr_spell_marks(const owf_deck *deck, ow_spell *spell, int slide, int obj, const opr_pos *caret,
                    opr_spell_mark *marks, int max_marks)
{
    const owf_obj *o;
    int n = 0, para;
    if (!deck || !spell || slide < 0 || slide >= deck->nslides) return -1;
    if (!(o = spell_object(deck, slide, obj))) return -1;
    if ((o->flags & OWF_OBJ_EMPTY) || !opr_obj_editable(deck, slide, obj)) return 0;
    for (para = 0; para < o->text.nparas; ++para) {
        size_t len, from = 0, a, b;
        char *text = opr_para_text(&o->text.paras[para], &len);
        if (!text) return -1;
        while (from < len && ow_spell_scan(spell, text, from, len, &a, &b)) {
            from = b;
            /* the word being typed: the caret in it or just after it */
            if (caret && caret->para == para && caret->offset >= a && caret->offset <= b) continue;
            if (marks && n < max_marks) {
                marks[n].para = para;
                marks[n].start = a;
                marks[n].end = b;
            }
            n++;
        }
        owf_free(text);
    }
    return n;
}

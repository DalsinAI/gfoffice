/*
 * GFOpenPresentation's core: spelling through a deck (see
 * openpresentation_core.h, "Spelling"). Each slide's typeable objects in
 * order, then its notes, checked with GFOffice's one dictionary and word
 * rules (owfspell.library, libowf/include/owf_spell.h). A placeholder still showing its prompt
 * is not checked: the prompt is not the author's text.
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_spell.h"

#include <stdio.h>
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

int opr_spell_words(owf_spell *spell, const char *text, size_t length,
                    opr_spell_range *ranges, int max_ranges)
{
    if (!spell) return 0;   /* on the Amiga the call goes through owfspell.library */
    return owf_spell_words(spell, text, length, ranges, max_ranges);
}

int opr_spell_next(const owf_deck *deck, owf_spell *spell, int start_slide, int start_obj,
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
                while (from < n && owf_spell_scan(spell, text, from, n, &a, &b)) {
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

int opr_spell_marks(const owf_deck *deck, owf_spell *spell, int slide, int obj, const opr_pos *caret,
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
        while (from < len && owf_spell_scan(spell, text, from, len, &a, &b)) {
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

/* ---- ARexx SPELL (opr_spell_rexx) ---- */

typedef int (*spell_word_fn)(void *user, int slide, int obj, const char *word, size_t length);

/* Every misspelt word of the deck in order, slides' objects then notes; how many, -1 no memory. */
static int spell_walk(const owf_deck *deck, owf_spell *spell, spell_word_fn fn, void *user)
{
    int n = 0, slide, k;
    for (slide = 0; slide < deck->nslides; ++slide) {
        const owf_slide *s = &deck->slides[slide];
        for (k = 0; k <= s->nobjs; ++k) {
            int obj = k < s->nobjs ? k : OPR_NOTES, para;
            const owf_obj *o = spell_object(deck, slide, obj);
            if (!o || (o->flags & OWF_OBJ_EMPTY) || !opr_obj_editable(deck, slide, obj)) continue;
            for (para = 0; para < o->text.nparas; ++para) {
                size_t len, from = 0, a, b;
                char *text = opr_para_text(&o->text.paras[para], &len);
                if (!text) return -1;
                while (from < len && owf_spell_scan(spell, text, from, len, &a, &b)) {
                    if (fn) fn(user, slide, obj, text + a, b - a);
                    n++;
                    from = b;
                }
                owf_free(text);
            }
        }
    }
    return n;
}

typedef struct {
    char *out;
    size_t size, used;
    int full;
} spell_list;

static int spell_list_word(void *user, int slide, int obj, const char *word, size_t length)
{
    spell_list *l = (spell_list *)user;
    char where[32];
    size_t need;
    if (l->full) return 0;
    if (obj == OPR_NOTES) snprintf(where, sizeof where, "%d NOTES ", slide + 1);
    else snprintf(where, sizeof where, "%d %d ", slide + 1, obj + 1);
    need = (l->used ? 1 : 0) + strlen(where) + length;
    if (l->used + need + 1 > l->size) {
        l->full = 1;
        return 0;
    }
    if (l->used) l->out[l->used++] = '\n';
    memcpy(l->out + l->used, where, strlen(where));
    l->used += strlen(where);
    memcpy(l->out + l->used, word, length);
    l->used += length;
    l->out[l->used] = 0;
    return 0;
}

static int rx_spell_word(const char *s, const char *word, const char **rest)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != word[i]) return 0;
    }
    if (s[n] && s[n] != ' ' && s[n] != '\t') return 0;
    s += n;
    while (*s == ' ' || *s == '\t') s++;
    *rest = s;
    return 1;
}

/* The word after a sub-command: "in quotes" or up to the next space. */
static void rx_spell_arg(const char *s, char *out, size_t size)
{
    size_t k = 0;
    if (*s == '"' || *s == '\'') {
        char q = *s++;
        for (; *s && *s != q; s++) if (k + 1 < size) out[k++] = *s;
    } else {
        for (; *s && *s != ' ' && *s != '\t'; s++) if (k + 1 < size) out[k++] = *s;
    }
    out[k] = 0;
}

int opr_spell_rexx(const owf_deck *deck, owf_spell *spell, const char *args, char *out, size_t size)
{
    const char *rest = "";
    char word[96];
    if (!out || size < 2) return 10;
    out[0] = 0;
    if (!spell) { snprintf(out, size, "No dictionary"); return 5; }
    if (!args) args = "";
    while (*args == ' ' || *args == '\t') args++;
    if (!*args || rx_spell_word(args, "COUNT", &rest) || rx_spell_word(args, "LIST", &rest)) {
        int list = *args && rx_spell_word(args, "LIST", &rest), n;
        spell_list l;
        if (!deck) { snprintf(out, size, "No document"); return 10; }
        l.out = out; l.size = size; l.used = 0; l.full = 0;
        n = spell_walk(deck, spell, list ? spell_list_word : NULL, &l);
        if (n < 0) { snprintf(out, size, "Not enough memory"); return 20; }
        if (!list) snprintf(out, size, "%d", n);
        return 0;
    }
    {
        static const char *const verbs[] = { "CHECK", "SUGGEST", "ADD", "IGNORE" };
        int v;
        for (v = 0; v < 4 && !rx_spell_word(args, verbs[v], &rest); v++) ;
        if (v == 4) {
            snprintf(out, size, "SPELL takes COUNT, LIST, CHECK, SUGGEST, ADD or IGNORE");
            return 10;
        }
        rx_spell_arg(rest, word, sizeof word);
        if (!word[0]) { snprintf(out, size, "SPELL %s needs a word", verbs[v]); return 10; }
        if (v == 0) {
            snprintf(out, size, "%d", owf_spell_word_ok(spell, word, strlen(word)));
        } else if (v == 1) {
            char sugg[8][48];
            int n = owf_spell_suggest(spell, word, sugg[0], 8), i;
            size_t used = 0;
            for (i = 0; i < n; i++) {
                size_t m = strlen(sugg[i]);
                if (used + m + 2 > size) break;
                if (used) out[used++] = ' ';
                memcpy(out + used, sugg[i], m);
                used += m;
                out[used] = 0;
            }
        } else if (v == 2) {
            if (!owf_spell_add(spell, word)) { snprintf(out, size, "The word could not be saved"); return 10; }
        } else if (!owf_spell_ignore(spell, word)) {
            snprintf(out, size, "Not enough memory");
            return 20;
        }
        return 0;
    }
}

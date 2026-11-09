/*
 * GFOpenPresentation's core: editing a text frame's text (see
 * openpresentation_core.h, "Editing"). Positions are a paragraph and a
 * byte offset into its text as opr_para_text gives it; text goes into the
 * text run at the position, so it takes that run's look. The caret and the
 * pointer are mapped through the slide's GFOpenLayout display list: each
 * paragraph is a block of the frame, each run a node after it (after the
 * bullet and its tab, for a list paragraph), as opr_layout.c builds them.
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PARAS 100000

static owf_obj *object(owf_deck *deck, int slide, int obj)
{
    if (!deck || slide < 0 || slide >= deck->nslides || obj < 0 || obj >= deck->slides[slide].nobjs)
        return NULL;
    return &deck->slides[slide].objs[obj];
}

int opr_obj_editable(const owf_deck *deck, int slide, int obj)
{
    const owf_obj *o = object((owf_deck *)deck, slide, obj);
    if (!o || (o->flags & OWF_OBJ_HIDDEN))
        return 0;
    if (o->kind == OWF_OBJ_TEXT || o->kind == OWF_OBJ_TABLE)
        return 1;
    /* a shape's text, but not a line's */
    return o->kind == OWF_OBJ_SHAPE && o->shape != OWF_SHAPE_LINE;
}

/* How many bytes a run counts for in its paragraph's text. */
static size_t run_length(const owf_run *r)
{
    switch (r->kind) {
    case OWF_RUN_TEXT: return r->text ? strlen(r->text) : 0;
    case OWF_RUN_TAB:
    case OWF_RUN_LINEBREAK: return 1;
    default: return 0;
    }
}

size_t opr_para_length(const owf_para *p)
{
    size_t n = 0;
    int j;
    for (j = 0; p && j < p->nruns; j++)
        n += run_length(&p->runs[j]);
    return n;
}

char *opr_para_text(const owf_para *p, size_t *length)
{
    size_t n = opr_para_length(p), at = 0;
    char *s = owf_malloc(n + 1);
    int j;
    if (!s)
        return NULL;
    for (j = 0; p && j < p->nruns; j++) {
        const owf_run *r = &p->runs[j];
        size_t k = run_length(r);
        if (r->kind == OWF_RUN_TEXT && k)
            memcpy(s + at, r->text, k);
        else if (r->kind == OWF_RUN_TAB)
            s[at] = '\t';
        else if (r->kind == OWF_RUN_LINEBREAK)
            s[at] = '\v';
        at += k;
    }
    s[at] = 0;
    if (length)
        *length = at;
    return s;
}

char *opr_text_get(const owf_deck *deck, int slide, int obj)
{
    const owf_obj *o = object((owf_deck *)deck, slide, obj);
    size_t total = 0, at = 0;
    char *s;
    int i;
    if (!o)
        return NULL;
    for (i = 0; i < o->text.nparas; i++)
        total += opr_para_length(&o->text.paras[i]) + 1;
    s = owf_malloc(total + 1);
    if (!s)
        return NULL;
    for (i = 0; i < o->text.nparas; i++) {
        size_t n;
        char *p = opr_para_text(&o->text.paras[i], &n);
        if (!p) {
            owf_free(s);
            return NULL;
        }
        if (i)
            s[at++] = '\n';
        memcpy(s + at, p, n);
        at += n;
        owf_free(p);
    }
    s[at] = 0;
    return s;
}

/* ---- Changing runs and paragraphs ---- */

static int insert_run(owf_para *p, int at, const owf_run *r)
{
    if (p->nruns == p->capruns) {
        int want = p->capruns ? p->capruns * 2 : 4;
        owf_run *n = owf_realloc(p->runs, (size_t)want * sizeof *n);
        if (!n)
            return -1;
        p->runs = n;
        p->capruns = want;
    }
    memmove(&p->runs[at + 1], &p->runs[at], (size_t)(p->nruns - at) * sizeof *p->runs);
    p->runs[at] = *r;
    p->nruns++;
    return 0;
}

static void remove_run(owf_para *p, int at)
{
    owf_free(p->runs[at].text);
    owf_free(p->runs[at].href);
    memmove(&p->runs[at], &p->runs[at + 1], (size_t)(p->nruns - at - 1) * sizeof *p->runs);
    p->nruns--;
}

static char *copy_text(const char *s, size_t n)
{
    char *out = owf_malloc(n + 1);
    if (out) {
        memcpy(out, s, n);
        out[n] = 0;
    }
    return out;
}

/* The run an offset falls in, and where in it: the text run it is inside
 * or at the end of, else -1 with *before the index a new run goes at. */
static int run_at(const owf_para *p, size_t offset, size_t *inner, int *before)
{
    size_t cum = 0;
    int j, best = -1;
    *before = 0;
    for (j = 0; j < p->nruns; j++) {
        size_t k = run_length(&p->runs[j]);
        if (offset < cum)
            break;
        if (p->runs[j].kind == OWF_RUN_TEXT && offset <= cum + k) {
            best = j;
            *inner = offset - cum;
            if (offset < cum + k)
                break;
        }
        if (offset >= cum + k)
            *before = j + 1;
        cum += k;
    }
    return best;
}

/* The look new text takes where there is no text run: the run before it,
 * else after it, else the level's (font -1, size 0, colour automatic). */
static owf_charfmt look_at(const owf_para *p, int before)
{
    owf_charfmt f;
    if (before > 0 && before <= p->nruns)
        return p->runs[before - 1].fmt;
    if (before < p->nruns)
        return p->runs[before].fmt;
    owf_charfmt_init(&f);
    return f;
}

static int insert_plain(owf_para *p, size_t offset, const char *s, size_t n)
{
    size_t inner = 0;
    int before, j = run_at(p, offset, &inner, &before);
    if (j >= 0) {
        owf_run *r = &p->runs[j];
        size_t len = r->text ? strlen(r->text) : 0;
        char *t = owf_malloc(len + n + 1);
        if (!t)
            return -1;
        memcpy(t, r->text ? r->text : "", inner);
        memcpy(t + inner, s, n);
        memcpy(t + inner + n, (r->text ? r->text : "") + inner, len - inner + 1);
        owf_free(r->text);
        r->text = t;
        return 0;
    } else {
        owf_run r;
        memset(&r, 0, sizeof r);
        r.kind = OWF_RUN_TEXT;
        r.fmt = look_at(p, before);
        r.text = copy_text(s, n);
        if (!r.text)
            return -1;
        if (insert_run(p, before, &r) < 0) {
            owf_free(r.text);
            return -1;
        }
        return 0;
    }
}

/* A tab run at offset: inside a text run, the run is cut in two. */
static int split_run_at(owf_para *p, size_t offset, int *index)
{
    size_t cum = 0;
    int j;
    for (j = 0; j < p->nruns; j++) {
        size_t k = run_length(&p->runs[j]);
        if (offset == cum) {
            *index = j;
            return 0;
        }
        if (offset < cum + k) {
            /* inside run j: a text run, cut */
            owf_run tail = p->runs[j];
            size_t inner = offset - cum;
            tail.text = copy_text(p->runs[j].text + inner, k - inner);
            tail.href = p->runs[j].href ? copy_text(p->runs[j].href, strlen(p->runs[j].href)) : NULL;
            if (!tail.text || (p->runs[j].href && !tail.href) || insert_run(p, j + 1, &tail) < 0) {
                owf_free(tail.text);
                owf_free(tail.href);
                return -1;
            }
            p->runs[j].text[inner] = 0;
            *index = j + 1;
            return 0;
        }
        cum += k;
    }
    *index = p->nruns;
    return 0;
}

static int insert_special(owf_para *p, size_t offset, owf_run_kind kind)
{
    owf_run r;
    int at;
    if (split_run_at(p, offset, &at) < 0)
        return -1;
    memset(&r, 0, sizeof r);
    r.kind = kind;
    r.fmt = look_at(p, at);
    return insert_run(p, at, &r);
}

/* A paragraph's levels moved up by one from index on (a paragraph
 * inserted at index), or down (removed). */
static int shift_levels(owf_obj *o, int index, int up)
{
    int i;
    if (up) {
        for (i = o->text.nparas - 1; i > index; i--)
            if (owf_obj_set_level(o, i, owf_obj_level(o, i - 1)) < 0)
                return -1;
    } else {
        for (i = index; i < o->text.nparas; i++)
            if (owf_obj_set_level(o, i, owf_obj_level(o, i + 1)) < 0)
                return -1;
        if (o->levels && o->text.nparas < o->caplevels)
            o->levels[o->text.nparas] = 0;
    }
    return 0;
}

/* An empty object (a placeholder showing its prompt) about to get text:
 * one paragraph, at its kind's level. */
static int make_room(owf_deck *deck, int slide, owf_obj *o)
{
    o->flags &= ~(unsigned)OWF_OBJ_EMPTY;
    if (!o->text.nparas) {
        int level = o->placeholder == OWF_PH_BODY || o->placeholder == OWF_PH_OBJECT || o->placeholder == OWF_PH_TEXT;
        if (!owf_deck_add_para(deck, owf_deck_slide_master(deck, slide), o, level))
            return -1;
    }
    return 0;
}

static void clamp_pos(const owf_obj *o, opr_pos *at)
{
    size_t n;
    if (at->para < 0)
        at->para = 0;
    if (at->para >= o->text.nparas)
        at->para = o->text.nparas ? o->text.nparas - 1 : 0;
    if (!o->text.nparas) {
        at->offset = 0;
        return;
    }
    n = opr_para_length(&o->text.paras[at->para]);
    if (at->offset > n)
        at->offset = n;
}

static int compare_pos(opr_pos a, opr_pos b)
{
    if (a.para != b.para)
        return a.para < b.para ? -1 : 1;
    if (a.offset != b.offset)
        return a.offset < b.offset ? -1 : 1;
    return 0;
}

int opr_text_selection_empty(const opr_selection *selection)
{
    return !selection || compare_pos(selection->anchor, selection->focus) == 0;
}

char *opr_text_selection_get(const owf_deck *deck, int slide, int obj,
                             const opr_selection *selection, size_t *length)
{
    const owf_obj *o = object((owf_deck *)deck, slide, obj);
    opr_pos start, end;
    size_t total = 0, at = 0;
    char *out;
    int para;
    if (length)
        *length = 0;
    if (!o || !selection || !o->text.nparas)
        return NULL;
    start = selection->anchor;
    end = selection->focus;
    clamp_pos(o, &start);
    clamp_pos(o, &end);
    if (compare_pos(start, end) > 0) {
        opr_pos swap = start;
        start = end;
        end = swap;
    }
    for (para = start.para; para <= end.para; ++para) {
        size_t begin = para == start.para ? start.offset : 0;
        size_t finish = para == end.para ? end.offset : opr_para_length(&o->text.paras[para]);
        if (finish > begin)
            total += finish - begin;
        if (para < end.para)
            ++total;
    }
    out = owf_malloc(total + 1);
    if (!out)
        return NULL;
    for (para = start.para; para <= end.para; ++para) {
        size_t n, begin = para == start.para ? start.offset : 0;
        size_t finish = para == end.para ? end.offset : opr_para_length(&o->text.paras[para]);
        char *text = opr_para_text(&o->text.paras[para], &n);
        if (!text) {
            owf_free(out);
            return NULL;
        }
        if (finish > n)
            finish = n;
        if (begin > finish)
            begin = finish;
        memcpy(out + at, text + begin, finish - begin);
        at += finish - begin;
        owf_free(text);
        if (para < end.para)
            out[at++] = '\n';
    }
    out[at] = 0;
    if (length)
        *length = at;
    return out;
}

static unsigned char find_fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int find_equal(const char *text, const char *needle, size_t length,
                      int match_case)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        unsigned char a = (unsigned char)text[i];
        unsigned char b = (unsigned char)needle[i];
        if (match_case ? a != b : find_fold(a) != find_fold(b))
            return 0;
    }
    return 1;
}

static int find_position_compare(int slide, int obj, int para, size_t offset,
                                 int other_slide, int other_obj, int other_para,
                                 size_t other_offset)
{
    if (slide != other_slide) return slide < other_slide ? -1 : 1;
    if (obj != other_obj) return obj < other_obj ? -1 : 1;
    if (para != other_para) return para < other_para ? -1 : 1;
    if (offset != other_offset) return offset < other_offset ? -1 : 1;
    return 0;
}

int opr_text_find(const owf_deck *deck, int start_slide, int start_obj,
                  opr_pos start, const char *needle, int match_case,
                  opr_find_result *result)
{
    opr_find_result first, after;
    size_t needle_length;
    int have_first = 0, have_after = 0, slide, obj, para;
    if (!deck || !needle || !*needle || !result || strchr(needle, '\n') || strchr(needle, '\r'))
        return -1;
    needle_length = strlen(needle);
    if (start_slide < 0 || start_slide >= deck->nslides) {
        start_slide = 0;
        start_obj = 0;
        start.para = 0;
        start.offset = 0;
    }
    for (slide = 0; slide < deck->nslides; ++slide) {
        const owf_slide *s = &deck->slides[slide];
        for (obj = 0; obj < s->nobjs; ++obj) {
            const owf_obj *o = &s->objs[obj];
            if (!opr_obj_editable(deck, slide, obj))
                continue;
            for (para = 0; para < o->text.nparas; ++para) {
                size_t n, offset;
                char *text = opr_para_text(&o->text.paras[para], &n);
                if (!text)
                    return -1;
                for (offset = 0; needle_length <= n && offset <= n - needle_length; ++offset) {
                    opr_find_result found;
                    if (!find_equal(text + offset, needle, needle_length, match_case))
                        continue;
                    found.slide = slide;
                    found.obj = obj;
                    found.selection.anchor.para = found.selection.focus.para = para;
                    found.selection.anchor.offset = offset;
                    found.selection.focus.offset = offset + needle_length;
                    if (!have_first) {
                        first = found;
                        have_first = 1;
                    }
                    if (find_position_compare(slide, obj, para, offset,
                                              start_slide, start_obj,
                                              start.para, start.offset) >= 0 &&
                        !have_after) {
                        after = found;
                        have_after = 1;
                    }
                }
                owf_free(text);
            }
        }
    }
    if (!have_after && !have_first)
        return 0;
    *result = have_after ? after : first;
    return 1;
}

int opr_text_newline(owf_deck *deck, int slide, int obj, opr_pos *at)
{
    owf_obj *o = object(deck, slide, obj);
    owf_para *p, *q;
    owf_parafmt fmt;
    int split, j, level;
    if (!o || !at || make_room(deck, slide, o) < 0)
        return -1;
    clamp_pos(o, at);
    if (o->text.nparas >= MAX_PARAS)
        return -1;
    p = &o->text.paras[at->para];
    if (split_run_at(p, at->offset, &split) < 0)
        return -1;
    level = owf_obj_level(o, at->para);
    fmt = p->fmt;                    /* owf_story_add may move the paragraphs */
    q = owf_story_add(&o->text, &fmt);
    if (!q)
        return -1;
    /* the new one goes after the paragraph: move it there */
    {
        owf_para made = *q;
        int n = o->text.nparas - 1;
        memmove(&o->text.paras[at->para + 2], &o->text.paras[at->para + 1],
                (size_t)(n - at->para - 1) * sizeof *o->text.paras);
        o->text.paras[at->para + 1] = made;
    }
    p = &o->text.paras[at->para];
    q = &o->text.paras[at->para + 1];
    q->table_id = p->table_id;
    q->table_row = p->table_row;
    q->table_col = p->table_col;
    q->table_cols = p->table_cols;
    q->cell_shading = p->cell_shading;
    /* the runs after the split move down */
    for (j = split; j < p->nruns; j++)
        if (insert_run(q, q->nruns, &p->runs[j]) < 0) {
            q->nruns = 0;            /* the runs stay the first paragraph's */
            return -1;
        }
    if (!q->nruns && p->nruns) {
        /* an empty new paragraph keeps the look of the text it came from */
        owf_run r;
        memset(&r, 0, sizeof r);
        r.kind = OWF_RUN_TEXT;
        r.fmt = p->runs[p->nruns - 1].fmt;
        r.text = copy_text("", 0);
        if (!r.text || insert_run(q, 0, &r) < 0) {
            owf_free(r.text);
            return -1;
        }
    }
    p->nruns = split;
    if (shift_levels(o, at->para + 1, 1) < 0 || owf_obj_set_level(o, at->para + 1, level) < 0)
        return -1;
    at->para++;
    at->offset = 0;
    return 0;
}

int opr_text_insert(owf_deck *deck, int slide, int obj, opr_pos *at, const char *utf8, size_t n)
{
    owf_obj *o = object(deck, slide, obj);
    size_t i = 0, start = 0;
    if (!o || !at || !utf8 || make_room(deck, slide, o) < 0)
        return -1;
    clamp_pos(o, at);
    for (i = 0; i <= n; i++) {
        if (i < n && utf8[i] != '\n' && utf8[i] != '\t' && utf8[i] != '\r' && utf8[i] != '\v')
            continue;
        if (i > start) {
            if (insert_plain(&o->text.paras[at->para], at->offset, utf8 + start, i - start) < 0)
                return -1;
            at->offset += i - start;
        }
        if (i < n) {
            if (utf8[i] == '\n' || utf8[i] == '\r') {
                if (opr_text_newline(deck, slide, obj, at) < 0)
                    return -1;
                o = object(deck, slide, obj);
            } else {
                if (insert_special(&o->text.paras[at->para], at->offset,
                                   utf8[i] == '\t' ? OWF_RUN_TAB : OWF_RUN_LINEBREAK) < 0)
                    return -1;
                at->offset++;
            }
        }
        start = i + 1;
    }
    return 0;
}

/* The bytes from..to of a paragraph's text (within one run each time). */
static void remove_bytes(owf_para *p, size_t from, size_t to)
{
    while (to > from) {
        size_t cum = 0;
        int changed = 0, j;
        for (j = 0; j < p->nruns; j++) {
            size_t k = run_length(&p->runs[j]);
            if (to > cum && to <= cum + k && k) {
                size_t a = from > cum ? from - cum : 0, b = to - cum;
                if (p->runs[j].kind == OWF_RUN_TEXT && (a > 0 || b < k)) {
                    memmove(p->runs[j].text + a, p->runs[j].text + b, k - b + 1);
                } else {
                    remove_run(p, j);
                }
                to = cum + a;
                changed = 1;
                break;
            }
            cum += k;
        }
        if (!changed)
            break;
    }
    /* text runs that are now empty go, but a paragraph keeps one run for its look */
    {
        int j;
        for (j = p->nruns - 1; j >= 0 && p->nruns > 1; j--)
            if (p->runs[j].kind == OWF_RUN_TEXT && (!p->runs[j].text || !p->runs[j].text[0]))
                remove_run(p, j);
    }
}

/* Paragraph index + 1 joined onto index. */
static int join(owf_obj *o, int index)
{
    owf_para *p = &o->text.paras[index], *q = &o->text.paras[index + 1];
    int j;
    for (j = 0; j < q->nruns; j++)
        if (insert_run(p, p->nruns, &q->runs[j]) < 0) {
            p->nruns -= j;           /* the runs stay the second paragraph's */
            return -1;
        }
    q = &o->text.paras[index + 1];
    owf_free(q->runs);
    owf_free(q->style);
    owf_free(q->bookmarks);
    memmove(&o->text.paras[index + 1], &o->text.paras[index + 2],
            (size_t)(o->text.nparas - index - 2) * sizeof *o->text.paras);
    o->text.nparas--;
    return shift_levels(o, index + 1, 0);
}

static void remove_para(owf_obj *o, int index)
{
    owf_para *p = &o->text.paras[index];
    int j;
    for (j = 0; j < p->nruns; ++j) {
        owf_free(p->runs[j].text);
        owf_free(p->runs[j].href);
    }
    owf_free(p->runs);
    owf_free(p->style);
    owf_free(p->bookmarks);
    memmove(&o->text.paras[index], &o->text.paras[index + 1],
            (size_t)(o->text.nparas - index - 1) * sizeof *o->text.paras);
    o->text.nparas--;
    shift_levels(o, index, 0);
}

static size_t utf8_back(const char *s, size_t at)
{
    if (!at)
        return 0;
    at--;
    while (at > 0 && ((unsigned char)s[at] & 0xC0) == 0x80)
        at--;
    return at;
}

static size_t utf8_forward(const char *s, size_t at, size_t n)
{
    if (at >= n)
        return n;
    at++;
    while (at < n && ((unsigned char)s[at] & 0xC0) == 0x80)
        at++;
    return at;
}

static int same_table_cell(const owf_para *a, const owf_para *b)
{
    return a && b && a->table_id >= 0 && a->table_id == b->table_id &&
           a->table_row == b->table_row && a->table_col == b->table_col;
}

int opr_text_backspace(owf_deck *deck, int slide, int obj, opr_pos *at)
{
    owf_obj *o = object(deck, slide, obj);
    if (!o || !at || !o->text.nparas)
        return -1;
    clamp_pos(o, at);
    if (at->offset > 0) {
        size_t n;
        char *t = opr_para_text(&o->text.paras[at->para], &n);
        size_t from;
        if (!t)
            return -1;
        from = utf8_back(t, at->offset);
        owf_free(t);
        remove_bytes(&o->text.paras[at->para], from, at->offset);
        at->offset = from;
        return 0;
    }
    if (at->para == 0)
        return 0;
    if (o->kind == OWF_OBJ_TABLE &&
        !same_table_cell(&o->text.paras[at->para - 1], &o->text.paras[at->para]))
        return 0;
    at->offset = opr_para_length(&o->text.paras[at->para - 1]);
    at->para--;
    return join(o, at->para);
}

int opr_text_delete(owf_deck *deck, int slide, int obj, opr_pos *at)
{
    owf_obj *o = object(deck, slide, obj);
    size_t n;
    if (!o || !at || !o->text.nparas)
        return -1;
    clamp_pos(o, at);
    n = opr_para_length(&o->text.paras[at->para]);
    if (at->offset < n) {
        char *t = opr_para_text(&o->text.paras[at->para], &n);
        size_t to;
        if (!t)
            return -1;
        to = utf8_forward(t, at->offset, n);
        owf_free(t);
        remove_bytes(&o->text.paras[at->para], at->offset, to);
        return 0;
    }
    if (at->para + 1 >= o->text.nparas)
        return 0;
    if (o->kind == OWF_OBJ_TABLE &&
        !same_table_cell(&o->text.paras[at->para], &o->text.paras[at->para + 1]))
        return 0;
    return join(o, at->para);
}

int opr_text_delete_selection(owf_deck *deck, int slide, int obj,
                              opr_selection *selection)
{
    owf_obj *o = object(deck, slide, obj);
    opr_pos start, end;
    if (!o || !selection || !o->text.nparas)
        return -1;
    start = selection->anchor;
    end = selection->focus;
    clamp_pos(o, &start);
    clamp_pos(o, &end);
    if (compare_pos(start, end) > 0) {
        opr_pos swap = start;
        start = end;
        end = swap;
    }
    if (compare_pos(start, end) == 0) {
        selection->anchor = selection->focus = start;
        return 0;
    }
    if (o->kind == OWF_OBJ_TABLE &&
        !same_table_cell(&o->text.paras[start.para], &o->text.paras[end.para]))
        return -2;
    if (start.para == end.para) {
        remove_bytes(&o->text.paras[start.para], start.offset, end.offset);
    } else {
        remove_bytes(&o->text.paras[start.para], start.offset,
                     opr_para_length(&o->text.paras[start.para]));
        remove_bytes(&o->text.paras[end.para], 0, end.offset);
        while (end.para > start.para + 1) {
            remove_para(o, start.para + 1);
            end.para--;
        }
        if (join(o, start.para) < 0)
            return -1;
    }
    selection->anchor = selection->focus = start;
    return 0;
}

int opr_text_move(const owf_deck *deck, int slide, int obj, opr_pos *at, opr_move how)
{
    const owf_obj *o = object((owf_deck *)deck, slide, obj);
    size_t n;
    char *t;
    if (!o || !at)
        return -1;
    if (!o->text.nparas) {
        at->para = 0;
        at->offset = 0;
        return 0;
    }
    clamp_pos(o, at);
    t = opr_para_text(&o->text.paras[at->para], &n);
    if (!t)
        return -1;
    switch (how) {
    case OPR_MOVE_LEFT:
        if (at->offset > 0)
            at->offset = utf8_back(t, at->offset);
        else if (at->para > 0) {
            at->para--;
            at->offset = opr_para_length(&o->text.paras[at->para]);
        }
        break;
    case OPR_MOVE_RIGHT:
        if (at->offset < n)
            at->offset = utf8_forward(t, at->offset, n);
        else if (at->para + 1 < o->text.nparas) {
            at->para++;
            at->offset = 0;
        }
        break;
    case OPR_MOVE_PARA_START: at->offset = 0; break;
    case OPR_MOVE_PARA_END: at->offset = n; break;
    case OPR_MOVE_TEXT_START: at->para = 0; at->offset = 0; break;
    case OPR_MOVE_TEXT_END:
        at->para = o->text.nparas - 1;
        at->offset = opr_para_length(&o->text.paras[at->para]);
        break;
    }
    owf_free(t);
    return 0;
}

int opr_text_set(owf_deck *deck, int slide, int obj, const char *utf8)
{
    owf_obj *o = object(deck, slide, obj);
    opr_pos at;
    if (!o || !utf8 || make_room(deck, slide, o) < 0)
        return -1;
    /* keep the first paragraph, emptied, for its look and level */
    while (o->text.nparas > 1) {
        owf_para *q = &o->text.paras[o->text.nparas - 1];
        int j;
        for (j = 0; j < q->nruns; j++) {
            owf_free(q->runs[j].text);
            owf_free(q->runs[j].href);
        }
        owf_free(q->runs);
        owf_free(q->style);
        owf_free(q->bookmarks);
        o->text.nparas--;
    }
    remove_bytes(&o->text.paras[0], 0, opr_para_length(&o->text.paras[0]));
    at.para = 0;
    at.offset = 0;
    return opr_text_insert(deck, slide, obj, &at, utf8, strlen(utf8));
}

/* ---- Undo: text and slide-structure changes ---- */

typedef enum {
    SNAP_TEXT,
    SNAP_SLIDE,
    SNAP_MOVE,
    SNAP_NAME,
    SNAP_HIDDEN,
    SNAP_TRANSITION,
    SNAP_OBJ_BOX,
    SNAP_OBJ_ROTATION,
    SNAP_OBJ_PRESENCE,
    SNAP_OBJ_ORDER,
    SNAP_OBJ_LOOK,
    SNAP_OBJ_COLUMNS,
    SNAP_OBJ_WRAP,
    SNAP_OBJ_CROP,
    SNAP_OBJ_FLOW
} snapshot_kind;

typedef struct {
    snapshot_kind kind;
    int slide, obj;
    owf_story text;
    unsigned char *levels;
    int caplevels;
    unsigned flags;
    /* SNAP_SLIDE owns saved when saved_present is set. The saved slide and
     * the deck's slot change places on undo/redo, so no deep copy is needed. */
    owf_slide saved;
    int saved_present;
    /* The selection belonging to the saved and live states. */
    int focus, other_focus;
    /* SNAP_MOVE/SNAP_OBJ_ORDER: the next move to apply; swapped afterward. */
    int from, to;
    /* SNAP_NAME: the name belonging to the saved state. */
    char *name;
    /* SNAP_HIDDEN: the visibility belonging to the saved state. */
    int hidden;
    /* SNAP_TRANSITION: one or all slides' transition state. */
    int transition_first, transition_count;
    owf_transition *transitions;
    /* SNAP_OBJ_BOX: the object's explicit box and inheritance state. */
    int box_x, box_y, box_width, box_height;
    unsigned box_flags;
    /* SNAP_OBJ_ROTATION: the shape's rotation in tenths of a degree. */
    int rotation;
    /* SNAP_OBJ_LOOK: complete fill/stroke appearance. */
    owf_look look;
    /* SNAP_OBJ_COLUMNS: complete text-frame columns, gap and insets. */
    int text_columns, text_column_gap;
    int inset[4];
    /* SNAP_OBJ_WRAP: object exclusion kind and top/right/bottom/left gaps. */
    unsigned char wrap;
    int wrap_gap[4];
    /* SNAP_OBJ_CROP: left/top/right/bottom thousandths removed. */
    int crop[4];
    /* SNAP_OBJ_FLOW: the one or two flow identities changed atomically. */
    int flow_count, flow_slide[2], flow_obj[2], flow_story[2];
    /* SNAP_OBJ_PRESENCE: the detached object when it is absent from the slide. */
    owf_obj saved_obj;
    int saved_obj_present;
} snapshot;

#define UNDO_STEPS 16

struct opr_undo {
    snapshot undo[UNDO_STEPS], redo[UNDO_STEPS];
    int nundo, nredo;
};

static void story_free(owf_story *s)
{
    int i, j;
    for (i = 0; i < s->nparas; i++) {
        for (j = 0; j < s->paras[i].nruns; j++) {
            owf_free(s->paras[i].runs[j].text);
            owf_free(s->paras[i].runs[j].href);
        }
        owf_free(s->paras[i].runs);
        owf_free(s->paras[i].style);
        owf_free(s->paras[i].bookmarks);
    }
    owf_free(s->paras);
    memset(s, 0, sizeof *s);
}

static void slide_free(owf_slide *s)
{
    int i;
    for (i = 0; i < s->nobjs; i++)
        owf_obj_clear(&s->objs[i]);
    owf_free(s->objs);
    story_free(&s->notes);
    owf_free(s->name);
    owf_free(s->footer);
    owf_free(s->date_text);
    memset(s, 0, sizeof *s);
}

static int story_copy(owf_story *to, const owf_story *from)
{
    int i, j;
    memset(to, 0, sizeof *to);
    if (!from->nparas)
        return 0;
    to->paras = owf_calloc((size_t)from->nparas, sizeof *to->paras);
    if (!to->paras)
        return -1;
    to->capparas = from->nparas;
    for (i = 0; i < from->nparas; i++) {
        const owf_para *p = &from->paras[i];
        owf_para *q = &to->paras[i];
        *q = *p;
        q->runs = NULL;
        q->nruns = q->capruns = 0;
        q->style = p->style ? copy_text(p->style, strlen(p->style)) : NULL;
        q->bookmarks = p->bookmarks ? copy_text(p->bookmarks, strlen(p->bookmarks)) : NULL;
        to->nparas = i + 1;
        if ((p->style && !q->style) || (p->bookmarks && !q->bookmarks))
            return -1;
        if (p->nruns) {
            q->runs = owf_calloc((size_t)p->nruns, sizeof *q->runs);
            if (!q->runs)
                return -1;
            q->capruns = p->nruns;
            for (j = 0; j < p->nruns; j++) {
                q->runs[j] = p->runs[j];
                q->runs[j].text = p->runs[j].text ? copy_text(p->runs[j].text, strlen(p->runs[j].text)) : NULL;
                q->runs[j].href = p->runs[j].href ? copy_text(p->runs[j].href, strlen(p->runs[j].href)) : NULL;
                q->nruns = j + 1;
                if ((p->runs[j].text && !q->runs[j].text) || (p->runs[j].href && !q->runs[j].href))
                    return -1;
            }
        }
    }
    return 0;
}

static void snap_free(snapshot *s)
{
    if (s->kind == SNAP_TEXT) {
        story_free(&s->text);
        owf_free(s->levels);
    } else if (s->kind == SNAP_SLIDE && s->saved_present) {
        slide_free(&s->saved);
    } else if (s->kind == SNAP_NAME) {
        owf_free(s->name);
    } else if (s->kind == SNAP_TRANSITION) {
        owf_free(s->transitions);
    } else if (s->kind == SNAP_OBJ_PRESENCE && s->saved_obj_present) {
        owf_obj_clear(&s->saved_obj);
    }
    memset(s, 0, sizeof *s);
}

static int snap_take(snapshot *s, const owf_deck *deck, int slide, int obj)
{
    const owf_obj *o = object((owf_deck *)deck, slide, obj);
    memset(s, 0, sizeof *s);
    if (!o)
        return -1;
    s->kind = SNAP_TEXT;
    s->slide = slide;
    s->obj = obj;
    s->flags = o->flags;
    if (story_copy(&s->text, &o->text) < 0) {
        snap_free(s);
        return -1;
    }
    if (o->levels && o->caplevels) {
        s->levels = owf_malloc((size_t)o->caplevels);
        if (!s->levels) {
            snap_free(s);
            return -1;
        }
        memcpy(s->levels, o->levels, (size_t)o->caplevels);
        s->caplevels = o->caplevels;
    }
    return 0;
}

/* Take a slide out without freeing it: the caller owns *out. */
static int slide_detach(owf_deck *deck, int index, owf_slide *out)
{
    if (!deck || !out || index < 0 || index >= deck->nslides)
        return -1;
    *out = deck->slides[index];
    memmove(&deck->slides[index], &deck->slides[index + 1],
            (size_t)(deck->nslides - index - 1) * sizeof *deck->slides);
    deck->nslides--;
    memset(&deck->slides[deck->nslides], 0, sizeof *deck->slides);
    return 0;
}

/* Put an owned slide into a deck, transferring ownership. */
static int slide_attach(owf_deck *deck, int index, owf_slide *in)
{
    owf_slide *slot;
    if (!deck || !in || index < 0 || index > deck->nslides)
        return -1;
    slot = owf_deck_add_slide(deck, index, -1);
    if (!slot)
        return -1;
    *slot = *in;
    memset(in, 0, sizeof *in);
    return 0;
}

/* Take one ungrouped object out without freeing it. */
static int obj_detach(owf_deck *deck, int slide, int index, owf_obj *out)
{
    owf_slide *s;
    int i;
    if (!deck || !out || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (index < 0 || index >= s->nobjs)
        return -1;
    *out = s->objs[index];
    memmove(&s->objs[index], &s->objs[index + 1],
            (size_t)(s->nobjs - index - 1) * sizeof *s->objs);
    s->nobjs--;
    memset(&s->objs[s->nobjs], 0, sizeof *s->objs);
    for (i = 0; i < s->nobjs; i++)
        if (s->objs[i].parent > index)
            s->objs[i].parent--;
    return 0;
}

/* Put an owned object back at its former index, transferring ownership. */
static int obj_attach(owf_deck *deck, int slide, int index, owf_obj *in)
{
    owf_slide *s;
    owf_obj *slot;
    int i;
    if (!deck || !in || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (index < 0 || index > s->nobjs)
        return -1;
    slot = owf_slide_add(s, OWF_OBJ_OTHER);
    if (!slot)
        return -1;
    memmove(&s->objs[index + 1], &s->objs[index],
            (size_t)(s->nobjs - index - 1) * sizeof *s->objs);
    s->objs[index] = *in;
    memset(in, 0, sizeof *in);
    for (i = 0; i < s->nobjs; i++)
        if (i != index && s->objs[i].parent >= index)
            s->objs[i].parent++;
    return 0;
}

/* Move one object to a final array index and repair every group parent index.
 * Callers keep group blocks intact by choosing only their boundaries. */
static int obj_reorder(owf_deck *deck, int slide, int from, int to)
{
    owf_slide *s;
    owf_obj moved;
    int i, parent;
    if (!deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (from < 0 || from >= s->nobjs || to < 0 || to >= s->nobjs)
        return -1;
    if (from == to)
        return 0;
    moved = s->objs[from];
    if (from < to)
        memmove(&s->objs[from], &s->objs[from + 1],
                (size_t)(to - from) * sizeof *s->objs);
    else
        memmove(&s->objs[to + 1], &s->objs[to],
                (size_t)(from - to) * sizeof *s->objs);
    s->objs[to] = moved;
    for (i = 0; i < s->nobjs; i++) {
        parent = s->objs[i].parent;
        if (parent == from)
            s->objs[i].parent = to;
        else if (from < to && parent > from && parent <= to)
            s->objs[i].parent--;
        else if (to < from && parent >= to && parent < from)
            s->objs[i].parent++;
    }
    return 0;
}

/* The saved state and the deck's current state change places. */
static int snap_swap(snapshot *s, owf_deck *deck, int *focus)
{
    int f;
    if (s->kind == SNAP_TEXT) {
        owf_obj *o = object(deck, s->slide, s->obj);
        owf_story t;
        unsigned char *l;
        int cap;
        unsigned flags;
        if (!o)
            return -1;
        t = o->text;
        o->text = s->text;
        s->text = t;
        l = o->levels;
        o->levels = s->levels;
        s->levels = l;
        cap = o->caplevels;
        o->caplevels = s->caplevels;
        s->caplevels = cap;
        flags = o->flags;
        o->flags = (o->flags & ~(unsigned)OWF_OBJ_EMPTY) | (s->flags & OWF_OBJ_EMPTY);
        s->flags = flags;
        if (focus) *focus = s->slide;
        return 0;
    }
    f = s->focus;
    if (s->kind == SNAP_SLIDE) {
        if (s->saved_present) {
            if (slide_attach(deck, s->slide, &s->saved) < 0)
                return -1;
            s->saved_present = 0;
        } else {
            if (slide_detach(deck, s->slide, &s->saved) < 0)
                return -1;
            s->saved_present = 1;
        }
    } else if (s->kind == SNAP_MOVE) {
        int k;
        if (owf_deck_move_slide(deck, s->from, s->to) < 0)
            return -1;
        k = s->from;
        s->from = s->to;
        s->to = k;
    } else if (s->kind == SNAP_NAME) {
        char *name;
        if (!deck || s->slide < 0 || s->slide >= deck->nslides)
            return -1;
        name = deck->slides[s->slide].name;
        deck->slides[s->slide].name = s->name;
        s->name = name;
    } else if (s->kind == SNAP_HIDDEN) {
        int hidden;
        if (!deck || s->slide < 0 || s->slide >= deck->nslides)
            return -1;
        hidden = deck->slides[s->slide].hidden;
        deck->slides[s->slide].hidden = s->hidden;
        s->hidden = hidden;
    } else if (s->kind == SNAP_TRANSITION) {
        int i;
        if (!deck || !s->transitions || s->transition_first < 0 || s->transition_count < 1 ||
            s->transition_first + s->transition_count > deck->nslides)
            return -1;
        for (i = 0; i < s->transition_count; i++) {
            owf_transition transition = deck->slides[s->transition_first + i].transition;
            deck->slides[s->transition_first + i].transition = s->transitions[i];
            s->transitions[i] = transition;
        }
    } else if (s->kind == SNAP_OBJ_BOX) {
        owf_obj *o = object(deck, s->slide, s->obj);
        int value;
        unsigned flags;
        if (!o)
            return -1;
        value = o->x; o->x = s->box_x; s->box_x = value;
        value = o->y; o->y = s->box_y; s->box_y = value;
        value = o->width; o->width = s->box_width; s->box_width = value;
        value = o->height; o->height = s->box_height; s->box_height = value;
        flags = o->flags; o->flags = s->box_flags; s->box_flags = flags;
    } else if (s->kind == SNAP_OBJ_ROTATION) {
        owf_obj *o = object(deck, s->slide, s->obj);
        int value;
        if (!o || o->kind != OWF_OBJ_SHAPE)
            return -1;
        value = o->rotation;
        o->rotation = s->rotation;
        s->rotation = value;
    } else if (s->kind == SNAP_OBJ_PRESENCE) {
        if (s->saved_obj_present) {
            if (obj_attach(deck, s->slide, s->obj, &s->saved_obj) < 0)
                return -1;
            s->saved_obj_present = 0;
        } else {
            if (obj_detach(deck, s->slide, s->obj, &s->saved_obj) < 0)
                return -1;
            s->saved_obj_present = 1;
        }
    } else if (s->kind == SNAP_OBJ_ORDER) {
        int k;
        if (obj_reorder(deck, s->slide, s->from, s->to) < 0)
            return -1;
        k = s->from;
        s->from = s->to;
        s->to = k;
    } else if (s->kind == SNAP_OBJ_LOOK) {
        owf_obj *o = object(deck, s->slide, s->obj);
        owf_look look;
        if (!o)
            return -1;
        look = o->look;
        o->look = s->look;
        s->look = look;
    } else if (s->kind == SNAP_OBJ_COLUMNS) {
        owf_obj *o = object(deck, s->slide, s->obj);
        int value, insets[4];
        if (!o || !opr_obj_editable(deck, s->slide, s->obj))
            return -1;
        value = o->text_columns; o->text_columns = s->text_columns; s->text_columns = value;
        value = o->text_column_gap; o->text_column_gap = s->text_column_gap; s->text_column_gap = value;
        memcpy(insets, o->inset, sizeof insets);
        memcpy(o->inset, s->inset, sizeof o->inset);
        memcpy(s->inset, insets, sizeof s->inset);
    } else if (s->kind == SNAP_OBJ_WRAP) {
        owf_obj *o = object(deck, s->slide, s->obj);
        unsigned char wrap;
        int gaps[4];
        if (!o)
            return -1;
        wrap = o->wrap;
        memcpy(gaps, o->wrap_gap, sizeof gaps);
        o->wrap = s->wrap;
        memcpy(o->wrap_gap, s->wrap_gap, sizeof o->wrap_gap);
        s->wrap = wrap;
        memcpy(s->wrap_gap, gaps, sizeof s->wrap_gap);
    } else if (s->kind == SNAP_OBJ_CROP) {
        owf_obj *o = object(deck, s->slide, s->obj);
        int crop[4];
        if (!o || o->kind != OWF_OBJ_PICTURE)
            return -1;
        memcpy(crop, o->crop, sizeof crop);
        memcpy(o->crop, s->crop, sizeof o->crop);
        memcpy(s->crop, crop, sizeof s->crop);
    } else if (s->kind == SNAP_OBJ_FLOW) {
        int i;
        if (s->flow_count < 1 || s->flow_count > 2)
            return -1;
        for (i = 0; i < s->flow_count; ++i) {
            owf_obj *o = object(deck, s->flow_slide[i], s->flow_obj[i]);
            int value;
            if (!o || o->kind != OWF_OBJ_TEXT)
                return -1;
            value = o->flow_story;
            o->flow_story = s->flow_story[i];
            s->flow_story[i] = value;
        }
    } else
        return -1;
    s->focus = s->other_focus;
    s->other_focus = f;
    if (focus) *focus = f;
    return 0;
}

opr_undo *opr_undo_new(void)
{
    return owf_calloc(1, sizeof(opr_undo));
}

void opr_undo_clear(opr_undo *u)
{
    int i;
    if (!u)
        return;
    for (i = 0; i < u->nundo; i++)
        snap_free(&u->undo[i]);
    for (i = 0; i < u->nredo; i++)
        snap_free(&u->redo[i]);
    u->nundo = u->nredo = 0;
}

void opr_undo_free(opr_undo *u)
{
    opr_undo_clear(u);
    owf_free(u);
}

int opr_undo_push(opr_undo *u, const owf_deck *deck, int slide, int obj)
{
    snapshot s;
    int i;
    if (!u)
        return -1;
    if (snap_take(&s, deck, slide, obj) < 0)
        return -1;
    for (i = 0; i < u->nredo; i++)
        snap_free(&u->redo[i]);
    u->nredo = 0;
    if (u->nundo == UNDO_STEPS) {
        snap_free(&u->undo[0]);
        memmove(&u->undo[0], &u->undo[1], (UNDO_STEPS - 1) * sizeof u->undo[0]);
        u->nundo--;
    }
    u->undo[u->nundo++] = s;
    return 0;
}

static owf_obj *format_object(owf_deck *deck, int slide, int obj,
                              int first_para, int last_para)
{
    owf_obj *o = object(deck, slide, obj);
    if (!o || !opr_obj_editable(deck, slide, obj) || first_para < 0 ||
        last_para < first_para || last_para >= o->text.nparas)
        return NULL;
    return o;
}

enum selection_format_kind {
    SEL_FORMAT_FLAGS,
    SEL_FORMAT_SIZE,
    SEL_FORMAT_COLOUR,
    SEL_FORMAT_FONT
};

static owf_obj *selection_object(owf_deck *deck, int slide, int obj,
                                 const opr_selection *selection,
                                 opr_pos *start, opr_pos *end)
{
    owf_obj *o = object(deck, slide, obj);
    if (!o || !selection || !opr_obj_editable(deck, slide, obj) ||
        !o->text.nparas)
        return NULL;
    *start = selection->anchor;
    *end = selection->focus;
    clamp_pos(o, start);
    clamp_pos(o, end);
    if (compare_pos(*start, *end) > 0) {
        opr_pos swap = *start;
        *start = *end;
        *end = swap;
    }
    return compare_pos(*start, *end) < 0 ? o : NULL;
}

static int selection_run_touched(size_t run_start, size_t run_length,
                                 size_t from, size_t to)
{
    return run_length && run_start < to && run_start + run_length > from;
}

static int format_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                            const opr_selection *selection,
                            enum selection_format_kind kind,
                            unsigned long value)
{
    owf_obj *o;
    opr_pos start, end;
    int p, r, touched = 0, changed = 0, all_set = 1;
    o = selection_object(deck, slide, obj, selection, &start, &end);
    if (!u || !o)
        return -2;

    for (p = start.para; p <= end.para; ++p) {
        owf_para *para = &o->text.paras[p];
        size_t at = 0;
        size_t from = p == start.para ? start.offset : 0;
        size_t to = p == end.para ? end.offset : opr_para_length(para);
        for (r = 0; r < para->nruns; ++r) {
            size_t n = run_length(&para->runs[r]);
            if (selection_run_touched(at, n, from, to)) {
                touched++;
                if (kind == SEL_FORMAT_FLAGS) {
                    if ((para->runs[r].fmt.flags & (unsigned)value) != (unsigned)value)
                        all_set = 0;
                } else if ((kind == SEL_FORMAT_SIZE && para->runs[r].fmt.size != (int)value) ||
                           (kind == SEL_FORMAT_COLOUR && para->runs[r].fmt.colour != value) ||
                           (kind == SEL_FORMAT_FONT && para->runs[r].fmt.font != (int)value)) {
                    changed = 1;
                }
            }
            at += n;
        }
    }
    if (!touched)
        return -2;
    if (kind != SEL_FORMAT_FLAGS && !changed)
        return 0;
    if (opr_undo_push(u, deck, slide, obj) < 0)
        return -1;

    for (p = start.para; p <= end.para; ++p) {
        owf_para *para = &o->text.paras[p];
        size_t from = p == start.para ? start.offset : 0;
        size_t to = p == end.para ? end.offset : opr_para_length(para);
        int first, after;
        if (to <= from)
            continue;
        if (split_run_at(para, from, &first) < 0 ||
            split_run_at(para, to, &after) < 0)
            return -1;
        for (r = first; r < after; ++r) {
            if (kind == SEL_FORMAT_FLAGS) {
                if (all_set) para->runs[r].fmt.flags &= ~(unsigned)value;
                else para->runs[r].fmt.flags |= (unsigned)value;
            } else if (kind == SEL_FORMAT_SIZE) {
                para->runs[r].fmt.size = (int)value;
            } else if (kind == SEL_FORMAT_COLOUR) {
                para->runs[r].fmt.colour = value;
            } else {
                para->runs[r].fmt.font = (int)value;
            }
        }
    }
    return 0;
}

int opr_text_toggle_flags_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                    const opr_selection *selection, unsigned flags)
{
    const unsigned allowed = OWF_BOLD | OWF_ITALIC | OWF_UNDERLINE | OWF_STRIKE;
    if (!flags || (flags & ~allowed)) return -2;
    return format_selection(u, deck, slide, obj, selection, SEL_FORMAT_FLAGS, flags);
}

int opr_text_set_size_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                const opr_selection *selection, int twips)
{
    if (twips < 1 || twips > 20000) return -2;
    return format_selection(u, deck, slide, obj, selection, SEL_FORMAT_SIZE,
                            (unsigned long)twips);
}

int opr_text_set_colour_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                  const opr_selection *selection, unsigned long colour)
{
    if (colour > 0xFFFFFFUL && colour != OWF_COLOUR_AUTO) return -2;
    return format_selection(u, deck, slide, obj, selection, SEL_FORMAT_COLOUR, colour);
}

int opr_text_set_font_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                const opr_selection *selection, const char *family)
{
    opr_pos start, end;
    int font;
    if (!u || !deck || !deck->res || !family || !*family || strlen(family) > 255 ||
        !selection_object(deck, slide, obj, selection, &start, &end))
        return -2;
    font = owf_doc_font(deck->res, family, OWF_FONT_ANY);
    if (font < 0) return -1;
    return format_selection(u, deck, slide, obj, selection, SEL_FORMAT_FONT,
                            (unsigned long)font);
}

int opr_text_toggle_flags(opr_undo *u, owf_deck *deck, int slide, int obj,
                          int first_para, int last_para, unsigned flags)
{
    const unsigned allowed = OWF_BOLD | OWF_ITALIC | OWF_UNDERLINE | OWF_STRIKE;
    owf_obj *o = format_object(deck, slide, obj, first_para, last_para);
    int p, r, all_set = 1, runs = 0;
    if (!u || !o || !flags || (flags & ~allowed)) return -2;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r) {
            runs++;
            if ((o->text.paras[p].runs[r].fmt.flags & flags) != flags) all_set = 0;
        }
    if (!runs) return -2;
    if (opr_undo_push(u, deck, slide, obj) < 0) return -1;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r)
            if (all_set) o->text.paras[p].runs[r].fmt.flags &= ~flags;
            else o->text.paras[p].runs[r].fmt.flags |= flags;
    return 0;
}

int opr_text_set_size(opr_undo *u, owf_deck *deck, int slide, int obj,
                      int first_para, int last_para, int twips)
{
    owf_obj *o = format_object(deck, slide, obj, first_para, last_para);
    int p, r, runs = 0, changed = 0;
    if (!u || !o || twips < 1 || twips > 20000) return -2;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r) {
            runs++;
            if (o->text.paras[p].runs[r].fmt.size != twips) changed = 1;
        }
    if (!runs) return -2;
    if (!changed) return 0;
    if (opr_undo_push(u, deck, slide, obj) < 0) return -1;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r)
            o->text.paras[p].runs[r].fmt.size = twips;
    return 0;
}

int opr_text_set_colour(opr_undo *u, owf_deck *deck, int slide, int obj,
                        int first_para, int last_para, unsigned long colour)
{
    owf_obj *o = format_object(deck, slide, obj, first_para, last_para);
    int p, r, runs = 0, changed = 0;
    if (!u || !o || (colour > 0xFFFFFFUL && colour != OWF_COLOUR_AUTO)) return -2;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r) {
            runs++;
            if (o->text.paras[p].runs[r].fmt.colour != colour) changed = 1;
        }
    if (!runs) return -2;
    if (!changed) return 0;
    if (opr_undo_push(u, deck, slide, obj) < 0) return -1;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r)
            o->text.paras[p].runs[r].fmt.colour = colour;
    return 0;
}

int opr_text_set_font(opr_undo *u, owf_deck *deck, int slide, int obj,
                      int first_para, int last_para, const char *family)
{
    owf_obj *o = format_object(deck, slide, obj, first_para, last_para);
    int p, r, runs = 0, changed = 0, font;
    if (!u || !o || !deck->res || !family || !*family || strlen(family) > 255)
        return -2;
    for (p = first_para; p <= last_para; ++p)
        runs += o->text.paras[p].nruns;
    if (!runs) return -2;
    font = owf_doc_font(deck->res, family, OWF_FONT_ANY);
    if (font < 0) return -1;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r)
            if (o->text.paras[p].runs[r].fmt.font != font) changed = 1;
    if (!changed) return 0;
    if (opr_undo_push(u, deck, slide, obj) < 0) return -1;
    for (p = first_para; p <= last_para; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r)
            o->text.paras[p].runs[r].fmt.font = font;
    return 0;
}

int opr_text_set_alignment(opr_undo *u, owf_deck *deck, int slide, int obj,
                           int first_para, int last_para, owf_align alignment)
{
    owf_obj *o = format_object(deck, slide, obj, first_para, last_para);
    int p, changed = 0;
    if (!u || !o || alignment < OWF_ALIGN_LEFT || alignment > OWF_ALIGN_JUSTIFY) return -2;
    for (p = first_para; p <= last_para; ++p)
        if (o->text.paras[p].fmt.align != alignment) changed = 1;
    if (!changed) return 0;
    if (opr_undo_push(u, deck, slide, obj) < 0) return -1;
    for (p = first_para; p <= last_para; ++p)
        o->text.paras[p].fmt.align = alignment;
    return 0;
}

int opr_text_set_indents(opr_undo *u, owf_deck *deck, int slide, int obj,
                         int first_para, int last_para,
                         int left, int first, int right)
{
    owf_obj *o = format_object(deck, slide, obj, first_para, last_para);
    owf_obj resolved;
    int usable;
    int p, changed = 0;
    if (!u || !o || left < 0 || right < 0 || first < -left ||
        left > 1000000 || first > 1000000 || right > 1000000)
        return -2;
    if (owf_deck_resolve(deck, slide, obj, &resolved, NULL) < 0)
        return -2;
    usable = resolved.width - resolved.inset[0] - resolved.inset[2];
    if (usable <= 0 || left + right >= usable || left + first + right >= usable)
        return -2;
    for (p = first_para; p <= last_para; ++p)
        if (o->text.paras[p].fmt.indent_left != left ||
            o->text.paras[p].fmt.indent_first != first ||
            o->text.paras[p].fmt.indent_right != right)
            changed = 1;
    if (!changed) return 0;
    if (opr_undo_push(u, deck, slide, obj) < 0) return -1;
    for (p = first_para; p <= last_para; ++p) {
        o->text.paras[p].fmt.indent_left = left;
        o->text.paras[p].fmt.indent_first = first;
        o->text.paras[p].fmt.indent_right = right;
    }
    return 0;
}

/* Add an already performed structural change to the undo stack. */
static void structural_push(opr_undo *u, snapshot *s)
{
    int i;
    for (i = 0; i < u->nredo; i++)
        snap_free(&u->redo[i]);
    u->nredo = 0;
    if (u->nundo == UNDO_STEPS) {
        snap_free(&u->undo[0]);
        memmove(&u->undo[0], &u->undo[1], (UNDO_STEPS - 1) * sizeof u->undo[0]);
        u->nundo--;
    }
    u->undo[u->nundo++] = *s;
    memset(s, 0, sizeof *s);
}

int opr_text_replace_all(opr_undo *u, owf_deck *deck, int slide, int obj,
                         const char *needle, const char *replacement,
                         int match_case, int *replacements, opr_pos *after)
{
    snapshot before;
    opr_find_result found;
    opr_selection selection;
    opr_pos start = { 0, 0 }, last = start;
    owf_obj *o = object(deck, slide, obj);
    int count = 0, rc;
    if (replacements) *replacements = 0;
    if (after) *after = start;
    if (!u || !o || !opr_obj_editable(deck, slide, obj) || !needle || !*needle ||
        !replacement || strchr(needle, '\n') || strchr(needle, '\r'))
        return -2;
    if (snap_take(&before, deck, slide, obj) < 0)
        return -1;
    for (;;) {
        rc = opr_text_find(deck, slide, obj, start, needle, match_case, &found);
        if (rc < 0)
            goto fail;
        if (!rc || found.slide != slide || found.obj != obj ||
            (count && find_position_compare(slide, obj,
                                            found.selection.anchor.para,
                                            found.selection.anchor.offset,
                                            slide, obj, start.para,
                                            start.offset) < 0))
            break;
        selection = found.selection;
        if (opr_text_delete_selection(deck, slide, obj, &selection) < 0)
            goto fail;
        last = selection.focus;
        if (*replacement && opr_text_insert(deck, slide, obj, &last,
                                            replacement, strlen(replacement)) < 0)
            goto fail;
        start = last;
        count++;
    }
    if (count)
        structural_push(u, &before);
    else
        snap_free(&before);
    if (replacements) *replacements = count;
    if (after) *after = last;
    return 0;

fail:
    (void)snap_swap(&before, deck, NULL);
    snap_free(&before);
    return -1;
}

static int selected_before(const owf_deck *deck, const int *selected, int fallback)
{
    int s = selected ? *selected : fallback;
    if (!deck || !deck->nslides)
        return -1;
    if (s < 0) s = 0;
    if (s >= deck->nslides) s = deck->nslides - 1;
    return s;
}

int opr_slide_insert(opr_undo *u, owf_deck *deck, int at, int layout, int *selected)
{
    snapshot s;
    int before, index;
    if (!u || !deck || layout < -1 || layout >= deck->nlayouts)
        return -1;
    before = selected_before(deck, selected, at);
    index = at < 0 || at > deck->nslides ? deck->nslides : at;
    if (!owf_deck_add_slide(deck, index, layout))
        return -1;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_SLIDE;
    s.slide = index;
    s.obj = -1;
    s.focus = before;
    s.other_focus = index;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_slide_delete(opr_undo *u, owf_deck *deck, int index, int *selected)
{
    snapshot s;
    int before, after;
    if (!u || !deck || index < 0 || index >= deck->nslides)
        return -1;
    before = selected_before(deck, selected, index);
    after = index < deck->nslides - 1 ? index : deck->nslides - 2;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_SLIDE;
    s.slide = index;
    s.obj = -1;
    s.focus = before;
    s.other_focus = after;
    if (slide_detach(deck, index, &s.saved) < 0)
        return -1;
    s.saved_present = 1;
    structural_push(u, &s);
    if (selected) *selected = after;
    return 0;
}

static int slide_name_exists(const owf_deck *deck, int except, const char *name)
{
    int i;
    for (i = 0; deck && name && i < deck->nslides; i++)
        if (i != except && deck->slides[i].name && !strcmp(deck->slides[i].name, name))
            return 1;
    return 0;
}

static int duplicate_name(owf_deck *deck, int source, int copy)
{
    const char *base = deck->slides[source < copy ? source : source + 1].name;
    char name[256];
    int n;
    if (base && *base)
        snprintf(name, sizeof name, "%.220s Copy", base);
    else
        snprintf(name, sizeof name, "Slide %d Copy", source + 1);
    for (n = 2; slide_name_exists(deck, copy, name) && n < 10000; n++) {
        if (base && *base)
            snprintf(name, sizeof name, "%.210s Copy %d", base, n);
        else
            snprintf(name, sizeof name, "Slide %d Copy %d", source + 1, n);
    }
    return n < 10000 && owf_deck_set_text(&deck->slides[copy].name, name) == 0 ? 0 : -1;
}

int opr_slide_duplicate(opr_undo *u, owf_deck *deck, int index, int at, int *selected)
{
    snapshot s;
    int before, copy;
    if (!u || !deck || index < 0 || index >= deck->nslides)
        return -1;
    before = selected_before(deck, selected, index);
    copy = at < 0 || at > deck->nslides ? deck->nslides : at;
    if (!owf_deck_duplicate_slide(deck, index, copy))
        return -1;
    if (duplicate_name(deck, index, copy) < 0) {
        owf_deck_delete_slide(deck, copy);
        return -1;
    }
    memset(&s, 0, sizeof s);
    s.kind = SNAP_SLIDE;
    s.slide = copy;
    s.obj = -1;
    s.focus = before;
    s.other_focus = copy;
    structural_push(u, &s);
    if (selected) *selected = copy;
    return 0;
}

int opr_slide_move(opr_undo *u, owf_deck *deck, int from, int to, int *selected)
{
    snapshot s;
    int before, after;
    if (!u || !deck || from < 0 || from >= deck->nslides || to < 0 || to >= deck->nslides)
        return -1;
    before = selected_before(deck, selected, from);
    after = before;
    if (before == from) after = to;
    else if (from < to && before > from && before <= to) after--;
    else if (to < from && before >= to && before < from) after++;
    if (from == to) {
        if (selected) *selected = after;
        return 0;
    }
    if (owf_deck_move_slide(deck, from, to) < 0)
        return -1;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_MOVE;
    s.obj = -1;
    s.from = to;
    s.to = from;
    s.focus = before;
    s.other_focus = after;
    structural_push(u, &s);
    if (selected) *selected = after;
    return 0;
}

int opr_slide_rename(opr_undo *u, owf_deck *deck, int index, const char *name, int *selected)
{
    snapshot s;
    char *copy;
    size_t n;
    if (!u || !deck || index < 0 || index >= deck->nslides || !name)
        return -1;
    n = strlen(name);
    if (!n || n > 255 || slide_name_exists(deck, index, name))
        return -2;
    if (deck->slides[index].name && !strcmp(deck->slides[index].name, name)) {
        if (selected) *selected = index;
        return 0;
    }
    copy = copy_text(name, n);
    if (!copy)
        return -1;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_NAME;
    s.slide = index;
    s.obj = -1;
    s.name = deck->slides[index].name;
    s.focus = s.other_focus = index;
    deck->slides[index].name = copy;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_slide_set_hidden(opr_undo *u, owf_deck *deck, int index, int hidden, int *selected)
{
    snapshot s;
    if (!u || !deck || index < 0 || index >= deck->nslides)
        return -1;
    hidden = hidden != 0;
    if (deck->slides[index].hidden == hidden) {
        if (selected) *selected = index;
        return 0;
    }
    memset(&s, 0, sizeof s);
    s.kind = SNAP_HIDDEN;
    s.slide = index;
    s.obj = -1;
    s.hidden = deck->slides[index].hidden;
    s.focus = s.other_focus = index;
    deck->slides[index].hidden = hidden;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

static int transition_equal(const owf_transition *a, const owf_transition *b)
{
    return a->kind == b->kind && a->direction == b->direction && a->duration == b->duration &&
           a->advance_after == b->advance_after && a->keep == b->keep;
}

int opr_slide_set_transition(opr_undo *u, owf_deck *deck, int index, const owf_transition *transition,
                             int apply_all, int *selected)
{
    snapshot s;
    owf_transition edited;
    int first, count, i, changed = 0;
    if (!u || !deck || index < 0 || index >= deck->nslides || !transition)
        return -1;
    if (transition->kind < OWF_TRANS_NONE || transition->kind > OWF_TRANS_OTHER ||
        transition->direction > OWF_DIR_DOWN || transition->duration < 0 || transition->duration > 600000 ||
        transition->advance_after < -1 || transition->advance_after >= 86400000 ||
        (transition->kind == OWF_TRANS_OTHER &&
         (transition->keep < 0 || transition->keep >= deck->nkept ||
          deck->kept[transition->keep].kind != OWF_KEPT_ATTRIBUTES)))
        return -2;
    edited = *transition;
    if (edited.kind != OWF_TRANS_OTHER)
        edited.keep = -1;                   /* edited state no longer uses kept foreign attributes */
    first = apply_all ? 0 : index;
    count = apply_all ? deck->nslides : 1;
    for (i = 0; i < count; i++)
        if (!transition_equal(&deck->slides[first + i].transition, &edited)) {
            changed = 1;
            break;
        }
    if (!changed) {
        if (selected) *selected = index;
        return 0;
    }
    memset(&s, 0, sizeof s);
    s.transitions = owf_malloc((size_t)count * sizeof *s.transitions);
    if (!s.transitions)
        return -1;
    s.kind = SNAP_TRANSITION;
    s.slide = index;
    s.obj = -1;
    s.focus = s.other_focus = index;
    s.transition_first = first;
    s.transition_count = count;
    for (i = 0; i < count; i++) {
        s.transitions[i] = deck->slides[first + i].transition;
        deck->slides[first + i].transition = edited;
    }
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_obj_set_box(opr_undo *u, owf_deck *deck, int slide, int obj,
                    int x, int y, int width, int height)
{
    snapshot s;
    owf_obj *o = object(deck, slide, obj);
    if (!u || !o)
        return -1;
    if (x < 0 || y < 0 || width < 1 || height < 1 ||
        (long)x + width > deck->width || (long)y + height > deck->height)
        return -2;
    if (!(o->flags & OWF_OBJ_INHERIT_BOX) && o->x == x && o->y == y &&
        o->width == width && o->height == height)
        return 0;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_BOX;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    s.box_x = o->x;
    s.box_y = o->y;
    s.box_width = o->width;
    s.box_height = o->height;
    s.box_flags = o->flags;
    o->x = x;
    o->y = y;
    o->width = width;
    o->height = height;
    o->flags &= ~(unsigned)OWF_OBJ_INHERIT_BOX;
    structural_push(u, &s);
    return 0;
}

int opr_grid_snap(int value, int step, int threshold)
{
    long long lower, upper, distance_lower, distance_upper, v = value;
    if (step <= 0 || threshold < 0)
        return value;
    lower = (v / step) * step;
    if (v < 0 && v % step)
        lower -= step;
    upper = lower + step;
    distance_lower = v - lower;
    distance_upper = upper - v;
    if (distance_lower <= distance_upper && distance_lower <= threshold)
        return (int)lower;
    if (distance_upper < distance_lower && distance_upper <= threshold)
        return (int)upper;
    return value;
}

int opr_guide_snap_axis(int start, int length, const int *guides, int count,
                        int threshold, int *matched)
{
    long long best_distance = (long long)threshold + 1, best_start = start;
    int anchors[3], a, g;
    if (matched) *matched = -1;
    if (length < 0 || !guides || count < 1 || threshold < 0)
        return start;
    anchors[0] = 0;
    anchors[1] = length / 2;
    anchors[2] = length;
    for (a = 0; a < 3; ++a) {
        long long position = (long long)start + anchors[a];
        for (g = 0; g < count; ++g) {
            long long delta = (long long)guides[g] - position;
            long long distance = delta < 0 ? -delta : delta;
            long long candidate = (long long)start + delta;
            if (distance < best_distance && distance <= threshold &&
                candidate >= INT_MIN && candidate <= INT_MAX) {
                best_distance = distance;
                best_start = candidate;
                if (matched) *matched = guides[g];
            }
        }
    }
    return (int)best_start;
}

static int valid_shade(unsigned long shade)
{
    return !shade || (OWF_SHADE_SET(shade) && !(shade & ~0x1ffffffUL));
}

static int valid_look(const owf_look *look)
{
    return look && valid_shade(look->fill) && valid_shade(look->line) &&
           look->line_width >= 0 && look->line_width <= 10000 &&
           look->line_style <= OWF_LINE_SLANTED_DASH_DOT &&
           !(look->arrows & ~(OWF_ARROW_START | OWF_ARROW_END));
}

int opr_obj_insert_shape(opr_undo *u, owf_deck *deck, int slide,
                         const opr_shape_spec *spec, int *selected)
{
    snapshot s;
    owf_obj *o;
    int index;
    if (!u || !deck || !spec || slide < 0 || slide >= deck->nslides)
        return -1;
    if (spec->shape < OWF_SHAPE_RECT || spec->shape > OWF_SHAPE_ARROW ||
        spec->x < 0 || spec->y < 0 || spec->width < 1 || spec->height < 1 ||
        (long)spec->x + spec->width > deck->width ||
        (long)spec->y + spec->height > deck->height || spec->radius < 0 ||
        !valid_look(&spec->look))
        return -2;
    index = deck->slides[slide].nobjs;
    o = owf_slide_add(&deck->slides[slide], OWF_OBJ_SHAPE);
    if (!o)
        return -1;
    o->shape = spec->shape;
    o->x = spec->x;
    o->y = spec->y;
    o->width = spec->width;
    o->height = spec->height;
    o->radius = spec->radius;
    o->look = spec->look;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_PRESENCE;
    s.slide = slide;
    s.obj = index;
    s.focus = s.other_focus = slide;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_obj_insert_textbox(opr_undo *u, owf_deck *deck, int slide,
                           const opr_textbox_spec *spec, int *selected)
{
    snapshot s;
    owf_obj *o;
    owf_para *p;
    owf_charfmt chars;
    const owf_textlevel *level;
    const owf_master *master;
    int index, i;
    if (!u || !deck || !spec || slide < 0 || slide >= deck->nslides)
        return -1;
    if (spec->x < 0 || spec->y < 0 || spec->width < 1 || spec->height < 1 ||
        (long)spec->x + spec->width > deck->width ||
        (long)spec->y + spec->height > deck->height ||
        spec->valign > OWF_VALIGN_BOTTOM || !valid_look(&spec->look))
        return -2;
    for (i = 0; i < 4; i++)
        if (spec->inset[i] < 0)
            return -2;
    if ((long)spec->inset[0] + spec->inset[2] >= spec->width ||
        (long)spec->inset[1] + spec->inset[3] >= spec->height)
        return -2;
    index = deck->slides[slide].nobjs;
    o = owf_slide_add(&deck->slides[slide], OWF_OBJ_TEXT);
    if (!o)
        return -1;
    o->x = spec->x;
    o->y = spec->y;
    o->width = spec->width;
    o->height = spec->height;
    o->look = spec->look;
    o->valign = spec->valign;
    memcpy(o->inset, spec->inset, sizeof o->inset);
    master = owf_deck_slide_master(deck, slide);
    p = owf_deck_add_para(deck, master, o, 0);
    if (!p) {
        owf_slide_delete(&deck->slides[slide], index);
        return -1;
    }
    level = owf_deck_para_level(deck, master, o, 0);
    if (level)
        chars = level->chars;
    else
        owf_charfmt_init(&chars);
    if (spec->text && *spec->text &&
        owf_para_add_text(p, &chars, spec->text, strlen(spec->text)) < 0) {
        owf_slide_delete(&deck->slides[slide], index);
        return -1;
    }
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_PRESENCE;
    s.slide = slide;
    s.obj = index;
    s.focus = s.other_focus = slide;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_obj_insert_table(opr_undo *u, owf_deck *deck, int slide,
                         const opr_table_spec *spec, int *selected)
{
    snapshot s;
    owf_obj *o;
    const owf_master *master;
    int index, row, col, remainder;
    if (!u || !deck || !spec || slide < 0 || slide >= deck->nslides)
        return -1;
    if (spec->x < 0 || spec->y < 0 || spec->width < 1 || spec->height < 1 ||
        (long)spec->x + spec->width > deck->width ||
        (long)spec->y + spec->height > deck->height ||
        spec->rows < 1 || spec->rows > 32 ||
        spec->columns < 1 || spec->columns > 32)
        return -2;
    index = deck->slides[slide].nobjs;
    o = owf_slide_add(&deck->slides[slide], OWF_OBJ_TABLE);
    if (!o)
        return -1;
    o->x = spec->x;
    o->y = spec->y;
    o->width = spec->width;
    o->height = spec->height;
    o->ncols = spec->columns;
    o->col_widths = owf_calloc((size_t)o->ncols, sizeof *o->col_widths);
    if (!o->col_widths) {
        owf_slide_delete(&deck->slides[slide], index);
        return -1;
    }
    remainder = spec->width;
    for (col = 0; col < o->ncols; ++col) {
        o->col_widths[col] = remainder / (o->ncols - col);
        remainder -= o->col_widths[col];
    }
    master = owf_deck_slide_master(deck, slide);
    for (row = 0; row < spec->rows; ++row)
        for (col = 0; col < spec->columns; ++col) {
            owf_para *p = owf_deck_add_para(deck, master, o, 0);
            if (!p) {
                owf_slide_delete(&deck->slides[slide], index);
                return -1;
            }
            p->table_id = 0;
            p->table_row = row;
            p->table_col = col;
            p->table_cols = spec->columns;
            p->fmt.borders = OWF_BORDER_TOP | OWF_BORDER_LEFT |
                             OWF_BORDER_BOTTOM | OWF_BORDER_RIGHT;
            p->fmt.border_colour = OWF_SHADE(0x808080);
            if (spec->header_row && row == 0)
                p->cell_shading = OWF_SHADE(0xd9e2f3);
        }
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_PRESENCE;
    s.slide = slide;
    s.obj = index;
    s.focus = s.other_focus = slide;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_obj_insert_picture(opr_undo *u, owf_deck *deck, int slide,
                           const opr_picture_spec *spec, int *selected)
{
    snapshot s;
    owf_obj *o;
    int image, index, i;
    if (!u || !deck || !deck->res || !spec || slide < 0 || slide >= deck->nslides)
        return -1;
    if (spec->x < 0 || spec->y < 0 || spec->width < 1 || spec->height < 1 ||
        (long)spec->x + spec->width > deck->width ||
        (long)spec->y + spec->height > deck->height ||
        !spec->data || !spec->length || spec->natural_width < 1 ||
        spec->natural_height < 1 || !spec->name || !*spec->name ||
        !spec->mime || !*spec->mime)
        return -2;
    for (i = 0; i < 4; i++)
        if (spec->crop[i] < 0 || spec->crop[i] >= 1000)
            return -2;
    if (spec->crop[0] + spec->crop[2] >= 1000 ||
        spec->crop[1] + spec->crop[3] >= 1000)
        return -2;
    index = deck->slides[slide].nobjs;
    o = owf_slide_add(&deck->slides[slide], OWF_OBJ_PICTURE);
    if (!o)
        return -1;
    o->x = spec->x;
    o->y = spec->y;
    o->width = spec->width;
    o->height = spec->height;
    memcpy(o->crop, spec->crop, sizeof o->crop);
    if (owf_deck_set_text(&o->name, spec->name) < 0 ||
        owf_deck_set_text(&o->alt, spec->alt) < 0) {
        owf_slide_delete(&deck->slides[slide], index);
        return -1;
    }
    image = owf_doc_add_image(deck->res, spec->name, spec->mime,
                              spec->data, spec->length,
                              spec->natural_width, spec->natural_height, spec->alt);
    if (image < 0) {
        owf_slide_delete(&deck->slides[slide], index);
        return -1;
    }
    o->image = image;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_PRESENCE;
    s.slide = slide;
    s.obj = index;
    s.focus = s.other_focus = slide;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_obj_duplicate(opr_undo *u, owf_deck *deck, int slide, int obj,
                      int offset_x, int offset_y, int *selected)
{
    snapshot s;
    owf_obj copy, resolved, *source, *added;
    int i, index;
    long long x, y;
    if (!u || !deck || slide < 0 || slide >= deck->nslides ||
        !(source = object(deck, slide, obj)))
        return -1;
    if (source->kind == OWF_OBJ_GROUP || source->parent >= 0)
        return -2;
    for (i = obj + 1; i < deck->slides[slide].nobjs; i++)
        if (deck->slides[slide].objs[i].parent == obj)
            return -2;
    if (owf_deck_resolve(deck, slide, obj, &resolved, NULL) < 0 ||
        resolved.width < 1 || resolved.height < 1 ||
        resolved.width > deck->width || resolved.height > deck->height)
        return -2;
    memset(&copy, 0, sizeof copy);
    if (owf_obj_copy(&copy, source) < 0)
        return -1;
    x = (long long)resolved.x + offset_x;
    y = (long long)resolved.y + offset_y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((long)x + resolved.width > deck->width) x = deck->width - resolved.width;
    if ((long)y + resolved.height > deck->height) y = deck->height - resolved.height;
    copy.x = (int)x;
    copy.y = (int)y;
    copy.width = resolved.width;
    copy.height = resolved.height;
    copy.flags &= ~(unsigned)OWF_OBJ_INHERIT_BOX;
    copy.parent = -1;
    index = deck->slides[slide].nobjs;
    added = owf_slide_add(&deck->slides[slide], copy.kind);
    if (!added) {
        owf_obj_clear(&copy);
        return -1;
    }
    *added = copy;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_PRESENCE;
    s.slide = slide;
    s.obj = index;
    s.focus = s.other_focus = slide;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

static int paste_font(owf_doc *to, const owf_doc *from, int font)
{
    if (font < 0)
        return -1;
    if (!to || !from || font >= from->nfonts || !from->fonts[font].name)
        return -2;
    return owf_doc_font(to, from->fonts[font].name, from->fonts[font].kind);
}

static int paste_image(owf_doc *to, const owf_doc *from, int image)
{
    const owf_image *im;
    if (!to || !from || image < 0 || image >= from->nimages)
        return -2;
    im = &from->images[image];
    if (!im->data || !im->length)
        return -2;
    return owf_doc_add_image(to, im->name, im->mime, im->data, im->length,
                             im->width, im->height, im->alt);
}

static int paste_object_resources(owf_obj *o, owf_doc *to, const owf_doc *from)
{
    int p, r, i, mapped;
    if (!o || !to || !from)
        return -2;
    if (o->kind == OWF_OBJ_PICTURE && o->image >= 0) {
        mapped = paste_image(to, from, o->image);
        if (mapped < 0) return mapped;
        o->image = mapped;
    }
    for (p = 0; p < o->text.nparas; ++p)
        for (r = 0; r < o->text.paras[p].nruns; ++r) {
            owf_run *run = &o->text.paras[p].runs[r];
            if (run->fmt.font >= 0) {
                mapped = paste_font(to, from, run->fmt.font);
                if (mapped < 0) return mapped;
                run->fmt.font = mapped;
            }
            if (run->kind == OWF_RUN_IMAGE) {
                mapped = paste_image(to, from, run->image);
                if (mapped < 0) return mapped;
                run->image = mapped;
            } else if (run->kind == OWF_RUN_FRAME) {
                return -2;
            }
        }
    if (o->list)
        for (i = 0; i < OWF_TEXT_LEVELS; ++i) {
            if (o->list[i].chars.font >= 0) {
                mapped = paste_font(to, from, o->list[i].chars.font);
                if (mapped < 0) return mapped;
                o->list[i].chars.font = mapped;
            }
            if (o->list[i].bullet_font >= 0) {
                mapped = paste_font(to, from, o->list[i].bullet_font);
                if (mapped < 0) return mapped;
                o->list[i].bullet_font = mapped;
            }
        }
    return 0;
}

int opr_obj_paste(opr_undo *u, owf_deck *deck, int slide,
                  const owf_deck *source_deck, int source_slide, int source_obj,
                  int offset_x, int offset_y, int *selected)
{
    snapshot s;
    owf_obj copy, resolved, *added;
    const owf_obj *source;
    long long x, y;
    int index, rc;
    if (!u || !deck || !deck->res || slide < 0 || slide >= deck->nslides ||
        !source_deck || !source_deck->res || source_slide < 0 ||
        source_slide >= source_deck->nslides || source_obj < 0 ||
        source_obj >= source_deck->slides[source_slide].nobjs)
        return -1;
    source = &source_deck->slides[source_slide].objs[source_obj];
    if (source->kind == OWF_OBJ_GROUP || source->kind == OWF_OBJ_CHART ||
        source->parent >= 0 || source->keep >= 0)
        return -2;
    if (owf_deck_resolve(source_deck, source_slide, source_obj, &resolved, NULL) < 0 ||
        resolved.width < 1 || resolved.height < 1 ||
        resolved.width > deck->width || resolved.height > deck->height)
        return -2;
    memset(&copy, 0, sizeof copy);
    if (owf_obj_copy(&copy, source) < 0)
        return -1;
    rc = paste_object_resources(&copy, deck->res, source_deck->res);
    if (rc < 0) {
        owf_obj_clear(&copy);
        return rc;
    }
    x = (long long)resolved.x + offset_x;
    y = (long long)resolved.y + offset_y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + resolved.width > deck->width) x = deck->width - resolved.width;
    if (y + resolved.height > deck->height) y = deck->height - resolved.height;
    copy.x = (int)x;
    copy.y = (int)y;
    copy.width = resolved.width;
    copy.height = resolved.height;
    copy.flags &= ~(unsigned)OWF_OBJ_INHERIT_BOX;
    copy.parent = -1;
    copy.flow_story = -1;
    free(copy.flow_next);
    copy.flow_next = NULL;
    index = deck->slides[slide].nobjs;
    added = owf_slide_add(&deck->slides[slide], copy.kind);
    if (!added) {
        owf_obj_clear(&copy);
        return -1;
    }
    *added = copy;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_PRESENCE;
    s.slide = slide;
    s.obj = index;
    s.focus = s.other_focus = slide;
    structural_push(u, &s);
    if (selected) *selected = index;
    return 0;
}

int opr_obj_delete(opr_undo *u, owf_deck *deck, int slide, int obj)
{
    snapshot s;
    int i;
    owf_obj *o = object(deck, slide, obj);
    if (!u || !o)
        return -1;
    if (o->kind == OWF_OBJ_GROUP)
        return -2;
    for (i = obj + 1; i < deck->slides[slide].nobjs; i++)
        if (deck->slides[slide].objs[i].parent == obj)
            return -2;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_PRESENCE;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    if (obj_detach(deck, slide, obj, &s.saved_obj) < 0)
        return -1;
    s.saved_obj_present = 1;
    structural_push(u, &s);
    return 0;
}

int opr_obj_arrange(opr_undo *u, owf_deck *deck, int slide, int obj,
                    opr_arrange how, int *selected)
{
    snapshot s;
    owf_slide *sl;
    owf_obj *o;
    int i, target = obj;
    if (!u || !deck || slide < 0 || slide >= deck->nslides ||
        obj < 0 || obj >= deck->slides[slide].nobjs)
        return -1;
    if (how < OPR_ARRANGE_FRONT || how > OPR_ARRANGE_BACK)
        return -2;
    sl = &deck->slides[slide];
    o = &sl->objs[obj];
    if (o->parent >= 0 || o->kind == OWF_OBJ_GROUP)
        return -2;
    if (how == OPR_ARRANGE_FRONT) {
        target = sl->nobjs - 1;
    } else if (how == OPR_ARRANGE_BACK) {
        target = 0;
    } else if (how == OPR_ARRANGE_FORWARD) {
        int next = -1;
        for (i = obj + 1; i < sl->nobjs; i++)
            if (sl->objs[i].parent < 0) {
                next = i;
                break;
            }
        if (next >= 0) {
            for (i = next + 1; i < sl->nobjs && sl->objs[i].parent >= 0; i++)
                ;
            target = i - 1;
        }
    } else {
        for (i = obj - 1; i >= 0; i--)
            if (sl->objs[i].parent < 0) {
                target = i;
                break;
            }
    }
    if (target == obj) {
        if (selected) *selected = obj;
        return 0;
    }
    if (obj_reorder(deck, slide, obj, target) < 0)
        return -1;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_ORDER;
    s.slide = slide;
    s.obj = target;
    s.from = target;
    s.to = obj;
    s.focus = s.other_focus = slide;
    structural_push(u, &s);
    if (selected) *selected = target;
    return 0;
}

int opr_obj_align(opr_undo *u, owf_deck *deck, int slide, int obj,
                  opr_align how)
{
    owf_obj resolved, *o;
    int x, y;
    if (!u || !deck || slide < 0 || slide >= deck->nslides ||
        !(o = object(deck, slide, obj)))
        return -1;
    if (how < OPR_ALIGN_LEFT || how > OPR_ALIGN_BOTTOM ||
        o->parent >= 0 || o->kind == OWF_OBJ_GROUP)
        return -2;
    if (owf_deck_resolve(deck, slide, obj, &resolved, NULL) < 0 ||
        resolved.width < 1 || resolved.height < 1 ||
        resolved.width > deck->width || resolved.height > deck->height)
        return -2;
    x = resolved.x;
    y = resolved.y;
    if (how == OPR_ALIGN_LEFT)
        x = 0;
    else if (how == OPR_ALIGN_CENTRE)
        x = (deck->width - resolved.width) / 2;
    else if (how == OPR_ALIGN_RIGHT)
        x = deck->width - resolved.width;
    else if (how == OPR_ALIGN_TOP)
        y = 0;
    else if (how == OPR_ALIGN_MIDDLE)
        y = (deck->height - resolved.height) / 2;
    else
        y = deck->height - resolved.height;
    return opr_obj_set_box(u, deck, slide, obj, x, y,
                           resolved.width, resolved.height);
}

int opr_obj_set_look(opr_undo *u, owf_deck *deck, int slide, int obj,
                     const owf_look *look)
{
    snapshot s;
    owf_obj *o = object(deck, slide, obj);
    if (!u || !o || !look)
        return -1;
    if (o->parent >= 0 || (o->kind != OWF_OBJ_TEXT && o->kind != OWF_OBJ_SHAPE) ||
        !valid_look(look))
        return -2;
    if (!memcmp(&o->look, look, sizeof *look))
        return 0;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_LOOK;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    s.look = o->look;
    o->look = *look;
    structural_push(u, &s);
    return 0;
}

int opr_obj_set_text_frame(opr_undo *u, owf_deck *deck, int slide, int obj,
                           int columns, int gap, const int inset[4])
{
    snapshot s;
    owf_obj *o = object(deck, slide, obj);
    owf_obj resolved;
    int stored_columns = columns == 1 ? 0 : columns;
    int stored_gap = columns == 1 ? 0 : gap;
    int side;
    if (!u || !o || !inset)
        return -1;
    for (side = 0; side < 4; ++side)
        if (inset[side] < 0 || inset[side] > 20000)
            return -2;
    if (!opr_obj_editable(deck, slide, obj) || o->parent >= 0 ||
        columns < 1 || columns > 16 || gap < 0 || gap > 20000 ||
        owf_deck_resolve(deck, slide, obj, &resolved, NULL) < 0 ||
        (long)inset[0] + inset[2] >= resolved.width ||
        (long)inset[1] + inset[3] >= resolved.height ||
        (long)gap * (columns - 1) >= resolved.width - inset[0] - inset[2])
        return -2;
    if (o->text_columns == stored_columns && o->text_column_gap == stored_gap &&
        !memcmp(o->inset, inset, sizeof o->inset))
        return 0;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_COLUMNS;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    s.text_columns = o->text_columns;
    s.text_column_gap = o->text_column_gap;
    memcpy(s.inset, o->inset, sizeof s.inset);
    o->text_columns = stored_columns;
    o->text_column_gap = stored_gap;
    memcpy(o->inset, inset, sizeof o->inset);
    structural_push(u, &s);
    return 0;
}

int opr_obj_set_text_columns(opr_undo *u, owf_deck *deck, int slide, int obj,
                             int columns, int gap)
{
    owf_obj *o = object(deck, slide, obj);
    return o ? opr_obj_set_text_frame(u, deck, slide, obj, columns, gap, o->inset) : -1;
}

int opr_obj_set_wrap(opr_undo *u, owf_deck *deck, int slide, int obj,
                     owf_frame_wrap wrap, const int gap[4])
{
    snapshot s;
    owf_obj *o = object(deck, slide, obj);
    int stored[4] = {0, 0, 0, 0};
    int side;
    if (!u || !o)
        return -1;
    if (o->parent >= 0 || (o->flags & OWF_OBJ_HIDDEN) || o->kind == OWF_OBJ_GROUP ||
        (o->kind == OWF_OBJ_SHAPE && o->shape == OWF_SHAPE_LINE) ||
        (wrap != OWF_WRAP_NONE && wrap != OWF_WRAP_SQUARE &&
         wrap != OWF_WRAP_TIGHT && wrap != OWF_WRAP_TOP_BOTTOM))
        return -2;
    if (wrap != OWF_WRAP_NONE) {
        if (!gap) return -2;
        for (side = 0; side < 4; ++side) {
            if (gap[side] < 0 || gap[side] > 20000) return -2;
            stored[side] = gap[side];
        }
    }
    if (o->wrap == wrap && !memcmp(o->wrap_gap, stored, sizeof stored))
        return 0;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_WRAP;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    s.wrap = o->wrap;
    memcpy(s.wrap_gap, o->wrap_gap, sizeof s.wrap_gap);
    o->wrap = (unsigned char)wrap;
    memcpy(o->wrap_gap, stored, sizeof o->wrap_gap);
    structural_push(u, &s);
    return 0;
}

int opr_obj_set_picture_crop(opr_undo *u, owf_deck *deck, int slide, int obj,
                             const int crop[4])
{
    snapshot s;
    owf_obj *o = object(deck, slide, obj);
    int side;
    if (!u || !o || !crop)
        return -1;
    if (o->parent >= 0 || o->kind != OWF_OBJ_PICTURE)
        return -2;
    for (side = 0; side < 4; ++side)
        if (crop[side] < 0 || crop[side] >= 1000)
            return -2;
    if (crop[0] + crop[2] >= 1000 || crop[1] + crop[3] >= 1000)
        return -2;
    if (!memcmp(o->crop, crop, sizeof o->crop))
        return 0;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_CROP;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    memcpy(s.crop, o->crop, sizeof s.crop);
    memcpy(o->crop, crop, sizeof o->crop);
    structural_push(u, &s);
    return 0;
}

static int story_has_content(const owf_story *story)
{
    int p, r;
    for (p = 0; story && p < story->nparas; ++p)
        for (r = 0; r < story->paras[p].nruns; ++r) {
            const owf_run *run = &story->paras[p].runs[r];
            if (run->kind != OWF_RUN_TEXT || (run->text && *run->text))
                return 1;
        }
    return 0;
}

int opr_obj_link_text_frame(opr_undo *u, owf_deck *deck, int slide, int obj,
                            int target_slide, int target_obj)
{
    snapshot s;
    owf_obj *source = object(deck, slide, obj);
    owf_obj *target = object(deck, target_slide, target_obj);
    int page, index, id, max_id = -1, changes = 0;
    int next_slide = -1, next_obj = -1;
    if (!u || !deck || !source || !target)
        return -1;
    if (source == target || source->kind != OWF_OBJ_TEXT || target->kind != OWF_OBJ_TEXT ||
        source->parent >= 0 || target->parent >= 0 ||
        target_slide < slide || (target_slide == slide && target_obj <= obj) ||
        target->flow_story >= 0 || story_has_content(&target->text))
        return -2;
    if (source->flow_story >= 0) {
        for (page = slide; page < deck->nslides; ++page) {
            int first = page == slide ? obj + 1 : 0;
            for (index = first; index < deck->slides[page].nobjs; ++index)
                if (deck->slides[page].objs[index].kind == OWF_OBJ_TEXT &&
                    deck->slides[page].objs[index].flow_story == source->flow_story) {
                    next_slide = page;
                    next_obj = index;
                    break;
                }
            if (next_slide >= 0) break;
        }
        if (next_slide >= 0 &&
            (target_slide > next_slide ||
             (target_slide == next_slide && target_obj > next_obj)))
            return -2;                      /* insert before the source's current successor */
        id = source->flow_story;
    } else {
        for (page = 0; page < deck->nslides; ++page)
            for (index = 0; index < deck->slides[page].nobjs; ++index)
                if (deck->slides[page].objs[index].flow_story > max_id)
                    max_id = deck->slides[page].objs[index].flow_story;
        if (max_id == 0x7fffffff)
            return -2;
        id = max_id + 1;
    }
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_FLOW;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    if (source->flow_story < 0) {
        s.flow_slide[changes] = slide;
        s.flow_obj[changes] = obj;
        s.flow_story[changes++] = source->flow_story;
        source->flow_story = id;
    }
    s.flow_slide[changes] = target_slide;
    s.flow_obj[changes] = target_obj;
    s.flow_story[changes++] = target->flow_story;
    target->flow_story = id;
    s.flow_count = changes;
    structural_push(u, &s);
    return 0;
}

int opr_obj_unlink_text_frame(opr_undo *u, owf_deck *deck, int slide, int obj)
{
    snapshot s;
    owf_obj *selected = object(deck, slide, obj), *other = NULL;
    int page, index, count = 0, first_slide = -1, first_obj = -1;
    int other_slide = -1, other_obj = -1;
    if (!u || !deck || !selected)
        return -1;
    if (selected->kind != OWF_OBJ_TEXT || selected->parent >= 0 || selected->flow_story < 0)
        return -2;
    for (page = 0; page < deck->nslides; ++page)
        for (index = 0; index < deck->slides[page].nobjs; ++index) {
            owf_obj *candidate = &deck->slides[page].objs[index];
            if (candidate->kind != OWF_OBJ_TEXT || candidate->flow_story != selected->flow_story)
                continue;
            if (!count) {
                first_slide = page;
                first_obj = index;
            }
            count++;
            if (candidate != selected) {
                other = candidate;
                other_slide = page;
                other_obj = index;
            }
        }
    if (count < 2 || (first_slide == slide && first_obj == obj))
        return -2;                          /* the story-owning first frame stays attached */
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_FLOW;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    s.flow_slide[0] = slide;
    s.flow_obj[0] = obj;
    s.flow_story[0] = selected->flow_story;
    s.flow_count = 1;
    selected->flow_story = -1;
    if (count == 2 && other) {
        s.flow_slide[1] = other_slide;
        s.flow_obj[1] = other_obj;
        s.flow_story[1] = other->flow_story;
        s.flow_count = 2;
        other->flow_story = -1;             /* one frame alone is not a flow */
    }
    structural_push(u, &s);
    return 0;
}

int opr_obj_set_rotation(opr_undo *u, owf_deck *deck, int slide, int obj,
                         int rotation_tenths)
{
    snapshot s;
    owf_obj *o = object(deck, slide, obj);
    if (!u || !o)
        return -1;
    if (o->kind != OWF_OBJ_SHAPE)
        return -2;
    rotation_tenths %= 3600;
    if (rotation_tenths < 0)
        rotation_tenths += 3600;
    if (o->rotation == rotation_tenths)
        return 0;
    memset(&s, 0, sizeof s);
    s.kind = SNAP_OBJ_ROTATION;
    s.slide = slide;
    s.obj = obj;
    s.focus = s.other_focus = slide;
    s.rotation = o->rotation;
    o->rotation = rotation_tenths;
    structural_push(u, &s);
    return 0;
}

int opr_undo_can(const opr_undo *u)
{
    return u && u->nundo > 0;
}

int opr_redo_can(const opr_undo *u)
{
    return u && u->nredo > 0;
}

/* One step back or forward: the object it changed. */
static int step(snapshot *from, int *nfrom, snapshot *to, int *nto, owf_deck *deck, int *slide, int *obj)
{
    snapshot *s;
    int focus;
    if (!*nfrom)
        return -1;
    s = &from[*nfrom - 1];
    if (snap_swap(s, deck, &focus) < 0)
        return -1;
    if (slide) *slide = focus;
    if (obj) {
        if (s->kind == SNAP_TEXT || s->kind == SNAP_OBJ_BOX || s->kind == SNAP_OBJ_ROTATION ||
            s->kind == SNAP_OBJ_LOOK || s->kind == SNAP_OBJ_COLUMNS || s->kind == SNAP_OBJ_WRAP ||
            s->kind == SNAP_OBJ_CROP)
            *obj = s->obj;
        else if (s->kind == SNAP_OBJ_ORDER)
            *obj = s->from;
        else if (s->kind == SNAP_OBJ_PRESENCE && !s->saved_obj_present)
            *obj = s->obj;
        else
            *obj = -1;
    }
    if (*nto == UNDO_STEPS) {
        snap_free(&to[0]);
        memmove(&to[0], &to[1], (UNDO_STEPS - 1) * sizeof to[0]);
        (*nto)--;
    }
    to[(*nto)++] = *s;
    memset(s, 0, sizeof *s);
    (*nfrom)--;
    return 0;
}

int opr_undo_undo(opr_undo *u, owf_deck *deck, int *slide, int *obj)
{
    return u ? step(u->undo, &u->nundo, u->redo, &u->nredo, deck, slide, obj) : -1;
}

int opr_undo_redo(opr_undo *u, owf_deck *deck, int *slide, int *obj)
{
    return u ? step(u->redo, &u->nredo, u->undo, &u->nundo, deck, slide, obj) : -1;
}

/* ---- The caret and the pointer, through the laid-out slide ---- */

int opr_slide_frame_for(const opr_slide *s, int obj)
{
    const owf_slide *sl;
    int i;
    if (!s || s->slide < 0 || s->slide >= s->deck->nslides)
        return -1;
    sl = &s->deck->slides[s->slide];
    if (obj < 0 || obj >= sl->nobjs)
        return -1;
    for (i = 0; i < s->nframes; i++)
        if (!s->frames[i].from_master && s->frames[i].obj == &sl->objs[obj])
            return i;
    return -1;
}

int opr_frame_obj(const opr_slide *s, int frame)
{
    const owf_slide *sl;
    const owf_obj *o;
    if (!s || frame < 0 || frame >= s->nframes || s->frames[frame].from_master || !s->frames[frame].obj)
        return -1;
    sl = &s->deck->slides[s->slide];
    o = s->frames[frame].obj;
    if (o < sl->objs || o >= sl->objs + sl->nobjs)
        return -1;
    return (int)(o - sl->objs);
}

/* A paragraph's block in a frame, and the other way. */
static int para_block(const opr_slide *s, const opr_frame *f, int para, ol_id *block)
{
    const ol_node *frame = ol_node_by_id(s->doc, f->node);
    ol_id id;
    int k = 0;
    for (id = f->first; frame && id < f->end; id++) {
        const ol_node *n = ol_node_by_id(s->doc, id);
        if (!n || ol_node_parent(n) != frame)
            continue;
        if (ol_node_role(n) != OL_ROLE_PARAGRAPH && ol_node_role(n) != OL_ROLE_HEADING)
            continue;
        if (k == para) {
            *block = id;
            return 1;
        }
        k++;
    }
    return 0;
}

static int block_para(const opr_slide *s, const opr_frame *f, ol_id block)
{
    const ol_node *frame = ol_node_by_id(s->doc, f->node);
    ol_id id;
    int k = 0;
    for (id = f->first; frame && id < f->end && id <= block; id++) {
        const ol_node *n = ol_node_by_id(s->doc, id);
        if (!n || ol_node_parent(n) != frame)
            continue;
        if (ol_node_role(n) != OL_ROLE_PARAGRAPH && ol_node_role(n) != OL_ROLE_HEADING)
            continue;
        if (id == block)
            return k;
        k++;
    }
    return -1;
}

/* The runs opr_layout.c puts before a paragraph's own: its bullet and tab. */
static int prefix_runs(const opr_slide *s, const owf_obj *o, int para)
{
    const owf_master *m = owf_deck_slide_master(s->deck, s->slide);
    const owf_textlevel *lv = owf_deck_para_level(s->deck, m, o, para);
    return owf_obj_level(o, para) > 0 && lv && lv->bullet ? 2 : 0;
}

static int to_text(const opr_slide *s, int frame, opr_pos at, ol_text_position *tp, ol_id *block)
{
    const opr_frame *f;
    const owf_obj *o;
    const owf_para *p;
    size_t cum = 0, last_len = 0;
    int j, k, last = -1;
    if (!s || frame < 0 || frame >= s->nframes)
        return 0;
    f = &s->frames[frame];
    o = f->obj;
    if (!o || at.para < 0 || at.para >= o->text.nparas || !para_block(s, f, at.para, block))
        return 0;
    p = &o->text.paras[at.para];
    k = prefix_runs(s, o, at.para);
    for (j = 0; j < p->nruns; j++) {
        size_t len = run_length(&p->runs[j]);
        if (at.offset < cum + len) {
            tp->node_id = *block + 1 + (ol_id)k + (ol_id)j;
            tp->offset = at.offset - cum;
            return 1;
        }
        if (len) {
            last = j;
            last_len = len;
        }
        cum += len;
    }
    if (last >= 0) {
        tp->node_id = *block + 1 + (ol_id)k + (ol_id)last;
        tp->offset = last_len;
    } else if (k) {
        tp->node_id = *block + 2;    /* after the bullet's tab */
        tp->offset = 1;
    } else {
        tp->node_id = *block + 1;    /* the empty paragraph's run */
        tp->offset = 0;
    }
    return 1;
}

static int from_text(const opr_slide *s, int frame, const ol_text_position *tp, opr_pos *at)
{
    const opr_frame *f = &s->frames[frame];
    const owf_obj *o = f->obj;
    const ol_node *n = ol_node_by_id(s->doc, tp->node_id), *b;
    const owf_para *p;
    ol_id block;
    size_t cum = 0;
    int para, j, k, run;
    if (!n || !o || !(b = ol_node_parent(n)))
        return 0;
    block = ol_node_id(b);
    para = block_para(s, f, block);
    if (para < 0 || para >= o->text.nparas)
        return 0;
    p = &o->text.paras[para];
    k = prefix_runs(s, o, para);
    run = (int)(tp->node_id - block) - 1 - k;
    at->para = para;
    if (run < 0) {
        at->offset = 0;
        return 1;
    }
    for (j = 0; j < p->nruns && j < run; j++)
        cum += run_length(&p->runs[j]);
    if (run < p->nruns) {
        size_t len = run_length(&p->runs[run]);
        cum += tp->offset < len ? tp->offset : len;
    }
    at->offset = cum;
    return 1;
}

int opr_caret_box(const opr_slide *s, int frame, opr_pos at, ol_measure_text_fn measure, void *userdata, ol_rect *box)
{
    ol_text_position tp;
    ol_id block;
    int page;
    if (!measure) {
        measure = opr_measure;
        userdata = (void *)(s ? s->deck : NULL);
    }
    if (!to_text(s, frame, at, &tp, &block))
        return 0;
    if (ol_caret_box(s->doc, &tp, measure, userdata, &page, box) && page == 0)
        return 1;
    {
        /* not laid out as text (an empty line): the paragraph's own box */
        const ol_node *b = ol_node_by_id(s->doc, block);
        ol_rect r = ol_node_bounds(b);
        const ol_style *st = ol_node_style(b);
        box->x = r.x + st->padding_left + (r.width > 0 ? 0 : 0);
        box->y = r.y;
        box->width = 0;
        box->height = r.height > 0 ? r.height : st->font_size + st->font_size / 5;
        return 1;
    }
}

int opr_hit_frame(const opr_slide *s, int frame, ol_unit x, ol_unit y, ol_measure_text_fn measure, void *userdata,
                  opr_pos *at)
{
    size_t i, count;
    const ol_display_op *best = NULL;
    ol_unit best_dy = 0, best_dx = 0;
    if (!s || frame < 0 || frame >= s->nframes || !s->frames[frame].obj)
        return 0;
    if (!measure) {
        measure = opr_measure;
        userdata = (void *)s->deck;
    }
    at->para = 0;
    at->offset = 0;
    count = ol_display_count(s->doc);
    for (i = 0; i < count; i++) {
        const ol_display_op *op = ol_display_get(s->doc, i);
        const ol_rect *r;
        ol_unit dy, dx;
        if (!op || op->type != OL_OP_TEXT || op->page != 0 || opr_slide_frame_of(s, op) != frame)
            continue;
        r = &op->u.text.bounds;
        dy = y < r->y ? r->y - y : y >= r->y + r->height ? y - (r->y + r->height) + 1 : 0;
        dx = x < r->x ? r->x - x : x > r->x + r->width ? x - (r->x + r->width) : 0;
        if (!best || dy < best_dy || (dy == best_dy && dx < best_dx)) {
            best = op;
            best_dy = dy;
            best_dx = dx;
        }
    }
    if (!best) {
        /* no text laid out: the end of what there is */
        const owf_obj *o = s->frames[frame].obj;
        if (o->text.nparas) {
            at->para = o->text.nparas - 1;
            at->offset = opr_para_length(&o->text.paras[at->para]);
        }
        return 1;
    }
    {
        const ol_node *n = ol_node_by_id(s->doc, best->u.text.node_id);
        const char *t = n ? ol_node_text(n) : NULL;
        size_t len = best->u.text.text_length, b, pick = 0;
        ol_unit pick_d = -1;
        ol_text_position tp;
        if (!t)
            return 1;
        t += best->u.text.text_offset;
        for (b = 0; b <= len; b++) {
            ol_text_metrics m;
            ol_unit d;
            if (b < len && b > 0 && ((unsigned char)t[b] & 0xC0) == 0x80)
                continue;
            m.width = 0;
            if (b)
                measure(userdata, t, b, &best->u.text.style, &m);
            d = best->u.text.bounds.x + m.width - x;
            if (d < 0)
                d = -d;
            if (pick_d < 0 || d < pick_d) {
                pick_d = d;
                pick = b;
            }
        }
        tp.node_id = best->u.text.node_id;
        tp.offset = best->u.text.text_offset + pick;
        return from_text(s, frame, &tp, at);
    }
}

int opr_hit(const opr_slide *s, ol_unit x, ol_unit y, ol_measure_text_fn measure, void *userdata, opr_pos *at)
{
    int i;
    if (!s)
        return -1;
    for (i = s->nframes - 1; i >= 0; i--) {
        const opr_frame *f = &s->frames[i];
        ol_unit fx = (ol_unit)f->box.x * OL_UNITS_PER_TWIP, fy = (ol_unit)f->box.y * OL_UNITS_PER_TWIP;
        ol_unit fw = (ol_unit)f->box.width * OL_UNITS_PER_TWIP, fh = (ol_unit)f->box.height * OL_UNITS_PER_TWIP;
        if (!f->obj || f->from_master || opr_frame_obj(s, i) < 0)
            continue;
        if (x < fx || y < fy || x >= fx + fw || y >= fy + fh)
            continue;
        if (at)
            opr_hit_frame(s, i, x, y, measure, userdata, at);
        return i;
    }
    return -1;
}

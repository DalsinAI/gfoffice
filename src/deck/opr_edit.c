/*
 * OpenPresentation's core: editing a text frame's text (see
 * openpresentation_core.h, "Editing"). Positions are a paragraph and a
 * byte offset into its text as opr_para_text gives it; text goes into the
 * text run at the position, so it takes that run's look. The caret and the
 * pointer are mapped through the slide's OpenLayout display list: each
 * paragraph is a block of the frame, each run a node after it (after the
 * bullet and its tab, for a list paragraph), as opr_layout.c builds them.
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "openpresentation_core.h"

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
    if (o->kind == OWF_OBJ_TEXT)
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
        int j;
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
                break;
            }
            cum += k;
        }
        if (j == p->nruns)
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
    return join(o, at->para);
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

/* ---- Undo: copies of an object's text ---- */

typedef struct {
    int slide, obj;
    owf_story text;
    unsigned char *levels;
    int caplevels;
    unsigned flags;
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
    story_free(&s->text);
    owf_free(s->levels);
    memset(s, 0, sizeof *s);
}

static int snap_take(snapshot *s, const owf_deck *deck, int slide, int obj)
{
    const owf_obj *o = object((owf_deck *)deck, slide, obj);
    memset(s, 0, sizeof *s);
    if (!o)
        return -1;
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

/* The object's text and the snapshot's change places. */
static int snap_swap(snapshot *s, owf_deck *deck)
{
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
    int i;
    if (!u)
        return -1;
    for (i = 0; i < u->nredo; i++)
        snap_free(&u->redo[i]);
    u->nredo = 0;
    if (u->nundo == UNDO_STEPS) {
        snap_free(&u->undo[0]);
        memmove(&u->undo[0], &u->undo[1], (UNDO_STEPS - 1) * sizeof u->undo[0]);
        u->nundo--;
    }
    if (snap_take(&u->undo[u->nundo], deck, slide, obj) < 0)
        return -1;
    u->nundo++;
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
    if (!*nfrom)
        return -1;
    s = &from[*nfrom - 1];
    if (snap_swap(s, deck) < 0)
        return -1;
    if (slide) *slide = s->slide;
    if (obj) *obj = s->obj;
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

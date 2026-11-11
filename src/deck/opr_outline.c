/*
 * GFOpenPresentation's core: the outline (openpresentation_core.h, "The
 * outline"). The deck as one list of rows: each slide's title, then the
 * paragraphs of its outline text box, each at its level. Typing changes a
 * row's text; Return adds a row (a new slide after a title, a paragraph
 * after body text); demoting and promoting change a row's level, a title
 * demoted joining the slide before and a top-level paragraph promoted
 * starting a slide of its own. Each change is one Undo step, made of the
 * single steps in opr_edit.c.
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdlib.h>
#include <string.h>

/* ---- Which boxes the outline shows ---- */

int opr_outline_title_obj(const owf_deck *deck, int slide)
{
    const owf_slide *s;
    int i;
    if (!deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    for (i = 0; i < s->nobjs; i++)
        if (s->objs[i].kind == OWF_OBJ_TEXT && s->objs[i].placeholder == OWF_PH_TITLE &&
            !(s->objs[i].flags & OWF_OBJ_HIDDEN))
            return i;
    return -1;
}

int opr_outline_body_obj(const owf_deck *deck, int slide)
{
    const owf_slide *s;
    int i;
    if (!deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    for (i = 0; i < s->nobjs; i++) {
        const owf_obj *o = &s->objs[i];
        if (o->kind == OWF_OBJ_TEXT && !(o->flags & OWF_OBJ_HIDDEN) &&
            (o->placeholder == OWF_PH_BODY || o->placeholder == OWF_PH_OBJECT || o->placeholder == OWF_PH_TEXT))
            return i;
    }
    return -1;
}

/* A box that has never been typed in shows nothing: no paragraphs. */
static int body_paras(const owf_deck *deck, int slide, int obj)
{
    const owf_obj *o;
    if (obj < 0)
        return 0;
    o = &deck->slides[slide].objs[obj];
    return (o->flags & OWF_OBJ_EMPTY) ? 0 : o->text.nparas;
}

/* ---- The rows ---- */

static int add_row(opr_outline *out, int slide, int obj, int para, int level)
{
    opr_outline_row *r;
    if (out->count == out->cap) {
        int want = out->cap ? out->cap * 2 : 32;
        r = realloc(out->rows, (size_t)want * sizeof *r);
        if (!r)
            return -1;
        out->rows = r;
        out->cap = want;
    }
    r = &out->rows[out->count++];
    r->slide = slide;
    r->obj = obj;
    r->para = para;
    r->level = level;
    return 0;
}

int opr_outline_build(const owf_deck *deck, opr_outline *out)
{
    int slide;
    if (!out)
        return -1;
    out->count = 0;
    if (!deck)
        return 0;
    for (slide = 0; slide < deck->nslides; slide++) {
        int title = opr_outline_title_obj(deck, slide), body = opr_outline_body_obj(deck, slide), n, i;
        if (add_row(out, slide, title, 0, 0) < 0)
            return -1;
        n = body_paras(deck, slide, body);
        for (i = 0; i < n; i++) {
            int level = owf_obj_level(&deck->slides[slide].objs[body], i);
            if (add_row(out, slide, body, i, 1 + (level < 0 ? 0 : level)) < 0)
                return -1;
        }
    }
    return 0;
}

void opr_outline_free(opr_outline *out)
{
    if (!out)
        return;
    free(out->rows);
    memset(out, 0, sizeof *out);
}

int opr_outline_find(const opr_outline *out, int slide, int obj, int para)
{
    int i;
    for (i = 0; out && i < out->count; i++)
        if (out->rows[i].slide == slide && (obj < 0 ? out->rows[i].level == 0
                                                    : out->rows[i].obj == obj && out->rows[i].para == para &&
                                                      (out->rows[i].level > 0 || para == 0)))
            return i;
    return -1;
}

char *opr_outline_text(const owf_deck *deck, const opr_outline_row *r)
{
    const owf_obj *o;
    size_t n;
    char *t;
    if (!deck || !r || r->slide < 0 || r->slide >= deck->nslides)
        return NULL;
    if (r->obj < 0 || r->obj >= deck->slides[r->slide].nobjs) {
        t = owf_malloc(1);
        if (t)
            t[0] = 0;
        return t;
    }
    o = &deck->slides[r->slide].objs[r->obj];
    if ((o->flags & OWF_OBJ_EMPTY) || r->para >= o->text.nparas) {
        t = owf_malloc(1);
        if (t)
            t[0] = 0;
        return t;
    }
    if (r->level == 0 && o->text.nparas > 1) {
        /* a title over several paragraphs reads as one line */
        char *all = opr_text_get(deck, r->slide, r->obj), *c;
        for (c = all; c && *c; c++)
            if (*c == '\n')
                *c = ' ';
        return all;
    }
    return opr_para_text(&o->text.paras[r->para], &n);
}

/* ---- Changes ---- */

/* A paragraph's text replaced, its format and level kept. */
static int replace_para(owf_deck *deck, int slide, int obj, int para, const char *utf8)
{
    opr_selection sel;
    opr_pos at;
    owf_obj *o = &deck->slides[slide].objs[obj];
    if (para >= o->text.nparas)
        return -1;
    sel.anchor.para = sel.focus.para = para;
    sel.anchor.offset = 0;
    sel.focus.offset = opr_para_length(&o->text.paras[para]);
    if (sel.focus.offset && opr_text_delete_selection(deck, slide, obj, &sel) < 0)
        return -1;
    at.para = para;
    at.offset = 0;
    return opr_text_insert(deck, slide, obj, &at, utf8, strlen(utf8));
}

int opr_outline_set_text(opr_undo *u, owf_deck *deck, const opr_outline_row *r, const char *utf8)
{
    owf_obj *o;
    int rc;
    if (!deck || !r || !utf8 || r->slide < 0 || r->slide >= deck->nslides)
        return -1;
    if (r->obj < 0 || r->obj >= deck->slides[r->slide].nobjs)
        return -2;               /* a slide with no title box */
    o = &deck->slides[r->slide].objs[r->obj];
    if (u && opr_undo_push(u, deck, r->slide, r->obj) < 0)
        return -1;
    if (r->level == 0 || (o->flags & OWF_OBJ_EMPTY) || !o->text.nparas)
        rc = opr_text_set(deck, r->slide, r->obj, utf8);
    else
        rc = replace_para(deck, r->slide, r->obj, r->para, utf8);
    return rc < 0 ? -1 : 0;
}

/* A paragraph copied onto the end of another box at a level (0-based). */
static int append_para(owf_deck *deck, int slide, int obj, const owf_para *from, int level)
{
    owf_obj *o = &deck->slides[slide].objs[obj];
    owf_para *p;
    int i;
    if (o->flags & OWF_OBJ_EMPTY) {
        /* its prompt text goes: the box starts with this paragraph */
        if (opr_text_set(deck, slide, obj, "") < 0)
            return -1;
        o = &deck->slides[slide].objs[obj];
        p = &o->text.paras[0];
        p->fmt = from->fmt;
    } else {
        p = owf_story_add(&o->text, &from->fmt);
        if (!p)
            return -1;
    }
    for (i = 0; i < from->nruns; i++) {
        const owf_run *r = &from->runs[i];
        int rc = 0;
        if (r->kind == OWF_RUN_TEXT && r->text)
            rc = r->href ? owf_para_add_link_text(p, &r->fmt, r->text, strlen(r->text), r->href)
                         : owf_para_add_text(p, &r->fmt, r->text, strlen(r->text));
        else if (r->kind == OWF_RUN_TAB || r->kind == OWF_RUN_LINEBREAK || r->kind == OWF_RUN_FIELD)
            rc = owf_para_add_special(p, &r->fmt, r->kind, r->field);
        if (rc < 0)
            return -1;
    }
    return owf_obj_set_level(o, o->text.nparas - 1, level < 0 ? 0 : level > OWF_TEXT_LEVELS - 1 ? OWF_TEXT_LEVELS - 1 : level);
}

/* Paragraphs first.. of a box taken out (the box keeps one, emptied, when all go). */
static int remove_paras(owf_deck *deck, int slide, int obj, int first)
{
    owf_obj *o = &deck->slides[slide].objs[obj];
    opr_selection sel;
    if (first >= o->text.nparas)
        return 0;
    if (first == 0) {
        /* all of them: the box back to its prompt */
        if (opr_text_set(deck, slide, obj, "") < 0)
            return -1;
        deck->slides[slide].objs[obj].flags |= OWF_OBJ_EMPTY;
        return 0;
    }
    sel.anchor.para = first - 1;
    sel.anchor.offset = opr_para_length(&o->text.paras[first - 1]);
    sel.focus.para = o->text.nparas - 1;
    sel.focus.offset = opr_para_length(&o->text.paras[o->text.nparas - 1]);
    return opr_text_delete_selection(deck, slide, obj, &sel) < 0 ? -1 : 0;
}

/* A new slide after this one, its layout; its index, or -1. */
static int new_slide_after(opr_undo *u, owf_deck *deck, int slide)
{
    int sel = slide;
    if (opr_slide_insert(u, deck, slide + 1, deck->slides[slide].layout, &sel) < 0)
        return -1;
    return slide + 1;
}

int opr_outline_insert_after(opr_undo *u, owf_deck *deck, const opr_outline_row *r, int *new_slide, int *new_para)
{
    int rc = -1;
    if (!deck || !r || r->slide < 0 || r->slide >= deck->nslides)
        return -1;
    if (new_slide) *new_slide = r->slide;
    if (new_para) *new_para = 0;
    if (u) opr_undo_group_begin(u, 4);
    if (r->level == 0) {
        /* after a title: a new slide, the same layout */
        int s = new_slide_after(u, deck, r->slide);
        if (s >= 0) {
            rc = 0;
            if (new_slide) *new_slide = s;
        }
    } else {
        owf_obj *o = &deck->slides[r->slide].objs[r->obj];
        opr_pos at;
        if (u && opr_undo_push(u, deck, r->slide, r->obj) < 0)
            goto done;
        at.para = r->para;
        at.offset = opr_para_length(&o->text.paras[r->para]);
        if (opr_text_newline(deck, r->slide, r->obj, &at) >= 0) {
            rc = 0;
            if (new_para) *new_para = at.para;
        }
    }
done:
    if (u) {
        if (rc < 0) opr_undo_group_cancel(u, deck);
        else if (opr_undo_group_end(u) < 0) rc = -1;
    }
    return rc;
}

int opr_outline_demote(opr_undo *u, owf_deck *deck, const opr_outline_row *r)
{
    int rc = -1;
    if (!deck || !r || r->slide < 0 || r->slide >= deck->nslides)
        return -1;
    if (r->level > 0) {
        owf_obj *o = &deck->slides[r->slide].objs[r->obj];
        int level = owf_obj_level(o, r->para);
        if (level >= OWF_TEXT_LEVELS - 1)
            return -2;
        if (u && opr_undo_push(u, deck, r->slide, r->obj) < 0)
            return -1;
        return owf_obj_set_level(o, r->para, level + 1) < 0 ? -1 : 0;
    }
    /* a title demoted: its slide joins the one before, the title a top-level paragraph there */
    {
        int prev = r->slide - 1, into, title = r->obj, body, n, i, sel = prev;
        if (prev < 0)
            return -2;
        into = opr_outline_body_obj(deck, prev);
        if (into < 0)
            return -2;
        body = opr_outline_body_obj(deck, r->slide);
        n = body_paras(deck, r->slide, body);
        if (u) opr_undo_group_begin(u, 4);
        if (u && opr_undo_push(u, deck, prev, into) < 0)
            goto cancel;
        if (title >= 0 && !(deck->slides[r->slide].objs[title].flags & OWF_OBJ_EMPTY)) {
            const owf_obj *t = &deck->slides[r->slide].objs[title];
            for (i = 0; i < t->text.nparas; i++)
                if (append_para(deck, prev, into, &t->text.paras[i], 0) < 0)
                    goto cancel;
        } else {
            owf_para empty;
            memset(&empty, 0, sizeof empty);
            owf_parafmt_init(&empty.fmt);
            if (append_para(deck, prev, into, &empty, 0) < 0)
                goto cancel;
        }
        for (i = 0; i < n; i++) {
            const owf_obj *b = &deck->slides[r->slide].objs[body];
            if (append_para(deck, prev, into, &b->text.paras[i], owf_obj_level(b, i) + 1) < 0)
                goto cancel;
        }
        if (opr_slide_delete(u, deck, r->slide, &sel) < 0)
            goto cancel;
        rc = 0;
        if (u && opr_undo_group_end(u) < 0)
            rc = -1;
        return rc;
cancel:
        if (u) opr_undo_group_cancel(u, deck);
        return -1;
    }
}

int opr_outline_promote(opr_undo *u, owf_deck *deck, const opr_outline_row *r, int *new_slide)
{
    owf_obj *o;
    int level, s, title, body, i, n;
    if (new_slide) *new_slide = r ? r->slide : -1;
    if (!deck || !r || r->slide < 0 || r->slide >= deck->nslides)
        return -1;
    if (r->level == 0)
        return -2;               /* a title is as high as it goes */
    o = &deck->slides[r->slide].objs[r->obj];
    level = owf_obj_level(o, r->para);
    if (level > 0) {
        if (u && opr_undo_push(u, deck, r->slide, r->obj) < 0)
            return -1;
        return owf_obj_set_level(o, r->para, level - 1) < 0 ? -1 : 0;
    }
    /* a top-level paragraph promoted: a slide of its own after this one, its title this paragraph, the
       paragraphs after it under it, a level higher */
    if (u) opr_undo_group_begin(u, 8);
    s = new_slide_after(u, deck, r->slide);
    if (s < 0)
        goto cancel;
    title = opr_outline_title_obj(deck, s);
    body = opr_outline_body_obj(deck, s);
    o = &deck->slides[r->slide].objs[r->obj];
    n = o->text.nparas;
    if (title >= 0) {
        if (u && opr_undo_push(u, deck, s, title) < 0)
            goto cancel;
        if (append_para(deck, s, title, &o->text.paras[r->para], 0) < 0)
            goto cancel;
        o = &deck->slides[r->slide].objs[r->obj];
    }
    if (r->para + 1 < n) {
        if (body < 0)
            goto cancel;
        if (u && opr_undo_push(u, deck, s, body) < 0)
            goto cancel;
        for (i = r->para + 1; i < n; i++) {
            o = &deck->slides[r->slide].objs[r->obj];
            if (append_para(deck, s, body, &o->text.paras[i], owf_obj_level(o, i) - 1) < 0)
                goto cancel;
        }
    }
    if (u && opr_undo_push(u, deck, r->slide, r->obj) < 0)
        goto cancel;
    if (remove_paras(deck, r->slide, r->obj, r->para) < 0)
        goto cancel;
    if (u && opr_undo_group_end(u) < 0)
        return -1;
    if (new_slide) *new_slide = s;
    return 0;
cancel:
    if (u) opr_undo_group_cancel(u, deck);
    return -1;
}

int opr_outline_delete(opr_undo *u, owf_deck *deck, const opr_outline_row *r)
{
    owf_obj *o;
    opr_selection sel;
    if (!deck || !r || r->slide < 0 || r->slide >= deck->nslides)
        return -1;
    if (r->level == 0) {
        int sel_slide = r->slide;
        if (deck->nslides <= 1)
            return -2;
        return opr_slide_delete(u, deck, r->slide, &sel_slide) < 0 ? -1 : 0;
    }
    o = &deck->slides[r->slide].objs[r->obj];
    if (u && opr_undo_push(u, deck, r->slide, r->obj) < 0)
        return -1;
    if (o->text.nparas <= 1)
        return remove_paras(deck, r->slide, r->obj, 0);
    if (r->para > 0) {
        sel.anchor.para = r->para - 1;
        sel.anchor.offset = opr_para_length(&o->text.paras[r->para - 1]);
        sel.focus.para = r->para;
        sel.focus.offset = opr_para_length(&o->text.paras[r->para]);
    } else {
        sel.anchor.para = 0;
        sel.anchor.offset = 0;
        sel.focus.para = 1;
        sel.focus.offset = 0;
    }
    return opr_text_delete_selection(deck, r->slide, r->obj, &sel) < 0 ? -1 : 0;
}

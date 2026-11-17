/*
 * libowf: footnotes and endnotes (owf_note.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_notenum_impl.h"
#include "owf_note.h"
#include "owf_comment.h"
#include "owf_track.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *s)
{
    size_t n;
    char *out;
    if (!s)
        return NULL;
    n = strlen(s);
    out = malloc(n + 1);
    if (out)
        memcpy(out, s, n + 1);
    return out;
}

int owf_note_add(owf_doc *doc, int endnote, const char *text, const char *label)
{
    owf_note *n;
    if (!doc)
        return -1;
    if (doc->nnotes == doc->capnotes) {
        int want = doc->capnotes ? doc->capnotes * 2 : 8;
        owf_note *grown = realloc(doc->notes, (size_t)want * sizeof *grown);
        if (!grown)
            return -1;
        doc->notes = grown;
        doc->capnotes = want;
    }
    n = &doc->notes[doc->nnotes];
    memset(n, 0, sizeof *n);
    n->endnote = endnote ? 1 : 0;
    n->text = copy_string(text ? text : "");
    n->label = label && *label ? copy_string(label) : NULL;
    if (!n->text || (label && *label && !n->label)) {
        free(n->text);
        free(n->label);
        memset(n, 0, sizeof *n);
        return -1;
    }
    return doc->nnotes++;
}

/* 1.12.2: the notes' formatted text (owf_doc.note_stories) */
void owf_note_story_free(owf_story *story)
{
    int i, j;
    if (!story)
        return;
    for (i = 0; i < story->nparas; i++) {
        for (j = 0; j < story->paras[i].nruns; j++) {
            free(story->paras[i].runs[j].text);
            free(story->paras[i].runs[j].href);
        }
        free(story->paras[i].runs);
        free(story->paras[i].style);
        free(story->paras[i].bookmarks);
    }
    free(story->paras);
    memset(story, 0, sizeof *story);
}

static int visible_run(const owf_run *r)
{
    return r->kind != OWF_RUN_TEXT || (r->text && *r->text);
}

static void drop_run(owf_para *p, int j)
{
    free(p->runs[j].text);
    free(p->runs[j].href);
    memmove(&p->runs[j], &p->runs[j + 1], (size_t)(p->nruns - j - 1) * sizeof *p->runs);
    p->nruns--;
}

/* What a note's story may hold: its text, tabs, line breaks and fields. Marks, comments, deleted text, pictures
 * and frames go (a note has no notes or comments of its own, and its part no pictures); the changes it kept are
 * accepted; the leading spaces after its mark and its empty paragraphs at the end go, as its plain text's do. */
static void tidy_story(const owf_doc *doc, owf_story *s)
{
    int i, j;
    for (i = 0; i < s->nparas; ++i) {
        owf_para *p = &s->paras[i];
        free(p->style);
        free(p->bookmarks);
        p->style = p->bookmarks = NULL;
        p->mark_revision = 0;
        p->table_id = -1;
        p->table_row = p->table_col = p->table_cols = 0;
        p->section_columns = p->section_gap = 0;
        p->section_break = 0;
        p->para_format_change = 0;                   /* 1.21 */
        for (j = 0; j < p->nruns;) {
            owf_run *r = &p->runs[j];
            const owf_revision *chg = OWF_DOC_REVISION(doc, r->revision);
            if (r->note || r->comment || (chg && chg->kind == OWF_REV_DELETE) || r->kind == OWF_RUN_IMAGE ||
                r->kind == OWF_RUN_FRAME || (r->kind == OWF_RUN_TEXT && !r->text)) {
                drop_run(p, j);
                continue;
            }
            r->revision = 0;
            r->format_change = 0;
            ++j;
        }
    }
    for (i = 0; i < s->nparas; ++i) {                /* the spaces after the mark */
        owf_para *p = &s->paras[i];
        int any = 0;
        for (j = 0; j < p->nruns && !any;) {
            owf_run *r = &p->runs[j];
            if (r->kind == OWF_RUN_TEXT) {
                size_t k = strspn(r->text, " \t");
                memmove(r->text, r->text + k, strlen(r->text + k) + 1);
                if (!*r->text) {
                    drop_run(p, j);
                    continue;
                }
            }
            any = 1;
        }
        if (any)
            break;
    }
    while (s->nparas > 0) {                          /* empty paragraphs at the end */
        owf_para *p = &s->paras[s->nparas - 1];
        for (j = 0; j < p->nruns && !visible_run(&p->runs[j]); ++j)
            ;
        if (j < p->nruns)
            break;
        for (j = 0; j < p->nruns; ++j) {
            free(p->runs[j].text);
            free(p->runs[j].href);
        }
        free(p->runs);
        s->nparas--;
    }
}

int owf_note_story_formatted(const owf_story *story)
{
    const owf_charfmt *first = NULL;
    int i, j;
    if (!story)
        return 0;
    for (i = 0; i < story->nparas; ++i)
        for (j = 0; j < story->paras[i].nruns; ++j) {
            const owf_run *r = &story->paras[i].runs[j];
            if (r->kind == OWF_RUN_FIELD || r->href || (r->fmt.flags & (OWF_BOLD | OWF_ITALIC | OWF_UNDERLINE |
                                                                        OWF_STRIKE | OWF_SUPER | OWF_SUB | OWF_HIDDEN)))
                return 1;
            if (r->kind != OWF_RUN_TEXT || !r->text || !*r->text)
                continue;
            if (!first)
                first = &r->fmt;
            else if (r->fmt.font != first->font || r->fmt.size != first->size || r->fmt.colour != first->colour)
                return 1;
        }
    return 0;
}

int owf_note_set_story(owf_doc *doc, int index, owf_story *story)
{
    if (!doc || index < 0 || index >= doc->nnotes) {
        owf_note_story_free(story);
        return -1;
    }
    if (story)
        tidy_story(doc, story);
    if (!story || !owf_note_story_formatted(story)) {   /* plain: its text says it all */
        if (index < doc->nnote_stories)
            owf_note_story_free(&doc->note_stories[index]);
        owf_note_story_free(story);
        return 0;
    }
    if (index >= doc->nnote_stories) {
        owf_story *grown = realloc(doc->note_stories, (size_t)(index + 1) * sizeof *grown);
        if (!grown) {
            owf_note_story_free(story);
            return -1;
        }
        memset(grown + doc->nnote_stories, 0, (size_t)(index + 1 - doc->nnote_stories) * sizeof *grown);
        doc->note_stories = grown;
        doc->nnote_stories = index + 1;
    }
    owf_note_story_free(&doc->note_stories[index]);
    doc->note_stories[index] = *story;               /* its paragraphs are the document's now */
    memset(story, 0, sizeof *story);
    return 0;
}

static char *copy_text(const char *s)
{
    return s ? copy_string(s) : NULL;
}

int owf_note_story_copy(owf_story *to, const owf_story *from)
{
    int i, j;
    memset(to, 0, sizeof *to);
    if (!from || !from->nparas)
        return 0;
    if (!(to->paras = calloc((size_t)from->nparas, sizeof *to->paras)))
        return -1;
    to->capparas = from->nparas;
    for (i = 0; i < from->nparas; ++i) {
        const owf_para *p = &from->paras[i];
        owf_para *q = &to->paras[i];
        *q = *p;
        q->runs = NULL;
        q->nruns = q->capruns = 0;
        q->style = copy_text(p->style);
        q->bookmarks = copy_text(p->bookmarks);
        to->nparas = i + 1;
        if ((p->style && !q->style) || (p->bookmarks && !q->bookmarks))
            goto fail;
        if (!p->nruns)
            continue;
        if (!(q->runs = calloc((size_t)p->nruns, sizeof *q->runs)))
            goto fail;
        q->capruns = p->nruns;
        for (j = 0; j < p->nruns; ++j) {
            q->runs[j] = p->runs[j];
            q->runs[j].text = copy_text(p->runs[j].text);
            q->runs[j].href = copy_text(p->runs[j].href);
            q->nruns = j + 1;
            if ((p->runs[j].text && !q->runs[j].text) || (p->runs[j].href && !q->runs[j].href))
                goto fail;
        }
    }
    return 0;
fail:
    owf_note_story_free(to);
    return -1;
}

int owf_note_copy(owf_doc *doc, int index)
{
    owf_note from;
    owf_story copy;
    int to;
    if (!doc || index < 0 || index >= doc->nnotes)
        return -1;
    from = doc->notes[index];                        /* owf_note_add may move the list */
    to = owf_note_add(doc, from.endnote, from.text, from.label);
    if (to >= 0 && index < doc->nnote_stories && doc->note_stories[index].nparas > 0) {   /* 1.12.2: its formats */
        if (owf_note_story_copy(&copy, &doc->note_stories[index]) < 0 || owf_note_set_story(doc, to, &copy) < 0)
            return -1;
    }
    return to;
}

int owf_para_add_note(owf_para *para, const owf_charfmt *fmt, int mark)
{
    owf_run *run;
    if (!para || !fmt || mark <= 0)
        return -1;
    if (para->nruns == para->capruns) {
        int want = para->capruns ? para->capruns * 2 : 4;
        owf_run *runs = realloc(para->runs, (size_t)want * sizeof *runs);
        if (!runs)
            return -1;
        para->runs = runs;
        para->capruns = want;
    }
    run = &para->runs[para->nruns];
    memset(run, 0, sizeof *run);
    run->text = copy_string("");
    if (!run->text)
        return -1;
    run->kind = OWF_RUN_TEXT;
    run->fmt = *fmt;
    run->fmt.flags &= ~(unsigned)(OWF_SUPER | OWF_SUB);  /* the mark is raised where it is shown */
    run->note = mark;
    para->nruns++;
    return 0;
}

void owf_builder_note(owf_builder *b, int mark)
{
    owf_para *p = owf_builder_para(b);               /* owf_track.h's: flushes the text before it */
    if (!p || owf_para_add_note(p, &b->charfmt, mark) < 0)
        b->failed = 1;
}

void owf_note_finish(owf_doc *doc)
{
    owf_story *s;
    int n, i, j;
    for (n = 0; doc && (s = owf_doc_story(doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j) {
                owf_run *r = &s->paras[i].runs[j];
                if (r->note && (r->note < 0 || r->note > doc->nnotes || r->kind != OWF_RUN_TEXT))
                    r->note = 0;
            }
}

int owf_doc_note_count(const owf_doc *doc, int endnote)
{
    owf_story *s;
    int n, i, j, count = 0;
    for (n = 0; doc && (s = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j) {
                int m = s->paras[i].runs[j].note;
                if (m > 0 && m <= doc->nnotes && (endnote < 0 || doc->notes[m - 1].endnote == endnote))
                    ++count;
            }
    return count;
}

int owf_note_number(const owf_doc *doc, int index)
{
    owf_story *s;
    int n, i, j, count[2] = {0, 0}, kind;
    if (!doc || index < 0 || index >= doc->nnotes)
        return 0;
    kind = doc->notes[index].endnote ? 1 : 0;
    for (n = 0; (s = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i) {
            owf_note_section_restart_impl(doc, s, i, count);   /* 1.12.3: again at each section */
            for (j = 0; j < s->paras[i].nruns; ++j) {
                int m = s->paras[i].runs[j].note;
                if (m <= 0 || m > doc->nnotes || (doc->notes[m - 1].endnote ? 1 : 0) != kind || doc->notes[m - 1].label)
                    continue;
                ++count[kind];
                if (m == index + 1)
                    return count[kind];
            }
        }
    return 0;
}

void owf_note_roman(int number, char *out, size_t size)
{
    static const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *const digits[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
    size_t at = 0;
    int k;
    if (!out || !size)
        return;
    out[0] = 0;
    if (number < 1 || number > 3999) {
        snprintf(out, size, "%d", number);
        return;
    }
    for (k = 0; k < 13; ++k)
        while (number >= values[k]) {
            size_t len = strlen(digits[k]);
            if (at + len >= size)
                return;
            memcpy(out + at, digits[k], len + 1);
            at += len;
            number -= values[k];
        }
}

int owf_note_kind_format(const owf_doc *doc, int endnote)
{
    return owf_note_kind_format_impl(doc, endnote);
}

int owf_note_kind_start(const owf_doc *doc, int endnote)
{
    return owf_note_kind_start_impl(doc, endnote);
}

void owf_note_format_number(int format, int endnote, int number, char *out, size_t size)
{
    owf_note_format_impl(format, endnote, number, out, size);
}

void owf_note_mark_text(const owf_doc *doc, int index, char *out, size_t size)
{
    int number;
    if (!out || !size)
        return;
    out[0] = 0;
    if (!doc || index < 0 || index >= doc->nnotes || !owf_note_place(doc, index, NULL, NULL, NULL))
        return;
    if (doc->notes[index].label) {
        snprintf(out, size, "%s", doc->notes[index].label);
        return;
    }
    number = owf_note_number(doc, index);
    if (number > 0)                                  /* 1.12.1: the page's format and first number */
        number += owf_note_kind_start(doc, doc->notes[index].endnote) - 1;
    owf_note_format_number(owf_note_kind_format(doc, doc->notes[index].endnote), doc->notes[index].endnote,
                           number, out, size);
}

int owf_note_place(const owf_doc *doc, int index, int *story, int *para, int *run)
{
    owf_story *s;
    int n, i, j;
    for (n = 0; doc && (s = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j)
                if (s->paras[i].runs[j].note == index + 1) {
                    if (story) *story = n;
                    if (para) *para = i;
                    if (run) *run = j;
                    return 1;
                }
    return 0;
}

int owf_notes_listing(const owf_doc *doc, int endnote, owf_buf *out)
{
    owf_story *s;
    int n, i, j, any = 0;
    if (!doc || !out)
        return -1;
    for (n = 0; (s = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j) {
                int m = s->paras[i].runs[j].note;
                char mark[32];
                const char *t;
                if (m <= 0 || m > doc->nnotes || doc->notes[m - 1].endnote != (endnote ? 1 : 0))
                    continue;
                if (!any) {
                    owf_buf_puts(out, endnote ? "Endnotes\n" : "Notes\n");
                    any = 1;
                }
                owf_note_mark_text(doc, m - 1, mark, sizeof mark);
                owf_buf_puts(out, mark);
                owf_buf_putc(out, '\t');
                for (t = doc->notes[m - 1].text; *t; ++t)
                    owf_buf_putc(out, *t == '\n' ? ' ' : *t);
                owf_buf_putc(out, '\n');
            }
    return out->failed ? -1 : 0;
}

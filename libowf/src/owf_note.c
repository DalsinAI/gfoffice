/*
 * libowf: footnotes and endnotes (owf_note.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
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

int owf_note_copy(owf_doc *doc, int index)
{
    owf_note from;
    if (!doc || index < 0 || index >= doc->nnotes)
        return -1;
    from = doc->notes[index];                        /* owf_note_add may move the list */
    return owf_note_add(doc, from.endnote, from.text, from.label);
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
    int n, i, j, count = 0, kind;
    if (!doc || index < 0 || index >= doc->nnotes)
        return 0;
    kind = doc->notes[index].endnote;
    for (n = 0; (s = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j) {
                int m = s->paras[i].runs[j].note;
                if (m <= 0 || m > doc->nnotes || doc->notes[m - 1].endnote != kind || doc->notes[m - 1].label)
                    continue;
                ++count;
                if (m == index + 1)
                    return count;
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
    if (doc->notes[index].endnote)
        owf_note_roman(number, out, size);
    else
        snprintf(out, size, "%d", number);
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

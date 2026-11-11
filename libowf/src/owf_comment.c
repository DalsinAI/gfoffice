/*
 * libowf: comments (owf_comment.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_comment.h"
#include "owf_track.h"

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

static void free_comment(owf_annotation *c)
{
    free(c->author);
    free(c->initials);
    free(c->date);
    free(c->text);
    memset(c, 0, sizeof *c);
}

int owf_comment_add(owf_doc *doc, const char *author, const char *initials, const char *date, const char *text,
                    int parent)
{
    owf_annotation *c;
    if (!doc)
        return -1;
    if (doc->ncomments == doc->capcomments) {
        int want = doc->capcomments ? doc->capcomments * 2 : 8;
        owf_annotation *grown = realloc(doc->comments, (size_t)want * sizeof *grown);
        if (!grown)
            return -1;
        doc->comments = grown;
        doc->capcomments = want;
    }
    c = &doc->comments[doc->ncomments];
    memset(c, 0, sizeof *c);
    c->author = author && *author ? copy_string(author) : NULL;
    c->initials = initials && *initials ? copy_string(initials) : NULL;
    c->date = date && *date ? copy_string(date) : NULL;
    c->text = copy_string(text ? text : "");
    c->parent = parent >= 0 && parent < doc->ncomments ? parent : -1;
    if (!c->text || (author && *author && !c->author) || (initials && *initials && !c->initials) ||
        (date && *date && !c->date)) {
        free_comment(c);
        return -1;
    }
    return doc->ncomments++;
}

int owf_comment_copy(owf_doc *doc, int index)
{
    owf_annotation from;
    int n;
    if (!doc || index < 0 || index >= doc->ncomments)
        return -1;
    from = doc->comments[index];                     /* owf_comment_add may move the list */
    n = owf_comment_add(doc, from.author, from.initials, from.date, from.text, from.parent);
    if (n >= 0)
        doc->comments[n].done = from.done;
    return n;
}

int owf_para_add_comment(owf_para *para, const owf_charfmt *fmt, int mark)
{
    owf_run *run;
    if (!para || !fmt || !mark)
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
    run->comment = mark;
    para->nruns++;
    return 0;
}

void owf_builder_comment(owf_builder *b, int mark)
{
    owf_para *p = owf_builder_para(b);               /* owf_track.h's: flushes the text before it */
    if (!p || owf_para_add_comment(p, &b->charfmt, mark) < 0)
        b->failed = 1;
}

owf_story *owf_doc_story(owf_doc *doc, int n)
{
    if (!doc || n < 0)
        return NULL;
    switch (n) {
    case 0: return &doc->body;
    case 1: return &doc->header;
    case 2: return &doc->footer;
    case 3: return &doc->header_left;
    case 4: return &doc->footer_left;
    }
    n -= 5;
    return n < doc->nframes ? &doc->frames[n].story : NULL;
}

void owf_comment_finish(owf_doc *doc)
{
    unsigned char *seen;
    owf_story *s;
    int n, i, j;
    if (!doc || !doc->ncomments) {
        /* no comments: no run may name one */
        for (n = 0; doc && (s = owf_doc_story(doc, n)) != NULL; ++n)
            for (i = 0; i < s->nparas; ++i)
                for (j = 0; j < s->paras[i].nruns; ++j)
                    s->paras[i].runs[j].comment = 0;
        return;
    }
    seen = calloc((size_t)doc->ncomments, 1);        /* 1: anchored, 2: a start */
    for (n = 0; (s = owf_doc_story(doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j) {
                owf_run *r = &s->paras[i].runs[j];
                int idx = r->comment > 0 ? r->comment - 1 : -r->comment - 1;
                if (!r->comment) continue;
                if (idx >= doc->ncomments || r->kind != OWF_RUN_TEXT) { r->comment = 0; continue; }
                if (seen) seen[idx] |= r->comment > 0 ? 1 : 2;
            }
    if (!seen)
        return;
    /* a start with no anchor: the anchor there */
    for (n = 0; (s = owf_doc_story(doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j) {
                owf_run *r = &s->paras[i].runs[j];
                if (r->comment < 0 && seen[-r->comment - 1] == 2) {
                    r->comment = -r->comment;
                    seen[r->comment - 1] = 1;
                }
            }
    free(seen);
}

int owf_doc_has_comments(const owf_doc *doc)
{
    owf_story *s;
    int n, i, j;
    for (n = 0; doc && (s = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j)
                if (s->paras[i].runs[j].comment > 0)
                    return 1;
    return 0;
}

int owf_comment_place(const owf_doc *doc, int index, int *story, int *para, int *run)
{
    owf_story *s;
    int n, i, j;
    for (n = 0; doc && (s = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < s->nparas; ++i)
            for (j = 0; j < s->paras[i].nruns; ++j)
                if (s->paras[i].runs[j].comment == index + 1) {
                    if (story) *story = n;
                    if (para) *para = i;
                    if (run) *run = j;
                    return 1;
                }
    return 0;
}

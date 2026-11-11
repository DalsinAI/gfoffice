/*
 * libowf: tracked changes (owf_track.h). The readers number the changes they find and mark the runs and
 * paragraph marks that are theirs; the formats without tracked changes write the document as it is with
 * them all accepted.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
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

static int same_string(const char *a, const char *b)
{
    return (!a && !b) || (a && b && !strcmp(a, b));
}

int owf_track_add(owf_doc *doc, int kind, const char *author, const char *date)
{
    owf_revision *r;
    int i;
    if (!doc || (kind != OWF_REV_INSERT && kind != OWF_REV_DELETE))
        return 0;
    if (author && !*author)
        author = NULL;
    if (date && !*date)
        date = NULL;
    for (i = 0; i < doc->nrevisions; ++i)
        if (doc->revisions[i].kind == kind && same_string(doc->revisions[i].author, author) &&
            same_string(doc->revisions[i].date, date))
            return i + 1;
    if (doc->nrevisions == doc->caprevisions) {
        int want = doc->caprevisions ? doc->caprevisions * 2 : 8;
        owf_revision *grown = realloc(doc->revisions, (size_t)want * sizeof *grown);
        if (!grown)
            return 0;
        doc->revisions = grown;
        doc->caprevisions = want;
    }
    r = &doc->revisions[doc->nrevisions];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->author = copy_string(author);
    r->date = copy_string(date);
    if ((author && !r->author) || (date && !r->date)) {
        free(r->author);
        free(r->date);
        memset(r, 0, sizeof *r);
        return 0;
    }
    return ++doc->nrevisions;
}

/* Where the next run the builder adds will be: its paragraph (nparas when it has none open) and run. */
static void builder_place(owf_builder *b, int *para, int *run)
{
    owf_story *s = b->story;
    if (b->para && s->nparas && b->para == &s->paras[s->nparas - 1]) {
        *para = s->nparas - 1;
        *run = b->para->nruns;
    } else {
        *para = s->nparas;
        *run = 0;
    }
}

void owf_track_begin(owf_track_span *t, owf_builder *b)
{
    t->story = b->story;
    t->rev = 0;
    t->marks = 0;
    builder_place(b, &t->para, &t->run);
}

void owf_track_switch(owf_track_span *t, owf_builder *b, int rev, int marks)
{
    int i, j, end_para, end_run;
    owf_story *s = t->story;
    if (rev < 0)
        rev = 0;
    owf_builder_flush(b);
    if (s != b->story) {
        /* a new story: what was begun in the last ends with it */
        owf_track_begin(t, b);
    } else {
        builder_place(b, &end_para, &end_run);
        if (t->rev) {
            for (i = t->para; i < s->nparas && i <= end_para; ++i) {
                owf_para *p = &s->paras[i];
                int from = i == t->para ? t->run : 0;
                int to = i == end_para ? end_run : p->nruns;
                for (j = from; j < to && j < p->nruns; ++j)
                    if (p->runs[j].kind != OWF_RUN_FRAME && p->runs[j].kind != OWF_RUN_IMAGE && !p->runs[j].comment)
                        p->runs[j].revision = t->rev;   /* text, tabs, breaks and fields: a frame or a comment's
                                                           place is no change */
                if (t->marks && i < end_para)
                    p->mark_revision = t->rev;
            }
        }
        t->para = end_para;
        t->run = end_run;
    }
    /* Text the builder adds next would join the paragraph's last run if that has no change and the same
     * format (owf_para_add_text): seal it, so that a change begins a run of its own. owf_track_finish
     * opens it again. */
    if (rev && b->para && b->para->nruns && b->para->runs[b->para->nruns - 1].revision == 0)
        b->para->runs[b->para->nruns - 1].revision = -1;
    t->rev = rev;
    t->marks = marks;
}

int owf_track_split_para(owf_story *story, int para, int run, int rev)
{
    owf_para *p, *np;
    owf_mark marks[64];
    size_t at = 0;
    int i, n, moved;
    char *keep = NULL, *move = NULL;
    if (!story || para < 0 || para >= story->nparas)
        return 1;
    if (story->nparas == story->capparas) {
        int want = story->capparas ? story->capparas * 2 : 16;
        owf_para *grown = realloc(story->paras, (size_t)want * sizeof *grown);
        if (!grown)
            return 0;
        story->paras = grown;
        story->capparas = want;
    }
    p = &story->paras[para];
    if (run < 0)
        run = 0;
    if (run > p->nruns)
        run = p->nruns;
    moved = p->nruns - run;
    for (i = 0; i < run; ++i)
        if (p->runs[i].kind == OWF_RUN_TEXT && p->runs[i].text)
            at += strlen(p->runs[i].text);
    /* the bookmarks at or after the place go with the second part, moved back by the text before it */
    n = p->bookmarks ? owf_marks_parse(p->bookmarks, marks, 64) : 0;
    if (n) {
        owf_buf k, m;
        owf_buf_init(&k);
        owf_buf_init(&m);
        for (i = 0; i < n; ++i) {
            int second = marks[i].start > at || (marks[i].start == at && at > 0);
            owf_buf *to = second ? &m : &k;
            size_t s = marks[i].start, e = marks[i].end;
            if (to->length)
                owf_buf_putc(to, ' ');
            owf_buf_put(to, marks[i].name, marks[i].length);
            if (second) {
                s -= at;
                e = e > at ? e - at : 0;
            } else if (e > at) {
                e = at;
            }
            if (s || e)
                owf_buf_printf(to, e > s ? "@%lu-%lu" : "@%lu", (unsigned long)s, (unsigned long)e);
        }
        if (k.failed || m.failed) {
            owf_buf_free(&k);
            owf_buf_free(&m);
            return 0;
        }
        owf_buf_putc(&k, 0);
        owf_buf_putc(&m, 0);
        if (k.failed || m.failed) {
            owf_buf_free(&k);
            owf_buf_free(&m);
            return 0;
        }
        keep = k.length > 1 ? copy_string((const char *)k.data) : NULL;
        move = m.length > 1 ? copy_string((const char *)m.data) : NULL;
        if ((k.length > 1 && !keep) || (m.length > 1 && !move)) {
            free(keep);
            free(move);
            owf_buf_free(&k);
            owf_buf_free(&m);
            return 0;
        }
        owf_buf_free(&k);
        owf_buf_free(&m);
    }
    memmove(&story->paras[para + 2], &story->paras[para + 1],
            (size_t)(story->nparas - para - 1) * sizeof story->paras[0]);
    story->nparas++;
    p = &story->paras[para];
    np = &story->paras[para + 1];
    memset(np, 0, sizeof *np);
    np->fmt = p->fmt;
    np->fmt.page_break_before = 0;
    np->table_id = p->table_id;
    np->table_row = p->table_row;
    np->table_col = p->table_col;
    np->table_cols = p->table_cols;
    np->cell_shading = p->cell_shading;
    np->mark_revision = p->mark_revision;
    np->style = copy_string(p->style);
    if (moved) {
        np->runs = malloc((size_t)moved * sizeof *np->runs);
        if (!np->runs) {
            free(np->style);
            memmove(&story->paras[para + 1], &story->paras[para + 2],
                    (size_t)(story->nparas - para - 2) * sizeof story->paras[0]);
            story->nparas--;
            free(keep);
            free(move);
            return 0;
        }
        memcpy(np->runs, &p->runs[run], (size_t)moved * sizeof *np->runs);
        np->nruns = np->capruns = moved;
        p->nruns = run;
    }
    if (n) {
        free(p->bookmarks);
        p->bookmarks = keep;
        np->bookmarks = move;
    }
    p->mark_revision = rev;
    return 1;
}

static void finish_story(owf_doc *doc, owf_story *s)
{
    int i, j;
    for (i = 0; i < s->nparas; ++i) {
        owf_para *p = &s->paras[i];
        for (j = 0; j < p->nruns; ++j)
            if (p->runs[j].revision < 0 || p->runs[j].revision > doc->nrevisions)
                p->runs[j].revision = 0;
        if (p->mark_revision < 0 || p->mark_revision > doc->nrevisions)
            p->mark_revision = 0;
    }
    if (s->nparas)                                   /* the story's last end is no paragraph mark */
        s->paras[s->nparas - 1].mark_revision = 0;
}

void owf_track_finish(owf_doc *doc)
{
    int i;
    if (!doc)
        return;
    finish_story(doc, &doc->body);
    finish_story(doc, &doc->header);
    finish_story(doc, &doc->footer);
    finish_story(doc, &doc->header_left);
    finish_story(doc, &doc->footer_left);
    for (i = 0; i < doc->nframes; ++i)
        finish_story(doc, &doc->frames[i].story);
}

static int story_has_changes(const owf_story *s)
{
    int i, j;
    for (i = 0; i < s->nparas; ++i) {
        if (s->paras[i].mark_revision > 0)
            return 1;
        for (j = 0; j < s->paras[i].nruns; ++j)
            if (s->paras[i].runs[j].revision > 0)
                return 1;
    }
    return 0;
}

int owf_doc_has_changes(const owf_doc *doc)
{
    int i;
    if (!doc || !doc->nrevisions)
        return 0;
    if (story_has_changes(&doc->body) || story_has_changes(&doc->header) || story_has_changes(&doc->footer) ||
        story_has_changes(&doc->header_left) || story_has_changes(&doc->footer_left))
        return 1;
    for (i = 0; i < doc->nframes; ++i)
        if (story_has_changes(&doc->frames[i].story))
            return 1;
    return 0;
}

static int rev_kind(const owf_doc *doc, int number)
{
    const owf_revision *r = OWF_DOC_REVISION(doc, number);
    return r ? r->kind : 0;
}

/* A story with its changes accepted, sharing the runs' text: new paragraph and run arrays only. */
static int accept_story(const owf_doc *doc, const owf_story *src, owf_story *dst)
{
    int i, j;
    memset(dst, 0, sizeof *dst);
    if (!src->nparas)
        return 1;
    dst->paras = calloc((size_t)src->nparas, sizeof *dst->paras);
    if (!dst->paras)
        return 0;
    dst->capparas = src->nparas;
    for (i = 0; i < src->nparas; ++i) {
        const owf_para *sp = &src->paras[i];
        owf_para *dp;
        int joined = dst->nparas && rev_kind(doc, src->paras[i - 1].mark_revision) == OWF_REV_DELETE &&
                     src->paras[i - 1].table_id == sp->table_id;
        if (joined) {
            dp = &dst->paras[dst->nparas - 1];
        } else {
            dp = &dst->paras[dst->nparas++];
            *dp = *sp;
            dp->runs = NULL;
            dp->nruns = dp->capruns = 0;
        }
        dp->mark_revision = 0;
        if (sp->nruns) {
            owf_run *runs = realloc(dp->runs, (size_t)(dp->nruns + sp->nruns) * sizeof *runs);
            if (!runs)
                return 0;
            dp->runs = runs;
            dp->capruns = dp->nruns + sp->nruns;
            for (j = 0; j < sp->nruns; ++j) {
                if (rev_kind(doc, sp->runs[j].revision) == OWF_REV_DELETE)
                    continue;
                dp->runs[dp->nruns] = sp->runs[j];
                dp->runs[dp->nruns].revision = 0;
                dp->nruns++;
            }
        }
    }
    return 1;
}

static void free_view_story(owf_story *s)
{
    int i;
    for (i = 0; i < s->nparas; ++i)
        free(s->paras[i].runs);
    free(s->paras);
    memset(s, 0, sizeof *s);
}

void owf_doc_accepted_free(owf_doc *view)
{
    int i;
    if (!view)
        return;
    free_view_story(&view->body);
    free_view_story(&view->header);
    free_view_story(&view->footer);
    free_view_story(&view->header_left);
    free_view_story(&view->footer_left);
    for (i = 0; i < view->nframes; ++i)
        free_view_story(&view->frames[i].story);
    free(view->frames);
    free(view);
}

int owf_doc_accepted(const owf_doc *doc, owf_doc **out)
{
    owf_doc *v;
    int i;
    *out = NULL;
    v = calloc(1, sizeof *v);
    if (!v)
        return OWF_ERR_MEMORY;
    *v = *doc;
    memset(&v->body, 0, sizeof v->body);
    memset(&v->header, 0, sizeof v->header);
    memset(&v->footer, 0, sizeof v->footer);
    memset(&v->header_left, 0, sizeof v->header_left);
    memset(&v->footer_left, 0, sizeof v->footer_left);
    v->frames = NULL;
    v->nframes = v->capframes = 0;
    v->revisions = NULL;
    v->nrevisions = v->caprevisions = 0;
    if (doc->nframes) {
        v->frames = calloc((size_t)doc->nframes, sizeof *v->frames);
        if (!v->frames)
            goto fail;
        v->nframes = v->capframes = doc->nframes;
        for (i = 0; i < doc->nframes; ++i) {
            v->frames[i] = doc->frames[i];
            memset(&v->frames[i].story, 0, sizeof v->frames[i].story);
            if (!accept_story(doc, &doc->frames[i].story, &v->frames[i].story))
                goto fail;
        }
    }
    if (!accept_story(doc, &doc->body, &v->body) || !accept_story(doc, &doc->header, &v->header) ||
        !accept_story(doc, &doc->footer, &v->footer) || !accept_story(doc, &doc->header_left, &v->header_left) ||
        !accept_story(doc, &doc->footer_left, &v->footer_left))
        goto fail;
    *out = v;
    return OWF_OK;
fail:
    owf_doc_accepted_free(v);
    return OWF_ERR_MEMORY;
}

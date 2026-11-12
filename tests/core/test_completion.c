/*
 * GFOpenWrite's word completion (core 1.10): the rest of the body's most used word put in after a typed start
 * and selected; typing on replaces it, accepting keeps it; nothing for short starts, inside a word, short words,
 * while changes are recorded; case and UTF-8 letters; one Undo.
 * make completion-test, make completion-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"
#include "owf_internal.h"

static int checks, failures;

#define CHECK(cond, what)                                                                                  \
    do {                                                                                                    \
        ++checks;                                                                                           \
        if (!(cond)) {                                                                                      \
            ++failures;                                                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);                                            \
        }                                                                                                   \
    } while (0)

static const char *text_of(const owf_doc *doc)
{
    static char out[2048];
    size_t used = 0;
    int i, j;
    out[0] = 0;
    for (i = 0; i < doc->body.nparas; ++i) {
        for (j = 0; j < doc->body.paras[i].nruns; ++j) {
            const owf_run *r = &doc->body.paras[i].runs[j];
            size_t n;
            if (r->kind != OWF_RUN_TEXT || !r->text) continue;
            n = strlen(r->text);
            if (used + n + 2 < sizeof out) { memcpy(out + used, r->text, n); used += n; }
        }
        if (i + 1 < doc->body.nparas && used + 2 < sizeof out) out[used++] = '|';
        out[used] = 0;
    }
    return out;
}

static owf_doc *doc_with(const char *first)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    if (!doc || !(p = owf_story_add(&doc->body, &pf)) || owf_para_add_text(p, &cf, first, strlen(first)) != OWF_OK ||
        !(p = owf_story_add(&doc->body, &pf)) || owf_para_add_text(p, &cf, "", 0) != OWF_OK) {
        owf_doc_free(doc);
        return NULL;
    }
    return doc;
}

static void caret_in_last(ow_editor *ed)
{
    owf_doc *doc = ow_editor_document(ed);
    owf_para *p = &doc->body.paras[doc->body.nparas - 1];
    ow_selection sel;
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = doc->body.nparas - 1;
    sel.anchor.run = p->nruns > 0 ? p->nruns - 1 : 0;
    sel.anchor.byte_offset = p->nruns > 0 && p->runs[p->nruns - 1].text ? strlen(p->runs[p->nruns - 1].text) : 0;
    sel.focus = sel.anchor;
    ow_editor_set_selection(ed, &sel);
}

/* types s a letter at a time, completing after each as the program does; the last result */
static int type(ow_editor *ed, const char *s)
{
    int last = 0;
    while (*s) {
        size_t n = 1;
        if ((unsigned char)*s >= 0xC0) while (((unsigned char)s[n] & 0xC0) == 0x80) ++n;
        ow_editor_insert_utf8(ed, s, n);
        last = ow_editor_complete(ed, 6);
        s += n;
    }
    return last;
}

static char *selected(ow_editor *ed)
{
    size_t len = 0;
    char *t = ow_editor_selection_text(ed, &len);
    return t;
}

int main(void)
{
    const char *body = "Paragraphs and paragraphing, a parade of Parliament. Paragraphs again; zone z\xc3\xa9nith z\xc3\xa9nithal.";
    owf_doc *doc = doc_with(body);
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    char *sel;
    if (!ed) { printf("FAIL editor\n"); return 1; }
    caret_in_last(ed);
    CHECK(type(ed, "pa") == 0 && ow_editor_selection_empty(ed), "two letters: nothing");
    CHECK(type(ed, "r") == 1, "three letters");
    sel = selected(ed);
    CHECK(sel && !strcmp(sel, "agraphs"), sel ? sel : "none");   /* Paragraphs twice beats paragraphing and Parliament */
    free(sel);
    CHECK(!strcmp(text_of(doc) + strlen(body) + 1, "paragraphs"), text_of(doc));
    type(ed, "l");                               /* typing on replaces it, and offers again */
    sel = selected(ed);
    CHECK(sel && !strcmp(sel, "iament"), sel ? sel : "none");
    free(sel);
    CHECK(ow_editor_complete_accept(ed) == 1 && ow_editor_selection_empty(ed), "accepted");
    CHECK(!strcmp(text_of(doc) + strlen(body) + 1, "parliament"), text_of(doc));
    CHECK(ow_editor_complete_accept(ed) == 0, "nothing to accept");
    ow_editor_insert_utf8(ed, " ", 1);
    CHECK(type(ed, "zon") == 0, "zone is too short");
    ow_editor_insert_utf8(ed, " ", 1);
    CHECK(type(ed, "z\xc3\xa9n") == 1, "UTF-8 letters");
    sel = selected(ed);
    CHECK(sel && !strcmp(sel, "ith"), sel ? sel : "none");
    free(sel);
    CHECK(ow_editor_undo(ed) == 1 && ow_editor_selection_empty(ed) &&
          strstr(text_of(doc), "parliament zon z\xc3\xa9n") && !strstr(text_of(doc), "z\xc3\xa9n z") , text_of(doc));
    ow_editor_insert_utf8(ed, " ", 1);
    CHECK(type(ed, "xyz") == 0, "no such word");
    {   /* inside a word: nothing */
        ow_selection s;
        ow_editor_insert_utf8(ed, " parqet", 7);
        s = ow_editor_selection(ed);
        s.focus.byte_offset -= 3;
        s.anchor = s.focus;
        ow_editor_set_selection(ed, &s);
        CHECK(ow_editor_complete(ed, 6) == 0, "inside a word");
    }
    caret_in_last(ed);
    ow_editor_set_track_changes(ed, 1);
    ow_editor_insert_utf8(ed, " ", 1);
    CHECK(type(ed, "par") == 0, "not while recording");
    ow_editor_free(ed);
    owf_doc_free(doc);
    printf("completion: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

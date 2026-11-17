/*
 * GFOpenWrite's spelling: the next misspelt word of a document, selected.
 * The dictionary and the word rules are libowf/src/owf_spell.c (owfspell.library).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ow_spell.h"

#include <string.h>

static int scan_run(owf_spell *s, ow_editor *editor, int pi, int ri,
                    size_t from, size_t to, char *word, size_t word_size)
{
    const owf_doc *doc = ow_editor_document_const(editor);
    const char *text = doc->body.paras[pi].runs[ri].text;
    size_t n = text ? strlen(text) : 0, a, b;
    if (to > n) to = n;
    if (!owf_spell_scan(s, text, from, to, &a, &b)) return 0;
    {
        ow_selection sel;
        sel.anchor.paragraph = sel.focus.paragraph = pi;
        sel.anchor.run = sel.focus.run = ri;
        sel.anchor.byte_offset = a; sel.focus.byte_offset = b;
        ow_editor_set_selection(editor, &sel);
        if (word && word_size) {
            size_t m = b - a < word_size - 1 ? b - a : word_size - 1;
            memcpy(word, text + a, m); word[m] = 0;
        }
    }
    return 1;
}

int ow_spell_next(owf_spell *s, ow_editor *editor, char *word,
                  size_t word_size, int wrap)
{
    const owf_doc *doc;
    ow_selection sel;
    int pass, pi, ri;
    if (!s || !editor || !(doc = ow_editor_document_const(editor))) return -1;
    sel = ow_editor_selection(editor);
    for (pass = 0; pass < (wrap ? 2 : 1); ++pass) {
        int pstart = pass ? 0 : sel.focus.paragraph;
        int pend = pass ? sel.focus.paragraph + 1 : doc->body.nparas;
        for (pi = pstart; pi < pend && pi < doc->body.nparas; ++pi) {
            const owf_para *p = &doc->body.paras[pi];
            for (ri = 0; ri < p->nruns; ++ri) {
                const owf_run *r = &p->runs[ri];
                size_t from = 0, to;
                if (r->kind != OWF_RUN_TEXT || !r->text) continue;
                to = strlen(r->text);
                if (!pass && pi == sel.focus.paragraph && ri < sel.focus.run) continue;
                if (!pass && pi == sel.focus.paragraph && ri == sel.focus.run)
                    from = sel.focus.byte_offset;
                if (pass && pi == sel.focus.paragraph) {
                    if (ri > sel.focus.run) continue;
                    if (ri == sel.focus.run) to = sel.focus.byte_offset;
                }
                if (scan_run(s, editor, pi, ri, from, to, word, word_size)) return 1;
            }
        }
    }
    return 0;
}


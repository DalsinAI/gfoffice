#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"

static int text_calls, text_run_calls;

static void on_text(void *ud, int x, int y, const char *utf8,
                    const owf_charfmt *fmt)
{
    (void)ud; (void)x; (void)y; (void)fmt;
    if (utf8) ++text_calls;
}

static void on_text_run(void *ud, int paragraph, int run, int x, int y,
                        const char *utf8, const owf_charfmt *fmt)
{
    (void)ud; (void)paragraph; (void)run; (void)x; (void)y; (void)fmt;
    if (utf8) ++text_run_calls;
}

static owf_doc *doc_with(const char *text)
{
    owf_doc *doc = owf_doc_new();
    owf_para *p;
    owf_parafmt pf;
    owf_charfmt cf;
    if (!doc) return NULL;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    p = owf_story_add(&doc->body, &pf);
    if (!p) { owf_doc_free(doc); return NULL; }
    if (text && *text && owf_para_add_text(p, &cf, text, strlen(text)) != OWF_OK) {
        owf_doc_free(doc);
        return NULL;
    }
    return doc;
}

static const char *run_text(const owf_doc *doc, int p, int r)
{
    if (!doc || p < 0 || p >= doc->body.nparas) return NULL;
    if (r < 0 || r >= doc->body.paras[p].nruns) return NULL;
    return doc->body.paras[p].runs[r].text;
}


static void paragraph_text(const owf_doc *doc, int p, char *out, size_t size)
{
    int r;
    size_t used = 0;
    if (!size) return;
    out[0] = 0;
    if (!doc || p < 0 || p >= doc->body.nparas) return;
    for (r = 0; r < doc->body.paras[p].nruns; ++r) {
        const owf_run *run = &doc->body.paras[p].runs[r];
        size_t n;
        if (run->kind != OWF_RUN_TEXT || !run->text) continue;
        n = strlen(run->text);
        if (n > size - used - 1) n = size - used - 1;
        memcpy(out + used, run->text, n);
        used += n;
        out[used] = 0;
        if (used + 1 >= size) break;
    }
}

static int test_render(void)
{
    owf_doc *doc = doc_with("OpenWrite");
    ow_editor *ed;
    ow_renderer r;
    if (!doc) return 101;
    ed = ow_editor_new(doc);
    if (!ed) return 102;
    if (ow_editor_zoom(ed) != 100) return 103;
    ow_editor_set_zoom(ed, 10);
    if (ow_editor_zoom(ed) != 25) return 104;
    if (ow_editor_layout(ed) != OWF_OK) return 105;
    if (ow_editor_page_count(ed) != 1) return 106;

    memset(&r, 0, sizeof(r));
    r.text = on_text;
    if (ow_editor_render_page(ed, 0, &r) != OWF_OK) return 107;
    if (text_calls != 1) return 108;

    memset(&r, 0, sizeof(r));
    r.text_run = on_text_run;
    if (ow_editor_render_page(ed, 0, &r) != OWF_OK) return 109;
    if (text_run_calls != 1) return 110;

    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int test_typing_undo_redo(void)
{
    owf_doc *doc = doc_with("hello");
    ow_editor *ed;
    if (!doc) return 201;
    ed = ow_editor_new(doc);
    if (!ed) return 202;

    ow_editor_move_caret(ed, OW_MOVE_END, 0);
    if (ow_editor_insert_utf8(ed, " ", 1) != OWF_OK) return 203;
    if (ow_editor_insert_utf8(ed, "w", 1) != OWF_OK) return 204;
    if (ow_editor_insert_utf8(ed, "o", 1) != OWF_OK) return 205;
    if (ow_editor_insert_utf8(ed, "rld", 3) != OWF_OK) return 206;
    if (strcmp(run_text(doc, 0, 0), "hello world")) return 207;
    if (!ow_editor_can_undo(ed)) return 208;
    if (ow_editor_undo(ed) != 1) return 209;
    if (strcmp(run_text(doc, 0, 0), "hello")) return 210;
    if (!ow_editor_can_redo(ed)) return 211;
    if (ow_editor_redo(ed) != 1) return 212;
    if (strcmp(run_text(doc, 0, 0), "hello world")) return 213;
    if (!ow_editor_is_dirty(ed)) return 214;
    ow_editor_mark_saved(ed);
    if (ow_editor_is_dirty(ed)) return 215;

    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int test_selection_replace(void)
{
    owf_doc *doc = doc_with("abcdef");
    ow_editor *ed;
    ow_selection s;
    if (!doc) return 301;
    ed = ow_editor_new(doc);
    if (!ed) return 302;

    memset(&s, 0, sizeof(s));
    s.anchor.paragraph = s.focus.paragraph = 0;
    s.anchor.run = s.focus.run = 0;
    s.anchor.byte_offset = 1;
    s.focus.byte_offset = 4;
    ow_editor_set_selection(ed, &s);
    if (ow_editor_selection_empty(ed)) return 303;
    if (ow_editor_insert_utf8(ed, "X", 1) != OWF_OK) return 304;
    if (strcmp(run_text(doc, 0, 0), "aXef")) return 305;
    if (ow_editor_undo(ed) != 1) return 306;
    if (strcmp(run_text(doc, 0, 0), "abcdef")) return 307;

    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int test_newline_and_merge(void)
{
    owf_doc *doc = doc_with("abcdef");
    ow_editor *ed;
    ow_selection s;
    if (!doc) return 401;
    ed = ow_editor_new(doc);
    if (!ed) return 402;

    memset(&s, 0, sizeof(s));
    s.anchor.paragraph = s.focus.paragraph = 0;
    s.anchor.run = s.focus.run = 0;
    s.anchor.byte_offset = s.focus.byte_offset = 3;
    ow_editor_set_selection(ed, &s);
    if (ow_editor_newline(ed) != OWF_OK) return 403;
    if (doc->body.nparas != 2) return 404;
    if (strcmp(run_text(doc, 0, 0), "abc")) return 405;
    if (strcmp(run_text(doc, 1, 0), "def")) return 406;

    if (ow_editor_backspace(ed) != OWF_OK) return 407;
    if (doc->body.nparas != 1) return 408;
    { char all[64]; paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "abcdef")) return 409; }

    if (ow_editor_undo(ed) != 1) return 410;
    if (doc->body.nparas != 2) return 411;
    if (strcmp(run_text(doc, 0, 0), "abc")) return 412;
    if (strcmp(run_text(doc, 1, 0), "def")) return 413;

    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int test_utf8_backspace(void)
{
    owf_doc *doc = doc_with("A\xC3\xA9" "B");
    ow_editor *ed;
    if (!doc) return 501;
    ed = ow_editor_new(doc);
    if (!ed) return 502;
    ow_editor_move_caret(ed, OW_MOVE_END, 0);
    if (ow_editor_backspace(ed) != OWF_OK) return 503;
    if (strcmp(run_text(doc, 0, 0), "A\xC3\xA9")) return 504;
    if (ow_editor_backspace(ed) != OWF_OK) return 505;
    if (strcmp(run_text(doc, 0, 0), "A")) return 506;
    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int test_cross_paragraph_selection(void)
{
    owf_doc *doc = doc_with("alpha");
    ow_editor *ed;
    owf_para *p;
    owf_parafmt pf;
    owf_charfmt cf;
    ow_selection s;
    if (!doc) return 601;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    p = owf_story_add(&doc->body, &pf);
    if (!p || owf_para_add_text(p, &cf, "beta", 4) != OWF_OK) return 602;

    ed = ow_editor_new(doc);
    if (!ed) return 603;
    memset(&s, 0, sizeof(s));
    s.anchor.paragraph = 0; s.anchor.run = 0; s.anchor.byte_offset = 2;
    s.focus.paragraph = 1; s.focus.run = 0; s.focus.byte_offset = 2;
    ow_editor_set_selection(ed, &s);
    if (ow_editor_insert_utf8(ed, "X", 1) != OWF_OK) return 604;
    if (doc->body.nparas != 1) return 605;
    { char all[64]; paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "alXta")) return 606; }
    if (ow_editor_undo(ed) != 1) return 607;
    if (doc->body.nparas != 2) return 608;
    if (strcmp(run_text(doc, 0, 0), "alpha")) return 609;
    if (strcmp(run_text(doc, 1, 0), "beta")) return 610;

    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

int main(void)
{
    int rc;
    if ((rc = test_render())) goto fail;
    if ((rc = test_typing_undo_redo())) goto fail;
    if ((rc = test_selection_replace())) goto fail;
    if ((rc = test_newline_and_merge())) goto fail;
    if ((rc = test_utf8_backspace())) goto fail;
    if ((rc = test_cross_paragraph_selection())) goto fail;
    puts("openwrite core editor tests passed");
    return 0;
fail:
    fprintf(stderr, "openwrite core editor test failed: %d\n", rc);
    return rc;
}

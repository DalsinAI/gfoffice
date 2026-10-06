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


static int test_character_formatting(void)
{
    owf_doc *doc = doc_with("abcdef");
    ow_editor *ed;
    ow_selection s;
    owf_charfmt cf;
    char *copied;
    size_t copied_len = 0;
    if (!doc) return 701;
    ed = ow_editor_new(doc);
    if (!ed) return 702;

    memset(&s, 0, sizeof(s));
    s.anchor.paragraph = s.focus.paragraph = 0;
    s.anchor.run = s.focus.run = 0;
    s.anchor.byte_offset = 1;
    s.focus.byte_offset = 3;
    ow_editor_set_selection(ed, &s);
    if (ow_editor_toggle_char_flags(ed, OWF_BOLD) != OWF_OK) return 703;
    if (doc->body.paras[0].nruns != 3) return 704;
    if (strcmp(run_text(doc, 0, 0), "a")) return 705;
    if (strcmp(run_text(doc, 0, 1), "bc")) return 706;
    if (!(doc->body.paras[0].runs[1].fmt.flags & OWF_BOLD)) return 707;
    if (strcmp(run_text(doc, 0, 2), "def")) return 708;
    copied = ow_editor_selection_text(ed, &copied_len);
    if (!copied || copied_len != 2 || strcmp(copied, "bc")) return 709;
    free(copied);
    if (ow_editor_undo(ed) != 1) return 710;
    { char all[64]; paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "abcdef")) return 711; }

    ow_editor_move_caret(ed, OW_MOVE_END, 0);
    if (!ow_editor_current_charfmt(ed, &cf)) return 712;
    cf.flags |= OWF_ITALIC;
    if (ow_editor_apply_charfmt(ed, &cf, OW_CHARFMT_FLAGS) != OWF_OK) return 713;
    if (ow_editor_insert_utf8(ed, "X", 1) != OWF_OK) return 714;
    { char all[64]; paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "abcdefX")) return 715; }
    if (!(doc->body.paras[0].runs[doc->body.paras[0].nruns - 1].fmt.flags & OWF_ITALIC)) return 716;

    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int test_paragraph_formatting_and_select_all(void)
{
    owf_doc *doc = doc_with("alpha");
    ow_editor *ed;
    owf_para *p;
    owf_parafmt pf, set;
    owf_charfmt cf;
    char *text;
    size_t n = 0;
    if (!doc) return 801;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    p = owf_story_add(&doc->body, &pf);
    if (!p || owf_para_add_text(p, &cf, "beta", 4) != OWF_OK) return 802;
    ed = ow_editor_new(doc);
    if (!ed) return 803;

    ow_editor_select_all(ed);
    text = ow_editor_selection_text(ed, &n);
    if (!text || n != 10 || strcmp(text, "alpha\nbeta")) return 804;
    free(text);
    if (!ow_editor_current_parafmt(ed, &set)) return 805;
    set.align = OWF_ALIGN_CENTRE;
    set.heading = 2;
    if (ow_editor_apply_parafmt(ed, &set, OW_PARAFMT_ALIGN | OW_PARAFMT_HEADING) != OWF_OK) return 806;
    if (doc->body.paras[0].fmt.align != OWF_ALIGN_CENTRE || doc->body.paras[1].fmt.align != OWF_ALIGN_CENTRE) return 807;
    if (doc->body.paras[0].fmt.heading != 2 || doc->body.paras[1].fmt.heading != 2) return 808;
    if (ow_editor_undo(ed) != 1) return 809;
    if (doc->body.paras[0].fmt.align != OWF_ALIGN_LEFT || doc->body.paras[1].fmt.align != OWF_ALIGN_LEFT) return 810;

    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}


static int test_text_block_paste(void)
{
    owf_doc *doc = doc_with("one");
    ow_editor *ed;
    if (!doc) return 901;
    ed = ow_editor_new(doc);
    if (!ed) return 902;
    ow_editor_move_caret(ed, OW_MOVE_END, 0);
    if (ow_editor_insert_text_block(ed, "\ntwo\r\nthree", 11) != OWF_OK) return 903;
    if (doc->body.nparas != 3) return 904;
    if (strcmp(run_text(doc, 0, 0), "one")) return 905;
    if (strcmp(run_text(doc, 1, 0), "two")) return 906;
    if (strcmp(run_text(doc, 2, 0), "three")) return 907;
    if (ow_editor_undo(ed) != 1) return 908;
    if (doc->body.nparas != 1) return 909;
    if (strcmp(run_text(doc, 0, 0), "one")) return 910;
    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}


static int test_lists(void)
{
    owf_doc *doc = doc_with("alpha");
    ow_editor *ed;
    owf_para *p;
    owf_parafmt pf;
    owf_charfmt cf;
    char all[64];
    if (!doc) return 1001;
    owf_parafmt_init(&pf); owf_charfmt_init(&cf);
    p = owf_story_add(&doc->body, &pf);
    if (!p || owf_para_add_text(p, &cf, "beta", 4) != OWF_OK) return 1002;
    ed = ow_editor_new(doc);
    if (!ed) return 1003;
    ow_editor_select_all(ed);
    if (ow_editor_toggle_list(ed, 0) != OWF_OK) return 1004;
    paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "\xe2\x80\xa2 alpha")) return 1005;
    paragraph_text(doc, 1, all, sizeof all); if (strcmp(all, "\xe2\x80\xa2 beta")) return 1006;
    if (doc->body.paras[0].fmt.indent_left != 360 || doc->body.paras[0].fmt.indent_first != -360) return 1007;
    if (ow_editor_toggle_list(ed, 0) != OWF_OK) return 1008;
    paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "alpha")) return 1009;
    paragraph_text(doc, 1, all, sizeof all); if (strcmp(all, "beta")) return 1010;
    if (ow_editor_toggle_list(ed, 1) != OWF_OK) return 1011;
    paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "1. alpha")) return 1012;
    paragraph_text(doc, 1, all, sizeof all); if (strcmp(all, "2. beta")) return 1013;
    if (ow_editor_undo(ed) != 1) return 1014;
    paragraph_text(doc, 0, all, sizeof all); if (strcmp(all, "alpha")) return 1015;
    ow_editor_free(ed); owf_doc_free(doc); return 0;
}


static int test_find(void)
{
    owf_doc *doc = doc_with("Alpha beta alpha");
    ow_editor *ed;
    char *t;
    size_t n;
    if (!doc) return 1101;
    ed = ow_editor_new(doc);
    if (!ed) return 1102;
    if (ow_editor_find(ed, "alpha", 0, 0, 1) != 1) return 1103;
    t = ow_editor_selection_text(ed, &n);
    if (!t || strcmp(t, "Alpha")) return 1104;
    free(t);
    if (ow_editor_find(ed, "alpha", 0, 0, 1) != 1) return 1105;
    t = ow_editor_selection_text(ed, &n);
    if (!t || strcmp(t, "alpha")) return 1106;
    free(t);
    if (ow_editor_find(ed, "Alpha", 0, 1, 1) != 1) return 1107;
    t = ow_editor_selection_text(ed, &n);
    if (!t || strcmp(t, "Alpha")) return 1108;
    free(t);
    if (ow_editor_find(ed, "beta", 1, 0, 1) != 1) return 1109;
    t = ow_editor_selection_text(ed, &n);
    if (!t || strcmp(t, "beta")) return 1110;
    free(t);
    ow_editor_free(ed); owf_doc_free(doc); return 0;
}


static int test_automatic_pagination(void)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    ow_editor *ed;
    ow_renderer r;
    int i, pages;
    const char *line = "This is a reasonably long paragraph used to exercise automatic page layout in OpenWrite. It should wrap onto several visual lines on an A4 page.";
    if (!doc) return 1201;
    owf_parafmt_init(&pf); owf_charfmt_init(&cf);
    for (i = 0; i < 40; ++i) {
        owf_para *p = owf_story_add(&doc->body, &pf);
        if (!p || owf_para_add_text(p, &cf, line, strlen(line)) != OWF_OK) return 1202;
    }
    ed = ow_editor_new(doc);
    if (!ed) return 1203;
    if (ow_editor_layout(ed) != OWF_OK) return 1204;
    pages = ow_editor_page_count(ed);
    if (pages < 2) return 1205;
    memset(&r, 0, sizeof r); r.text_run = on_text_run;
    text_run_calls = 0;
    if (ow_editor_render_page(ed, 0, &r) != OWF_OK) return 1206;
    if (text_run_calls <= 0 || text_run_calls >= 40) return 1207;
    text_run_calls = 0;
    if (ow_editor_render_page(ed, 1, &r) != OWF_OK) return 1208;
    if (text_run_calls <= 0) return 1209;
    ow_editor_free(ed); owf_doc_free(doc); return 0;
}


static int check_roundtrip_format(const char *path)
{
    owf_doc *doc = doc_with("Formatted document");
    ow_editor *ed;
    owf_charfmt cf;
    owf_parafmt pf;
    owf_doc *back = NULL;
    const owf_format *used = NULL;
    int rc = 0;
    if (!doc) return 1;
    ed = ow_editor_new(doc);
    if (!ed) { owf_doc_free(doc); return 2; }
    ow_editor_select_all(ed);
    if (!ow_editor_current_charfmt(ed, &cf)) { rc = 3; goto out; }
    cf.flags |= OWF_BOLD | OWF_ITALIC;
    cf.size = 18 * OWF_TWIPS_PER_POINT;
    if (ow_editor_apply_charfmt(ed, &cf, OW_CHARFMT_FLAGS | OW_CHARFMT_SIZE) != OWF_OK) { rc = 4; goto out; }
    if (!ow_editor_current_parafmt(ed, &pf)) { rc = 5; goto out; }
    pf.align = OWF_ALIGN_CENTRE;
    if (ow_editor_apply_parafmt(ed, &pf, OW_PARAFMT_ALIGN) != OWF_OK) { rc = 6; goto out; }
    if (owf_export_file(doc, path, NULL, NULL) != OWF_OK) { rc = 7; goto out; }
    if (owf_import_file(path, NULL, &back, NULL, &used) != OWF_OK || !back) { rc = 8; goto out; }
    if (back->body.nparas < 1 || back->body.paras[0].nruns < 1) { rc = 9; goto out; }
    if (strcmp(back->body.paras[0].runs[0].text, "Formatted document")) { rc = 10; goto out; }
    if (!(back->body.paras[0].runs[0].fmt.flags & OWF_BOLD)) { rc = 11; goto out; }
    if (!(back->body.paras[0].runs[0].fmt.flags & OWF_ITALIC)) { rc = 12; goto out; }
    if (back->body.paras[0].runs[0].fmt.size != 18 * OWF_TWIPS_PER_POINT) { rc = 13; goto out; }
    if (back->body.paras[0].fmt.align != OWF_ALIGN_CENTRE) { rc = 14; goto out; }
out:
    if (back) owf_doc_free(back);
    ow_editor_free(ed);
    owf_doc_free(doc);
    remove(path);
    return rc;
}

static int test_modern_roundtrip(void)
{
    int rc;
    rc = check_roundtrip_format("/tmp/openwrite-core-format.docx");
    if (rc) return 1300 + rc;
    rc = check_roundtrip_format("/tmp/openwrite-core-format.odt");
    if (rc) return 1320 + rc;
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
    if ((rc = test_character_formatting())) goto fail;
    if ((rc = test_paragraph_formatting_and_select_all())) goto fail;
    if ((rc = test_text_block_paste())) goto fail;
    if ((rc = test_lists())) goto fail;
    if ((rc = test_find())) goto fail;
    if ((rc = test_automatic_pagination())) goto fail;
    if ((rc = test_modern_roundtrip())) goto fail;
    puts("openwrite core editor tests passed");
    return 0;
fail:
    fprintf(stderr, "openwrite core editor test failed: %d\n", rc);
    return rc;
}

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


/* the position of a byte offset in a paragraph's text: the run it falls in, or the end of the one before */
static ow_position position_at(const owf_doc *doc, int p, size_t offset)
{
    ow_position pos;
    size_t at = 0;
    int r;
    memset(&pos, 0, sizeof pos);
    pos.paragraph = p;
    for (r = 0; r < doc->body.paras[p].nruns; ++r) {
        const owf_run *run = &doc->body.paras[p].runs[r];
        size_t n = run->kind == OWF_RUN_TEXT && run->text ? strlen(run->text) : 0;
        if (run->kind != OWF_RUN_TEXT) continue;
        pos.run = r;
        if (offset <= at + n) { pos.byte_offset = offset - at; return pos; }
        pos.byte_offset = n;
        at += n;
    }
    return pos;
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

    /* 2.0.13: the runs before and after the selection are kept as they were (those after it were
     * written twice, from before their start, and those before it ran past their end) */
    {
        static const int spans[][3] = { { 1, 3, 1 }, { 0, 1, 1 }, { 4, 6, 1 }, { 2, 5, 0 } };
        char all[64];
        int k;
        doc = doc_with("abcdef");
        if (!doc) return 717;
        ed = ow_editor_new(doc);
        if (!ed) return 718;
        for (k = 0; k < 4; ++k) {
            memset(&s, 0, sizeof(s));
            s.anchor = position_at(doc, 0, (size_t)spans[k][0]);
            s.focus = position_at(doc, 0, (size_t)spans[k][1]);
            ow_editor_set_selection(ed, &s);
            owf_charfmt_init(&cf);
            cf.flags = spans[k][2] ? OWF_ITALIC : 0;
            if (ow_editor_apply_charfmt(ed, &cf, OW_CHARFMT_FLAGS) != OWF_OK) return 719;
            paragraph_text(doc, 0, all, sizeof all);
            if (strcmp(all, "abcdef")) return 720 + k;
        }
        /* a b italic, c d e plain again, f italic */
        if (doc->body.paras[0].nruns != 3 || strcmp(run_text(doc, 0, 0), "ab") ||
            strcmp(run_text(doc, 0, 1), "cde") || strcmp(run_text(doc, 0, 2), "f") ||
            !(doc->body.paras[0].runs[0].fmt.flags & OWF_ITALIC) ||
            (doc->body.paras[0].runs[1].fmt.flags & OWF_ITALIC) ||
            !(doc->body.paras[0].runs[2].fmt.flags & OWF_ITALIC))
            return 724;
        ow_editor_free(ed);
        owf_doc_free(doc);
    }
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


static int check_rich_roundtrip(const char *path)
{
    static const unsigned char fake_png[]={0x89,'P','N','G','\r','\n',0x1a,'\n',1,2,3,4};
    owf_doc *doc=owf_doc_new(),*back=NULL;owf_parafmt pf;owf_charfmt cf;owf_para *p;const owf_format *used=NULL;int i,img,links=0,cells=0,images=0,rc=0;
    if(!doc)return 1;
    owf_parafmt_init(&pf);owf_charfmt_init(&cf);
    p=owf_story_add(&doc->body,&pf);if(!p||owf_para_add_link_text(p,&cf,"OpenWrite",9,"https://amigachrome.example/openwrite")!=OWF_OK){rc=2;goto out;}
    for(i=0;i<4;++i){p=owf_story_add(&doc->body,&pf);if(!p||owf_para_add_text(p,&cf,"cell",4)!=OWF_OK){rc=3;goto out;}p->table_id=0;p->table_row=i/2;p->table_col=i%2;p->table_cols=2;}
    img=owf_doc_add_image(doc,"tiny.png","image/png",fake_png,sizeof fake_png,1440,960,"tiny image");if(img<0){rc=4;goto out;}p=owf_story_add(&doc->body,&pf);if(!p||owf_para_add_image(p,&cf,img)!=OWF_OK){rc=5;goto out;}
    if(owf_export_file(doc,path,NULL,NULL)!=OWF_OK){rc=6;goto out;}if(owf_import_file(path,NULL,&back,NULL,&used)!=OWF_OK||!back){rc=7;goto out;}
    for(i=0;i<back->body.nparas;++i){int j;const owf_para *q=&back->body.paras[i];if(q->table_id>=0)++cells;for(j=0;j<q->nruns;++j){if(q->runs[j].href&&!strcmp(q->runs[j].href,"https://amigachrome.example/openwrite"))++links;if(q->runs[j].kind==OWF_RUN_IMAGE)++images;}}
    if(!links){rc=8;goto out;}if(cells<4){rc=9;goto out;}if(back->nimages<1||!images){rc=10;goto out;}if(back->images[0].length!=sizeof fake_png){rc=11;goto out;}
out:if(back)owf_doc_free(back);owf_doc_free(doc);if(!rc)remove(path);return rc;
}

static int test_table_editing(void)
{
    owf_doc *doc=doc_with("before after");ow_editor *ed;ow_selection sel;int i,cells=0;
    if(!doc)return 1651;
    ed=ow_editor_new(doc);if(!ed)return 1652;
    memset(&sel,0,sizeof sel);sel.anchor.paragraph=sel.focus.paragraph=0;sel.anchor.run=sel.focus.run=0;sel.anchor.byte_offset=sel.focus.byte_offset=6;ow_editor_set_selection(ed,&sel);
    if(ow_editor_insert_table(ed,2,2)!=OWF_OK)return 1653;
    for(i=0;i<doc->body.nparas;++i)if(doc->body.paras[i].table_id>=0)++cells;
    if(cells!=4)return 1654;
    if(!ow_editor_in_table(ed))return 1655;
    if(ow_editor_insert_utf8(ed,"A",1)!=OWF_OK)return 1656;
    if(!ow_editor_table_move(ed,1))return 1657;
    if(ow_editor_table_insert_row(ed)!=OWF_OK)return 1658;
    cells=0;for(i=0;i<doc->body.nparas;++i)if(doc->body.paras[i].table_id>=0)++cells;
    if(cells!=6)return 1659;
    if(ow_editor_table_insert_column(ed)!=OWF_OK)return 1660;
    cells=0;for(i=0;i<doc->body.nparas;++i)if(doc->body.paras[i].table_id>=0)++cells;
    if(cells!=9)return 1661;
    if(ow_editor_table_delete_column(ed)!=OWF_OK)return 1662;
    cells=0;for(i=0;i<doc->body.nparas;++i)if(doc->body.paras[i].table_id>=0)++cells;
    if(cells!=6)return 1663;
    if(ow_editor_table_delete_row(ed)!=OWF_OK)return 1664;
    cells=0;for(i=0;i<doc->body.nparas;++i)if(doc->body.paras[i].table_id>=0)++cells;
    if(cells!=4)return 1665;
    if(ow_editor_undo(ed)!=1)return 1666;
    cells=0;for(i=0;i<doc->body.nparas;++i)if(doc->body.paras[i].table_id>=0)++cells;
    if(cells!=6)return 1667;
    ow_editor_free(ed);owf_doc_free(doc);return 0;
}

static int test_rich_roundtrip(void)
{
    int rc=check_rich_roundtrip("/tmp/openwrite-rich.docx");if(rc)return 1600+rc;rc=check_rich_roundtrip("/tmp/openwrite-rich.odt");if(rc)return 1620+rc;return 0;
}

static int test_page_setup_and_break(void)
{
    owf_doc *doc = doc_with("before after");
    ow_editor *ed;
    ow_selection sel;
    owf_page pg;
    if (!doc) return 1401;
    ed = ow_editor_new(doc);
    if (!ed) return 1402;
    ow_editor_page_setup(ed, &pg);
    pg.width = 15840; pg.height = 12240;
    pg.margin_left = pg.margin_right = 720;
    pg.margin_top = pg.margin_bottom = 720;
    if (ow_editor_apply_page_setup(ed, &pg) != OWF_OK) return 1403;
    if (doc->page.width != 15840 || doc->page.height != 12240 || doc->page.margin_left != 720) return 1404;
    if (ow_editor_undo(ed) != 1) return 1405;
    if (doc->page.width != 11906 || doc->page.height != 16838) return 1406;

    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = sel.focus.paragraph = 0;
    sel.anchor.run = sel.focus.run = 0;
    sel.anchor.byte_offset = sel.focus.byte_offset = 6;
    ow_editor_set_selection(ed, &sel);
    if (ow_editor_insert_page_break(ed) != OWF_OK) return 1407;
    if (doc->body.nparas != 2) return 1408;
    if (!doc->body.paras[1].fmt.page_break_before) return 1409;
    if (ow_editor_page_count(ed) < 2) return 1410;
    if (ow_editor_current_page(ed) != 1) return 1411;
    if (ow_editor_undo(ed) != 1) return 1412;
    if (doc->body.nparas != 1) return 1413;
    ow_editor_free(ed); owf_doc_free(doc); return 0;
}


/* 2.0.10: a Word cover's frames (tests/core/fixtures/cover.docx) through typing in their paragraph, whose runs
 * are all frames, undo and redo: the undo copy holds them and their text boxes' paragraphs. */
static int test_frames_undo(void)
{
    owf_doc *doc = NULL;
    ow_editor *ed;
    ow_selection sel;
    int i, frames = 0;
    if (owf_import_file("tests/core/fixtures/cover.docx", NULL, &doc, NULL, NULL) != OWF_OK || !doc) return 1901;
    if (doc->nframes != 6 || doc->frames[1].story.nparas != 1 || doc->body.paras[0].nruns != 5) return 1902;
    ed = ow_editor_new(doc);
    if (!ed) return 1903;
    memset(&sel, 0, sizeof sel);
    ow_editor_set_selection(ed, &sel);
    if (ow_editor_insert_utf8(ed, "x", 1) != OWF_OK) return 1904;
    for (i = 0; i < doc->body.paras[0].nruns; ++i) frames += doc->body.paras[0].runs[i].kind == OWF_RUN_FRAME;
    if (frames != 5 || doc->body.paras[0].nruns != 6) return 1905;
    if (ow_editor_undo(ed) != 1) return 1906;
    if (doc->nframes != 6 || doc->frames[1].story.nparas != 1 || doc->frames[1].story.paras[0].nruns != 1 ||
        strcmp(doc->frames[1].story.paras[0].runs[0].text, "Cover Title"))
        return 1907;
    /* the frames as they were (and the empty text run the caret was given) */
    if (doc->body.paras[0].nruns < 5 || doc->body.paras[0].runs[4].kind != OWF_RUN_FRAME ||
        doc->body.paras[0].runs[4].image != 4 ||
        (doc->body.paras[0].nruns == 6 && strcmp(doc->body.paras[0].runs[5].text, "")))
        return 1908;
    if (ow_editor_redo(ed) != 1 || doc->nframes != 6 || doc->body.paras[0].nruns != 6) return 1909;
    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

/* 2.0.11: an undo doesn't copy the pictures (the lab: one undo on the plan cost 6 MB), and undo and redo
 * hold about 4 MB of snapshots between them, the last edit's always. */
static int test_undo_memory(void)
{
    owf_doc *doc = NULL;
    ow_editor *ed;
    ow_selection sel;
    const unsigned char *pic;
    owf_parafmt pf;
    owf_charfmt cf;
    char line[601];
    int i, n, undos;
    if (owf_import_file("tests/core/fixtures/cover.docx", NULL, &doc, NULL, NULL) != OWF_OK || !doc) return 2001;
    n = doc->nimages;
    if (n < 1 || !doc->images[0].data) return 2002;
    pic = doc->images[0].data;
    ed = ow_editor_new(doc);
    if (!ed) return 2003;
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = sel.focus.paragraph = 3;
    ow_editor_set_selection(ed, &sel);
    if (ow_editor_insert_utf8(ed, "x", 1) != OWF_OK) return 2004;
    if (ow_editor_undo(ed) != 1 || ow_editor_redo(ed) != 1 || ow_editor_undo(ed) != 1) return 2005;
    /* the same pictures, not copies back from a snapshot */
    if (doc->nimages != n || doc->images[0].data != pic || doc->frames[0].image < 0) return 2006;
    ow_editor_free(ed);
    owf_doc_free(doc);

    /* 1500 paragraphs of 600 letters: about 1.6 MB a snapshot, so two at most */
    doc = owf_doc_new();
    if (!doc) return 2007;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    memset(line, 'a', 600);
    line[600] = 0;
    for (i = 0; i < 1500; ++i) {
        owf_para *p = owf_story_add(&doc->body, &pf);
        if (!p || owf_para_add_text(p, &cf, line, 600) != OWF_OK) return 2008;
    }
    ed = ow_editor_new(doc);
    if (!ed) return 2009;
    for (i = 0; i < 6; ++i) {
        memset(&sel, 0, sizeof sel);
        sel.anchor.paragraph = sel.focus.paragraph = i * 100;   /* each a new edit, not more typing */
        ow_editor_set_selection(ed, &sel);
        if (ow_editor_insert_utf8(ed, "b", 1) != OWF_OK) return 2010;
    }
    for (undos = 0; undos < 10 && ow_editor_undo(ed) == 1; ++undos)
        ;
    if (undos < 1 || undos > 2) return 2011;
    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int name_is(const char *a, const char *b)
{
    return a && b && !strcmp(a, b);
}

/* 2.0.12: paragraphs' styles and bookmarks (tests/core/fixtures/contents.docx) through editing: Return gives
 * the new paragraph the style and no bookmark, Backspace joining two keeps both's bookmarks, undo brings back
 * the names as they were. */
static int test_names_editing(void)
{
    owf_doc *doc = NULL;
    ow_editor *ed;
    ow_selection sel;
    if (owf_import_file("tests/core/fixtures/contents.docx", NULL, &doc, NULL, NULL) != OWF_OK || !doc) return 2101;
    if (doc->body.nparas != 10 || !name_is(doc->body.paras[1].style, "TOC1") ||
        !name_is(doc->body.paras[7].bookmarks, "_Toc2"))
        return 2102;
    ed = ow_editor_new(doc);
    if (!ed) return 2103;
    /* Return in the middle of the first entry: both halves TOC1, the bookmark-free entry stays so */
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = sel.focus.paragraph = 1;
    sel.anchor.byte_offset = sel.focus.byte_offset = 3;
    ow_editor_set_selection(ed, &sel);
    if (ow_editor_newline(ed) != OWF_OK || doc->body.nparas != 11) return 2104;
    if (!name_is(doc->body.paras[1].style, "TOC1") || !name_is(doc->body.paras[2].style, "TOC1") ||
        doc->body.paras[2].bookmarks)
        return 2105;
    if (ow_editor_undo(ed) != 1 || doc->body.nparas != 10 || !name_is(doc->body.paras[2].style, "TOC2")) return 2106;
    /* Backspace at the start of "1.1 Purpose": joined to "01 Executive summary", both bookmarks there */
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = sel.focus.paragraph = 7;
    ow_editor_set_selection(ed, &sel);
    if (ow_editor_backspace(ed) != OWF_OK || doc->body.nparas != 9) return 2107;
    if (!name_is(doc->body.paras[6].bookmarks, "_Toc1 _Toc2")) return 2108;
    if (ow_editor_undo(ed) != 1 || doc->body.nparas != 10 || !name_is(doc->body.paras[6].bookmarks, "_Toc1") ||
        !name_is(doc->body.paras[7].bookmarks, "_Toc2"))
        return 2109;
    /* bold on part of an entry: every part still links to its heading (the link was lost) */
    {
        int k, linked = 0, runs;
        memset(&sel, 0, sizeof sel);
        sel.anchor.paragraph = sel.focus.paragraph = 1;
        sel.anchor.byte_offset = 3;
        sel.focus.byte_offset = 12;
        ow_editor_set_selection(ed, &sel);
        if (ow_editor_toggle_char_flags(ed, OWF_BOLD) != OWF_OK) return 2110;
        runs = doc->body.paras[1].nruns;
        for (k = 0; k < runs; ++k)
            if (doc->body.paras[1].runs[k].kind == OWF_RUN_TEXT && doc->body.paras[1].runs[k].text &&
                *doc->body.paras[1].runs[k].text)
                linked += name_is(doc->body.paras[1].runs[k].href, "#_Toc1");
        if (linked < 3) return 2111;
    }
    ow_editor_free(ed);
    owf_doc_free(doc);
    return 0;
}

static int test_headers_fields(void)
{
    owf_doc *doc = doc_with("Body");
    ow_editor *ed;
    char *text;
    ow_selection sel;
    if(!doc)return 1451;
    ed=ow_editor_new(doc);if(!ed)return 1452;
    if(ow_editor_set_story_text(ed,OW_STORY_HEADER,"OpenWrite  {PAGE}/{PAGES}")!=OWF_OK)return 1453;
    if(doc->header.nparas!=1||doc->header.paras[0].nruns<3)return 1454;
    text=ow_editor_story_text(ed,OW_STORY_HEADER);if(!text)return 1455;
    if(strcmp(text,"OpenWrite  {PAGE}/{PAGES}")){free(text);return 1456;}free(text);
    memset(&sel,0,sizeof sel);sel.anchor.paragraph=sel.focus.paragraph=0;sel.anchor.run=sel.focus.run=0;sel.anchor.byte_offset=sel.focus.byte_offset=4;ow_editor_set_selection(ed,&sel);
    if(ow_editor_insert_field(ed,OWF_FIELD_DATE)!=OWF_OK)return 1457;
    if(doc->body.paras[0].nruns<3||doc->body.paras[0].runs[1].kind!=OWF_RUN_FIELD||doc->body.paras[0].runs[1].field!=OWF_FIELD_DATE)return 1458;
    if(ow_editor_undo(ed)!=1)return 1459;
    if(doc->body.paras[0].nruns!=1||strcmp(doc->body.paras[0].runs[0].text,"Body"))return 1460;
    ow_editor_free(ed);owf_doc_free(doc);return 0;
}

static int test_pdf_export(void)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    int i;
    FILE *f;
    char *data;
    long n;
    if (!doc) return 1501;
    owf_parafmt_init(&pf); owf_charfmt_init(&cf);
    cf.flags = OWF_BOLD; cf.size = 14 * OWF_TWIPS_PER_POINT;
    for (i = 0; i < 55; ++i) {
        owf_para *p = owf_story_add(&doc->body, &pf);
        if (!p || owf_para_add_text(p, &cf, "OpenWrite searchable PDF output with formatting and automatic pagination.", 70) != OWF_OK) return 1502;
    }
    if (owf_export_file(doc, "/tmp/openwrite-core.pdf", "pdf", NULL) != OWF_OK) return 1503;
    f = fopen("/tmp/openwrite-core.pdf", "rb"); if (!f) return 1504;
    fseek(f,0,SEEK_END); n=ftell(f); fseek(f,0,SEEK_SET);
    if (n < 1000) { fclose(f); return 1505; }
    data=(char *)malloc((size_t)n+1); if(!data){fclose(f);return 1506;}
    if(fread(data,1,(size_t)n,f)!=(size_t)n){free(data);fclose(f);return 1507;}fclose(f);data[n]=0;
    if(strncmp(data,"%PDF-1.4",8)){free(data);return 1508;}
    if(!strstr(data,"/Type /Pages /Count ")){free(data);return 1509;}
    if(!strstr(data,"Helvetica-Bold") && !strstr(data,"Times-Bold")){free(data);return 1510;}
    if(!strstr(data,"(OpenWrite)") || !strstr(data,"(searchable)")){free(data);return 1511;}
    free(data);remove("/tmp/openwrite-core.pdf");owf_doc_free(doc);return 0;
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
    if ((rc = test_rich_roundtrip())) goto fail;
    if ((rc = test_table_editing())) goto fail;
    if ((rc = test_page_setup_and_break())) goto fail;
    if ((rc = test_headers_fields())) goto fail;
    if ((rc = test_frames_undo())) goto fail;
    if ((rc = test_undo_memory())) goto fail;
    if ((rc = test_names_editing())) goto fail;
    if ((rc = test_pdf_export())) goto fail;
    puts("openwrite core editor tests passed");
    return 0;
fail:
    fprintf(stderr, "openwrite core editor test failed: %d\n", rc);
    return rc;
}

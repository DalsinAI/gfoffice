/*
 * GFOpenWrite's AutoCorrect (core 1.8): the list (add, replace, remove, saved and read again), and typing through
 * ow_editor_type: words replaced (keeping a capital or capitals), a sentence's first letter, TWo capitals, curly
 * quotes, en and em dashes, each option on its own, one Undo giving back what was typed, nothing done while
 * changes are recorded but the quotes, and ow_editor_autocorrect_word before Return.
 * make autocorrect-test, make autocorrect-check (ASan).
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

static owf_doc *empty_doc(void)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    if (!doc || !(p = owf_story_add(&doc->body, &pf)) || owf_para_add_text(p, &cf, "", 0) != OWF_OK) {
        owf_doc_free(doc);
        return NULL;
    }
    return doc;
}

/* types s a byte at a time (a UTF-8 sequence whole), as the keyboard does */
static void type(ow_editor *ed, const ow_autocorrect *ac, const char *s)
{
    while (*s) {
        size_t n = 1;
        if ((unsigned char)*s >= 0xC0) while (((unsigned char)s[n] & 0xC0) == 0x80) ++n;
        if (*s == '\n') { ow_editor_autocorrect_word(ed, ac); ow_editor_newline(ed); }
        else ow_editor_type(ed, ac, s, n);
        s += n;
    }
}

/* what typing s into an empty document gives */
static const char *typed(const ow_autocorrect *ac, const char *s)
{
    static char out[2048];
    owf_doc *doc = empty_doc();
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    out[0] = 0;
    if (ed) {
        type(ed, ac, s);
        snprintf(out, sizeof out, "%s", text_of(doc));
        ow_editor_free(ed);
    }
    owf_doc_free(doc);
    return out;
}

#define TYPED(ac, in, want)                                                                                \
    do {                                                                                                    \
        const char *got_ = typed((ac), (in));                                                               \
        char msg_[600];                                                                                     \
        snprintf(msg_, sizeof msg_, "\"%s\" gave \"%s\", wanted \"%s\"", (in), got_, (want));               \
        CHECK(!strcmp(got_, (want)), msg_);                                                                 \
    } while (0)

static void test_list(void)
{
    ow_autocorrect *ac = ow_autocorrect_new(1), *back;
    char *saved;
    size_t len = 0;
    int n;
    CHECK(ac && ow_autocorrect_count(ac) > 20, "a starting list");
    CHECK(ow_autocorrect_options(ac) == OW_AC_ALL, "all on");
    n = ow_autocorrect_count(ac);
    CHECK(!strcmp(ow_autocorrect_lookup(ac, "teh"), "the"), "teh");
    CHECK(ow_autocorrect_add(ac, "gfo", "GFOffice") == OWF_OK && ow_autocorrect_count(ac) == n + 1, "added");
    CHECK(ow_autocorrect_add(ac, "gfo", "GF Office") == OWF_OK && ow_autocorrect_count(ac) == n + 1 &&
          !strcmp(ow_autocorrect_lookup(ac, "gfo"), "GF Office"), "replaced");
    CHECK(ow_autocorrect_add(ac, "two words", "x") == OWF_ERR_FORMAT, "a space in the word");
    CHECK(ow_autocorrect_add(ac, "x", "") == OWF_ERR_FORMAT && ow_autocorrect_add(ac, "", "x") == OWF_ERR_FORMAT, "empty");
    CHECK(ow_autocorrect_add(ac, "nl", "a\nb") == OWF_ERR_FORMAT, "a line end in the replacement");
    CHECK(ow_autocorrect_remove(ac, "wich") == OWF_OK && !ow_autocorrect_lookup(ac, "wich"), "removed");
    CHECK(ow_autocorrect_remove(ac, "wich") == OWF_ERR_UNSUPPORTED, "not there");
    ow_autocorrect_set_options(ac, OW_AC_REPLACE | OW_AC_QUOTES | 64);
    CHECK(ow_autocorrect_options(ac) == (OW_AC_REPLACE | OW_AC_QUOTES), "options");
    saved = ow_autocorrect_save(ac, &len);
    CHECK(saved && !strncmp(saved, "options 9\n", 10) && strstr(saved, "gfo\tGF Office\n"), "saved");
    back = ow_autocorrect_new(0);
    CHECK(back && ow_autocorrect_count(back) == 0 && ow_autocorrect_load(back, saved, len) == OWF_OK, "read again");
    CHECK(back && ow_autocorrect_count(back) == ow_autocorrect_count(ac) &&
          ow_autocorrect_options(back) == ow_autocorrect_options(ac) &&
          !strcmp(ow_autocorrect_lookup(back, "gfo"), "GF Office") && !ow_autocorrect_lookup(back, "wich"), "the same");
    CHECK(ow_autocorrect_load(back, "x\ty\r\n\nno tab line\n", strlen("x\ty\r\n\nno tab line\n")) == OWF_OK && !strcmp(ow_autocorrect_lookup(back, "x"), "y"),
          "CRLF and blank lines");
    owf_free(saved);
    ow_autocorrect_free(back);
    ow_autocorrect_free(ac);
}

static void test_typing(void)
{
    ow_autocorrect *ac = ow_autocorrect_new(1);
    if (!ac) { CHECK(0, "list"); return; }
    TYPED(ac, "I saw teh cat ", "I saw the cat ");
    TYPED(ac, "Teh cat ", "The cat ");
    TYPED(ac, "TEH CAT ", "THE CAT ");
    TYPED(ac, "a lot of teh, ", "A lot of the, ");
    TYPED(ac, "see (teh) ", "See (the) ");
    TYPED(ac, "alot ", "A lot ");
    TYPED(ac, "(c) 2026 ", "\xc2\xa9 2026 ");
    TYPED(ac, "and so... ", "And so\xe2\x80\xa6 ");
    TYPED(ac, "the end. then more! and? yes ", "The end. Then more! And? Yes ");
    TYPED(ac, "use e.g. this ", "Use e.g. this ");
    TYPED(ac, "see www.amicradle.com now ", "See www.amicradle.com now ");
    TYPED(ac, "mail bob@example.org today ", "Mail bob@example.org today ");
    TYPED(ac, "THe cat ", "The cat ");
    TYPED(ac, "NASA rocks ", "NASA rocks ");
    TYPED(ac, "CDs ", "CDs ");
    TYPED(ac, "red - blue ", "Red \xe2\x80\x93 blue ");
    TYPED(ac, "red--blue ", "Red\xe2\x80\x94" "blue ");
    TYPED(ac, "x -- y ", "X -- y ");
    TYPED(ac, "-5 ", "-5 ");
    TYPED(ac, "he said \"hi\" ", "He said \xe2\x80\x9chi\xe2\x80\x9d ");
    TYPED(ac, "it's 'ok' ", "It\xe2\x80\x99s \xe2\x80\x98ok\xe2\x80\x99 ");
    TYPED(ac, "(\"x\") ", "(\xe2\x80\x9cx\xe2\x80\x9d) ");
    TYPED(ac, "teh\nteh end\n", "The|The end|");
    TYPED(ac, "\xc3\xa9t\xc3\xa9 ", "\xc3\xa9t\xc3\xa9 ");
    ow_autocorrect_set_options(ac, OW_AC_REPLACE);
    TYPED(ac, "teh cat \"x\" a - b ", "the cat \"x\" a - b ");
    ow_autocorrect_set_options(ac, OW_AC_SENTENCE);
    TYPED(ac, "teh cat. dog ", "Teh cat. Dog ");
    ow_autocorrect_set_options(ac, OW_AC_QUOTES);
    TYPED(ac, "teh \"x\" ", "teh \xe2\x80\x9cx\xe2\x80\x9d ");
    ow_autocorrect_set_options(ac, 0);
    TYPED(ac, "teh THe \"x\" a - b. c ", "teh THe \"x\" a - b. c ");
    TYPED(NULL, "teh ", "teh ");
    ow_autocorrect_free(ac);
}

static void test_undo_and_tracking(void)
{
    ow_autocorrect *ac = ow_autocorrect_new(1);
    owf_doc *doc = empty_doc();
    ow_editor *ed = doc && ac ? ow_editor_new(doc) : NULL;
    if (!ed) { CHECK(0, "editor"); ow_autocorrect_free(ac); owf_doc_free(doc); return; }
    type(ed, ac, "The cat sat on teh");
    CHECK(!strcmp(text_of(doc), "The cat sat on teh"), text_of(doc));
    type(ed, ac, " ");
    CHECK(!strcmp(text_of(doc), "The cat sat on the "), text_of(doc));
    CHECK(ow_editor_undo(ed) == 1 && !strcmp(text_of(doc), "The cat sat on teh"), text_of(doc));
    type(ed, ac, "m");
    CHECK(!strcmp(text_of(doc), "The cat sat on tehm"), "typing goes on after the undo");
    ow_editor_free(ed);
    owf_doc_free(doc);

    doc = empty_doc();
    ed = doc ? ow_editor_new(doc) : NULL;
    if (ed) {
        ow_editor_set_track_changes(ed, 1);
        type(ed, ac, "teh \"x\" ");
        CHECK(!strcmp(text_of(doc), "teh \xe2\x80\x9cx\xe2\x80\x9d "), text_of(doc));
        ow_editor_free(ed);
    }
    owf_doc_free(doc);

    /* a word in two formats is left alone */
    doc = empty_doc();
    ed = doc ? ow_editor_new(doc) : NULL;
    if (ed) {
        type(ed, ac, "x t");
        ow_editor_toggle_char_flags(ed, OWF_BOLD);
        type(ed, ac, "eh ");
        CHECK(!strcmp(text_of(doc), "X teh "), text_of(doc));
        ow_editor_free(ed);
    }
    owf_doc_free(doc);
    ow_autocorrect_free(ac);
}

int main(void)
{
    test_list();
    test_typing();
    test_undo_and_tracking();
    printf("autocorrect: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

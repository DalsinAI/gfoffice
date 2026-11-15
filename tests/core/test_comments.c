/*
 * GFOpenWrite's comments (core 1.3, libowf 1.6): making, answering, marking done, changing and deleting them in
 * the editor, undo, and the comments through ODT and DOCX and out to formats without them. Word's and
 * OpenOffice's forms of the files are made here by hand from the formats' specifications (ECMA-376 part 1,
 * 17.13.4; [MS-DOCX] 2.5.39 for commentsExtended; ODF 1.3 part 3, 14.1), so no document from either program
 * goes into the repository. 2.0.36: the comments printed, through "pdf-comments". make comments-test, make
 * comments-check (ASan).
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

static owf_doc *doc_of(const char *const *paras, int n)
{
    owf_doc *doc = owf_doc_new();
    int i;
    owf_parafmt pf;
    owf_charfmt cf;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    for (i = 0; doc && i < n; ++i) {
        owf_para *p = owf_story_add(&doc->body, &pf);
        if (!p || (paras[i][0] && owf_para_add_text(p, &cf, paras[i], strlen(paras[i])) != 0)) {
            owf_doc_free(doc);
            return NULL;
        }
    }
    return doc;
}

/* The body's text with each comment's range start as '<' and its place as [its text], replies as [^text],
 * done ones as [text!], paragraphs between '|'; a tracked insertion's text in {+...+}. */
static const char *dump(const owf_doc *doc)
{
    static char out[2048];
    size_t used = 0;
    int i, j;
#define PUT(s)                                                                                             \
    do {                                                                                                    \
        const char *s_ = (s);                                                                               \
        size_t n_ = strlen(s_);                                                                             \
        if (used + n_ + 1 < sizeof out) { memcpy(out + used, s_, n_); used += n_; out[used] = 0; }         \
    } while (0)
    out[0] = 0;
    for (i = 0; i < doc->body.nparas; ++i) {
        const owf_para *p = &doc->body.paras[i];
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            if (r->comment < 0) {
                PUT("<");
            } else if (r->comment > 0) {
                const owf_annotation *c = &doc->comments[r->comment - 1];
                PUT("[");
                if (c->parent >= 0) PUT("^");
                PUT(c->text);
                if (c->done) PUT("!");
                PUT("]");
            } else if (r->kind == OWF_RUN_TEXT && r->text) {
                if (r->revision) PUT("{+");
                PUT(r->text);
                if (r->revision) PUT("+}");
            }
        }
        if (i + 1 < doc->body.nparas) PUT("|");
    }
#undef PUT
    return out;
}

static void caret(ow_editor *ed, int paragraph, size_t offset)
{
    owf_doc *doc = ow_editor_document(ed);
    owf_para *p = &doc->body.paras[paragraph];
    ow_selection sel;
    int j;
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = paragraph;
    for (j = 0; j < p->nruns; ++j) {
        size_t n = p->runs[j].kind == OWF_RUN_TEXT && p->runs[j].text ? strlen(p->runs[j].text) : 0;
        if (p->runs[j].kind != OWF_RUN_TEXT || p->runs[j].comment) continue;
        sel.anchor.run = j;
        if (offset <= n) { sel.anchor.byte_offset = offset; break; }
        offset -= n;
        sel.anchor.byte_offset = n;
    }
    sel.focus = sel.anchor;
    ow_editor_set_selection(ed, &sel);
}

static void select_range(ow_editor *ed, int p1, size_t o1, int p2, size_t o2)
{
    ow_selection a, b;
    caret(ed, p1, o1);
    a = ow_editor_selection(ed);
    caret(ed, p2, o2);
    b = ow_editor_selection(ed);
    a.focus = b.focus;
    ow_editor_set_selection(ed, &a);
}

static void test_editing(void)
{
    static const char *const text[] = { "The quick fox", "jumps over" };
    owf_doc *doc = doc_of(text, 2);
    ow_editor *ed = ow_editor_new(doc);
    ow_comment_info info;
    CHECK(doc && ed, "editor");
    if (!ed) return;
    ow_editor_set_review_author(ed, "Ann");
    CHECK(ow_editor_comment_count(ed) == 0, "none at first");

    /* on "quick" */
    select_range(ed, 0, 4, 0, 9);
    CHECK(ow_editor_insert_comment(ed, "Which fox?") == OWF_OK, "insert");
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?] fox|jumps over"), dump(doc));
    CHECK(ow_editor_comment_count(ed) == 1, "one");
    CHECK(ow_editor_comment_info(ed, 0, &info) && info.author && !strcmp(info.author, "Ann") && info.date &&
          strlen(info.date) == 19 && !strcmp(info.text, "Which fox?") && info.reply_to == -1 && !info.done,
          "its author, date and text");

    /* typing at its place goes after it, not into it */
    ow_editor_insert_utf8(ed, "!", 1);
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?]! fox|jumps over"), dump(doc));
    ow_editor_undo(ed);
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?] fox|jumps over"), dump(doc));

    /* a point comment, in the second paragraph */
    caret(ed, 1, 5);
    CHECK(ow_editor_insert_comment(ed, "Over what?\nA fence.") == OWF_OK, "a point");
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?] fox|jumps[Over what?\nA fence.] over"), dump(doc));
    CHECK(ow_editor_comment_count(ed) == 2, "two");

    /* the caret inside the first one's text: it is the current one */
    caret(ed, 0, 6);
    CHECK(ow_editor_current_comment(ed, &info) && info.index == 0, "current");
    caret(ed, 0, 1);
    CHECK(!ow_editor_current_comment(ed, &info), "none at the start");
    CHECK(ow_editor_reply_comment(ed, "x") == OWF_ERR_UNSUPPORTED, "no reply without a comment");

    /* next and previous */
    CHECK(ow_editor_next_comment(ed, 0) == 1 && ow_editor_current_comment(ed, &info) && info.index == 0, "next");
    CHECK(ow_editor_next_comment(ed, 0) == 1 && ow_editor_current_comment(ed, &info) && info.index == 1, "next again");
    CHECK(ow_editor_next_comment(ed, 0) == 2 && ow_editor_current_comment(ed, &info) && info.index == 0, "round");
    CHECK(ow_editor_next_comment(ed, 1) == 2 && ow_editor_current_comment(ed, &info) && info.index == 1, "back round");

    /* Bob answers the first */
    ow_editor_set_review_author(ed, "Bob");
    caret(ed, 0, 6);
    CHECK(ow_editor_reply_comment(ed, "The brown one.") == OWF_OK, "reply");
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?][^The brown one.] fox|jumps[Over what?\nA fence.] over"), dump(doc));
    CHECK(ow_editor_comment_info(ed, 1, &info) && info.reply_to == 0 && info.author && !strcmp(info.author, "Bob"),
          "the reply answers the first");
    /* next passes replies over */
    caret(ed, 0, 0);
    CHECK(ow_editor_next_comment(ed, 0) == 1 && ow_editor_current_comment(ed, &info) && info.index == 0, "not the reply");

    /* done: the comment and its reply */
    caret(ed, 0, 6);
    CHECK(ow_editor_resolve_comment(ed, 1) == OWF_OK, "done");
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?!][^The brown one.!] fox|jumps[Over what?\nA fence.] over"), dump(doc));
    CHECK(ow_editor_comment_info(ed, 0, &info) && info.author && !strcmp(info.author, "Ann"), "still Ann's");
    ow_editor_undo(ed);
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?][^The brown one.] fox|jumps[Over what?\nA fence.] over"), dump(doc));

    /* its text changed */
    caret(ed, 1, 5);
    CHECK(ow_editor_set_comment_text(ed, "Over the dog.") == OWF_OK, "changed");
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?][^The brown one.] fox|jumps[Over the dog.] over"), dump(doc));

    /* bold over the commented text keeps the comment */
    select_range(ed, 0, 0, 0, 13);
    ow_editor_toggle_char_flags(ed, OWF_BOLD);
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?][^The brown one.] fox|jumps[Over the dog.] over"), dump(doc));

    /* delete the first, its reply with it; undo brings both back */
    caret(ed, 0, 6);
    CHECK(ow_editor_delete_comment(ed) == OWF_OK, "delete");
    CHECK(!strcmp(dump(doc), "The quick fox|jumps[Over the dog.] over"), dump(doc));
    CHECK(ow_editor_comment_count(ed) == 1, "one left");
    ow_editor_undo(ed);
    CHECK(!strcmp(dump(doc), "The <quick[Which fox?][^The brown one.] fox|jumps[Over the dog.] over"), dump(doc));
    CHECK(ow_editor_comment_count(ed) == 3, "three again");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

/* 2.1.1: formatting a tracked insertion keeps it a change */
static void test_format_keeps_change(void)
{
    static const char *const text[] = { "One two" };
    owf_doc *doc = doc_of(text, 1);
    ow_editor *ed = ow_editor_new(doc);
    if (!ed) { CHECK(0, "editor"); return; }
    ow_editor_set_track_changes(ed, 1);
    caret(ed, 0, 3);
    ow_editor_insert_utf8(ed, " new", 4);
    CHECK(!strcmp(dump(doc), "One{+ new+} two"), dump(doc));
    select_range(ed, 0, 0, 0, 11);
    ow_editor_toggle_char_flags(ed, OWF_BOLD);
    CHECK(!strcmp(dump(doc), "One{+ new+} two"), dump(doc));
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static owf_doc *commented_doc(void)
{
    static const char *const text[] = { "The quick fox", "jumps over" };
    owf_doc *doc = doc_of(text, 2);
    ow_editor *ed;
    if (!doc) return NULL;
    ed = ow_editor_new(doc);
    ow_editor_set_review_author(ed, "Ann & co");
    select_range(ed, 0, 4, 0, 9);
    ow_editor_insert_comment(ed, "Which <fox>?\nThe red one.");
    ow_editor_set_review_author(ed, "Bob");
    caret(ed, 0, 6);
    ow_editor_reply_comment(ed, "The brown one.");
    caret(ed, 1, 5);
    ow_editor_insert_comment(ed, "Over.");
    ow_editor_resolve_comment(ed, 1);
    ow_editor_free(ed);
    return doc;
}

static const char *const commented_dump =
    "The <quick[Which <fox>?\nThe red one.][^The brown one.] fox|jumps[Over.!] over";

static void test_round_trip(const char *format, const char *want)
{
    owf_doc *doc = commented_doc(), *back = NULL;
    char path[64];
    int i, ann = 0;
    CHECK(doc != NULL, "document");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), commented_dump), dump(doc));
    snprintf(path, sizeof path, "/tmp/openwrite-comments.%s", format);
    CHECK(owf_export_file(doc, path, format, NULL) == OWF_OK, format);
    CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, format);
    if (back) {
        CHECK(!strcmp(dump(back), want), dump(back));
        for (i = 0; i < back->ncomments; ++i)
            if (back->comments[i].author && !strcmp(back->comments[i].author, "Ann & co") &&
                back->comments[i].date && !strncmp(back->comments[i].date, doc->comments[0].date, 16))
                ann = 1;
        CHECK(ann, "the author and date");
        /* and again: the same */
        CHECK(owf_export_file(back, path, format, NULL) == OWF_OK, format);
        owf_doc_free(back);
        back = NULL;
        CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back && !strcmp(dump(back), want),
              back ? dump(back) : "second read");
        owf_doc_free(back);
    }
    owf_doc_free(doc);
}

static void test_other_formats(void)
{
    owf_doc *doc = commented_doc(), *back = NULL;
    owf_report *rp = owf_report_new();
    int i, lost = 0;
    if (!doc || !rp) { CHECK(0, "document"); return; }
    CHECK(owf_export_file(doc, "/tmp/openwrite-comments.rtf", "rtf", rp) == OWF_OK, "rtf");
    CHECK(owf_import_file("/tmp/openwrite-comments.rtf", NULL, &back, NULL, NULL) == OWF_OK && back, "rtf back");
    if (back) {
        CHECK(!strcmp(dump(back), "The quick fox|jumps over"), dump(back));
        owf_doc_free(back);
    }
    for (i = 0; i < owf_report_count(rp); ++i) {
        owf_note_level level;
        const char *note = owf_report_note(rp, i, &level, NULL);
        if (note && strstr(note, "Comments were left out")) lost = 1;
    }
    CHECK(lost, "the report says the comments were left out");
    CHECK(!strcmp(dump(doc), commented_dump), dump(doc));
    owf_report_free(rp);
    owf_doc_free(doc);
}

static unsigned char *pdf_of(const owf_doc *doc, const char *format, const char *tag, size_t *n, owf_report *rp)
{
    unsigned char *data = NULL;
    char path[64];
    FILE *f;
    long size;
    *n = 0;
    snprintf(path, sizeof path, "/tmp/openwrite-comments-%s%s.pdf", tag, format);
    if (owf_export_file(doc, path, format, rp) != OWF_OK) return NULL;
    if (!(f = fopen(path, "rb"))) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (data = (unsigned char *)malloc((size_t)size + 1)) != NULL) {
        *n = fread(data, 1, (size_t)size, f);
        data[*n] = 0;
    }
    fclose(f);
    return data;
}

/* how many times s is in the PDF's bytes */
static int pdf_count(const unsigned char *pdf, size_t n, const char *s)
{
    size_t m = strlen(s), i;
    int c = 0;
    for (i = 0; m && i + m <= n; ++i)
        if (!memcmp(pdf + i, s, m)) ++c;
    return c;
}

/* 2.0.36: Print Comments: the PDF with the comments, a raised mark where each is and the list after the text */
static void test_print_comments(void)
{
    static const char *const plain_text[] = { "No comments here" };
    owf_doc *doc = commented_doc(), *plain = doc_of(plain_text, 1);
    owf_report *rp = owf_report_new();
    unsigned char *with = NULL, *without = NULL, *a = NULL, *b = NULL;
    size_t nw = 0, no = 0, na = 0, nb = 0;
    int i, kept = 0;
    char when[24];
    if (!doc || !plain || !rp) { CHECK(0, "document"); goto out; }
    CHECK(owf_format_named("pdf-comments") != NULL, "the pdf-comments format");
    CHECK(owf_format_for_filename("Letter.pdf") == owf_format_named("pdf"), "a .pdf name is still plain pdf");
    with = pdf_of(doc, "pdf-comments", "", &nw, NULL);
    without = pdf_of(doc, "pdf", "", &no, rp);
    CHECK(with && nw > 0 && without && no > 0, "both PDFs written");
    if (!with || !without) goto out;
    /* the marks: the ranged comment [1], its reply [2] beside it, the done one [3] in the second paragraph; each
     * in the text once and in the list once, and [1] again where the reply says what it answers */
    CHECK(pdf_count(with, nw, "([1]) Tj") == 3 && pdf_count(with, nw, "([2]) Tj") == 2 &&
          pdf_count(with, nw, "([3]) Tj") == 2, "each mark in the text and in the list");
    CHECK(pdf_count(with, nw, "(Comments) Tj") == 1, "the list's heading");
    CHECK(pdf_count(with, nw, "(Which) Tj") == 1 && pdf_count(with, nw, "(<fox>?) Tj") == 1 &&
          pdf_count(with, nw, "(red) Tj") == 1 && pdf_count(with, nw, "(brown) Tj") == 1 &&
          pdf_count(with, nw, "(Over.) Tj") == 1, "every comment's text, each paragraph of it");
    CHECK(pdf_count(with, nw, "(Ann) Tj") == 1 && pdf_count(with, nw, "(Bob,) Tj") == 2, "who wrote each");
    snprintf(when, sizeof when, "(%.10s) Tj", doc->comments[0].date ? doc->comments[0].date : "");
    CHECK(doc->comments[0].date && pdf_count(with, nw, when) >= 1, "the date, without its T");
    CHECK(pdf_count(with, nw, "(answering) Tj") == 1, "the reply names the comment it answers");
    CHECK(pdf_count(with, nw, "(done) Tj") == 1, "the done one says so");
    /* the order: the text, then the list */
    {
        const char *body = strstr((const char *)with, "(jumps) Tj"), *list = strstr((const char *)with, "(Comments) Tj");
        CHECK(body && list && body < list, "the list after the text");
    }
    /* plain pdf, as before: no marks, no list, and the report says where they are */
    CHECK(!pdf_count(without, no, "([1]) Tj") && !pdf_count(without, no, "(Comments) Tj") &&
          !pdf_count(without, no, "(Which) Tj"), "plain pdf leaves the comments out");
    CHECK(pdf_count(without, no, "(jumps) Tj") == 1, "plain pdf has the text");
    for (i = 0; i < owf_report_count(rp); ++i) {
        owf_note_level level;
        const char *note = owf_report_note(rp, i, &level, NULL);
        if (note && strstr(note, "pdf-comments keeps them")) kept = 1;
    }
    CHECK(kept, "the plain pdf's report names pdf-comments");
    /* a document without comments: the same PDF either way */
    a = pdf_of(plain, "pdf", "plain-", &na, NULL);
    b = pdf_of(plain, "pdf-comments", "plain-", &nb, NULL);
    CHECK(a && b && na == nb && !memcmp(a, b, na), "no comments: the same bytes");
    CHECK(!strcmp(dump(doc), commented_dump), dump(doc));
out:
    free(with); free(without); free(a); free(b);
    owf_report_free(rp);
    owf_doc_free(doc);
    owf_doc_free(plain);
}

static int zip_import(const char *const *names, const char *const *parts, int n, owf_doc **doc)
{
    owf_buf out;
    owf_zip *zip;
    int i, rc;
    owf_buf_init(&out);
    zip = owf_zip_new(&out);
    if (!zip) return OWF_ERR_MEMORY;
    for (i = 0; i < n; ++i)
        owf_zip_add(zip, names[i], parts[i], strlen(parts[i]), names[i][0] != 'm');
    owf_zip_finish(zip);
    rc = owf_import_memory(out.data, out.length, NULL, doc, NULL, NULL);
    owf_buf_free(&out);
    return rc;
}

/* Word's form: a ranged comment, a reply to it in commentsExtended, a point comment marked done */
static void test_word_form(void)
{
    static const char *const names[] = { "[Content_Types].xml", "_rels/.rels", "word/document.xml",
                                          "word/comments.xml", "word/commentsExtended.xml",
                                          "word/_rels/document.xml.rels" };
    static const char *const parts[] = {
        "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>",
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>",
        "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        "<w:p><w:r><w:t xml:space=\"preserve\">Keep </w:t></w:r><w:commentRangeStart w:id=\"0\"/><w:commentRangeStart w:id=\"1\"/>"
        "<w:r><w:t>this</w:t></w:r><w:commentRangeEnd w:id=\"0\"/><w:r><w:commentReference w:id=\"0\"/></w:r>"
        "<w:commentRangeEnd w:id=\"1\"/><w:r><w:commentReference w:id=\"1\"/></w:r>"
        "<w:r><w:t xml:space=\"preserve\"> line</w:t></w:r><w:r><w:commentReference w:id=\"2\"/></w:r></w:p>"
        "<w:sectPr/></w:body></w:document>",
        "<?xml version=\"1.0\"?><w:comments xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\">"
        "<w:comment w:id=\"0\" w:author=\"Cy\" w:date=\"2026-11-02T10:00:00Z\" w:initials=\"C\">"
        "<w:p w14:paraId=\"1A000001\"><w:r><w:annotationRef/></w:r><w:r><w:t>Why</w:t></w:r><w:r><w:tab/><w:t>keep?</w:t></w:r></w:p>"
        "<w:p w14:paraId=\"1A000002\"><w:r><w:t>Say.</w:t></w:r></w:p></w:comment>"
        "<w:comment w:id=\"1\" w:author=\"Di\" w:date=\"2026-11-02T10:05:00Z\">"
        "<w:p w14:paraId=\"1A000003\"><w:r><w:annotationRef/></w:r><w:r><w:t>Because.</w:t></w:r></w:p></w:comment>"
        "<w:comment w:id=\"2\" w:author=\"Di\"><w:p w14:paraId=\"1A000004\"><w:r><w:t>Fine.</w:t></w:r></w:p></w:comment>"
        "</w:comments>",
        "<?xml version=\"1.0\"?><w15:commentsEx xmlns:w15=\"http://schemas.microsoft.com/office/word/2012/wordml\">"
        "<w15:commentEx w15:paraId=\"1A000002\" w15:done=\"0\"/>"
        "<w15:commentEx w15:paraId=\"1A000003\" w15:paraIdParent=\"1A000002\" w15:done=\"0\"/>"
        "<w15:commentEx w15:paraId=\"1A000004\" w15:done=\"1\"/></w15:commentsEx>",
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId7\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments\" Target=\"comments.xml\"/>"
        "<Relationship Id=\"rId8\" Type=\"http://schemas.microsoft.com/office/2011/relationships/commentsExtended\" Target=\"commentsExtended.xml\"/>"
        "</Relationships>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 6, &doc) == OWF_OK && doc, "Word's form");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Keep <<this[Why\tkeep?\nSay.][^Because.] line[Fine.!]"), dump(doc));
    CHECK(doc->ncomments == 3 && doc->comments[0].initials && !strcmp(doc->comments[0].initials, "C") &&
          doc->comments[0].date && !strcmp(doc->comments[0].date, "2026-11-02T10:00:00Z"), "Cy's initials and date");
    owf_doc_free(doc);
}

/* OpenOffice's form: a named annotation with its end, a point one with initials */
static void test_odf_form(void)
{
    static const char *const names[] = { "mimetype", "content.xml", "META-INF/manifest.xml" };
    static const char *const parts[] = {
        "application/vnd.oasis.opendocument.text",
        "<?xml version=\"1.0\"?><office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.3\">"
        "<office:body><office:text>"
        "<text:p>One <office:annotation office:name=\"a1\"><dc:creator>Eve</dc:creator><dc:date>2026-10-30T08:00:00</dc:date>"
        "<text:p>First</text:p><text:p>second <text:s/>line</text:p></office:annotation>two"
        "<office:annotation-end office:name=\"a1\"/> three</text:p>"
        "<text:p>Four<office:annotation><dc:creator>Fay</dc:creator><meta:creator-initials>FF</meta:creator-initials>"
        "<text:p>Point</text:p></office:annotation></text:p>"
        "<text:p>Five<office:annotation office:name=\"lost\"><text:p>No end</text:p></office:annotation> six</text:p>"
        "</office:text></office:body></office:document-content>",
        "<?xml version=\"1.0\"?><manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 3, &doc) == OWF_OK && doc, "OpenOffice's form");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "One <two[First\nsecond  line] three|Four[Point]|Five[No end] six"), dump(doc));
    CHECK(doc->ncomments == 3 && doc->comments[1].initials && !strcmp(doc->comments[1].initials, "FF") &&
          doc->comments[0].author && !strcmp(doc->comments[0].author, "Eve"), "Eve's, Fay's initials");
    owf_doc_free(doc);
}

int main(void)
{
    test_editing();
    test_format_keeps_change();
    test_round_trip("docx", commented_dump);
    /* ODF keeps no replies or done: they come back as comments of their own */
    test_round_trip("odt", "The <quick[Which <fox>?\nThe red one.][The brown one.] fox|jumps[Over.] over");
    test_other_formats();
    test_print_comments();
    test_word_form();
    test_odf_form();
    printf("comments: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

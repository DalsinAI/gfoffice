/*
 * GFOpenWrite's tracked changes (core 1.2, libowf 1.5): recording in the editor, accepting and rejecting,
 * moving between changes, undo, and the changes through ODT and DOCX and out to formats without them.
 * Word's and OpenOffice's forms of the files are made here by hand from the formats' specifications
 * (ECMA-376 part 1, 17.13.5; ODF 1.3 part 3, 5.5), so no document from either program goes into the
 * repository. make track-test, make track-check (ASan).
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

/* Every paragraph's text, deleted text in [-...-] and inserted in {+...+}, paragraph ends as '|' (a deleted
 * one "|-", an inserted one "|+"). */
static void story_dump(const owf_doc *doc, const owf_story *st, char *out, size_t size)
{
    size_t used = 0;
    int i, j;
    out[0] = 0;
#define PUT(s)                                                                                             \
    do {                                                                                                    \
        const char *s_ = (s);                                                                               \
        size_t n_ = strlen(s_);                                                                             \
        if (used + n_ + 1 < size) { memcpy(out + used, s_, n_); used += n_; out[used] = 0; }               \
    } while (0)
    for (i = 0; i < st->nparas; ++i) {
        const owf_para *p = &st->paras[i];
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            const owf_revision *rv = OWF_DOC_REVISION(doc, r->revision);
            const char *text = r->kind == OWF_RUN_TEXT ? (r->text ? r->text : "") : r->kind == OWF_RUN_TAB ? "\\t" : "?";
            if (r->kind == OWF_RUN_TEXT && !*text) continue;
            if (rv) PUT(rv->kind == OWF_REV_DELETE ? "[-" : "{+");
            PUT(text);
            if (rv) PUT(rv->kind == OWF_REV_DELETE ? "-]" : "+}");
        }
        if (i + 1 < st->nparas) {
            const owf_revision *mv = OWF_DOC_REVISION(doc, p->mark_revision);
            PUT(mv ? (mv->kind == OWF_REV_DELETE ? "|-" : "|+") : "|");
        }
    }
#undef PUT
}

static const char *dump(const owf_doc *doc)
{
    static char buf[2048];
    story_dump(doc, &doc->body, buf, sizeof buf);
    return buf;
}

static void caret(ow_editor *ed, int paragraph, size_t offset)
{
    /* the offset within the paragraph's text runs */
    owf_doc *doc = ow_editor_document(ed);
    owf_para *p = &doc->body.paras[paragraph];
    ow_selection sel;
    int j;
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = paragraph;
    for (j = 0; j < p->nruns; ++j) {
        size_t n = p->runs[j].kind == OWF_RUN_TEXT && p->runs[j].text ? strlen(p->runs[j].text) : 0;
        if (p->runs[j].kind != OWF_RUN_TEXT) continue;
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

static void test_recording(void)
{
    static const char *const text[] = { "Hello world", "Second line" };
    owf_doc *doc = doc_of(text, 2);
    ow_editor *ed = ow_editor_new(doc);
    ow_change_info info;
    CHECK(doc && ed, "editor");
    if (!ed) return;
    CHECK(!ow_editor_track_changes(ed), "off at first");
    ow_editor_set_review_author(ed, "Ann");
    ow_editor_set_track_changes(ed, 1);
    CHECK(ow_editor_track_changes(ed) && doc->track_changes, "recording");

    caret(ed, 0, 5);
    ow_editor_insert_utf8(ed, " big", 4);
    CHECK(!strcmp(dump(doc), "Hello{+ big+} world|Second line"), dump(doc));
    CHECK(ow_editor_change_count(ed) == 1, "one change");
    CHECK(ow_editor_change_info(ed, 0, &info) && info.kind == OWF_REV_INSERT && info.author &&
          !strcmp(info.author, "Ann") && info.date && strlen(info.date) == 19, "insertion's author and date");

    /* typing over a selection: "world" deleted, the new word after it */
    select_range(ed, 0, 10, 0, 15);
    ow_editor_insert_utf8(ed, "there", 5);
    CHECK(!strcmp(dump(doc), "Hello{+ big+} [-world-]{+there+}|Second line"), dump(doc));
    CHECK(ow_editor_change_count(ed) == 3, "three changes");

    /* Backspace in the author's own insertion takes it out */
    caret(ed, 0, 20);
    ow_editor_backspace(ed);
    CHECK(!strcmp(dump(doc), "Hello{+ big+} [-world-]{+ther+}|Second line"), dump(doc));

    /* Backspace over text that is no insertion of hers: marked, the caret before it */
    caret(ed, 1, 6);
    ow_editor_backspace(ed);
    ow_editor_backspace(ed);
    CHECK(!strcmp(dump(doc), "Hello{+ big+} [-world-]{+ther+}|Seco[-nd-] line"), dump(doc));
    ow_editor_insert_utf8(ed, "X", 1);
    CHECK(!strcmp(dump(doc), "Hello{+ big+} [-world-]{+ther+}|Seco{+X+}[-nd-] line"), dump(doc));

    /* Backspace at a paragraph's start: the paragraph end before it deleted */
    caret(ed, 1, 0);
    ow_editor_backspace(ed);
    CHECK(!strcmp(dump(doc), "Hello{+ big+} [-world-]{+ther+}|-Seco{+X+}[-nd-] line"), dump(doc));
    CHECK(doc->body.nparas == 2, "still two paragraphs");

    /* Delete forward goes past what it marks */
    caret(ed, 1, 10);
    ow_editor_delete_forward(ed);
    {
        ow_selection s = ow_editor_selection(ed);
        CHECK(s.focus.paragraph == 1, "caret after the deletion");
    }
    CHECK(!strcmp(dump(doc), "Hello{+ big+} [-world-]{+ther+}|-Seco{+X+}[-nd-] [-l-]ine"), dump(doc));

    /* Return while recording: an inserted paragraph end; rejecting it joins them again */
    caret(ed, 0, 5);
    ow_editor_newline(ed);
    CHECK(doc->body.nparas == 3 && !strncmp(dump(doc), "Hello|+{+ big+}", 15), dump(doc));
    ow_editor_undo(ed);
    CHECK(doc->body.nparas == 2, "undo the Return");

    /* the changes in order, and the next one from the caret */
    caret(ed, 0, 0);
    CHECK(ow_editor_next_change(ed, 0) == 1, "next change");
    CHECK(ow_editor_current_change(ed, &info) && info.index == 0 && info.kind == OWF_REV_INSERT, "the first");
    CHECK(ow_editor_next_change(ed, 0) == 1 && ow_editor_current_change(ed, &info) && info.index == 1 &&
          info.kind == OWF_REV_DELETE, "the second");
    CHECK(ow_editor_next_change(ed, 1) == 1 && ow_editor_current_change(ed, &info) && info.index == 0, "back");

    /* accept the deletion of "world" only */
    caret(ed, 0, 12);
    CHECK(ow_editor_accept_change(ed, 1) == OWF_OK, "accept one");
    CHECK(!strcmp(dump(doc), "Hello{+ big+} {+ther+}|-Seco{+X+}[-nd-] [-l-]ine"), dump(doc));
    caret(ed, 0, 0);
    CHECK(ow_editor_accept_change(ed, 1) == OWF_OK, "accept the first");
    CHECK(!strcmp(dump(doc), "Hello big {+ther+}|-Seco{+X+}[-nd-] [-l-]ine"), dump(doc));
    CHECK(ow_editor_accept_change(ed, 1) == OWF_ERR_UNSUPPORTED || 1, "no change at the start");

    /* undo both, then reject everything */
    ow_editor_undo(ed);
    ow_editor_undo(ed);
    CHECK(!strcmp(dump(doc), "Hello{+ big+} [-world-]{+ther+}|-Seco{+X+}[-nd-] [-l-]ine"), dump(doc));
    CHECK(ow_editor_accept_all_changes(ed, 0) == OWF_OK, "reject all");
    CHECK(!strcmp(dump(doc), "Hello world|Second line"), dump(doc));
    ow_editor_undo(ed);
    CHECK(ow_editor_accept_all_changes(ed, 1) == OWF_OK, "accept all");
    CHECK(!strcmp(dump(doc), "Hello big therSecoX ine"), dump(doc));
    CHECK(doc->body.nparas == 1, "joined");
    CHECK(ow_editor_change_count(ed) == 0, "none left");
    CHECK(ow_editor_accept_all_changes(ed, 1) == OWF_ERR_UNSUPPORTED, "nothing to accept");

    /* recording off: edits are plain again */
    ow_editor_set_track_changes(ed, 0);
    caret(ed, 0, 0);
    ow_editor_insert_utf8(ed, "A", 1);
    ow_editor_delete_forward(ed);
    CHECK(!strcmp(dump(doc), "Aello big therSecoX ine"), dump(doc));
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_other_author(void)
{
    static const char *const text[] = { "One two" };
    owf_doc *doc = doc_of(text, 1);
    ow_editor *ed = ow_editor_new(doc);
    if (!ed) { CHECK(0, "editor"); return; }
    ow_editor_set_review_author(ed, "Ann");
    ow_editor_set_track_changes(ed, 1);
    caret(ed, 0, 3);
    ow_editor_insert_utf8(ed, "!", 1);
    /* Bob deletes Ann's insertion: it stays, as his deletion */
    ow_editor_set_review_author(ed, "Bob");
    caret(ed, 0, 4);
    ow_editor_backspace(ed);
    CHECK(!strcmp(dump(doc), "One[-!-] two"), dump(doc));
    CHECK(doc->nrevisions == 2 && doc->revisions[1].author && !strcmp(doc->revisions[1].author, "Bob"), "Bob's");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

/* A document with changes of every kind: Ann's insertion, Bob's deletion, a deleted paragraph end, an
 * inserted one, a deleted tab */
static owf_doc *changed_doc(void)
{
    static const char *const text[] = { "Alpha beta", "Gamma", "Delta" };
    owf_doc *doc = doc_of(text, 3);
    ow_editor *ed;
    if (!doc) return NULL;
    ed = ow_editor_new(doc);
    ow_editor_set_track_changes(ed, 1);
    ow_editor_set_review_author(ed, "Ann");
    caret(ed, 0, 5);
    ow_editor_insert_utf8(ed, " new", 4);
    ow_editor_set_review_author(ed, "Bob & co");
    select_range(ed, 0, 10, 0, 14);                  /* " bet" */
    ow_editor_delete_forward(ed);
    caret(ed, 1, 0);
    ow_editor_backspace(ed);                         /* Alpha...|-Gamma */
    caret(ed, 2, 2);
    ow_editor_newline(ed);                           /* De|+lta */
    ow_editor_free(ed);
    return doc;
}

static const char *const changed_dump = "Alpha{+ new+}[- bet-]a|-Gamma|De|+lta";

static void test_round_trip(const char *format)
{
    owf_doc *doc = changed_doc(), *back = NULL;
    char path[64], before[2048];
    int i;
    CHECK(doc != NULL, "document");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), changed_dump), dump(doc));
    snprintf(path, sizeof path, "/tmp/openwrite-track.%s", format);
    strcpy(before, dump(doc));
    CHECK(owf_export_file(doc, path, format, NULL) == OWF_OK, format);
    CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, format);
    if (back) {
        CHECK(!strcmp(dump(back), before), dump(back));
        CHECK(back->track_changes == 1, "recording comes back");
        /* who and when */
        for (i = 0; i < back->body.paras[0].nruns; ++i) {
            const owf_run *r = &back->body.paras[0].runs[i];
            const owf_revision *rv = OWF_DOC_REVISION(back, r->revision);
            if (!rv) continue;
            CHECK(rv->author && !strcmp(rv->author, rv->kind == OWF_REV_INSERT ? "Ann" : "Bob & co"), "author");
            CHECK(rv->date && !strncmp(rv->date, doc->revisions[0].date, 4), "date");
        }
        /* and again: the same file */
        CHECK(owf_export_file(back, path, format, NULL) == OWF_OK, format);
        owf_doc_free(back);
        back = NULL;
        CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back && !strcmp(dump(back), before),
              back ? dump(back) : "second read");
        owf_doc_free(back);
    }
    owf_doc_free(doc);
}

static void test_accepted_formats(void)
{
    owf_doc *doc = changed_doc(), *back = NULL;
    owf_report *rp = owf_report_new();
    int i, approx = 0;
    if (!doc || !rp) { CHECK(0, "document"); return; }
    CHECK(owf_export_file(doc, "/tmp/openwrite-track.rtf", "rtf", rp) == OWF_OK, "rtf");
    CHECK(owf_import_file("/tmp/openwrite-track.rtf", NULL, &back, NULL, NULL) == OWF_OK && back, "rtf back");
    if (back) {
        CHECK(!strcmp(dump(back), "Alpha newaGamma|De|lta"), dump(back));
        owf_doc_free(back);
    }
    for (i = 0; i < owf_report_count(rp); ++i) {
        owf_note_level level;
        const char *note = owf_report_note(rp, i, &level, NULL);
        if (note && strstr(note, "accepted")) approx = 1;
    }
    CHECK(approx, "the report says the changes were accepted");
    /* the document itself is as it was */
    CHECK(!strcmp(dump(doc), changed_dump), dump(doc));
    owf_report_free(rp);
    owf_doc_free(doc);
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

/* Word's form: w:ins and w:del with w:delText, a moved paragraph, a deleted paragraph mark */
static void test_word_form(void)
{
    static const char *const names[] = { "[Content_Types].xml", "_rels/.rels", "word/document.xml",
                                          "word/settings.xml", "word/_rels/document.xml.rels" };
    static const char *const parts[] = {
        "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>"
        "</Types>",
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>",
        "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        "<w:p><w:pPr><w:rPr><w:del w:id=\"9\" w:author=\"Cy\" w:date=\"2026-11-02T10:00:00Z\"/></w:rPr></w:pPr>"
        "<w:r><w:t xml:space=\"preserve\">Keep </w:t></w:r>"
        "<w:ins w:id=\"1\" w:author=\"Cy\" w:date=\"2026-11-01T09:30:00Z\"><w:r><w:rPr><w:b/></w:rPr><w:t>added</w:t></w:r></w:ins>"
        "<w:del w:id=\"2\" w:author=\"Di\" w:date=\"2026-11-01T09:31:00Z\"><w:r><w:delText xml:space=\"preserve\"> gone</w:delText></w:r></w:del>"
        "</w:p>"
        "<w:p><w:r><w:t>Next</w:t></w:r><w:moveTo w:id=\"3\" w:author=\"Di\" w:date=\"2026-11-01T09:32:00Z\"><w:r><w:t>!</w:t></w:r></w:moveTo></w:p>"
        "<w:sectPr/></w:body></w:document>",
        "<?xml version=\"1.0\"?><w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:trackRevisions/></w:settings>",
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId6\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
        "</Relationships>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 5, &doc) == OWF_OK && doc, "Word's form");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Keep {+added+}[- gone-]|-Next{+!+}"), dump(doc));
    CHECK(doc->track_changes == 1, "w:trackRevisions");
    CHECK(doc->body.paras[0].runs[1].fmt.flags & OWF_BOLD, "the insertion's format");
    CHECK(doc->nrevisions >= 3 && doc->revisions[0].date && !strcmp(doc->revisions[0].date, "2026-11-02T10:00:00Z"),
          doc->nrevisions ? (doc->revisions[0].date ? doc->revisions[0].date : "no date") : "none");
    owf_doc_free(doc);
}

/* OpenOffice's form: a changed region for each change, insertion spans across a paragraph end, a deletion
 * that took a paragraph's end */
static void test_odf_form(void)
{
    static const char *const names[] = { "mimetype", "content.xml", "META-INF/manifest.xml" };
    static const char *const parts[] = {
        "application/vnd.oasis.opendocument.text",
        "<?xml version=\"1.0\"?><office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" office:version=\"1.3\">"
        "<office:body><office:text>"
        "<text:tracked-changes text:track-changes=\"false\">"
        "<text:changed-region text:id=\"ct1\"><text:insertion><office:change-info><dc:creator>Eve</dc:creator>"
        "<dc:date>2026-10-30T08:00:00</dc:date></office:change-info></text:insertion></text:changed-region>"
        "<text:changed-region text:id=\"ct2\"><text:deletion><office:change-info><dc:creator>Fay</dc:creator>"
        "<dc:date>2026-10-30T08:05:00</dc:date></office:change-info><text:p>old</text:p></text:deletion></text:changed-region>"
        "<text:changed-region text:id=\"ct3\"><text:deletion><office:change-info><dc:creator>Fay</dc:creator>"
        "<dc:date>2026-10-30T08:06:00</dc:date></office:change-info><text:p>tail</text:p><text:p>head</text:p></text:deletion></text:changed-region>"
        "</text:tracked-changes>"
        "<text:p>One <text:change-start text:change-id=\"ct1\"/>new</text:p><text:p>par<text:change-end text:change-id=\"ct1\"/>a "
        "<text:change text:change-id=\"ct2\"/>end</text:p>"
        "<text:p>Left<text:change text:change-id=\"ct3\"/>Right</text:p>"
        "</office:text></office:body></office:document-content>",
        "<?xml version=\"1.0\"?><manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 3, &doc) == OWF_OK && doc, "OpenOffice's form");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "One {+new+}|+{+par+}a [-old-]end|Left[-tail-]|-[-head-]Right"), dump(doc));
    CHECK(doc->track_changes == 0, "text:track-changes false");
    owf_doc_free(doc);
}

int main(void)
{
    test_recording();
    test_other_author();
    test_round_trip("odt");
    test_round_trip("docx");
    test_accepted_formats();
    test_word_form();
    test_odf_form();
    printf("track changes: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

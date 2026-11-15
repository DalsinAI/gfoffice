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
#include "owf_track.h"

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
    caret(ed, 1, 8);                                 /* the offsets count the deleted "nd" too */
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
    caret(ed, 0, 0);
    CHECK(ow_editor_accept_change(ed, 1) == OWF_ERR_UNSUPPORTED, "no change at the start");
    caret(ed, 0, 12);
    CHECK(ow_editor_accept_change(ed, 1) == OWF_OK, "accept one");
    CHECK(!strcmp(dump(doc), "Hello{+ big+} {+ther+}|-Seco{+X+}[-nd-] [-l-]ine"), dump(doc));
    caret(ed, 0, 6);
    CHECK(ow_editor_accept_change(ed, 1) == OWF_OK, "accept the first");
    CHECK(!strcmp(dump(doc), "Hello big {+ther+}|-Seco{+X+}[-nd-] [-l-]ine"), dump(doc));

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
    select_range(ed, 0, 10, 0, 14);                  /* "beta" */
    ow_editor_delete_forward(ed);
    caret(ed, 1, 0);
    ow_editor_backspace(ed);                         /* Alpha...|-Gamma */
    caret(ed, 2, 2);
    ow_editor_newline(ed);                           /* De|+lta */
    ow_editor_free(ed);
    return doc;
}

static const char *const changed_dump = "Alpha{+ new+} [-beta-]|-Gamma|De|+lta";

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
        CHECK(!strcmp(dump(back), "Alpha new Gamma|De|lta"), dump(back));
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
    {
        const owf_revision *mark = OWF_DOC_REVISION(doc, doc->body.paras[0].mark_revision);
        CHECK(doc->nrevisions >= 3 && mark && mark->kind == OWF_REV_DELETE && mark->date &&
              !strcmp(mark->date, "2026-11-02T10:00:00Z"), mark && mark->date ? mark->date : "the mark's date");
    }
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

/* 1.11: Word's tracked format changes (w:rPrChange, ECMA-376 part 1, 17.13.5.31): the run's format now, the
 * format before in the change's own w:rPr; a change on its own, one inside an insertion, and one whose text
 * has the same format now as the plain text round it (it must not join it) */
static const char *format_changes_dump(const owf_doc *doc)
{
    static char buf[1024];
    size_t used = 0;
    int j;
    buf[0] = 0;
    for (j = 0; doc->body.nparas && j < doc->body.paras[0].nruns; ++j) {
        const owf_run *r = &doc->body.paras[0].runs[j];
        const owf_format_change *fc = OWF_DOC_FORMAT_CHANGE(doc, r->format_change);
        if (r->kind != OWF_RUN_TEXT || !r->text || !*r->text) continue;
        used += (size_t)snprintf(buf + used, sizeof buf - used, "<%s%s%s%s", r->text,
                                 r->fmt.flags & OWF_BOLD ? " b" : "", r->fmt.flags & OWF_ITALIC ? " i" : "",
                                 r->fmt.flags & OWF_UNDERLINE ? " u" : "");
        if (r->revision) used += (size_t)snprintf(buf + used, sizeof buf - used, " ins");
        if (fc)
            used += (size_t)snprintf(buf + used, sizeof buf - used, " was%s%s%s %s %s",
                                     fc->before.flags & OWF_BOLD ? " b" : "", fc->before.flags & OWF_ITALIC ? " i" : "",
                                     fc->before.flags & OWF_UNDERLINE ? " u" : "", fc->author ? fc->author : "-",
                                     fc->date ? fc->date : "-");
        used += (size_t)snprintf(buf + used, sizeof buf - used, ">");
        if (used >= sizeof buf) break;
    }
    return buf;
}

static void test_format_changes(void)
{
    static const char *const names[] = { "[Content_Types].xml", "_rels/.rels", "word/document.xml" };
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
        "<w:p><w:r><w:t xml:space=\"preserve\">Plain </w:t></w:r>"
        "<w:r><w:rPr><w:rPrChange w:id=\"4\" w:author=\"Gil\" w:date=\"2026-11-03T11:00:00Z\"><w:rPr><w:b/></w:rPr>"
        "</w:rPrChange></w:rPr><w:t>unbolded</w:t></w:r>"
        "<w:r><w:t xml:space=\"preserve\"> then </w:t></w:r>"
        "<w:r><w:rPr><w:b/><w:rPrChange w:id=\"5\" w:author=\"Gil\" w:date=\"2026-11-03T11:01:00Z\"><w:rPr><w:i/></w:rPr>"
        "</w:rPrChange></w:rPr><w:t>bolded</w:t></w:r>"
        "<w:ins w:id=\"6\" w:author=\"Hal\" w:date=\"2026-11-03T11:02:00Z\"><w:r><w:rPr><w:u w:val=\"single\"/>"
        "<w:rPrChange w:id=\"7\" w:author=\"Hal\"><w:rPr/></w:rPrChange></w:rPr><w:t>both</w:t></w:r></w:ins>"
        "</w:p><w:sectPr/></w:body></w:document>" };
    static const char want[] = "<Plain ><unbolded was b Gil 2026-11-03T11:00:00Z>< then ><bolded b was i Gil 2026-11-03T11:01:00Z>"
                               "<both u ins was Hal ->";
    owf_doc *doc = NULL, *back = NULL, *view = NULL;
    owf_report *rp = owf_report_new();
    char first[1024];
    int i, kept = 0;
    CHECK(rp && zip_import(names, parts, 3, &doc) == OWF_OK && doc, "Word's format changes");
    if (!doc) { owf_report_free(rp); return; }
    strcpy(first, format_changes_dump(doc));
    CHECK(!strcmp(first, want), first);
    CHECK(doc->nformat_changes == 3 && owf_doc_has_changes(doc), "three format changes, and the document has changes");
    CHECK(doc->nformat_changes == 3 && doc->format_changes[0].before.font == doc->body.paras[0].runs[0].fmt.font &&
          doc->format_changes[0].before.size == doc->body.paras[0].runs[0].fmt.size,
          "the format before is kept as the runs' are, the document's defaults left out");

    /* out as DOCX and back: the same, and the same again */
    CHECK(owf_export_file(doc, "/tmp/openwrite-fmtchange.docx", "docx", rp) == OWF_OK, "docx");
    CHECK(owf_import_file("/tmp/openwrite-fmtchange.docx", NULL, &back, NULL, NULL) == OWF_OK && back, "docx back");
    if (back) {
        CHECK(!strcmp(format_changes_dump(back), want), format_changes_dump(back));
        CHECK(owf_export_file(back, "/tmp/openwrite-fmtchange.docx", "docx", NULL) == OWF_OK, "docx again");
        owf_doc_free(back);
        back = NULL;
        CHECK(owf_import_file("/tmp/openwrite-fmtchange.docx", NULL, &back, NULL, NULL) == OWF_OK && back &&
              !strcmp(format_changes_dump(back), want), back ? format_changes_dump(back) : "second read");
        owf_doc_free(back);
    }
    for (i = 0; i < owf_report_count(rp); ++i) {
        owf_note_level level;
        const char *note = owf_report_note(rp, i, &level, NULL);
        if (note && strstr(note, "format changes are kept as the format is now")) kept = 1;
    }
    CHECK(!kept, "the runs' format changes are kept, not reported");

    /* ODT keeps none yet, and says so */
    {
        owf_report *op = owf_report_new();
        int said = 0;
        CHECK(op && owf_export_file(doc, "/tmp/openwrite-fmtchange.odt", "odt", op) == OWF_OK, "odt");
        for (i = 0; op && i < owf_report_count(op); ++i) {
            owf_note_level level;
            const char *note = owf_report_note(op, i, &level, NULL);
            if (note && strstr(note, "Tracked format changes were saved accepted")) said = 1;
        }
        CHECK(said, "the ODT report says the format changes were saved accepted");
        owf_report_free(op);
    }
    /* an edit and its undo leave the changes the document's (the editor keeps the list through undo) */
    {
        ow_editor *ed = ow_editor_new(doc);
        CHECK(ed != NULL, "editor");
        if (ed) {
            caret(ed, 0, 3);
            CHECK(ow_editor_insert_utf8(ed, "x", 1) == OWF_OK && ow_editor_undo(ed) == 1, "an edit undone");
            CHECK(!strcmp(format_changes_dump(doc), want), format_changes_dump(doc));
            CHECK(ow_editor_redo(ed) == 1 && ow_editor_undo(ed) == 1 && doc->nformat_changes == 3, "redo, undo");
            ow_editor_free(ed);
        }
    }
    /* accepted (formats without tracked changes): the format now, no change left */
    CHECK(owf_doc_accepted(doc, &view) == OWF_OK && view, "accepted view");
    if (view) {
        CHECK(!strcmp(format_changes_dump(view), "<Plain ><unbolded>< then ><bolded b><both u>"), format_changes_dump(view));
        CHECK(!owf_doc_has_changes(view), "no changes once accepted");
        owf_doc_accepted_free(view);
    }
    owf_report_free(rp);
    owf_doc_free(doc);
}

/* 2.0.37: the final view (Show Changes off): caret moves step over deleted text and never stop in it */
static size_t focus_at(ow_editor *ed, int *para)
{
    owf_doc *doc = ow_editor_document(ed);
    ow_selection sel = ow_editor_selection(ed);
    size_t off = sel.focus.byte_offset;
    int j;
    *para = sel.focus.paragraph;
    for (j = 0; j < sel.focus.run; ++j)
        if (doc->body.paras[*para].runs[j].kind == OWF_RUN_TEXT && doc->body.paras[*para].runs[j].text)
            off += strlen(doc->body.paras[*para].runs[j].text);
    return off;
}

static void test_final_view(void)
{
    owf_doc *doc = changed_doc();                    /* Alpha{+ new+} [-beta-]|-Gamma|De|+lta */
    ow_editor *ed;
    int para;
    size_t off;
    if (!doc) { CHECK(0, "document"); return; }
    ed = ow_editor_new(doc);
    CHECK(ed && !ow_editor_final_view(ed), "changes shown at first");
    if (!ed) { owf_doc_free(doc); return; }
    /* shown: Right from before "beta" goes into it */
    caret(ed, 0, 10);
    ow_editor_move_caret(ed, OW_MOVE_RIGHT, 0);
    off = focus_at(ed, &para);
    ow_editor_move_caret(ed, OW_MOVE_RIGHT, 0);       /* (the first may only cross from the space's run to beta's) */
    off = focus_at(ed, &para);
    CHECK(para == 0 && off == 11, "shown, the caret goes into the deletion");
    /* hidden: the deletion is the paragraph's end as the reader sees it, so Right goes to the next paragraph */
    ow_editor_set_final_view(ed, 1);
    CHECK(ow_editor_final_view(ed), "final view on");
    caret(ed, 0, 10);
    ow_editor_move_caret(ed, OW_MOVE_RIGHT, 0);
    off = focus_at(ed, &para);
    CHECK(para == 1 && off == 0, "hidden, Right steps over the deletion to the next paragraph");
    ow_editor_move_caret(ed, OW_MOVE_LEFT, 0);
    off = focus_at(ed, &para);
    CHECK(para == 0, "Left goes back to the paragraph's end");
    ow_editor_move_caret(ed, OW_MOVE_LEFT, 0);
    off = focus_at(ed, &para);
    CHECK(para == 0 && off == 9, "then over the deletion and the space before it, one step");
    /* a caret put in the deletion's middle comes out of it with the next move */
    caret(ed, 0, 12);
    ow_editor_move_caret(ed, OW_MOVE_END, 0);
    off = focus_at(ed, &para);
    CHECK(para == 0 && off == 14, "End: the paragraph's end");
    caret(ed, 0, 12);
    ow_editor_move_caret(ed, OW_MOVE_UP, 0);
    off = focus_at(ed, &para);
    CHECK(para == 0 && (off <= 10 || off == 14), "Up from inside it stops outside it");
    /* the document still has every change */
    CHECK(!strcmp(dump(doc), changed_dump), dump(doc));
    ow_editor_free(ed);
    owf_doc_free(doc);

    /* Backspace and Delete take the character the reader sees, never the hidden deletion next to it (Agnus);
     * caret at 14 shows right after "new ", at 10 right before the paragraph's end */
    {
        static const struct { int track, backspace; size_t at; int para; size_t then; const char *dump; } keys[] = {
            { 0, 1, 14, 0, 9, "Alpha{+ new+}[-beta-]|-Gamma|De|+lta" },     /* the space, not "a" of beta */
            { 1, 1, 14, 0, 9, "Alpha{+ new+}[- -][-beta-]|-Gamma|De|+lta" },/* the space marked; caret before it */
            { 0, 0, 9, 0, 9, "Alpha{+ new+}[-beta-]|-Gamma|De|+lta" },      /* Delete: the space */
            { 1, 0, 10, 0, 10, "Alpha{+ new+} [-beta-]|-[-G-]amma|De|+lta" },/* past beta and the deleted end: G */
            /* not recording, the paragraph end between: only the join, beta whole (Agnus, 0146) */
            { 0, 1, 100, 0, 14, "Alpha{+ new+} [-beta-]Gamma|De|+lta" },    /* Backspace at Gamma's start */
            { 0, 0, 10, 0, 10, "Alpha{+ new+} [-beta-]Gamma|De|+lta" }      /* Delete at the end before it */
        };
        size_t k;
        for (k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
            doc = changed_doc();
            ed = doc ? ow_editor_new(doc) : NULL;
            if (!ed) { CHECK(0, "document"); owf_doc_free(doc); return; }
            ow_editor_set_final_view(ed, 1);
            ow_editor_set_track_changes(ed, keys[k].track);
            ow_editor_set_review_author(ed, "Cy");
            if (keys[k].at == 100) caret(ed, 1, 0);      /* (100: the next paragraph's start) */
            else caret(ed, 0, keys[k].at);
            CHECK((keys[k].backspace ? ow_editor_backspace(ed) : ow_editor_delete_forward(ed)) == OWF_OK, "key");
            CHECK(!strcmp(dump(doc), keys[k].dump), dump(doc));
            off = focus_at(ed, &para);
            CHECK(para == keys[k].para && off == keys[k].then, keys[k].backspace ? "caret after Backspace" : "caret after Delete");
            ow_editor_free(ed);
            owf_doc_free(doc);
        }
    }
}

/* 2.0.38: pictures in the line as tracked changes: one put in while recording is an insertion with the two
 * paragraph ends round it, one deleted is marked deleted (the author's own insertion goes), accepting and
 * rejecting take or keep them, and DOCX and ODT keep them */
static const unsigned char tiny_png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 1, 2, 3, 4 };

static owf_doc *picture_doc(int with_picture)
{
    static const char *const text[] = { "Alpha", "", "Omega" };
    owf_doc *doc = doc_of(text, with_picture ? 3 : 1);
    owf_charfmt cf;
    int img;
    if (!doc) return NULL;
    owf_charfmt_init(&cf);
    img = owf_doc_add_image(doc, "tiny.png", "image/png", tiny_png, sizeof tiny_png, 1440, 960, "tiny");
    if (img < 0 || (with_picture && owf_para_add_image(&doc->body.paras[1], &cf, img) != OWF_OK)) {
        owf_doc_free(doc);
        return NULL;
    }
    if (!with_picture) {
        owf_parafmt pf;
        owf_para *p;
        owf_parafmt_init(&pf);
        p = owf_story_add(&doc->body, &pf);
        if (!p || owf_para_add_text(p, &cf, "Omega", 5) != OWF_OK) { owf_doc_free(doc); return NULL; }
    }
    return doc;
}

static ow_editor *recording(owf_doc *doc, const char *author)
{
    ow_editor *ed = ow_editor_new(doc);
    if (!ed) return NULL;
    ow_editor_set_track_changes(ed, 1);
    ow_editor_set_review_author(ed, author);
    return ed;
}

static void test_pictures(void)
{
    owf_doc *doc = picture_doc(0), *back = NULL;
    ow_editor *ed;
    static const char *const inserted = "Alpha|+{+?+}|+|Omega";
    const char *formats[] = { "docx", "odt" };
    char path[64];
    int i;
    if (!doc || !(ed = recording(doc, "Ann"))) { CHECK(0, "document"); owf_doc_free(doc); return; }
    CHECK(!strcmp(dump(doc), "Alpha|Omega"), dump(doc));
    caret(ed, 0, 5);
    CHECK(ow_editor_insert_image(ed, 0) == OWF_OK, "picture put in while recording");
    CHECK(!strcmp(dump(doc), inserted), dump(doc));
    CHECK(OWF_DOC_REVISION(doc, doc->body.paras[1].runs[0].revision) &&
          !strcmp(OWF_DOC_REVISION(doc, doc->body.paras[1].runs[0].revision)->author, "Ann"), "Ann's insertion");
    /* both formats keep it */
    for (i = 0; i < 2; ++i) {
        snprintf(path, sizeof path, "/tmp/openwrite-track-picture.%s", formats[i]);
        CHECK(owf_export_file(doc, path, formats[i], NULL) == OWF_OK, formats[i]);
        back = NULL;
        CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, formats[i]);
        if (back) {
            CHECK(!strcmp(dump(back), inserted), dump(back));
            CHECK(back->nimages == 1, "the picture comes back");
        }
        owf_doc_free(back);
        remove(path);
    }
    /* rejected: as it was */
    CHECK(ow_editor_accept_all_changes(ed, 0) == OWF_OK, "reject all");
    CHECK(!strcmp(dump(doc), "Alpha|Omega"), dump(doc));
    /* Ann deletes her own: it goes, nothing is marked */
    caret(ed, 0, 5);
    CHECK(ow_editor_insert_image(ed, 0) == OWF_OK, "again");
    select_range(ed, 0, 5, 2, 0);                    /* the picture and the two ends round it, not the old end */
    ow_editor_delete_forward(ed);
    CHECK(!strcmp(dump(doc), "Alpha|Omega"), dump(doc));
    ow_editor_free(ed);
    owf_doc_free(doc);

    /* someone else's picture deleted: marked, then accepted (gone) or rejected (kept) */
    for (i = 0; i < 2; ++i) {
        doc = picture_doc(1);
        if (!doc || !(ed = recording(doc, "Bob"))) { CHECK(0, "document"); owf_doc_free(doc); return; }
        CHECK(!strcmp(dump(doc), "Alpha|?|Omega"), dump(doc));
        select_range(ed, 0, 5, 2, 0);
        ow_editor_delete_forward(ed);
        CHECK(!strcmp(dump(doc), "Alpha|-[-?-]|-Omega"), dump(doc));
        if (!i) {
            snprintf(path, sizeof path, "/tmp/openwrite-track-picture.docx");
            back = NULL;
            CHECK(owf_export_file(doc, path, "docx", NULL) == OWF_OK &&
                  owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, "deleted picture saved");
            CHECK(back && !strcmp(dump(back), "Alpha|-[-?-]|-Omega"), back ? dump(back) : "read");
            owf_doc_free(back);
            remove(path);
        }
        CHECK(ow_editor_accept_all_changes(ed, !i) == OWF_OK, i ? "reject all" : "accept all");
        CHECK(!strcmp(dump(doc), i ? "Alpha|?|Omega" : "AlphaOmega"), dump(doc));
        ow_editor_free(ed);
        owf_doc_free(doc);
    }
}

/* 2.0.38: frames (text boxes) as tracked changes: one put in while recording is an insertion (its run's),
 * Delete Frame marks someone else's deleted (the author's own goes), accepting and rejecting take the frame
 * or keep it, later frames renumbered; DOCX keeps them, ODT saves them accepted and says so */
static int frame_change(const owf_doc *doc, int frame_index)
{
    int i, j;
    for (i = 0; i < doc->body.nparas; ++i)
        for (j = 0; j < doc->body.paras[i].nruns; ++j)
            if (doc->body.paras[i].runs[j].kind == OWF_RUN_FRAME && doc->body.paras[i].runs[j].image == frame_index) {
                const owf_revision *rv = OWF_DOC_REVISION(doc, doc->body.paras[i].runs[j].revision);
                return rv ? (rv->kind == OWF_REV_DELETE ? -1 : 1) : 0;
            }
    return -2;                                       /* no run */
}

static int report_has(const owf_report *rp, const char *s)
{
    int i;
    for (i = 0; i < owf_report_count(rp); ++i) {
        const char *note = owf_report_note(rp, i, NULL, NULL);
        if (note && strstr(note, s)) return 1;
    }
    return 0;
}

static void test_frames(void)
{
    static const char *const text[] = { "Alpha", "Omega" };
    owf_doc *doc = doc_of(text, 2), *back = NULL;
    ow_editor *ed;
    owf_report *rp;
    const char *path = "/tmp/openwrite-track-frame.docx", *odt = "/tmp/openwrite-track-frame.odt";
    int i;
    if (!doc || !(ed = recording(doc, "Ann"))) { CHECK(0, "document"); owf_doc_free(doc); return; }
    caret(ed, 0, 2);
    CHECK(ow_editor_insert_text_box(ed, "Box", 2880, 1440, OWF_WRAP_SQUARE) == OWF_OK, "text box while recording");
    CHECK(doc->nframes == 1 && frame_change(doc, 0) == 1, "an insertion");
    CHECK(!strcmp(OWF_DOC_REVISION(doc, doc->body.paras[0].runs[doc->body.paras[0].nruns - 1].revision)->author, "Ann"),
          "Ann's");
    /* DOCX keeps it; ODT saves it accepted and says so */
    CHECK(owf_export_file(doc, path, "docx", NULL) == OWF_OK &&
          owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, "docx");
    CHECK(back && back->nframes == 1 && frame_change(back, 0) == 1, "docx keeps the frame's insertion");
    owf_doc_free(back);
    back = NULL;
    rp = owf_report_new();
    CHECK(rp && owf_export_file(doc, odt, "odt", rp) == OWF_OK, "odt");
    CHECK(report_has(rp, "text box and frame changes were saved accepted"), "odt says so");
    owf_report_free(rp);
    CHECK(owf_import_file(odt, NULL, &back, NULL, NULL) == OWF_OK && back && back->nframes == 1 &&
          frame_change(back, 0) == 0, "odt: the frame, accepted");
    owf_doc_free(back);
    back = NULL;
    /* rejected: gone, and the caret back in the body */
    CHECK(ow_editor_accept_all_changes(ed, 0) == OWF_OK, "reject all");
    CHECK(doc->nframes == 0 && frame_change(doc, 0) == -2, "the frame is gone");
    CHECK(ow_editor_selection(ed).focus.box == 0, "the caret in the body");
    CHECK(!strcmp(dump(doc), "Alpha|Omega"), dump(doc));
    CHECK(ow_editor_undo(ed) == 1 && doc->nframes == 1 && frame_change(doc, 0) == 1, "undone: back");
    /* Ann deletes her own: it goes */
    CHECK(ow_editor_delete_frame(ed, 0) == OWF_OK && doc->nframes == 0, "her own frame goes");
    ow_editor_free(ed);
    owf_doc_free(doc);

    /* two frames from before; Bob deletes the first: marked, then accepted (gone, the second renumbered) or
     * rejected (kept) */
    for (i = 0; i < 2; ++i) {
        doc = doc_of(text, 2);
        ed = doc ? ow_editor_new(doc) : NULL;
        if (!ed) { CHECK(0, "document"); owf_doc_free(doc); return; }
        caret(ed, 0, 2);
        CHECK(ow_editor_insert_text_box(ed, "One", 2880, 1440, OWF_WRAP_SQUARE) == OWF_OK, "first");
        caret(ed, 1, 2);
        CHECK(ow_editor_insert_text_box(ed, "Two", 2880, 1440, OWF_WRAP_SQUARE) == OWF_OK, "second");
        caret(ed, 0, 0);
        ow_editor_set_track_changes(ed, 1);
        ow_editor_set_review_author(ed, "Bob");
        CHECK(ow_editor_delete_frame(ed, 0) == OWF_OK, "delete frame while recording");
        CHECK(doc->nframes == 2 && frame_change(doc, 0) == -1 && frame_change(doc, 1) == 0, "marked deleted, kept");
        CHECK(ow_editor_delete_frame(ed, 0) == OWF_OK && doc->nframes == 2, "again: nothing more");
        if (!i) {
            CHECK(owf_export_file(doc, path, "docx", NULL) == OWF_OK &&
                  owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, "docx");
            CHECK(back && back->nframes == 2 && frame_change(back, 0) == -1, "docx keeps the deletion");
            owf_doc_free(back);
            back = NULL;
        }
        CHECK(ow_editor_accept_all_changes(ed, !i) == OWF_OK, i ? "reject all" : "accept all");
        if (!i) {
            CHECK(doc->nframes == 1 && frame_change(doc, 0) == 0 && doc->frames[0].story.nparas &&
                  doc->frames[0].story.paras[0].nruns && doc->frames[0].story.paras[0].runs[0].text &&
                  !strcmp(doc->frames[0].story.paras[0].runs[0].text, "Two"), "the first gone, Two now frame 0");
        } else {
            CHECK(doc->nframes == 2 && frame_change(doc, 0) == 0 && frame_change(doc, 1) == 0, "both kept");
        }
        ow_editor_free(ed);
        owf_doc_free(doc);
    }
    remove(path);
    remove(odt);
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
    test_format_changes();
    test_final_view();
    test_pictures();
    test_frames();
    printf("track changes: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

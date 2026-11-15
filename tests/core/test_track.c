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

static const char *const format_names[] = { "[Content_Types].xml", "_rels/.rels", "word/document.xml" };
static const char *const format_parts[] = {
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

static void test_format_changes(void)
{
    static const char want[] = "<Plain ><unbolded was b Gil 2026-11-03T11:00:00Z>< then ><bolded b was i Gil 2026-11-03T11:01:00Z>"
                               "<both u ins was Hal ->";
    owf_doc *doc = NULL, *back = NULL, *view = NULL;
    owf_report *rp = owf_report_new();
    char first[1024];
    int i, kept = 0;
    CHECK(rp && zip_import(format_names, format_parts, 3, &doc) == OWF_OK && doc, "Word's format changes");
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

/* Accepting and rejecting format changes in the editor: the one at the caret, then all; undo */
static void test_format_accept(void)
{
    static const char want[] = "<Plain ><unbolded was b Gil 2026-11-03T11:00:00Z>< then ><bolded b was i Gil 2026-11-03T11:01:00Z>"
                               "<both u ins was Hal ->";
    owf_doc *doc = NULL;
    ow_editor *ed;
    int i;
    CHECK(zip_import(format_names, format_parts, 3, &doc) == OWF_OK && doc, "Word's format changes");
    if (!doc) return;
    ed = ow_editor_new(doc);
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    /* reject the one at the caret, in "bolded" */
    caret(ed, 0, 22);
    CHECK(ow_editor_accept_change(ed, 0) == OWF_OK, "reject at the caret");
    CHECK(!strcmp(format_changes_dump(doc), "<Plain ><unbolded was b Gil 2026-11-03T11:00:00Z>< then ><bolded i>"
                                            "<both u ins was Hal ->"), format_changes_dump(doc));
    CHECK(ow_editor_undo(ed) == 1 && !strcmp(format_changes_dump(doc), want), format_changes_dump(doc));
    /* accept the one at the caret, in "unbolded": it joins the plain text before and after it */
    caret(ed, 0, 8);
    CHECK(ow_editor_accept_change(ed, 1) == OWF_OK, "accept at the caret");
    CHECK(!strcmp(format_changes_dump(doc), "<Plain unbolded then ><bolded b was i Gil 2026-11-03T11:01:00Z>"
                                            "<both u ins was Hal ->"), format_changes_dump(doc));
    CHECK(ow_editor_undo(ed) == 1 && !strcmp(format_changes_dump(doc), want), format_changes_dump(doc));
    /* all: accepted, the formats now and the insertion kept; rejected, the formats before and no insertion */
    CHECK(ow_editor_accept_all_changes(ed, 1) == OWF_OK, "accept all");
    CHECK(!strcmp(format_changes_dump(doc), "<Plain unbolded then ><bolded b><both u>"), format_changes_dump(doc));
    CHECK(!owf_doc_has_changes(doc), "none left");
    CHECK(ow_editor_undo(ed) == 1 && !strcmp(format_changes_dump(doc), want), format_changes_dump(doc));
    CHECK(ow_editor_accept_all_changes(ed, 0) == OWF_OK, "reject all");
    CHECK(!strcmp(format_changes_dump(doc), "<Plain ><unbolded b>< then ><bolded i>"), format_changes_dump(doc));
    /* a caret with no change anywhere near it: nothing to do */
    CHECK(ow_editor_undo(ed) == 1, "undo reject all");
    caret(ed, 0, 2);
    CHECK(ow_editor_accept_change(ed, 1) == OWF_ERR_UNSUPPORTED, "no change at the caret");
    for (i = 0; i < 2; ++i) CHECK(doc->nformat_changes == 3, "the list stays");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

/* Next and Previous Change stop at format changes, and say what they are; a complex field whose runs all carry
 * the change (as Word writes them) gets it, with no change left over for its begin and code runs */
static void test_format_nav(void)
{
    static const char *const fparts[] = {
        NULL, NULL,
        "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        "<w:p><w:r><w:t xml:space=\"preserve\">Title: </w:t></w:r>"
        "<w:r><w:rPr><w:b/><w:rPrChange w:id=\"1\" w:author=\"Gil\"><w:rPr/></w:rPrChange></w:rPr><w:fldChar w:fldCharType=\"begin\"/></w:r>"
        "<w:r><w:rPr><w:b/><w:rPrChange w:id=\"2\" w:author=\"Gil\"><w:rPr/></w:rPrChange></w:rPr><w:instrText xml:space=\"preserve\"> TITLE </w:instrText></w:r>"
        "<w:r><w:rPr><w:b/><w:rPrChange w:id=\"3\" w:author=\"Gil\"><w:rPr/></w:rPrChange></w:rPr><w:fldChar w:fldCharType=\"separate\"/></w:r>"
        "<w:r><w:rPr><w:b/><w:rPrChange w:id=\"4\" w:author=\"Gil\"><w:rPr/></w:rPrChange></w:rPr><w:t>Lamps</w:t></w:r>"
        "<w:r><w:rPr><w:b/><w:rPrChange w:id=\"5\" w:author=\"Gil\"><w:rPr/></w:rPrChange></w:rPr><w:fldChar w:fldCharType=\"end\"/></w:r>"
        "</w:p><w:sectPr/></w:body></w:document>" };
    const char *parts[3];
    owf_doc *doc = NULL;
    ow_editor *ed;
    ow_change_info ci;
    int i, field = -1, used = 0;
    /* the format changes' document: three changes in a row, the second inside an insertion (counted as that) */
    CHECK(zip_import(format_names, format_parts, 3, &doc) == OWF_OK && doc, "Word's format changes");
    if (!doc) return;
    ed = ow_editor_new(doc);
    CHECK(ed != NULL, "editor");
    if (ed) {
        CHECK(ow_editor_change_count(ed) == 3, "two format changes and an insertion");
        caret(ed, 0, 0);
        CHECK(ow_editor_next_change(ed, 0) == 1 && ow_editor_current_change(ed, &ci) && ci.kind == OW_CHANGE_FORMAT &&
              ci.author && !strcmp(ci.author, "Gil") && ci.date && !strcmp(ci.date, "2026-11-03T11:00:00Z") && ci.index == 0,
              "the first is a format change by Gil");
        CHECK(ow_editor_next_change(ed, 0) == 1 && ow_editor_current_change(ed, &ci) && ci.kind == OW_CHANGE_FORMAT &&
              ci.index == 1, "then the second");
        CHECK(ow_editor_next_change(ed, 0) == 1 && ow_editor_current_change(ed, &ci) && ci.kind == OWF_REV_INSERT &&
              ci.author && !strcmp(ci.author, "Hal"), "then the insertion, with its own format change inside it");
        CHECK(ow_editor_next_change(ed, 1) == 1 && ow_editor_current_change(ed, &ci) && ci.index == 1, "back to the second");
        ow_editor_free(ed);
    }
    owf_doc_free(doc);

    parts[0] = format_parts[0];
    parts[1] = format_parts[1];
    parts[2] = fparts[2];
    doc = NULL;
    CHECK(zip_import(format_names, parts, 3, &doc) == OWF_OK && doc, "a changed complex field");
    if (!doc) return;
    for (i = 0; i < doc->body.paras[0].nruns; ++i) {
        const owf_run *r = &doc->body.paras[0].runs[i];
        if (r->kind == OWF_RUN_FIELD) field = i;
        if (r->format_change) ++used;
    }
    CHECK(field >= 0 && doc->body.paras[0].runs[field].format_change > 0 &&
          (doc->body.paras[0].runs[field].fmt.flags & OWF_BOLD), "the field has the change");
    CHECK(doc->nformat_changes == used && used == 1, "no change is left over for the field's other runs");
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
    test_format_changes();
    test_format_accept();
    test_format_nav();
    printf("track changes: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

/*
 * GFOpenWrite's footnotes and endnotes (core 1.5, libowf 1.8, layout 2.4.0): made, numbered, changed, made
 * the other kind and deleted in the editor (and undone); through DOCX and ODT; RTF, HTML and plain text keep
 * their text; and on the pages, each footnote at the foot of its mark's page and the endnotes after the
 * text. Word's and OpenOffice's forms are made here by hand from the formats' specifications (ECMA-376 part
 * 1, 17.11; ODF 1.3 part 3, 6.3), so no document from either program goes into the repository.
 * make notes-test, make notes-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"
#include "ow_layout.h"
#include "owf_internal.h"
#include "owf_note.h"

static int checks, failures;

#define CHECK(cond, what)                                                                                  \
    do {                                                                                                    \
        ++checks;                                                                                           \
        if (!(cond)) {                                                                                      \
            ++failures;                                                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);                                            \
        }                                                                                                   \
    } while (0)

/* The body's text with each note as [F:text] or [E:text], paragraphs between '|' */
static const char *dump(const owf_doc *doc)
{
    static char out[4096];
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
            if (r->kind == OWF_RUN_TEXT && r->note > 0 && r->note <= doc->nnotes) {
                PUT(doc->notes[r->note - 1].endnote ? "[E:" : "[F:");
                PUT(doc->notes[r->note - 1].text);
                PUT("]");
            } else if (r->kind == OWF_RUN_TEXT && r->text) {
                PUT(r->text);
            }
        }
        if (i + 1 < doc->body.nparas) PUT("|");
    }
#undef PUT
    return out;
}

static owf_doc *doc_of(int n)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    int i;
    char line[160];
    owf_charfmt_init(&cf);
    for (i = 0; doc && i < n; ++i) {
        owf_para *p;
        owf_parafmt_init(&pf);
        p = owf_story_add(&doc->body, &pf);
        if (n <= 4)
            snprintf(line, sizeof line, "Paragraph %d.", i + 1);
        else
            snprintf(line, sizeof line, "Paragraph %d has enough words in it to fill two lines of the page or more "
                                        "when it is laid out at the width of the body.", i + 1);
        if (!p || owf_para_add_text(p, &cf, line, strlen(line)) != 0) {
            owf_doc_free(doc);
            return NULL;
        }
    }
    return doc;
}

static void caret_end(ow_editor *ed, int paragraph)
{
    owf_doc *doc = ow_editor_document(ed);
    owf_para *p = &doc->body.paras[paragraph];
    ow_selection sel;
    int j;
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = paragraph;
    for (j = p->nruns - 1; j >= 0; --j)
        if (p->runs[j].kind == OWF_RUN_TEXT && p->runs[j].text && !p->runs[j].note) {
            sel.anchor.run = j;
            sel.anchor.byte_offset = strlen(p->runs[j].text);
            break;
        }
    sel.focus = sel.anchor;
    ow_editor_set_selection(ed, &sel);
}

static void test_editing(void)
{
    owf_doc *doc = doc_of(3);
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    ow_note_info info;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    CHECK(ow_editor_note_count(ed) == 0 && !ow_editor_current_note(ed, &info), "no notes");
    CHECK(ow_editor_delete_note(ed) == OWF_ERR_UNSUPPORTED, "none to delete");
    caret_end(ed, 1);
    CHECK(ow_editor_insert_note(ed, 0, "A footnote.") == OWF_OK, "footnote");
    CHECK(!strcmp(dump(doc), "Paragraph 1.|Paragraph 2.[F:A footnote.]|Paragraph 3."), dump(doc));
    /* typing goes after the mark */
    ow_editor_insert_utf8(ed, " More", 5);
    CHECK(!strcmp(dump(doc), "Paragraph 1.|Paragraph 2.[F:A footnote.] More|Paragraph 3."), dump(doc));
    caret_end(ed, 0);
    CHECK(ow_editor_insert_note(ed, 0, "First.") == OWF_OK, "a footnote before it");
    caret_end(ed, 2);
    CHECK(ow_editor_insert_note(ed, 1, "An endnote.\nIts second paragraph.") == OWF_OK, "endnote");
    CHECK(ow_editor_note_count(ed) == 3, "three notes");
    CHECK(ow_editor_note_info(ed, 0, &info) && !strcmp(info.mark, "1") && !info.endnote &&
          !strcmp(info.text, "First."), info.mark);
    CHECK(ow_editor_note_info(ed, 1, &info) && !strcmp(info.mark, "2") && !strcmp(info.text, "A footnote."), info.mark);
    CHECK(ow_editor_note_info(ed, 2, &info) && !strcmp(info.mark, "i") && info.endnote, info.mark);
    CHECK(!ow_editor_note_info(ed, 3, &info), "no fourth");
    /* the caret after the endnote's mark: it is the current note */
    CHECK(ow_editor_current_note(ed, &info) && info.index == 2, "current");
    /* next and previous go round */
    CHECK(ow_editor_next_note(ed, 0) == 2 && ow_editor_current_note(ed, &info) && info.index == 0, "round to the first");
    CHECK(ow_editor_next_note(ed, 0) == 1 && ow_editor_current_note(ed, &info) && info.index == 1, "next");
    CHECK(ow_editor_next_note(ed, 1) == 1 && ow_editor_current_note(ed, &info) && info.index == 0, "previous");
    /* changed, made an endnote, deleted, and undone */
    CHECK(ow_editor_set_note_text(ed, "The first.") == OWF_OK, "text changed");
    CHECK(!strcmp(dump(doc), "Paragraph 1.[F:The first.]|Paragraph 2.[F:A footnote.] More|Paragraph 3."
                             "[E:An endnote.\nIts second paragraph.]"), dump(doc));
    CHECK(ow_editor_convert_note(ed) == OWF_OK && ow_editor_current_note(ed, &info) && info.endnote &&
          !strcmp(info.mark, "i"), "made an endnote");
    CHECK(ow_editor_note_info(ed, 2, &info) && !strcmp(info.mark, "ii"), "the other endnote renumbered");
    CHECK(ow_editor_note_info(ed, 1, &info) && !strcmp(info.mark, "1"), "the footnote renumbered");
    CHECK(ow_editor_delete_note(ed) == OWF_OK && ow_editor_note_count(ed) == 2, "deleted");
    CHECK(!strcmp(dump(doc), "Paragraph 1.|Paragraph 2.[F:A footnote.] More|Paragraph 3."
                             "[E:An endnote.\nIts second paragraph.]"), dump(doc));
    ow_editor_undo(ed);
    CHECK(ow_editor_note_count(ed) == 3 && strstr(dump(doc), "Paragraph 1.[E:The first.]") != NULL, dump(doc));
    ow_editor_undo(ed);
    ow_editor_undo(ed);
    CHECK(strstr(dump(doc), "Paragraph 1.[F:First.]") != NULL, dump(doc));
    /* bold over the mark keeps it */
    {
        ow_selection sel;
        memset(&sel, 0, sizeof sel);
        sel.focus.paragraph = 1;
        sel.focus.run = doc->body.paras[1].nruns - 1;
        sel.focus.byte_offset = 2;
        ow_editor_set_selection(ed, &sel);
        ow_editor_toggle_char_flags(ed, OWF_BOLD);
        CHECK(ow_editor_note_count(ed) == 3 && strstr(dump(doc), "Paragraph 2.[F:A footnote.] More"), dump(doc));
    }
    ow_editor_free(ed);
    owf_doc_free(doc);
}

/* A document with a footnote (two paragraphs of text) and an endnote */
static owf_doc *noted_doc(void)
{
    owf_doc *doc = doc_of(2);
    owf_charfmt cf;
    int f, e;
    if (!doc) return NULL;
    owf_charfmt_init(&cf);
    f = owf_note_add(doc, 0, "See the manual.\nPage 4.", NULL);
    e = owf_note_add(doc, 1, "Ends & notes.", NULL);
    if (f < 0 || e < 0) { owf_doc_free(doc); return NULL; }
    {
        owf_para *p = &doc->body.paras[0];
        owf_run *runs = (owf_run *)realloc(p->runs, (size_t)(p->nruns + 2) * sizeof(*runs));
        if (!runs) { owf_doc_free(doc); return NULL; }
        p->runs = runs;
        p->capruns = p->nruns + 2;
        memset(&p->runs[p->nruns], 0, sizeof(owf_run));
        p->runs[p->nruns].kind = OWF_RUN_TEXT;
        p->runs[p->nruns].fmt = cf;
        p->runs[p->nruns].text = (char *)calloc(1, 1);
        p->runs[p->nruns].note = f + 1;
        ++p->nruns;
    }
    {
        owf_para *p = &doc->body.paras[1];
        owf_run *runs = (owf_run *)realloc(p->runs, (size_t)(p->nruns + 1) * sizeof(*runs));
        if (!runs) { owf_doc_free(doc); return NULL; }
        p->runs = runs;
        p->capruns = p->nruns + 1;
        memset(&p->runs[p->nruns], 0, sizeof(owf_run));
        p->runs[p->nruns].kind = OWF_RUN_TEXT;
        p->runs[p->nruns].fmt = cf;
        p->runs[p->nruns].text = (char *)calloc(1, 1);
        p->runs[p->nruns].note = e + 1;
        ++p->nruns;
    }
    return doc;
}

static const char *const noted = "Paragraph 1.[F:See the manual.\nPage 4.]|Paragraph 2.[E:Ends & notes.]";

static void test_round_trip(const char *format)
{
    owf_doc *doc = noted_doc(), *back = NULL;
    char path[64];
    CHECK(doc != NULL, "document");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), noted), dump(doc));
    snprintf(path, sizeof path, "/tmp/openwrite-notes.%s", format);
    CHECK(owf_export_file(doc, path, format, NULL) == OWF_OK, format);
    CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, format);
    if (back) {
        CHECK(!strcmp(dump(back), noted), dump(back));
        owf_doc_free(back);
    }
    owf_doc_free(doc);
}

/* RTF, HTML and plain text: the notes' text is in the file */
static void test_listed(const char *format, const char *const *want, int n)
{
    owf_doc *doc = noted_doc();
    unsigned char *data = NULL;
    size_t length = 0;
    char *text;
    int k;
    if (!doc) { CHECK(0, "document"); return; }
    {
        const owf_format *f = owf_format_named(format);
        CHECK(f && f->export && f->export(doc, &data, &length, NULL) == OWF_OK && data, format);
    }
    text = (char *)calloc(1, length + 1);
    if (text && data) memcpy(text, data, length);
    for (k = 0; text && k < n; ++k)
        CHECK(strstr(text, want[k]) != NULL, want[k]);
    free(text);
    free(data);
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

#define W "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
#define R "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""

/* Word's form: separators, footnoteRef, a footnote and an endnote */
static void test_word_form(void)
{
    static const char *const names[] = { "[Content_Types].xml", "_rels/.rels", "word/_rels/document.xml.rels",
                                         "word/document.xml", "word/footnotes.xml", "word/endnotes.xml" };
    static const char *const parts[] = {
        "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>",
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>",
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId4\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes\" Target=\"footnotes.xml\"/>"
        "<Relationship Id=\"rId5\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/endnotes\" Target=\"endnotes.xml\"/>"
        "</Relationships>",
        "<?xml version=\"1.0\"?><w:document " W " " R "><w:body>"
        "<w:p><w:r><w:t>Lamps</w:t></w:r><w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr>"
        "<w:footnoteReference w:id=\"2\"/></w:r><w:r><w:t xml:space=\"preserve\"> glow</w:t></w:r>"
        "<w:r><w:endnoteReference w:id=\"1\"/></w:r></w:p>"
        "</w:body></w:document>",
        "<?xml version=\"1.0\"?><w:footnotes " W ">"
        "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
        "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>"
        "<w:footnote w:id=\"2\"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t xml:space=\"preserve\"> Mostly.</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>Not all.</w:t></w:r></w:p></w:footnote>"
        "</w:footnotes>",
        "<?xml version=\"1.0\"?><w:endnotes " W ">"
        "<w:endnote w:id=\"1\"><w:p><w:r><w:endnoteRef/></w:r><w:r><w:t xml:space=\"preserve\"> The end.</w:t></w:r></w:p></w:endnote>"
        "</w:endnotes>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 6, &doc) == OWF_OK && doc, "Word's form read");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Lamps[F:Mostly.\nNot all.] glow[E:The end.]"), dump(doc));
    owf_doc_free(doc);
}

#define ODF_NS "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" " \
               "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""

/* OpenOffice's form: text:note with its citation and body */
static void test_odf_form(void)
{
    static const char *const names[] = { "mimetype", "content.xml" };
    static const char *const parts[] = {
        "application/vnd.oasis.opendocument.text",
        "<?xml version=\"1.0\"?><office:document-content " ODF_NS "><office:body><office:text>"
        "<text:p>Lamps<text:note text:id=\"ftn1\" text:note-class=\"footnote\"><text:note-citation>1</text:note-citation>"
        "<text:note-body><text:p>Mostly.</text:p><text:p>Not all.</text:p></text:note-body></text:note> glow"
        "<text:note text:id=\"ftn2\" text:note-class=\"endnote\"><text:note-citation text:label=\"*\">*</text:note-citation>"
        "<text:note-body><text:p>The end.</text:p></text:note-body></text:note></text:p>"
        "</office:text></office:body></office:document-content>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 2, &doc) == OWF_OK && doc, "OpenOffice's form read");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Lamps[F:Mostly.\nNot all.] glow[E:The end.]"), dump(doc));
    CHECK(doc->nnotes == 2 && doc->notes[1].label && !strcmp(doc->notes[1].label, "*"), "its own mark kept");
    owf_doc_free(doc);
}

/* half the size a character */
static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * style->font_size / 2;
    m->ascent = style->font_size * 4 / 5;
    m->descent = style->font_size / 5;
    return 1;
}

/* The page a body run's node is drawn on, -1: none */
static int page_of_run(ow_layout *l, int para, int run)
{
    ow_position pos;
    int page = -1;
    ol_rect box;
    memset(&pos, 0, sizeof pos);
    pos.paragraph = para;
    pos.run = run;
    return ow_layout_caret(l, pos, &page, &box) ? page : -1;
}

static void test_pages(void)
{
    owf_doc *doc = doc_of(100);
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    ow_layout *l, *plain;
    int k, pages, plain_pages, found = 0;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    plain = ow_layout_new(doc, measure, NULL);
    plain_pages = plain ? ow_layout_page_count(plain) : 0;
    ow_layout_free(plain);
    CHECK(plain_pages > 3, "pages");
    caret_end(ed, 0);
    ow_editor_insert_note(ed, 0, "The first footnote, on the first page.");
    caret_end(ed, 40);
    ow_editor_insert_note(ed, 0, "A footnote further on.\nWith a second paragraph.");
    caret_end(ed, 99);
    ow_editor_insert_note(ed, 1, "An endnote, after the text.");
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL, "laid out");
    if (!l) { ow_editor_free(ed); owf_doc_free(doc); return; }
    pages = ow_layout_page_count(l);
    CHECK(pages >= plain_pages, "no fewer pages");
    /* each footnote at the foot of its mark's page, below the text */
    for (k = 0; k < pages; ++k) {
        ol_unit x = 0, y = 0;
        ol_document *feet = ow_layout_footnotes(l, k, &x, &y);
        if (!feet) continue;
        ++found;
        {
            const ol_page *pg = ow_layout_page(l);
            ol_document *body = ow_layout_body(l);
            size_t first, count, i;
            CHECK(y > pg->margin_top && y < pg->height - pg->margin_bottom, "the notes' place");
            CHECK(ol_display_page_range(body, k, &first, &count), "the page's ops");
            for (i = first; i < first + count; ++i) {
                const ol_display_op *op = ol_display_get(body, i);
                if (op->type == OL_OP_TEXT)
                    CHECK(op->u.text.bounds.y + op->u.text.bounds.height <= y - OW_LAYOUT_NOTE_SEPARATOR / 2, "text above them");
            }
        }
        ol_document_free(feet);
    }
    CHECK(found == 2, "two pages with footnotes");
    {
        int p0 = -1, j;
        for (j = 0; j < doc->body.paras[0].nruns; ++j)
            if (doc->body.paras[0].runs[j].note) p0 = page_of_run(l, 0, j);
        CHECK(p0 == 0 && ow_layout_footnote_count(l, 0) == 1, "the first on page 1");
        for (j = 0; j < doc->body.paras[40].nruns; ++j)
            if (doc->body.paras[40].runs[j].note) p0 = page_of_run(l, 40, j);
        CHECK(p0 > 0 && ow_layout_footnote_count(l, p0) == 1, "the second on its mark's page");
    }
    {
        char mark[16];
        CHECK(ow_layout_note_mark(doc, 0, mark, sizeof mark) && !strcmp(mark, "1"), mark);
        CHECK(ow_layout_note_mark(doc, 2, mark, sizeof mark) && !strcmp(mark, "i"), mark);
        CHECK(!ow_layout_note_mark(doc, 9, mark, sizeof mark), "no such note");
    }
    ow_layout_free(l);
    ow_editor_free(ed);
    owf_doc_free(doc);
}

int main(void)
{
    static const char *const rtf[] = { "{\\footnote", "\\ftnalt", "See the manual.", "\\par Page 4.", "Ends & notes." };
    static const char *const html[] = { "<sup class=\"ow-note-mark\"><a id=\"ow-noteref1\" href=\"#ow-note1\">1</a></sup>",
                                        "See the manual.<br>Page 4.", "Ends &amp; notes.", "ow-endnotes" };
    static const char *const txt[] = { "Paragraph 1.[1]", "Paragraph 2.[i]", "Notes\n1\tSee the manual. Page 4.",
                                       "Endnotes\ni\tEnds & notes." };
    test_editing();
    test_round_trip("docx");
    test_round_trip("odt");
    test_listed("rtf", rtf, 5);
    test_listed("html", html, 4);
    test_listed("text", txt, 4);
    test_word_form();
    test_odf_form();
    test_pages();
    printf("notes: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

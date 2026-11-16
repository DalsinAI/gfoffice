/*
 * GFOpenWrite's footnotes and endnotes (core 1.5, libowf 1.8, layout 2.4.0): made, numbered, changed, made
 * the other kind and deleted in the editor (and undone); through DOCX and ODT; RTF, HTML and plain text keep
 * their text; and on the pages, each footnote at the foot of its mark's page and the endnotes after the
 * text. Word's and OpenOffice's forms are made here by hand from the formats' specifications (ECMA-376 part
 * 1, 17.11; ODF 1.3 part 3, 6.3), so no document from either program goes into the repository. libowf 1.12.2:
 * notes with formats (bold and the rest) read from DOCX, written back, laid out and kept by the editor.
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
#include "owf_zip.h"

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

/* Word's RTF form (RTF 1.9.1, "Footnotes"): the mark's {\super\chftn}, then the note's group with its
 * own {\super\chftn} first; \ftnalt makes it an endnote. Made here by hand from the specification. */
static void test_rtf_form(void)
{
    static const char doc_rtf[] =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}"
        "\\pard\\plain Lamps{\\cs16\\super\\chftn}{\\footnote\\pard\\plain\\s15{\\cs16\\super\\chftn} Mostly"
        "{\\*\\bkmkstart x}{\\*\\bkmkend x}.\\par \\pard Not \\u233?t\\'e9 all.} glow"
        "{\\super\\chftn}{\\footnote\\ftnalt\\pard\\plain{\\super\\chftn} The\\tab end.\\par }\\par}";
    owf_doc *doc = NULL;
    CHECK(owf_import_memory((const unsigned char *)doc_rtf, sizeof doc_rtf - 1, NULL, &doc, NULL, NULL) == OWF_OK && doc,
          "Word's RTF form read");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Lamps[F:Mostly.\nNot \xc3\xa9t\xc3\xa9 all.] glow[E:The\tend.]"), dump(doc));
    CHECK(doc->nnotes == 2 && !doc->notes[0].endnote && doc->notes[1].endnote, "a footnote, then an endnote");
    owf_doc_free(doc);
    {
        /* an empty note (Lisette, #218): its buffer has no data, and the note is kept with no text */
        static const char empty_rtf[] = "{\\rtf1 A{\\footnote}B{\\footnote  \\par }C\\par}";
        doc = NULL;
        CHECK(owf_import_memory((const unsigned char *)empty_rtf, sizeof empty_rtf - 1, NULL, &doc, NULL, NULL) == OWF_OK && doc,
              "empty notes read");
        if (!doc) return;
        CHECK(!strcmp(dump(doc), "A[F:]B[F:]C"), dump(doc));
        CHECK(doc->nnotes == 2 && !*doc->notes[0].text && !*doc->notes[1].text, "empty notes have empty text");
        owf_doc_free(doc);
    }
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

/* The note a body paragraph's mark names (its index), -1: none */
static int note_in(const owf_doc *doc, int paragraph)
{
    int j;
    for (j = 0; j < doc->body.paras[paragraph].nruns; ++j)
        if (doc->body.paras[paragraph].runs[j].note > 0) return doc->body.paras[paragraph].runs[j].note - 1;
    return -1;
}

/* libowf 1.12.1 (2.0.40): numbering settings: each kind's format and first number, on the marks, in the editor
 * (undoable), through DOCX and ODT (ODF has no symbols), and on the pages */
static void test_numbering(void)
{
    owf_doc *doc = doc_of(4), *back = NULL;
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    ow_note_info info;
    char mark[32];
    int f = -1, st = -1, i;
    ow_layout *l;
    owf_report *rp = owf_report_new();
    /* the formats */
    owf_note_format_number(OWF_NOTE_FORMAT_ARABIC, 0, 12, mark, sizeof mark);
    CHECK(!strcmp(mark, "12"), mark);
    owf_note_format_number(OWF_NOTE_FORMAT_DEFAULT, 1, 4, mark, sizeof mark);
    CHECK(!strcmp(mark, "iv"), "an endnote's own: iv");
    owf_note_format_number(OWF_NOTE_FORMAT_DEFAULT, 0, 4, mark, sizeof mark);
    CHECK(!strcmp(mark, "4"), "a footnote's own: 4");
    owf_note_format_number(OWF_NOTE_FORMAT_UPPER_ROMAN, 0, 14, mark, sizeof mark);
    CHECK(!strcmp(mark, "XIV"), mark);
    owf_note_format_number(OWF_NOTE_FORMAT_LOWER_LETTER, 0, 1, mark, sizeof mark);
    CHECK(!strcmp(mark, "a"), mark);
    owf_note_format_number(OWF_NOTE_FORMAT_LOWER_LETTER, 0, 27, mark, sizeof mark);
    CHECK(!strcmp(mark, "aa"), "27: aa, as Word does");
    owf_note_format_number(OWF_NOTE_FORMAT_UPPER_LETTER, 0, 54, mark, sizeof mark);
    CHECK(!strcmp(mark, "BBB"), "54: BBB");
    owf_note_format_number(OWF_NOTE_FORMAT_SYMBOL, 0, 1, mark, sizeof mark);
    CHECK(!strcmp(mark, "*"), mark);
    owf_note_format_number(OWF_NOTE_FORMAT_SYMBOL, 0, 2, mark, sizeof mark);
    CHECK(!strcmp(mark, "\xe2\x80\xa0"), "2: a dagger");
    owf_note_format_number(OWF_NOTE_FORMAT_SYMBOL, 0, 4, mark, sizeof mark);
    CHECK(!strcmp(mark, "\xc2\xa7"), "4: a section sign");
    owf_note_format_number(OWF_NOTE_FORMAT_SYMBOL, 0, 6, mark, sizeof mark);
    CHECK(!strcmp(mark, "\xe2\x80\xa0\xe2\x80\xa0"), "6: two daggers");
    owf_note_format_number(OWF_NOTE_FORMAT_SYMBOL, 0, 400, mark, 8);
    CHECK(strlen(mark) < 8, "cut to the room");

    CHECK(ed != NULL && rp != NULL, "editor");
    if (!ed || !rp) { owf_report_free(rp); owf_doc_free(doc); return; }
    for (i = 0; i < 3; ++i) { caret_end(ed, i); ow_editor_insert_note(ed, 0, "A footnote."); }
    caret_end(ed, 3);
    ow_editor_insert_note(ed, 1, "An endnote.");
    CHECK(ow_editor_note_numbering(ed, 0, &f, &st) && f == OWF_NOTE_FORMAT_ARABIC && st == 1, "footnotes 1, 2 at first");
    CHECK(ow_editor_note_numbering(ed, 1, &f, &st) && f == OWF_NOTE_FORMAT_LOWER_ROMAN && st == 1, "endnotes i at first");
    CHECK(ow_editor_note_info(ed, 2, &info) && !strcmp(info.mark, "3"), info.mark);

    CHECK(ow_editor_set_note_numbering(ed, 0, OWF_NOTE_FORMAT_LOWER_LETTER, 3) == OWF_OK, "footnotes c, d, e");
    CHECK(ow_editor_set_note_numbering(ed, 1, OWF_NOTE_FORMAT_UPPER_ROMAN, 2) == OWF_OK, "endnotes II");
    CHECK(ow_editor_note_info(ed, 0, &info) && !strcmp(info.mark, "c") && ow_editor_note_info(ed, 2, &info) &&
          !strcmp(info.mark, "e") && ow_editor_note_info(ed, 3, &info) && !strcmp(info.mark, "II"), "the marks");
    owf_note_mark_text(doc, note_in(doc, 1), mark, sizeof mark);
    CHECK(!strcmp(mark, "d"), "libowf's mark the same");
    CHECK(ow_editor_set_note_numbering(ed, 0, OWF_NOTE_FORMAT_COUNT, 1) == OWF_ERR_FORMAT &&
          ow_editor_set_note_numbering(ed, 0, 1, -1) == OWF_ERR_FORMAT, "no such format or start");
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l && ow_layout_note_mark(doc, note_in(doc, 0), mark,
                                   sizeof mark) && !strcmp(mark, "c"), "the layout's mark the same");
    ow_layout_free(l);

    /* DOCX: w:footnotePr and w:endnotePr in the w:sectPr, before w:pgSz; back the same */
    CHECK(owf_export_file(doc, "/tmp/openwrite-notes-numbering.docx", "docx", NULL) == OWF_OK, "docx");
    CHECK(owf_import_file("/tmp/openwrite-notes-numbering.docx", NULL, &back, NULL, NULL) == OWF_OK && back, "docx back");
    if (back) {
        CHECK(back->page.note_format[0] == OWF_NOTE_FORMAT_LOWER_LETTER && back->page.note_start[0] == 3 &&
              back->page.note_format[1] == OWF_NOTE_FORMAT_UPPER_ROMAN && back->page.note_start[1] == 2,
              "docx keeps both kinds' numbering");
        owf_doc_free(back);
        back = NULL;
    }
    {
        unsigned char *data = NULL;
        size_t n = 0;
        const owf_format *fmt = owf_format_named("docx");
        CHECK(fmt && fmt->export(doc, &data, &n, NULL) == OWF_OK, "docx bytes");
        if (data) {
            owf_unzip z;
            unsigned char *xml = NULL;
            size_t xn = 0;
            CHECK(owf_unzip_open(&z, data, n) && owf_unzip_read(&z, "word/document.xml", &xml, &xn) == OWF_OK && xml,
                  "document.xml");
            if (xml) {
                CHECK(strstr((const char *)xml, "<w:footnotePr><w:numFmt w:val=\"lowerLetter\"/><w:numStart w:val=\"3\"/>"
                             "</w:footnotePr><w:endnotePr><w:numFmt w:val=\"upperRoman\"/><w:numStart w:val=\"2\"/>"
                             "</w:endnotePr><w:pgSz") != NULL, "Word's form, in CT_SectPr's order");
                owf_free(xml);
            }
            owf_free(data);
        }
    }
    /* ODT: text:notes-configuration, start-value LibreOffice's offset from 1 */
    CHECK(owf_export_file(doc, "/tmp/openwrite-notes-numbering.odt", "odt", NULL) == OWF_OK, "odt");
    CHECK(owf_import_file("/tmp/openwrite-notes-numbering.odt", NULL, &back, NULL, NULL) == OWF_OK && back, "odt back");
    if (back) {
        char m2[32];
        CHECK(back->page.note_format[0] == OWF_NOTE_FORMAT_LOWER_LETTER && back->page.note_start[0] == 3 &&
              back->page.note_format[1] == OWF_NOTE_FORMAT_UPPER_ROMAN && back->page.note_start[1] == 2,
              "odt keeps both kinds' numbering");
        owf_note_mark_text(back, note_in(back, 0), m2, sizeof m2);
        CHECK(!strcmp(m2, "c"), "the first footnote is still c");
        owf_doc_free(back);
        back = NULL;
    }
    /* symbols: ODT has none, so numbers, and the report says so; DOCX keeps Word's chicago */
    CHECK(ow_editor_set_note_numbering(ed, 0, OWF_NOTE_FORMAT_SYMBOL, 0) == OWF_OK &&
          ow_editor_note_info(ed, 1, &info) && !strcmp(info.mark, "\xe2\x80\xa0"), "symbols: a dagger second");
    {   /* the PDF draws the dagger in WinAnsi (0x86, written \206), not '?' */
        unsigned char *data = NULL;
        size_t n = 0, k;
        int dagger = 0;
        const owf_format *fmt = owf_format_named("pdf");
        CHECK(fmt && fmt->export(doc, &data, &n, NULL) == OWF_OK && data, "pdf");
        for (k = 0; data && k + 2 < n; ++k)
            if (k + 6 < n && !memcmp(data + k, "(\\206)", 6)) dagger = 1;
        CHECK(dagger, "the PDF's second mark is a dagger");
        owf_free(data);
    }
    CHECK(owf_export_file(doc, "/tmp/openwrite-notes-numbering.odt", "odt", rp) == OWF_OK, "odt with symbols");
    {
        int said = 0;
        for (i = 0; i < owf_report_count(rp); ++i) {
            const char *note = owf_report_note(rp, i, NULL, NULL);
            if (note && strstr(note, "ODT has no symbols")) said = 1;
        }
        CHECK(said, "the report says symbols went as numbers");
    }
    CHECK(owf_import_file("/tmp/openwrite-notes-numbering.odt", NULL, &back, NULL, NULL) == OWF_OK && back &&
          back->page.note_format[0] == OWF_NOTE_FORMAT_DEFAULT && back->page.note_start[0] == 0, "odt: 1, 2, 3");
    owf_doc_free(back);
    back = NULL;
    CHECK(owf_export_file(doc, "/tmp/openwrite-notes-numbering.docx", "docx", NULL) == OWF_OK &&
          owf_import_file("/tmp/openwrite-notes-numbering.docx", NULL, &back, NULL, NULL) == OWF_OK && back &&
          back->page.note_format[0] == OWF_NOTE_FORMAT_SYMBOL, "docx keeps symbols");
    owf_doc_free(back);
    back = NULL;

    /* undo, back to the usual: and nothing written for the usual numbering */
    ow_editor_undo(ed);
    ow_editor_undo(ed);
    ow_editor_undo(ed);
    CHECK(ow_editor_note_info(ed, 0, &info) && !strcmp(info.mark, "1") && ow_editor_note_info(ed, 3, &info) &&
          !strcmp(info.mark, "i"), "undone: 1 and i");
    CHECK(ow_editor_set_note_numbering(ed, 0, OWF_NOTE_FORMAT_ARABIC, 1) == OWF_OK &&
          doc->page.note_format[0] == OWF_NOTE_FORMAT_DEFAULT && doc->page.note_start[0] == 0, "the kind's own is the usual");
    ow_editor_free(ed);
    owf_report_free(rp);
    owf_doc_free(doc);
}

/* libowf 1.12.2: notes with formats (owf_doc.note_stories) */

/* The text op in a laid-out document whose text starts with s, NULL: none */
static const ol_display_op *op_with(const ol_document *od, const char *s)
{
    size_t i, n = ol_display_count(od);
    for (i = 0; i < n; ++i) {
        const ol_display_op *op = ol_display_get(od, i);
        const char *t;
        if (op->type != OL_OP_TEXT) continue;
        t = ol_node_text(ol_node_by_id(od, op->u.text.node_id));
        if (t && !strncmp(t + op->u.text.text_offset, s, strlen(s))) return op;
    }
    return NULL;
}

/* The run of a story whose text is s, NULL: none */
static const owf_run *story_run(const owf_story *st, const char *s)
{
    int i, j;
    for (i = 0; st && i < st->nparas; ++i)
        for (j = 0; j < st->paras[i].nruns; ++j)
            if (st->paras[i].runs[j].kind == OWF_RUN_TEXT && st->paras[i].runs[j].text &&
                !strcmp(st->paras[i].runs[j].text, s))
                return &st->paras[i].runs[j];
    return NULL;
}

/* How many times s is in a DOCX's part */
static int part_count(const unsigned char *data, size_t length, const char *part, const char *s)
{
    owf_unzip zip;
    unsigned char *xml = NULL;
    size_t n = 0;
    int count = 0;
    const char *at;
    if (!owf_unzip_open(&zip, data, length) || owf_unzip_read(&zip, part, &xml, &n) != OWF_OK) return -1;
    for (at = (const char *)xml; (at = strstr(at, s)) != NULL; at += strlen(s)) ++count;
    owf_free(xml);
    return count;
}

/* The formatted note's checks: its plain copy, the runs' formats, the plain note left plain */
static void check_formatted(const owf_doc *doc, const char *where)
{
    const owf_story *st;
    const owf_run *r;
    char what[96];
    snprintf(what, sizeof what, "%s: two notes, the one in the note none", where);
    CHECK(doc->nnotes == 2, what);
    if (doc->nnotes < 2) return;
    snprintf(what, sizeof what, "%s: plain copy", where);
    CHECK(!strcmp(doc->notes[0].text, "See bold and italic\nUnder"), doc->notes[0].text);
    st = OWF_DOC_NOTE_STORY(doc, 0);
    snprintf(what, sizeof what, "%s: formatted", where);
    CHECK(st && st->nparas == 2, what);
    if (!st) return;
    r = story_run(st, "See ");
    snprintf(what, sizeof what, "%s: leading space gone, plain run", where);
    CHECK(r && !(r->fmt.flags & (OWF_BOLD | OWF_ITALIC)), what);
    r = story_run(st, "bold");
    snprintf(what, sizeof what, "%s: bold", where);
    CHECK(r && (r->fmt.flags & OWF_BOLD) && !(r->fmt.flags & OWF_ITALIC), what);
    r = story_run(st, " and italic");
    snprintf(what, sizeof what, "%s: italic", where);
    CHECK(r && (r->fmt.flags & OWF_ITALIC), what);
    r = story_run(st, "Under");
    snprintf(what, sizeof what, "%s: underlined, second paragraph", where);
    CHECK(r && (r->fmt.flags & OWF_UNDERLINE) && st->paras[1].nruns >= 1, what);
    snprintf(what, sizeof what, "%s: no mark or comment runs", where);
    {
        int i, j, bad = 0;
        for (i = 0; i < st->nparas; ++i)
            for (j = 0; j < st->paras[i].nruns; ++j)
                if (st->paras[i].runs[j].note || st->paras[i].runs[j].comment) bad = 1;
        CHECK(!bad, what);
    }
    snprintf(what, sizeof what, "%s: the plain note stays plain", where);
    CHECK(!OWF_DOC_NOTE_STORY(doc, 1) && !strcmp(doc->notes[1].text, "Plain."), what);
}

static void test_formatted(void)
{
    static const char *const names[] = { "[Content_Types].xml", "_rels/.rels", "word/_rels/document.xml.rels",
                                         "word/document.xml", "word/footnotes.xml" };
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
        "</Relationships>",
        "<?xml version=\"1.0\"?><w:document " W " " R "><w:body>"
        "<w:p><w:r><w:t>Lamps</w:t></w:r><w:r><w:footnoteReference w:id=\"2\"/></w:r>"
        "<w:r><w:t xml:space=\"preserve\"> glow</w:t></w:r><w:r><w:footnoteReference w:id=\"3\"/></w:r></w:p>"
        "</w:body></w:document>",
        "<?xml version=\"1.0\"?><w:footnotes " W ">"
        "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
        "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>"
        "<w:footnote w:id=\"2\"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t xml:space=\"preserve\"> See </w:t></w:r>"
        "<w:r><w:rPr><w:b/></w:rPr><w:t>bold</w:t></w:r><w:r><w:footnoteReference w:id=\"3\"/></w:r>"
        "<w:r><w:rPr><w:i/></w:rPr><w:t xml:space=\"preserve\"> and italic</w:t></w:r></w:p>"
        "<w:p><w:r><w:rPr><w:u w:val=\"single\"/></w:rPr><w:t>Under</w:t></w:r></w:p><w:p/></w:footnote>"
        "<w:footnote w:id=\"3\"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t xml:space=\"preserve\"> Plain.</w:t></w:r></w:p></w:footnote>"
        "</w:footnotes>" };
    owf_doc *doc = NULL, *back = NULL;
    unsigned char *data = NULL;
    size_t length = 0;
    const owf_format *docx = owf_format_named("docx");
    CHECK(zip_import(names, parts, 5, &doc) == OWF_OK && doc, "formatted note read");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Lamps[F:See bold and italic\nUnder] glow[F:Plain.]"), dump(doc));
    check_formatted(doc, "read");

    /* out and back in: the runs' formats in footnotes.xml, the mark once in each note */
    CHECK(docx && docx->export && docx->export(doc, &data, &length, NULL) == OWF_OK && data, "formatted note written");
    if (data) {
        CHECK(part_count(data, length, "word/footnotes.xml", "<w:footnoteRef/>") == 2, "one mark each");
        CHECK(part_count(data, length, "word/footnotes.xml", "<w:b/>") == 1, "bold written");
        CHECK(part_count(data, length, "word/footnotes.xml", "<w:i/>") == 1, "italic written");
        CHECK(owf_import_memory(data, length, NULL, &back, NULL, NULL) == OWF_OK && back, "read back");
        if (back) {
            check_formatted(back, "back");
            owf_doc_free(back);
        }
        free(data);
    }

    /* owf_note_copy copies the formats; a story with none is dropped (the note is plain) */
    {
        int c = owf_note_copy(doc, 0);
        owf_story plain;
        owf_parafmt pf;
        owf_charfmt cf;
        CHECK(c == 2 && OWF_DOC_NOTE_STORY(doc, 2) && story_run(OWF_DOC_NOTE_STORY(doc, 2), "bold"), "copied with formats");
        memset(&plain, 0, sizeof plain);
        owf_parafmt_init(&pf);
        owf_charfmt_init(&cf);
        owf_para_add_text(owf_story_add(&plain, &pf), &cf, " just text", 10);
        CHECK(owf_note_set_story(doc, 2, &plain) == 0 && !OWF_DOC_NOTE_STORY(doc, 2) && plain.nparas == 0,
              "a story with no formats: plain");
        CHECK(owf_note_set_story(doc, 99, NULL) == -1, "no such note");
    }

    /* laid out with the formats, sized as a plain note */
    {
        ow_layout *l = ow_layout_new(doc, measure, NULL);
        ol_unit x = 0, y = 0;
        ol_document *feet = l ? ow_layout_footnotes(l, 0, &x, &y) : NULL;
        const ol_display_op *b = feet ? op_with(feet, "bold") : NULL, *p = feet ? op_with(feet, "See") : NULL;
        const ol_display_op *pl = feet ? op_with(feet, "Plain.") : NULL;
        CHECK(feet != NULL, "footnotes on page 1");
        CHECK(b && (b->u.text.style.text_flags & OL_TEXT_BOLD), "bold on the page");
        CHECK(p && !(p->u.text.style.text_flags & OL_TEXT_BOLD), "plain run not bold");
        CHECK(op_with(feet ? feet : NULL, "Under") != NULL, "second paragraph laid out");
        CHECK(p && pl && p->u.text.style.font_size == pl->u.text.style.font_size, "same size as a plain note");
        if (feet) ol_document_free(feet);
        ow_layout_free(l);
    }

    /* the editor: made the other kind keeps them; new text is plain; undo brings them back */
    {
        ow_editor *ed = ow_editor_new(doc);
        ow_selection sel;
        ow_note_info info;
        int j, at = -1;
        CHECK(ed != NULL, "editor");
        if (ed) {
            owf_doc *d = ow_editor_document(ed);
            for (j = 0; j < d->body.paras[0].nruns; ++j)
                if (d->body.paras[0].runs[j].note == 1) at = j;
            memset(&sel, 0, sizeof sel);
            sel.anchor.paragraph = 0;
            sel.anchor.run = at;
            sel.focus = sel.anchor;
            ow_editor_set_selection(ed, &sel);
            CHECK(at >= 0 && ow_editor_current_note(ed, &info), "caret at the note");
            CHECK(ow_editor_convert_note(ed) == OWF_OK, "made an endnote");
            d = ow_editor_document(ed);
            {
                int m = d->body.paras[0].runs[at].note;
                CHECK(m > 0 && d->notes[m - 1].endnote && OWF_DOC_NOTE_STORY(d, m - 1) &&
                      story_run(OWF_DOC_NOTE_STORY(d, m - 1), "bold"), "the endnote keeps its formats");
            }
            CHECK(ow_editor_set_note_text(ed, "New words.") == OWF_OK, "new text");
            d = ow_editor_document(ed);
            {
                int m = d->body.paras[0].runs[at].note;
                CHECK(m > 0 && !OWF_DOC_NOTE_STORY(d, m - 1) && !strcmp(d->notes[m - 1].text, "New words."),
                      "new text is plain");
            }
            ow_editor_undo(ed);
            ow_editor_undo(ed);
            d = ow_editor_document(ed);
            {
                int m = d->body.paras[0].runs[at].note;
                CHECK(m == 1 && !d->notes[0].endnote && OWF_DOC_NOTE_STORY(d, 0), "back as it was, with its formats");
            }
            ow_editor_free(ed);
        }
    }
    owf_doc_free(doc);
}

/* libowf 1.12.3: numbered again on each page (footnotes) or at each section, through DOCX and ODT */
static void test_restart(void)
{
    owf_doc *doc = doc_of(100), *back = NULL;
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    ow_layout *l;
    ow_note_info info;
    char mark[16];
    int later = -1, j, k;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    caret_end(ed, 0);
    ow_editor_insert_note(ed, 0, "The first footnote.");
    caret_end(ed, 1);
    ow_editor_insert_note(ed, 0, "The second footnote.");
    caret_end(ed, 40);
    ow_editor_insert_note(ed, 0, "A footnote further on.");
    caret_end(ed, 99);
    ow_editor_insert_note(ed, 1, "An endnote.");
    CHECK(ow_editor_note_restart(ed, 0) == OWF_NOTE_RESTART_CONTINUOUS, "through at first");
    CHECK(ow_editor_set_note_restart(ed, 1, OWF_NOTE_RESTART_PAGE) == OWF_ERR_FORMAT, "endnotes never on each page");
    CHECK(ow_editor_set_note_restart(ed, 0, OWF_NOTE_RESTART_COUNT) == OWF_ERR_FORMAT, "no such restart");
    /* through: the later page's footnote is 3 */
    l = ow_layout_new(doc, measure, NULL);
    for (j = 0; l && j < doc->body.paras[40].nruns; ++j)
        if (doc->body.paras[40].runs[j].note) later = page_of_run(l, 40, j);
    CHECK(later > 0, "the third on a later page");
    {
        ol_unit x, y;
        ol_document *feet = l && later > 0 ? ow_layout_footnotes(l, later, &x, &y) : NULL;
        CHECK(feet && op_with(feet, "3"), "numbered through: 3");
        if (feet) ol_document_free(feet);
    }
    ow_layout_free(l);
    /* on each page: 1, 2 on the first page, 1 again on the later one */
    CHECK(ow_editor_set_note_restart(ed, 0, OWF_NOTE_RESTART_PAGE) == OWF_OK &&
          ow_editor_note_restart(ed, 0) == OWF_NOTE_RESTART_PAGE, "footnotes again on each page");
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL, "laid out");
    if (l) {
        ol_unit x, y;
        ol_document *first = ow_layout_footnotes(l, 0, &x, &y), *feet = later > 0 ? ow_layout_footnotes(l, later, &x, &y) : NULL;
        CHECK(first && op_with(first, "1") && op_with(first, "2"), "1 and 2 on the first page");
        CHECK(feet && op_with(feet, "1") && !op_with(feet, "3"), "1 again on the later page");
        if (first) ol_document_free(first);
        if (feet) ol_document_free(feet);
        ow_layout_free(l);
    }
    /* DOCX: w:numRestart, last in w:footnotePr; endnotes at each section */
    CHECK(ow_editor_set_note_restart(ed, 1, OWF_NOTE_RESTART_SECTION) == OWF_OK, "endnotes again at each section");
    {
        const owf_format *docx = owf_format_named("docx");
        unsigned char *data = NULL;
        size_t length = 0;
        CHECK(docx && docx->export(doc, &data, &length, NULL) == OWF_OK && data, "written");
        if (data) {
            CHECK(part_count(data, length, "word/document.xml",
                             "<w:footnotePr><w:numRestart w:val=\"eachPage\"/></w:footnotePr>") == 1, "eachPage");
            CHECK(part_count(data, length, "word/document.xml",
                             "<w:endnotePr><w:numRestart w:val=\"eachSect\"/></w:endnotePr>") == 1, "eachSect");
            CHECK(owf_import_memory(data, length, NULL, &back, NULL, NULL) == OWF_OK && back, "read back");
            if (back) {
                CHECK(back->page.note_restart[0] == OWF_NOTE_RESTART_PAGE &&
                      back->page.note_restart[1] == OWF_NOTE_RESTART_SECTION, "DOCX: back the same");
                owf_doc_free(back);
                back = NULL;
            }
            free(data);
        }
    }
    /* ODT: text:start-numbering-at="page"; ODF has no sections' restart: through, and the report says so */
    {
        const owf_format *odt = owf_format_named("odt");
        unsigned char *data = NULL;
        size_t length = 0;
        owf_report *rp = owf_report_new();
        CHECK(odt && odt->export(doc, &data, &length, rp) == OWF_OK && data, "ODT written");
        if (data) {
            CHECK(part_count(data, length, "styles.xml", "text:start-numbering-at=\"page\"") == 1, "page in ODT");
            CHECK(owf_import_memory(data, length, NULL, &back, NULL, NULL) == OWF_OK && back, "ODT read back");
            if (back) {
                CHECK(back->page.note_restart[0] == OWF_NOTE_RESTART_PAGE &&
                      back->page.note_restart[1] == OWF_NOTE_RESTART_CONTINUOUS, "ODT: page kept, section through");
                owf_doc_free(back);
                back = NULL;
            }
            free(data);
        }
        {
            int n, found = 0, times;
            owf_note_level lv;
            const char *t;
            for (n = 0; rp && (t = owf_report_note(rp, n, &lv, &times)) != NULL; ++n)
                if (strstr(t, "each section")) found = 1;
            CHECK(found, "ODT's report: sections");
        }
        owf_report_free(rp);
    }
    /* undone: through again */
    ow_editor_undo(ed);
    ow_editor_undo(ed);
    CHECK(ow_editor_note_restart(ed, 0) == OWF_NOTE_RESTART_CONTINUOUS &&
          ow_editor_note_restart(ed, 1) == OWF_NOTE_RESTART_CONTINUOUS, "undone");
    ow_editor_free(ed);
    owf_doc_free(doc);

    /* at each section: the second section's notes from 1 again, in the libowf, editor and layout marks */
    doc = doc_of(4);
    ed = doc ? ow_editor_new(doc) : NULL;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    for (k = 0; k < 4; ++k) {
        caret_end(ed, k);
        ow_editor_insert_note(ed, 0, "A note.");
    }
    doc->body.paras[2].section_columns = 1;          /* a section starts at the third paragraph */
    CHECK(ow_editor_set_note_restart(ed, 0, OWF_NOTE_RESTART_SECTION) == OWF_OK, "again at each section");
    for (k = 0; k < 4; ++k) {
        int m = note_in(doc, k);
        const char *want = k == 0 || k == 2 ? "1" : "2";
        owf_note_mark_text(doc, m, mark, sizeof mark);
        CHECK(!strcmp(mark, want), mark);
        CHECK(ow_layout_note_mark(doc, m, mark, sizeof mark) && !strcmp(mark, want), mark);
        CHECK(ow_editor_note_info(ed, k, &info) && !strcmp(info.mark, want), info.mark);
    }
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
    test_round_trip("rtf");
    test_rtf_form();
    test_listed("html", html, 4);
    test_listed("text", txt, 4);
    test_word_form();
    test_odf_form();
    test_pages();
    test_numbering();
    test_formatted();
    test_restart();
    printf("notes: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

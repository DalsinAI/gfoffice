/*
 * GFOpenWrite's alphabetical index (core 1.7, libowf 1.10): entries marked at the caret (after a selection,
 * which stays), and subentries; the index built at the caret in alphabetical order with each entry's pages,
 * and undone; the entries through DOCX and ODT, and Word's and OpenOffice's forms, made here by hand from the
 * formats' specifications (ECMA-376 part 1, 17.16.5.80; ODF 1.3 part 3, 8.1.10), so no document from either
 * program goes into the repository; other formats show nothing for an entry.
 * make index-test, make index-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"
#include "owf_internal.h"
#include "owf_fields.h"

static int checks, failures;

#define CHECK(cond, what)                                                                                  \
    do {                                                                                                    \
        ++checks;                                                                                           \
        if (!(cond)) {                                                                                      \
            ++failures;                                                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);                                            \
        }                                                                                                   \
    } while (0)

/* The paragraphs from first on, each entry as {entry}, an indented paragraph with "  ", between '|' */
static const char *dump_from(const owf_doc *doc, int first)
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
    for (i = first; i < doc->body.nparas; ++i) {
        const owf_para *p = &doc->body.paras[i];
        if (p->fmt.indent_left > 0) PUT("  ");
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            if (r->kind == OWF_RUN_FIELD && r->field == OWF_FIELD_INDEX) {
                PUT("{");
                PUT(r->href ? r->href : "?");
                PUT("}");
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
    char line[32];
    int i;
    owf_charfmt_init(&cf);
    for (i = 0; doc && i < n; ++i) {
        owf_para *p;
        owf_parafmt_init(&pf);
        p = owf_story_add(&doc->body, &pf);
        snprintf(line, sizeof line, "P%d.", i);
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
        if (p->runs[j].kind == OWF_RUN_TEXT && p->runs[j].text) {
            sel.anchor.run = j;
            sel.anchor.byte_offset = strlen(p->runs[j].text);
            break;
        }
    sel.focus = sel.anchor;
    ow_editor_set_selection(ed, &sel);
}

/* each paragraph's page: ten to a page */
static int ten_a_page(void *userdata, const owf_doc *doc, const int *paragraphs, int count, int *pages)
{
    int i;
    (void)userdata; (void)doc;
    for (i = 0; i < count; ++i) pages[i] = paragraphs[i] / 10;
    return 1;
}

static void mark(ow_editor *ed, int paragraph, const char *entry)
{
    caret_end(ed, paragraph);
    CHECK(ow_editor_insert_index_entry(ed, entry) == OWF_OK, entry);
}

/* thirty paragraphs, entries on pages 1 to 3 */
static ow_editor *marked(owf_doc **doc)
{
    ow_editor *ed;
    *doc = doc_of(30);
    ed = *doc ? ow_editor_new(*doc) : NULL;
    if (!ed) return NULL;
    mark(ed, 1, "Lamps");
    mark(ed, 25, "Lamps");
    mark(ed, 3, "Wicks");
    mark(ed, 10, "Lamps:LED");
    mark(ed, 12, "Lamps:LED");
    mark(ed, 2, "apples");
    mark(ed, 5, "Lamps");
    return ed;
}

static void test_index(void)
{
    owf_doc *doc = NULL;
    ow_editor *ed = marked(&doc);
    ow_selection sel;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    CHECK(ow_editor_index_entry_count(ed) == 7, "seven entries");
    CHECK(!strcmp(dump_from(doc, 0), "P0.|P1.{Lamps}|P2.{apples}|P3.{Wicks}|P4.|P5.{Lamps}|P6.|P7.|P8.|P9.|P10.{Lamps:LED}|"
                  "P11.|P12.{Lamps:LED}|P13.|P14.|P15.|P16.|P17.|P18.|P19.|P20.|P21.|P22.|P23.|P24.|P25.{Lamps}|P26.|P27.|P28.|P29."),
          dump_from(doc, 0));
    CHECK(ow_editor_insert_index_entry(ed, "") == OWF_ERR_FORMAT && ow_editor_insert_index_entry(ed, ":x") == OWF_ERR_FORMAT,
          "an entry needs a name");
    /* a selection stays: the mark goes after it */
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = sel.focus.paragraph = 4;
    sel.focus.byte_offset = 2;
    ow_editor_set_selection(ed, &sel);
    CHECK(ow_editor_insert_index_entry(ed, "P4") == OWF_OK, "after a selection");
    CHECK(!strcmp(dump_from(doc, 4), "P4{P4}.|P5.{Lamps}|P6.|P7.|P8.|P9.|P10.{Lamps:LED}|P11.|P12.{Lamps:LED}|P13.|P14.|P15.|"
                  "P16.|P17.|P18.|P19.|P20.|P21.|P22.|P23.|P24.|P25.{Lamps}|P26.|P27.|P28.|P29."), dump_from(doc, 4));
    CHECK(ow_editor_undo(ed) == 1 && ow_editor_index_entry_count(ed) == 7, "undone");
    /* straight after marking, the caret beside the mark: the index goes there too */
    mark(ed, 29, "Zinc");
    CHECK(ow_editor_insert_index(ed, ten_a_page, NULL) == OWF_OK, "index after a mark");
    CHECK(ow_editor_undo(ed) == 1 && ow_editor_undo(ed) == 1 && ow_editor_index_entry_count(ed) == 7, "both undone");
    caret_end(ed, 29);
    CHECK(ow_editor_insert_index(ed, ten_a_page, NULL) == OWF_OK, "index made");
    CHECK(!strcmp(dump_from(doc, 30), "Index|apples, 1|Lamps, 1, 3|  LED, 2|Wicks, 1|"), dump_from(doc, 30));
    CHECK(doc->body.paras[31].style && !strcmp(doc->body.paras[31].style, "Index1") &&
          doc->body.paras[33].style && !strcmp(doc->body.paras[33].style, "Index2"), "its styles");
    CHECK(ow_editor_undo(ed) == 1 && doc->body.nparas == 30, "the index undone");
    caret_end(ed, 0);
    CHECK(ow_editor_insert_index(ed, ten_a_page, NULL) == OWF_OK, "index at the start");
    /* seven paragraphs went in (title, five lines, the split): P1 is 8 (page 1), P5 is 12 (page 2), P25 is 32 (page 4) */
    CHECK(strstr(dump_from(doc, 0), "Lamps, 1, 2, 4|  LED, 2|") != NULL, dump_from(doc, 0));
    ow_editor_free(ed);
    owf_doc_free(doc);
    doc = doc_of(3);
    ed = doc ? ow_editor_new(doc) : NULL;
    if (ed) {
        caret_end(ed, 0);
        CHECK(ow_editor_insert_index(ed, ten_a_page, NULL) == OWF_ERR_UNSUPPORTED, "no entries");
        ow_editor_free(ed);
    }
    owf_doc_free(doc);
}

static void test_round_trip(const char *format)
{
    owf_doc *doc = NULL, *back = NULL;
    ow_editor *ed = marked(&doc);
    char path[64];
    if (!ed) { CHECK(0, "editor"); owf_doc_free(doc); return; }
    snprintf(path, sizeof path, "/tmp/openwrite-index.%s", format);
    CHECK(owf_export_file(doc, path, format, NULL) == OWF_OK, format);
    CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, format);
    if (back) {
        CHECK(!strcmp(dump_from(back, 0), dump_from(doc, 0)), dump_from(back, 0));
        owf_doc_free(back);
    }
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_shown(const char *format)
{
    owf_doc *doc = NULL;
    ow_editor *ed = marked(&doc);
    unsigned char *data = NULL;
    size_t length = 0;
    char *text;
    const owf_format *f = owf_format_named(format);
    if (!ed) { CHECK(0, "editor"); owf_doc_free(doc); return; }
    CHECK(f && f->export && f->export(doc, &data, &length, NULL) == OWF_OK && data, format);
    text = (char *)calloc(1, length + 1);
    if (text && data) memcpy(text, data, length);
    CHECK(text && strstr(text, "P29.") && !strstr(text, "Wicks") && !strstr(text, "LED"), format);
    free(text);
    free(data);
    ow_editor_free(ed);
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


/* Word's form: XE in a simple field and in a complex one with no result */
static void test_word_form(void)
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
        "<?xml version=\"1.0\"?><w:document " W "><w:body>"
        "<w:p><w:r><w:t>Lamps</w:t></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> XE \"Lamps:LED\" </w:instrText></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
        "<w:fldSimple w:instr=\" XE &quot;Wicks&quot; \"/>"
        "<w:r><w:t xml:space=\"preserve\"> glow</w:t></w:r></w:p>"
        "<w:sectPr/></w:body></w:document>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 3, &doc) == OWF_OK && doc, "Word's form read");
    if (!doc) return;
    CHECK(!strcmp(dump_from(doc, 0), "Lamps{Lamps:LED}{Wicks} glow"), dump_from(doc, 0));
    owf_doc_free(doc);
}

#define ODF_NS "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" " \
               "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""

/* OpenOffice's form: point marks, one under a main entry (key1) */
static void test_odf_form(void)
{
    static const char *const names[] = { "mimetype", "content.xml" };
    static const char *const parts[] = {
        "application/vnd.oasis.opendocument.text",
        "<?xml version=\"1.0\"?><office:document-content " ODF_NS "><office:body><office:text>"
        "<text:p>Lamps<text:alphabetical-index-mark text:string-value=\"Lamps\"/> and LEDs"
        "<text:alphabetical-index-mark text:key1=\"Lamps\" text:string-value=\"LED\"/></text:p>"
        "</office:text></office:body></office:document-content>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 2, &doc) == OWF_OK && doc, "OpenOffice's form read");
    if (!doc) return;
    CHECK(!strcmp(dump_from(doc, 0), "Lamps{Lamps} and LEDs{Lamps:LED}"), dump_from(doc, 0));
    owf_doc_free(doc);
}

int main(void)
{
    test_index();
    test_round_trip("docx");
    test_round_trip("odt");
    test_word_form();
    test_odf_form();
    test_shown("rtf");
    test_shown("html");
    test_shown("text");
    test_shown("pdf");
    printf("index: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

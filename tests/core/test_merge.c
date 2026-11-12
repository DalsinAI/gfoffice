/*
 * GFOpenWrite's mail merge (core 1.6, libowf 1.9): a CSV data source read (separators, quotes, Latin-1, a
 * byte-order mark, empty lines and short rows), merge fields inserted, shown with their names and with a
 * record's values, and undone; the merge into one document (each record from a new page, the fields made
 * plain text) and into a file each; the fields and the data source through DOCX and ODT; and Word's and
 * OpenOffice's forms, made here by hand from the formats' specifications (ECMA-376 part 1, 17.16.5.35 and
 * 17.15.1.57; ODF 1.3 part 3, 7.6.3), so no document from either program goes into the repository.
 * make merge-test, make merge-check (ASan).
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

/* The body's text, each merge field as {column=shown}, other fields as {FIELD}, paragraphs between '|' and a
 * page break before one as '#' */
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
        if (i && p->fmt.page_break_before) PUT("#");
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            if (r->kind == OWF_RUN_FIELD && r->field == OWF_FIELD_MERGE) {
                PUT("{");
                PUT(r->href ? r->href : "?");
                PUT("=");
                PUT(r->text ? r->text : "");
                PUT("}");
            } else if (r->kind == OWF_RUN_FIELD) {
                PUT("{");
                PUT(owf_field_name(r->field));
                PUT("}");
            } else if (r->kind == OWF_RUN_TEXT && r->text) {
                PUT(r->text);
            } else if (r->kind == OWF_RUN_LINEBREAK) {
                PUT("\n");
            }
        }
        if (i + 1 < doc->body.nparas) PUT("|");
    }
#undef PUT
    return out;
}

static owf_doc *doc_of(const char *const *lines, int n)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    int i;
    owf_charfmt_init(&cf);
    for (i = 0; doc && i < n; ++i) {
        owf_para *p;
        owf_parafmt_init(&pf);
        p = owf_story_add(&doc->body, &pf);
        if (!p || owf_para_add_text(p, &cf, lines[i], strlen(lines[i])) != 0) {
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

static const char csv[] =
    "\xEF\xBB\xBF" "Name;Town; ;Note\r\n"
    "Ann;Leeds;x;\"Says \"\"hi\"\"; twice\"\r\n"
    "\r\n"
    "Bob;\"York\nNorth\"\r\n"
    ";;;\r\n"
    "Cy;Bath;y;z;extra\r\n";

static void test_source(void)
{
    static const char *const lines[] = { "Text." };
    static const char latin1[] = "Name,Town\nJos\xe9,M\xe1laga\n";
    owf_doc *doc = doc_of(lines, 1);
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    CHECK(ow_editor_merge_columns(ed) == 0 && ow_editor_merge_records(ed) == 0 && !ow_editor_merge_source(ed),
          "no data source");
    CHECK(ow_editor_merge_load_text(ed, "RAM:people.csv", csv, sizeof csv - 1) == OWF_OK, "CSV read");
    CHECK(ow_editor_merge_source(ed) && !strcmp(ow_editor_merge_source(ed), "RAM:people.csv"), "its path kept");
    CHECK(ow_editor_is_dirty(ed), "a changed data source is a change");
    CHECK(ow_editor_merge_columns(ed) == 4, "four columns");
    CHECK(ow_editor_merge_column(ed, 0) && !strcmp(ow_editor_merge_column(ed, 0), "Name"), "no byte-order mark");
    CHECK(ow_editor_merge_column(ed, 2) && !strcmp(ow_editor_merge_column(ed, 2), "Column3"), "a name made");
    CHECK(ow_editor_merge_records(ed) == 3, "three records: empty lines and rows left out");
    CHECK(ow_editor_merge_value(ed, 0, "Note") && !strcmp(ow_editor_merge_value(ed, 0, "Note"), "Says \"hi\"; twice"),
          "quotes");
    CHECK(ow_editor_merge_value(ed, 1, "town") && !strcmp(ow_editor_merge_value(ed, 1, "town"), "York\nNorth"),
          "a line break in quotes, and a name in another case");
    CHECK(ow_editor_merge_value(ed, 1, "Note") && !strcmp(ow_editor_merge_value(ed, 1, "Note"), ""), "a short row");
    CHECK(ow_editor_merge_value(ed, 2, "Note") && !strcmp(ow_editor_merge_value(ed, 2, "Note"), "z"), "extra cells");
    CHECK(!ow_editor_merge_value(ed, 3, "Name") && !ow_editor_merge_value(ed, 0, "Age"), "no such record or column");
    CHECK(ow_editor_merge_load_text(ed, NULL, latin1, sizeof latin1 - 1) == OWF_OK, "Latin-1 read");
    CHECK(ow_editor_merge_value(ed, 0, "Name") && !strcmp(ow_editor_merge_value(ed, 0, "Name"), "Jos\xC3\xA9"),
          "Latin-1 made UTF-8");
    CHECK(ow_editor_merge_value(ed, 0, "Town") && !strcmp(ow_editor_merge_value(ed, 0, "Town"), "M\xC3\xA1laga"),
          "Latin-1 made UTF-8 again");
    CHECK(ow_editor_merge_load_text(ed, NULL, "\n\n", 2) == OWF_ERR_FORMAT, "no columns");
    CHECK(ow_editor_merge_load_text(ed, NULL, "A,B\n1,2\n", 8) == OWF_OK && ow_editor_merge_columns(ed) == 2,
          "commas");
    CHECK(ow_editor_merge_load_text(ed, NULL, "A\tB;C\n1\t2;3\n", 12) == OWF_OK && ow_editor_merge_columns(ed) == 2,
          "tabs");
    CHECK(ow_editor_merge_load(ed, "/tmp/openwrite-no-such-source.csv") == OWF_ERR_IO, "no such file");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

/* "Dear «Name»," and "Town: «Town»." */
static ow_editor *letter(owf_doc **doc)
{
    static const char *const lines[] = { "Dear ", "Town: " };
    ow_editor *ed;
    *doc = doc_of(lines, 2);
    ed = *doc ? ow_editor_new(*doc) : NULL;
    if (!ed) return NULL;
    if (ow_editor_merge_load_text(ed, "RAM:people.csv", csv, sizeof csv - 1) != OWF_OK) return ed;
    caret_end(ed, 0);
    ow_editor_insert_merge_field(ed, "Name");
    ow_editor_insert_utf8(ed, ",", 1);
    caret_end(ed, 1);
    ow_editor_insert_merge_field(ed, "Town");
    ow_editor_insert_utf8(ed, ".", 1);
    return ed;
}

static void test_fields(void)
{
    owf_doc *doc = NULL;
    ow_editor *ed = letter(&doc);
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    CHECK(!strcmp(dump(doc), "Dear {Name=\xC2\xABName\xC2\xBB},|Town: {Town=\xC2\xABTown\xC2\xBB}."), dump(doc));
    CHECK(ow_editor_merge_shown(ed) == -1, "the names shown");
    CHECK(ow_editor_merge_show(ed, 1) == OWF_OK && ow_editor_merge_shown(ed) == 1, "record 2 shown");
    CHECK(!strcmp(dump(doc), "Dear {Name=Bob},|Town: {Town=York\nNorth}."), dump(doc));
    CHECK(ow_editor_merge_show(ed, 3) == OWF_ERR_UNSUPPORTED && ow_editor_merge_shown(ed) == 1, "no record 4");
    CHECK(ow_editor_merge_show(ed, -1) == OWF_OK, "the names again");
    CHECK(!strcmp(dump(doc), "Dear {Name=\xC2\xABName\xC2\xBB},|Town: {Town=\xC2\xABTown\xC2\xBB}."), dump(doc));
    CHECK(ow_editor_insert_merge_field(ed, "") == OWF_ERR_FORMAT, "a column is needed");
    CHECK(ow_editor_undo(ed) == 1 && ow_editor_undo(ed) == 1, "undone");
    CHECK(!strcmp(dump(doc), "Dear {Name=\xC2\xABName\xC2\xBB},|Town: "), dump(doc));
    CHECK(ow_editor_merge_source(ed) && !strcmp(ow_editor_merge_source(ed), "RAM:people.csv"),
          "the data source is not undone");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_merge_document(void)
{
    owf_doc *doc = NULL, *all = NULL;
    ow_editor *ed = letter(&doc);
    int j, fields = 0;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    ow_editor_merge_show(ed, 2);
    CHECK(ow_editor_merge_document(ed, 0, -1, &all) == OWF_OK && all, "merged");
    if (all) {
        CHECK(!strcmp(dump(all), "Dear Ann,|Town: Leeds.|#Dear Bob,|Town: York\nNorth.|#Dear Cy,|Town: Bath."),
              dump(all));
        for (j = 0; j < all->body.nparas; ++j) {
            int k;
            for (k = 0; k < all->body.paras[j].nruns; ++k) fields += all->body.paras[j].runs[k].kind == OWF_RUN_FIELD;
        }
        CHECK(!fields && !all->merge_source, "no fields and no data source left");
        owf_doc_free(all);
        all = NULL;
    }
    CHECK(ow_editor_merge_shown(ed) == 2 && !strcmp(dump(doc), "Dear {Name=Cy},|Town: {Town=Bath}."),
          "the record shown before is shown again");
    CHECK(ow_editor_merge_document(ed, 1, 1, &all) == OWF_OK && all, "one record");
    if (all) {
        CHECK(!strcmp(dump(all), "Dear Bob,|Town: York\nNorth."), dump(all));
        owf_doc_free(all);
    }
    ow_editor_merge_load_text(ed, NULL, "Name\n", 5);
    CHECK(ow_editor_merge_document(ed, 0, -1, &all) == OWF_ERR_UNSUPPORTED && !all, "no records");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_files(void)
{
    owf_doc *doc = NULL, *back = NULL;
    ow_editor *ed = letter(&doc);
    int written = 0;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    remove("/tmp/openwrite-letter1.odt");
    remove("/tmp/openwrite-letter3.docx");
    CHECK(ow_editor_merge_to_files(ed, "/tmp/openwrite-letter.odt", 0, -1, &written) == OWF_OK && written == 3,
          "three files");
    CHECK(owf_import_file("/tmp/openwrite-letter2.odt", NULL, &back, NULL, NULL) == OWF_OK && back, "record 2's");
    if (back) {
        CHECK(!strcmp(dump(back), "Dear Bob,|Town: York\nNorth."), dump(back));
        owf_doc_free(back);
        back = NULL;
    }
    CHECK(ow_editor_merge_to_files(ed, "/tmp/openwrite-letter.docx", 2, 2, &written) == OWF_OK && written == 1,
          "one DOCX");
    CHECK(owf_import_file("/tmp/openwrite-letter3.docx", NULL, &back, NULL, NULL) == OWF_OK && back, "record 3's");
    if (back) {
        CHECK(!strcmp(dump(back), "Dear Cy,|Town: Bath."), dump(back));
        owf_doc_free(back);
    }
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_load_file(void)
{
    static const char *const lines[] = { "Text." };
    owf_doc *doc = doc_of(lines, 1);
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    FILE *f = fopen("/tmp/openwrite-people.csv", "wb");
    CHECK(ed && f, "editor and file");
    if (f) { fwrite(csv, 1, sizeof csv - 1, f); fclose(f); }
    if (!ed) { owf_doc_free(doc); return; }
    CHECK(ow_editor_merge_load(ed, "/tmp/openwrite-people.csv") == OWF_OK && ow_editor_merge_records(ed) == 3,
          "read from a file");
    CHECK(doc->merge_source && !strcmp(doc->merge_source, "/tmp/openwrite-people.csv"), "the document's source");
    ow_editor_merge_close(ed);
    CHECK(ow_editor_merge_records(ed) == 3, "read again from the document's source when asked");
    CHECK(ow_editor_merge_load(ed, NULL) == OWF_OK && !doc->merge_source && !ow_editor_merge_records(ed),
          "the source forgotten");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_round_trip(const char *format)
{
    owf_doc *doc = NULL, *back = NULL;
    ow_editor *ed = letter(&doc);
    char path[64];
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    snprintf(path, sizeof path, "/tmp/openwrite-merge.%s", format);
    CHECK(owf_export_file(doc, path, format, NULL) == OWF_OK, format);
    CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, format);
    if (back) {
        CHECK(!strcmp(dump(back), "Dear {Name=\xC2\xABName\xC2\xBB},|Town: {Town=\xC2\xABTown\xC2\xBB}."), dump(back));
        CHECK(back->merge_source && !strcmp(back->merge_source, "RAM:people.csv"), "the data source kept");
        owf_doc_free(back);
    }
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

/* Word's form: a simple and a complex MERGEFIELD, and w:mailMerge's query */
static void test_word_form(void)
{
    static const char *const names[] = { "[Content_Types].xml", "_rels/.rels", "word/_rels/document.xml.rels",
                                         "word/document.xml", "word/settings.xml" };
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
        "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
        "</Relationships>",
        "<?xml version=\"1.0\"?><w:document " W "><w:body>"
        "<w:p><w:r><w:t xml:space=\"preserve\">Dear </w:t></w:r>"
        "<w:fldSimple w:instr=\" MERGEFIELD First_Name \"><w:r><w:t>\xC2\xAB" "First_Name\xC2\xBB</w:t></w:r></w:fldSimple>"
        "<w:r><w:t xml:space=\"preserve\"> of </w:t></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> MERGEFIELD \"Home Town\" \\* MERGEFORMAT </w:instrText></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>\xC2\xAB" "Home Town\xC2\xBB</w:t></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>"
        "<w:sectPr/></w:body></w:document>",
        "<?xml version=\"1.0\"?><w:settings " W "><w:mailMerge><w:mainDocumentType w:val=\"formLetters\"/>"
        "<w:linkToQuery/><w:dataType w:val=\"textFile\"/><w:connectString w:val=\"\"/>"
        "<w:query w:val=\"SELECT * FROM `C:\\Data\\people.csv` \"/></w:mailMerge></w:settings>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 5, &doc) == OWF_OK && doc, "Word's form read");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Dear {First_Name=\xC2\xAB" "First_Name\xC2\xBB} of {Home Town=\xC2\xAB" "Home Town\xC2\xBB}"),
          dump(doc));
    CHECK(doc->merge_source && !strcmp(doc->merge_source, "C:\\Data\\people.csv"), doc->merge_source ? doc->merge_source : "no source");
    owf_doc_free(doc);
}

#define ODF_NS "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" " \
               "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""

/* OpenOffice's form: text:database-display */
static void test_odf_form(void)
{
    static const char *const names[] = { "mimetype", "content.xml" };
    static const char *const parts[] = {
        "application/vnd.oasis.opendocument.text",
        "<?xml version=\"1.0\"?><office:document-content " ODF_NS "><office:body><office:text>"
        "<text:p>Dear <text:database-display text:table-name=\"people\" text:table-type=\"table\" "
        "text:database-name=\"Addresses\" text:column-name=\"Name\">&lt;Name&gt;</text:database-display>,</text:p>"
        "</office:text></office:body></office:document-content>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 2, &doc) == OWF_OK && doc, "OpenOffice's form read");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Dear {Name=<Name>},"), dump(doc));
    CHECK(doc->merge_source && !strcmp(doc->merge_source, "Addresses"), "its database");
    owf_doc_free(doc);
}

/* RTF, HTML and plain text show the value shown */
static void test_shown(const char *format)
{
    owf_doc *doc = NULL;
    ow_editor *ed = letter(&doc);
    unsigned char *data = NULL;
    size_t length = 0;
    char *text;
    const owf_format *f = owf_format_named(format);
    if (!ed) { CHECK(0, "editor"); owf_doc_free(doc); return; }
    ow_editor_merge_show(ed, 0);
    CHECK(f && f->export && f->export(doc, &data, &length, NULL) == OWF_OK && data, format);
    text = (char *)calloc(1, length + 1);
    if (text && data) memcpy(text, data, length);
    CHECK(text && strstr(text, "Ann") && strstr(text, "Leeds"), format);
    free(text);
    free(data);
    ow_editor_free(ed);
    owf_doc_free(doc);
}

int main(void)
{
    test_source();
    test_fields();
    test_merge_document();
    test_files();
    test_load_file();
    test_round_trip("docx");
    test_round_trip("odt");
    test_word_form();
    test_odf_form();
    test_shown("rtf");
    test_shown("html");
    test_shown("text");
    printf("merge: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

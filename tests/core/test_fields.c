/*
 * GFOpenWrite's fields with a value (core 1.4, libowf 1.7): the document's title, author and subject, the
 * file's name, the word count, the chapter, user fields and references to a bookmarked paragraph, in the
 * editor (insert, update, undo) and through DOCX and ODT; RTF and plain text keep their shown values. Word's
 * and OpenOffice's forms are made here by hand from the formats' specifications (ECMA-376 part 1, 17.16;
 * ODF 1.3 part 3, 7.3), so no document from either program goes into the repository.
 * make fields-test, make fields-check (ASan).
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

/* The body's text with each field as {NAME:arg=value}, paragraphs between '|' */
static const char *dump(const owf_doc *doc)
{
    static const char *const names[] = { "PAGE", "PAGES", "DATE", "TIME", "TITLE", "AUTHOR", "SUBJECT", "FILENAME",
                                         "WORDS", "CHAPTER", "USER", "REF", "REFPAGE" };
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
            if (r->kind == OWF_RUN_FIELD) {
                PUT("{");
                PUT(names[r->field]);
                if (r->href) { PUT(":"); PUT(r->href); }
                if (r->field > OWF_FIELD_TIME) { PUT("="); PUT(r->text ? r->text : "(none)"); }
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

static owf_doc *doc_of(void)
{
    static const char *const paras[] = { "Getting started", "Plug it in and wait.", "Using it", "Press the button." };
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    int i;
    owf_charfmt_init(&cf);
    for (i = 0; doc && i < 4; ++i) {
        owf_para *p;
        owf_parafmt_init(&pf);
        pf.heading = i % 2 ? 0 : 1;
        p = owf_story_add(&doc->body, &pf);
        if (!p || owf_para_add_text(p, &cf, paras[i], strlen(paras[i])) != 0) {
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

static owf_doc *fielded_doc(void)
{
    owf_doc *doc = doc_of();
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    if (!ed) { owf_doc_free(doc); return NULL; }
    ow_editor_set_file_name(ed, "DH0:Manuals/Lamp.odt");
    ow_editor_set_doc_info(ed, OW_INFO_TITLE, "Lamp & Co");
    ow_editor_set_doc_info(ed, OW_INFO_AUTHOR, "Ann");
    ow_editor_set_doc_info(ed, OW_INFO_SUBJECT, "Lamps");
    ow_editor_set_user_field(ed, "Model", "L-2");
    caret_end(ed, 1);
    ow_editor_insert_field(ed, OWF_FIELD_TITLE);
    ow_editor_insert_field(ed, OWF_FIELD_AUTHOR);
    ow_editor_insert_field_arg(ed, OWF_FIELD_USER, "Model");
    caret_end(ed, 3);
    ow_editor_insert_field(ed, OWF_FIELD_CHAPTER);
    ow_editor_insert_cross_reference(ed, 0, 0);
    ow_editor_insert_cross_reference(ed, 0, 1);
    ow_editor_free(ed);
    return doc;
}

static const char *const fielded =
    "Getting started|Plug it in and wait.{TITLE=Lamp & Co}{AUTHOR=Ann}{USER:Model=L-2}|Using it|"
    "Press the button.{CHAPTER=Using it}{REF:_Ref1=Getting started}{REFPAGE:_Ref1=1}";

static void test_editing(void)
{
    owf_doc *doc = doc_of();
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    const char *name, *value;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    CHECK(ow_editor_insert_field_arg(ed, OWF_FIELD_USER, NULL) == OWF_ERR_FORMAT, "a user field needs a name");
    CHECK(ow_editor_insert_field_arg(ed, OWF_FIELD_TITLE, "x") == OWF_ERR_FORMAT, "a title takes none");
    ow_editor_set_file_name(ed, "Work:Docs/Lamp.odt");
    caret_end(ed, 1);
    CHECK(ow_editor_insert_field(ed, OWF_FIELD_TITLE) == OWF_OK, "title");
    CHECK(!strcmp(dump(doc), "Getting started|Plug it in and wait.{TITLE=}|Using it|Press the button."), dump(doc));
    CHECK(ow_editor_set_doc_info(ed, OW_INFO_TITLE, "The lamp") == OWF_OK && doc->title &&
          !strcmp(ow_editor_doc_info(ed, OW_INFO_TITLE), "The lamp"), "title set");
    CHECK(!strcmp(dump(doc), "Getting started|Plug it in and wait.{TITLE=The lamp}|Using it|Press the button."), dump(doc));
    CHECK(ow_editor_insert_field(ed, OWF_FIELD_FILENAME) == OWF_OK && ow_editor_insert_field(ed, OWF_FIELD_WORDS) == OWF_OK,
          "file name, words");
    CHECK(!strcmp(dump(doc), "Getting started|Plug it in and wait.{TITLE=The lamp}{FILENAME=Lamp.odt}{WORDS=12}|"
                             "Using it|Press the button."), dump(doc));
    /* a user field: made, shown, changed, removed */
    CHECK(ow_editor_set_user_field(ed, "Model", "L-1") == OWF_OK && ow_editor_user_field_count(ed) == 1 &&
          ow_editor_user_field_at(ed, 0, &name, &value) && !strcmp(name, "Model") && !strcmp(value, "L-1"), "user field");
    caret_end(ed, 3);
    CHECK(ow_editor_insert_field_arg(ed, OWF_FIELD_USER, "Model") == OWF_OK, "user field inserted");
    CHECK(ow_editor_set_user_field(ed, "Model", "L-2") == OWF_OK, "changed");
    CHECK(strstr(dump(doc), "{USER:Model=L-2}") != NULL, dump(doc));
    CHECK(ow_editor_set_user_field(ed, "Model", NULL) == OWF_OK && ow_editor_user_field_count(ed) == 0, "removed");
    CHECK(strstr(dump(doc), "{USER:Model=}") != NULL, dump(doc));
    CHECK(ow_editor_set_user_field(ed, "Model", NULL) == OWF_ERR_UNSUPPORTED, "none to remove");
    /* the chapter, and references */
    CHECK(ow_editor_insert_field(ed, OWF_FIELD_CHAPTER) == OWF_OK, "chapter");
    CHECK(strstr(dump(doc), "{CHAPTER=Using it}") != NULL, dump(doc));
    CHECK(ow_editor_insert_cross_reference(ed, 2, 0) == OWF_OK, "reference");
    CHECK(doc->body.paras[2].bookmarks && !strcmp(doc->body.paras[2].bookmarks, "_Ref1"), "a bookmark made");
    CHECK(strstr(dump(doc), "{REF:_Ref1=Using it}") != NULL, dump(doc));
    CHECK(ow_editor_insert_cross_reference(ed, 2, 1) == OWF_OK && strstr(dump(doc), "{REFPAGE:_Ref1=1}") != NULL, dump(doc));
    CHECK(ow_editor_insert_cross_reference(ed, 9, 0) == OWF_ERR_FORMAT, "no such paragraph");
    /* the heading changed: the reference follows on update */
    caret_end(ed, 2);
    ow_editor_insert_utf8(ed, " today", 6);
    CHECK(ow_editor_update_fields(ed) > 0, "updated");
    CHECK(strstr(dump(doc), "{CHAPTER=Using it today}{REF:_Ref1=Using it today}") != NULL, dump(doc));
    /* undo takes a field away; the document's information stays */
    ow_editor_undo(ed);
    ow_editor_undo(ed);
    CHECK(strstr(dump(doc), "{REFPAGE") == NULL && strstr(dump(doc), "{REF:_Ref1") != NULL, dump(doc));
    CHECK(doc->title && !strcmp(doc->title, "The lamp"), "the title is not undone");
    /* bold over a field keeps it */
    {
        ow_selection sel;
        memset(&sel, 0, sizeof sel);
        sel.anchor.paragraph = 1;
        sel.focus.paragraph = 1;
        sel.focus.run = doc->body.paras[1].nruns - 1;
        sel.focus.byte_offset = 0;
        ow_editor_set_selection(ed, &sel);
        ow_editor_toggle_char_flags(ed, OWF_BOLD);
        CHECK(strstr(dump(doc), "{TITLE=The lamp}{FILENAME=Lamp.odt}") != NULL, dump(doc));
    }
    /* a header's {TITLE} */
    CHECK(ow_editor_set_story_text(ed, OW_STORY_HEADER, "{TITLE} - {PAGE}") == OWF_OK &&
          doc->header.nparas == 1 && doc->header.paras[0].nruns >= 3 &&
          doc->header.paras[0].runs[0].kind == OWF_RUN_FIELD && doc->header.paras[0].runs[0].text &&
          !strcmp(doc->header.paras[0].runs[0].text, "The lamp"), "header title");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_round_trip(const char *format, const char *want)
{
    owf_doc *doc = fielded_doc(), *back = NULL;
    char path[64];
    CHECK(doc != NULL, "document");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), fielded), dump(doc));
    snprintf(path, sizeof path, "/tmp/openwrite-fields.%s", format);
    CHECK(owf_export_file(doc, path, format, NULL) == OWF_OK, format);
    CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, format);
    if (back) {
        CHECK(!strcmp(dump(back), want), dump(back));
        CHECK(back->title && !strcmp(back->title, "Lamp & Co") && back->author && !strcmp(back->author, "Ann") &&
              back->subject && !strcmp(back->subject, "Lamps"), "title, author, subject");
        CHECK(back->nuserfields == 1 && !strcmp(back->userfields[0].name, "Model") &&
              !strcmp(back->userfields[0].value, "L-2"), "the user field");
        CHECK(back->body.paras[0].bookmarks && strstr(back->body.paras[0].bookmarks, "_Ref1"), "the bookmark");
        owf_doc_free(back);
    }
    owf_doc_free(doc);
}

static void test_shown_values(const char *format, const char *want)
{
    owf_doc *doc = fielded_doc(), *back = NULL;
    char path[64];
    if (!doc) { CHECK(0, "document"); return; }
    snprintf(path, sizeof path, "/tmp/openwrite-fields.%s", format);
    CHECK(owf_export_file(doc, path, format, NULL) == OWF_OK, format);
    CHECK(owf_import_file(path, NULL, &back, NULL, NULL) == OWF_OK && back, format);
    if (back) {
        CHECK(!strcmp(dump(back), want), dump(back));
        owf_doc_free(back);
    }
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

/* Word's form: complex and simple fields with their results, core properties and document variables */
static void test_word_form(void)
{
    static const char *const names[] = { "[Content_Types].xml", "_rels/.rels", "word/document.xml",
                                          "word/settings.xml", "word/_rels/document.xml.rels", "docProps/core.xml" };
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
        "<w:p><w:bookmarkStart w:id=\"0\" w:name=\"_Ref9\"/><w:r><w:t>Intro</w:t></w:r><w:bookmarkEnd w:id=\"0\"/></w:p>"
        "<w:p><w:r><w:t xml:space=\"preserve\">By </w:t></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r><w:r><w:instrText xml:space=\"preserve\"> AUTHOR \\* MERGEFORMAT </w:instrText></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r><w:r><w:t>Bo</w:t></w:r><w:r><w:t>b</w:t></w:r>"
        "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>"
        "<w:fldSimple w:instr=\" DOCVARIABLE  &quot;Batch no&quot; \"><w:r><w:t>42</w:t></w:r></w:fldSimple>"
        "<w:r><w:t xml:space=\"preserve\">, see </w:t></w:r>"
        "<w:fldSimple w:instr=\" REF _Ref9 \\h \"><w:r><w:t>Intro</w:t></w:r></w:fldSimple>"
        "<w:r><w:t xml:space=\"preserve\"> on page </w:t></w:r>"
        "<w:fldSimple w:instr=\"PAGEREF _Ref9 \\h\"><w:r><w:t>1</w:t></w:r></w:fldSimple>"
        "<w:fldSimple w:instr=\" HYPERLINK x \"><w:r><w:t>kept</w:t></w:r></w:fldSimple>"
        "</w:p><w:sectPr/></w:body></w:document>",
        "<?xml version=\"1.0\"?><w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docVars><w:docVar w:name=\"Batch no\" w:val=\"42\"/></w:docVars></w:settings>",
        "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId6\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
        "</Relationships>",
        "<?xml version=\"1.0\"?><cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>Batch</dc:title><dc:creator>Bob</dc:creator>"
        "<dc:subject>Beer</dc:subject></cp:coreProperties>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 6, &doc) == OWF_OK && doc, "Word's form");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Intro|By {AUTHOR=Bob}{USER:Batch no=42}, see {REF:_Ref9=Intro} on page {REFPAGE:_Ref9=1}kept"),
          dump(doc));
    CHECK(doc->title && !strcmp(doc->title, "Batch") && doc->author && !strcmp(doc->author, "Bob") &&
          doc->subject && !strcmp(doc->subject, "Beer"), "core properties");
    CHECK(doc->nuserfields == 1 && !strcmp(doc->userfields[0].name, "Batch no") && !strcmp(doc->userfields[0].value, "42"),
          "document variable");
    owf_doc_free(doc);
}

/* OpenOffice's form: document fields, a user field and its declaration, a bookmark reference, meta.xml */
static void test_odf_form(void)
{
    static const char *const names[] = { "mimetype", "content.xml", "meta.xml", "META-INF/manifest.xml" };
    static const char *const parts[] = {
        "application/vnd.oasis.opendocument.text",
        "<?xml version=\"1.0\"?><office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" office:version=\"1.3\">"
        "<office:body><office:text>"
        "<text:user-field-decls><text:user-field-decl office:value-type=\"float\" office:value=\"3\" text:name=\"Copies\"/>"
        "</text:user-field-decls>"
        "<text:h text:outline-level=\"1\"><text:bookmark text:name=\"top\"/>Start</text:h>"
        "<text:p><text:title>Old title</text:title> by <text:initial-creator>Cat</text:initial-creator>, "
        "<text:user-field-get text:name=\"Copies\">3</text:user-field-get> copies of <text:file-name>a.odt</text:file-name>"
        " in <text:chapter text:display=\"name\" text:outline-level=\"1\">Start</text:chapter>"
        " (<text:bookmark-ref text:reference-format=\"page\" text:ref-name=\"top\">1</text:bookmark-ref>,"
        " <text:word-count>12</text:word-count> words)</text:p>"
        "</office:text></office:body></office:document-content>",
        "<?xml version=\"1.0\"?><office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<office:meta><dc:title>Old title</dc:title><meta:initial-creator>Cat</meta:initial-creator>"
        "<dc:subject>Cats</dc:subject></office:meta></office:document-meta>",
        "<?xml version=\"1.0\"?><manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>" };
    owf_doc *doc = NULL;
    CHECK(zip_import(names, parts, 4, &doc) == OWF_OK && doc, "OpenOffice's form");
    if (!doc) return;
    CHECK(!strcmp(dump(doc), "Start|{TITLE=Old title} by {AUTHOR=Cat}, {USER:Copies=3} copies of {FILENAME=a.odt} in "
                             "{CHAPTER=Start} ({REFPAGE:top=1}, {WORDS=12} words)"), dump(doc));
    CHECK(doc->author && !strcmp(doc->author, "Cat") && doc->subject && !strcmp(doc->subject, "Cats"), "meta");
    CHECK(doc->nuserfields == 1 && !strcmp(doc->userfields[0].value, "3"), "user field declaration");
    owf_doc_free(doc);
}

int main(void)
{
    test_editing();
    test_round_trip("docx", fielded);
    test_round_trip("odt", fielded);
    test_shown_values("rtf", "Getting started|Plug it in and wait.Lamp & CoAnnL-2|Using it|"
                             "Press the button.Using itGetting started1");
    test_word_form();
    test_odf_form();
    printf("fields: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

/*
 * OWFLibsTest: owfwrite.library (built with OWF_USE_WRITE_LIBRARY). Builds a
 * document through the library's calls, saves it as ODT, DOCX, HTML, PDF
 * and text in T:, reads the ODT and DOCX back and compares the text; opens
 * the sample DOCX and ODT; frees memory the library allocated with owf_free
 * and grows a document's runs with owf_realloc, as the editor does (as
 * OWFLibTest does for owf.library 2).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_open.h"
#include "owf_libs_shared.h"

/* the body's text, paragraphs joined by '\n', into out */
static void body_text(const owf_doc *doc, char *out, size_t size)
{
    size_t n = 0;
    int p, r;
    out[0] = 0;
    for (p = 0; p < doc->body.nparas; p++) {
        const owf_para *para = &doc->body.paras[p];
        for (r = 0; r < para->nruns; r++) {
            const char *t = para->runs[r].kind == OWF_RUN_TEXT ? para->runs[r].text : "";
            size_t len = t ? strlen(t) : 0;
            if (n + len + 2 >= size) return;
            memcpy(out + n, t, len);
            n += len;
        }
        out[n++] = '\n';
        out[n] = 0;
    }
}

static owf_doc *make_document(void)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    if (!doc) return NULL;
    owf_doc_set_title(doc, "OWFLibsTest");
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    pf.heading = 1;
    p = owf_story_add(&doc->body, &pf);
    if (!p || owf_para_add_text(p, &cf, "Library test", 12) < 0) goto fail;
    pf.heading = 0;
    p = owf_story_add(&doc->body, &pf);
    if (!p) goto fail;
    cf.flags = OWF_BOLD;
    if (owf_para_add_text(p, &cf, "Bold, ", 6) < 0) goto fail;
    cf.flags = OWF_ITALIC;
    cf.font = owf_doc_font(doc, "Liberation Serif", OWF_FONT_SERIF);
    if (owf_para_add_text(p, &cf, "italic", 6) < 0) goto fail;
    if (owf_para_add_link_text(p, &cf, " and a link", 11, "https://amigachrome.com/") < 0) goto fail;
    p = owf_story_add(&doc->body, &pf);
    if (!p || owf_para_add_special(p, &cf, OWF_RUN_FIELD, OWF_FIELD_PAGE) < 0) goto fail;
    return doc;
fail:
    owf_doc_free(doc);
    return NULL;
}

/* save in a format, read it back, and compare the body's text */
static int round_trip(const owf_doc *doc, const char *format, const char *path, int read_back)
{
    char b[1024];
    owf_report *report = owf_report_new();
    owf_doc *back = NULL;
    const owf_format *used = NULL;
    int ok = owf_export_file(doc, path, format, report) == OWF_OK;
    if (ok && read_back) {
        ok = owf_import_file(path, format, &back, report, &used) == OWF_OK && back && used &&
             !strcmp(used->name, format);
        if (ok) {
            body_text(back, b, sizeof b);
            /* the page field comes back as a field, not text: compare the words */
            ok = strstr(b, "Library test") && strstr(b, "Bold, italic and a link");
        }
    }
    if (back) owf_doc_free(back);
    owf_report_free(report);
    return ok;
}

static int open_sample(const char *dir, const char *name, const char *format)
{
    char path[512];
    owf_doc *doc = NULL;
    owf_report *report = owf_report_new();
    const owf_format *used = NULL;
    int ok;
    unit_path(path, sizeof path, dir, name);
    ok = owf_import_file(path, NULL, &doc, report, &used) == OWF_OK && doc && used &&
         !strcmp(used->name, format) && doc->body.nparas > 3;
    if (doc) owf_doc_free(doc);
    owf_report_free(report);
    return ok;
}

/* what the editor does: grow a paragraph's runs and replace a run's text
 * with memory of its own, then let the library free it all */
static int editor_memory(void)
{
    owf_doc *doc = make_document();
    owf_para *p;
    owf_run *runs;
    char *text;
    int i, start, ok = 1;
    if (!doc) return 0;
    p = &doc->body.paras[1];
    start = p->nruns;
    for (i = 0; i < 50 && ok; i++) {
        runs = (owf_run *)owf_realloc(p->runs, (size_t)(p->nruns + 1) * sizeof *runs);
        if (!runs) { ok = 0; break; }
        p->runs = runs;
        p->capruns = p->nruns + 1;
        memset(&p->runs[p->nruns], 0, sizeof *runs);
        p->runs[p->nruns].kind = OWF_RUN_TEXT;
        p->runs[p->nruns].fmt = p->runs[0].fmt;
        text = (char *)owf_malloc(8);
        if (!text) { ok = 0; break; }
        strcpy(text, "x");
        p->runs[p->nruns].text = text;
        p->nruns++;
    }
    owf_free(p->runs[0].text);
    p->runs[0].text = (char *)owf_calloc(1, 4);
    ok = ok && p->runs[0].text && p->nruns == start + 50;
    owf_doc_free(doc);
    return ok;
}

static void run(const owf_libs_env *e)
{
    static const char *const formats[] = { "odt", "docx", "html", "pdf", "text" };
    static const char *const exts[] = { "odt", "docx", "html", "pdf", "txt" };
    char path[96];
    owf_doc *doc;
    int i;

    shared_checks(e, OWF_KIND_DOCUMENT);
    LCHECK(e, owf_format_count() >= 10 && owf_format_at(0) && owf_format_named("docx") &&
           owf_format_for_filename("Letter.ODT") == owf_format_named("odt"),
           "%s: document formats listed, by name and by file name", e->tag);
    doc = make_document();
    LCHECK(e, doc != NULL && doc->body.nparas == 3, "%s: document built through the library", e->tag);
    if (!doc)
        return;
    for (i = 0; i < 5; i++) {
        int back = i < 2;
        snprintf(path, sizeof path, "%sowflibstest-%s.%s", e->temp, e->tag, exts[i]);
        for (char *s = path; *s; s++)
            if (*s == ' ') *s = '-';
        LCHECK(e, round_trip(doc, formats[i], path, back), "%s: %s saved%s", e->tag, formats[i],
               back ? " and read back" : "");
        unit_delete_file(path);
    }
    owf_doc_free(doc);
    LCHECK(e, open_sample(e->samples, "OpenWrite-Tour.docx", "docx"), "%s: OpenWrite-Tour.docx opens", e->tag);
    LCHECK(e, open_sample(e->samples, "OpenWrite-Tour.odt", "odt"), "%s: OpenWrite-Tour.odt opens", e->tag);
    LCHECK(e, editor_memory(), "%s: editor-style memory, freed by the library", e->tag);
}

OWF_LIBS_UNIT(owf_libs_write, run);

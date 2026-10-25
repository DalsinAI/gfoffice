/*
 * OWFLibTest: owf.library from LIBS:, as a program uses it.
 *
 *   OWFLibTest [SAMPLES=dir] [FIXTURES=dir]
 *              (default PROGDIR:samples and PROGDIR:fixtures, as
 *              build-library.sh lays them out)
 *
 * Builds a document through the library's calls, saves it as ODT, DOCX,
 * HTML, PDF and text in T:, reads the ODT and DOCX back and compares the
 * text; opens the sample DOCX and ODT; frees memory the library allocated
 * with owf_free and grows a document's runs with owf_realloc, as the
 * editor does. Then version 2's calls (owf_lib_checks.c, which the PC runs
 * too): the fixture workbook as XLS, XLSX and ODS, saved and read back;
 * the formula engine on a new book, 27 formulas through the library's own
 * maths among them; what files are; a zip read and raw DEFLATE inflated;
 * a .pub read. Then it all again from a second task at the same time.
 * Prints one line per check (the second task's as one line) and
 * "OWFLibTest: N passed, M failed".
 * z.library (ours, openzlib) is optional: without it the library reads
 * zip packages with its own inflate, and writes them stored.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_USE_LIBRARY
#define OWF_USE_LIBRARY
#endif
#include "owf.h"
#include "owf_open.h"
#include "owf_lib_checks.h"

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

/* each task counts its own checks; only the first task prints, and it
 * prints the second task's results once that one has finished (libnix's
 * stdout is not for two tasks at once) */
struct results {
    int passed, failed, print;
    char first_failure[96];
};

static struct results main_results = { 0, 0, 1, "" };

static void check_in(struct results *r, int ok, const char *what)
{
    if (r->print) printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) {
        r->passed++;
    } else {
        if (!r->failed) strncpy(r->first_failure, what, sizeof r->first_failure - 1);
        r->failed++;
    }
}

static void check(int ok, const char *what)
{
    check_in(&main_results, ok, what);
}

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
    owf_doc_set_title(doc, "OWFLibTest");
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
    char a[1024], b[1024];
    owf_report *report = owf_report_new();
    owf_doc *back = NULL;
    const owf_format *used = NULL;
    int ok = owf_export_file(doc, path, format, report) == OWF_OK;
    if (ok && read_back) {
        ok = owf_import_file(path, format, &back, report, &used) == OWF_OK && back && used &&
             !strcmp(used->name, format);
        if (ok) {
            body_text(doc, a, sizeof a);
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
    snprintf(path, sizeof path, "%s/%s", dir, name);
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

static const char *samples = "PROGDIR:samples";
static const char *fixtures = "PROGDIR:fixtures";

/* a whole file through dos.library into the library's memory: safe from
 * either task, as libnix's stdio is not */
static int read_file(const char *path, unsigned char **data, size_t *length)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    LONG n;
    *data = NULL;
    *length = 0;
    if (!fh)
        return 0;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n < 0 || !(*data = (unsigned char *)owf_malloc((size_t)n + 1)) || Read(fh, *data, n) != n) {
        Close(fh);
        owf_free(*data);
        *data = NULL;
        return 0;
    }
    Close(fh);
    (*data)[n] = 0;
    *length = (size_t)n;
    return 1;
}

static void delete_file(const char *path)
{
    DeleteFile((CONST_STRPTR)path);
}

static void check_v2(void *ctx, int ok, const char *what)
{
    check_in((struct results *)ctx, ok, what);
}

static void run_checks(struct results *res, const char *tag)
{
    char path[64], what[96];
    owf_doc *doc = make_document();
    static const char *const formats[] = { "odt", "docx", "html", "pdf", "text" };
    static const char *const exts[] = { "odt", "docx", "html", "pdf", "txt" };
    int i;
    snprintf(what, sizeof what, "%s: document built through the library", tag);
    check_in(res, doc != NULL && doc->body.nparas == 3, what);
    if (!doc) return;
    for (i = 0; i < 5; i++) {
        int back = i < 2;
        snprintf(path, sizeof path, "T:owflibtest-%s.%s", tag, exts[i]);
        snprintf(what, sizeof what, "%s: %s saved%s", tag, formats[i], back ? " and read back" : "");
        check_in(res, round_trip(doc, formats[i], path, back), what);
        DeleteFile((CONST_STRPTR)path);
    }
    owf_doc_free(doc);
    snprintf(what, sizeof what, "%s: OpenWrite-Tour.docx opens", tag);
    check_in(res, open_sample(samples, "OpenWrite-Tour.docx", "docx"), what);
    snprintf(what, sizeof what, "%s: OpenWrite-Tour.odt opens", tag);
    check_in(res, open_sample(samples, "OpenWrite-Tour.odt", "odt"), what);
    snprintf(what, sizeof what, "%s: editor-style memory, freed by the library", tag);
    check_in(res, editor_memory(), what);
    {
        owf_lib_checks c;
        c.check = check_v2;
        c.ctx = res;
        c.read_file = read_file;
        c.delete_file = delete_file;
        c.fixtures = fixtures;
        c.temp = "T:";
        c.tag = tag;
        owf_lib_run_checks(&c);
    }
}

/* ---- a second task using the library at the same time -------------------- */

static struct Task *parent;
static volatile int child_done;
static struct results child_results = { 0, 0, 0, "" };

static void child_main(void)
{
    run_checks(&child_results, "task 2");
    Forbid();                       /* the parent may wake only once we are gone */
    child_done = 1;
    Signal(parent, SIGBREAKF_CTRL_F);
}

int main(void)
{
    static const char template[] = "SAMPLES,FIXTURES";
    LONG args[2] = { 0, 0 };
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    struct Process *child;
    int i, nformats;

    if (rda && args[0]) samples = (const char *)args[0];
    if (rda && args[1]) fixtures = (const char *)args[1];
    if (!owf_open()) {
        if (owf_open_sizes_differ)
            printf("OWFLibTest: LIBS:owf.library's structures are not the sizes this test was built with\n");
        else
            printf("OWFLibTest: owf.library %d is not in LIBS:\n", OWF_LIBRARY_VERSION);
        if (rda) FreeArgs(rda);
        return RETURN_FAIL;
    }
    printf("OWFLibTest: %s %d.%d, libowf %s\n", OWF_LIBRARY_NAME, OWFBase->lib_Version,
           OWFBase->lib_Revision, OWF_VERSION);
    check(OWFBase->lib_Version >= OWF_LIBRARY_VERSION, "library version");
    check(owf_struct_size(OWF_STRUCT_CELL) == sizeof(owf_cell) && owf_struct_size(OWF_STRUCT_COUNT) == 0,
          "structure sizes as this program's (owf_open checked them all)");
    nformats = owf_format_count();
    check(nformats >= 10, "formats listed");
    for (i = 0; i < nformats; i++) {
        const owf_format *f = owf_format_at(i);
        printf("     %-11s %s\n", f->name, f->description);
    }
    check(owf_format_named("docx") && owf_format_for_filename("Letter.ODT") == owf_format_named("odt"),
          "formats by name and by file name");
    check(!strcmp(owf_error_text(OWF_ERR_FORMAT), owf_error_text(OWF_ERR_FORMAT)) &&
          strlen(owf_error_text(OWF_ERR_IO)) > 0, "error texts");

    run_checks(&main_results, "task 1");

    /* the same checks from a second process while this one runs them again */
    parent = FindTask(NULL);
    child_done = 0;
    SetSignal(0, SIGBREAKF_CTRL_F);
    child = CreateNewProcTags(NP_Entry, (ULONG)child_main, NP_Name, (ULONG)"OWFLibTest task 2",
                              NP_StackSize, 32768, NP_Output, (ULONG)Output(),
                              NP_CloseOutput, FALSE, TAG_DONE);
    check(child != NULL, "second task started");
    run_checks(&main_results, "task 1 again");
    if (child) {
        char what[160];
        while (!child_done) Wait(SIGBREAKF_CTRL_F);
        snprintf(what, sizeof what, "task 2, at the same time: %d passed, %d failed%s%s",
                 child_results.passed, child_results.failed,
                 child_results.failed ? ", first: " : "", child_results.first_failure);
        check(child_results.failed == 0 && child_results.passed > 0, what);
    }

    printf("OWFLibTest: %d passed, %d failed\n", main_results.passed, main_results.failed);
    owf_close();
    if (rda) FreeArgs(rda);
    return main_results.failed ? RETURN_ERROR : RETURN_OK;
}

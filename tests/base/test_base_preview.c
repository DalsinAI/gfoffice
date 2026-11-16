/*
 * GFOpenBase's print preview on the PC (include/openbase_preview.h): a report built and laid out into the
 * pages the PDF and the printer get, a page at a time, with its page header and footer filled in for each
 * page. Against the pinned SQLite, libowf and GFOpenLayout (mk/base-preview.mk).
 *
 *   test-base-preview [DIR]   DIR: a scratch drawer (default: a new one in /tmp)
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "openbase_preview.h"

static int fails, checks;
static char dir[512];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* half the size a character, as tests/core/test_layout.c measures */
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

/* Orders in two towns, and a long table of readings for a report many pages long. */
static ob_project *project(void)
{
    static ob_table t;
    static const char *const fields[] = { "Customer", "Town", "Amount" };
    static const char *const rows[][3] = {
        { "Ann", "Leeds", "10.50" }, { "Bob", "York", "3.25" }, { "Cy", "Leeds", "1,000.00" }, { "Di", "York", "" },
    };
    static const char *const rfields[] = { "Reading", "Value" };
    char err[OB_ERR_MAX], name[32], value[32], file[600];
    const char *vals[2];
    ob_project *p;
    ob_int key;
    int i;
    snprintf(file, sizeof file, "%s/preview.oadb", dir);
    remove(file);
    p = ob_open(file, OB_OPEN_CREATE, err, sizeof err);
    CHECK(p != NULL, "create: %s", err);
    if (!p) return NULL;
    ob_table_init(&t, "Orders");
    ob_table_add_field(&t, "Customer", OB_TEXT);
    ob_table_add_field(&t, "Town", OB_TEXT);
    ob_table_add_field(&t, "Amount", OB_CURRENCY);
    CHECK(ob_table_create(p, &t), "table: %s", ob_error(p));
    for (i = 0; i < 4; i++)
        CHECK(ob_row_add(p, "Orders", 3, fields, rows[i], &key), "row %d: %s", i, ob_error(p));
    ob_table_init(&t, "Readings");
    ob_table_add_field(&t, "Reading", OB_TEXT);
    ob_table_add_field(&t, "Value", OB_INTEGER);
    CHECK(ob_table_create(p, &t), "readings: %s", ob_error(p));
    for (i = 0; i < 300; i++) {
        snprintf(name, sizeof name, "Reading %03d", i + 1);
        snprintf(value, sizeof value, "%d", i * 7 % 101);
        vals[0] = name;
        vals[1] = value;
        CHECK(ob_row_add(p, "Readings", 2, rfields, vals, &key), "reading %d: %s", i, ob_error(p));
    }
    return p;
}

static void sales(ob_report *r)
{
    ob_report_init(r, "Sales");
    strcpy(r->title, "Sales by town");
    strcpy(r->table, "Orders");
    strcpy(r->group, "Town");
    ob_report_add_column(r, "Customer", OB_TOTAL_COUNT);
    ob_report_add_column(r, "Amount", OB_TOTAL_SUM);
    strcpy(r->page_head, "[Title] on [Date]");
    strcpy(r->page_foot, "Page [Page] of [Pages]");
}

/* A new page for each town: two pages, each with its header and its own number in the footer. */
static void test_pages_and_bands(ob_project *p)
{
    ob_report r;
    ob_preview *v;
    char err[OB_ERR_MAX] = "", text[200];
    sales(&r);
    r.new_page = 1;
    v = ob_preview_new(p, &r, NULL, measure, NULL, err, sizeof err);
    CHECK(v != NULL, "preview: %s", err);
    if (!v) return;
    CHECK(ob_preview_pages(v) == 2, "a page a town: %d", ob_preview_pages(v));
    CHECK(ob_preview_doc(v) != NULL && ob_preview_layout(v) != NULL, "the document and its layout");
    CHECK(ow_layout_page_count(ob_preview_layout(v)) == ob_preview_pages(v), "the layout's pages");
    ob_preview_band_text(v, OW_STORY_HEADER, 0, "16/11/2026", text, sizeof text);
    CHECK(!strcmp(text, "Sales by town on 16/11/2026"), "page header: [%s]", text);
    ob_preview_band_text(v, OW_STORY_FOOTER, 0, NULL, text, sizeof text);
    CHECK(!strcmp(text, "Page 1 of 2"), "page 1 footer: [%s]", text);
    ob_preview_band_text(v, OW_STORY_FOOTER, 1, NULL, text, sizeof text);
    CHECK(!strcmp(text, "Page 2 of 2"), "page 2 footer: [%s]", text);
    ob_preview_band_text(v, OW_STORY_FOOTER, 2, NULL, text, sizeof text);
    CHECK(text[0] == 0, "no page 3: [%s]", text);
    ob_preview_label(v, 1, text, sizeof text);
    CHECK(!strcmp(text, "Page 2 of 2"), "label: [%s]", text);
    /* turning the pages stays inside them */
    CHECK(ob_preview_turn(v, 0, 1) == 1, "next");
    CHECK(ob_preview_turn(v, 1, 1) == 1, "no page after the last");
    CHECK(ob_preview_turn(v, 1, -1) == 0, "back");
    CHECK(ob_preview_turn(v, 0, -5) == 0, "no page before the first");
    CHECK(ob_preview_turn(v, 0, 1000) == 1, "last");
    ob_preview_free(v);

    /* all on one page without the new page; a report without a page footer still numbers its pages */
    r.new_page = 0;
    r.page_foot[0] = 0;
    v = ob_preview_new(p, &r, NULL, measure, NULL, err, sizeof err);
    CHECK(v != NULL, "preview: %s", err);
    if (!v) return;
    CHECK(ob_preview_pages(v) == 1, "one page: %d", ob_preview_pages(v));
    ob_preview_band_text(v, OW_STORY_FOOTER, 0, NULL, text, sizeof text);
    CHECK(!strcmp(text, "Page 1 of 1"), "default footer: [%s]", text);
    ob_preview_label(v, 0, text, sizeof text);
    CHECK(!strcmp(text, "Page 1 of 1"), "label: [%s]", text);
    ob_preview_free(v);
}

/* Three hundred rows run over many pages; the last page's footer says it is the last. */
static void test_long_report(ob_project *p)
{
    ob_report r;
    ob_preview *v;
    char err[OB_ERR_MAX] = "", text[200], want[64];
    int n;
    ob_report_init(&r, "Readings");
    strcpy(r.title, "All readings");
    strcpy(r.table, "Readings");
    ob_report_add_column(&r, "Reading", OB_TOTAL_COUNT);
    ob_report_add_column(&r, "Value", OB_TOTAL_SUM);
    v = ob_preview_new(p, &r, NULL, measure, NULL, err, sizeof err);
    CHECK(v != NULL, "preview: %s", err);
    if (!v) return;
    n = ob_preview_pages(v);
    CHECK(n > 3, "many pages: %d", n);
    snprintf(want, sizeof want, "Page %d of %d", n, n);
    ob_preview_band_text(v, OW_STORY_FOOTER, n - 1, NULL, text, sizeof text);
    CHECK(!strcmp(text, want), "last footer: [%s], want [%s]", text, want);
    ob_preview_label(v, n - 1, text, sizeof text);
    CHECK(!strcmp(text, want), "label: [%s]", text);
    ob_preview_free(v);
}

/* A report that can't be built says why, and makes no preview. */
static void test_errors(ob_project *p)
{
    ob_report r;
    char err[OB_ERR_MAX] = "";
    char text[40] = "x";
    sales(&r);
    strcpy(r.table, "Nothing");
    CHECK(ob_preview_new(p, &r, NULL, measure, NULL, err, sizeof err) == NULL && err[0], "no table: [%s]", err);
    sales(&r);
    CHECK(ob_preview_new(p, &r, NULL, NULL, NULL, err, sizeof err) == NULL && err[0], "no measure: [%s]", err);
    ob_preview_free(NULL);
    ob_preview_label(NULL, 0, text, sizeof text);
    CHECK(text[0] == 0, "no preview, no label");
    CHECK(ob_preview_pages(NULL) == 0 && ob_preview_turn(NULL, 3, 1) == 0, "no preview, no pages");
}

/* UTF-8 to Latin-1 for the Amiga's fonts, a piece at a time: a piece never ends inside a character. */
static void test_latin1(void)
{
    static char big[2000], out[600];
    const char *s = "Caf\xc3\xa9 \xe2\x82\xac" "5";      /* "Café €5" */
    size_t chars = 99, took, total = 0, all = 0;
    int i, pieces = 0;
    took = ob_preview_latin1(s, strlen(s), out, sizeof out, &chars);
    CHECK(took == strlen(s) && chars == 7 && !strcmp(out, "Caf\xe9 ?5"), "latin1: %u %u [%s]", (unsigned)took,
          (unsigned)chars, out);
    /* a piece of 4 characters: "Caf" then the two bytes of the e-acute together */
    took = ob_preview_latin1(s, strlen(s), out, 5, &chars);
    CHECK(took == 5 && chars == 4 && !strcmp(out, "Caf\xe9"), "piece: %u %u", (unsigned)took, (unsigned)chars);
    took = ob_preview_latin1(s, 4, out, sizeof out, &chars);
    CHECK(took == 3 && chars == 3 && !strcmp(out, "Caf"), "a cut character is left for the next piece: %u",
          (unsigned)took);
    /* 1000 e-acutes, 2000 bytes, in pieces of 511: every one comes through */
    for (i = 0; i < 1000; i++) { big[2 * i] = (char)0xc3; big[2 * i + 1] = (char)0xa9; }
    while (total < 2000 && pieces < 20) {
        took = ob_preview_latin1(big + total, 2000 - total, out, 512, &chars);
        if (!took) break;
        total += took;
        all += chars;
        pieces++;
    }
    CHECK(total == 2000 && all == 1000 && pieces == 2, "pieces: %u bytes %u chars %d pieces", (unsigned)total,
          (unsigned)all, pieces);
    CHECK(ob_preview_latin1("x", 1, out, 1, &chars) == 0 && chars == 0 && out[0] == 0, "no room");
    CHECK(ob_preview_latin1(NULL, 0, out, sizeof out, &chars) == 0 && out[0] == 0, "nothing");
}

int main(int argc, char **argv)
{
    ob_project *p;
    if (argc > 1) {
        snprintf(dir, sizeof dir, "%s", argv[1]);
    } else {
        char t[] = "/tmp/ob-preview-XXXXXX";
        if (!mkdtemp(t)) { perror("mkdtemp"); return 1; }
        snprintf(dir, sizeof dir, "%s", t);
    }
    p = project();
    if (p) {
        test_pages_and_bands(p);
        test_long_report(p);
        test_errors(p);
        test_latin1();
        ob_close(p);
    }
    printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "base preview", checks, fails);
    return fails ? 1 : 0;
}

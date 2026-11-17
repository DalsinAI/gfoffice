/*
 * test_opr_pptx_chart: a slide's chart saved as PowerPoint (src/deck/opr_pptx.c, Presentation gap 2) and read
 * back with the PPTX reader:
 *
 *   - the chart comes back a chart, with its kind, title, legend, series, categories and values (a gap kept),
 *     in its box, with its name;
 *   - the package holds its chart part, the part's relationship to its embedded workbook and that workbook,
 *     with their content types, and the slide's relationship to the chart;
 *   - the workbook holds the chart's data, so PowerPoint's Edit Data opens on it;
 *   - two charts on two slides, each its own part and workbook;
 *   - nothing is reported left out, and saved twice the bytes are the same;
 *   - a chart with no chart data (an old picture-only object) is still saved as its picture or left out.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#include "chart_sales.h"

#define BLANK 0                  /* owf_deck_new's "Blank Slide" */

/* A slide with the sales chart on it at x, y, its name name; 0 or -1. */
static int add_chart(owf_deck *d, int x, int y, const char *name, const char *title)
{
    owf_slide *s = owf_deck_add_slide(d, -1, BLANK);
    owf_obj *o = s ? owf_slide_add(s, OWF_OBJ_CHART) : NULL;
    if (!o || !(o->chart = calloc(1, sizeof *o->chart)))
        return -1;
    if (owf_chart_from_part(chart_xml, strlen(chart_xml), o->chart) != OWF_OK)
        return -1;
    if (title) {
        free(o->chart->title);
        if (!(o->chart->title = malloc(strlen(title) + 1))) return -1;
        strcpy(o->chart->title, title);
    }
    o->x = x;
    o->y = y;
    o->width = 11520;
    o->height = 7200;
    o->image = -1;
    if (!(o->name = malloc(strlen(name) + 1))) return -1;
    strcpy(o->name, name);
    return 0;
}

static const owf_obj *chart_on(const owf_deck *d, int slide)
{
    int i;
    if (slide >= d->nslides) return NULL;
    for (i = 0; i < d->slides[slide].nobjs; i++)
        if (d->slides[slide].objs[i].kind == OWF_OBJ_CHART) return &d->slides[slide].objs[i];
    return NULL;
}

/* A package part as a string (NUL ended), or NULL. */
static char *part_of(const unsigned char *data, size_t n, const char *name)
{
    owf_unzip z;
    unsigned char *p = NULL, *q;
    size_t len = 0;
    if (!owf_unzip_open(&z, data, n) || owf_unzip_read(&z, name, &p, &len) != OWF_OK) return NULL;
    q = malloc(len + 1);
    if (q) { memcpy(q, p, len); q[len] = 0; }
    owf_free(p);
    return (char *)q;
}

static int has(const unsigned char *data, size_t n, const char *name, const char *needle)
{
    char *s = part_of(data, n, name);
    int ok = s && (!needle || strstr(s, needle));
    free(s);
    return ok;
}

static int lost_notes(const owf_report *r)
{
    int i, lost = 0;
    owf_note_level level;
    for (i = 0; i < owf_report_count(r); i++) {
        owf_report_note(r, i, &level, NULL);
        if (level == OWF_NOTE_LOST) lost++;
    }
    return lost;
}

static void test_round_trip(void)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT), *back = NULL;
    owf_report *r = owf_report_new();
    unsigned char *data = NULL, *again = NULL, *xl = NULL;
    size_t n = 0, n2 = 0, xn = 0;
    const owf_obj *o;
    owf_book *book = NULL;
    owf_unzip z;
    CHECK(d && r && add_chart(d, 1440, 2160, "Chart 3", NULL) == 0 && add_chart(d, 720, 720, "Chart 9", "Costs") == 0,
          "the deck");
    if (!d || !r || d->nslides != 2) return;
    CHECK(opr_export_pptx(d, &data, &n, r) == OWF_OK && data, "saved");
    CHECK(lost_notes(r) == 0, "nothing left out (%d lost)", lost_notes(r));
    CHECK(opr_export_pptx(d, &again, &n2, NULL) == OWF_OK && n2 == n && !memcmp(data, again, n), "saved twice the same");
    free(again);

    CHECK(has(data, n, "[Content_Types].xml", "PartName=\"/ppt/charts/chart1.xml\" "
              "ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\""), "chart1's type");
    CHECK(has(data, n, "[Content_Types].xml", "/ppt/charts/chart2.xml"), "chart2's type");
    CHECK(has(data, n, "[Content_Types].xml", "Extension=\"xlsx\" "
              "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet\""), "the workbooks' type");
    CHECK(has(data, n, "ppt/slides/_rels/slide1.xml.rels", "relationships/chart\" Target=\"../charts/chart1.xml\""),
          "slide 1 to its chart");
    CHECK(has(data, n, "ppt/slides/_rels/slide2.xml.rels", "Target=\"../charts/chart2.xml\""), "slide 2 to its own");
    CHECK(has(data, n, "ppt/charts/_rels/chart1.xml.rels",
              "relationships/package\" Target=\"../embeddings/Microsoft_Excel_Worksheet1.xlsx\""), "chart1 to its data");
    CHECK(has(data, n, "ppt/charts/chart1.xml", "<c:externalData r:id=\"rId1\">"), "chart1 names its data");
    CHECK(has(data, n, "ppt/slides/slide1.xml", "<p:graphicFrame>"), "a graphic frame on the slide");

    /* the embedded workbook: the chart's data */
    if (owf_unzip_open(&z, data, n)) {
        CHECK(owf_unzip_read(&z, "ppt/embeddings/Microsoft_Excel_Worksheet1.xlsx", &xl, &xn) == OWF_OK, "the workbook");
        CHECK(owf_unzip_has(&z, "ppt/embeddings/Microsoft_Excel_Worksheet2.xlsx"), "the second's");
    }
    if (xl) {
        const owf_cell *c;
        CHECK(owf_import_book_memory(xl, xn, "xlsx", &book, NULL, NULL) == OWF_OK && book && book->nsheets == 1,
              "the workbook reads");
        if (book && book->nsheets) {
            c = owf_sheet_find(&book->sheets[0], 0, 2);
            CHECK(c && c->type == OWF_VALUE_TEXT && !strcmp(c->text, "South"), "C1 South");
            c = owf_sheet_find(&book->sheets[0], 1, 1);
            CHECK(c && c->type == OWF_VALUE_NUMBER && c->number == 4.3, "B2 4.3");
        }
        if (book) owf_book_free(book);
        owf_free(xl);
    }

    /* read back */
    CHECK(owf_import_deck_memory(data, n, "pptx", &back, NULL, NULL) == OWF_OK && back, "read back");
    if (back) {
        CHECK(back->nslides == 2, "%d slides", back->nslides);
        o = chart_on(back, 0);
        CHECK(o && o->chart, "slide 1's chart is a chart");
        if (o && o->chart) {
            check_sales(o->chart, "read back");
            CHECK(o->x == 1440 && o->y == 2160 && o->width == 11520 && o->height == 7200, "its box %d %d %d %d", o->x,
                  o->y, o->width, o->height);
            CHECK(o->name && !strcmp(o->name, "Chart 3"), "its name");
        }
        o = chart_on(back, 1);
        CHECK(o && o->chart && o->chart->title && !strcmp(o->chart->title, "Costs"), "slide 2's, its own title");
        owf_deck_free(back);
    }
    free(data);
    owf_report_free(r);
    owf_deck_free(d);
}

/* A chart object with no chart data and no picture: left out, as before, and said so. */
static void test_no_data(void)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    owf_report *r = owf_report_new();
    owf_slide *s = d ? owf_deck_add_slide(d, -1, BLANK) : NULL;
    owf_obj *o = s ? owf_slide_add(s, OWF_OBJ_CHART) : NULL;
    unsigned char *data = NULL;
    size_t n = 0;
    CHECK(o && r, "the deck");
    if (!o || !r) return;
    o->image = -1;
    o->width = o->height = 1440;
    CHECK(opr_export_pptx(d, &data, &n, r) == OWF_OK, "saved");
    CHECK(lost_notes(r) == 1, "said left out (%d)", lost_notes(r));
    CHECK(!has(data, n, "ppt/charts/chart1.xml", NULL), "no chart part");
    free(data);
    owf_report_free(r);
    owf_deck_free(d);
}

int main(void)
{
    /* saved twice, the same bytes: one zip time for every save (not the clock's 2-second steps) */
    owf_time fixed = { 2023, 11, 14, 22, 13, 20 };
    owf_zip_fix_time(&fixed);
    test_round_trip();
    test_no_data();
    printf("test_opr_pptx_chart: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

/*
 * test_opr_table_pptx: a slide table saved as PowerPoint (src/deck/opr_pptx.c) as
 * a real table (a:tbl in a p:graphicFrame), not a text box, and read back the same:
 *
 *   - three rows and three columns, the columns' widths;
 *   - each cell's text in its row and column, the covered cells there too;
 *   - the rows' heights (a row without one as tall as the table shares);
 *   - the merges: across two columns (gridSpan, hMerge), down two rows
 *     (rowSpan, vMerge);
 *   - the design's parts (firstRow, bandRow ...) and a PowerPoint style's id;
 *     a design name that is not one (an ODP template's) left out;
 *   - a cell's fill;
 *   - no "saved as text boxes" note.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_deck_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#define STYLE "{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"

static owf_para *cell_para(owf_obj *o, int row, int col)
{
    int i;
    for (i = 0; i < o->text.nparas; i++)
        if (o->text.paras[i].table_row == row && o->text.paras[i].table_col == col)
            return &o->text.paras[i];
    return NULL;
}

/* a 3 x 3 table: cells "r,c"; (0,0) across two columns, (1,0) down two rows;
 * rows 0 and 1 with heights, row 2 without; (2,2) filled */
static owf_deck *table_deck(int *obj, const char *style)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    opr_table_spec spec;
    owf_obj *o;
    owf_charfmt f;
    int sel = 0, r, c;
    char t[32];
    if (!d || !u || opr_slide_insert(u, d, 0, -1, &sel) < 0)
        goto out;
    memset(&spec, 0, sizeof spec);
    spec.x = 720;
    spec.y = 720;
    spec.width = 3 * 2400;
    spec.height = 3 * 600;
    spec.rows = 3;
    spec.columns = 3;
    if (opr_obj_insert_table(u, d, 0, &spec, obj) < 0)
        goto out;
    o = &d->slides[0].objs[*obj];
    o->col_widths[0] = 3000;
    o->col_widths[1] = 2000;
    o->col_widths[2] = 2200;
    owf_charfmt_init(&f);
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++) {
            snprintf(t, sizeof t, "%d,%d", r, c);
            owf_para_add_text(cell_para(o, r, c), &f, t, strlen(t));
        }
    cell_para(o, 2, 2)->cell_shading = OWF_SHADE(0x336699);
    owf_table_add_merge(o, 0, 0, 1, 2);
    owf_table_add_merge(o, 1, 0, 2, 1);
    owf_table_set_row_height(o, 0, 576);
    owf_table_set_row_height(o, 1, 1152);
    owf_table_set_row_height(o, 2, 0);
    o->table_design = OWF_TABLE_FIRST_ROW | OWF_TABLE_BANDED_ROWS | OWF_TABLE_LAST_COL;
    o->table_style = malloc(strlen(style) + 1);
    if (o->table_style)
        strcpy(o->table_style, style);
out:
    opr_undo_free(u);
    return d;
}

static const owf_obj *table_of(const owf_deck *d)
{
    int i;
    for (i = 0; d && d->nslides && i < d->slides[0].nobjs; i++)
        if (d->slides[0].objs[i].kind == OWF_OBJ_TABLE)
            return &d->slides[0].objs[i];
    return NULL;
}

static int has_merge(const owf_obj *o, int row, int col, int rows, int cols)
{
    int i;
    for (i = 0; i < o->nmerges; i++)
        if (o->merges[i].row == row && o->merges[i].col == col &&
            o->merges[i].rows == rows && o->merges[i].cols == cols)
            return 1;
    return 0;
}

static int near(int got, int want)
{
    return got >= want - 2 && got <= want + 2;
}

static void cell_text(const owf_obj *o, int row, int col, char *out, size_t size)
{
    int i, j;
    size_t n = 0;
    out[0] = 0;
    for (i = 0; i < o->text.nparas; i++) {
        const owf_para *p = &o->text.paras[i];
        if (p->table_row != row || p->table_col != col)
            continue;
        for (j = 0; j < p->nruns; j++)
            if (p->runs[j].kind == OWF_RUN_TEXT && p->runs[j].text && n + strlen(p->runs[j].text) < size) {
                strcpy(out + n, p->runs[j].text);
                n += strlen(p->runs[j].text);
            }
    }
}

static void round_trip(const char *style, int keep_style)
{
    int obj = -1, r, c;
    owf_deck *d = table_deck(&obj, style), *back = NULL;
    unsigned char *data = NULL;
    size_t n = 0;
    owf_report *report = owf_report_new();
    const owf_obj *o;
    char got[32], want[32];

    CHECK(d && obj >= 0, "a deck with a table");
    CHECK(d && opr_export_pptx(d, &data, &n, report) == OWF_OK && data, "saved as PPTX");
    CHECK(owf_import_deck_memory(data, n, "pptx", &back, NULL, NULL) == OWF_OK && back, "read back");
    o = table_of(back);
    CHECK(o != NULL, "a table read back, not a text box");
    if (o) {
        CHECK(o->ncols == 3 && o->col_widths && near(o->col_widths[0], 3000) && near(o->col_widths[1], 2000) &&
              near(o->col_widths[2], 2200), "the columns' widths");
        CHECK(o->nrows == 3 && o->row_heights && near(o->row_heights[0], 576) && near(o->row_heights[1], 1152) &&
              o->row_heights[2] > 0, "the rows' heights (%d)", o->nrows);
        CHECK(o->nmerges == 2 && has_merge(o, 0, 0, 1, 2) && has_merge(o, 1, 0, 2, 1), "the merges (%d)", o->nmerges);
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++) {
                int covered = (r == 0 && c == 1) || (r == 2 && c == 0);
                cell_text(o, r, c, got, sizeof got);
                snprintf(want, sizeof want, "%d,%d", r, c);
                CHECK(covered ? 1 : !strcmp(got, want), "cell (%d,%d): \"%s\"", r, c, got);
            }
        CHECK(o->table_design == (OWF_TABLE_FIRST_ROW | OWF_TABLE_BANDED_ROWS | OWF_TABLE_LAST_COL),
              "the design (%#x)", o->table_design);
        if (keep_style)
            CHECK(o->table_style && !strcmp(o->table_style, style), "the style's id");
        else
            CHECK(!o->table_style, "no style id for a name that is not one");
        {
            const owf_para *p = NULL;
            int i;
            for (i = 0; i < o->text.nparas && !p; i++)
                if (o->text.paras[i].table_row == 2 && o->text.paras[i].table_col == 2)
                    p = &o->text.paras[i];
            CHECK(p && OWF_SHADE_SET(p->cell_shading) && OWF_SHADE_RGB(p->cell_shading) == 0x336699,
                  "(2,2)'s fill");
        }
    }
    {
        int i, note = 0;
        for (i = 0; i < owf_report_count(report); i++) {
            const char *t = owf_report_note(report, i, NULL, NULL);
            note |= t && strstr(t, "text boxes") != NULL;
        }
        CHECK(!note, "no text-box note");
    }
    owf_report_free(report);
    free(data);
    owf_deck_free(back);
    owf_deck_free(d);
}

/* a block merge: two rows by two columns at (1,1), gridSpan with rowSpan */
static void block_merge(void)
{
    int obj = -1;
    owf_deck *d = table_deck(&obj, STYLE), *back = NULL;
    unsigned char *data = NULL;
    size_t n = 0;
    const owf_obj *o;
    char got[32];
    owf_report *report = owf_report_new();

    CHECK(d && obj >= 0, "a deck with a table (block)");
    if (d && obj >= 0) {
        owf_obj *w = &d->slides[0].objs[obj];
        w->nmerges = 0;
        owf_table_add_merge(w, 1, 1, 2, 2);
    }
    CHECK(d && opr_export_pptx(d, &data, &n, report) == OWF_OK && data, "saved as PPTX (block)");
    CHECK(owf_import_deck_memory(data, n, "pptx", &back, NULL, NULL) == OWF_OK && back, "read back (block)");
    o = table_of(back);
    if (o) {
        CHECK(o->nmerges == 1 && has_merge(o, 1, 1, 2, 2), "the 2 x 2 merge (%d)", o->nmerges);
        cell_text(o, 1, 1, got, sizeof got);
        CHECK(!strcmp(got, "1,1"), "the block's head text: \"%s\"", got);
        cell_text(o, 1, 0, got, sizeof got);
        CHECK(!strcmp(got, "1,0"), "the cell beside the block: \"%s\"", got);
    } else
        CHECK(0, "a table read back (block)");
    owf_report_free(report);
    free(data);
    owf_deck_free(back);
    owf_deck_free(d);
}

static int noted(const owf_report *report, const char *what)
{
    int i, note = 0;
    for (i = 0; i < owf_report_count(report); i++) {
        const char *t = owf_report_note(report, i, NULL, NULL);
        note |= t && strstr(t, what) != NULL;
    }
    return note;
}

/* a table wider than the 64 columns both formats are written with: a note
 * says the rest were left out (as a table read from a file can be) */
static void too_wide(void)
{
    int obj = -1, c;
    owf_deck *d = table_deck(&obj, STYLE);
    unsigned char *data = NULL;
    size_t n = 0;
    owf_report *pptx = owf_report_new(), *odp = owf_report_new(), *fine = owf_report_new();

    CHECK(d && obj >= 0, "a deck with a table (wide)");
    CHECK(d && opr_export_pptx(d, &data, &n, fine) == OWF_OK && !noted(fine, "past the 64th"),
          "no note for 3 columns");
    free(data);
    data = NULL;
    if (d && obj >= 0) {
        owf_obj *w = &d->slides[0].objs[obj];
        int *cw = owf_realloc(w->col_widths, 70 * sizeof *cw);
        CHECK(cw != NULL, "70 columns");
        if (cw) {
            for (c = w->ncols; c < 70; c++)
                cw[c] = 100;
            w->col_widths = cw;
            w->ncols = 70;
        }
    }
    CHECK(d && opr_export_pptx(d, &data, &n, pptx) == OWF_OK && data, "saved as PPTX (wide)");
    CHECK(noted(pptx, "past the 64th"), "PPTX: a note for the columns left out");
    CHECK(d && owf_export_deck_file(d, "build/test-opr-table-wide.odp", "odp", odp) == 0, "saved as ODP (wide)");
    CHECK(noted(odp, "past the 64th"), "ODP: a note for the columns left out");
    owf_report_free(fine);
    owf_report_free(odp);
    owf_report_free(pptx);
    free(data);
    owf_deck_free(d);
}

int main(void)
{
    round_trip(STYLE, 1);
    round_trip("blue", 0);
    block_merge();
    too_wide();
    printf("test_opr_table_pptx: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

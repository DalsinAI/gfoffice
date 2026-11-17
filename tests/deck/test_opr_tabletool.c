/*
 * test_opr_tabletool: the table tools' words (src/deck/opr_tabletool.c,
 * Presentation tables part 4b), which the window's Table menu and ARexx
 * TABLE both send:
 *
 *   - lengths and design parts as text, both ways;
 *   - a cell's facts (opr_table_cell_info) and INFO;
 *   - MERGE, SPLIT, ROWHEIGHT and DESIGN: done, each one Undo step, with
 *     the sentence for the status line; refused with the reason and
 *     nothing changed.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* a slide with a plain 3 by 4 table */
static owf_deck *table_deck(opr_undo *u, int *obj)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    opr_table_spec spec;
    int sel = 0;
    *obj = -1;
    if (!d || opr_slide_insert(u, d, 0, -1, &sel) < 0)
        return d;
    memset(&spec, 0, sizeof spec);
    spec.x = 720;
    spec.y = 720;
    spec.width = 9600;
    spec.height = 1200;
    spec.rows = 3;
    spec.columns = 4;
    if (opr_obj_insert_table(u, d, 0, &spec, obj) < 0)
        *obj = -1;
    opr_undo_clear(u);
    return d;
}

static void test_words(void)
{
    char t[96];
    unsigned d = 99;
    int tw = -1;
    opr_table_length_text(0, t, sizeof t);
    CHECK(!strcmp(t, "auto"), "0 is auto: %s", t);
    opr_table_length_text(567, t, sizeof t);
    CHECK(!strcmp(t, "1cm"), "567 twips: %s", t);
    opr_table_length_text(850, t, sizeof t);
    CHECK(!strcmp(t, "1.5cm"), "850 twips: %s", t);
    opr_table_length_text(1440, t, sizeof t);
    CHECK(!strcmp(t, "2.54cm"), "an inch: %s", t);
    CHECK(opr_table_length_parse("1.5cm", &tw) && tw == 851, "1.5cm: %d", tw);
    CHECK(opr_table_length_parse(" 2 ", &tw) && tw == 1134, "no unit is cm: %d", tw);
    CHECK(opr_table_length_parse("15 mm", &tw) && tw == 851, "15 mm: %d", tw);
    CHECK(opr_table_length_parse("0.5in", &tw) && tw == 720, "0.5in: %d", tw);
    CHECK(opr_table_length_parse("36PT", &tw) && tw == 720, "36PT: %d", tw);
    CHECK(opr_table_length_parse("Auto", &tw) && tw == 0, "Auto: %d", tw);
    tw = 7;
    CHECK(!opr_table_length_parse("tall", &tw) && !opr_table_length_parse("", &tw) &&
          !opr_table_length_parse("-1cm", &tw) && !opr_table_length_parse("2 feet", &tw) &&
          !opr_table_length_parse("1cm high", &tw) && !opr_table_length_parse("60cm", &tw) && tw == 7,
          "not lengths, and one past 22 inches: %d", tw);
    /* a length's text reads back within a twip */
    opr_table_length_text(851, t, sizeof t);
    CHECK(opr_table_length_parse(t, &tw) && tw >= 850 && tw <= 852, "%s back: %d", t, tw);

    opr_table_design_text(0, t, sizeof t);
    CHECK(!strcmp(t, "none"), "no parts: %s", t);
    opr_table_design_text(OWF_TABLE_FIRST_ROW | OWF_TABLE_BANDED_ROWS, t, sizeof t);
    CHECK(!strcmp(t, "header bandedrows"), "two parts: %s", t);
    opr_table_design_text(0x3f, t, sizeof t);
    CHECK(!strcmp(t, "header total firstcolumn lastcolumn bandedrows bandedcolumns"), "every part: %s", t);
    CHECK(opr_table_design_parse(t, &d) && d == 0x3f, "and back: %x", d);
    CHECK(opr_table_design_parse("Header, BandedRows", &d) && d == (OWF_TABLE_FIRST_ROW | OWF_TABLE_BANDED_ROWS),
          "commas and capitals: %x", d);
    CHECK(opr_table_design_parse("firstcol lastcol bandedcols", &d) &&
          d == (OWF_TABLE_FIRST_COL | OWF_TABLE_LAST_COL | OWF_TABLE_BANDED_COLS), "short words: %x", d);
    CHECK(opr_table_design_parse("none", &d) && d == 0, "none: %x", d);
    d = 5;
    CHECK(!opr_table_design_parse("", &d) && !opr_table_design_parse("stripes", &d) &&
          !opr_table_design_parse("none header", &d) && !opr_table_design_parse("headers", &d) && d == 5,
          "nothing, an unknown word, none with a part: %x", d);
}

static void test_tools(void)
{
    opr_undo *u = opr_undo_new();
    int obj = -1, s = 0, o = 0;
    owf_deck *d = table_deck(u, &obj);
    opr_table_info in;
    char said[160];
    const owf_obj *t;
    CHECK(u && d && obj >= 0, "a deck with a table");
    if (!u || !d || obj < 0) { owf_deck_free(d); opr_undo_free(u); return; }
    t = &d->slides[0].objs[obj];

    CHECK(opr_table_cell_info(d, 0, obj, 2, 3, &in) && in.rows == 3 && in.columns == 4 && !in.merged &&
          in.merge_row == 2 && in.merge_column == 3 && in.merge_rows == 1 && in.merge_columns == 1 &&
          in.row_height == 0, "the last cell's facts");
    CHECK(!opr_table_cell_info(d, 0, obj, 3, 0, &in) && !opr_table_cell_info(d, 0, obj, 0, 4, &in) &&
          !opr_table_cell_info(d, 0, obj + 1, 0, 0, &in) && !opr_table_cell_info(d, 1, obj, 0, 0, &in) &&
          !opr_table_cell_info(d, 0, obj, -1, 0, &in), "no such cell, object or slide");
    CHECK(opr_table_tool(u, d, 0, obj, 3, 0, "SPLIT", said, sizeof said) == -2 &&
          !strcmp(said, "That is not a cell of a table."), "a cell past the table: %s", said);

    /* MERGE */
    CHECK(opr_table_tool(u, d, 0, obj, 0, 1, "merge 2 x 3", said, sizeof said) == 0 &&
          !strcmp(said, "Merged 2 rows by 3 columns; Undo splits them again."), "merge 2 x 3: %s", said);
    CHECK(t->nmerges == 1 && t->merges[0].row == 0 && t->merges[0].col == 1 && t->merges[0].rows == 2 &&
          t->merges[0].cols == 3, "the merge is the table's");
    CHECK(opr_table_cell_info(d, 0, obj, 1, 3, &in) && in.merged && in.merge_row == 0 && in.merge_column == 1 &&
          in.merge_rows == 2 && in.merge_columns == 3, "a covered cell knows its merge");
    CHECK(opr_table_tool(u, d, 0, obj, 1, 2, "INFO", said, sizeof said) == 0 && !strcmp(said, "3 4 1 2 2 3 auto none"),
          "INFO in the merge: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 1, 0, "MERGE 1 2", said, sizeof said) == -2 &&
          !strcmp(said, "Those cells hold a merged cell already: split it first.") && t->nmerges == 1,
          "over a merge: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 2, 2, "MERGE 2 2", said, sizeof said) == -2 &&
          !strcmp(said, "The table has 1 row below and 2 columns right of this cell, itself counted.") &&
          t->nmerges == 1, "past the table: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 2, 0, "MERGE 1 1", said, sizeof said) == -2 &&
          !strcmp(said, "A merge covers at least two cells."), "one cell: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 2, 0, "MERGE two", said, sizeof said) == -2 &&
          opr_table_tool(u, d, 0, obj, 2, 0, "MERGE 2", said, sizeof said) == -2 &&
          opr_table_tool(u, d, 0, obj, 2, 0, "MERGE 1 2 3", said, sizeof said) == -2 &&
          opr_table_tool(u, d, 0, obj, 2, 0, "MERGE 0 2", said, sizeof said) == -2 &&
          opr_table_tool(u, d, 0, obj, 2, 0, "MERGE 1 33", said, sizeof said) == -2 &&
          !strncmp(said, "Merge takes rows and columns", 28) && t->nmerges == 1, "not a span: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 2, 0, "MERGE 1 by 2", said, sizeof said) == 0 && t->nmerges == 2 &&
          !strcmp(said, "Merged 1 row by 2 columns; Undo splits them again."), "1 by 2: %s", said);
    CHECK(opr_undo_undo(u, d, &s, &o) > 0 && t == &d->slides[0].objs[obj] && d->slides[0].objs[obj].nmerges == 1,
          "one Undo takes the second merge back");
    t = &d->slides[0].objs[obj];

    /* SPLIT */
    CHECK(opr_table_tool(u, d, 0, obj, 2, 0, "SPLIT", said, sizeof said) == -2 &&
          !strcmp(said, "This cell is not merged.") && t->nmerges == 1, "split a plain cell: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 1, 3, "SPLIT now", said, sizeof said) == -2 && t->nmerges == 1,
          "SPLIT takes nothing: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 1, 3, "split", said, sizeof said) == 0 &&
          !strcmp(said, "Split into 6 cells again; their text is back.") && d->slides[0].objs[obj].nmerges == 0,
          "split from a covered cell: %s", said);
    CHECK(opr_undo_undo(u, d, &s, &o) > 0 && d->slides[0].objs[obj].nmerges == 1, "Undo merges them again");
    CHECK(opr_undo_redo(u, d, &s, &o) > 0 && d->slides[0].objs[obj].nmerges == 0, "Redo splits them");

    /* ROWHEIGHT */
    CHECK(opr_table_tool(u, d, 0, obj, 1, 0, "ROWHEIGHT 1.5cm", said, sizeof said) == 0 &&
          !strcmp(said, "Row 2 is at least 1.5cm high."), "row height: %s", said);
    CHECK(opr_table_cell_info(d, 0, obj, 1, 2, &in) && in.row_height == 851 &&
          opr_table_cell_info(d, 0, obj, 0, 0, &in) && in.row_height == 0, "row 2 only: %d", in.row_height);
    CHECK(opr_table_tool(u, d, 0, obj, 1, 0, "ROWHEIGHT 1 metre", said, sizeof said) == -2 &&
          !strncmp(said, "A row's height is a length", 26) &&
          opr_table_cell_info(d, 0, obj, 1, 0, &in) && in.row_height == 851, "not a length: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 1, 0, "ROWHEIGHT", said, sizeof said) == -2, "no length: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 1, 3, "rowheight auto", said, sizeof said) == 0 &&
          !strcmp(said, "Row 2 is as high as its text needs.") &&
          opr_table_cell_info(d, 0, obj, 1, 0, &in) && in.row_height == 0, "auto: %s", said);
    CHECK(opr_undo_undo(u, d, &s, &o) > 0 && opr_table_cell_info(d, 0, obj, 1, 0, &in) && in.row_height == 851,
          "Undo gives the height back");

    /* DESIGN */
    CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "DESIGN", said, sizeof said) == 0 &&
          opr_table_cell_info(d, 0, obj, 0, 0, &in), "DESIGN alone");
    {
        char now[96];
        opr_table_design_text(in.design, now, sizeof now);
        CHECK(!strcmp(said, now), "DESIGN alone says the parts on: %s", said);
    }
    CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "DESIGN header total bandedrows", said, sizeof said) == 0 &&
          !strcmp(said, "Table design: header total bandedrows.") &&
          d->slides[0].objs[obj].table_design == (OWF_TABLE_FIRST_ROW | OWF_TABLE_LAST_ROW | OWF_TABLE_BANDED_ROWS),
          "three parts: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "DESIGN zebra", said, sizeof said) == -2 &&
          !strncmp(said, "Design parts:", 13) &&
          d->slides[0].objs[obj].table_design == (OWF_TABLE_FIRST_ROW | OWF_TABLE_LAST_ROW | OWF_TABLE_BANDED_ROWS),
          "an unknown part: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 2, 3, "INFO", said, sizeof said) == 0 &&
          !strcmp(said, "3 4 3 4 1 1 auto header total bandedrows"), "INFO: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "design none", said, sizeof said) == 0 &&
          !strcmp(said, "Table design: none.") && d->slides[0].objs[obj].table_design == 0, "none: %s", said);
    CHECK(opr_undo_undo(u, d, &s, &o) > 0 &&
          d->slides[0].objs[obj].table_design == (OWF_TABLE_FIRST_ROW | OWF_TABLE_LAST_ROW | OWF_TABLE_BANDED_ROWS),
          "Undo gives the design back");

    /* anything else */
    CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "PAINT red", said, sizeof said) == -2 &&
          !strncmp(said, "Table tools:", 12), "an unknown tool: %s", said);
    CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "", said, sizeof said) == -2 &&
          opr_table_tool(u, d, 0, obj, 0, 0, NULL, said, sizeof said) == -2, "no tool");
    CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "INFO", NULL, 0) == 0, "nowhere to say it");
    {
        char tiny[8];
        CHECK(opr_table_tool(u, d, 0, obj, 0, 0, "MERGE 9 9", tiny, sizeof tiny) == -2 && strlen(tiny) == 7,
              "a short answer is cut, not overrun: %s", tiny);
    }
    owf_deck_free(d);
    opr_undo_free(u);
}

int main(void)
{
    test_words();
    test_tools();
    printf("test-opr-tabletool: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

/*
 * GFOpenPresentation's portable core: the table tools' words (Presentation
 * tables part 4b). The window's Table menu and the ARexx TABLE command both
 * come through opr_table_tool, so what a requester takes and what a script
 * sends are read the same way, and the sentence the status line shows is
 * made here, where a host test reads it.
 *
 * The edits themselves are opr_edit.c's (opr_table_merge, opr_table_split,
 * opr_table_set_row_height, opr_table_set_design): each is one Undo step.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *word; unsigned bit; } parts[] = {
    { "header", OWF_TABLE_FIRST_ROW },
    { "total", OWF_TABLE_LAST_ROW },
    { "firstcolumn", OWF_TABLE_FIRST_COL },
    { "lastcolumn", OWF_TABLE_LAST_COL },
    { "bandedrows", OWF_TABLE_BANDED_ROWS },
    { "bandedcolumns", OWF_TABLE_BANDED_COLS }
};
#define NPARTS ((int)(sizeof parts / sizeof parts[0]))

static const char *skip(const char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

static void say(char *out, size_t size, const char *text)
{
    if (out && size) snprintf(out, size, "%s", text);
}

/* A word of letters at s, without case; where it ends in *rest. */
static int word_is(const char *s, const char *word, const char **rest)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; ++i)
        if (tolower((unsigned char)s[i]) != word[i]) return 0;
    if (s[n] && !isspace((unsigned char)s[n]) && s[n] != ',') return 0;
    if (rest) *rest = s + n;
    return 1;
}

void opr_table_design_text(unsigned design, char *out, size_t size)
{
    size_t used = 0;
    int i;
    if (!out || !size) return;
    out[0] = 0;
    for (i = 0; i < NPARTS; ++i)
        if ((design & parts[i].bit) && used + strlen(parts[i].word) + 2 < size)
            used += (size_t)snprintf(out + used, size - used, "%s%s", used ? " " : "", parts[i].word);
    if (!used) say(out, size, "none");
}

int opr_table_design_parse(const char *text, unsigned *design)
{
    unsigned d = 0;
    int any = 0, none = 0, i;
    const char *s = text ? skip(text) : "";
    while (*s) {
        const char *rest = NULL;
        for (i = 0; i < NPARTS && !word_is(s, parts[i].word, &rest); ++i) ;
        if (i < NPARTS) d |= parts[i].bit;
        else if (word_is(s, "none", &rest)) none = 1;
        else if (word_is(s, "bandedcols", &rest)) d |= OWF_TABLE_BANDED_COLS;
        else if (word_is(s, "firstcol", &rest)) d |= OWF_TABLE_FIRST_COL;
        else if (word_is(s, "lastcol", &rest)) d |= OWF_TABLE_LAST_COL;
        else return 0;
        any = 1;
        s = rest;
        while (*s == ',' || isspace((unsigned char)*s)) ++s;
    }
    if (!any || (none && d)) return 0;
    if (design) *design = d;
    return 1;
}

void opr_table_length_text(int twips, char *out, size_t size)
{
    char *dot;
    if (!out || !size) return;
    if (twips <= 0) { say(out, size, "auto"); return; }
    /* hundredths of a centimetre, rounded; 567 twips to the centimetre */
    {
        long h = ((long)twips * 100 + 283) / 567;
        snprintf(out, size, "%ld.%02ld", h / 100, h % 100);
    }
    dot = strchr(out, '.');
    if (dot) {                                       /* 1.50 is 1.5, 2.00 is 2 */
        char *e = out + strlen(out) - 1;
        while (e > dot && *e == '0') *e-- = 0;
        if (e == dot) *e = 0;
    }
    if (strlen(out) + 3 <= size) strcat(out, "cm");
}

int opr_table_length_parse(const char *text, int *twips)
{
    const char *s = text ? skip(text) : "";
    char *end;
    double v, per;
    if (word_is(s, "auto", &s) || word_is(s, "automatic", &s)) {
        if (*skip(s)) return 0;
        if (twips) *twips = 0;
        return 1;
    }
    if (!isdigit((unsigned char)*s) && *s != '.') return 0;
    v = strtod(s, &end);
    if (end == s || v < 0) return 0;
    s = skip(end);
    if (!*s || word_is(s, "cm", &s)) per = 567.0;
    else if (word_is(s, "mm", &s)) per = 56.7;
    else if (word_is(s, "in", &s) || word_is(s, "inch", &s) || word_is(s, "inches", &s)) per = 1440.0;
    else if (word_is(s, "pt", &s)) per = 20.0;
    else return 0;
    if (*skip(s)) return 0;
    v = v * per + 0.5;
    if (v > 31680.0) return 0;                       /* opr_table_set_row_height's limit: 22 inches */
    if (twips) *twips = (int)v;
    return 1;
}

int opr_table_cell_info(const owf_deck *deck, int slide, int obj, int row, int column, opr_table_info *info)
{
    const owf_obj *o;
    int i, rows = 0;
    if (!deck || !info || slide < 0 || slide >= deck->nslides || obj < 0 || obj >= deck->slides[slide].nobjs)
        return 0;
    o = &deck->slides[slide].objs[obj];
    if (o->kind != OWF_OBJ_TABLE || o->ncols < 1) return 0;
    for (i = 0; i < o->text.nparas; ++i)
        if (o->text.paras[i].table_row + 1 > rows) rows = o->text.paras[i].table_row + 1;
    if (row < 0 || row >= rows || column < 0 || column >= o->ncols) return 0;
    memset(info, 0, sizeof *info);
    info->rows = rows;
    info->columns = o->ncols;
    info->merge_row = row;
    info->merge_column = column;
    info->merge_rows = info->merge_columns = 1;
    for (i = 0; i < o->nmerges; ++i) {
        const owf_table_merge *m = &o->merges[i];
        if (row >= m->row && row < m->row + m->rows && column >= m->col && column < m->col + m->cols) {
            info->merge_row = m->row;
            info->merge_column = m->col;
            info->merge_rows = m->rows;
            info->merge_columns = m->cols;
            info->merged = 1;
        }
    }
    info->row_height = o->row_heights && row < o->nrows ? o->row_heights[row] : 0;
    info->design = o->table_design;
    return 1;
}

/* "2 3", "2x3", "2 x 3", "2 by 3": rows then columns. */
static int parse_span(const char *s, int *rows, int *columns)
{
    char *end;
    long r, c;
    s = skip(s);
    if (!isdigit((unsigned char)*s)) return 0;
    r = strtol(s, &end, 10);
    s = skip(end);
    if (*s == 'x' || *s == 'X' || *s == ',') s = skip(s + 1);
    else if (word_is(s, "by", &s)) s = skip(s);
    if (!isdigit((unsigned char)*s)) return 0;
    c = strtol(s, &end, 10);
    if (*skip(end) || r < 1 || c < 1 || r > 32 || c > 32) return 0;
    *rows = (int)r;
    *columns = (int)c;
    return 1;
}

int opr_table_tool(opr_undo *u, owf_deck *deck, int slide, int obj, int row, int column, const char *command,
                   char *said, size_t size)
{
    opr_table_info in;
    const char *arg = command ? skip(command) : "";
    int rc;
    say(said, size, "");
    if (!opr_table_cell_info(deck, slide, obj, row, column, &in)) {
        say(said, size, "That is not a cell of a table.");
        return -2;
    }
    if (word_is(arg, "info", &arg)) {
        char design[96], height[24];
        if (*skip(arg)) { say(said, size, "INFO takes nothing after it."); return -2; }
        opr_table_design_text(in.design, design, sizeof design);
        opr_table_length_text(in.row_height, height, sizeof height);
        if (said && size)
            snprintf(said, size, "%d %d %d %d %d %d %s %s", in.rows, in.columns, in.merge_row + 1, in.merge_column + 1,
                     in.merge_rows, in.merge_columns, height, design);
        return 0;
    }
    if (word_is(arg, "merge", &arg)) {
        int rows, columns;
        if (!parse_span(arg, &rows, &columns)) {
            say(said, size, "Merge takes rows and columns, such as 2 3: each from 1 to 32.");
            return -2;
        }
        if (rows * columns < 2) { say(said, size, "A merge covers at least two cells."); return -2; }
        if (row + rows > in.rows || column + columns > in.columns) {
            if (said && size)
                snprintf(said, size, "The table has %d row%s below and %d column%s right of this cell, itself counted.",
                         in.rows - row, in.rows - row == 1 ? "" : "s", in.columns - column,
                         in.columns - column == 1 ? "" : "s");
            return -2;
        }
        rc = opr_table_merge(u, deck, slide, obj, row, column, rows, columns);
        if (rc == -2) { say(said, size, "Those cells hold a merged cell already: split it first."); return -2; }
        if (rc < 0) { say(said, size, "There is not enough memory to merge the cells."); return -1; }
        if (said && size)
            snprintf(said, size, "Merged %d row%s by %d column%s; Undo splits them again.", rows, rows == 1 ? "" : "s",
                     columns, columns == 1 ? "" : "s");
        return 0;
    }
    if (word_is(arg, "split", &arg)) {
        if (*skip(arg)) { say(said, size, "SPLIT takes nothing after it."); return -2; }
        if (!in.merged) { say(said, size, "This cell is not merged."); return -2; }
        rc = opr_table_split(u, deck, slide, obj, row, column);
        if (rc < 0) { say(said, size, rc == -2 ? "This cell is not merged." : "There is not enough memory to split the cell."); return rc == -2 ? -2 : -1; }
        if (said && size)
            snprintf(said, size, "Split into %d cell%s again; their text is back.", in.merge_rows * in.merge_columns,
                     in.merge_rows * in.merge_columns == 1 ? "" : "s");
        return 0;
    }
    if (word_is(arg, "rowheight", &arg)) {
        int twips;
        char text[24];
        if (!opr_table_length_parse(arg, &twips)) {
            say(said, size, "A row's height is a length up to 55cm, such as 1.5cm, 15mm, 0.5in or 36pt, or auto.");
            return -2;
        }
        rc = opr_table_set_row_height(u, deck, slide, obj, row, twips);
        if (rc < 0) { say(said, size, rc == -2 ? "That row's height cannot be set." : "There is not enough memory to set the row's height."); return rc == -2 ? -2 : -1; }
        opr_table_length_text(twips, text, sizeof text);
        if (said && size) {
            if (twips) snprintf(said, size, "Row %d is at least %s high.", row + 1, text);
            else snprintf(said, size, "Row %d is as high as its text needs.", row + 1);
        }
        return 0;
    }
    if (word_is(arg, "design", &arg)) {
        unsigned design;
        char text[96];
        if (!*skip(arg)) {
            opr_table_design_text(in.design, text, sizeof text);
            say(said, size, text);
            return 0;
        }
        if (!opr_table_design_parse(arg, &design)) {
            say(said, size, "Design parts: header, total, firstcolumn, lastcolumn, bandedrows, bandedcolumns, or none.");
            return -2;
        }
        rc = opr_table_set_design(u, deck, slide, obj, design);
        if (rc < 0) { say(said, size, rc == -2 ? "That design cannot be set." : "There is not enough memory to change the design."); return rc == -2 ? -2 : -1; }
        opr_table_design_text(design, text, sizeof text);
        if (said && size) snprintf(said, size, "Table design: %s.", text);
        return 0;
    }
    say(said, size, "Table tools: MERGE rows columns, SPLIT, ROWHEIGHT length, DESIGN parts, INFO.");
    return -2;
}

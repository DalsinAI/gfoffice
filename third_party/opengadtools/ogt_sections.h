/* ogt_sections: OpenGadTools' inspector sections (DESIGN.md section 1): a
 * column of titled sections, each a grid of label and field, the fields
 * ordinary GadTools gadgets (cycle, string, integer, checkbox, button). The
 * program says what goes in each row; ogt_sections lays the rows out down
 * the column, makes the gadgets and draws the section titles and rules in
 * the theme. The prefs editors do this by hand today; OpenWrite 2's
 * inspector is the first user (the user, 10 October 2026).
 *
 * Call ogt_sections_draw() after GT_RefreshWindow() and between
 * GT_BeginRefresh() and GT_EndRefresh().
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_SECTIONS_H
#define OGT_SECTIONS_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include "ogt_draw.h"

#define OGT_SEC_MAX 8
#define OGT_SEC_ROWS 12

enum { OGT_ROW_CYCLE = 1, OGT_ROW_STRING, OGT_ROW_INTEGER, OGT_ROW_CHECKBOX, OGT_ROW_BUTTON, OGT_ROW_TEXT, OGT_ROW_SLIDER };

typedef struct ogt_row {
    int kind;                   /* OGT_ROW_* */
    int id;                     /* the gadget's id */
    const char *label;          /* at the left (NULL: the field spans the row) */
    int width;                  /* the field's width in pixels (0: the column's) */
    const char *const *labels;  /* a cycle's choices */
    const char *text;           /* a string's or a text's initial text */
    LONG value;                 /* a cycle's active, an integer's number, a checkbox's state, a slider's level */
    LONG min, max;              /* a slider's */
    int disabled;
} ogt_row;

typedef struct ogt_isection {
    const char *title;
    int n;
    ogt_row row[OGT_SEC_ROWS];
    int y, h;                   /* where it landed */
} ogt_isection;

typedef struct ogt_sections {
    int n;
    ogt_isection sec[OGT_SEC_MAX];
    int x, y, w, h;             /* the column */
    int label_w, lh, gap;       /* the labels' column, a row's height, the space between rows */
    struct TextAttr *ta;
    APTR vi;
    struct Gadget *gad[OGT_SEC_MAX][OGT_SEC_ROWS];
} ogt_sections;

/* Starts a column at x,y, w wide (label_w for the labels; 0: a third); rows lh high (the font's plus 6 is right). */
void ogt_sections_init(ogt_sections *s, int x, int y, int w, int label_w, int lh, struct TextAttr *ta, APTR vi);
/* Adds a section (the rows copied); returns its index. */
int ogt_sections_add(ogt_sections *s, const char *title, const ogt_row *rows, int n);
/* Makes every row's gadget after `g` (from CreateContext), top to bottom; returns the last, NULL on failure.
 * Sets each section's y and h and the column's h. */
struct Gadget *ogt_sections_gadgets(ogt_sections *s, struct Gadget *g);
/* The gadget of a row, by id. */
struct Gadget *ogt_sections_gadget(ogt_sections *s, int id);
/* Draws the titles and the rules between sections. */
void ogt_sections_draw(ogt_sections *s, struct Window *win, ogt_ctx *ctx);

#endif

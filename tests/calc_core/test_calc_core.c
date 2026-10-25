/*
 * OpenCalc's core on the PC: navigation, editing, undo, fill, insert and
 * delete, the clipboard's text, styles, sheets, files and the draw list.
 * Run from the repository's top (make calc-test, make calc-check).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opencalc_core.h"
#include "owf_zip.h"

static int failures, checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        checks++;                                                                    \
        if (!(cond)) {                                                               \
            failures++;                                                              \
            fprintf(stderr, "%s:%d: %s: failed: %s\n", __FILE__, __LINE__, __func__, #cond); \
        }                                                                            \
    } while (0)

#define CHECK_STR(got, want)                                                         \
    do {                                                                             \
        const char *g_ = (got), *w_ = (want);                                        \
        checks++;                                                                    \
        if (!g_ || strcmp(g_, w_)) {                                                 \
            failures++;                                                              \
            fprintf(stderr, "%s:%d: %s: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, __func__, g_ ? g_ : "(null)", w_); \
        }                                                                            \
    } while (0)

/* ---- Helpers ---- */

static void type(oc_core *core, const char *s)
{
    oc_edit_type(core, s, strlen(s));
}

static void enter_at(oc_core *core, const char *cell, const char *input)
{
    owf_range r;
    unsigned row, col;
    if (!oc_parse_range(cell, &r))
        return;
    oc_core_active(core, &row, &col);
    oc_core_set_input(core, oc_core_sheet(core), r.first_row, r.first_col, input);
}

static const owf_cell *at(oc_core *core, const char *cell)
{
    owf_range r;
    if (!oc_parse_range(cell, &r))
        return NULL;
    return owf_sheet_find(&oc_core_book(core)->sheets[oc_core_sheet(core)], r.first_row, r.first_col);
}

static double num(oc_core *core, const char *cell)
{
    const owf_cell *c = at(core, cell);
    return c && c->type == OWF_VALUE_NUMBER ? c->number : -99999;
}

static const char *formula(oc_core *core, const char *cell)
{
    const owf_cell *c = at(core, cell);
    return c && c->formula ? c->formula : "";
}

static const char *text(oc_core *core, const char *cell)
{
    const owf_cell *c = at(core, cell);
    return c && c->type == OWF_VALUE_TEXT ? c->text : "";
}

static int empty(oc_core *core, const char *cell)
{
    const owf_cell *c = at(core, cell);
    return !c || (c->type == OWF_VALUE_EMPTY && !c->formula);
}

static const char *active_name(oc_core *core)
{
    static char name[16];
    unsigned row, col;
    oc_core_active(core, &row, &col);
    oc_cell_name(row, col, name);
    return name;
}

static const char *name_box(oc_core *core)
{
    static char name[40];
    oc_core_name_box(core, name, sizeof name);
    return name;
}

static void go(oc_core *core, const char *cell)
{
    oc_core_goto(core, cell);
}

static int key(oc_core *core, int k, unsigned mods)
{
    return oc_core_key(core, k, mods);
}

/* 6 pixels a character at 100%, for exact numbers */
static int measure6(void *user, const char *s, size_t n, unsigned flags, int font, int size)
{
    size_t i, chars = 0;
    (void)user;
    (void)flags;
    (void)font;
    (void)size;
    for (i = 0; i < n; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80)
            chars++;
    return (int)chars * 6;
}

static oc_core *fresh(void)
{
    oc_core *core = oc_core_new(NULL);
    if (core) {
        oc_view_set_measure(core, measure6, NULL);
        oc_view_set_size(core, 640, 400);
        oc_view_set_headers(core, 40, 18);
    }
    return core;
}

/* ---- Tests ---- */

static void test_names(void)
{
    char s[16];
    owf_range r;
    oc_col_name(0, s);
    CHECK_STR(s, "A");
    oc_col_name(25, s);
    CHECK_STR(s, "Z");
    oc_col_name(26, s);
    CHECK_STR(s, "AA");
    oc_col_name(OC_MAX_COLS - 1, s);
    CHECK_STR(s, "XFD");
    oc_cell_name(11, 1, s);
    CHECK_STR(s, "B12");
    CHECK(oc_parse_range("$B$12", &r) && r.first_row == 11 && r.first_col == 1 && r.last_row == 11);
    CHECK(oc_parse_range("D8:B2", &r) && r.first_row == 1 && r.last_row == 7 && r.first_col == 1 && r.last_col == 3);
    CHECK(oc_parse_range("C:E", &r) && r.first_col == 2 && r.last_col == 4 && r.first_row == 0 && r.last_row == OC_MAX_ROWS - 1);
    CHECK(oc_parse_range("3:5", &r) && r.first_row == 2 && r.last_row == 4 && r.last_col == OC_MAX_COLS - 1);
    CHECK(!oc_parse_range("B", &r));
    CHECK(!oc_parse_range("A0", &r));
    CHECK(!oc_parse_range("XFE1", &r));
    CHECK(!oc_parse_range("hello", &r));
}

static void test_navigation(void)
{
    oc_core *core = fresh();
    unsigned top, left;
    int i;
    CHECK(core != NULL);
    if (!core)
        return;
    CHECK_STR(oc_core_book(core)->sheets[0].name, "Sheet1");
    CHECK_STR(active_name(core), "A1");
    key(core, OC_KEY_LEFT, 0);
    key(core, OC_KEY_UP, 0);
    CHECK_STR(active_name(core), "A1");                  /* the sheet's edge holds */
    key(core, OC_KEY_RIGHT, 0);
    key(core, OC_KEY_DOWN, 0);
    CHECK_STR(active_name(core), "B2");
    key(core, OC_KEY_RIGHT, OC_MOD_SHIFT);
    key(core, OC_KEY_RIGHT, OC_MOD_SHIFT);
    key(core, OC_KEY_DOWN, OC_MOD_SHIFT);
    CHECK_STR(active_name(core), "B2");                  /* Shift extends from the active cell, which stays */
    CHECK_STR(name_box(core), "B2:D3");
    key(core, OC_KEY_LEFT, OC_MOD_SHIFT);
    CHECK_STR(name_box(core), "B2:C3");
    key(core, OC_KEY_DOWN, 0);
    CHECK_STR(name_box(core), "B3");

    /* Ctrl+arrows: the edges of the data */
    for (i = 1; i <= 5; i++) {
        char cell[16];
        sprintf(cell, "A%d", i);
        enter_at(core, cell, "1");
    }
    enter_at(core, "A10", "x");
    go(core, "A1");
    key(core, OC_KEY_DOWN, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A5");
    key(core, OC_KEY_DOWN, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A10");
    key(core, OC_KEY_DOWN, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A1048576");
    oc_view_scroll_pos(core, &top, &left);
    CHECK(top > 1048000);                                /* the view followed */
    key(core, OC_KEY_UP, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A10");
    key(core, OC_KEY_UP, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A5");
    key(core, OC_KEY_UP, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A1");
    key(core, OC_KEY_DOWN, OC_MOD_CTRL | OC_MOD_SHIFT);
    CHECK_STR(name_box(core), "A1:A5");
    enter_at(core, "C1", "1");
    enter_at(core, "D1", "1");
    enter_at(core, "G1", "1");
    go(core, "A1");
    key(core, OC_KEY_RIGHT, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "C1");
    key(core, OC_KEY_RIGHT, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "D1");
    key(core, OC_KEY_RIGHT, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "G1");
    key(core, OC_KEY_RIGHT, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "XFD1");
    key(core, OC_KEY_LEFT, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "G1");

    /* Home, End, Ctrl+Home, Ctrl+End */
    go(core, "E3");
    key(core, OC_KEY_HOME, 0);
    CHECK_STR(active_name(core), "A3");
    go(core, "B1");
    key(core, OC_KEY_END, 0);
    CHECK_STR(active_name(core), "G1");                  /* the row's last value, as LibreOffice */
    key(core, OC_KEY_END, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "G10");                 /* the used part's last cell */
    key(core, OC_KEY_HOME, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A1");

    /* Tab, Tab, Return: back to the column the Tabs began in */
    go(core, "B20");
    key(core, OC_KEY_TAB, 0);
    key(core, OC_KEY_TAB, 0);
    CHECK_STR(active_name(core), "D20");
    key(core, OC_KEY_RETURN, 0);
    CHECK_STR(active_name(core), "B21");
    key(core, OC_KEY_TAB, OC_MOD_SHIFT);
    CHECK_STR(active_name(core), "A21");
    key(core, OC_KEY_RETURN, OC_MOD_SHIFT);
    CHECK_STR(active_name(core), "A20");

    /* Return and Tab inside a selection */
    go(core, "B2:C3");
    CHECK_STR(active_name(core), "B2");
    key(core, OC_KEY_RETURN, 0);
    CHECK_STR(active_name(core), "B3");
    key(core, OC_KEY_RETURN, 0);
    CHECK_STR(active_name(core), "C2");
    key(core, OC_KEY_RETURN, 0);
    CHECK_STR(active_name(core), "C3");
    key(core, OC_KEY_RETURN, 0);
    CHECK_STR(active_name(core), "B2");                  /* round again */
    CHECK_STR(name_box(core), "B2:C3");                  /* the selection stays */
    key(core, OC_KEY_TAB, 0);
    CHECK_STR(active_name(core), "C2");
    key(core, OC_KEY_TAB, 0);
    CHECK_STR(active_name(core), "B3");
    key(core, OC_KEY_TAB, OC_MOD_SHIFT);
    CHECK_STR(active_name(core), "C2");

    /* PageDown: a screen of rows (400 - 18 = 382 pixels, 17 a row: 22 whole rows) */
    go(core, "A1");
    key(core, OC_KEY_PAGEDOWN, 0);
    CHECK_STR(active_name(core), "A23");
    oc_view_scroll_pos(core, &top, &left);
    CHECK(top == 22);
    key(core, OC_KEY_PAGEUP, 0);
    CHECK_STR(active_name(core), "A1");
    oc_view_scroll_pos(core, &top, &left);
    CHECK(top == 0);
    key(core, OC_KEY_PAGEDOWN, OC_MOD_ALT);              /* a screen to the right */
    oc_view_scroll_pos(core, &top, &left);
    CHECK(left > 0);

    /* hidden rows are stepped over */
    go(core, "A2");
    CHECK(oc_core_hide(core, 0, 1) == OWF_OK);
    CHECK(oc_core_row_height(core, 1) == 0);
    go(core, "A1");
    key(core, OC_KEY_DOWN, 0);
    CHECK_STR(active_name(core), "A3");
    key(core, OC_KEY_UP, 0);
    CHECK_STR(active_name(core), "A1");

    /* frozen panes: Ctrl+Home goes to the first cell after them */
    go(core, "B3");
    CHECK(oc_core_freeze(core) == OWF_OK);
    CHECK(oc_core_book(core)->sheets[0].freeze_rows == 2 && oc_core_book(core)->sheets[0].freeze_cols == 1);
    go(core, "F30");
    key(core, OC_KEY_HOME, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "B3");
    key(core, OC_KEY_HOME, OC_MOD_CTRL);
    CHECK_STR(active_name(core), "A1");
    oc_core_freeze(core);                                /* again: unfrozen */
    CHECK(oc_core_book(core)->sheets[0].freeze_rows == 0);

    /* whole rows, columns, all */
    go(core, "C4");
    oc_core_select_cols(core);
    CHECK_STR(name_box(core), "C:C");
    go(core, "C4");
    oc_core_select_rows(core);
    CHECK_STR(name_box(core), "4:4");
    oc_core_select_all(core);
    CHECK_STR(active_name(core), "C4");
    CHECK(oc_core_goto(core, "nonsense") == OWF_ERR_FORMAT);

    /* Ctrl+PageDown: the next sheet */
    oc_core_add_sheet(core, NULL);
    oc_core_set_sheet(core, 0);
    key(core, OC_KEY_PAGEDOWN, OC_MOD_CTRL);
    CHECK(oc_core_sheet(core) == 1);
    key(core, OC_KEY_PAGEUP, OC_MOD_CTRL);
    CHECK(oc_core_sheet(core) == 0);
    oc_core_free(core);
}

static void test_editing(void)
{
    oc_core *core = fresh();
    size_t caret;
    const char *t;
    int rc;
    if (!core)
        return;
    type(core, "12");
    CHECK(oc_core_editing(core) == OC_EDIT_ENTER);
    CHECK_STR(oc_core_input_text(core), "12");
    key(core, OC_KEY_RETURN, 0);
    CHECK(!oc_core_editing(core));
    CHECK(num(core, "A1") == 12);
    CHECK_STR(active_name(core), "A2");
    CHECK(oc_core_is_dirty(core));
    type(core, "=A1*2");
    key(core, OC_KEY_RETURN, 0);
    CHECK(num(core, "A2") == 24);
    CHECK_STR(formula(core, "A2"), "A1*2");
    type(core, "99");
    key(core, OC_KEY_ESCAPE, 0);
    CHECK(empty(core, "A3"));
    CHECK_STR(active_name(core), "A3");

    /* F2: edit what is there; the arrows move the caret */
    go(core, "A2");
    key(core, OC_KEY_F2, 0);
    CHECK(oc_core_editing(core) == OC_EDIT_EDIT);
    t = oc_edit_text(core, &caret);
    CHECK_STR(t, "=A1*2");
    CHECK(caret == 5);
    key(core, OC_KEY_LEFT, 0);
    key(core, OC_KEY_BACKSPACE, 0);
    type(core, "+");
    key(core, OC_KEY_END, 0);
    key(core, OC_KEY_BACKSPACE, 0);
    type(core, "3");
    CHECK_STR(oc_edit_text(core, NULL), "=A1+3");
    key(core, OC_KEY_RETURN, 0);
    CHECK(num(core, "A2") == 15);
    CHECK_STR(active_name(core), "A3");

    /* a change reaches what reads it */
    enter_at(core, "A1", "5");
    CHECK(num(core, "A2") == 8);

    /* typing a reference with the arrows */
    go(core, "A3");
    type(core, "=");
    CHECK(oc_edit_wants_ref(core));
    key(core, OC_KEY_UP, 0);
    CHECK_STR(oc_edit_text(core, NULL), "=A2");
    key(core, OC_KEY_UP, 0);
    CHECK_STR(oc_edit_text(core, NULL), "=A1");
    type(core, "+");
    key(core, OC_KEY_RIGHT, 0);
    CHECK_STR(oc_edit_text(core, NULL), "=A1+B3");
    key(core, OC_KEY_RETURN, 0);
    CHECK_STR(formula(core, "A3"), "A1+B3");
    CHECK(num(core, "A3") == 5);

    /* in enter mode, an arrow commits and moves */
    go(core, "C1");
    type(core, "7");
    key(core, OC_KEY_RIGHT, 0);
    CHECK(num(core, "C1") == 7);
    CHECK_STR(active_name(core), "D1");

    /* F2 changes mode */
    type(core, "abc");
    key(core, OC_KEY_F2, 0);
    CHECK(oc_core_editing(core) == OC_EDIT_EDIT);
    key(core, OC_KEY_LEFT, 0);
    type(core, "X");
    CHECK_STR(oc_edit_text(core, NULL), "abXc");
    key(core, OC_KEY_TAB, 0);
    CHECK_STR(text(core, "D1"), "abXc");
    CHECK_STR(active_name(core), "E1");

    /* UTF-8: Backspace takes a whole character */
    type(core, "a\xC3\xA9");
    key(core, OC_KEY_BACKSPACE, 0);
    CHECK_STR(oc_edit_text(core, NULL), "a");
    key(core, OC_KEY_ESCAPE, 0);

    /* Ctrl+Return: into every selected cell, formulas moved */
    go(core, "F1:F3");
    type(core, "=A1+1");
    key(core, OC_KEY_RETURN, OC_MOD_CTRL);
    CHECK_STR(formula(core, "F1"), "A1+1");
    CHECK_STR(formula(core, "F2"), "A2+1");
    CHECK_STR(formula(core, "F3"), "A3+1");
    CHECK(num(core, "F2") == 9);

    /* the formula bar's text for what isn't typed as it shows */
    enter_at(core, "G1", "'TRUE");
    go(core, "G1");
    CHECK_STR(text(core, "G1"), "TRUE");
    CHECK_STR(oc_core_input_text(core), "'TRUE");
    enter_at(core, "G2", "plain");
    go(core, "G2");
    CHECK_STR(oc_core_input_text(core), "plain");
    go(core, "A2");
    CHECK_STR(oc_core_input_text(core), "=A1+3");
    go(core, "A1");
    CHECK_STR(oc_core_input_text(core), "5");

    /* the formula bar: the window's gadget sets the whole text */
    go(core, "H1");
    oc_edit_set(core, "=2+2", 4);
    CHECK(oc_edit_commit(core, OC_MOVE_DOWN) == OWF_OK);
    CHECK(num(core, "H1") == 4);
    CHECK_STR(active_name(core), "H2");

    /* Backspace not typing: empties the cell's text and starts typing */
    go(core, "H1");
    key(core, OC_KEY_BACKSPACE, 0);
    CHECK(oc_core_editing(core));
    CHECK_STR(oc_edit_text(core, NULL), "");
    key(core, OC_KEY_ESCAPE, 0);
    CHECK(num(core, "H1") == 4);                         /* Escape: as it was */

    /* a formula that won't parse is kept, and said */
    go(core, "H3");
    type(core, "=SUM(");
    rc = key(core, OC_KEY_RETURN, 0);
    CHECK(rc == OWF_ERR_FORMAT);
    CHECK_STR(formula(core, "H3"), "SUM(");

    /* Delete clears the selection */
    go(core, "F1:F3");
    key(core, OC_KEY_DELETE, 0);
    CHECK(empty(core, "F1") && empty(core, "F2") && empty(core, "F3"));
    oc_core_free(core);
}

static void test_undo(void)
{
    oc_core *core = fresh();
    int i;
    if (!core)
        return;
    enter_at(core, "A1", "1");
    enter_at(core, "A2", "2");
    enter_at(core, "A3", "3");
    enter_at(core, "A4", "=SUM(A1:A3)");
    CHECK(num(core, "A4") == 6);
    enter_at(core, "A1", "10");
    CHECK(num(core, "A4") == 15);
    CHECK(oc_core_can_undo(core));
    CHECK(!oc_core_can_redo(core));
    CHECK(oc_core_undo(core) == OWF_OK);
    CHECK(num(core, "A1") == 1);
    CHECK(num(core, "A4") == 6);                         /* worked out again */
    CHECK(oc_core_can_redo(core));
    CHECK(oc_core_redo(core) == OWF_OK);
    CHECK(num(core, "A4") == 15);
    oc_core_undo(core);
    oc_core_undo(core);
    CHECK(empty(core, "A4"));
    oc_core_undo(core);
    oc_core_undo(core);
    oc_core_undo(core);
    CHECK(empty(core, "A1") && empty(core, "A2") && empty(core, "A3"));
    CHECK(!oc_core_can_undo(core));
    CHECK(oc_core_undo(core) == OWF_ERR_UNSUPPORTED);
    for (i = 0; i < 5; i++)
        oc_core_redo(core);
    CHECK(num(core, "A1") == 10 && num(core, "A4") == 15);
    /* a new change ends redo */
    oc_core_undo(core);
    enter_at(core, "B1", "x");
    CHECK(!oc_core_can_redo(core));

    /* the cursor comes back with the step */
    go(core, "C5");
    type(core, "5");
    key(core, OC_KEY_RETURN, 0);
    CHECK_STR(active_name(core), "C6");
    oc_core_undo(core);
    CHECK_STR(active_name(core), "C5");

    /* bounded: 16 steps */
    for (i = 0; i < 20; i++) {
        char v[16];
        sprintf(v, "%d", i);
        enter_at(core, "D1", v);
    }
    for (i = 0; oc_core_can_undo(core); i++)
        oc_core_undo(core);
    CHECK(i == OC_UNDO_LIMIT);
    CHECK(num(core, "D1") == 3);                         /* the 4th entry: the first 4 steps fell off */
    oc_core_free(core);

    /* a change too big to keep clears undo and says so */
    core = fresh();
    {
        size_t n = 0, cap = 4 * 1024 * 1024;
        char *big = malloc(cap);
        for (i = 0; big && i < 12000; i++)
            n += (size_t)sprintf(big + n, "a long piece of text to make the undo step big %05d\n", i);
        go(core, "A1");
        CHECK(big && oc_core_paste(core, big, n) == OWF_OK);
        CHECK_STR(text(core, "A12000"), "a long piece of text to make the undo step big 11999");
        free(big);
    }
    go(core, "A:A");
    CHECK(oc_core_clear(core, OC_CLEAR_CONTENTS) == OWF_OK);
    CHECK(empty(core, "A5"));
    CHECK(oc_core_undo_lost(core));
    CHECK(!oc_core_can_undo(core));
    oc_core_free(core);
}

static void test_fill(void)
{
    oc_core *core = fresh();
    if (!core)
        return;
    enter_at(core, "A1", "1");
    enter_at(core, "A2", "2");
    enter_at(core, "A3", "3");
    enter_at(core, "A4", "4");
    enter_at(core, "B1", "=A1*10");
    go(core, "B1");
    oc_core_toggle_flags(core, OWF_BOLD);
    go(core, "B1:B4");
    CHECK(oc_core_fill(core, OC_FILL_DOWN) == OWF_OK);
    CHECK_STR(formula(core, "B2"), "A2*10");
    CHECK_STR(formula(core, "B4"), "A4*10");
    CHECK(num(core, "B3") == 30);
    {
        owf_cellstyle st;
        go(core, "B4");
        oc_core_current_style(core, &st);
        CHECK(st.font.flags & OWF_BOLD);                 /* the style comes too */
    }
    /* one cell: from the one above */
    enter_at(core, "C1", "word");
    go(core, "C2");
    CHECK(oc_core_fill(core, OC_FILL_DOWN) == OWF_OK);
    CHECK_STR(text(core, "C2"), "word");
    /* right */
    enter_at(core, "A6", "=A1+A2");
    go(core, "A6:C6");
    CHECK(oc_core_fill(core, OC_FILL_RIGHT) == OWF_OK);
    CHECK_STR(formula(core, "B6"), "B1+B2");
    CHECK_STR(formula(core, "C6"), "C1+C2");
    CHECK(oc_core_undo(core) == OWF_OK);
    CHECK(empty(core, "B6") && empty(core, "C6"));
    /* nothing to fill from */
    go(core, "A1");
    CHECK(oc_core_fill(core, OC_FILL_DOWN) == OWF_ERR_UNSUPPORTED);
    oc_core_free(core);
}

static void test_insert_delete(void)
{
    oc_core *core = fresh();
    owf_sheet *s;
    if (!core)
        return;
    enter_at(core, "A1", "1");
    enter_at(core, "A2", "2");
    enter_at(core, "A3", "3");
    enter_at(core, "A4", "=SUM(A1:A3)");
    enter_at(core, "B2", "=A2*2");
    s = &oc_core_book(core)->sheets[0];
    go(core, "A2");
    oc_core_set_row_height(core, 600);
    go(core, "A2:A3");
    CHECK(oc_core_insert(core, 0) == OWF_OK);            /* two rows go in above row 2 */
    s = &oc_core_book(core)->sheets[0];
    CHECK(num(core, "A1") == 1);
    CHECK(empty(core, "A2") && empty(core, "A3"));
    CHECK(num(core, "A4") == 2);
    CHECK_STR(formula(core, "A6"), "SUM(A1:A5)");      /* the range grew */
    CHECK_STR(formula(core, "B4"), "A4*2");
    CHECK(num(core, "A6") == 6);
    CHECK(oc_core_row_height(core, 3) == 600);           /* the row's height went with it */
    CHECK(oc_core_undo(core) == OWF_OK);
    CHECK_STR(formula(core, "A4"), "SUM(A1:A3)");
    CHECK(num(core, "A2") == 2);
    CHECK(oc_core_row_height(core, 1) == 600);
    CHECK(oc_core_redo(core) == OWF_OK);
    CHECK_STR(formula(core, "A6"), "SUM(A1:A5)");
    oc_core_undo(core);

    /* delete row 1 */
    go(core, "A1");
    CHECK(oc_core_delete(core, 0) == OWF_OK);
    CHECK(num(core, "A1") == 2);
    CHECK_STR(formula(core, "A3"), "SUM(A1:A2)");
    CHECK(num(core, "A3") == 5);
    CHECK(oc_core_undo(core) == OWF_OK);
    CHECK(num(core, "A1") == 1);
    CHECK_STR(formula(core, "A4"), "SUM(A1:A3)");
    CHECK(num(core, "A4") == 6);

    /* columns */
    go(core, "A1");
    CHECK(oc_core_insert(core, 1) == OWF_OK);
    CHECK(num(core, "B1") == 1);
    CHECK_STR(formula(core, "C2"), "B2*2");
    go(core, "A1");
    CHECK(oc_core_delete(core, 1) == OWF_OK);
    CHECK_STR(formula(core, "B2"), "A2*2");

    /* nothing falls off the sheet */
    enter_at(core, "A1048576", "last");
    go(core, "A5");
    CHECK(oc_core_insert(core, 0) == OWF_ERR_FORMAT);
    (void)s;
    oc_core_free(core);
}

static void test_clear_and_styles(void)
{
    oc_core *core = fresh();
    owf_cellstyle st;
    char shown[64];
    if (!core)
        return;
    enter_at(core, "A1", "1.5");
    enter_at(core, "A2", "2");
    go(core, "A1:B2");
    CHECK(oc_core_toggle_flags(core, OWF_BOLD) == OWF_OK);
    go(core, "B2");
    oc_core_current_style(core, &st);
    CHECK(st.font.flags & OWF_BOLD);                     /* an empty cell takes a style too */
    go(core, "A1:B2");
    oc_core_toggle_flags(core, OWF_BOLD);                /* on everywhere: now off */
    oc_core_current_style(core, &st);
    CHECK(!(st.font.flags & OWF_BOLD));
    oc_core_undo(core);
    oc_core_current_style(core, &st);
    CHECK(st.font.flags & OWF_BOLD);

    go(core, "A1");
    CHECK(oc_core_set_number_format(core, "0.00") == OWF_OK);
    oc_core_cell_text(core, 0, 0, 0, shown, sizeof shown);
    CHECK_STR(shown, "1.50");
    CHECK(oc_core_set_halign(core, OWF_HALIGN_CENTRE) == OWF_OK);
    CHECK(oc_core_set_fill(core, OWF_SHADE(0xFFFF00)) == OWF_OK);
    oc_core_current_style(core, &st);
    CHECK(st.halign == OWF_HALIGN_CENTRE && OWF_SHADE_RGB(st.fill) == 0xFFFF00 && (st.font.flags & OWF_BOLD));
    CHECK(oc_core_set_font_colour(core, 0xFF0000) == OWF_OK);

    go(core, "C3:D4");
    CHECK(oc_core_set_borders(core, OC_BORDERS_OUTLINE, OWF_LINE_THIN, 0) == OWF_OK);
    go(core, "C3");
    oc_core_current_style(core, &st);
    CHECK(st.border[OWF_SIDE_TOP] == OWF_LINE_THIN && st.border[OWF_SIDE_LEFT] == OWF_LINE_THIN &&
          !st.border[OWF_SIDE_RIGHT] && !st.border[OWF_SIDE_BOTTOM]);
    go(core, "D4");
    oc_core_current_style(core, &st);
    CHECK(st.border[OWF_SIDE_BOTTOM] == OWF_LINE_THIN && st.border[OWF_SIDE_RIGHT] == OWF_LINE_THIN && !st.border[OWF_SIDE_TOP]);

    /* a whole column: the column's own style */
    go(core, "F:F");
    CHECK(oc_core_toggle_flags(core, OWF_ITALIC) == OWF_OK);
    go(core, "F500");
    oc_core_current_style(core, &st);
    CHECK(st.font.flags & OWF_ITALIC);
    oc_core_undo(core);
    oc_core_current_style(core, &st);
    CHECK(!(st.font.flags & OWF_ITALIC));

    /* clear: contents keep the style; formats go */
    go(core, "A1");
    CHECK(oc_core_clear(core, OC_CLEAR_CONTENTS) == OWF_OK);
    CHECK(empty(core, "A1"));
    oc_core_current_style(core, &st);
    CHECK(st.font.flags & OWF_BOLD);
    CHECK(oc_core_clear(core, OC_CLEAR_FORMATS) == OWF_OK);
    oc_core_current_style(core, &st);
    CHECK(!(st.font.flags & OWF_BOLD) && !st.fill);
    oc_core_undo(core);
    oc_core_undo(core);
    CHECK(num(core, "A1") == 1.5);

    /* widths */
    go(core, "B1");
    CHECK(oc_core_set_col_width(core, 2000) == OWF_OK);
    CHECK(oc_core_col_width(core, 1) == 2000);
    CHECK(oc_core_col_width(core, 2) == 960);
    CHECK(oc_core_set_cols_width(core, 1, 3, 1500) == OWF_OK);
    CHECK(oc_core_col_width(core, 1) == 1500 && oc_core_col_width(core, 3) == 1500 && oc_core_col_width(core, 4) == 960);
    go(core, "C1");
    CHECK(oc_core_hide(core, 1, 1) == OWF_OK);
    CHECK(oc_core_col_width(core, 2) == 0);
    go(core, "B1");
    key(core, OC_KEY_RIGHT, 0);
    CHECK_STR(active_name(core), "D1");
    oc_core_undo(core);
    CHECK(oc_core_col_width(core, 2) == 1500);
    oc_core_undo(core);
    CHECK(oc_core_col_width(core, 1) == 2000);
    oc_core_set_col_width(core, 0);                      /* the default again */
    CHECK(oc_core_col_width(core, 1) == 960);
    CHECK(oc_core_book(core)->sheets[0].ncols <= 1);     /* joined up */
    oc_core_free(core);
}

static void test_clipboard(void)
{
    oc_core *core = fresh();
    char *t;
    size_t n;
    owf_cellstyle st;
    if (!core)
        return;
    enter_at(core, "A1", "1");
    enter_at(core, "A2", "2");
    enter_at(core, "B1", "=A1*2");
    enter_at(core, "B2", "tab\there");
    go(core, "B1");
    oc_core_toggle_flags(core, OWF_BOLD);
    go(core, "A1:B2");
    t = oc_core_copy(core, &n);
    CHECK_STR(t, "1\t2\n2\t\"tab\there\"\n");
    CHECK(t && n == strlen(t));

    /* back in: its own text, so the cells with their formulas and styles */
    go(core, "D5");
    CHECK(oc_core_paste(core, t, n) == OWF_OK);
    CHECK_STR(formula(core, "E5"), "D5*2");
    CHECK(num(core, "E5") == 2);
    CHECK_STR(text(core, "E6"), "tab\there");
    go(core, "E5");
    oc_core_current_style(core, &st);
    CHECK(st.font.flags & OWF_BOLD);
    CHECK_STR(name_box(core), "E5");
    oc_core_undo(core);
    CHECK(empty(core, "E5") && empty(core, "D5"));

    /* a whole number of copies: filled */
    go(core, "A1");
    owf_free(t);
    t = oc_core_copy(core, &n);
    go(core, "H1:H3");
    CHECK(oc_core_paste(core, t, n) == OWF_OK);
    CHECK(num(core, "H1") == 1 && num(core, "H3") == 1);
    owf_free(t);

    /* text from elsewhere: tab separated values, typed */
    {
        const char *other = "a\tb\r\n1\t=1+1\n\"x\"\"y\"\t\"two\nlines\"\n";
        go(core, "A10");
        CHECK(oc_core_paste(core, other, strlen(other)) == OWF_OK);
        CHECK_STR(text(core, "A10"), "a");
        CHECK_STR(text(core, "B10"), "b");
        CHECK(num(core, "A11") == 1);
        CHECK_STR(formula(core, "B11"), "1+1");
        CHECK(num(core, "B11") == 2);
        CHECK_STR(text(core, "A12"), "x\"y");
        CHECK_STR(text(core, "B12"), "two\nlines");
        CHECK(empty(core, "A13"));
        CHECK_STR(name_box(core), "A10:B12");
    }

    /* cut, as Excel's: the cells stay until the paste moves them, and
       what read them follows (owf_calc_move_range) */
    go(core, "A1:B1");
    t = oc_core_cut(core, &n);
    CHECK_STR(t, "1\t2\n");
    CHECK(num(core, "A1") == 1 && num(core, "B1") == 2);
    go(core, "J1");
    CHECK(oc_core_paste(core, t, n) == OWF_OK);
    CHECK(num(core, "J1") == 1);
    CHECK_STR(formula(core, "K1"), "J1*2");             /* it read A1, which moved to J1 */
    CHECK(num(core, "K1") == 2);
    CHECK(empty(core, "A1") && empty(core, "B1"));
    CHECK_STR(name_box(core), "J1:K1");
    CHECK(oc_core_paste(core, t, n) == OWF_OK);         /* a cut pastes once: again, it is text */
    owf_free(t);
    oc_core_undo(core);
    oc_core_undo(core);
    CHECK(num(core, "A1") == 1);
    CHECK_STR(formula(core, "B1"), "A1*2");
    CHECK(empty(core, "J1"));
    /* a cut and then a change: the paste copies, the cells stay */
    go(core, "A1");
    t = oc_core_cut(core, &n);
    enter_at(core, "C30", "5");
    go(core, "L1");
    CHECK(oc_core_paste(core, t, n) == OWF_OK);
    CHECK(num(core, "A1") == 1 && num(core, "L1") == 1);
    owf_free(t);
    oc_core_undo(core);
    oc_core_undo(core);
    CHECK(empty(core, "C30") && empty(core, "L1"));

    /* empty cells pasted onto empty cells make none */
    {
        int before;
        enter_at(core, "B21", "1");
        go(core, "A20:C22");
        t = oc_core_copy(core, &n);
        go(core, "E30");
        before = oc_core_book(core)->sheets[0].ncells;
        CHECK(oc_core_paste(core, t, n) == OWF_OK);
        CHECK(oc_core_book(core)->sheets[0].ncells == before + 1);
        CHECK(num(core, "F31") == 1);
        owf_free(t);
    }

    /* typing: a paste goes in at the caret */
    go(core, "L1");
    type(core, "x");
    oc_core_paste(core, "yz\nmore", 7);
    CHECK_STR(oc_edit_text(core, NULL), "xyz");
    oc_edit_cancel(core);

    /* the selection's text without copying */
    go(core, "A10:B11");
    t = oc_core_selection_text(core, &n);
    CHECK_STR(t, "a\tb\n1\t2\n");
    owf_free(t);
    oc_core_free(core);
}

static void test_sheets(void)
{
    oc_core *core = fresh();
    owf_book *b;
    if (!core)
        return;
    enter_at(core, "A1", "5");
    CHECK(oc_core_add_sheet(core, NULL) == 1);
    b = oc_core_book(core);
    CHECK(b->nsheets == 2 && oc_core_sheet(core) == 1);
    CHECK_STR(b->sheets[1].name, "Sheet2");
    CHECK(oc_core_add_sheet(core, "sheet2") == -1);      /* the name is taken */
    enter_at(core, "A1", "=Sheet1!A1+1");
    enter_at(core, "A2", "=\"Sheet1!A1\"&Sheet1!B2");
    CHECK(oc_core_rename_sheet(core, 0, "Data") == OWF_OK);
    b = oc_core_book(core);
    CHECK_STR(b->sheets[0].name, "Data");
    CHECK_STR(formula(core, "A1"), "Data!A1+1");
    CHECK_STR(formula(core, "A2"), "\"Sheet1!A1\"&Data!B2");
    CHECK(oc_core_rename_sheet(core, 0, "My Data") == OWF_OK);
    CHECK_STR(formula(core, "A1"), "'My Data'!A1+1");
    CHECK(oc_core_rename_sheet(core, 0, "Bad:Name") == OWF_ERR_FORMAT);
    CHECK(oc_core_rename_sheet(core, 0, "") == OWF_ERR_FORMAT);
    oc_core_undo(core);
    CHECK_STR(formula(core, "A1"), "Data!A1+1");
    oc_core_undo(core);
    CHECK_STR(oc_core_book(core)->sheets[0].name, "Sheet1");
    CHECK(oc_core_sheet(core) == 1);
    /* each sheet keeps its cursor */
    go(core, "C7");
    oc_core_set_sheet(core, 0);
    go(core, "B2");
    oc_core_set_sheet(core, 1);
    CHECK_STR(active_name(core), "C7");
    /* delete, and back */
    CHECK(oc_core_delete_sheet(core, 0) == OWF_OK);
    b = oc_core_book(core);
    CHECK(b->nsheets == 1 && oc_core_sheet(core) == 0);
    CHECK_STR(b->sheets[0].name, "Sheet2");
    CHECK(oc_core_delete_sheet(core, 0) == OWF_ERR_UNSUPPORTED);   /* never the last */
    CHECK(oc_core_undo(core) == OWF_OK);
    b = oc_core_book(core);
    CHECK(b->nsheets == 2);
    CHECK_STR(b->sheets[0].name, "Sheet1");
    oc_core_set_sheet(core, 0);
    CHECK(num(core, "A1") == 5);
    CHECK_STR(active_name(core), "B2");
    oc_core_free(core);
}

/* ---- The draw list ---- */

static int count_kind(const oc_draw_list *l, int kind, int role)
{
    int i, n = 0;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == kind && (role < 0 || l->items[i].role == role))
            n++;
    return n;
}

static const oc_draw_item *text_of(const oc_draw_list *l, unsigned row, unsigned col)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_TEXT && l->items[i].row == row && l->items[i].col == col)
            return &l->items[i];
    return NULL;
}

/* A grid line down x that crosses y. */
static int vline_at(const oc_draw_list *l, int x, int y)
{
    int i;
    for (i = 0; i < l->n; i++) {
        const oc_draw_item *it = &l->items[i];
        if (it->kind == OC_DRAW_LINE && it->role == OC_ROLE_GRID && it->w == 0 && it->x == x && y >= it->y && y < it->y + it->h)
            return 1;
    }
    return 0;
}

static void test_draw(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    const oc_draw_item *it;
    oc_hit hit;
    int i, order_ok;
    if (!core)
        return;
    oc_draw_list_init(&l);
    oc_view_set_size(core, 400, 200);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    /* 64-pixel columns from 40: A to F show (F in part); 17-pixel rows from 18: 1 to 11 */
    CHECK(count_kind(&l, OC_DRAW_HEADER, -1) == 6 + 11);
    for (i = 0; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_HEADER && l.items[i].col == 5 && l.items[i].y == 0)
            CHECK_STR(l.items[i].text, "F");
    CHECK(count_kind(&l, OC_DRAW_FRAME, OC_ROLE_CURSOR) == 1);
    CHECK(count_kind(&l, OC_DRAW_TEXT, -1) == 0);

    enter_at(core, "B2", "42");
    enter_at(core, "A1", "Hello wide world");     /* 16 x 6 = 96 pixels: runs on into B1 */
    enter_at(core, "C3", "123456789012345");      /* doesn't fit: #### */
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = text_of(&l, 1, 1);
    CHECK(it && !strcmp(it->text, "42") && it->align == OC_ALIGN_RIGHT);
    CHECK(it && it->text_x == 104 + 64 - 3 - 12);   /* right: the cell's right less the pad less 12 pixels */
    it = text_of(&l, 0, 0);
    CHECK(it && it->align == OC_ALIGN_LEFT && it->clip_w > 64 && it->text_x == 40 + 3);
    CHECK(!vline_at(&l, 40 + 63, 20));                   /* no grid line where the text runs on */
    CHECK(vline_at(&l, 40 + 63, 40));                    /* but the row below has it */
    it = text_of(&l, 2, 2);
    CHECK(it && !strcmp(it->text, "1.235E+14"));         /* General: scientific, as Excel narrows it */
    go(core, "C3");
    oc_core_set_number_format(core, "#,##0.00");         /* a fixed format that doesn't fit: #### */
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = text_of(&l, 2, 2);
    CHECK(it && !strcmp(it->text, "#########"));
    /* a General number is fitted to the column: (64 - 6) / 6 = 9 characters, not #### */
    enter_at(core, "D2", "=1/3");
    enter_at(core, "D3", "=2/3");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = text_of(&l, 1, 3);
    CHECK(it && !strcmp(it->text, "0.3333333"));
    it = text_of(&l, 2, 3);
    CHECK(it && !strcmp(it->text, "0.6666667"));
    /* a value stops the run */
    enter_at(core, "B1", "x");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = text_of(&l, 0, 0);
    CHECK(it && it->clip_w <= 64);
    CHECK(vline_at(&l, 40 + 63, 20));

    /* the selection: filled, outlined, its headers marked; the active cell clear */
    go(core, "B2:C3");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(count_kind(&l, OC_DRAW_FILL, OC_ROLE_SELECTION) == 1);
    CHECK(count_kind(&l, OC_DRAW_FRAME, OC_ROLE_SELECTION) == 1);
    CHECK(count_kind(&l, OC_DRAW_HEADER, OC_ROLE_HEADER_SELECTED) == 4);
    /* in paint order: fills, then lines, then text, then headers, then frames */
    order_ok = 1;
    for (i = 1; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_FILL && l.items[i].role == OC_ROLE_SELECTION && l.items[i - 1].kind == OC_DRAW_TEXT)
            order_ok = 0;
    CHECK(order_ok);

    /* the cell being typed: its text and the caret */
    go(core, "D4");
    type(core, "abc");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(count_kind(&l, OC_DRAW_CARET, -1) == 1);
    CHECK(count_kind(&l, OC_DRAW_FILL, OC_ROLE_EDIT) == 1);
    for (i = 0; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_CARET)
            CHECK(l.items[i].x == 232 + 3 + 18);         /* D's left, the pad, 3 characters */
    key(core, OC_KEY_ESCAPE, 0);

    /* fills and borders */
    go(core, "E5");
    oc_core_set_fill(core, OWF_SHADE(0x00FF00));
    oc_core_set_borders(core, OC_BORDERS_ALL, OWF_LINE_MEDIUM, OWF_SHADE(0x0000FF));
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = 0, order_ok = 0; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_FILL && l.items[i].role == OC_ROLE_CELL_FILL && l.items[i].rgb == 0x00FF00)
            order_ok++;
    CHECK(order_ok == 1);
    for (i = 0, order_ok = 0; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_LINE && l.items[i].role == OC_ROLE_BORDER && l.items[i].rgb == 0x0000FF &&
            l.items[i].line == OWF_LINE_MEDIUM)
            order_ok++;
    CHECK(order_ok == 4);

    /* frozen panes: their lines; the frozen rows stay as the rest scroll */
    go(core, "B3");
    oc_core_freeze(core);
    oc_view_scroll_to(core, 50, 5);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(count_kind(&l, OC_DRAW_LINE, OC_ROLE_FREEZE) == 2);
    for (i = 0, order_ok = 0; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_HEADER && l.items[i].x == 0 && (!strcmp(l.items[i].text, "1") || !strcmp(l.items[i].text, "2") ||
                                                                      !strcmp(l.items[i].text, "51")))
            order_ok++;
    CHECK(order_ok == 3);
    oc_core_freeze(core);
    oc_view_scroll_to(core, 0, 0);

    /* merged cells: one text across them, no grid lines inside */
    owf_sheet_merge(&oc_core_book(core)->sheets[0], &(owf_range){ 7, 7, 0, 2 });
    enter_at(core, "A8", "merged");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = text_of(&l, 7, 0);
    CHECK(it && it->w == 3 * 64);
    CHECK(!vline_at(&l, 40 + 63, 18 + 7 * 17 + 5));
    go(core, "B8");
    CHECK_STR(active_name(core), "A8");                  /* a merge's first cell */
    key(core, OC_KEY_RIGHT, 0);
    CHECK_STR(active_name(core), "D8");                  /* out past its far side */

    /* the pointer */
    CHECK(oc_view_hit(core, 40 + 64 + 10, 18 + 17 * 2 + 5, &hit) == OC_HIT_CELL && hit.row == 2 && hit.col == 1);
    CHECK(oc_view_hit(core, 40 + 64 + 10, 5, &hit) == OC_HIT_COL_HEADER && hit.col == 1);
    CHECK(oc_view_hit(core, 40 + 63, 5, &hit) == OC_HIT_COL_EDGE && hit.col == 0);
    CHECK(oc_view_hit(core, 40 + 64 + 1, 5, &hit) == OC_HIT_COL_EDGE && hit.col == 0);
    CHECK(oc_view_hit(core, 10, 18 + 17 * 4 + 5, &hit) == OC_HIT_ROW_HEADER && hit.row == 4);
    CHECK(oc_view_hit(core, 5, 5, &hit) == OC_HIT_CORNER);
    oc_view_hit(core, 40 + 10, 18 + 5, &hit);
    oc_core_press(core, &hit, 0, 0);
    oc_view_hit(core, 40 + 64 * 2 + 10, 18 + 17 * 3 + 5, &hit);
    oc_core_drag(core, &hit);
    CHECK_STR(name_box(core), "A1:C4");
    oc_view_hit(core, 40 + 64 * 3 + 10, 5, &hit);
    oc_core_press(core, &hit, 0, 0);
    CHECK_STR(name_box(core), "D:D");
    oc_view_hit(core, 40 + 64 * 4 + 10, 5, &hit);
    oc_core_drag(core, &hit);
    CHECK_STR(name_box(core), "D:E");
    oc_view_hit(core, 40 + 64 + 10, 18 + 5, &hit);
    oc_core_press(core, &hit, 0, 1);                     /* Ctrl: a second range */
    {
        oc_selection sel;
        oc_core_selection(core, &sel);
        CHECK(sel.nranges == 2);
    }
    /* typing a formula, a click types the cell's name */
    go(core, "E10");
    type(core, "=");
    oc_view_hit(core, 40 + 10, 18 + 17 + 5, &hit);
    oc_core_press(core, &hit, 0, 0);
    CHECK_STR(oc_edit_text(core, NULL), "=A2");
    oc_view_hit(core, 40 + 64 + 10, 18 + 17 * 2 + 5, &hit);
    oc_core_drag(core, &hit);
    CHECK_STR(oc_edit_text(core, NULL), "=A2:B3");
    oc_edit_cancel(core);

    /* scrolling to a cell, and the scroller */
    go(core, "A1");
    oc_view_show_cell(core, 200, 0);
    {
        unsigned top, left, first, vis, total;
        oc_view_scroll_pos(core, &top, &left);
        CHECK(top <= 200 && top + 10 >= 200);
        oc_view_scroller(core, 0, &first, &vis, &total);
        CHECK(first == top && vis == 10 && total >= first + vis);
    }
    CHECK(oc_view_px_to_twips(core, 64) == 960);
    oc_view_set_zoom(core, 200);
    oc_view_scroll_to(core, 0, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(count_kind(&l, OC_DRAW_HEADER, -1) == 3 + 6);  /* twice as big: fewer show */
    oc_draw_list_free(&l);
    oc_core_free(core);
}

/* A cell given a style with these borders (left, right, top, bottom) in one colour. */
static void bordered(oc_core *core, const char *cell, int left, int right, int top, int bottom, unsigned long colour)
{
    owf_book *book = oc_core_book(core);
    owf_cellstyle st;
    owf_range r;
    owf_cell *c;
    int side;
    if (!oc_parse_range(cell, &r))
        return;
    owf_cellstyle_init(&st);
    st.border[OWF_SIDE_LEFT] = (unsigned char)left;
    st.border[OWF_SIDE_RIGHT] = (unsigned char)right;
    st.border[OWF_SIDE_TOP] = (unsigned char)top;
    st.border[OWF_SIDE_BOTTOM] = (unsigned char)bottom;
    for (side = 0; side < 4; side++)
        st.border_colour[side] = colour;
    c = owf_sheet_cell(&book->sheets[0], r.first_row, r.first_col, 1);
    if (c)
        c->style = owf_book_style(book, &st);
}

/* Border lines that cover pixel (x, y): how many, and the last one. */
static int borders_at(const oc_draw_list *l, int x, int y, const oc_draw_item **found)
{
    int i, n = 0;
    for (i = 0; i < l->n; i++) {
        const oc_draw_item *it = &l->items[i];
        if (it->kind != OC_DRAW_LINE || it->role != OC_ROLE_BORDER)
            continue;
        if ((it->w == 0 && it->x == x && y >= it->y && y < it->y + it->h) ||
            (it->h == 0 && it->y == y && x >= it->x && x < it->x + it->w)) {
            n++;
            if (found)
                *found = it;
        }
    }
    return n;
}

static void test_borders(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    const oc_draw_item *it = NULL;
    if (!core)
        return;
    oc_draw_list_init(&l);
    /* weights and dashes */
    CHECK(oc_line_weight(OWF_LINE_NONE) == 0 && oc_line_weight(OWF_LINE_THIN) == 1 && oc_line_weight(OWF_LINE_HAIR) == 1);
    CHECK(oc_line_weight(OWF_LINE_MEDIUM) == 2 && oc_line_weight(OWF_LINE_MEDIUM_DASHED) == 2);
    CHECK(oc_line_weight(OWF_LINE_THICK) == 3 && oc_line_weight(OWF_LINE_DOUBLE) == 3);
    CHECK(oc_line_weight(OWF_LINE_DASHED) == 1 && oc_line_weight(OWF_LINE_DOTTED) == 1);
    CHECK(oc_line_pattern(OWF_LINE_THIN) == 0xFFFF && oc_line_pattern(OWF_LINE_THICK) == 0xFFFF);
    CHECK(oc_line_pattern(OWF_LINE_DASHED) != 0xFFFF && oc_line_pattern(OWF_LINE_DOTTED) != 0xFFFF &&
          oc_line_pattern(OWF_LINE_HAIR) != 0xFFFF && oc_line_pattern(OWF_LINE_DASH_DOT) != 0xFFFF);
    CHECK(oc_line_pattern(OWF_LINE_DASHED) != oc_line_pattern(OWF_LINE_DOTTED));

    /* B2's right side and C2's left side are one edge, drawn once in the heavier line (C2's medium), in its colour */
    bordered(core, "B2", 0, OWF_LINE_THIN, 0, 0, OWF_SHADE(0x808080));
    bordered(core, "C2", OWF_LINE_MEDIUM, 0, 0, 0, OWF_SHADE(0x112233));
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(borders_at(&l, 40 + 2 * 64 - 1, 18 + 17 + 8, &it) == 1);
    CHECK(it && it->line == OWF_LINE_MEDIUM && it->rgb == 0x112233);
    CHECK(count_kind(&l, OC_DRAW_LINE, OC_ROLE_BORDER) == 1);
    /* the same lines on both sides: still once, in the file's colour (automatic when it gives none) */
    bordered(core, "B2", 0, OWF_LINE_THIN, 0, 0, OWF_SHADE(0x808080));
    bordered(core, "C2", OWF_LINE_THIN, 0, 0, 0, OWF_SHADE(0x808080));
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(borders_at(&l, 40 + 2 * 64 - 1, 18 + 17 + 8, &it) == 1 && it && it->rgb == 0x808080 && it->line == OWF_LINE_THIN);
    bordered(core, "B2", 0, OWF_LINE_THIN, 0, 0, 0);
    bordered(core, "C2", 0, 0, 0, 0, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(borders_at(&l, 40 + 2 * 64 - 1, 18 + 17 + 8, &it) == 1 && it && it->rgb == OWF_COLOUR_AUTO);
    bordered(core, "B2", 0, 0, 0, 0, 0);

    /* B4:D4 boxed, every cell with all four sides: the top and bottom are one line each across the three,
       and the three cells' four upright edges one each */
    bordered(core, "B4", OWF_LINE_THIN, OWF_LINE_THIN, OWF_LINE_THIN, OWF_LINE_THIN, 0);
    bordered(core, "C4", OWF_LINE_THIN, OWF_LINE_THIN, OWF_LINE_THIN, OWF_LINE_THIN, 0);
    bordered(core, "D4", OWF_LINE_THIN, OWF_LINE_THIN, OWF_LINE_THIN, OWF_LINE_THIN, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(count_kind(&l, OC_DRAW_LINE, OC_ROLE_BORDER) == 2 + 4);
    CHECK(borders_at(&l, 40 + 64 + 10, 18 + 3 * 17 - 1, &it) == 1 && it && it->x == 40 + 64 - 1 && it->w == 3 * 64 + 1);
    CHECK(borders_at(&l, 40 + 2 * 64 - 1, 18 + 3 * 17 + 8, NULL) == 1);
    /* merged, the lines inside it go: the outline stays */
    owf_sheet_merge(&oc_core_book(core)->sheets[0], &(owf_range){ 3, 3, 1, 3 });
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(borders_at(&l, 40 + 2 * 64 - 1, 18 + 3 * 17 + 8, NULL) == 0);
    CHECK(borders_at(&l, 40 + 3 * 64 - 1, 18 + 3 * 17 + 8, NULL) == 0);
    CHECK(borders_at(&l, 40 + 64 - 1, 18 + 3 * 17 + 8, NULL) == 1);
    CHECK(borders_at(&l, 40 + 4 * 64 - 1, 18 + 3 * 17 + 8, NULL) == 1);
    CHECK(count_kind(&l, OC_DRAW_LINE, OC_ROLE_BORDER) == 2 + 2);
    /* a dashed line under a row is one item, so its dashes run on */
    bordered(core, "B7", 0, 0, 0, OWF_LINE_DASHED, 0);
    bordered(core, "C7", 0, 0, 0, OWF_LINE_DASHED, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(borders_at(&l, 40 + 64 + 100, 18 + 7 * 17 - 1, &it) == 1 && it && it->line == OWF_LINE_DASHED && it->w == 2 * 64 + 1);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

/* The row header item of a row, or NULL. */
static const oc_draw_item *row_label(const oc_draw_list *l, const char *text)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_HEADER && l->items[i].x == 0 && l->items[i].text && !strcmp(l->items[i].text, text))
            return &l->items[i];
    return NULL;
}

static const oc_draw_item *col_label(const oc_draw_list *l, const char *text)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_HEADER && l->items[i].y == 0 && l->items[i].text && !strcmp(l->items[i].text, text))
            return &l->items[i];
    return NULL;
}

static void test_row_numbers(void)
{
    oc_core *core = fresh();                             /* 400 pixels high: 17-pixel rows from 18, 23 show */
    oc_draw_list l;
    const oc_draw_item *it;
    oc_hit hit;
    if (!core)
        return;
    oc_draw_list_init(&l);
    /* set: the width given, the numbers centred */
    CHECK(oc_view_row_header(core) == 40);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = row_label(&l, "1");
    CHECK(it && it->w == 40 && it->align == OC_ALIGN_CENTRE);
    /* following the rows: 7 pixels a digit, 8 beside them; rows 1 to 23 show, two digits: 22 */
    oc_view_set_row_numbers(core, 7, 8);
    CHECK(oc_view_row_header(core) == 8 + 2 * 7);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = row_label(&l, "1");
    CHECK(it && it->w == 22 && it->align == OC_ALIGN_LEFT && it->text_x == 4);
    it = col_label(&l, "A");
    CHECK(it && it->x == 22 && it->align == OC_ALIGN_CENTRE);
    CHECK(oc_view_hit(core, 23, 18 + 5, &hit) == OC_HIT_CELL && hit.row == 0 && hit.col == 0 && hit.x == 22);
    CHECK(oc_view_hit(core, 21, 18 + 5, &hit) == OC_HIT_ROW_HEADER && hit.w == 22);
    /* rows 90 to 112: three digits */
    oc_view_scroll_to(core, 89, 0);
    CHECK(oc_view_row_header(core) == 8 + 3 * 7);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = col_label(&l, "A");
    CHECK(it && it->x == 29);
    /* back to rows 70 to 92: two digits again */
    oc_view_scroll_to(core, 69, 0);
    CHECK(oc_view_row_header(core) == 8 + 2 * 7);
    /* 99 is the last of two digits: rows 78 to 100 make it three */
    oc_view_scroll_to(core, 77, 0);
    CHECK(oc_view_row_header(core) == 8 + 3 * 7);
    /* rows past ten thousand: five */
    oc_view_scroll_to(core, 9990, 0);
    CHECK(oc_view_row_header(core) == 8 + 5 * 7);
    /* the frozen rows show too, and the split follows the bar */
    oc_view_scroll_to(core, 0, 0);
    go(core, "B3");
    oc_core_freeze(core);
    oc_view_scroll_to(core, 999, 0);                     /* rows 1, 2, then 1000 on: four digits */
    CHECK(oc_view_row_header(core) == 8 + 4 * 7);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = row_label(&l, "1000");
    CHECK(it && it->w == 36);
    it = col_label(&l, "A");
    CHECK(it && it->x == 36);
    CHECK(count_kind(&l, OC_DRAW_LINE, OC_ROLE_FREEZE) == 2);
    {
        int i, ok = 0;
        for (i = 0; i < l.n; i++)
            if (l.items[i].kind == OC_DRAW_LINE && l.items[i].role == OC_ROLE_FREEZE && l.items[i].w == 0 &&
                l.items[i].x == 36 + 64 - 1)
                ok = 1;
        CHECK(ok);                                       /* beside column A, which moved with the bar */
    }
    /* a tall view showing a hundred rows from the top: three digits */
    oc_core_freeze(core);
    oc_view_scroll_to(core, 0, 0);
    oc_view_set_size(core, 640, 18 + 100 * 17);
    CHECK(oc_view_row_header(core) == 8 + 3 * 7);
    /* back to set */
    oc_view_set_row_numbers(core, 0, 0);
    CHECK(oc_view_row_header(core) == 40);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

/* A cell's text wrapped (or not) and placed: its style's wrap, vertical alignment and font size. */
static void wrapped(oc_core *core, const char *cell, int wrap, int valign, int size)
{
    owf_book *book = oc_core_book(core);
    owf_cellstyle st;
    owf_range r;
    owf_cell *c;
    if (!oc_parse_range(cell, &r))
        return;
    owf_cellstyle_init(&st);
    st.wrap = (unsigned char)wrap;
    st.valign = (unsigned char)valign;
    st.font.size = size;
    c = owf_sheet_cell(&book->sheets[0], r.first_row, r.first_col, 1);
    if (c)
        c->style = owf_book_style(book, &st);
}

/* The text items of a cell, in order: how many, and the k-th. */
static int texts_of(const oc_draw_list *l, unsigned row, unsigned col, int k, const oc_draw_item **found)
{
    int i, n = 0;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_TEXT && l->items[i].row == row && l->items[i].col == col) {
            if (n == k && found)
                *found = &l->items[i];
            n++;
        }
    return n;
}

static void test_text_layout(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    const oc_draw_item *it = NULL;
    int y2 = 18 + 17;                                    /* row 2's top */
    if (!core)
        return;
    oc_draw_list_init(&l);
    /* a cell font's pixels: 15 twips a pixel at 100%, 11 points 15 pixels, the default 10 points 13 */
    CHECK(oc_font_px(core, 220) == 15 && oc_font_px(core, 0) == 13 && oc_font_px(core, 285) == 19);
    CHECK(oc_line_px(core, 220) == 17 && oc_line_px(core, 0) == 15);   /* wrapped lines 1.15 em apart */
    oc_view_set_zoom(core, 200);
    CHECK(oc_font_px(core, 220) == 29);
    oc_view_set_zoom(core, 100);

    /* one line: the item is the row's box less its grid line, the font's size and the cell's alignment given */
    enter_at(core, "A2", "plain");
    wrapped(core, "A2", 0, OWF_VALIGN_BOTTOM, 220);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 0, 0, &it) == 1 && it && it->y == y2 && it->h == 16 && it->valign == OWF_VALIGN_BOTTOM &&
          it->size == 220);

    /* wrapped: 6 pixels a character, 64 less 2 x 3 of pad = 58, 9 characters a line; the word too long for
       a line is broken in it, the rest at spaces */
    oc_core_set_rows_height(core, 1, 1, 47 * 15);        /* 47 pixels: three 15-pixel lines */
    enter_at(core, "B2", "Software/Service Type");
    wrapped(core, "B2", 1, OWF_VALIGN_TOP, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 1, 0, &it) == 3 && it && !strcmp(it->text, "Software/") && it->y == y2 + 1 && it->h == 15);
    CHECK(texts_of(&l, 1, 1, 1, &it) == 3 && it && !strcmp(it->text, "Service") && it->y == y2 + 1 + 15);
    CHECK(texts_of(&l, 1, 1, 2, &it) == 3 && it && !strcmp(it->text, "Type") && it->y == y2 + 1 + 30 &&
          it->valign == OWF_VALIGN_CENTRE && it->clip_y == it->y && it->clip_w == 63);
    /* centred: the three lines' block in the middle; at the bottom, a pixel up from the grid line */
    wrapped(core, "B2", 1, OWF_VALIGN_CENTRE, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 1, 0, &it) == 3 && it && it->y == y2 + (46 - 45) / 2);
    /* a word wider than the cell breaks after its "/" (or "-"), not inside a part that fits */
    enter_at(core, "B2", "ab/cdefghijk");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 1, 0, &it) == 2 && it && !strcmp(it->text, "ab/"));
    CHECK(texts_of(&l, 1, 1, 1, &it) == 2 && it && !strcmp(it->text, "cdefghijk"));
    enter_at(core, "B2", "Cloud-hosted service");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 1, 0, &it) == 3 && it && !strcmp(it->text, "Cloud-"));
    CHECK(texts_of(&l, 1, 1, 1, &it) == 3 && it && !strcmp(it->text, "hosted"));
    /* none: where it must */
    enter_at(core, "B2", "abcdefghijkl");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 1, 0, &it) == 2 && it && !strcmp(it->text, "abcdefghi"));
    enter_at(core, "B2", "two several");
    wrapped(core, "B2", 1, OWF_VALIGN_BOTTOM, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 1, 1, &it) == 2 && it && !strcmp(it->text, "several") && it->y + it->h == y2 + 46 - 1);
    /* the text's own line breaks */
    enter_at(core, "C2", "a\nb\nc");
    wrapped(core, "C2", 1, OWF_VALIGN_TOP, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 2, 2, &it) == 3 && it && !strcmp(it->text, "c"));
    /* not wrapped, the first line shows */
    wrapped(core, "C2", 0, OWF_VALIGN_TOP, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 2, 0, &it) == 1 && it && !strcmp(it->text, "a"));

    /* lines that don't fit the row are left out: a 17-pixel row holds one */
    enter_at(core, "B4", "one two three");
    wrapped(core, "B4", 1, OWF_VALIGN_TOP, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 3, 1, 0, &it) == 1 && it && !strcmp(it->text, "one two"));
    wrapped(core, "B4", 1, OWF_VALIGN_BOTTOM, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 3, 1, 0, &it) == 1 && it && !strcmp(it->text, "three"));

    /* a merged range's text is placed in all of it, wrapped across its width */
    owf_sheet_merge(&oc_core_book(core)->sheets[0], &(owf_range){ 5, 6, 0, 1 });
    enter_at(core, "A6", "merged and wrapped text");
    wrapped(core, "A6", 1, OWF_VALIGN_CENTRE, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 5, 0, 0, &it) == 2 && it && it->w == 128 && it->clip_w == 127);
    CHECK(texts_of(&l, 5, 0, 1, &it) == 2 && it && it->y == y2 + 3 * 17 + 47 + (2 * 17 - 1 - 30) / 2 + 15);
    /* not wrapped: one item, the merge's box */
    enter_at(core, "A6", "merged");
    wrapped(core, "A6", 0, OWF_VALIGN_CENTRE, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 5, 0, 0, &it) == 1 && it && it->y == y2 + 47 + 3 * 17 && it->h == 2 * 17 - 1);
    /* an empty merged cell adds nothing (and moves no other text) */
    owf_sheet_merge(&oc_core_book(core)->sheets[0], &(owf_range){ 9, 9, 0, 1 });
    enter_at(core, "A10", "");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(texts_of(&l, 1, 0, 0, &it) == 1 && it && it->y == y2);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

/* The sheet tabs a strip shows: eight sheets, as Dale's workbook has. */
static void test_tab_strip(void)
{
    int w[8] = { 150, 90, 60, 100, 100, 80, 120, 70 }, first = 0, scroll = 0, end, k, sum;
    /* every tab fits: no arrows, from the first */
    first = 3;
    end = oc_tab_strip(w, 8, 1000, 40, 0, &first, &scroll);
    CHECK(end == 8 && first == 0 && !scroll);
    /* only some fit (400 less 40 of arrows: 150 + 90 + 60 = 300, the 100 after doesn't): arrows */
    first = 0;
    end = oc_tab_strip(w, 8, 400, 40, 0, &first, &scroll);
    CHECK(scroll && first == 0 && end == 3);
    /* the sixth sheet chosen: the strip moves on until its tab shows, and no further */
    end = oc_tab_strip(w, 8, 400, 40, 5, &first, &scroll);
    CHECK(first <= 5 && end > 5);
    for (k = first, sum = 0; k < end; k++)
        sum += w[k];
    CHECK(sum <= 360 && sum + w[end < 8 ? end : 7] > 360 - (end < 8 ? 0 : 1000));
    /* back to the first sheet: it moves back */
    end = oc_tab_strip(w, 8, 400, 40, 0, &first, &scroll);
    CHECK(first == 0 && end == 3);
    /* the arrows scroll it with no sheet to bring in (current -1): it stays where they put it, and doesn't
       leave room at the end (the last tabs fill the strip) */
    first = 7;
    end = oc_tab_strip(w, 8, 400, 40, -1, &first, &scroll);
    CHECK(end == 8 && first == 5);                       /* 80 + 120 + 70 = 270; with 100 more it's 370 > 360 */
    first = 2;
    end = oc_tab_strip(w, 8, 400, 40, -1, &first, &scroll);
    CHECK(first == 2 && end == 6);                       /* 60 + 100 + 100 + 80 = 340; 120 more is 460 */
    /* hidden sheets (0) have no tab and take no room */
    w[1] = 0;
    w[2] = 0;
    first = 0;
    end = oc_tab_strip(w, 8, 400, 40, 0, &first, &scroll);
    CHECK(first == 0 && end == 5);                       /* 150 + 100 + 100 = 350; 80 more is 430 */
    /* one tab wider than the strip: it shows all the same (cut short by the window) */
    w[0] = 900;
    first = 0;
    end = oc_tab_strip(w, 8, 400, 40, 0, &first, &scroll);
    CHECK(first == 0 && end == 3);                       /* sheets 1 and 2 hidden, then sheet 3's doesn't fit */
    end = oc_tab_strip(w, 8, 400, 40, 7, &first, &scroll);
    CHECK(end == 8 && first > 0);
    /* no sheets at all */
    end = oc_tab_strip(w, 0, 400, 40, 0, &first, &scroll);
    CHECK(end == 0 && first == 0 && !scroll);
}

/* A conditional format on the first sheet over one range ("A1:A5"). */
static owf_condfmt *rule_on(oc_core *core, const char *range, int type, int priority)
{
    owf_condfmt *cf = owf_sheet_add_condfmt(&oc_core_book(core)->sheets[0]);
    owf_range r;
    if (!cf || !oc_parse_range(range, &r) || !(cf->ranges = malloc(sizeof *cf->ranges)))
        return cf;
    cf->ranges[0] = r;
    cf->nranges = 1;
    cf->type = type;
    cf->priority = priority;
    return cf;
}

static char *dup_text(const char *s)
{
    char *d = malloc(strlen(s) + 1);
    if (d)
        strcpy(d, s);
    return d;
}

/* The fill drawn on a cell (its own: not the selection's), or -1. */
static long fill_of(const oc_draw_list *l, unsigned row, unsigned col)
{
    int i;
    long found = -1;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_FILL && l->items[i].role == OC_ROLE_CELL_FILL && l->items[i].row == row &&
            l->items[i].col == col)
            found = (long)l->items[i].rgb;
    return found;
}

static void test_condfmt(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    owf_condfmt *cf;
    const oc_draw_item *it;
    if (!core)
        return;
    oc_draw_list_init(&l);
    enter_at(core, "A1", "5");
    enter_at(core, "A2", "15");
    enter_at(core, "A3", "25");
    enter_at(core, "A4", "x");
    enter_at(core, "B1", "Yes");
    enter_at(core, "B2", "No");
    enter_at(core, "C1", "0");
    enter_at(core, "C2", "50");
    enter_at(core, "C3", "100");
    go(core, "E9");
    /* cellIs greater than 10: a red fill; text is greater than any number, as in Excel */
    cf = rule_on(core, "A1:A5", OWF_CF_CELL_IS, 2);
    CHECK(cf != NULL);
    cf->op = OWF_CF_GREATER;
    cf->formula[0] = dup_text("10");
    cf->fill = OWF_SHADE(0xFF0000);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 0, 0) == -1 && fill_of(&l, 1, 0) == 0xFF0000 && fill_of(&l, 2, 0) == 0xFF0000);
    CHECK(fill_of(&l, 3, 0) == 0xFF0000 && fill_of(&l, 4, 0) == -1);
    /* a rule before it (priority 1) that holds for 25 and stops: green there, the red rule left out */
    cf = rule_on(core, "A1:A5", OWF_CF_CELL_IS, 1);
    cf->op = OWF_CF_EQUAL;
    cf->formula[0] = dup_text("25");
    cf->fill = OWF_SHADE(0x00FF00);
    cf->font_set = OWF_BOLD;
    cf->font_flags = OWF_BOLD;
    cf->stop = 1;
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 2, 0) == 0x00FF00 && fill_of(&l, 1, 0) == 0xFF0000);
    it = text_of(&l, 2, 0);
    CHECK(it && (it->flags & OWF_BOLD));
    it = text_of(&l, 1, 0);
    CHECK(it && !(it->flags & OWF_BOLD));
    /* an expression moved down the rows with its relative row, its column fixed: grey italic text where B is "No" */
    cf = rule_on(core, "A1:C5", OWF_CF_EXPRESSION, 3);
    cf->formula[0] = dup_text("$B1=\"No\"");
    cf->font_colour = OWF_SHADE(0x808080);
    cf->font_set = OWF_ITALIC;
    cf->font_flags = OWF_ITALIC;
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    it = text_of(&l, 1, 2);
    CHECK(it && it->rgb == 0x808080 && (it->flags & OWF_ITALIC));
    it = text_of(&l, 0, 2);
    CHECK(it && it->rgb != 0x808080 && !(it->flags & OWF_ITALIC));
    it = text_of(&l, 1, 0);                             /* the red rule's fill stays; the italic is added */
    CHECK(it && (it->flags & OWF_ITALIC) && fill_of(&l, 1, 0) == 0xFF0000);
    /* between, its bounds either way round */
    cf = rule_on(core, "C1:C3", OWF_CF_CELL_IS, 4);
    cf->op = OWF_CF_BETWEEN;
    cf->formula[0] = dup_text("60");
    cf->formula[1] = dup_text("40");
    cf->fill = OWF_SHADE(0x0000FF);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 1, 2) == 0x0000FF && fill_of(&l, 0, 2) == -1 && fill_of(&l, 2, 2) == -1);
    /* a colour scale from white (the lowest) to blue (the highest), after it: the middle one half way */
    cf = rule_on(core, "C1:C3", OWF_CF_COLOUR_SCALE, 5);
    cf->npoints = 2;
    cf->points[0].kind = OWF_CFV_MIN;
    cf->points[0].colour = OWF_SHADE(0xFFFFFF);
    cf->points[1].kind = OWF_CFV_MAX;
    cf->points[1].colour = OWF_SHADE(0x0000FF);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 0, 2) == 0xFFFFFF && fill_of(&l, 2, 2) == 0x0000FF);
    CHECK(fill_of(&l, 1, 2) == 0x0000FF);               /* the between rule came first */
    /* a kept rule ("contains text") is shown by its formula */
    cf = rule_on(core, "B1:B5", OWF_CF_KEPT, 6);
    cf->formula[0] = dup_text("NOT(ISERROR(SEARCH(\"es\",B1)))");
    cf->xml = dup_text("<cfRule type=\"containsText\" operator=\"containsText\" text=\"es\"></cfRule>");
    cf->fill = OWF_SHADE(0xFFFF00);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 0, 1) == 0xFFFF00 && fill_of(&l, 1, 1) == -1);
    /* a change shows: A1 made 30 gets the red fill */
    enter_at(core, "A1", "30");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 0, 0) == 0xFF0000);
    /* a row inserted above moves the ranges and the formulas */
    go(core, "A1");
    CHECK(oc_core_insert(core, 0) == OWF_OK);
    {
        const owf_sheet *sh = &oc_core_book(core)->sheets[0];
        CHECK(sh->ncondfmts == 6 && sh->condfmts[0].ranges[0].first_row == 1 && sh->condfmts[0].ranges[0].last_row == 5);
        CHECK(sh->condfmts[2].formula[0] && !strcmp(sh->condfmts[2].formula[0], "$B2=\"No\""));
    }
    go(core, "E9");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 1, 0) == 0xFF0000 && fill_of(&l, 0, 0) == -1);
    it = text_of(&l, 2, 2);
    CHECK(it && it->rgb == 0x808080);
    /* a sheet deleted and the deletion undone: its rules come back with it */
    oc_core_add_sheet(core, "Two");
    CHECK(oc_core_delete_sheet(core, 0) == OWF_OK);
    CHECK(oc_core_book(core)->sheets[0].ncondfmts == 0);
    CHECK(oc_core_undo(core) == OWF_OK);
    CHECK(oc_core_book(core)->nsheets == 2 && oc_core_book(core)->sheets[0].ncondfmts == 6);
    CHECK(oc_core_book(core)->sheets[0].condfmts[5].xml != NULL);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

/* Made-up faces for oc_font_size_for_caps: the capitals' height at each size, and the sizes asked for. */
typedef struct {
    int kind;                    /* 0 capitals = size; 1 two thirds of it; 2 never made; 3 bitmap sizes only */
    int asked[16], nasked;
} fake_face;

static int fake_caps(void *user, int size)
{
    fake_face *f = (fake_face *)user;
    if (f->nasked < 16)
        f->asked[f->nasked++] = size;
    switch (f->kind) {
    case 0: return size;
    case 1: return size * 2 / 3;
    case 2: return 0;
    default:                     /* 11, 13 and 15 pixels made, capitals 8, 9 and 11: the nearest */
        return size <= 12 ? 8 : size <= 14 ? 9 : 11;
    }
}

static void test_font_caps(void)
{
    fake_face f;
    int cap, size, i, j, twice = 0;
    /* capitals as tall as the size (openfont's DejaVu Sans): 11 points' 15-pixel em made at 11 */
    memset(&f, 0, sizeof f);
    size = oc_font_size_for_caps(15, fake_caps, &f, &cap);
    CHECK(size == 11 && cap == 11 && f.nasked == 2);
    /* two thirds (the lab's CGTriumvirate at 110%: 16 pixels gave capitals of 10, the guess 18 gives 12):
       a step from the nearest finds 17, capitals 11 */
    memset(&f, 0, sizeof f);
    f.kind = 1;
    size = oc_font_size_for_caps(16, fake_caps, &f, &cap);
    CHECK(size == 17 && cap == 11);
    for (i = 0; i < f.nasked; i++)
        for (j = 0; j < i; j++)
            twice |= f.asked[i] == f.asked[j];
    CHECK(!twice && f.nasked <= 1 + OC_CAP_TRIES);
    /* 18 pixels want 13: the guess in proportion, 20, has them */
    memset(&f, 0, sizeof f);
    f.kind = 1;
    size = oc_font_size_for_caps(18, fake_caps, &f, &cap);
    CHECK(size == 20 && cap == 13 && f.nasked == 2);
    /* already right: asked once */
    memset(&f, 0, sizeof f);
    size = oc_font_size_for_caps(11, fake_caps, &f, &cap);   /* 11 wants 8; capitals = size */
    CHECK(size == 8 && cap == 8);
    memset(&f, 0, sizeof f);
    f.kind = 1;
    size = oc_font_size_for_caps(25, fake_caps, &f, &cap);   /* 25 wants 18: two thirds of 27 */
    CHECK(cap == 18 && (size == 27 || size == 28) && f.nasked == 2);
    memset(&f, 0, sizeof f);
    f.kind = 0;
    size = oc_font_size_for_caps(7, fake_caps, &f, &cap);    /* 7 wants 5 */
    CHECK(size == 5 && cap == 5);
    /* a face that is never made: the size asked, no capitals */
    memset(&f, 0, sizeof f);
    f.kind = 2;
    size = oc_font_size_for_caps(15, fake_caps, &f, &cap);
    CHECK(size == 15 && cap == 0 && f.nasked == 1);
    /* a bitmap face's sizes: the nearest kept, the tries bounded, none twice */
    memset(&f, 0, sizeof f);
    f.kind = 3;
    size = oc_font_size_for_caps(20, fake_caps, &f, &cap);   /* wants 14: 11 is the nearest it has */
    CHECK(cap == 11 && f.nasked <= 1 + OC_CAP_TRIES);
    for (i = 0, twice = 0; i < f.nasked; i++)
        for (j = 0; j < i; j++)
            twice |= f.asked[i] == f.asked[j];
    CHECK(!twice);
}

static void test_comments(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    owf_sheet *sh;
    int i, marks, notes, texts;
    if (!core)
        return;
    oc_draw_list_init(&l);
    sh = &oc_core_book(core)->sheets[0];
    enter_at(core, "B2", "commented");
    CHECK(owf_sheet_set_comment(sh, 1, 1, "Team 5", "Owner to confirm. The supplier hosts it.") == 0);
    CHECK(owf_sheet_set_comment(sh, 4, 2, NULL, "C5's") == 0);
    CHECK(owf_sheet_set_comment(sh, 1, 1, "Team 5", "Owner to confirm. The supplier hosts it, as before.") == 0);
    CHECK(sh->ncomments == 2 && oc_core_comment(core, 0, 1, 1) && !oc_core_comment(core, 0, 1, 2));
    /* each commented cell has its mark in the top right corner; no note while another cell is active */
    go(core, "E9");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = marks = notes = 0; i < l.n; i++) {
        const oc_draw_item *it = &l.items[i];
        if (it->kind == OC_DRAW_MARK) {
            marks++;
            if (it->row == 1 && it->col == 1)
                CHECK(it->x + it->w == 40 + 2 * 64 - 1 && it->y == 18 + 17 && it->w == 5 && it->h == 5);
        }
        notes += it->kind == OC_DRAW_FILL && it->role == OC_ROLE_NOTE;
    }
    CHECK(marks == 2 && notes == 0);
    /* the active cell's comment beside it: a box right of the cell, its author in bold, the text wrapped,
       after everything else */
    go(core, "B2");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = notes = texts = 0; i < l.n; i++) {
        const oc_draw_item *it = &l.items[i];
        if (it->kind == OC_DRAW_FILL && it->role == OC_ROLE_NOTE) {
            notes++;
            CHECK(it->x == 40 + 2 * 64 + 6 && it->y == 18 + 17 && it->w == 160);
        }
        if (notes && it->kind == OC_DRAW_TEXT) {
            if (!texts)
                CHECK(it->text && !strcmp(it->text, "Team 5") && (it->flags & OWF_BOLD));
            texts++;
        }
    }
    CHECK(notes == 1 && texts >= 3);                    /* the author, then at least two lines of text */
    CHECK(l.items[l.n - 1].kind == OC_DRAW_TEXT && l.items[l.n - 1].row == 1);
    /* not while typing */
    type(core, "x");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = notes = 0; i < l.n; i++)
        notes += l.items[i].kind == OC_DRAW_FILL && l.items[i].role == OC_ROLE_NOTE;
    CHECK(notes == 0);
    key(core, OC_KEY_ESCAPE, 0);
    /* rows inserted above move it; a deleted row takes its comment */
    go(core, "A1");
    CHECK(oc_core_insert(core, 0) == OWF_OK);
    CHECK(oc_core_comment(core, 0, 2, 1) && !oc_core_comment(core, 0, 1, 1));
    go(core, "A6");
    CHECK(oc_core_delete(core, 0) == OWF_OK);
    CHECK(oc_core_book(core)->sheets[0].ncomments == 1);
    /* a sheet deleted and the deletion undone: its comments come back */
    oc_core_add_sheet(core, "Two");
    CHECK(oc_core_delete_sheet(core, 0) == OWF_OK);
    CHECK(oc_core_undo(core) == OWF_OK);
    CHECK(oc_core_comment(core, 0, 2, 1) && !strcmp(oc_core_comment(core, 0, 2, 1)->author, "Team 5"));
    /* taken away */
    CHECK(owf_sheet_set_comment(&oc_core_book(core)->sheets[0], 2, 1, NULL, NULL) == 0);
    CHECK(oc_core_book(core)->sheets[0].ncomments == 0);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

static void test_validation(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    owf_sheet *sh;
    owf_validation *dv;
    int i, notes, texts;
    if (!core)
        return;
    oc_draw_list_init(&l);
    sh = &oc_core_book(core)->sheets[0];
    dv = owf_sheet_add_validation(sh);
    CHECK(dv != NULL);
    dv->ranges = malloc(sizeof *dv->ranges);
    dv->ranges[0] = (owf_range){ 1, 5, 1, 1 };            /* B2:B6 */
    dv->nranges = 1;
    dv->type = OWF_DV_LIST;
    dv->formula[0] = dup_text("\"Yes,No\"");
    dv->prompt_title = dup_text("Hosted");
    dv->prompt = dup_text("Hosted by us?");
    CHECK(owf_sheet_validation(sh, 3, 1) == dv && !owf_sheet_validation(sh, 6, 1) && !owf_sheet_validation(sh, 3, 2));
    /* its input message shows only when the file says to show it */
    go(core, "B3");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = notes = 0; i < l.n; i++)
        notes += l.items[i].kind == OC_DRAW_FILL && l.items[i].role == OC_ROLE_NOTE;
    CHECK(notes == 0);
    dv->show_prompt = 1;
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = notes = texts = 0; i < l.n; i++) {
        notes += l.items[i].kind == OC_DRAW_FILL && l.items[i].role == OC_ROLE_NOTE;
        if (notes && l.items[i].kind == OC_DRAW_TEXT) {
            if (!texts)
                CHECK(!strcmp(l.items[i].text, "Hosted") && (l.items[i].flags & OWF_BOLD));
            else
                CHECK(!strcmp(l.items[i].text, "Hosted by us?"));
            texts++;
        }
    }
    CHECK(notes == 1 && texts == 2);
    /* a comment on the cell shows instead */
    owf_sheet_set_comment(sh, 2, 1, NULL, "a comment");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(l.items[l.n - 1].kind == OC_DRAW_TEXT && !strcmp(l.items[l.n - 1].text, "a comment"));
    /* rows inserted move it; a sheet deleted and undone keeps it */
    go(core, "A1");
    CHECK(oc_core_insert(core, 0) == OWF_OK);
    CHECK(sh == &oc_core_book(core)->sheets[0] && sh->validations[0].ranges[0].first_row == 2 &&
          sh->validations[0].ranges[0].last_row == 6);
    oc_core_add_sheet(core, "Two");
    CHECK(oc_core_delete_sheet(core, 0) == OWF_OK);
    CHECK(oc_core_undo(core) == OWF_OK);
    sh = &oc_core_book(core)->sheets[0];
    CHECK(sh->nvalidations == 1 && sh->validations[0].formula[0] && !strcmp(sh->validations[0].formula[0], "\"Yes,No\"") &&
          sh->validations[0].prompt && !strcmp(sh->validations[0].prompt, "Hosted by us?"));
    /* its rows deleted, it goes */
    oc_core_set_sheet(core, 0);
    go(core, "A3:A7");
    oc_core_select_rows(core);
    CHECK(oc_core_delete(core, 0) == OWF_OK);
    CHECK(oc_core_book(core)->sheets[0].nvalidations == 0);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

static void test_autofilter(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    owf_sheet *sh;
    int i, buttons, filtered;
    if (!core)
        return;
    oc_draw_list_init(&l);
    sh = &oc_core_book(core)->sheets[0];
    sh->has_autofilter = 1;
    sh->autofilter = (owf_range){ 1, 20, 1, 3 };         /* B2:D21 */
    sh->autofilter_xml = dup_text("<filterColumn colId=\"2\"><filters><filter val=\"Yes\"/></filters></filterColumn>");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = buttons = filtered = 0; i < l.n; i++) {
        const oc_draw_item *it = &l.items[i];
        if (it->kind != OC_DRAW_FILTER)
            continue;
        buttons++;
        CHECK(it->row == 1 && it->w == it->h && it->w == 14);   /* a 17-pixel row: 14 */
        CHECK(it->x + it->w == 40 + (int)(it->col + 1) * 64 - 2 && it->y + it->h == 18 + 2 * 17 - 2);
        if (it->flags) {
            filtered++;
            CHECK(it->col == 3);                         /* colId 2 of B:D */
        }
    }
    CHECK(buttons == 3 && filtered == 1);
    /* a column inserted before it moves it; rows deleted under it shrink it */
    go(core, "A1");
    CHECK(oc_core_insert(core, 1) == OWF_OK);
    sh = &oc_core_book(core)->sheets[0];
    CHECK(sh->has_autofilter && sh->autofilter.first_col == 2 && sh->autofilter.last_col == 4);
    go(core, "A10:A12");
    oc_core_select_rows(core);
    CHECK(oc_core_delete(core, 0) == OWF_OK);
    CHECK(sh->autofilter.last_row == 17);
    /* a sheet deleted and undone keeps it */
    oc_core_add_sheet(core, "Two");
    CHECK(oc_core_delete_sheet(core, 0) == OWF_OK);
    CHECK(oc_core_undo(core) == OWF_OK);
    sh = &oc_core_book(core)->sheets[0];
    CHECK(sh->has_autofilter && sh->autofilter_xml && strstr(sh->autofilter_xml, "colId=\"2\""));
    /* all its rows deleted, it goes */
    oc_core_set_sheet(core, 0);
    go(core, "A2:A18");
    oc_core_select_rows(core);
    CHECK(oc_core_delete(core, 0) == OWF_OK);
    CHECK(!oc_core_book(core)->sheets[0].has_autofilter && !oc_core_book(core)->sheets[0].autofilter_xml);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

/* Whether a saved file (a zip) holds a part. */
static int saved_has(const char *path, const char *part)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data = NULL;
    long n = 0;
    int has = 0;
    owf_unzip zip;
    if (f && fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = malloc((size_t)n)) &&
        fread(data, 1, (size_t)n, f) == (size_t)n && owf_unzip_open(&zip, data, (size_t)n))
        has = owf_unzip_has(&zip, part);
    if (f)
        fclose(f);
    free(data);
    return has;
}

/* A sheet's pictures and charts kept as they were (the book's kept parts
 * and the sheet's drawing): through a change and its undo, a sheet
 * deleted and brought back, and saving; a deleted sheet's not written. */
static void test_drawings_kept(void)
{
    static const char *const path = "build/host/calc-core-drawing.xlsx";
    oc_core *core = fresh();
    owf_report *report = owf_report_new();
    const owf_book_format *used = NULL;
    const owf_book *book;
    const owf_part *p;
    if (!core || !report) {
        CHECK(0);
        oc_core_free(core);
        owf_report_free(report);
        return;
    }
    CHECK(oc_core_open(core, "tests/book/fixtures/features-openpyxl.xlsx", NULL, report, &used) == OWF_OK);
    book = oc_core_book(core);
    CHECK(book->nsheets == 8 && book->nparts == 3);
    CHECK(book->sheets[7].drawing && !strcmp(book->sheets[7].drawing, "xl/drawings/drawing1.xml"));
    p = owf_book_part(book, "xl/charts/chart1.xml");
    CHECK(p && p->length > 100 && !strcmp(p->type, "application/vnd.openxmlformats-officedocument.drawingml.chart+xml"));
    /* a change on its sheet, and its undo */
    oc_core_set_sheet(core, 7);
    enter_at(core, "A10", "5");
    CHECK(oc_core_undo(core) == OWF_OK);
    book = oc_core_book(core);
    CHECK(book->nparts == 3 && book->sheets[7].drawing && owf_book_part(book, "xl/drawings/_rels/drawing1.xml.rels"));
    /* the sheet deleted, then brought back */
    CHECK(oc_core_delete_sheet(core, 7) == OWF_OK);
    CHECK(oc_core_book(core)->nsheets == 7);
    CHECK(oc_core_undo(core) == OWF_OK);
    book = oc_core_book(core);
    CHECK(book->nsheets == 8 && book->sheets[7].drawing && !strcmp(book->sheets[7].drawing, "xl/drawings/drawing1.xml"));
    p = owf_book_part(book, "xl/charts/chart1.xml");
    CHECK(p && p->length > 100);
    /* saved: the drawing and its chart go with the sheet */
    CHECK(oc_core_save(core, path, "xlsx", report) == OWF_OK);
    CHECK(saved_has(path, "xl/drawings/drawing1.xml") && saved_has(path, "xl/charts/chart1.xml") &&
          saved_has(path, "xl/worksheets/_rels/sheet8.xml.rels"));
    /* the sheet deleted for good: none of it written */
    CHECK(oc_core_delete_sheet(core, 7) == OWF_OK);
    CHECK(oc_core_save(core, path, "xlsx", report) == OWF_OK);
    CHECK(saved_has(path, "xl/worksheets/sheet7.xml") && !saved_has(path, "xl/drawings/drawing1.xml") &&
          !saved_has(path, "xl/charts/chart1.xml"));
    remove(path);
    owf_report_free(report);
    oc_core_free(core);
}

/* ---- Charts ---- */

#define XDR "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"
#define DML "http://schemas.openxmlformats.org/drawingml/2006/main"
#define CML "http://schemas.openxmlformats.org/drawingml/2006/chart"
#define RNS "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

/* A chart on sheet 0 from B2 to F12 (columns 1 to 5, rows 1 to 11), as Excel keeps one: its drawing, the
 * drawing's relationships and the chart, whose plot area is `plot`. */
static void put_chart(oc_core *core, const char *plot, const char *more)
{
    static const char drawing[] =
        "<xdr:wsDr xmlns:xdr=\"" XDR "\" xmlns:a=\"" DML "\"><xdr:twoCellAnchor>"
        "<xdr:from><xdr:col>1</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>1</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>"
        "<xdr:to><xdr:col>5</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>11</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>"
        "<xdr:graphicFrame macro=\"\"><xdr:nvGraphicFramePr><xdr:cNvPr id=\"2\" name=\"Chart 1\"/><xdr:cNvGraphicFramePr/>"
        "</xdr:nvGraphicFramePr><xdr:xfrm/><a:graphic><a:graphicData uri=\"" CML "\"><c:chart xmlns:c=\"" CML "\" "
        "xmlns:r=\"" RNS "\" r:id=\"rId1\"/></a:graphicData></a:graphic></xdr:graphicFrame><xdr:clientData/>"
        "</xdr:twoCellAnchor></xdr:wsDr>";
    static const char rels[] =
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" "
        "Type=\"" RNS "/chart\" Target=\"../charts/chart1.xml\"/></Relationships>";
    char chart[4096];
    owf_book *book = oc_core_book(core);
    snprintf(chart, sizeof chart,
             "<c:chartSpace xmlns:c=\"" CML "\" xmlns:a=\"" DML "\"><c:chart><c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r>"
             "<a:t>Sales</a:t></a:r></a:p></c:rich></c:tx></c:title><c:plotArea>%s<c:valAx><c:axId val=\"2\"/>"
             "<c:majorGridlines/></c:valAx></c:plotArea>%s</c:chart></c:chartSpace>", plot, more);
    CHECK(owf_book_keep_part(book, "xl/drawings/drawing1.xml", "application/vnd.openxmlformats-officedocument.drawing+xml",
                             drawing, strlen(drawing)) == 0);
    CHECK(owf_book_keep_part(book, "xl/drawings/_rels/drawing1.xml.rels",
                             "application/vnd.openxmlformats-package.relationships+xml", rels, strlen(rels)) == 0);
    CHECK(owf_book_keep_part(book, "xl/charts/chart1.xml", "application/vnd.openxmlformats-officedocument.drawingml.chart+xml",
                             chart, strlen(chart)) == 0);
    book->sheets[0].drawing = dup_text("xl/drawings/drawing1.xml");
}

#define SERIES(n, name, col, cache)                                                                             \
    "<c:ser><c:idx val=\"" #n "\"/><c:tx><c:strRef><c:f>Sheet1!$" col "$1</c:f><c:strCache><c:pt idx=\"0\"><c:v>" name \
    "</c:v></c:pt></c:strCache></c:strRef></c:tx><c:cat><c:strRef><c:f>Sheet1!$A$2:$A$4</c:f></c:strRef></c:cat>"     \
    "<c:val><c:numRef><c:f>Sheet1!$" col "$2:$" col "$4</c:f>" cache "</c:numRef></c:val></c:ser>"

/* The chart's items of a kind (role OC_ROLE_CHART), and of a colour (rgb, or any: -1). */
static int chart_items(const oc_draw_list *l, int kind, long rgb)
{
    int i, n = 0;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == kind && l->items[i].role == OC_ROLE_CHART && (rgb < 0 || l->items[i].rgb == (unsigned long)rgb))
            n++;
    return n;
}

/* The tallest (the widest, `across`) fill of a colour. */
static int biggest(const oc_draw_list *l, unsigned long rgb, int across)
{
    int i, most = 0;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_FILL && l->items[i].role == OC_ROLE_CHART && l->items[i].rgb == rgb &&
            (across ? l->items[i].w : l->items[i].h) > most)
            most = across ? l->items[i].w : l->items[i].h;
    return most;
}

/* The first chart polygon of a colour, and a polygon's area (the shoelace). */
static const oc_draw_item *poly_of(const oc_draw_list *l, unsigned long rgb)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_POLY && l->items[i].rgb == rgb && l->items[i].points)
            return &l->items[i];
    return NULL;
}

static double poly_area(const oc_draw_item *it)
{
    double a = 0;
    int k;
    for (k = 0; k < it->npoints; k++) {
        int j = (k + 1) % it->npoints;
        a += (double)it->points[2 * k] * it->points[2 * j + 1] - (double)it->points[2 * j] * it->points[2 * k + 1];
    }
    return fabs(a) / 2;
}

/* A chart's text: after its area's fill (the cells' text comes before). */
static const oc_draw_item *find_text(const oc_draw_list *l, const char *s)
{
    int i, chart = 0;
    for (i = 0; i < l->n; i++) {
        if (l->items[i].kind == OC_DRAW_FILL && l->items[i].role == OC_ROLE_CHART && l->items[i].rgb == 0xFFFFFF)
            chart = 1;
        if (chart && l->items[i].kind == OC_DRAW_TEXT && l->items[i].text && !strcmp(l->items[i].text, s))
            return &l->items[i];
    }
    return NULL;
}

/* Whether a fill of a colour is about a part of the tallest one (h * times = tall * parts). */
static int has_bar(const oc_draw_list *l, unsigned long rgb, int tall, int parts, int times)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (l->items[i].kind == OC_DRAW_FILL && l->items[i].role == OC_ROLE_CHART && l->items[i].rgb == rgb &&
            abs(l->items[i].h * times - tall * parts) <= times)
            return 1;
    return 0;
}

static void chart_values(oc_core *core)
{
    enter_at(core, "A2", "North");
    enter_at(core, "A3", "South");
    enter_at(core, "A4", "West");
    enter_at(core, "B1", "2025");
    enter_at(core, "B2", "10");
    enter_at(core, "B3", "20");
    enter_at(core, "B4", "30");
    enter_at(core, "C1", "2026");
    enter_at(core, "C2", "15");
    enter_at(core, "C3", "5");
    enter_at(core, "C4", "=C2*2");
}

static void test_charts(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    const oc_draw_item *it;
    int i, tall, inside = 1, panes_ok = 1, ysplit;
    if (!core)
        return;
    oc_draw_list_init(&l);
    chart_values(core);
    put_chart(core, "<c:barChart><c:barDir val=\"col\"/><c:grouping val=\"clustered\"/>"
                    SERIES(0, "2025", "B", "") SERIES(1, "2026", "C", "")
                    "<c:gapWidth val=\"100\"/><c:axId val=\"1\"/><c:axId val=\"2\"/></c:barChart>",
              "<c:legend><c:legendPos val=\"r\"/></c:legend>");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    /* its area: white, from B2 (40 + 64, 18 + 17) four columns wide and ten rows tall */
    for (i = 0; i < l.n && !(l.items[i].kind == OC_DRAW_FILL && l.items[i].role == OC_ROLE_CHART); i++)
        ;
    CHECK(i < l.n && l.items[i].rgb == 0xFFFFFF && l.items[i].x == 104 && l.items[i].y == 35 && l.items[i].w == 256 &&
          l.items[i].h == 170);
    /* over the cells: after their text */
    CHECK(i < l.n && text_of(&l, 1, 0) && text_of(&l, 1, 0) < &l.items[i]);
    /* the title, the legend, the categories from the cells, the values' scale */
    CHECK((it = find_text(&l, "Sales")) != NULL && (it->flags & OWF_BOLD) && it->x >= 104 && it->x + it->w <= 360);
    CHECK(find_text(&l, "2025") && find_text(&l, "2026") && find_text(&l, "North") && find_text(&l, "West"));
    CHECK(find_text(&l, "0") && find_text(&l, "30"));
    /* six columns, three of each series' colour (Office's first two accents), 30 three times as tall as 10 */
    CHECK(chart_items(&l, OC_DRAW_FILL, 0x4472C4) == 4 && chart_items(&l, OC_DRAW_FILL, 0xED7D31) == 4);   /* and the legend's */
    tall = biggest(&l, 0x4472C4, 0);
    CHECK(tall > 60 && has_bar(&l, 0x4472C4, tall, 1, 3) && has_bar(&l, 0x4472C4, tall, 2, 3));
    /* the gridlines and the axis: lines across the plot */
    CHECK(chart_items(&l, OC_DRAW_SEGMENT, 0xD9D9D9) >= 5 && chart_items(&l, OC_DRAW_SEGMENT, 0xBFBFBF) == 1);
    for (i = 0; i < l.n; i++)
        if (l.items[i].role == OC_ROLE_CHART &&
            (l.items[i].x < 104 || l.items[i].y < 35 || l.items[i].x + (l.items[i].w > 0 ? l.items[i].w : 0) > 360 ||
             l.items[i].y + (l.items[i].h > 0 ? l.items[i].h : 0) > 205))
            inside = 0;
    CHECK(inside);
    /* typed values show: B4 from 30 to 60, the tallest column twice as tall */
    enter_at(core, "B4", "60");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    tall = biggest(&l, 0x4472C4, 0);
    CHECK(find_text(&l, "60") && has_bar(&l, 0x4472C4, tall, 1, 6) && has_bar(&l, 0x4472C4, tall, 1, 3));
    /* a formula's value: C4 = C2*2 = 30 */
    CHECK(chart_items(&l, OC_DRAW_FILL, 0xED7D31) == 4);
    /* scrolled away below it: nothing of it */
    oc_view_scroll_to(core, 20, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(chart_items(&l, OC_DRAW_FILL, -1) == 0 && chart_items(&l, OC_DRAW_SEGMENT, -1) == 0);
    /* frozen rows: its top in the frozen pane, the rest scrolled; nothing crosses the split */
    oc_core_book(core)->sheets[0].freeze_rows = 3;
    oc_view_scroll_to(core, 6, 0);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    ysplit = 18 + 3 * 17;
    for (i = 0; i < l.n; i++) {
        const oc_draw_item *c = &l.items[i];
        int y0 = c->kind == OC_DRAW_SEGMENT && c->h < 0 ? c->y + c->h : c->y, y1 = y0 + (c->h < 0 ? -c->h : c->h);
        if (c->role == OC_ROLE_CHART && y0 < ysplit && y1 > ysplit)
            panes_ok = 0;
    }
    CHECK(panes_ok);
    CHECK(chart_items(&l, OC_DRAW_FILL, 0xFFFFFF) == 2);                  /* its area in each pane */
    oc_draw_list_free(&l);
    oc_core_free(core);

    /* lines, stacked; one series from cells another book holds: the values the file last had */
    core = fresh();
    oc_draw_list_init(&l);
    chart_values(core);
    put_chart(core, "<c:lineChart><c:grouping val=\"standard\"/>"
                    SERIES(0, "2025", "B", "")
                    "<c:ser><c:idx val=\"1\"/><c:spPr><a:ln><a:solidFill><a:srgbClr val=\"00B050\"/></a:solidFill></a:ln>"
                    "</c:spPr><c:val><c:numRef><c:f>[2]Other!$B$2:$B$4</c:f><c:numCache><c:ptCount val=\"3\"/>"
                    "<c:pt idx=\"0\"><c:v>5</c:v></c:pt><c:pt idx=\"2\"><c:v>45</c:v></c:pt></c:numCache></c:numRef></c:val></c:ser>"
                    "<c:axId val=\"1\"/><c:axId val=\"2\"/></c:lineChart>", "");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(chart_items(&l, OC_DRAW_SEGMENT, 0x4472C4) == 2 * 3);          /* two segments, 3 pixels thick */
    CHECK(chart_items(&l, OC_DRAW_SEGMENT, 0x00B050) == 0);               /* 5, a gap, 45: no line, two dots */
    CHECK(chart_items(&l, OC_DRAW_FILL, 0x00B050) == 2);
    CHECK(find_text(&l, "50") != NULL && !find_text(&l, "2026"));       /* its scale takes in 45; no legend */
    oc_draw_list_free(&l);
    oc_core_free(core);

    /* a pie: a slice a point, in the accents in turn (the second coloured by hand), from 12 o'clock, sized as
       its value, white between; the legend the categories */
    core = fresh();
    oc_draw_list_init(&l);
    chart_values(core);
    put_chart(core, "<c:pieChart><c:varyColors val=\"1\"/><c:ser><c:idx val=\"0\"/><c:dPt><c:idx val=\"1\"/><c:spPr>"
                    "<a:solidFill><a:srgbClr val=\"C00000\"/></a:solidFill></c:spPr></c:dPt><c:cat><c:strRef>"
                    "<c:f>Sheet1!$A$2:$A$4</c:f></c:strRef></c:cat><c:val><c:numRef><c:f>Sheet1!$B$2:$B$4</c:f></c:numRef>"
                    "</c:val></c:ser></c:pieChart>", "<c:legend><c:legendPos val=\"r\"/></c:legend>");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    {
        const oc_draw_item *a = poly_of(&l, 0x4472C4), *b = poly_of(&l, 0xC00000), *c = poly_of(&l, 0xA5A5A5);
        CHECK(a && b && c && chart_items(&l, OC_DRAW_POLY, -1) == 3);
        if (a && b && c) {
            double sa = poly_area(a), sb = poly_area(b), sc = poly_area(c);
            CHECK(fabs(sb / sa - 2) < 0.1 && fabs(sc / sa - 3) < 0.1);
            /* the first slice from 12 o'clock: its first point straight above its last (the centre) */
            CHECK(abs(a->points[0] - a->points[2 * (a->npoints - 1)]) <= 1 && a->points[1] < a->points[2 * (a->npoints - 1) + 1]);
        }
        CHECK(chart_items(&l, OC_DRAW_SEGMENT, 0xFFFFFF) == 3);
        CHECK(find_text(&l, "North") && find_text(&l, "South") && find_text(&l, "West"));
    }
    oc_draw_list_free(&l);
    oc_core_free(core);

    /* a doughnut of two series, rings around a hole, the first innermost */
    core = fresh();
    oc_draw_list_init(&l);
    chart_values(core);
    put_chart(core, "<c:doughnutChart><c:varyColors val=\"1\"/>" SERIES(0, "2025", "B", "") SERIES(1, "2026", "C", "")
                    "<c:firstSliceAng val=\"90\"/><c:holeSize val=\"50\"/></c:doughnutChart>", "");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(chart_items(&l, OC_DRAW_POLY, -1) == 6);
    {
        int cx, cy, near = 1 << 30, far = 0, k, x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
        for (i = 0; i < l.n; i++)                /* the rings' middle: the middle of all they cover */
            if (l.items[i].kind == OC_DRAW_POLY) {
                x0 = l.items[i].x < x0 ? l.items[i].x : x0;
                y0 = l.items[i].y < y0 ? l.items[i].y : y0;
                x1 = l.items[i].x + l.items[i].w > x1 ? l.items[i].x + l.items[i].w : x1;
                y1 = l.items[i].y + l.items[i].h > y1 ? l.items[i].y + l.items[i].h : y1;
            }
        cx = (x0 + x1) / 2;
        cy = (y0 + y1) / 2;
        for (i = 0; i < l.n; i++)
            if (l.items[i].kind == OC_DRAW_POLY)
                for (k = 0; k < l.items[i].npoints; k++) {
                    int dx = l.items[i].points[2 * k] - cx, dy = l.items[i].points[2 * k + 1] - cy, d2 = dx * dx + dy * dy;
                    if (d2 < near)
                        near = d2;
                    if (d2 > far)
                        far = d2;
                }
        CHECK(near > far / 5);                   /* a hole half the size: a quarter of the area, no point near the centre */
    }
    oc_draw_list_free(&l);
    oc_core_free(core);

    /* a scatter: x and y from cells; lines and diamonds, and markers only (squares) */
    core = fresh();
    oc_draw_list_init(&l);
    chart_values(core);
    enter_at(core, "D2", "1");
    enter_at(core, "D3", "2");
    enter_at(core, "D4", "4");
    put_chart(core, "<c:scatterChart><c:scatterStyle val=\"lineMarker\"/>"
                    "<c:ser><c:idx val=\"0\"/><c:marker><c:symbol val=\"diamond\"/></c:marker><c:xVal><c:numRef>"
                    "<c:f>Sheet1!$D$2:$D$4</c:f></c:numRef></c:xVal><c:yVal><c:numRef><c:f>Sheet1!$B$2:$B$4</c:f></c:numRef>"
                    "</c:yVal></c:ser><c:ser><c:idx val=\"1\"/><c:spPr><a:ln><a:noFill/></a:ln></c:spPr><c:marker>"
                    "<c:symbol val=\"square\"/></c:marker><c:xVal><c:numRef><c:f>Sheet1!$D$2:$D$4</c:f></c:numRef></c:xVal>"
                    "<c:yVal><c:numRef><c:f>Sheet1!$C$2:$C$4</c:f></c:numRef></c:yVal></c:ser><c:axId val=\"7\"/>"
                    "<c:axId val=\"2\"/></c:scatterChart><c:valAx><c:axId val=\"7\"/><c:axPos val=\"b\"/></c:valAx>", "");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(chart_items(&l, OC_DRAW_SEGMENT, 0x4472C4) == 2 * 3 && chart_items(&l, OC_DRAW_POLY, 0x4472C4) == 3);
    CHECK(chart_items(&l, OC_DRAW_SEGMENT, 0xED7D31) == 0 && chart_items(&l, OC_DRAW_FILL, 0xED7D31) == 3);
    {
        int xs[3], n = 0;
        for (i = 0; i < l.n && n < 3; i++)
            if (l.items[i].kind == OC_DRAW_POLY && l.items[i].rgb == 0x4472C4)
                xs[n++] = l.items[i].x + l.items[i].w / 2;
        CHECK(n == 3 && abs((xs[2] - xs[1]) - 2 * (xs[1] - xs[0])) <= 2);   /* x 1, 2, 4 */
    }
    CHECK(find_text(&l, "4") != NULL);                                    /* the x axis's labels */
    oc_draw_list_free(&l);
    oc_core_free(core);

    /* values at the ends of what a double holds, limits the wrong way round, every zoom: drawn, inside its box */
    core = fresh();
    oc_draw_list_init(&l);
    enter_at(core, "B2", "1E+300");
    enter_at(core, "B3", "-1E+300");
    enter_at(core, "B4", "=1/0");
    put_chart(core, "<c:barChart><c:barDir val=\"bar\"/><c:grouping val=\"percentStacked\"/>" SERIES(0, "x", "B", "")
                    SERIES(1, "y", "C", "") "</c:barChart><c:valAx><c:scaling><c:min val=\"10\"/><c:max val=\"5\"/></c:scaling>"
                    "</c:valAx>", "<c:legend><c:legendPos val=\"t\"/></c:legend>");
    {
        static const int zooms[] = { 10, 50, 100, 200, 400 };
        int z, bad = 0;
        for (z = 0; z < 5; z++) {
            oc_view_set_zoom(core, zooms[z]);
            CHECK(oc_view_draw(core, &l) == OWF_OK);
            CHECK(l.n < 2000);
            for (i = 0; i < l.n; i++)
                if (l.items[i].role == OC_ROLE_CHART && (l.items[i].x < 40 || l.items[i].y < 18 || l.items[i].x > 640 || l.items[i].y > 400))
                    bad++;
        }
        CHECK(bad == 0);
    }
    oc_draw_list_free(&l);
    oc_core_free(core);

    core = fresh();
    oc_draw_list_init(&l);
    put_chart(core, "<c:pieChart>" SERIES(0, "2025", "B", "") "</c:pieChart>", "");
    oc_draw_list_free(&l);
    oc_core_free(core);

    /* the charts fixture: three charts beside a table frozen at B2, their series named from the cells */
    core = fresh();
    oc_draw_list_init(&l);
    {
        owf_report *report = owf_report_new();
        const owf_book_format *used = NULL;
        CHECK(report && oc_core_open(core, "tests/book/fixtures/charts-openpyxl.xlsx", NULL, report, &used) == OWF_OK);
        owf_report_free(report);
    }
    oc_view_set_size(core, 1700, 700);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(chart_items(&l, OC_DRAW_FILL, 0xFFFFFF) == 3);                  /* three areas */
    CHECK(find_text(&l, "Sales by month") && find_text(&l, "Stacked bars") && find_text(&l, "Share"));
    CHECK(find_text(&l, "Amiga") && find_text(&l, "Atari") && find_text(&l, "PC") && find_text(&l, "100%"));
    CHECK(chart_items(&l, OC_DRAW_SEGMENT, 0xA5A5A5) >= 5 * 3);         /* PC's line: five segments */
    CHECK(chart_items(&l, OC_DRAW_FILL, 0x4472C4) >= 6 + 6 + 3);        /* Amiga's bars, columns and swatches */
    /* scrolled under the frozen first row and column: no chart reaches into them */
    oc_view_scroll_to(core, 10, 3);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = 0, panes_ok = 1; i < l.n; i++)
        if (l.items[i].role == OC_ROLE_CHART && l.items[i].kind == OC_DRAW_FILL && (l.items[i].x < 40 + 64 || l.items[i].y < 18 + 17))
            panes_ok = 0;
    CHECK(panes_ok && chart_items(&l, OC_DRAW_FILL, 0xFFFFFF) >= 1);
    oc_draw_list_free(&l);
    oc_core_free(core);

    core = fresh();
    oc_draw_list_init(&l);
    put_chart(core, "<c:pieChart>" SERIES(0, "2025", "B", "") "</c:pieChart>", "");
    /* the sheet changed: another sheet has none */
    oc_core_add_sheet(core, "Two");
    oc_core_set_sheet(core, 1);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(chart_items(&l, OC_DRAW_FILL, -1) == 0);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

/* Formatting set on whole rows (a row's own style, over its columns') and whole columns: shown on the empty
 * cells there, given to cells typed into them, set and cleared by selecting whole rows, undone, saved. */
static void test_row_styles(void)
{
    static const char *const path = "build/host/calc-core-rows.xlsx";
    oc_core *core = fresh();
    owf_report *report = owf_report_new();
    const owf_book_format *used = NULL;
    oc_draw_list l;
    const owf_sheet *s;
    int i, styled;
    if (!core || !report) {
        oc_core_free(core);
        owf_report_free(report);
        return;
    }
    oc_draw_list_init(&l);
    /* column C yellow, then row 3 green: C3 green (the row's over the column's), C2 and C4 yellow */
    oc_core_select_cols(core);                   /* A1: column A */
    go(core, "C1");
    oc_core_select_cols(core);
    CHECK(oc_core_set_fill(core, OWF_SHADE(0xFFFF00)) == OWF_OK);
    go(core, "A3");
    oc_core_select_rows(core);
    CHECK(oc_core_set_fill(core, OWF_SHADE(0x00FF00)) == OWF_OK);
    s = &oc_core_book(core)->sheets[0];
    for (i = styled = 0; i < s->nrows; i++)
        if (s->rows[i].row == 2 && s->rows[i].style > 0)
            styled = 1;
    CHECK(styled && s->ncells == 0);             /* the row's own style; no cells made */
    go(core, "A1");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 2, 2) == 0x00FF00 && fill_of(&l, 2, 7) == 0x00FF00 && fill_of(&l, 1, 2) == 0xFFFF00 &&
          fill_of(&l, 3, 2) == 0xFFFF00 && fill_of(&l, 1, 1) == -1);
    /* typed into: the cell takes its row's style, else its column's */
    enter_at(core, "E3", "12");
    enter_at(core, "C9", "7");
    enter_at(core, "B9", "8");
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 2, 4) == 0x00FF00 && fill_of(&l, 8, 2) == 0xFFFF00 && fill_of(&l, 8, 1) == -1);
    /* undone, the row loses it again; done again, back */
    go(core, "A3");
    oc_core_select_rows(core);
    CHECK(oc_core_set_fill(core, OWF_SHADE(0x0000FF)) == OWF_OK);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 2, 7) == 0x0000FF && fill_of(&l, 2, 4) == 0x0000FF);
    CHECK(oc_core_undo(core) == OWF_OK);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 2, 7) == 0x00FF00 && fill_of(&l, 2, 4) == 0x00FF00);
    /* saved as .xlsx and opened again: the same */
    CHECK(oc_core_save(core, path, "xlsx", report) == OWF_OK);
    CHECK(oc_core_open(core, path, NULL, report, &used) == OWF_OK);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 2, 7) == 0x00FF00 && fill_of(&l, 1, 2) == 0xFFFF00 && fill_of(&l, 2, 2) == 0x00FF00);
    /* a row put in above moves it down; its formats cleared, the row's own go too */
    go(core, "A1");
    oc_core_select_rows(core);
    CHECK(oc_core_insert(core, 0) == OWF_OK);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 3, 7) == 0x00FF00 && fill_of(&l, 2, 7) == -1);
    go(core, "A4");
    oc_core_select_rows(core);
    CHECK(oc_core_clear(core, OC_CLEAR_FORMATS) == OWF_OK);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(fill_of(&l, 3, 7) == -1 && fill_of(&l, 3, 2) == 0xFFFF00);    /* C4: its column's again */
    remove(path);
    oc_draw_list_free(&l);
    owf_report_free(report);
    oc_core_free(core);
}

static void test_status(void)
{
    oc_core *core = fresh();
    char s[128];
    if (!core)
        return;
    enter_at(core, "A1", "1");
    enter_at(core, "A2", "2");
    enter_at(core, "A3", "3");
    go(core, "A1");
    oc_core_status(core, s, sizeof s);
    CHECK_STR(s, "");
    go(core, "A1:A3");
    oc_core_status(core, s, sizeof s);
    CHECK_STR(s, "Sum 6  Average 2  Count 3");
    oc_core_free(core);
}

static void test_files(void)
{
    oc_core *core = fresh();
    owf_report *report = owf_report_new();
    oc_draw_list l;
    FILE *f;
    char buf[256];
    size_t n;
    const owf_book_format *used = NULL;
    if (!core)
        return;
    enter_at(core, "A1", "1");
    enter_at(core, "B1", "two");
    enter_at(core, "A2", "=A1+1");
    CHECK(oc_core_is_dirty(core));
    CHECK(oc_core_save(core, "build/host/calc-core-test.csv", NULL, report) == OWF_OK);
    CHECK(!oc_core_is_dirty(core));
    f = fopen("build/host/calc-core-test.csv", "rb");
    n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f)
        fclose(f);
    buf[n] = 0;
    CHECK_STR(buf, "1,two\r\n2,\r\n");
    remove("build/host/calc-core-test.csv");
    /* an export is a copy: the workbook stays unsaved */
    enter_at(core, "B2", "x");
    CHECK(oc_core_is_dirty(core));
    CHECK(oc_core_export(core, "build/host/calc-core-test.csv", NULL, report) == OWF_OK);
    CHECK(oc_core_is_dirty(core));
    remove("build/host/calc-core-test.csv");
    oc_core_mark_saved(core);
    CHECK(!oc_core_is_dirty(core));
    oc_core_set_dirty(core);
    CHECK(oc_core_is_dirty(core));
    {
        char *in = oc_core_cell_input(core, 0, 1, 0);
        CHECK_STR(in, "=A1+1");
        owf_free(in);
        in = oc_core_cell_input(core, 0, 5, 5);
        CHECK_STR(in, "");
        owf_free(in);
    }
    CHECK(oc_core_save(core, "nosuchvolume:/x.csv", NULL, report) != OWF_OK);
    CHECK(oc_core_save(core, "build/host/calc-core-test.xyz", NULL, report) == OWF_ERR_FORMAT);
    CHECK(oc_core_is_dirty(core));
    /* typing commits before a save */
    go(core, "C1");
    type(core, "3");
    CHECK(oc_core_save(core, "build/host/calc-core-test.csv", "csv", report) == OWF_OK);
    CHECK(num(core, "C1") == 3);
    remove("build/host/calc-core-test.csv");

    /* a workbook from a file, then shown */
    CHECK(oc_core_open(core, "tests/book/fixtures/book.xls", NULL, report, &used) == OWF_OK);
    CHECK(used && !strcmp(used->name, "xls"));
    CHECK(oc_core_book(core)->nsheets >= 1);
    CHECK(!oc_core_is_dirty(core) && !oc_core_can_undo(core));
    oc_draw_list_init(&l);
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    CHECK(count_kind(&l, OC_DRAW_TEXT, -1) > 0);
    oc_draw_list_free(&l);
    CHECK(oc_core_open(core, "tests/book/fixtures/no-such-file.xls", NULL, report, NULL) != OWF_OK);
    CHECK(oc_core_book(core)->nsheets >= 1);             /* still the one that opened */
    CHECK(oc_core_set_book(core, NULL) == OWF_OK);
    CHECK(oc_core_book(core)->nsheets == 1 && empty(core, "A1"));
    owf_report_free(report);
    oc_core_free(core);
}

static void test_latin1(void)
{
    size_t n;
    char *t = oc_text_to_latin1("caf\xC3\xA9 \xE2\x82\xAC" "5 \xC2\xA3", 13, &n);
    CHECK(t && n == 9 && !memcmp(t, "caf\xE9 ?5 \xA3", 9));
    owf_free(t);
    t = oc_text_to_latin1("a\xC3", 2, &n);            /* cut short */
    CHECK(t && n == 2 && !strcmp(t, "a?"));
    owf_free(t);
    t = oc_text_to_latin1("\xF0\x9F\x98\x80!", 5, &n);
    CHECK_STR(t, "?!");
    owf_free(t);
    t = oc_text_from_latin1("caf\xE9\t\xA3", 6, &n);
    CHECK(t && n == 8 && !strcmp(t, "caf\xC3\xA9\t\xC2\xA3"));
    owf_free(t);
}

static void test_more(void)
{
    oc_core *core = fresh();
    oc_draw_list l;
    oc_hit hit;
    int i, found;
    if (!core)
        return;
    oc_draw_list_init(&l);
    /* Ctrl+click ranges, then Delete clears them all */
    enter_at(core, "A1", "1");
    enter_at(core, "C3", "2");
    go(core, "A1");
    oc_core_select_cell(core, 2, 2, 0, 1);
    CHECK_STR(active_name(core), "C3");
    key(core, OC_KEY_DELETE, 0);
    CHECK(empty(core, "A1") && empty(core, "C3"));
    oc_core_undo(core);
    CHECK(num(core, "A1") == 1 && num(core, "C3") == 2);

    /* a hidden column has no letter; a tall row is drawn tall */
    go(core, "B1");
    oc_core_hide(core, 1, 1);
    go(core, "A2");
    oc_core_set_row_height(core, 600);                   /* 40 pixels */
    CHECK(oc_view_draw(core, &l) == OWF_OK);
    for (i = 0, found = 0; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_HEADER && l.items[i].text && !strcmp(l.items[i].text, "B"))
            found = 1;
    CHECK(!found);
    for (i = 0, found = 0; i < l.n; i++)
        if (l.items[i].kind == OC_DRAW_HEADER && l.items[i].text && !strcmp(l.items[i].text, "2") && l.items[i].h == 40)
            found = 1;
    CHECK(found);
    CHECK(oc_view_hit(core, 40 + 64 + 5, 18 + 17 + 30, &hit) == OC_HIT_CELL && hit.row == 1 && hit.col == 2);
    oc_core_undo(core);
    oc_core_undo(core);
    CHECK(oc_core_col_width(core, 1) == 960 && oc_core_row_height(core, 1) == 255);

    /* a percentage reads back as typed */
    go(core, "D1");
    type(core, "0.5");
    key(core, OC_KEY_RETURN, 0);
    go(core, "D1");
    oc_core_set_number_format(core, "0%");
    CHECK_STR(oc_core_input_text(core), "50%");

    /* a paste that would go off the sheet */
    go(core, "A1:B2");
    owf_free(oc_core_copy(core, NULL));
    go(core, "XFD1");
    CHECK(oc_core_paste(core, NULL, 0) == OWF_ERR_UNSUPPORTED);

    /* Escape while pointing leaves the cell as it was */
    go(core, "E5");
    type(core, "=");
    key(core, OC_KEY_UP, 0);
    key(core, OC_KEY_ESCAPE, 0);
    CHECK(empty(core, "E5") && !oc_core_editing(core));

    /* adding a sheet is undone */
    CHECK(oc_core_add_sheet(core, "Two") == 1);
    oc_core_undo(core);
    CHECK(oc_core_book(core)->nsheets == 1 && oc_core_sheet(core) == 0);

    /* 3-D references follow a rename too */
    oc_core_add_sheet(core, NULL);
    oc_core_add_sheet(core, NULL);
    oc_core_set_sheet(core, 0);
    enter_at(core, "F1", "=SUM(Sheet2:Sheet3!A1)");
    CHECK(oc_core_rename_sheet(core, 2, "Last") == OWF_OK);
    CHECK_STR(formula(core, "F1"), "SUM(Sheet2:Last!A1)");
    CHECK(oc_core_rename_sheet(core, 1, "First") == OWF_OK);
    CHECK_STR(formula(core, "F1"), "SUM(First:Last!A1)");

    /* the scroller's total follows the used part as it grows */
    {
        unsigned first, vis, total;
        oc_view_scroller(core, 0, &first, &vis, &total);
        CHECK(total < 500);
        enter_at(core, "A700", "far");
        oc_view_scroller(core, 0, &first, &vis, &total);
        CHECK(total >= 700);
    }

    /* zoom: the cell stays in view */
    go(core, "A60");
    oc_view_set_zoom(core, 50);
    oc_view_show_cell(core, 59, 0);
    CHECK(oc_view_zoom(core) == 50);
    oc_view_set_zoom(core, 1000);
    CHECK(oc_view_zoom(core) == 400);
    oc_draw_list_free(&l);
    oc_core_free(core);
}

int main(void)
{
    test_more();
    test_latin1();
    test_names();
    test_navigation();
    test_editing();
    test_undo();
    test_fill();
    test_insert_delete();
    test_clear_and_styles();
    test_clipboard();
    test_sheets();
    test_draw();
    test_borders();
    test_condfmt();
    test_font_caps();
    test_comments();
    test_validation();
    test_autofilter();
    test_drawings_kept();
    test_row_styles();
    test_charts();
    test_row_numbers();
    test_text_layout();
    test_tab_strip();
    test_status();
    test_files();
    if (failures) {
        fprintf(stderr, "opencalc core tests: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("opencalc core tests passed (%d checks)\n", checks);
    return 0;
}

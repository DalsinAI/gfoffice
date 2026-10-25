/*
 * OpenCalc's core on the PC: navigation, editing, undo, fill, insert and
 * delete, the clipboard's text, styles, sheets, files and the draw list.
 * Run from the repository's top (make calc-test, make calc-check).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opencalc_core.h"

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
    test_status();
    test_files();
    if (failures) {
        fprintf(stderr, "opencalc core tests: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("opencalc core tests passed (%d checks)\n", checks);
    return 0;
}

/*
 * OpenCalc's core: the book and its engine, sheets, cell names, the active
 * cell and the selection, and moving them with the keys as Excel and
 * LibreOffice do.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Small things ---- */

char *oc_strdup(const char *s)
{
    size_t n = strlen(s);
    char *p = owf_malloc(n + 1);
    if (p)
        memcpy(p, s, n + 1);
    return p;
}

int oc_cell_empty(const owf_cell *cell)
{
    return !cell || (cell->type == OWF_VALUE_EMPTY && !cell->formula);
}

owf_sheet *oc_sheet(const oc_core *core)
{
    return &core->book->sheets[core->sheet];
}

oc_sheetstate *oc_state(oc_core *core)
{
    return &core->states[core->sheet];
}

const oc_sheetstate *oc_state_const(const oc_core *core)
{
    return &core->states[core->sheet];
}

const owf_cell *oc_find(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    if (sheet < 0 || sheet >= core->book->nsheets)
        return NULL;
    return owf_sheet_find(&core->book->sheets[sheet], row, col);
}

static int has_value(const owf_sheet *sheet, unsigned row, unsigned col)
{
    return !oc_cell_empty(owf_sheet_find(sheet, row, col));
}

/* How far the sheet is used: one past the last row and column with a value
 * or a style (0, 0 when empty). */
void oc_used_range(const owf_sheet *sheet, unsigned *rows, unsigned *cols)
{
    int i;
    unsigned r = 0, c = 0;
    for (i = 0; i < sheet->ncells; i++) {
        const owf_cell *cell = &sheet->cells[i];
        if (oc_cell_empty(cell) && !cell->style)
            continue;
        if (cell->row + 1 > r)
            r = cell->row + 1;
        if (cell->col + 1 > c)
            c = cell->col + 1;
    }
    *rows = r;
    *cols = c;
}

/* A range cut down to the used part of the sheet, for whole rows and
 * columns. 0 when nothing is left. */
int oc_range_clip(const oc_core *core, const owf_range *in, owf_range *out)
{
    unsigned rows, cols;
    oc_used_range(oc_sheet(core), &rows, &cols);
    *out = *in;
    if (in->last_row == OC_MAX_ROWS - 1 && in->first_row == 0)
        out->last_row = rows ? rows - 1 : 0;
    if (in->last_col == OC_MAX_COLS - 1 && in->first_col == 0)
        out->last_col = cols ? cols - 1 : 0;
    if ((in->first_row == 0 && in->last_row == OC_MAX_ROWS - 1 && !rows) ||
        (in->first_col == 0 && in->last_col == OC_MAX_COLS - 1 && !cols))
        return 0;
    return out->first_row <= out->last_row && out->first_col <= out->last_col;
}

void oc_changed(oc_core *core)
{
    core->dirty = 1;
    core->changes++;
}

/* ---- The book and its engine ---- */

static owf_book *blank_book(void)
{
    owf_book *book = owf_book_new();
    if (book && !owf_book_add_sheet(book, "Sheet1")) {
        owf_book_free(book);
        book = NULL;
    }
    return book;
}

static void state_init(oc_sheetstate *st)
{
    memset(st, 0, sizeof *st);
    st->sel.nranges = 1;
    st->tab_col = -1;
}

/* One state per sheet, the frozen panes respected. */
static int states_fit(oc_core *core)
{
    int i, n = core->book->nsheets;
    oc_sheetstate *p;
    if (n <= core->nstates)
        return 1;
    p = realloc(core->states, (size_t)n * sizeof *p);
    if (!p)
        return 0;
    for (i = core->nstates; i < n; i++) {
        state_init(&p[i]);
        p[i].top = core->book->sheets[i].freeze_rows;
        p[i].left = core->book->sheets[i].freeze_cols;
    }
    core->states = p;
    core->nstates = n;
    return 1;
}

oc_core *oc_core_new(owf_book *book)
{
    oc_core *core = calloc(1, sizeof *core);
    if (!book)
        book = blank_book();
    if (!core || !book) {
        free(core);
        owf_book_free(book);
        return NULL;
    }
    core->zoom = 100;
    core->width = 640;
    core->height = 400;
    core->rowhdr = 40;
    core->colhdr = 18;
    if (oc_core_set_book(core, book) != OWF_OK) {
        free(core);
        return NULL;
    }
    return core;
}

void oc_core_free(oc_core *core)
{
    if (!core)
        return;
    oc_edit_free(core);
    oc_undo_clear(core);
    oc_clip_free(&core->clip);
    owf_calc_free(core->calc);
    owf_book_free(core->book);
    free(core->states);
    owf_free(core->input);
    free(core);
}

owf_book *oc_core_book(const oc_core *core)
{
    return core->book;
}

owf_calc *oc_core_calc(const oc_core *core)
{
    return core->calc;
}

int oc_core_set_book(oc_core *core, owf_book *book)
{
    owf_calc *calc;
    int i;
    if (!book && !(book = blank_book()))
        return OWF_ERR_MEMORY;
    if (!book->nsheets && !owf_book_add_sheet(book, "Sheet1")) {
        owf_book_free(book);
        return OWF_ERR_MEMORY;
    }
    calc = owf_calc_new(book);
    if (!calc) {
        owf_book_free(book);
        return OWF_ERR_MEMORY;
    }
    oc_edit_free(core);
    oc_undo_clear(core);
    oc_clip_free(&core->clip);
    owf_calc_free(core->calc);
    owf_book_free(core->book);
    core->book = book;
    core->calc = calc;
    free(core->states);
    core->states = NULL;
    core->nstates = 0;
    if (!states_fit(core)) {
        /* keep a usable core: one state is all a sheet needs to be shown */
        core->states = calloc(1, sizeof *core->states);
        if (!core->states)
            return OWF_ERR_MEMORY;
        state_init(core->states);
        core->nstates = 1;
        core->sheet = 0;
    }
    core->sheet = book->active >= 0 && book->active < book->nsheets && book->active < core->nstates ? book->active : 0;
    for (i = 0; i < core->nstates; i++)
        if (book->sheets[i].hidden && i == core->sheet)
            core->sheet = 0;
    core->dirty = 0;
    core->changes++;
    core->undo_lost = 0;
    return OWF_OK;
}

int oc_core_open(oc_core *core, const char *path, const char *format, owf_report *report,
                 const owf_book_format **used)
{
    owf_book *book = NULL;
    int rc = owf_import_book_file(path, format, &book, report, used);
    if (rc != OWF_OK)
        return rc;
    return oc_core_set_book(core, book);
}

int oc_core_export(oc_core *core, const char *path, const char *format, owf_report *report)
{
    int rc, active = core->book->active;
    if (core->edit_mode)
        oc_edit_commit(core, OC_MOVE_NONE);
    core->book->active = core->sheet;         /* CSV writes the sheet showing; the others keep it too */
    rc = owf_export_book_file(core->book, path, format, report);
    if (rc != OWF_OK)
        core->book->active = active;
    return rc;
}

int oc_core_save(oc_core *core, const char *path, const char *format, owf_report *report)
{
    int rc = oc_core_export(core, path, format, report);
    if (rc == OWF_OK)
        core->dirty = 0;
    return rc;
}

void oc_core_set_dirty(oc_core *core)
{
    oc_changed(core);
}

int oc_core_is_dirty(const oc_core *core)
{
    return core->dirty;
}

void oc_core_mark_saved(oc_core *core)
{
    core->dirty = 0;
}

unsigned long oc_core_changes(const oc_core *core)
{
    return core->changes;
}

int oc_recalc(oc_core *core, int rebuild)
{
    int rc = OWF_OK;
    if (rebuild)
        rc = owf_calc_rebuild(core->calc);
    if (rc == OWF_OK)
        rc = owf_calc_recalc(core->calc);
    return rc;
}

/* ---- Sheets ---- */

int oc_core_sheet(const oc_core *core)
{
    return core->sheet;
}

int oc_core_set_sheet(oc_core *core, int sheet)
{
    if (sheet < 0 || sheet >= core->book->nsheets || sheet >= core->nstates)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit(core, OC_MOVE_NONE);
    core->sheet = sheet;
    return OWF_OK;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}

static int name_taken(const owf_book *book, const char *name, int except)
{
    int i;
    for (i = 0; i < book->nsheets; i++)
        if (i != except && same_name(book->sheets[i].name, name))
            return 1;
    return 0;
}

static int name_ok(const owf_book *book, const char *name, int except)
{
    const char *p;
    if (!*name || strlen(name) > 31 || name[0] == '\'' || name_taken(book, name, except))
        return 0;
    for (p = name; *p; p++)
        if (strchr("[]:*?/\\", *p))
            return 0;
    return 1;
}

int oc_core_add_sheet(oc_core *core, const char *name)
{
    owf_book *book = core->book;
    char made[32];
    oc_undo *u;
    owf_sheet *s, keep;
    int at = core->sheet + 1, i, n = book->nsheets + 1;
    if (core->edit_mode)
        oc_edit_commit(core, OC_MOVE_NONE);
    if (!name) {
        do
            snprintf(made, sizeof made, "Sheet%d", n++);
        while (name_taken(book, made, -1));
        name = made;
    } else if (!name_ok(book, name, -1))
        return -1;
    u = oc_undo_new(core, OC_UNDO_BOOK);
    if (u && oc_undo_save_book(core, u) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    if (!(s = owf_book_add_sheet(book, name))) {
        oc_undo_free(u);
        return -1;
    }
    /* the new sheet goes after the one showing */
    keep = *s;
    for (i = book->nsheets - 1; i > at; i--)
        book->sheets[i] = book->sheets[i - 1];
    book->sheets[at] = keep;
    if (book->active >= at)
        book->active++;
    if (!states_fit(core)) {
        oc_undo_free(u);
        return -1;
    }
    for (i = book->nsheets - 1; i > at; i--)
        core->states[i] = core->states[i - 1];
    state_init(&core->states[at]);
    oc_undo_push(core, u);
    owf_calc_rebuild(core->calc);
    owf_calc_recalc(core->calc);
    core->sheet = at;
    oc_changed(core);
    return at;
}

int oc_core_rename_sheet(oc_core *core, int sheet, const char *name)
{
    owf_book *book = core->book;
    oc_undo *u;
    int rc;
    if (sheet < 0 || sheet >= book->nsheets || !name_ok(book, name, sheet))
        return OWF_ERR_FORMAT;
    if (!strcmp(book->sheets[sheet].name, name))
        return OWF_OK;
    if (core->edit_mode)
        oc_edit_commit(core, OC_MOVE_NONE);
    u = oc_undo_new(core, OC_UNDO_BOOK);
    if (u && oc_undo_save_book(core, u) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    /* the engine renames it in every formula and name that refers to it */
    rc = owf_calc_rename_sheet(core->calc, sheet, name);
    if (rc != OWF_OK) {
        oc_undo_free(u);
        return rc;
    }
    oc_undo_push(core, u);
    owf_calc_recalc(core->calc);
    oc_changed(core);
    return OWF_OK;
}

int oc_core_delete_sheet(oc_core *core, int sheet)
{
    owf_book *book = core->book;
    oc_undo *u;
    owf_sheet gone;
    int i, j;
    if (sheet < 0 || sheet >= book->nsheets || book->nsheets < 2)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_cancel(core);
    u = oc_undo_new(core, OC_UNDO_BOOK);
    if (u && oc_undo_save_book(core, u) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    oc_undo_push(core, u);
    gone = book->sheets[sheet];
    for (i = sheet; i + 1 < book->nsheets; i++) {
        book->sheets[i] = book->sheets[i + 1];
        core->states[i] = core->states[i + 1];
    }
    book->nsheets--;
    core->nstates--;
    for (j = 0; j < gone.ncells; j++) {
        owf_free(gone.cells[j].text);
        owf_free(gone.cells[j].formula);
    }
    owf_free(gone.cells);
    owf_free(gone.cols);
    owf_free(gone.rows);
    owf_free(gone.merges);
    owf_free(gone.name);
    /* names that belonged to it go with it */
    for (i = j = 0; i < book->nnames; i++) {
        owf_defname *nm = &book->names[i];
        if (nm->sheet == sheet) {
            owf_free(nm->name);
            owf_free(nm->formula);
            continue;
        }
        if (nm->sheet > sheet)
            nm->sheet--;
        book->names[j++] = *nm;
    }
    book->nnames = j;
    if (book->active >= book->nsheets || book->active > sheet)
        book->active = book->active > 0 ? book->active - 1 : 0;
    if (core->sheet >= book->nsheets || core->sheet > sheet)
        core->sheet = core->sheet > 0 ? core->sheet - 1 : 0;
    owf_calc_rebuild(core->calc);
    owf_calc_recalc(core->calc);
    if (core->clip.valid)
        core->clip.cut = 0;
    oc_changed(core);
    return OWF_OK;
}

/* ---- Names of cells ---- */

void oc_col_name(unsigned col, char *out)
{
    char t[4];
    int n = 0, i;
    col++;
    while (col && n < 3) {
        t[n++] = (char)('A' + (col - 1) % 26);
        col = (col - 1) / 26;
    }
    for (i = 0; i < n; i++)
        out[i] = t[n - 1 - i];
    out[n] = 0;
}

void oc_cell_name(unsigned row, unsigned col, char *out)
{
    owf_calc_cell_name(row, col, out);
}

/* Column letters at s (with an optional $): the column, and the length. */
static int col_part(const char *s, unsigned *col)
{
    const char *p = s;
    unsigned long c = 0;
    int n = 0;
    if (*p == '$')
        p++;
    while (((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) && n < 4) {
        c = c * 26 + (unsigned long)((*p & 0x1F) - 1 + 1);
        p++;
        n++;
    }
    if (!n || c > OC_MAX_COLS)
        return 0;
    *col = (unsigned)(c - 1);
    return (int)(p - s);
}

static int row_part(const char *s, unsigned *row)
{
    const char *p = s;
    unsigned long r = 0;
    int n = 0;
    if (*p == '$')
        p++;
    while (*p >= '0' && *p <= '9' && n < 8) {
        r = r * 10 + (unsigned long)(*p - '0');
        p++;
        n++;
    }
    if (!n || r == 0 || r > OC_MAX_ROWS)
        return 0;
    *row = (unsigned)(r - 1);
    return (int)(p - s);
}

static int cell_part(const char *s, unsigned *row, unsigned *col)
{
    int a = col_part(s, col), b;
    if (!a || !(b = row_part(s + a, row)))
        return 0;
    return a + b;
}

int oc_parse_range(const char *text, owf_range *range)
{
    unsigned r1, c1, r2, c2;
    int n;
    while (*text == ' ')
        text++;
    if ((n = cell_part(text, &r1, &c1))) {
        if (!text[n]) {
            range->first_row = range->last_row = r1;
            range->first_col = range->last_col = c1;
            return 1;
        }
        if (text[n] == ':' && cell_part(text + n + 1, &r2, &c2) == (int)strlen(text + n + 1)) {
            range->first_row = r1 < r2 ? r1 : r2;
            range->last_row = r1 < r2 ? r2 : r1;
            range->first_col = c1 < c2 ? c1 : c2;
            range->last_col = c1 < c2 ? c2 : c1;
            return 1;
        }
        return 0;
    }
    if ((n = col_part(text, &c1)) && text[n] == ':' && col_part(text + n + 1, &c2) == (int)strlen(text + n + 1)) {
        range->first_row = 0;
        range->last_row = OC_MAX_ROWS - 1;
        range->first_col = c1 < c2 ? c1 : c2;
        range->last_col = c1 < c2 ? c2 : c1;
        return 1;
    }
    if ((n = row_part(text, &r1)) && text[n] == ':' && row_part(text + n + 1, &r2) == (int)strlen(text + n + 1)) {
        range->first_col = 0;
        range->last_col = OC_MAX_COLS - 1;
        range->first_row = r1 < r2 ? r1 : r2;
        range->last_row = r1 < r2 ? r2 : r1;
        return 1;
    }
    return 0;
}

/* A range as a formula writes it: "B2", "B2:D8", "B:D", "3:5". */
static void range_name(const owf_range *r, char *out)
{
    char a[16], b[16];
    if (r->first_row == 0 && r->last_row == OC_MAX_ROWS - 1 && !(r->first_col == 0 && r->last_col == OC_MAX_COLS - 1)) {
        oc_col_name(r->first_col, a);
        oc_col_name(r->last_col, b);
        sprintf(out, "%s:%s", a, b);
    } else if (r->first_col == 0 && r->last_col == OC_MAX_COLS - 1) {
        sprintf(out, "%u:%u", r->first_row + 1, r->last_row + 1);
    } else if (r->first_row == r->last_row && r->first_col == r->last_col) {
        oc_cell_name(r->first_row, r->first_col, out);
    } else {
        oc_cell_name(r->first_row, r->first_col, a);
        oc_cell_name(r->last_row, r->last_col, b);
        sprintf(out, "%s:%s", a, b);
    }
}

/* ---- The selection ---- */

const owf_range *oc_merge_at(const owf_sheet *sheet, unsigned row, unsigned col)
{
    int i;
    for (i = 0; i < sheet->nmerges; i++) {
        const owf_range *m = &sheet->merges[i];
        if (row >= m->first_row && row <= m->last_row && col >= m->first_col && col <= m->last_col)
            return m;
    }
    return NULL;
}

/* A range grown to take in every merged range it touches. */
static void grow_to_merges(const owf_sheet *sheet, owf_range *r)
{
    int i, again = 1;
    while (again) {
        again = 0;
        for (i = 0; i < sheet->nmerges; i++) {
            const owf_range *m = &sheet->merges[i];
            if (m->first_row > r->last_row || m->last_row < r->first_row || m->first_col > r->last_col ||
                m->last_col < r->first_col)
                continue;
            if (m->first_row < r->first_row) { r->first_row = m->first_row; again = 1; }
            if (m->last_row > r->last_row) { r->last_row = m->last_row; again = 1; }
            if (m->first_col < r->first_col) { r->first_col = m->first_col; again = 1; }
            if (m->last_col > r->last_col) { r->last_col = m->last_col; again = 1; }
        }
    }
}

static void range_of(owf_range *r, unsigned r1, unsigned c1, unsigned r2, unsigned c2)
{
    r->first_row = r1 < r2 ? r1 : r2;
    r->last_row = r1 < r2 ? r2 : r1;
    r->first_col = c1 < c2 ? c1 : c2;
    r->last_col = c1 < c2 ? c2 : c1;
}

void oc_collapse_to(oc_core *core, unsigned row, unsigned col)
{
    oc_sheetstate *st = oc_state(core);
    const owf_range *m = oc_merge_at(oc_sheet(core), row, col);
    if (m) {
        row = m->first_row;
        col = m->first_col;
    }
    st->sel.row = st->sel.anchor_row = st->sel.focus_row = row;
    st->sel.col = st->sel.anchor_col = st->sel.focus_col = col;
    st->sel.nranges = 1;
    range_of(&st->sel.ranges[0], row, col, row, col);
    grow_to_merges(oc_sheet(core), &st->sel.ranges[0]);
    st->tab_col = -1;
}

static void extend_to(oc_core *core, unsigned row, unsigned col)
{
    oc_sheetstate *st = oc_state(core);
    owf_range *r = &st->sel.ranges[st->sel.nranges - 1];
    st->sel.focus_row = row;
    st->sel.focus_col = col;
    range_of(r, st->sel.anchor_row, st->sel.anchor_col, row, col);
    grow_to_merges(oc_sheet(core), r);
    st->tab_col = -1;
}

void oc_core_selection(const oc_core *core, oc_selection *out)
{
    *out = oc_state_const(core)->sel;
}

void oc_core_active(const oc_core *core, unsigned *row, unsigned *col)
{
    const oc_sheetstate *st = oc_state_const(core);
    *row = st->sel.row;
    *col = st->sel.col;
}

void oc_core_select_cell(oc_core *core, unsigned row, unsigned col, int extend, int add)
{
    oc_sheetstate *st = oc_state(core);
    if (row >= OC_MAX_ROWS)
        row = OC_MAX_ROWS - 1;
    if (col >= OC_MAX_COLS)
        col = OC_MAX_COLS - 1;
    if (extend) {
        extend_to(core, row, col);
    } else if (add && st->sel.nranges < OC_MAX_RANGES) {
        owf_range *r = &st->sel.ranges[st->sel.nranges++];
        const owf_range *m = oc_merge_at(oc_sheet(core), row, col);
        if (m) {
            row = m->first_row;
            col = m->first_col;
        }
        range_of(r, row, col, row, col);
        grow_to_merges(oc_sheet(core), r);
        st->sel.row = st->sel.anchor_row = st->sel.focus_row = row;
        st->sel.col = st->sel.anchor_col = st->sel.focus_col = col;
        st->tab_col = -1;
    } else {
        oc_collapse_to(core, row, col);
    }
    oc_view_show_cell(core, extend ? st->sel.focus_row : st->sel.row, extend ? st->sel.focus_col : st->sel.col);
}

void oc_core_select_range(oc_core *core, const owf_range *range)
{
    oc_sheetstate *st = oc_state(core);
    oc_collapse_to(core, range->first_row, range->first_col);
    st->sel.ranges[0] = *range;
    if (st->sel.ranges[0].last_row >= OC_MAX_ROWS)
        st->sel.ranges[0].last_row = OC_MAX_ROWS - 1;
    if (st->sel.ranges[0].last_col >= OC_MAX_COLS)
        st->sel.ranges[0].last_col = OC_MAX_COLS - 1;
    st->sel.focus_row = st->sel.ranges[0].last_row;
    st->sel.focus_col = st->sel.ranges[0].last_col;
    grow_to_merges(oc_sheet(core), &st->sel.ranges[0]);
    oc_view_show_cell(core, st->sel.row, st->sel.col);
}

void oc_core_select_rows(oc_core *core)
{
    oc_sheetstate *st = oc_state(core);
    owf_range *r = &st->sel.ranges[st->sel.nranges - 1];
    r->first_col = 0;
    r->last_col = OC_MAX_COLS - 1;
    st->sel.anchor_col = 0;
    st->sel.focus_col = OC_MAX_COLS - 1;
}

void oc_core_select_cols(oc_core *core)
{
    oc_sheetstate *st = oc_state(core);
    owf_range *r = &st->sel.ranges[st->sel.nranges - 1];
    r->first_row = 0;
    r->last_row = OC_MAX_ROWS - 1;
    st->sel.anchor_row = 0;
    st->sel.focus_row = OC_MAX_ROWS - 1;
}

void oc_core_select_all(oc_core *core)
{
    oc_sheetstate *st = oc_state(core);
    unsigned row = st->sel.row, col = st->sel.col;
    st->sel.nranges = 1;
    range_of(&st->sel.ranges[0], 0, 0, OC_MAX_ROWS - 1, OC_MAX_COLS - 1);
    st->sel.anchor_row = 0;
    st->sel.anchor_col = 0;
    st->sel.focus_row = OC_MAX_ROWS - 1;
    st->sel.focus_col = OC_MAX_COLS - 1;
    st->sel.row = row;
    st->sel.col = col;
}

int oc_core_goto(oc_core *core, const char *name)
{
    owf_range r;
    if (!oc_parse_range(name, &r))
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit(core, OC_MOVE_NONE);
    oc_core_select_range(core, &r);
    return OWF_OK;
}

void oc_core_name_box(const oc_core *core, char *out, size_t size)
{
    const oc_sheetstate *st = oc_state_const(core);
    const owf_range *r = &st->sel.ranges[st->sel.nranges - 1];
    char t[40];
    if (st->sel.nranges == 1 && (r->first_row != r->last_row || r->first_col != r->last_col) &&
        !oc_merge_at(oc_sheet(core), r->first_row, r->first_col))
        range_name(r, t);
    else
        oc_cell_name(st->sel.row, st->sel.col, t);
    snprintf(out, size, "%s", t);
}

/* ---- Moving ---- */

static int row_shows(const owf_sheet *s, unsigned row)
{
    return oc_row_twips(s, row) > 0;
}

static int col_shows(const owf_sheet *s, unsigned col)
{
    return oc_col_twips(s, col) > 0;
}

/* The next row (axis 0) or column showing from v in direction d (+1/-1);
 * 0 when there is none. */
static int step(const owf_sheet *s, int axis, unsigned v, int d, unsigned *out)
{
    unsigned max = axis ? OC_MAX_COLS : OC_MAX_ROWS;
    long x = (long)v;
    for (;;) {
        x += d;
        if (x < 0 || x >= (long)max)
            return 0;
        if (axis ? col_shows(s, (unsigned)x) : row_shows(s, (unsigned)x)) {
            *out = (unsigned)x;
            return 1;
        }
    }
}

/* Where the cells array holds (row, col), or would. */
static int place(const owf_sheet *s, unsigned row, unsigned col)
{
    int lo = 0, hi = s->ncells;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        const owf_cell *c = &s->cells[mid];
        if (c->row < row || (c->row == row && c->col < col))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* The next cell with a value from (row, col) along an axis, or 0. */
static int next_value(const owf_sheet *s, int axis, int d, unsigned row, unsigned col, unsigned *out)
{
    int i;
    if (axis == 1) {                                     /* along the row: its cells are together */
        if (d > 0) {
            for (i = place(s, row, col + 1); i < s->ncells && s->cells[i].row == row; i++)
                if (!oc_cell_empty(&s->cells[i]) && col_shows(s, s->cells[i].col)) {
                    *out = s->cells[i].col;
                    return 1;
                }
        } else {
            for (i = place(s, row, col) - 1; i >= 0 && s->cells[i].row == row; i--)
                if (!oc_cell_empty(&s->cells[i]) && col_shows(s, s->cells[i].col)) {
                    *out = s->cells[i].col;
                    return 1;
                }
        }
        return 0;
    }
    if (d > 0) {
        for (i = place(s, row + 1, 0); i < s->ncells; i++)
            if (s->cells[i].col == col && !oc_cell_empty(&s->cells[i]) && row_shows(s, s->cells[i].row)) {
                *out = s->cells[i].row;
                return 1;
            }
    } else {
        for (i = place(s, row, 0) - 1; i >= 0; i--)
            if (s->cells[i].col == col && !oc_cell_empty(&s->cells[i]) && row_shows(s, s->cells[i].row)) {
                *out = s->cells[i].row;
                return 1;
            }
    }
    return 0;
}

/* Ctrl+arrow: the edge of the data from (row, col). */
static void data_edge(const owf_sheet *s, int axis, int d, unsigned *row, unsigned *col)
{
    unsigned *v = axis ? col : row, next, n2, found;
    unsigned r = *row, c = *col;
    if (!step(s, axis, *v, d, &next))
        return;
    if (has_value(s, r, c) && has_value(s, axis ? r : next, axis ? next : c)) {
        while (step(s, axis, next, d, &n2) && has_value(s, axis ? r : n2, axis ? n2 : c))
            next = n2;
        *v = next;
        return;
    }
    if (next_value(s, axis, d, r, c, &found)) {
        *v = found;
        return;
    }
    /* none: the sheet's edge, the last row or column showing */
    if (d > 0) {
        unsigned last = axis ? OC_MAX_COLS - 1 : OC_MAX_ROWS - 1;
        while (last > *v && !(axis ? col_shows(s, last) : row_shows(s, last)))
            last--;
        *v = last;
    } else {
        unsigned first = 0;
        while (first < *v && !(axis ? col_shows(s, first) : row_shows(s, first)))
            first++;
        *v = first;
    }
}

/* The point keys move: the focus when extending, else the active cell. */
static void key_point(oc_core *core, int extend, unsigned *row, unsigned *col)
{
    const oc_sheetstate *st = oc_state_const(core);
    *row = extend ? st->sel.focus_row : st->sel.row;
    *col = extend ? st->sel.focus_col : st->sel.col;
}

static void go(oc_core *core, unsigned row, unsigned col, int extend)
{
    oc_sheetstate *st = oc_state(core);
    if (extend)
        extend_to(core, row, col);
    else
        oc_collapse_to(core, row, col);
    oc_view_show_cell(core, extend ? st->sel.focus_row : st->sel.row, extend ? st->sel.focus_col : st->sel.col);
}

static void arrow(oc_core *core, int axis, int d, unsigned mods)
{
    const owf_sheet *s = oc_sheet(core);
    int extend = (mods & OC_MOD_SHIFT) != 0;
    unsigned row, col, v;
    key_point(core, extend, &row, &col);
    if (mods & OC_MOD_CTRL) {
        data_edge(s, axis, d, &row, &col);
    } else {
        /* out of a merged cell from its far edge */
        const owf_range *m = !extend ? oc_merge_at(s, row, col) : NULL;
        if (m) {
            if (axis == 0)
                row = d > 0 ? m->last_row : m->first_row;
            else
                col = d > 0 ? m->last_col : m->first_col;
        }
        if (step(s, axis, axis ? col : row, d, &v)) {
            if (axis)
                col = v;
            else
                row = v;
        }
    }
    go(core, row, col, extend);
}

/* Tab and Return inside a selection of more than one cell: the active
 * cell moves through it (across then down for Tab, down then across for
 * Return), from one range to the next. */
static void move_in_selection(oc_core *core, int across, int d)
{
    oc_sheetstate *st = oc_state(core);
    const owf_sheet *s = oc_sheet(core);
    int k, guard;
    unsigned row = st->sel.row, col = st->sel.col;
    owf_range r;
    for (k = st->sel.nranges - 1; k > 0; k--) {
        const owf_range *q = &st->sel.ranges[k];
        if (row >= q->first_row && row <= q->last_row && col >= q->first_col && col <= q->last_col)
            break;
    }
    for (guard = 0; guard < 4096; guard++) {
        oc_range_clip(core, &st->sel.ranges[k], &r);
        if (r.first_row > r.last_row || r.first_col > r.last_col)
            r = st->sel.ranges[k];
        if (across) {
            if (d > 0 ? col < r.last_col : col > r.first_col)
                col = (unsigned)((long)col + d);
            else if (d > 0 ? row < r.last_row : row > r.first_row) {
                row = (unsigned)((long)row + d);
                col = d > 0 ? r.first_col : r.last_col;
            } else
                goto next_range;
        } else {
            if (d > 0 ? row < r.last_row : row > r.first_row)
                row = (unsigned)((long)row + d);
            else if (d > 0 ? col < r.last_col : col > r.first_col) {
                col = (unsigned)((long)col + d);
                row = d > 0 ? r.first_row : r.last_row;
            } else
                goto next_range;
        }
        if (!row_shows(s, row) || !col_shows(s, col))
            continue;
        {
            const owf_range *m = oc_merge_at(s, row, col);
            if (m && (m->first_row != row || m->first_col != col))
                continue;
        }
        break;
    next_range:
        k = (k + (d > 0 ? 1 : st->sel.nranges - 1)) % st->sel.nranges;
        oc_range_clip(core, &st->sel.ranges[k], &r);
        if (r.first_row > r.last_row || r.first_col > r.last_col)
            r = st->sel.ranges[k];
        row = d > 0 ? r.first_row : r.last_row;
        col = d > 0 ? r.first_col : r.last_col;
        break;
    }
    st->sel.row = row;
    st->sel.col = col;
    oc_view_show_cell(core, row, col);
}

static int selection_is_one(const oc_core *core)
{
    const oc_sheetstate *st = oc_state_const(core);
    const owf_range *r = &st->sel.ranges[0];
    const owf_range *m;
    if (st->sel.nranges != 1)
        return 0;
    if (r->first_row == r->last_row && r->first_col == r->last_col)
        return 1;
    m = oc_merge_at(oc_sheet(core), r->first_row, r->first_col);
    return m && m->first_row == r->first_row && m->last_row == r->last_row && m->first_col == r->first_col &&
           m->last_col == r->last_col;
}

static void tab_key(oc_core *core, unsigned mods)
{
    oc_sheetstate *st = oc_state(core);
    int back = (mods & OC_MOD_SHIFT) != 0;
    long tab_col;
    unsigned v;
    if (!selection_is_one(core)) {
        move_in_selection(core, 1, back ? -1 : 1);
        return;
    }
    tab_col = st->tab_col >= 0 ? st->tab_col : (long)st->sel.col;
    arrow(core, 1, back ? -1 : 1, 0);
    st = oc_state(core);
    st->tab_col = back ? -1 : tab_col;
    (void)v;
}

static void return_key(oc_core *core, unsigned mods)
{
    oc_sheetstate *st = oc_state(core);
    int back = (mods & OC_MOD_SHIFT) != 0;
    unsigned row = st->sel.row, v;
    if (!selection_is_one(core)) {
        move_in_selection(core, 0, back ? -1 : 1);
        return;
    }
    if (!back && st->tab_col >= 0) {
        const owf_range *m = oc_merge_at(oc_sheet(core), row, st->sel.col);
        if (m)
            row = m->last_row;
        if (step(oc_sheet(core), 0, row, 1, &v))
            row = v;
        go(core, row, (unsigned)st->tab_col, 0);
        return;
    }
    arrow(core, 0, back ? -1 : 1, 0);
}

static void home_key(oc_core *core, unsigned mods)
{
    const owf_sheet *s = oc_sheet(core);
    int extend = (mods & OC_MOD_SHIFT) != 0;
    unsigned row, col;
    key_point(core, extend, &row, &col);
    col = s->freeze_cols && col > s->freeze_cols ? s->freeze_cols : 0;
    if (mods & OC_MOD_CTRL)
        row = s->freeze_rows && row > s->freeze_rows ? s->freeze_rows : 0;
    while (col + 1 < OC_MAX_COLS && !col_shows(s, col))
        col++;
    while (row + 1 < OC_MAX_ROWS && !row_shows(s, row))
        row++;
    go(core, row, col, extend);
}

static void end_key(oc_core *core, unsigned mods)
{
    const owf_sheet *s = oc_sheet(core);
    int extend = (mods & OC_MOD_SHIFT) != 0, i;
    unsigned row, col, rows, cols;
    key_point(core, extend, &row, &col);
    if (mods & OC_MOD_CTRL) {                            /* the used part's last cell */
        oc_used_range(s, &rows, &cols);
        row = rows ? rows - 1 : 0;
        col = cols ? cols - 1 : 0;
    } else {                                             /* the row's last cell with a value (LibreOffice) */
        int found = 0;
        for (i = place(s, row + 1, 0) - 1; i >= 0 && s->cells[i].row == row; i--)
            if (!oc_cell_empty(&s->cells[i])) {
                col = s->cells[i].col;
                found = 1;
                break;
            }
        if (!found)
            return;
    }
    go(core, row, col, extend);
}

static void page_key(oc_core *core, int d, unsigned mods)
{
    oc_sheetstate *st = oc_state(core);
    const owf_sheet *s = oc_sheet(core);
    int extend = (mods & OC_MOD_SHIFT) != 0, axis = (mods & OC_MOD_ALT) ? 1 : 0;
    int n = axis ? oc_cols_showing(core, 1) : oc_rows_showing(core, 1), i;
    unsigned row, col, *v, *top, freeze, x;
    if (n < 1)
        n = 1;
    key_point(core, extend, &row, &col);
    v = axis ? &col : &row;
    top = axis ? &st->left : &st->top;
    freeze = axis ? s->freeze_cols : s->freeze_rows;
    for (i = 0; i < n && step(s, axis, *v, d, &x); i++)
        *v = x;
    for (i = 0; i < n && step(s, axis, *top, d, &x) && x >= freeze; i++)
        *top = x;
    go(core, row, col, extend);
}

int oc_core_key(oc_core *core, int key, unsigned mods)
{
    if (core->edit_mode)
        return oc_edit_key(core, key, mods);
    return oc_nav_key(core, key, mods);
}

/* A key, not editing. */
int oc_nav_key(oc_core *core, int key, unsigned mods)
{
    switch (key) {
    case OC_KEY_LEFT:
        arrow(core, 1, -1, mods);
        break;
    case OC_KEY_RIGHT:
        arrow(core, 1, 1, mods);
        break;
    case OC_KEY_UP:
        arrow(core, 0, -1, mods);
        break;
    case OC_KEY_DOWN:
        arrow(core, 0, 1, mods);
        break;
    case OC_KEY_HOME:
        home_key(core, mods);
        break;
    case OC_KEY_END:
        end_key(core, mods);
        break;
    case OC_KEY_PAGEUP:
    case OC_KEY_PAGEDOWN:
        if (mods & OC_MOD_CTRL) {
            int s = core->sheet + (key == OC_KEY_PAGEUP ? -1 : 1);
            while (s >= 0 && s < core->book->nsheets && core->book->sheets[s].hidden)
                s += key == OC_KEY_PAGEUP ? -1 : 1;
            return oc_core_set_sheet(core, s);
        }
        page_key(core, key == OC_KEY_PAGEUP ? -1 : 1, mods);
        break;
    case OC_KEY_TAB:
        tab_key(core, mods);
        break;
    case OC_KEY_RETURN:
        return_key(core, mods);
        break;
    case OC_KEY_DELETE:
        return oc_core_clear(core, OC_CLEAR_CONTENTS);
    case OC_KEY_BACKSPACE:
        return oc_edit_begin(core, 0);
    case OC_KEY_F2:
        return oc_edit_begin(core, 1);
    default:
        return OWF_ERR_UNSUPPORTED;
    }
    return OWF_OK;
}

/* ---- What the cells hold, as text ---- */

int oc_core_cell_text(const oc_core *core, int sheet, unsigned row, unsigned col, char *out, size_t size)
{
    const owf_cell *c = oc_find(core, sheet, row, col);
    if (size)
        out[0] = 0;
    if (!c || !size)
        return 0;
    return owf_calc_format(core->book, c, out, size);
}

/* A number format's kind: 'd' dates and times, '%' percentages, 0 others. */
static int format_kind(const char *f)
{
    int quote = 0;
    if (!f)
        return 0;
    for (; *f; f++) {
        if (*f == '"')
            quote = !quote;
        else if (quote)
            continue;
        else if (*f == '\\' && f[1])
            f++;
        else if (*f == '[') {                            /* [Red], [$-409]: not the format's letters */
            while (*f && *f != ']')
                f++;
            if (!*f)
                break;
        } else if (*f == '%')
            return '%';
        else if (strchr("dDmMyYhHsS", *f) && !(f[0] == 'E' || f[0] == 'e'))
            return 'd';
        else if (*f == ';')
            break;
    }
    return 0;
}

/* Would typed input read this text as something other than text? Then it
 * needs an apostrophe in front. */
static int text_needs_quote(const char *t)
{
    static const char *const words[] = { "TRUE", "FALSE", NULL };
    int i;
    if (!*t)
        return 0;
    if (strchr("=+-'#(.$0123456789", *t) || (unsigned char)*t >= 0x80)
        return 1;
    for (i = 0; words[i]; i++)
        if (same_name(t, words[i]))
            return 1;
    return 0;
}

static int input_into(const oc_core *core, const owf_cell *c, char *out, size_t size)
{
    char num[40];
    const char *e;
    if (!c)
        return snprintf(out, size, "%s", "");
    if (c->formula)
        return snprintf(out, size, "=%s", c->formula);
    switch (c->type) {
    case OWF_VALUE_NUMBER: {
        const owf_cellstyle *st = c->style > 0 && c->style < core->book->nstyles ? &core->book->styles[c->style] : NULL;
        const char *f = st && st->number_format > 0 && st->number_format < core->book->nnumfmts ?
                            core->book->numfmts[st->number_format] : NULL;
        int kind = format_kind(f);
        if (kind == 'd')                                 /* a date reads back as typed */
            return owf_calc_format(core->book, c, out, size);
        if (kind == '%') {
            owf_number_text(c->number * 100, num);
            return snprintf(out, size, "%s%%", num);
        }
        owf_number_text(c->number, num);
        return snprintf(out, size, "%s", num);
    }
    case OWF_VALUE_BOOL:
        return snprintf(out, size, "%s", c->number ? "TRUE" : "FALSE");
    case OWF_VALUE_ERROR:
        e = owf_error_name(c->error);
        return snprintf(out, size, "%s", e ? e : "#N/A");
    case OWF_VALUE_TEXT:
        return snprintf(out, size, "%s%s", text_needs_quote(c->text) ? "'" : "", c->text ? c->text : "");
    case OWF_VALUE_EMPTY:
        break;
    }
    return snprintf(out, size, "%s", "");
}

char *oc_core_cell_input(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    return oc_cell_input(core, sheet, row, col);
}

/* What a cell holds as one would type it: owf_malloc'd. */
char *oc_cell_input(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    const owf_cell *c = oc_find(core, sheet, row, col);
    size_t n = 64;
    char *out;
    if (c && c->formula)
        n += strlen(c->formula);
    if (c && c->text)
        n += strlen(c->text);
    out = owf_malloc(n);
    if (out)
        input_into(core, c, out, n);
    return out;
}

const char *oc_core_input_text(oc_core *core)
{
    const oc_sheetstate *st;
    if (core->edit_mode)
        return core->edit;
    st = oc_state_const(core);
    owf_free(core->input);
    core->input = oc_cell_input(core, core->sheet, st->sel.row, st->sel.col);
    return core->input ? core->input : "";
}

/* ---- The status line ---- */

void oc_core_status(oc_core *core, char *out, size_t size)
{
    const oc_sheetstate *st = oc_state_const(core);
    unsigned state = owf_calc_state(core->calc, core->sheet, st->sel.row, st->sel.col);
    char refs[OC_MAX_RANGES * 24 + 16], f[OC_MAX_RANGES * 24 + 32], sum[40], avg[40], cnt[40];
    owf_cell r;
    int i;
    size_t n = 0;
    out[0] = 0;
    if (state & OWF_CALC_CIRCULAR) {
        snprintf(out, size, "Circular reference");
        return;
    }
    if (state & OWF_CALC_BAD) {
        snprintf(out, size, "The formula has an error");
        return;
    }
    if (owf_calc_circular_count(core->calc) > 0) {
        snprintf(out, size, "Circular references: %d cells", owf_calc_circular_count(core->calc));
        return;
    }
    if (selection_is_one(core))
        return;
    for (i = 0; i < st->sel.nranges; i++) {
        if (i)
            refs[n++] = ',';
        range_name(&st->sel.ranges[i], refs + n);
        n += strlen(refs + n);
    }
    refs[n] = 0;
    sum[0] = avg[0] = cnt[0] = 0;
    snprintf(f, sizeof f, "COUNT(%s)", refs);
    if (owf_calc_evaluate(core->calc, core->sheet, st->sel.row, st->sel.col, f, &r) == OWF_OK) {
        if (r.type == OWF_VALUE_NUMBER && r.number > 0) {
            owf_number_text(r.number, cnt);
            snprintf(f, sizeof f, "SUM(%s)", refs);
            owf_free(r.text);
            if (owf_calc_evaluate(core->calc, core->sheet, st->sel.row, st->sel.col, f, &r) == OWF_OK &&
                r.type == OWF_VALUE_NUMBER)
                owf_number_text(r.number, sum);
            snprintf(f, sizeof f, "AVERAGE(%s)", refs);
            owf_free(r.text);
            if (owf_calc_evaluate(core->calc, core->sheet, st->sel.row, st->sel.col, f, &r) == OWF_OK &&
                r.type == OWF_VALUE_NUMBER)
                owf_number_text(r.number, avg);
        }
        owf_free(r.text);
    }
    if (sum[0])
        snprintf(out, size, "Sum %s  Average %s  Count %s", sum, avg, cnt);
}

/* ---- The sheet tabs ---- */

/* The sheet after the last whose tab fits room pixels from sheet first (at least one tab). */
static int tabs_fit(const int *width, int n, int room, int first)
{
    int k, x = 0, shown = 0;
    for (k = first; k < n; k++) {
        if (width[k] <= 0)
            continue;
        if (x + width[k] > room && shown)
            break;
        x += width[k];
        shown++;
    }
    return k;
}

int oc_tab_strip(const int *width, int n, int avail, int arrows, int current, int *first, int *scroll)
{
    int k, total = 0, room, end;
    for (k = 0; k < n; k++)
        total += width[k] > 0 ? width[k] : 0;
    if (total <= avail) {
        *first = 0;
        *scroll = 0;
        return n;
    }
    *scroll = 1;
    room = avail - arrows;
    if (*first < 0)
        *first = 0;
    if (*first >= n)
        *first = n > 0 ? n - 1 : 0;
    if (current >= 0 && current < n && current < *first)
        *first = current;
    end = tabs_fit(width, n, room, *first);
    while (current >= 0 && current < n && current >= end && *first < current)
        end = tabs_fit(width, n, room, ++*first);
    /* room left at the end: earlier tabs come back */
    while (*first > 0 && end >= n) {
        int prev = *first - 1, e = tabs_fit(width, n, room, prev);
        if (e < n || (current >= 0 && current >= e))
            break;
        *first = prev;
        end = e;
    }
    return end;
}

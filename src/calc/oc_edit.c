/*
 * OpenCalc's core: editing a cell. The text being typed and its caret, the
 * keys while typing (Excel's "enter" and "edit" modes, and pointing at
 * cells with the arrows in a formula), and committing it through the
 * engine.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void oc_edit_free(oc_core *core)
{
    free(core->edit);
    core->edit = NULL;
    core->edit_len = core->edit_cap = core->caret = 0;
    core->edit_mode = OC_EDIT_NONE;
    core->point = 0;
}

static int edit_room(oc_core *core, size_t more)
{
    size_t want = core->edit_len + more + 1;
    char *p;
    if (want <= core->edit_cap)
        return 1;
    want = want < 64 ? 64 : want * 2;
    p = realloc(core->edit, want);
    if (!p)
        return 0;
    core->edit = p;
    core->edit_cap = want;
    return 1;
}

static int edit_replace(oc_core *core, size_t from, size_t to, const char *s, size_t n)
{
    if (from > core->edit_len)
        from = core->edit_len;
    if (to > core->edit_len)
        to = core->edit_len;
    if (to < from)
        to = from;
    if (n > to - from && !edit_room(core, n - (to - from)))
        return OWF_ERR_MEMORY;
    memmove(core->edit + from + n, core->edit + to, core->edit_len - to + 1);
    memcpy(core->edit + from, s, n);
    core->edit_len = core->edit_len - (to - from) + n;
    core->caret = from + n;
    return OWF_OK;
}

int oc_core_editing(const oc_core *core)
{
    return core->edit_mode;
}

int oc_edit_begin(oc_core *core, int keep)
{
    const oc_sheetstate *st = oc_state_const(core);
    char *text = NULL;
    size_t n = 0;
    if (core->edit_mode)
        return OWF_OK;
    if (keep) {
        text = oc_cell_input(core, core->sheet, st->sel.row, st->sel.col);
        if (!text)
            return OWF_ERR_MEMORY;
        n = strlen(text);
    }
    core->edit_len = 0;
    if (!edit_room(core, n)) {
        owf_free(text);
        return OWF_ERR_MEMORY;
    }
    if (n)
        memcpy(core->edit, text, n);
    core->edit[n] = 0;
    core->edit_len = core->caret = n;
    owf_free(text);
    core->edit_mode = keep ? OC_EDIT_EDIT : OC_EDIT_ENTER;
    core->point = 0;
    oc_view_show_cell(core, st->sel.row, st->sel.col);
    return OWF_OK;
}

int oc_edit_type(oc_core *core, const char *utf8, size_t length)
{
    int rc;
    if (!core->edit_mode && (rc = oc_edit_begin(core, 0)) != OWF_OK)
        return rc;
    core->point = 0;
    return edit_replace(core, core->caret, core->caret, utf8, length);
}

int oc_edit_set(oc_core *core, const char *utf8, size_t caret)
{
    int rc;
    size_t n = strlen(utf8);
    if (!core->edit_mode && (rc = oc_edit_begin(core, 1)) != OWF_OK)
        return rc;
    core->point = 0;
    if ((rc = edit_replace(core, 0, core->edit_len, utf8, n)) != OWF_OK)
        return rc;
    core->caret = caret > n ? n : caret;
    return OWF_OK;
}

const char *oc_edit_text(const oc_core *core, size_t *caret)
{
    if (!core->edit_mode)
        return NULL;
    if (caret)
        *caret = core->caret;
    return core->edit;
}

void oc_edit_cancel(oc_core *core)
{
    oc_edit_free(core);
}

/* UTF-8: the start of the character before or after a byte offset. */
static size_t prev_char(const char *s, size_t at)
{
    if (!at)
        return 0;
    at--;
    while (at && ((unsigned char)s[at] & 0xC0) == 0x80)
        at--;
    return at;
}

static size_t next_char(const char *s, size_t len, size_t at)
{
    if (at >= len)
        return len;
    at++;
    while (at < len && ((unsigned char)s[at] & 0xC0) == 0x80)
        at++;
    return at;
}

int oc_edit_wants_ref(const oc_core *core)
{
    size_t i;
    if (!core->edit_mode || !core->edit_len || core->edit[0] != '=')
        return 0;
    if (core->point)
        return 1;
    for (i = core->caret; i > 0 && core->edit[i - 1] == ' '; i--)
        ;
    if (!i)
        return 0;
    {
        /* after an operator, an opening bracket or a separator, with nothing after the caret but spaces
           or a closing bracket */
        size_t j;
        for (j = core->caret; j < core->edit_len; j++)
            if (core->edit[j] != ' ' && core->edit[j] != ')')
                return 0;
    }
    return strchr("=(,;+-*/^&<>:", core->edit[i - 1]) != NULL;
}

int oc_edit_insert_ref(oc_core *core, const owf_range *range)
{
    char name[40], a[16], b[16];
    size_t from, to;
    int rc;
    if (!core->edit_mode)
        return OWF_ERR_UNSUPPORTED;
    if (range->first_row == range->last_row && range->first_col == range->last_col)
        oc_cell_name(range->first_row, range->first_col, name);
    else {
        oc_cell_name(range->first_row, range->first_col, a);
        oc_cell_name(range->last_row, range->last_col, b);
        snprintf(name, sizeof name, "%s:%s", a, b);
    }
    from = core->point ? core->point_start : core->caret;
    to = core->point ? core->point_end : core->caret;
    if ((rc = edit_replace(core, from, to, name, strlen(name))) != OWF_OK)
        return rc;
    core->point = 1;
    core->point_start = from;
    core->point_end = from + strlen(name);
    core->point_row = range->first_row;
    core->point_col = range->first_col;
    return OWF_OK;
}

/* The arrows while pointing: the reference moves. */
static int point_move(oc_core *core, int dr, int dc)
{
    const oc_sheetstate *st = oc_state_const(core);
    owf_range r;
    long row = core->point ? (long)core->point_row : (long)st->sel.row;
    long col = core->point ? (long)core->point_col : (long)st->sel.col;
    row += dr;
    col += dc;
    if (row < 0)
        row = 0;
    if (col < 0)
        col = 0;
    if (row >= (long)OC_MAX_ROWS)
        row = OC_MAX_ROWS - 1;
    if (col >= (long)OC_MAX_COLS)
        col = OC_MAX_COLS - 1;
    r.first_row = r.last_row = (unsigned)row;
    r.first_col = r.last_col = (unsigned)col;
    oc_view_show_cell(core, (unsigned)row, (unsigned)col);
    return oc_edit_insert_ref(core, &r);
}

/* The text as typed into every cell of the selection (Ctrl+Return), a
 * formula moved for each from the active cell. */
static int commit_fill(oc_core *core, const char *text, int *bad)
{
    oc_sheetstate *st = oc_state(core);
    oc_undo *u = oc_undo_new(core, OC_UNDO_CELLS);
    int k, rc = OWF_OK;
    long total = 0;
    owf_range r;
    for (k = 0; k < st->sel.nranges; k++)
        if (oc_range_clip(core, &st->sel.ranges[k], &r))
            total += (long)(r.last_row - r.first_row + 1) * (long)(r.last_col - r.first_col + 1);
    if (total > OC_MAX_RANGE_CELLS) {
        oc_undo_free(u);
        return OWF_ERR_UNSUPPORTED;
    }
    for (k = 0; u && k < st->sel.nranges; k++)
        if (oc_range_clip(core, &st->sel.ranges[k], &r) && oc_undo_save_range(core, u, core->sheet, &r, 0) != OWF_OK) {
            oc_undo_free(u);
            u = NULL;
        }
    oc_undo_push(core, u);
    for (k = 0; k < st->sel.nranges && rc != OWF_ERR_MEMORY; k++) {
        unsigned row, col;
        if (!oc_range_clip(core, &st->sel.ranges[k], &r))
            continue;
        for (row = r.first_row; row <= r.last_row && rc != OWF_ERR_MEMORY; row++)
            for (col = r.first_col; col <= r.last_col; col++) {
                char *moved = NULL, *in;
                int one;
                if (text[0] == '=' && (row != st->sel.row || col != st->sel.col)) {
                    moved = owf_calc_move_formula(text, (int)((long)row - (long)st->sel.row),
                                                  (int)((long)col - (long)st->sel.col));
                    if (!moved) {
                        rc = OWF_ERR_MEMORY;
                        break;
                    }
                }
                in = moved ? moved : (char *)text;
                if (moved && in[0] != '=') {            /* given back without its "=" */
                    size_t n = strlen(moved);
                    char *with = owf_malloc(n + 2);
                    if (!with) {
                        owf_free(moved);
                        rc = OWF_ERR_MEMORY;
                        break;
                    }
                    with[0] = '=';
                    memcpy(with + 1, moved, n + 1);
                    owf_free(moved);
                    in = moved = with;
                }
                one = owf_calc_enter(core->calc, core->sheet, row, col, in);
                owf_free(moved);
                if (one == OWF_ERR_FORMAT)
                    *bad = 1;
                else if (one != OWF_OK) {
                    rc = one;
                    break;
                }
            }
    }
    return rc;
}

int oc_edit_commit(oc_core *core, int move)
{
    oc_sheetstate *st = oc_state(core);
    char *text;
    int rc, bad = 0;
    if (!core->edit_mode)
        return OWF_OK;
    text = core->edit;
    core->edit = NULL;
    oc_edit_free(core);
    if (move == OC_MOVE_FILL) {
        rc = commit_fill(core, text, &bad);
    } else {
        owf_range r;
        oc_undo *u = oc_undo_new(core, OC_UNDO_CELLS);
        r.first_row = r.last_row = st->sel.row;
        r.first_col = r.last_col = st->sel.col;
        if (u && oc_undo_save_range(core, u, core->sheet, &r, 0) != OWF_OK) {
            oc_undo_free(u);
            u = NULL;
        }
        rc = owf_calc_enter(core->calc, core->sheet, st->sel.row, st->sel.col, text);
        if (rc == OWF_ERR_MEMORY) {
            oc_undo_free(u);
        } else {
            oc_undo_push(core, u);
            if (rc == OWF_ERR_FORMAT) {
                bad = 1;
                rc = OWF_OK;
            }
        }
    }
    if (rc == OWF_ERR_MEMORY) {                          /* nothing changed: keep typing */
        core->edit = text;
        core->edit_len = core->caret = strlen(text);
        core->edit_cap = core->edit_len + 1;
        core->edit_mode = OC_EDIT_ENTER;
        return rc;
    }
    free(text);
    oc_changed(core);
    if (oc_recalc(core, 0) == OWF_ERR_MEMORY)
        rc = OWF_ERR_MEMORY;
    switch (move) {
    case OC_MOVE_DOWN:
        oc_nav_key(core, OC_KEY_RETURN, 0);
        break;
    case OC_MOVE_UP:
        oc_nav_key(core, OC_KEY_RETURN, OC_MOD_SHIFT);
        break;
    case OC_MOVE_RIGHT:
        oc_nav_key(core, OC_KEY_TAB, 0);
        break;
    case OC_MOVE_LEFT:
        oc_nav_key(core, OC_KEY_TAB, OC_MOD_SHIFT);
        break;
    }
    if (rc == OWF_OK && bad)
        rc = OWF_ERR_FORMAT;
    return rc;
}

int oc_edit_key(oc_core *core, int key, unsigned mods)
{
    size_t at;
    int enter = core->edit_mode == OC_EDIT_ENTER;
    if (!core->edit_mode)
        return oc_nav_key(core, key, mods);
    switch (key) {
    case OC_KEY_ESCAPE:
        oc_edit_cancel(core);
        return OWF_OK;
    case OC_KEY_RETURN:
        if (mods & OC_MOD_ALT)                           /* a new line in the cell */
            return oc_edit_type(core, "\n", 1);
        return oc_edit_commit(core, (mods & OC_MOD_CTRL) ? OC_MOVE_FILL : (mods & OC_MOD_SHIFT) ? OC_MOVE_UP : OC_MOVE_DOWN);
    case OC_KEY_TAB:
        return oc_edit_commit(core, (mods & OC_MOD_SHIFT) ? OC_MOVE_LEFT : OC_MOVE_RIGHT);
    case OC_KEY_F2:
        core->edit_mode = enter ? OC_EDIT_EDIT : OC_EDIT_ENTER;
        core->point = 0;
        return OWF_OK;
    case OC_KEY_LEFT:
    case OC_KEY_RIGHT:
    case OC_KEY_UP:
    case OC_KEY_DOWN:
        if (enter && oc_edit_wants_ref(core)) {
            int dr = key == OC_KEY_UP ? -1 : key == OC_KEY_DOWN ? 1 : 0;
            int dc = key == OC_KEY_LEFT ? -1 : key == OC_KEY_RIGHT ? 1 : 0;
            return point_move(core, dr, dc);
        }
        if (enter) {                                     /* commit, then the arrow moves */
            int rc = oc_edit_commit(core, OC_MOVE_NONE);
            if (rc == OWF_ERR_MEMORY)
                return rc;
            oc_nav_key(core, key, mods & ~(unsigned)OC_MOD_SHIFT);
            return rc;
        }
        core->point = 0;
        if (key == OC_KEY_LEFT)
            core->caret = prev_char(core->edit, core->caret);
        else if (key == OC_KEY_RIGHT)
            core->caret = next_char(core->edit, core->edit_len, core->caret);
        else
            core->caret = key == OC_KEY_UP ? 0 : core->edit_len;
        return OWF_OK;
    case OC_KEY_HOME:
        core->point = 0;
        core->caret = 0;
        return OWF_OK;
    case OC_KEY_END:
        core->point = 0;
        core->caret = core->edit_len;
        return OWF_OK;
    case OC_KEY_BACKSPACE:
        core->point = 0;
        if (!core->caret)
            return OWF_OK;
        at = prev_char(core->edit, core->caret);
        return edit_replace(core, at, core->caret, "", 0);
    case OC_KEY_DELETE:
        core->point = 0;
        at = next_char(core->edit, core->edit_len, core->caret);
        return edit_replace(core, core->caret, at, "", 0);
    case OC_KEY_PAGEUP:
    case OC_KEY_PAGEDOWN: {
        int rc = oc_edit_commit(core, OC_MOVE_NONE);
        if (rc != OWF_ERR_MEMORY)
            oc_nav_key(core, key, mods);
        return rc;
    }
    }
    return OWF_ERR_UNSUPPORTED;
}

int oc_core_set_input(oc_core *core, int sheet, unsigned row, unsigned col, const char *input)
{
    owf_range r;
    oc_undo *u;
    int rc;
    if (sheet < 0 || sheet >= core->book->nsheets || row >= OC_MAX_ROWS || col >= OC_MAX_COLS)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit(core, OC_MOVE_NONE);
    u = oc_undo_new(core, OC_UNDO_CELLS);
    r.first_row = r.last_row = row;
    r.first_col = r.last_col = col;
    if (u && oc_undo_save_range(core, u, sheet, &r, 0) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    rc = owf_calc_enter(core->calc, sheet, row, col, input);
    if (rc == OWF_ERR_MEMORY || rc == OWF_ERR_UNSUPPORTED) {
        oc_undo_free(u);
        return rc;
    }
    oc_undo_push(core, u);
    oc_changed(core);
    if (oc_recalc(core, 0) == OWF_ERR_MEMORY)
        return OWF_ERR_MEMORY;
    return rc;
}

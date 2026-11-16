/*
 * GFOpenCalc's core: tracked changes (Edit > Track Changes). While the book records them, each command's Undo
 * step is compared, when the command has done (oc_changed), with the book as it is now: the cells whose content
 * changed, or what the command said it did (rows or columns inserted or deleted, a sheet added or renamed), become
 * the book's changes (owf_change). Each Undo step keeps the list as it was, so Undo takes a change back with it.
 * The places in the list are the book's as it is now: inserting and deleting move them.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- The list ---- */

static void change_clear(owf_change *c)
{
    int j;
    owf_free(c->author);
    owf_free(c->date);
    owf_free(c->before);
    owf_free(c->after);
    for (j = 0; j < c->ncells; j++)
        owf_free(c->cells[j].input);
    owf_free(c->cells);
    oc_track_free_list(c->children, c->nchildren);
    memset(c, 0, sizeof *c);
}

void oc_track_free_list(owf_change *changes, int n)
{
    int i;
    for (i = 0; i < n; i++)
        change_clear(&changes[i]);
    owf_free(changes);
}

static char *dup_or_null(const char *s, int *lost)
{
    char *d;
    if (!s)
        return NULL;
    if (!(d = oc_strdup(s)))
        *lost = 1;
    return d;
}

static int change_copy(owf_change *d, const owf_change *s);

static owf_change *list_copy(const owf_change *src, int n, int *lost)
{
    owf_change *d;
    int i;
    if (!n)
        return NULL;
    if (!(d = owf_malloc((size_t)n * sizeof *d))) {
        *lost = 1;
        return NULL;
    }
    memset(d, 0, (size_t)n * sizeof *d);
    for (i = 0; i < n; i++)
        if (!change_copy(&d[i], &src[i]))
            *lost = 1;
    return d;
}

static int change_copy(owf_change *d, const owf_change *s)
{
    int lost = 0, j;
    *d = *s;
    d->author = dup_or_null(s->author, &lost);
    d->date = dup_or_null(s->date, &lost);
    d->before = dup_or_null(s->before, &lost);
    d->after = dup_or_null(s->after, &lost);
    d->cells = NULL;
    d->ncells = 0;
    if (s->ncells && (d->cells = owf_malloc((size_t)s->ncells * sizeof *d->cells)))
        for (j = 0; j < s->ncells; j++) {
            d->cells[j] = s->cells[j];
            if (!(d->cells[j].input = oc_strdup(s->cells[j].input))) {
                lost = 1;
                break;
            }
            d->ncells++;
        }
    else if (s->ncells)
        lost = 1;
    d->children = list_copy(s->children, s->nchildren, &lost);
    d->nchildren = d->children ? s->nchildren : 0;
    return !lost;
}

int oc_track_save(oc_core *core, oc_undo *u)
{
    const owf_book *b = core->book;
    int lost = 0, i;
    u->track_pending = b->track_changes;
    if (!b->nchanges)
        return OWF_OK;
    u->changes = list_copy(b->changes, b->nchanges, &lost);
    u->nchanges = u->changes ? b->nchanges : 0;
    if (lost) {
        oc_track_free_list(u->changes, u->nchanges);
        u->changes = NULL;
        u->nchanges = 0;
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < b->nchanges; i++)
        u->bytes += 96 + (long)b->changes[i].ncells * 32;
    return OWF_OK;
}

void oc_track_restore(oc_core *core, oc_undo *u)
{
    owf_book *b = core->book;
    oc_track_free_list(b->changes, b->nchanges);
    b->changes = u->changes;
    b->nchanges = b->capchanges = u->nchanges;
    u->changes = NULL;
    u->nchanges = 0;
}

void oc_track_free(oc_core *core)
{
    owf_free(core->track_author);
    core->track_author = NULL;
    oc_track_hint(core, OC_HINT_NONE, 0, 0, 0, 0, NULL);
}

void oc_track_hint(oc_core *core, int kind, int sheet, int axis, unsigned at, unsigned count, const char *old_name)
{
    owf_free(core->hint.old_name);
    core->hint.kind = kind;
    core->hint.sheet = sheet;
    core->hint.axis = axis;
    core->hint.at = at;
    core->hint.count = count;
    core->hint.old_name = old_name ? oc_strdup(old_name) : NULL;
}

/* The highest number in a list, its deletions' changes too (a new change takes the next). */
static int top_id(const owf_change *list, int n)
{
    int i, id = 0, k;
    for (i = 0; i < n; i++) {
        if (list[i].id > id)
            id = list[i].id;
        if ((k = top_id(list[i].children, list[i].nchildren)) > id)
            id = k;
    }
    return id;
}

/* A new change at the end of the list, all zero but its number, author and date; NULL when memory runs out. */
static owf_change *change_add(oc_core *core, int kind, int sheet)
{
    owf_book *b = core->book;
    owf_change *c;
    char when[32];
    int id;
    if (b->nchanges == b->capchanges) {
        int cap = b->capchanges ? b->capchanges * 2 : 16;
        owf_change *p = owf_realloc(b->changes, (size_t)cap * sizeof *p);
        if (!p)
            return NULL;
        b->changes = p;
        b->capchanges = cap;
    }
    id = top_id(b->changes, b->nchanges);
    c = &b->changes[b->nchanges];
    memset(c, 0, sizeof *c);
    c->kind = kind;
    c->sheet = sheet;
    c->id = id + 1;
    when[0] = 0;
    if (core->track_clock)
        core->track_clock(core->track_clock_user, when, sizeof when);
    else {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        if (tm)
            strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%S", tm);
    }
    if ((core->track_author && !(c->author = oc_strdup(core->track_author))) ||
        (when[0] && !(c->date = oc_strdup(when)))) {
        change_clear(c);
        return NULL;
    }
    b->nchanges++;
    return c;
}

static void change_remove(owf_book *b, int index, int free_it)
{
    if (free_it)
        change_clear(&b->changes[index]);
    memmove(&b->changes[index], &b->changes[index + 1], (size_t)(b->nchanges - index - 1) * sizeof *b->changes);
    b->nchanges--;
}

/* ---- Places ---- */

static int is_rows(int kind)
{
    return kind == OWF_CHANGE_INSERT_ROWS || kind == OWF_CHANGE_DELETE_ROWS;
}

static int is_cols(int kind)
{
    return kind == OWF_CHANGE_INSERT_COLS || kind == OWF_CHANGE_DELETE_COLS;
}

/* The change's place on an axis (0 rows, 1 columns), when it has one there. */
static unsigned *place(owf_change *c, int axis)
{
    if (c->kind == OWF_CHANGE_CELL)
        return axis ? &c->col : &c->row;
    if (!axis && is_rows(c->kind))
        return &c->row;
    if (axis && is_cols(c->kind))
        return &c->col;
    return NULL;
}

static void shift_list(owf_change *list, int n, int axis, unsigned at, unsigned count, int del);

/* Inside a deletion of the other axis, cells and changes keep their places on this one: they move too (count
 * inserted before at, or deleted from at when del: those in them stay at at). */
static void move_inside(owf_change *c, int axis, unsigned at, unsigned count, int del)
{
    int j;
    if (!(c->kind == OWF_CHANGE_DELETE_ROWS ? axis : c->kind == OWF_CHANGE_DELETE_COLS ? !axis : 0))
        return;
    for (j = 0; j < c->ncells; j++) {
        unsigned *p = axis ? &c->cells[j].col : &c->cells[j].row;
        if (!del && *p >= at)
            *p += count;
        else if (del && *p >= at)
            *p = *p >= at + count ? *p - count : at;
    }
    shift_list(c->children, c->nchildren, axis, at, count, del);
}

/* count rows (columns) inserted before at, or deleted from at (del), on a sheet: the changes after move down
 * (right), or up (left); those in deleted ones stay at at (oc_track's deleted() takes them first). */
static void shift_list(owf_change *list, int n, int axis, unsigned at, unsigned count, int del)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned *p;
        move_inside(&list[i], axis, at, count, del);
        if (!(p = place(&list[i], axis)))
            continue;
        if (!del && *p >= at)
            *p += count;
        else if (del && *p >= at)
            *p = *p >= at + count ? *p - count : at;
    }
}

static int grows_insertion(owf_book *b, int sheet, int axis, unsigned at, unsigned count)
{
    int i;
    for (i = b->nchanges - 1; i >= 0; i--) {
        owf_change *c = &b->changes[i];
        unsigned *p;
        if (c->sheet != sheet || c->kind != (axis ? OWF_CHANGE_INSERT_COLS : OWF_CHANGE_INSERT_ROWS))
            continue;
        p = place(c, axis);
        if (at > *p && at < *p + c->count) {
            c->count += count;
            return 1;
        }
    }
    return 0;
}

static void inserted(owf_book *b, int sheet, int axis, unsigned at, unsigned count)
{
    int i;
    for (i = 0; i < b->nchanges; i++)
        if (b->changes[i].sheet == sheet)
            shift_list(&b->changes[i], 1, axis, at, count, 0);
}

/* Rows (columns) at..at+count-1 of a sheet deleted: the changes in them go into *taken (their place counted from
 * at), when taken is given, else they go; those after move up. A change of inserted rows partly in them keeps the
 * rest. 0 when memory runs out (the list is as it was). */
static int deleted(owf_book *b, int sheet, int axis, unsigned at, unsigned count, owf_change **taken, int *ntaken)
{
    unsigned end = at + count;
    int i, j, n = 0;
    owf_change *t = NULL;
    if (taken) {
        for (i = 0; i < b->nchanges; i++) {
            unsigned *p = place(&b->changes[i], axis);
            if (b->changes[i].sheet == sheet && p && *p >= at && *p < end &&
                !(*p == at && (b->changes[i].kind == (axis ? OWF_CHANGE_DELETE_COLS : OWF_CHANGE_DELETE_ROWS))))
                n++;
        }
        if (n && !(t = owf_malloc((size_t)n * sizeof *t)))
            return 0;
    }
    for (i = j = 0; i < b->nchanges; i++) {
        owf_change *c = &b->changes[i];
        unsigned *p = place(c, axis);
        int deletion = c->kind == (axis ? OWF_CHANGE_DELETE_COLS : OWF_CHANGE_DELETE_ROWS);
        int insertion = c->kind == (axis ? OWF_CHANGE_INSERT_COLS : OWF_CHANGE_INSERT_ROWS);
        if (c->sheet == sheet)
            move_inside(c, axis, at, count, 1);
        if (c->sheet == sheet && p) {
            if (*p >= at && *p < end && !(deletion && *p == at)) {
                if (insertion && *p + c->count > end) {        /* the inserted rows past the deleted ones stay */
                    c->count = *p + c->count - end;
                    *p = at;
                } else {
                    if (t) {
                        t[*ntaken] = *c;
                        *place(&t[*ntaken], axis) -= at;
                        (*ntaken)++;
                    } else
                        change_clear(c);
                    continue;
                }
            } else if (*p >= end)
                *p -= count;
            else if (insertion && *p + c->count > at)         /* inserted rows that the deleted ones end */
                c->count -= (*p + c->count < end ? *p + c->count : end) - at;
        }
        b->changes[j++] = *c;
    }
    b->nchanges = j;
    if (taken)
        *taken = t;
    return 1;
}

/* ---- Recording ---- */

static int same_content(const owf_cell *a, const owf_cell *b)
{
    int ea = oc_cell_empty(a), eb = oc_cell_empty(b);
    if (ea || eb)
        return ea == eb;
    if (a->formula || b->formula)
        return a->formula && b->formula && !strcmp(a->formula, b->formula);
    if (a->type != b->type)
        return 0;
    switch (a->type) {
    case OWF_VALUE_TEXT:
        return !strcmp(a->text ? a->text : "", b->text ? b->text : "");
    case OWF_VALUE_ERROR:
        return a->error == b->error;
    default:
        return a->number == b->number;
    }
}

static int record_cell(oc_core *core, int sheet, unsigned row, unsigned col, const owf_cell *was, const owf_cell *now)
{
    owf_change *c;
    int lost = 0;
    if (same_content(was, now))
        return 1;
    if (!(c = change_add(core, OWF_CHANGE_CELL, sheet)))
        return 0;
    c->row = row;
    c->col = col;
    c->before = oc_cell_input_of(core, was, &lost);
    c->after = oc_cell_input_of(core, now, &lost);
    return !lost;
}

static int first_from(const owf_sheet *s, unsigned row)
{
    int lo = 0, hi = s->ncells;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s->cells[mid].row < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int in_range(const owf_cell *c, const owf_range *r)
{
    return c->row >= r->first_row && c->row <= r->last_row && c->col >= r->first_col && c->col <= r->last_col;
}

/* The cells of a range as they were (was, nwas: by row, then column) and as they are now, side by side. */
static int compare_range(oc_core *core, int sheet, const owf_cell *was, int nwas, const owf_range *r)
{
    const owf_sheet *s = &core->book->sheets[sheet];
    int i = first_from(s, r->first_row), j = 0;
    for (;;) {
        const owf_cell *a, *b;
        while (i < s->ncells && s->cells[i].row <= r->last_row && !in_range(&s->cells[i], r))
            i++;
        while (j < nwas && !in_range(&was[j], r))
            j++;
        a = i < s->ncells && s->cells[i].row <= r->last_row ? &s->cells[i] : NULL;
        b = j < nwas ? &was[j] : NULL;
        if (!a && !b)
            return 1;
        if (b && (!a || b->row < a->row || (b->row == a->row && b->col < a->col))) {
            if (!record_cell(core, sheet, b->row, b->col, b, NULL))
                return 0;
            j++;
        } else if (a && (!b || a->row < b->row || (a->row == b->row && a->col < b->col))) {
            if (!record_cell(core, sheet, a->row, a->col, NULL, a))
                return 0;
            i++;
        } else {
            if (!record_cell(core, sheet, a->row, a->col, b, a))
                return 0;
            i++;
            j++;
        }
    }
}

static void record_insert(oc_core *core, const oc_trackhint *h)
{
    owf_book *b = core->book;
    owf_change *c;
    int grew = grows_insertion(b, h->sheet, h->axis, h->at, h->count);
    inserted(b, h->sheet, h->axis, h->at, h->count);
    if (grew)
        return;                                      /* rows inserted into inserted rows: one change */
    if ((c = change_add(core, h->axis ? OWF_CHANGE_INSERT_COLS : OWF_CHANGE_INSERT_ROWS, h->sheet))) {
        *place(c, h->axis) = h->at;
        c->count = h->count;
    }
}

static void record_delete(oc_core *core, const oc_trackhint *h, const oc_undo *u)
{
    owf_book *b = core->book;
    owf_change *c, *taken = NULL;
    int ntaken = 0, lost = 0, i;
    unsigned end = h->at + h->count;
    if (!deleted(b, h->sheet, h->axis, h->at, h->count, &taken, &ntaken))
        return;
    if (!(c = change_add(core, h->axis ? OWF_CHANGE_DELETE_COLS : OWF_CHANGE_DELETE_ROWS, h->sheet))) {
        oc_track_free_list(taken, ntaken);
        return;
    }
    *place(c, h->axis) = h->at;
    c->count = h->count;
    c->children = taken;
    c->nchildren = ntaken;
    if ((i = top_id(taken, ntaken)) >= c->id)                   /* numbered past the ones it holds */
        c->id = i + 1;
    if (u && u->book && h->sheet < u->book->nsheets) {           /* what they held, from the step's book */
        const owf_sheet *s = &u->book->sheets[h->sheet];
        int n = 0;
        for (i = 0; i < s->ncells; i++) {
            unsigned p = h->axis ? s->cells[i].col : s->cells[i].row;
            if (p >= h->at && p < end && !oc_cell_empty(&s->cells[i]))
                n++;
        }
        if (n && !(c->cells = owf_malloc((size_t)n * sizeof *c->cells)))
            return;
        for (i = 0; i < s->ncells && c->ncells < n; i++) {
            const owf_cell *cell = &s->cells[i];
            unsigned p = h->axis ? cell->col : cell->row;
            owf_changecell *k;
            if (p < h->at || p >= end || oc_cell_empty(cell))
                continue;
            k = &c->cells[c->ncells];
            k->row = h->axis ? cell->row : cell->row - h->at;
            k->col = h->axis ? cell->col - h->at : cell->col;
            if (!(k->input = oc_cell_input_of(core, cell, &lost)))
                break;
            c->ncells++;
        }
    }
}

static void record_sheet(oc_core *core, const oc_trackhint *h)
{
    owf_book *b = core->book;
    owf_change *c;
    int i;
    if (h->sheet < 0 || h->sheet >= b->nsheets)
        return;
    if (h->kind == OC_HINT_ADD_SHEET) {
        for (i = 0; i < b->nchanges; i++)
            if (b->changes[i].sheet >= h->sheet)
                b->changes[i].sheet++;
        if ((c = change_add(core, OWF_CHANGE_INSERT_SHEET, h->sheet)) && !(c->after = oc_strdup(b->sheets[h->sheet].name)))
            change_remove(b, b->nchanges - 1, 1);
    } else if (h->old_name && (c = change_add(core, OWF_CHANGE_RENAME_SHEET, h->sheet))) {
        c->before = oc_strdup(h->old_name);
        c->after = oc_strdup(b->sheets[h->sheet].name);
        if (!c->before || !c->after)
            change_remove(b, b->nchanges - 1, 1);
    }
}

void oc_track_changed(oc_core *core)
{
    oc_undo *u = core->nundo ? core->undo[core->nundo - 1] : NULL;
    oc_trackhint h = core->hint;
    int i;
    core->hint.old_name = NULL;
    core->hint.kind = OC_HINT_NONE;
    if (core->book->track_changes && u && u->track_pending) {
        u->track_pending = 0;
        switch (h.kind) {
        case OC_HINT_QUIET:
            break;
        case OC_HINT_INSERT:
            record_insert(core, &h);
            break;
        case OC_HINT_DELETE:
            record_delete(core, &h, u);
            break;
        case OC_HINT_ADD_SHEET:
        case OC_HINT_RENAME:
            record_sheet(core, &h);
            break;
        default:
            if (u->kind == OC_UNDO_CELLS)
                for (i = 0; i < u->nsaves; i++) {
                    if (u->saves[i].sheet < core->book->nsheets &&
                        !compare_range(core, u->saves[i].sheet, u->saves[i].cells, u->saves[i].ncells,
                                       &u->saves[i].range))
                        break;
                }
            else if (u->kind == OC_UNDO_BOOK && u->book && u->book->nsheets == core->book->nsheets) {
                owf_range all;
                all.first_row = all.first_col = 0;
                all.last_row = OC_MAX_ROWS - 1;
                all.last_col = OC_MAX_COLS - 1;
                for (i = 0; i < core->book->nsheets; i++)
                    if (!compare_range(core, i, u->book->sheets[i].cells, u->book->sheets[i].ncells, &all))
                        break;
            }
            break;
        }
    }
    owf_free(h.old_name);
}

/* A log's changes (places as each was made) as the list keeps them (places as the book is now): each in turn,
 * the rows and columns inserted and deleted moving those before, as when they were recorded. */
static void replay(owf_book *b)
{
    owf_change *all = b->changes;
    int n = b->nchanges, i;
    b->changes = NULL;
    b->nchanges = b->capchanges = 0;
    b->changes_as_made = 0;
    if (!n)
        return;
    if (!(b->changes = owf_malloc((size_t)n * sizeof *b->changes))) {
        b->changes = all;                            /* kept as they are: places may be off */
        b->nchanges = b->capchanges = n;
        return;
    }
    b->capchanges = n;
    for (i = 0; i < n; i++) {
        owf_change c = all[i];
        int axis = is_cols(c.kind);
        if (c.kind == OWF_CHANGE_INSERT_ROWS || c.kind == OWF_CHANGE_INSERT_COLS)
            inserted(b, c.sheet, axis, axis ? c.col : c.row, c.count);
        else if (c.kind == OWF_CHANGE_DELETE_ROWS || c.kind == OWF_CHANGE_DELETE_COLS) {
            owf_change *taken = NULL;
            int ntaken = 0;
            if (deleted(b, c.sheet, axis, axis ? c.col : c.row, c.count, &taken, &ntaken) && ntaken) {
                owf_change *both = owf_realloc(c.children, (size_t)(c.nchildren + ntaken) * sizeof *both);
                if (both) {
                    memcpy(both + c.nchildren, taken, (size_t)ntaken * sizeof *taken);
                    c.children = both;
                    c.nchildren += ntaken;
                } else
                    oc_track_free_list(taken, ntaken), taken = NULL;
                if (both)
                    owf_free(taken);
            }
        }                                            /* (a log names sheets as they are now) */
        b->changes[b->nchanges++] = c;
    }
    owf_free(all);
}

void oc_track_opened(oc_core *core)
{
    owf_book *b = core->book;
    int i, j, lost = 0;
    if (b->changes_as_made)
        replay(b);
    for (i = 0; i < b->nchanges; i++) {
        owf_change *c = &b->changes[i], *d = NULL;
        if (c->kind != OWF_CHANGE_CELL || c->after || c->sheet < 0 || c->sheet >= b->nsheets)
            continue;
        for (j = i + 1; j < b->nchanges && !d; j++)
            if (b->changes[j].kind == OWF_CHANGE_CELL && b->changes[j].sheet == c->sheet &&
                b->changes[j].row == c->row && b->changes[j].col == c->col)
                d = &b->changes[j];
        if (d)
            c->after = d->before ? oc_strdup(d->before) : NULL;
        else
            c->after = oc_cell_input_of(core, owf_sheet_find(&b->sheets[c->sheet], c->row, c->col), &lost);
    }
}

void oc_track_sheet_moved(oc_core *core, int from, int to)
{
    owf_book *b = core->book;
    int i;
    for (i = 0; i < b->nchanges; i++) {
        int s = b->changes[i].sheet;
        if (s == from)
            s = to;
        else if (from < to && s > from && s <= to)
            s--;
        else if (to < from && s >= to && s < from)
            s++;
        b->changes[i].sheet = s;
    }
}

/* ---- What the window reads ---- */

int oc_core_track_changes(const oc_core *core)
{
    return core->book->track_changes;
}

int oc_core_set_track_changes(oc_core *core, int on)
{
    owf_book *b = core->book;
    on = on != 0;
    if (b->track_changes == on)
        return OWF_OK;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    oc_undo_clear(core);
    b->track_changes = on;
    if (!on) {
        oc_track_free_list(b->changes, b->nchanges);
        b->changes = NULL;
        b->nchanges = b->capchanges = 0;
    }
    core->dirty = 1;
    core->changes++;
    return OWF_OK;
}

int oc_core_set_track_author(oc_core *core, const char *author)
{
    char *a = author ? oc_strdup(author) : NULL;
    if (author && !a)
        return OWF_ERR_MEMORY;
    owf_free(core->track_author);
    core->track_author = a;
    return OWF_OK;
}

void oc_core_set_track_clock(oc_core *core, oc_clock_fn clock, void *user)
{
    core->track_clock = clock;
    core->track_clock_user = user;
}

int oc_core_change_count(const oc_core *core)
{
    return core->book->nchanges;
}

const owf_change *oc_core_change_at(const oc_core *core, int index)
{
    return index >= 0 && index < core->book->nchanges ? &core->book->changes[index] : NULL;
}

static void lines_name(int cols, unsigned first, unsigned count, char *out)
{
    char a[8], b[8];
    if (cols) {
        oc_col_name(first, a);
        oc_col_name(first + count - 1, b);
    } else {
        snprintf(a, sizeof a, "%u", first + 1);
        snprintf(b, sizeof b, "%u", first + count);
    }
    sprintf(out, "%s:%s", a, b);
}

int oc_core_change_text(const oc_core *core, int index, char *out, size_t size)
{
    const owf_change *c = oc_core_change_at(core, index);
    const char *sheet;
    char where[24];
    if (!c)
        return snprintf(out, size, "%s", "");
    sheet = c->sheet >= 0 && c->sheet < core->book->nsheets ? core->book->sheets[c->sheet].name : "?";
    switch (c->kind) {
    case OWF_CHANGE_CELL:
        oc_cell_name(c->row, c->col, where);
        return snprintf(out, size, "Cell %s.%s changed from %s to %s", sheet, where, c->before ? c->before : "(empty)",
                        c->after ? c->after : "(empty)");
    case OWF_CHANGE_INSERT_ROWS:
    case OWF_CHANGE_INSERT_COLS:
        lines_name(c->kind == OWF_CHANGE_INSERT_COLS, c->kind == OWF_CHANGE_INSERT_COLS ? c->col : c->row, c->count,
                   where);
        return snprintf(out, size, "%s %s inserted on %s", c->kind == OWF_CHANGE_INSERT_COLS ? "Columns" : "Rows",
                        where, sheet);
    case OWF_CHANGE_DELETE_ROWS:
    case OWF_CHANGE_DELETE_COLS:
        lines_name(c->kind == OWF_CHANGE_DELETE_COLS, c->kind == OWF_CHANGE_DELETE_COLS ? c->col : c->row, c->count,
                   where);
        return snprintf(out, size, "%s %s deleted on %s", c->kind == OWF_CHANGE_DELETE_COLS ? "Columns" : "Rows",
                        where, sheet);
    case OWF_CHANGE_INSERT_SHEET:
        return snprintf(out, size, "Sheet %s inserted", c->after ? c->after : sheet);
    case OWF_CHANGE_RENAME_SHEET:
        return snprintf(out, size, "Sheet %s renamed to %s", c->before ? c->before : "?", c->after ? c->after : sheet);
    }
    return snprintf(out, size, "%s", "");
}

int oc_core_cell_change(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    const owf_book *b = core->book;
    int i;
    for (i = b->nchanges - 1; i >= 0; i--) {
        const owf_change *c = &b->changes[i];
        if (c->sheet != sheet)
            continue;
        if ((c->kind == OWF_CHANGE_CELL && c->row == row && c->col == col) ||
            (c->kind == OWF_CHANGE_INSERT_ROWS && row >= c->row && row - c->row < c->count) ||
            (c->kind == OWF_CHANGE_INSERT_COLS && col >= c->col && col - c->col < c->count))
            return i;
    }
    return -1;
}

/* ---- Accepting and rejecting ---- */

/* The step for an accept or reject: the whole book for a reject (cells, rows and sheets come back), only the list
 * for an accept; never itself recorded. */
static void begin_step(oc_core *core, int whole)
{
    oc_undo *u;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    u = oc_undo_new(core, whole ? OC_UNDO_BOOK : OC_UNDO_CELLS);
    if (u && whole && oc_undo_save_book(core, u) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    if (u)
        u->track_pending = 0;
    oc_undo_push(core, u);
}

static void end_step(oc_core *core, int whole)
{
    if (whole) {
        oc_charts_forget(core);
        if (core->clip.valid)
            core->clip.cut = 0;
        oc_recalc(core, 1);
    }
    oc_track_hint(core, OC_HINT_QUIET, 0, 0, 0, 0, NULL);
    oc_changed(core);
}

static int reject_one(oc_core *core, int index);

static int reject_cell(oc_core *core, int index)
{
    owf_book *b = core->book;
    owf_change c;
    int i, rc;
    for (i = b->nchanges - 1; i > index; i--) {      /* the cell's later changes first */
        const owf_change *d = &b->changes[i];
        if (d->kind == OWF_CHANGE_CELL && d->sheet == b->changes[index].sheet && d->row == b->changes[index].row &&
            d->col == b->changes[index].col && (rc = reject_cell(core, i)) != OWF_OK)
            return rc;
    }
    c = b->changes[index];
    if ((rc = owf_calc_enter(core->calc, c.sheet, c.row, c.col, c.before ? c.before : "")) == OWF_ERR_MEMORY)
        return rc;
    change_remove(b, index, 1);
    return OWF_OK;
}

static int reject_lines(oc_core *core, int index)
{
    owf_book *b = core->book;
    owf_change c = b->changes[index];
    int axis = is_cols(c.kind), rc, i, at;
    unsigned p = axis ? c.col : c.row;
    change_remove(b, index, 0);
    if (c.kind == OWF_CHANGE_INSERT_ROWS || c.kind == OWF_CHANGE_INSERT_COLS) {
        rc = owf_calc_shift(core->calc, c.sheet, axis, p, -(int)c.count);
        deleted(b, c.sheet, axis, p, c.count, NULL, NULL);   /* the changes in them go with them */
        change_clear(&c);
        return rc == OWF_ERR_UNSUPPORTED ? OWF_OK : rc;
    }
    /* deleted ones come back, with what they held and the changes there were in them */
    rc = owf_calc_shift(core->calc, c.sheet, axis, p, (int)c.count);
    inserted(b, c.sheet, axis, p, c.count);
    for (i = 0; i < c.ncells && rc != OWF_ERR_MEMORY; i++)
        rc = owf_calc_enter(core->calc, c.sheet, axis ? c.cells[i].row : c.cells[i].row + p,
                            axis ? c.cells[i].col + p : c.cells[i].col, c.cells[i].input);
    if (c.nchildren) {
        owf_change *grown = owf_realloc(b->changes, (size_t)(b->nchanges + c.nchildren) * sizeof *grown);
        if (grown) {
            b->changes = grown;
            b->capchanges = b->nchanges + c.nchildren;
            for (i = 0; i < c.nchildren; i++)
                *place(&c.children[i], axis) += p;
            /* back where they were in the list: before the first change made after them */
            for (at = 0; at < b->nchanges && b->changes[at].id < c.children[0].id; at++)
                ;
            memmove(&b->changes[at + c.nchildren], &b->changes[at], (size_t)(b->nchanges - at) * sizeof *b->changes);
            memcpy(&b->changes[at], c.children, (size_t)c.nchildren * sizeof *c.children);
            b->nchanges += c.nchildren;
            owf_free(c.children);
            c.children = NULL;
            c.nchildren = 0;
        } else
            rc = OWF_ERR_MEMORY;
    }
    change_clear(&c);
    return rc == OWF_ERR_MEMORY ? rc : OWF_OK;
}

static int reject_sheet(oc_core *core, int index)
{
    owf_book *b = core->book;
    owf_change *c = &b->changes[index];
    int sheet = c->sheet, i, j, rc;
    if (c->kind == OWF_CHANGE_RENAME_SHEET) {
        for (i = b->nchanges - 1; i > index; i--)    /* its later renames first */
            if (b->changes[i].kind == OWF_CHANGE_RENAME_SHEET && b->changes[i].sheet == sheet &&
                (rc = reject_sheet(core, i)) != OWF_OK)
                return rc;
        c = &b->changes[index];
        if (c->before && (rc = owf_calc_rename_sheet(core->calc, sheet, c->before)) != OWF_OK)
            return rc == OWF_ERR_FORMAT ? OWF_ERR_UNSUPPORTED : rc;  /* its old name is another sheet's now */
        change_remove(b, index, 1);
        return OWF_OK;
    }
    if (b->nsheets < 2)
        return OWF_ERR_UNSUPPORTED;
    oc_drop_sheet(core, sheet);
    for (i = j = 0; i < b->nchanges; i++) {           /* its changes go with it, the later sheets' move up */
        if (b->changes[i].sheet == sheet) {
            change_clear(&b->changes[i]);
            continue;
        }
        if (b->changes[i].sheet > sheet)
            b->changes[i].sheet--;
        b->changes[j++] = b->changes[i];
    }
    b->nchanges = j;
    return OWF_OK;
}

static int reject_one(oc_core *core, int index)
{
    switch (core->book->changes[index].kind) {
    case OWF_CHANGE_CELL:
        return reject_cell(core, index);
    case OWF_CHANGE_INSERT_SHEET:
    case OWF_CHANGE_RENAME_SHEET:
        return reject_sheet(core, index);
    default:
        return reject_lines(core, index);
    }
}

int oc_core_accept_change(oc_core *core, int index)
{
    if (index < 0 || index >= core->book->nchanges)
        return OWF_ERR_UNSUPPORTED;
    begin_step(core, 0);
    change_remove(core->book, index, 1);
    end_step(core, 0);
    return OWF_OK;
}

int oc_core_accept_all_changes(oc_core *core)
{
    owf_book *b = core->book;
    if (!b->nchanges)
        return OWF_ERR_UNSUPPORTED;
    begin_step(core, 0);
    oc_track_free_list(b->changes, b->nchanges);
    b->changes = NULL;
    b->nchanges = b->capchanges = 0;
    end_step(core, 0);
    return OWF_OK;
}

static int sheet_change_refused(const oc_core *core, const owf_change *c)
{
    return (c->kind == OWF_CHANGE_INSERT_SHEET && core->book->nsheets < 2) ||
           ((c->kind == OWF_CHANGE_INSERT_SHEET || c->kind == OWF_CHANGE_RENAME_SHEET) && oc_book_locked(core));
}

int oc_core_reject_change(oc_core *core, int index)
{
    int rc;
    if (index < 0 || index >= core->book->nchanges || sheet_change_refused(core, &core->book->changes[index]))
        return OWF_ERR_UNSUPPORTED;
    begin_step(core, 1);
    rc = reject_one(core, index);
    end_step(core, 1);
    return rc;
}

int oc_core_reject_all_changes(oc_core *core)
{
    int rc = OWF_OK;
    if (!core->book->nchanges)
        return OWF_ERR_UNSUPPORTED;
    begin_step(core, 1);
    while (core->book->nchanges && rc != OWF_ERR_MEMORY) {
        int last = core->book->nchanges - 1;
        if (sheet_change_refused(core, &core->book->changes[last]) || (rc = reject_one(core, last)) != OWF_OK) {
            if (rc == OWF_ERR_MEMORY)
                break;
            change_remove(core->book, last, 1);      /* one that cannot go back stays as it is: accepted */
            rc = OWF_OK;
        }
    }
    end_step(core, 1);
    return rc;
}

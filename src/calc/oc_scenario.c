/*
 * GFOpenCalc's portable core: Data > Scenarios. A scenario keeps the values
 * of some cells on a sheet under a name; showing it puts them back. Each
 * change is made on a private copy of the book and swapped in as one Undo
 * step, as Solver's and the pivot tables' are. The scenarios themselves are
 * the sheet's hidden names (owf_scenario.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"
#include "owf_scenario.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return !*a && !*b;
}

int oc_core_scenario_count(const oc_core *core)
{
    return core ? owf_scenario_count(core->book, core->sheet) : 0;
}

int oc_core_scenario_get(const oc_core *core, int index, owf_scenario *sc)
{
    return core && owf_scenario_get(core->book, core->sheet, index, sc);
}

int oc_core_scenario_find(const oc_core *core, const char *name)
{
    int i, n = oc_core_scenario_count(core);
    for (i = 0; i < n; i++) {
        owf_scenario sc;
        int same;
        if (!owf_scenario_get(core->book, core->sheet, i, &sc))
            continue;
        same = same_name(sc.name, name);
        owf_scenario_free(&sc);
        if (same)
            return i;
    }
    return -1;
}

/* The work on a copy swapped in as one Undo step. */
static int commit(oc_core *core, owf_book *work, owf_calc *calc)
{
    owf_book *old_book;
    owf_calc *old_calc;
    oc_undo *undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    oc_cf_forget(core);
    oc_charts_forget(core);
    old_book = core->book;
    old_calc = core->calc;
    core->book = work;
    core->calc = calc;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    return OWF_OK;
}

static int start(oc_core *core, owf_book **work, owf_calc **calc)
{
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    *work = oc_book_copy(core->book);
    if (!*work)
        return OWF_ERR_MEMORY;
    *calc = owf_calc_new(*work);
    if (!*calc) {
        owf_book_free(*work);
        return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

static int give_up(owf_book *work, owf_calc *calc, int rc)
{
    owf_calc_free(calc);
    owf_book_free(work);
    return rc;
}

/* "B2:C4, E7; 'Plan'!F1" (ranges on the active sheet) into sc's ranges. */
static int read_cells(const oc_core *core, const char *text, owf_scenario *sc)
{
    char part[200];
    const char *p = text;
    sc->nranges = 0;
    while (*p) {
        size_t n = 0;
        int quoted = 0, sheet;
        owf_range r;
        while (*p == ' ' || *p == ',' || *p == ';')
            p++;
        if (!*p)
            break;
        while (*p && (quoted || (*p != ',' && *p != ';'))) {
            if (*p == '\'')
                quoted = !quoted;
            if (n + 1 >= sizeof part)
                return 0;
            part[n++] = *p++;
        }
        while (n && part[n - 1] == ' ')
            n--;
        part[n] = 0;
        if (sc->nranges >= OWF_SCENARIO_RANGES || !oc_sheet_range(core->book, part, core->sheet, &sheet, &r) ||
            sheet != core->sheet)
            return 0;
        sc->range[sc->nranges++] = r;
    }
    return sc->nranges > 0;
}

/* The cells' present inputs into sc->value. */
static int capture(const oc_core *core, owf_scenario *sc)
{
    unsigned k, row, col, i = 0, n = owf_scenario_cells(sc);
    if (!n || n > OWF_SCENARIO_CELLS)
        return OWF_ERR_UNSUPPORTED;
    sc->value = owf_malloc(n * sizeof *sc->value);
    if (!sc->value)
        return OWF_ERR_MEMORY;
    sc->ncells = 0;
    for (k = 0; k < sc->nranges; k++)
        for (row = sc->range[k].first_row; row <= sc->range[k].last_row; row++)
            for (col = sc->range[k].first_col; col <= sc->range[k].last_col; col++) {
                char *v = oc_cell_input(core, core->sheet, row, col);
                if (!v)
                    return OWF_ERR_MEMORY;
                sc->value[i++] = v;
                sc->ncells = i;
            }
    return OWF_OK;
}

int oc_core_scenario_save(oc_core *core, int index, const char *name, const char *cells, const char *comment)
{
    owf_scenario sc;
    owf_book *work;
    owf_calc *calc;
    int rc, count, other;
    if (!core || !name || !cells)
        return OWF_ERR_FORMAT;
    count = oc_core_scenario_count(core);
    if (index < 0)
        index = count;
    if (index > count)
        return OWF_ERR_FORMAT;
    if (index == count && count >= OWF_SCENARIOS)
        return OWF_ERR_UNSUPPORTED;
    while (*name == ' ')
        name++;
    if (!*name || strlen(name) >= OWF_SCENARIO_NAME)
        return OWF_ERR_FORMAT;
    other = oc_core_scenario_find(core, name);
    if (other >= 0 && other != index)
        return OWF_ERR_UNSUPPORTED;          /* a name the sheet's other scenario has */
    memset(&sc, 0, sizeof sc);
    snprintf(sc.name, sizeof sc.name, "%s", name);
    snprintf(sc.comment, sizeof sc.comment, "%s", comment ? comment : "");
    if (!read_cells(core, cells, &sc))
        return OWF_ERR_FORMAT;
    if (index < count) {
        owf_scenario old;
        if (owf_scenario_get(core->book, core->sheet, index, &old)) {
            sc.active = old.active;
            owf_scenario_free(&old);
        }
    }
    if ((rc = capture(core, &sc)) != OWF_OK) {
        owf_scenario_free(&sc);
        return rc;
    }
    if ((rc = start(core, &work, &calc)) != OWF_OK) {
        owf_scenario_free(&sc);
        return rc;
    }
    rc = owf_scenario_put(work, core->sheet, index, &sc);
    owf_scenario_free(&sc);
    if (rc != OWF_OK)
        return give_up(work, calc, rc);
    return commit(core, work, calc);
}

int oc_core_scenario_show(oc_core *core, int index)
{
    owf_scenario sc;
    owf_book *work;
    owf_calc *calc;
    unsigned k, row, col, i = 0;
    int rc, n, j;
    if (!core || !owf_scenario_get(core->book, core->sheet, index, &sc))
        return OWF_ERR_FORMAT;
    if ((rc = start(core, &work, &calc)) != OWF_OK) {
        owf_scenario_free(&sc);
        return rc;
    }
    for (k = 0; k < sc.nranges && rc == OWF_OK; k++)
        for (row = sc.range[k].first_row; row <= sc.range[k].last_row && rc == OWF_OK; row++)
            for (col = sc.range[k].first_col; col <= sc.range[k].last_col && rc == OWF_OK; col++)
                rc = owf_calc_enter(calc, core->sheet, row, col, sc.value[i++]);
    owf_scenario_free(&sc);
    /* this one is the sheet's shown scenario now */
    n = owf_scenario_count(work, core->sheet);
    for (j = 0; j < n && rc == OWF_OK; j++) {
        owf_scenario other;
        if (!owf_scenario_get(work, core->sheet, j, &other))
            continue;
        if (other.active != (j == index)) {
            other.active = j == index;
            rc = owf_scenario_put(work, core->sheet, j, &other);
        }
        owf_scenario_free(&other);
    }
    if (rc == OWF_OK)
        rc = owf_calc_recalc(calc);
    if (rc != OWF_OK)
        return give_up(work, calc, rc == OWF_ERR_MEMORY ? rc : OWF_ERR_FORMAT);
    return commit(core, work, calc);
}

int oc_core_scenario_delete(oc_core *core, int index)
{
    owf_book *work;
    owf_calc *calc;
    int rc;
    if (!core || index < 0 || index >= oc_core_scenario_count(core))
        return OWF_ERR_FORMAT;
    if ((rc = start(core, &work, &calc)) != OWF_OK)
        return rc;
    rc = owf_scenario_remove(work, core->sheet, index);
    if (rc != OWF_OK)
        return give_up(work, calc, rc);
    return commit(core, work, calc);
}

int oc_core_scenario_cells_text(const oc_core *core, const owf_scenario *sc, char *out, size_t size)
{
    size_t at = 0;
    unsigned k;
    if (!size)
        return 0;
    out[0] = 0;
    for (k = 0; k < sc->nranges; k++) {
        char a[16], b[16];
        int n;
        oc_cell_name(sc->range[k].first_row, sc->range[k].first_col, a);
        oc_cell_name(sc->range[k].last_row, sc->range[k].last_col, b);
        if (sc->range[k].first_row == sc->range[k].last_row && sc->range[k].first_col == sc->range[k].last_col)
            n = snprintf(out + at, size - at, "%s%s", k ? "," : "", a);
        else
            n = snprintf(out + at, size - at, "%s%s:%s", k ? "," : "", a, b);
        if (n < 0 || (size_t)n >= size - at)
            return 0;
        at += (size_t)n;
    }
    (void)core;
    return 1;
}

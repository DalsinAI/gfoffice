/*
 * GFOpenCalc's portable core: what the tracked-changes window and ARexx
 * need beside the changes themselves (oc_track.c): going to a change, and
 * a change's line with its author and time.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <string.h>

int oc_core_change_go(oc_core *core, int index)
{
    const owf_change *c = core ? oc_core_change_at(core, index) : NULL;
    unsigned row, col;
    if (!c || c->sheet < 0 || c->sheet >= core->book->nsheets)
        return OWF_ERR_UNSUPPORTED;
    if (oc_core_set_sheet(core, c->sheet) != OWF_OK)
        return OWF_ERR_UNSUPPORTED;
    switch (c->kind) {
    case OWF_CHANGE_CELL: row = c->row; col = c->col; break;
    case OWF_CHANGE_INSERT_ROWS: case OWF_CHANGE_DELETE_ROWS: row = c->row; col = 0; break;
    case OWF_CHANGE_INSERT_COLS: case OWF_CHANGE_DELETE_COLS: row = 0; col = c->col; break;
    default: return OWF_OK;                     /* a sheet's: the sheet shown is the place */
    }
    oc_core_select_cell(core, row, col, 0, 0);
    return OWF_OK;
}

int oc_core_change_line(const oc_core *core, int index, char *out, size_t size)
{
    const owf_change *c = oc_core_change_at(core, index);
    char text[300], when[20];
    if (!c)
        return snprintf(out, size, "%s", "");
    oc_core_change_text(core, index, text, sizeof text);
    /* "2026-11-16T06:30:00" as "2026-11-16 06:30"; a time in another form as it is */
    when[0] = 0;
    if (c->date && strlen(c->date) >= 16 && c->date[10] == 'T') {
        memcpy(when, c->date, 16);
        when[10] = ' ';
        when[16] = 0;
    } else if (c->date) {
        snprintf(when, sizeof when, "%s", c->date);
    }
    return snprintf(out, size, "%s (%s%s%s)", text, c->author && *c->author ? c->author : "Unknown Author",
                    when[0] ? ", " : "", when);
}

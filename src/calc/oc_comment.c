/* GFOpenCalc's portable core: editable cell comments (Excel notes).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "oc_internal.h"

int oc_core_set_comment(oc_core *core, const char *author, const char *text)
{
    oc_sheetstate *state;
    oc_undo *undo;
    int rc;
    if (!core) return OWF_ERR_FORMAT;
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    state = oc_state(core);
    undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    rc = owf_sheet_set_comment(oc_sheet(core), state->sel.row, state->sel.col,
                               author, text && *text ? text : NULL);
    if (rc < 0) {
        oc_undo_free(undo);
        return OWF_ERR_MEMORY;
    }
    oc_undo_push(core, undo);
    oc_changed(core);
    return OWF_OK;
}

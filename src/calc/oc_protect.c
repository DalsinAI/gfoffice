/*
 * GFOpenCalc's portable core: sheet and cell protection, as Excel's. A
 * protected sheet's locked cells (every cell, unless its style says
 * unlocked) cannot change; what else users may do there is the sheet's
 * protect_allow. The password is Excel's 16-bit hash, kept in XLSX and ODS.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

unsigned oc_password_hash(const char *password)
{
    unsigned hash = 0;
    size_t n = password ? strlen(password) : 0, i;
    if (!n)
        return 0;
    for (i = n; i-- > 0;) {
        hash = ((hash >> 14) & 1u) | ((hash << 1) & 0x7FFFu);
        hash ^= (unsigned char)password[i];
    }
    hash = ((hash >> 14) & 1u) | ((hash << 1) & 0x7FFFu);
    hash ^= (unsigned)n;
    hash ^= 0xCE4Bu;
    return hash & 0xFFFFu;
}

static const owf_cellstyle *style_at(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    const owf_sheet *s = &core->book->sheets[sheet];
    const owf_cell *c = owf_sheet_find(s, row, col);
    int st = c ? c->style : oc_empty_style(s, row, col);
    if (st < 0 || st >= core->book->nstyles)
        st = 0;
    return &core->book->styles[st];
}

int oc_cell_locked(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    if (!core || sheet < 0 || sheet >= core->book->nsheets || !core->book->sheets[sheet].protect)
        return 0;
    return !style_at(core, sheet, row, col)->unlocked;
}

int oc_range_locked(const oc_core *core, int sheet, const owf_range *range)
{
    unsigned r, c;
    if (!core || sheet < 0 || sheet >= core->book->nsheets || !core->book->sheets[sheet].protect)
        return 0;
    if ((unsigned long)(range->last_row - range->first_row + 1) * (range->last_col - range->first_col + 1) > 65536UL)
        return 1;                                    /* too many to look at: as if locked */
    for (r = range->first_row; r <= range->last_row; r++)
        for (c = range->first_col; c <= range->last_col; c++)
            if (!style_at(core, sheet, r, c)->unlocked)
                return 1;
    return 0;
}

int oc_protected(const oc_core *core, unsigned allow)
{
    const owf_sheet *s = &core->book->sheets[core->sheet];
    return s->protect && (!allow || !(s->protect_allow & allow));
}

int oc_protected_selection(const oc_core *core)
{
    const oc_sheetstate *st = oc_state_const((oc_core *)core);
    int k;
    if (!core->book->sheets[core->sheet].protect)
        return 0;
    for (k = 0; k < st->sel.nranges; k++) {
        owf_range r;
        if (oc_range_clip(core, &st->sel.ranges[k], &r) && oc_range_locked(core, core->sheet, &r))
            return 1;
    }
    return 0;
}

int oc_formula_hidden(const oc_core *core, int sheet, unsigned row, unsigned col)
{
    if (!core || sheet < 0 || sheet >= core->book->nsheets || !core->book->sheets[sheet].protect)
        return 0;
    return style_at(core, sheet, row, col)->formula_hidden;
}

int oc_core_protect_sheet(oc_core *core, const char *password, unsigned allow)
{
    owf_sheet *s;
    char *hash = NULL;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    s = oc_sheet(core);
    if (s->protect)
        return OWF_ERR_UNSUPPORTED;
    if (password && *password) {
        char hex[8];
        snprintf(hex, sizeof hex, "%04X", oc_password_hash(password));
        if (!(hash = oc_strdup(hex)))
            return OWF_ERR_MEMORY;
    }
    owf_free(s->protect_hash);
    owf_free(s->protect_kept);
    s->protect_hash = hash;
    s->protect_kept = NULL;
    s->protect_allow = allow;
    s->protect = 1;
    oc_changed(core);
    return OWF_OK;
}

int oc_core_unprotect_sheet(oc_core *core, const char *password)
{
    owf_sheet *s;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    s = oc_sheet(core);
    if (!s->protect)
        return OWF_ERR_UNSUPPORTED;
    if (s->protect_hash) {
        char hex[8];
        size_t i;
        snprintf(hex, sizeof hex, "%04X", oc_password_hash(password));
        for (i = 0; i < 4; i++)
            if (toupper((unsigned char)s->protect_hash[i]) != hex[i] || !password || !*password)
                return OC_WRONG_PASSWORD;
    }
    /* a hash from another program (SHA-512, SHA-1...) is not checked: GFOpenCalc keeps it, it cannot work it out */
    owf_free(s->protect_hash);
    owf_free(s->protect_kept);
    s->protect_hash = s->protect_kept = NULL;
    s->protect = 0;
    oc_changed(core);
    return OWF_OK;
}

int oc_core_sheet_protection(const oc_core *core, unsigned *allow, int *has_password)
{
    const owf_sheet *s = &core->book->sheets[core->sheet];
    if (allow)
        *allow = s->protect_allow;
    if (has_password)
        *has_password = s->protect_hash != NULL || s->protect_kept != NULL;
    return s->protect;
}

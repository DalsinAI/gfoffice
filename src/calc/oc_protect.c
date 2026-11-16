/*
 * GFOpenCalc's portable core: sheet and cell protection, as Excel's. A
 * protected sheet's locked cells (every cell, unless its style says
 * unlocked) cannot change; what else users may do there is the sheet's
 * protect_allow. A protected workbook structure keeps its sheets as they
 * are: none added, deleted, renamed, moved, hidden or shown. The password is
 * Excel's 16-bit hash, kept in XLSX and ODS.
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

/* 1 when password is the one hash (4 hex digits, or NULL: none) was made from. */
static int password_fits(const char *hash, const char *password)
{
    char hex[8];
    size_t i;
    if (!hash)
        return 1;
    if (!password || !*password)
        return 0;
    snprintf(hex, sizeof hex, "%04X", oc_password_hash(password));
    for (i = 0; i < 4; i++)
        if (toupper((unsigned char)hash[i]) != hex[i])
            return 0;
    return 1;
}

static int new_hash(const char *password, char **hash)
{
    char hex[8];
    *hash = NULL;
    if (!password || !*password)
        return OWF_OK;
    snprintf(hex, sizeof hex, "%04X", oc_password_hash(password));
    return (*hash = oc_strdup(hex)) ? OWF_OK : OWF_ERR_MEMORY;
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
    if (new_hash(password, &hash) != OWF_OK)
        return OWF_ERR_MEMORY;
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
    if (!password_fits(s->protect_hash, password))
        return OC_WRONG_PASSWORD;
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

int oc_book_locked(const oc_core *core)
{
    return core && core->book->protect_structure;
}

int oc_core_protect_book(oc_core *core, const char *password)
{
    owf_book *book;
    char *hash;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    book = core->book;
    if (book->protect_structure)
        return OWF_ERR_UNSUPPORTED;
    if (new_hash(password, &hash) != OWF_OK)
        return OWF_ERR_MEMORY;
    owf_free(book->protect_hash);
    owf_free(book->protect_kept);
    book->protect_hash = hash;
    book->protect_kept = NULL;
    book->protect_structure = 1;
    oc_undo_clear(core);                     /* as Excel: no Undo past it brings back a sheet it keeps out */
    oc_changed(core);
    return OWF_OK;
}

int oc_core_unprotect_book(oc_core *core, const char *password)
{
    owf_book *book;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    book = core->book;
    if (!book->protect_structure)
        return OWF_ERR_UNSUPPORTED;
    if (!password_fits(book->protect_hash, password))
        return OC_WRONG_PASSWORD;
    /* as a sheet's, a hash another program made with another digest is kept, not checked */
    owf_free(book->protect_hash);
    owf_free(book->protect_kept);
    book->protect_hash = book->protect_kept = NULL;
    book->protect_structure = 0;
    oc_changed(core);
    return OWF_OK;
}

int oc_core_book_protection(const oc_core *core, int *has_password)
{
    if (has_password)
        *has_password = core && (core->book->protect_hash != NULL || core->book->protect_kept != NULL);
    return oc_book_locked(core);
}

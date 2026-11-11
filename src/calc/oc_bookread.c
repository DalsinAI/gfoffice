/* GFOpenCalc's portable core: what the window reads of the book, as copies
 * into the window's own memory (opencalc_core.h, "What the window reads of the
 * book"). None of these hands out a pointer into the book.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "oc_internal.h"

#include <string.h>

/* text (NULL: "") into out, cut at a whole UTF-8 character to fit size and ended;
 * the whole text's length. */
static int copy_text(const char *text, char *out, size_t size)
{
    size_t n = text ? strlen(text) : 0, k;
    if (out && size) {
        k = n < size ? n : size - 1;
        if (k < n)
            while (k > 0 && ((unsigned char)text[k] & 0xC0) == 0x80)   /* not inside a character */
                k--;
        if (k) memcpy(out, text, k);
        out[k] = 0;
    }
    return n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n;
}

static const owf_sheet *sheet_at(const oc_core *core, int sheet)
{
    if (!core || !core->book || sheet < 0 || sheet >= core->book->nsheets)
        return NULL;
    return &core->book->sheets[sheet];
}

int oc_core_sheet_count(const oc_core *core)
{
    return core && core->book ? core->book->nsheets : 0;
}

int oc_core_sheet_info(const oc_core *core, int sheet, oc_sheet_info *info, char *name, size_t size)
{
    const owf_sheet *sh = sheet_at(core, sheet);
    if (!sh)
        return -1;
    if (info) {
        memset(info, 0, sizeof *info);
        info->hidden = sh->hidden;
        info->freeze_rows = sh->freeze_rows;
        info->freeze_cols = sh->freeze_cols;
        info->has_autofilter = sh->has_autofilter;
        info->autofilter = sh->autofilter;
        info->nvalidations = sh->nvalidations;
    }
    return copy_text(sh->name, name, size);
}

int oc_core_font_info(const oc_core *core, int index, char *name, size_t size, owf_font_kind *kind, int *length)
{
    const owf_book *book = core ? core->book : NULL;
    int n = -1;
    if (kind) *kind = OWF_FONT_ANY;
    if (book && index < 0 && book->nstyles)
        index = book->styles[0].font.font;
    if (book && index >= 0 && index < book->nfonts) {
        n = copy_text(book->fonts[index].name, name, size);
        if (kind) *kind = book->fonts[index].kind;
    } else
        copy_text(NULL, name, size);
    if (length) *length = n;
    return index;
}

int oc_core_cell_info(const oc_core *core, int sheet, unsigned row, unsigned col, oc_cell_info *info,
                      char *text, size_t size)
{
    const owf_sheet *sh = sheet_at(core, sheet);
    const owf_cell *c;
    const owf_book *book;
    if (!sh)
        return -1;
    book = core->book;
    c = owf_sheet_find(sh, row, col);
    if (info) {
        memset(info, 0, sizeof *info);
        info->exists = c != NULL;
        info->type = c ? c->type : OWF_VALUE_EMPTY;
        info->style = c ? c->style : 0;
        if (book->nstyles)
            info->look = book->styles[c && c->style >= 0 && c->style < book->nstyles ? c->style : 0];
        else
            owf_cellstyle_init(&info->look);
    }
    return copy_text(c && c->type == OWF_VALUE_TEXT ? c->text : NULL, text, size);
}

int oc_core_comment_info(const oc_core *core, int sheet, unsigned row, unsigned col, char *text, size_t size,
                         char *author, size_t author_size, int *author_length)
{
    const owf_sheet *sh = sheet_at(core, sheet);
    const owf_comment *cm = sh ? owf_sheet_comment(sh, row, col) : NULL;
    int n;
    if (!cm) {
        copy_text(NULL, text, size);
        copy_text(NULL, author, author_size);
        if (author_length) *author_length = -1;
        return -1;
    }
    n = copy_text(cm->text, text, size);
    copy_text(cm->author, author, author_size);
    if (author_length) *author_length = cm->author ? (int)strlen(cm->author) : -1;
    return n;
}

int oc_core_name_info(const oc_core *core, int index, char *name, size_t size, int *sheet)
{
    const owf_defname *dn = oc_core_name_at(core, index);
    if (!dn) {
        copy_text(NULL, name, size);
        return -1;
    }
    if (sheet) *sheet = dn->sheet;
    return copy_text(dn->name, name, size);
}

int oc_core_validation_info(const oc_core *core, int sheet, int index, oc_validation_info *info)
{
    const owf_sheet *sh = sheet_at(core, sheet);
    const owf_validation *v;
    if (!sh || index < 0 || index >= sh->nvalidations || !info)
        return OWF_ERR_UNSUPPORTED;
    v = &sh->validations[index];
    memset(info, 0, sizeof *info);
    info->type = v->type;
    info->nranges = v->nranges;
    if (v->nranges && v->ranges)
        info->first = v->ranges[0];
    info->show_error = v->show_error;
    info->error_style = v->error_style;
    info->no_dropdown = v->no_dropdown;
    return OWF_OK;
}

int oc_core_sheet_charts(const oc_core *core, owf_chart **charts, int *count)
{
    return owf_sheet_charts(core->book, core->sheet, charts, count);
}

int oc_core_write_copy(const oc_core *core, const char *path, const char *format, owf_report *report)
{
    return owf_export_book_file(core->book, path, format, report);
}

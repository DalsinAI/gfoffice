/*
 * GFOpenCalc's reusable printed-sheet renderer. The spreadsheet view is
 * deliberately the single source of truth: its platform-neutral drawing
 * list is paginated and sent to libowf's page PDF writer. This keeps cell
 * formatting, conditional formats, merged cells and charts identical on
 * screen and paper without an Amiga dependency.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"
#include "owf_pdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A4_W 11906
#define A4_H 16838
#define MARGIN 720
#define PAGE_HEAD 300
#define PAGE_FOOT 300
#define PX_TWIPS 15

void oc_pdf_options_init(oc_pdf_options *o)
{
    if (!o)
        return;
    memset(o, 0, sizeof *o);
    o->sheet = -1;
    o->fit_width = 1;
    o->gridlines = 1;
}

/* One past the last row and column that paper needs. Besides cells this
 * includes merged ranges, comments, an AutoFilter and charts. */
static void paper_range(oc_core *core, unsigned *rows, unsigned *cols)
{
    const owf_sheet *s = oc_sheet(core);
    const owf_chart *charts;
    int i, n;
    oc_used_range(s, rows, cols);
    for (i = 0; i < s->nmerges; i++) {
        unsigned er = s->merges[i].last_row < OC_MAX_ROWS ? s->merges[i].last_row + 1 : OC_MAX_ROWS;
        unsigned ec = s->merges[i].last_col < OC_MAX_COLS ? s->merges[i].last_col + 1 : OC_MAX_COLS;
        if (er > *rows) *rows = er;
        if (ec > *cols) *cols = ec;
    }
    for (i = 0; i < s->ncomments; i++) {
        unsigned er = s->comments[i].row < OC_MAX_ROWS ? s->comments[i].row + 1 : OC_MAX_ROWS;
        unsigned ec = s->comments[i].col < OC_MAX_COLS ? s->comments[i].col + 1 : OC_MAX_COLS;
        if (er > *rows) *rows = er;
        if (ec > *cols) *cols = ec;
    }
    if (s->has_autofilter) {
        unsigned er = s->autofilter.last_row < OC_MAX_ROWS ? s->autofilter.last_row + 1 : OC_MAX_ROWS;
        unsigned ec = s->autofilter.last_col < OC_MAX_COLS ? s->autofilter.last_col + 1 : OC_MAX_COLS;
        if (er > *rows) *rows = er;
        if (ec > *cols) *cols = ec;
    }
    charts = oc_core_charts(core, &n);
    for (i = 0; i < n; i++) {
        const owf_chart *c = &charts[i];
        unsigned er = c->to_row < OC_MAX_ROWS ? c->to_row + 1 : OC_MAX_ROWS;
        unsigned ec = c->to_col < OC_MAX_COLS ? c->to_col + 1 : OC_MAX_COLS;
        if (c->sized) {
            long h = c->height / 635 + c->from_row_off / 635;
            long w = c->width / 635 + c->from_col_off / 635;
            unsigned r = c->from_row, col = c->from_col;
            while (h > 0 && r < OC_MAX_ROWS) { h -= oc_row_twips(s, r++); }
            while (w > 0 && col < OC_MAX_COLS) { w -= oc_col_twips(s, col++); }
            er = r > c->from_row ? r : c->from_row + 1;
            ec = col > c->from_col ? col : c->from_col + 1;
        }
        if (er > *rows) *rows = er;
        if (ec > *cols) *cols = ec;
    }
    if (!*rows) *rows = 1;
    if (!*cols) *cols = 1;
    if (*rows > OC_MAX_ROWS) *rows = OC_MAX_ROWS;
    if (*cols > OC_MAX_COLS) *cols = OC_MAX_COLS;
}

static long span_twips(const owf_sheet *s, int axis, unsigned end)
{
    unsigned i;
    long long total = 0;
    for (i = 0; i < end; i++) {
        total += axis ? oc_col_twips(s, i) : oc_row_twips(s, i);
        if (total > 100000000L)
            return 100000000L;
    }
    return (long)total;
}

/* The first index for the following page. Hidden lines consume no room;
 * a single line wider/taller than the page still advances. */
static unsigned next_line(const oc_core *core, int axis, unsigned first, unsigned end)
{
    const owf_sheet *s = oc_sheet(core);
    unsigned i, frozen = axis ? s->freeze_cols : s->freeze_rows;
    int avail = axis ? core->width : core->height, used = 0;
    for (i = 0; i < frozen; i++)
        avail -= oc_px(core, axis ? oc_col_twips(s, i) : oc_row_twips(s, i));
    if (avail < 1) avail = 1;
    for (i = first; i < end; i++) {
        int sz = oc_px(core, axis ? oc_col_twips(s, i) : oc_row_twips(s, i));
        if (!sz)
            continue;
        if (used && used + sz > avail)
            break;
        used += sz;
        if (used >= avail) {
            i++;
            break;
        }
    }
    if (i <= first)
        i = first + 1;
    return i > end ? end : i;
}

/* The part of a page that actually holds sheet lines. The view normally
 * fills a window with empty cells; paper instead stops the grid at the
 * used range (or a chart's far edge), leaving the rest of the page blank. */
static int page_pixels(const oc_core *core, int axis, unsigned first, unsigned end, unsigned used)
{
    const owf_sheet *s = oc_sheet(core);
    unsigned i, frozen = axis ? s->freeze_cols : s->freeze_rows;
    int px = axis ? core->rowhdr : core->colhdr;
    for (i = 0; i < frozen && i < used; i++)
        px += oc_px(core, axis ? oc_col_twips(s, i) : oc_row_twips(s, i));
    if (first < frozen) first = frozen;
    if (end > used) end = used;
    for (i = first; i < end; i++)
        px += oc_px(core, axis ? oc_col_twips(s, i) : oc_row_twips(s, i));
    return px > 0 ? px : 1;
}

static owf_font_kind font_kind(const owf_book *b, int font)
{
    if (font < 0 && b->nstyles)
        font = b->styles[0].font.font;
    if (font >= 0 && font < b->nfonts && b->fonts[font].kind != OWF_FONT_ANY)
        return b->fonts[font].kind;
    return OWF_FONT_SANS;
}

static int line_dashed(int line)
{
    return line == OWF_LINE_DASHED || line == OWF_LINE_DOTTED || line == OWF_LINE_MEDIUM_DASHED ||
           line == OWF_LINE_DASH_DOT || line == OWF_LINE_MEDIUM_DASH_DOT || line == OWF_LINE_DASH_DOT_DOT ||
           line == OWF_LINE_MEDIUM_DASH_DOT_DOT || line == OWF_LINE_SLANTED_DASH_DOT;
}

static int draw_list(owf_pdf *pdf, const oc_core *core, const oc_draw_list *list,
                     int ox, int oy, int gridlines, int headings)
{
    int k;
    for (k = 0; k < list->n; k++) {
        const oc_draw_item *it = &list->items[k];
        int x = ox + it->x * PX_TWIPS, y = oy + it->y * PX_TWIPS;
        int w = it->w * PX_TWIPS, h = it->h * PX_TWIPS;
        unsigned long rgb = it->rgb == OWF_COLOUR_AUTO ? 0 : it->rgb;
        int rc = 0;
        if ((!headings && (it->kind == OC_DRAW_HEADER || it->role == OC_ROLE_HEADER ||
                           it->role == OC_ROLE_HEADER_SELECTED || it->role == OC_ROLE_HEADER_LINE)) ||
            (!gridlines && it->role == OC_ROLE_GRID) || it->role == OC_ROLE_SELECTION ||
            it->role == OC_ROLE_CURSOR || it->role == OC_ROLE_EDIT || it->role == OC_ROLE_NOTE ||
            it->role == OC_ROLE_NOTE_LINE || it->role == OC_ROLE_FILTER || it->role == OC_ROLE_FREEZE ||
            it->kind == OC_DRAW_DROPDOWN || it->kind == OC_DRAW_FILTER || it->kind == OC_DRAW_CARET)
            continue;
        switch (it->kind) {
        case OC_DRAW_FILL:
            if (it->role == OC_ROLE_PAPER) rgb = 0xFFFFFFUL;
            else if (it->role == OC_ROLE_HEADER || it->role == OC_ROLE_HEADER_SELECTED) rgb = 0xE7E7E7UL;
            rc = owf_pdf_rect(pdf, x, y, w, h, OWF_SHADE(rgb), 0, 0);
            break;
        case OC_DRAW_LINE: {
            int xy[4], lw = oc_line_weight(it->line) * PX_TWIPS;
            xy[0] = x; xy[1] = y; xy[2] = x + w; xy[3] = y + h;
            if (it->role == OC_ROLE_GRID) rgb = 0xD9D9D9UL;
            if (line_dashed(it->line)) owf_pdf_dash(pdf, 1);
            rc = owf_pdf_path(pdf, xy, 2, 0, 0, OWF_SHADE(rgb), lw > 0 ? lw : 10);
            owf_pdf_dash(pdf, 0);
            break;
        }
        case OC_DRAW_TEXT:
        case OC_DRAW_HEADER: {
            int size = it->size > 0 ? it->size : OC_DEFAULT_FONT_TWIPS;
            int scaled = size * core->zoom / 100;
            int baseline, tx, tw, natural;
            if (!it->text) break;
            if (scaled < 40) scaled = 40;
            natural = owf_pdf_text_width(it->text, strlen(it->text), font_kind(core->book, it->font),
                                         it->flags & (OWF_BOLD | OWF_ITALIC), scaled);
            if (it->kind == OC_DRAW_HEADER) {
                tx = x + (w - natural) / 2;
                if (it->align == OC_ALIGN_LEFT) tx = x + it->text_x * PX_TWIPS;
                tw = natural;
                baseline = y + (h + scaled * 7 / 10) / 2;
                rgb = 0x303030UL;
            } else {
                int cy = oy + it->y * PX_TWIPS, ch = it->h * PX_TWIPS;
                int right = ox + (it->clip_x + it->clip_w) * PX_TWIPS;
                tx = ox + it->text_x * PX_TWIPS;
                if (tx < ox + it->clip_x * PX_TWIPS) tx = ox + it->clip_x * PX_TWIPS;
                tw = natural;
                if (tw > right - tx) tw = right - tx;
                if (tw <= 0) break;
                if (it->valign == OWF_VALIGN_TOP) baseline = cy + scaled * 4 / 5 + 15;
                else if (it->valign == OWF_VALIGN_CENTRE) baseline = cy + (ch + scaled * 7 / 10) / 2;
                else baseline = cy + ch - scaled / 5 - 15;
            }
            rc = owf_pdf_text(pdf, tx, baseline, tw, it->text, strlen(it->text),
                              font_kind(core->book, it->font), it->flags, scaled, rgb);
            break;
        }
        case OC_DRAW_SEGMENT: {
            int xy[4] = { x, y, x + w, y + h };
            rc = owf_pdf_path(pdf, xy, 2, 0, 0, OWF_SHADE(rgb), PX_TWIPS);
            break;
        }
        case OC_DRAW_POLY:
            if (it->points && it->npoints >= 3) {
                int *xy = malloc((size_t)it->npoints * 2 * sizeof *xy);
                int i;
                if (!xy) return OWF_ERR_MEMORY;
                for (i = 0; i < it->npoints; i++) {
                    xy[i * 2] = ox + it->points[i * 2] * PX_TWIPS;
                    xy[i * 2 + 1] = oy + it->points[i * 2 + 1] * PX_TWIPS;
                }
                rc = owf_pdf_path(pdf, xy, it->npoints, 1, OWF_SHADE(rgb), 0, 0);
                free(xy);
            }
            break;
        case OC_DRAW_MARK: {
            int xy[6] = { x + w, y, x + w, y + h, x, y };
            rc = owf_pdf_path(pdf, xy, 3, 1, OWF_SHADE(0xD01010UL), 0, 0);
            break;
        }
        default:
            break;
        }
        if (rc < 0)
            return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

static int print_sheet(oc_core *core, owf_pdf *pdf, const oc_pdf_options *o,
                       int page_w, int page_h, int *page_number)
{
    const owf_sheet *s = oc_sheet(core);
    oc_draw_list list;
    unsigned rows, cols, row, col, next_row, next_col;
    int body_w = page_w - 2 * MARGIN, body_h = page_h - 2 * MARGIN - PAGE_HEAD - PAGE_FOOT;
    int view_w = body_w / PX_TWIPS, view_h = body_h / PX_TWIPS;
    int zoom = 100, rc = OWF_OK;
    char page_label[64];
    paper_range(core, &rows, &cols);
    if (o->fit_width) {
        long width = span_twips(s, 1, cols);
        if (o->headings) width += 600;
        if (width > body_w)
            zoom = (int)((long)body_w * 100 / width);
        if (zoom < 25) zoom = 25;
    }
    oc_view_set_zoom(core, zoom);
    oc_view_set_headers(core, o->headings ? 40 : 0, o->headings ? 18 : 0);
    oc_view_set_row_numbers(core, 0, 0);
    oc_view_set_size(core, view_w, view_h);
    core->chart_sel = 0;
    oc_draw_list_init(&list);
    row = s->freeze_rows;
    do {
        next_row = next_line(core, 0, row, rows);
        col = s->freeze_cols;
        do {
            int drawn_w, drawn_h;
            next_col = next_line(core, 1, col, cols);
            oc_view_scroll_to(core, row, col);
            if (owf_pdf_page(pdf, page_w, page_h) < 0) { rc = OWF_ERR_MEMORY; goto out; }
            (*page_number)++;
            owf_pdf_rect(pdf, 0, 0, page_w, page_h, OWF_SHADE(0xFFFFFFUL), 0, 0);
            if (s->name) {
                int sw = owf_pdf_text_width(s->name, strlen(s->name), OWF_FONT_SANS, OWF_BOLD, 180);
                owf_pdf_text(pdf, MARGIN, MARGIN + 200, sw, s->name, strlen(s->name),
                             OWF_FONT_SANS, OWF_BOLD, 180, 0x303030UL);
            }
            snprintf(page_label, sizeof page_label, "Page %d", *page_number);
            {
                int pw = owf_pdf_text_width(page_label, strlen(page_label), OWF_FONT_SANS, 0, 160);
                owf_pdf_text(pdf, page_w - MARGIN - pw, page_h - MARGIN, pw, page_label, strlen(page_label),
                         OWF_FONT_SANS, 0, 160, 0x505050UL);
            }
            drawn_w = page_pixels(core, 1, col, next_col, cols);
            drawn_h = page_pixels(core, 0, row, next_row, rows);
            oc_view_set_size(core, drawn_w, drawn_h);
            if (oc_view_draw(core, &list) != OWF_OK) { rc = OWF_ERR_MEMORY; goto out; }
            rc = draw_list(pdf, core, &list, MARGIN, MARGIN + PAGE_HEAD, o->gridlines, o->headings);
            if (rc != OWF_OK) goto out;
            oc_view_set_size(core, view_w, view_h);
            col = next_col;
        } while (col < cols);
        row = next_row;
    } while (row < rows);
out:
    oc_draw_list_free(&list);
    return rc;
}

int oc_core_pdf(const oc_core *source, const oc_pdf_options *given,
                unsigned char **data, size_t *length, owf_report *report)
{
    oc_pdf_options def, o;
    owf_book *copy;
    oc_core *core;
    owf_pdf *pdf;
    int page_w = A4_W, page_h = A4_H, first, last, i, pages = 0, rc = OWF_OK, printed = 0;
    if (!source || !data || !length)
        return OWF_ERR_FORMAT;
    *data = NULL;
    *length = 0;
    oc_pdf_options_init(&def);
    o = given ? *given : def;
    if (o.sheet < -1 || o.sheet >= source->book->nsheets)
        return OWF_ERR_FORMAT;
    copy = oc_book_copy(source->book);
    if (!copy)
        return OWF_ERR_MEMORY;
    core = oc_core_new(copy);
    if (!core)
        return OWF_ERR_MEMORY;
    pdf = owf_pdf_new();
    if (!pdf) { oc_core_free(core); return OWF_ERR_MEMORY; }
    if (o.landscape) { int t = page_w; page_w = page_h; page_h = t; }
    first = o.sheet >= 0 ? o.sheet : 0;
    last = o.sheet >= 0 ? o.sheet + 1 : core->book->nsheets;
    for (i = first; i < last; i++) {
        if (o.sheet < 0 && core->book->sheets[i].hidden)
            continue;
        if (oc_core_set_sheet(core, i) != OWF_OK) { rc = OWF_ERR_FORMAT; break; }
        rc = print_sheet(core, pdf, &o, page_w, page_h, &pages);
        if (rc != OWF_OK) break;
        printed++;
    }
    if (rc == OWF_OK && !printed)
        rc = OWF_ERR_FORMAT;
    if (rc == OWF_OK)
        rc = owf_pdf_finish(pdf, "GFOpenCalc", data, length, report);
    owf_pdf_free(pdf);
    oc_core_free(core);
    return rc;
}

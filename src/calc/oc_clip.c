/*
 * OpenCalc's core: cut, copy and paste. The system clipboard gets the
 * cells as tab separated text (as Excel and LibreOffice put them there);
 * the core keeps the cells themselves, with formulas and styles, and
 * pastes those when the clipboard still holds its own text.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

#include <stdlib.h>
#include <string.h>

void oc_clip_free(oc_clip *clip)
{
    int i;
    for (i = 0; i < clip->ncells; i++)
        oc_free_cell(&clip->cells[i]);
    free(clip->cells);
    free(clip->styles);
    owf_free(clip->text);
    memset(clip, 0, sizeof *clip);
}

/* ---- Text out ---- */

typedef struct {
    char *p;
    size_t n, cap;
    int failed;
} sbuf;

static void put(sbuf *b, const char *s, size_t n)
{
    if (b->failed)
        return;
    if (b->n + n + 1 > b->cap) {
        size_t want = (b->n + n + 1) * 2;
        char *q;
        if (want < 256)
            want = 256;
        if (!(q = owf_realloc(b->p, want))) {
            b->failed = 1;
            return;
        }
        b->p = q;
        b->cap = want;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

/* A value as Excel writes it to the clipboard: quoted when it holds a tab,
 * a line break or a quote. */
static void put_field(sbuf *b, const char *s)
{
    const char *p;
    for (p = s; *p && *p != '\t' && *p != '\r' && *p != '\n' && *p != '"'; p++)
        ;
    if (!*p) {
        put(b, s, (size_t)(p - s));
        return;
    }
    put(b, "\"", 1);
    for (p = s; *p; p++) {
        if (*p == '"')
            put(b, "\"", 1);
        put(b, p, 1);
    }
    put(b, "\"", 1);
}

static char *range_text(const oc_core *core, const owf_range *r, size_t *length)
{
    sbuf b;
    unsigned row, col;
    char cell[512];
    memset(&b, 0, sizeof b);
    for (row = r->first_row; row <= r->last_row; row++) {
        for (col = r->first_col; col <= r->last_col; col++) {
            if (col > r->first_col)
                put(&b, "\t", 1);
            oc_core_cell_text(core, core->sheet, row, col, cell, sizeof cell);
            put_field(&b, cell);
        }
        put(&b, "\n", 1);
    }
    if (b.failed || !b.p) {
        owf_free(b.p);
        return NULL;
    }
    if (length)
        *length = b.n;
    return b.p;
}

/* The range copy and paste use: the selection's last, cut down to the
 * used part for whole rows and columns. 0 when it is empty or too big. */
static int copy_range(const oc_core *core, owf_range *r)
{
    const oc_sheetstate *st = oc_state_const(core);
    if (!oc_range_clip(core, &st->sel.ranges[st->sel.nranges - 1], r))
        return 0;
    return (long)(r->last_row - r->first_row + 1) * (long)(r->last_col - r->first_col + 1) <= OC_MAX_RANGE_CELLS;
}

char *oc_core_selection_text(const oc_core *core, size_t *length)
{
    owf_range r;
    if (length)
        *length = 0;
    if (!copy_range(core, &r))
        return NULL;
    return range_text(core, &r, length);
}

char *oc_core_copy(oc_core *core, size_t *length)
{
    const owf_sheet *s;
    owf_range r;
    char *text, *keep;
    size_t n = 0;
    int i, count = 0;
    if (length)
        *length = 0;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!copy_range(core, &r) || !(text = range_text(core, &r, &n)))
        return NULL;
    oc_clip_free(&core->clip);
    s = oc_sheet(core);
    for (i = 0; i < s->ncells; i++)
        if (s->cells[i].row >= r.first_row && s->cells[i].row <= r.last_row && s->cells[i].col >= r.first_col &&
            s->cells[i].col <= r.last_col)
            count++;
    keep = owf_malloc(n + 1);
    if (count) {
        core->clip.cells = calloc((size_t)count, sizeof *core->clip.cells);
        core->clip.styles = calloc((size_t)count, sizeof *core->clip.styles);
    }
    if (!keep || (count && (!core->clip.cells || !core->clip.styles))) {
        owf_free(keep);
        oc_clip_free(&core->clip);
        return text;                                     /* the text still goes to the clipboard */
    }
    memcpy(keep, text, n + 1);
    for (i = 0; i < s->ncells; i++) {
        const owf_cell *c = &s->cells[i];
        owf_cell *d;
        if (c->row < r.first_row || c->row > r.last_row || c->col < r.first_col || c->col > r.last_col)
            continue;
        d = &core->clip.cells[core->clip.ncells];
        if (!oc_copy_cell(d, c)) {
            owf_free(keep);
            oc_clip_free(&core->clip);
            return text;
        }
        d->row -= r.first_row;
        d->col -= r.first_col;
        core->clip.styles[core->clip.ncells] = *oc_cell_style(core, c, c->col);
        core->clip.ncells++;
    }
    core->clip.valid = 1;
    core->clip.rows = r.last_row - r.first_row + 1;
    core->clip.cols = r.last_col - r.first_col + 1;
    core->clip.row0 = r.first_row;
    core->clip.col0 = r.first_col;
    core->clip.text = keep;
    core->clip.length = n;
    if (length)
        *length = n;
    return text;
}

/* A cut copies, as Excel's does: the cells stay until the paste moves
 * them (and what reads them follows). */
char *oc_core_cut(oc_core *core, size_t *length)
{
    char *text = oc_core_copy(core, length);
    if (text && core->clip.valid) {
        core->clip.cut = 1;
        core->clip.cut_sheet = core->sheet;
        core->clip.cut_changes = core->changes;
    }
    return text;
}

/* ---- Pasting ---- */

/* The target: the selection's last range when it is a whole number of
 * copies of rows x cols, else rows x cols from its first cell. 0 when it
 * would go off the sheet. */
static int paste_target(oc_core *core, unsigned rows, unsigned cols, owf_range *t, int tile)
{
    const oc_sheetstate *st = oc_state_const(core);
    owf_range sel = st->sel.ranges[st->sel.nranges - 1], c;
    unsigned sr, sc;
    if (!oc_range_clip(core, &sel, &c))
        c = sel;
    sr = c.last_row - c.first_row + 1;
    sc = c.last_col - c.first_col + 1;
    if (tile && sr >= rows && sc >= cols && sr % rows == 0 && sc % cols == 0 &&
        (long)sr * (long)sc <= OC_MAX_RANGE_CELLS && !(sel.first_row == 0 && sel.last_row == OC_MAX_ROWS - 1) &&
        !(sel.first_col == 0 && sel.last_col == OC_MAX_COLS - 1)) {
        *t = c;
        return 1;
    }
    t->first_row = sel.first_row;
    t->first_col = sel.first_col;
    if ((unsigned long)sel.first_row + rows > OC_MAX_ROWS || (unsigned long)sel.first_col + cols > OC_MAX_COLS)
        return 0;
    t->last_row = sel.first_row + rows - 1;
    t->last_col = sel.first_col + cols - 1;
    return 1;
}

static void paste_done(oc_core *core, const owf_range *t)
{
    oc_sheetstate *st = oc_state(core);
    unsigned row = st->sel.row, col = st->sel.col;
    oc_core_select_range(core, t);
    st = oc_state(core);
    if (row >= t->first_row && row <= t->last_row && col >= t->first_col && col <= t->last_col) {
        st->sel.row = row;
        st->sel.col = col;
    }
}

/* A cut pasted: the cells move, with the references to them (the
 * engine's owf_calc_move_range); the whole book is the undo step, as
 * references anywhere may change. A cut pastes once. */
static int paste_move(oc_core *core)
{
    oc_clip *clip = &core->clip;
    owf_range from, t;
    oc_undo *u;
    int rc;
    if (!paste_target(core, clip->rows, clip->cols, &t, 0))
        return OWF_ERR_UNSUPPORTED;
    from.first_row = clip->row0;
    from.first_col = clip->col0;
    from.last_row = clip->row0 + clip->rows - 1;
    from.last_col = clip->col0 + clip->cols - 1;
    u = oc_undo_new(core, OC_UNDO_BOOK);
    if (u && oc_undo_save_book(core, u) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    rc = owf_calc_move_range(core->calc, clip->cut_sheet, &from, core->sheet,
                             (int)((long)t.first_row - (long)from.first_row),
                             (int)((long)t.first_col - (long)from.first_col));
    if (rc == OWF_ERR_UNSUPPORTED) {
        oc_undo_free(u);
        return rc;
    }
    oc_undo_push(core, u);
    clip->cut = 0;
    clip->valid = 0;
    oc_changed(core);
    if (oc_recalc(core, rc == OWF_ERR_MEMORY) == OWF_ERR_MEMORY)
        rc = OWF_ERR_MEMORY;
    paste_done(core, &t);
    return rc;
}

static int paste_cells(oc_core *core)
{
    oc_clip *clip = &core->clip;
    owf_range t;
    oc_undo *u;
    unsigned tr, tc, dr, dc;
    int rc = OWF_OK, rebuild = 0;
    if (clip->cut && clip->cut_sheet < core->book->nsheets && clip->cut_changes == core->changes)
        return paste_move(core);
    if (!paste_target(core, clip->rows, clip->cols, &t, 1))
        return OWF_ERR_UNSUPPORTED;
    if ((long)(t.last_row - t.first_row + 1) * (long)(t.last_col - t.first_col + 1) > OC_MAX_RANGE_CELLS)
        return OWF_ERR_UNSUPPORTED;
    u = oc_undo_new(core, OC_UNDO_CELLS);
    if (u && oc_undo_save_range(core, u, core->sheet, &t, 0) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    oc_undo_push(core, u);
    for (tr = t.first_row; tr <= t.last_row && rc != OWF_ERR_MEMORY; tr += clip->rows)
        for (tc = t.first_col; tc <= t.last_col && rc != OWF_ERR_MEMORY; tc += clip->cols) {
            int k = 0;
            for (dr = 0; dr < clip->rows && rc != OWF_ERR_MEMORY; dr++)
                for (dc = 0; dc < clip->cols; dc++) {
                    const owf_cell *c = NULL;
                    const owf_cellstyle *style = NULL;
                    char *moved = NULL;
                    int one;
                    while (k < clip->ncells && (clip->cells[k].row < dr || (clip->cells[k].row == dr && clip->cells[k].col < dc)))
                        k++;
                    if (k < clip->ncells && clip->cells[k].row == dr && clip->cells[k].col == dc) {
                        c = &clip->cells[k];
                        style = &clip->styles[k];
                    }
                    if (c && c->formula && !clip->cut) {
                        moved = owf_calc_move_formula(c->formula, (int)((long)tr - (long)clip->row0),
                                                      (int)((long)tc - (long)clip->col0));
                        if (!moved) {
                            rc = OWF_ERR_MEMORY;
                            break;
                        }
                    }
                    one = oc_put_cell(core, core->sheet, tr + dr, tc + dc, c, moved, style, &rebuild);
                    owf_free(moved);
                    if (one == OWF_ERR_MEMORY) {
                        rc = one;
                        break;
                    }
                }
        }
    oc_changed(core);
    if (oc_recalc(core, rebuild) == OWF_ERR_MEMORY)
        rc = OWF_ERR_MEMORY;
    paste_done(core, &t);
    return rc;
}

/* Tab separated text into fields: rows of fields, each NUL-ended in buf. */
typedef struct {
    char *buf;                  /* the fields, one after another */
    unsigned rows, cols;
    unsigned *row_fields;       /* fields in each row */
    long nfields;
} tsv;

static void tsv_free(tsv *t)
{
    free(t->buf);
    free(t->row_fields);
    memset(t, 0, sizeof *t);
}

static int tsv_parse(const char *text, size_t length, tsv *t)
{
    size_t i = 0, o = 0;
    unsigned fields = 0, caprows = 16;
    memset(t, 0, sizeof *t);
    if (!(t->buf = malloc(length + 1)) || !(t->row_fields = malloc(caprows * sizeof *t->row_fields))) {
        tsv_free(t);
        return 0;
    }
    while (i < length) {
        /* one field */
        if (text[i] == '"') {
            i++;
            while (i < length) {
                if (text[i] == '"' && i + 1 < length && text[i + 1] == '"') {
                    t->buf[o++] = '"';
                    i += 2;
                } else if (text[i] == '"') {
                    i++;
                    break;
                } else
                    t->buf[o++] = text[i++];
            }
            while (i < length && text[i] != '\t' && text[i] != '\n' && text[i] != '\r')
                t->buf[o++] = text[i++];
        } else {
            while (i < length && text[i] != '\t' && text[i] != '\n' && text[i] != '\r')
                t->buf[o++] = text[i++];
        }
        t->buf[o++] = 0;
        fields++;
        t->nfields++;
        if (t->nfields > OC_MAX_RANGE_CELLS) {
            tsv_free(t);
            return 0;
        }
        if (i < length && text[i] == '\t') {
            i++;
            if (i == length) {                           /* a tab at the very end: one more, empty */
                t->buf[o++] = 0;
                fields++;
                t->nfields++;
            } else
                continue;
        }
        /* the row ends */
        if (t->rows == caprows) {
            unsigned *p = realloc(t->row_fields, (size_t)caprows * 2 * sizeof *p);
            if (!p) {
                tsv_free(t);
                return 0;
            }
            t->row_fields = p;
            caprows *= 2;
        }
        t->row_fields[t->rows++] = fields;
        if (fields > t->cols)
            t->cols = fields;
        fields = 0;
        if (i < length && text[i] == '\r')
            i++;
        if (i < length && text[i] == '\n')
            i++;
    }
    return t->rows > 0;
}

static int paste_text(oc_core *core, const char *text, size_t length)
{
    tsv t;
    owf_range target;
    oc_undo *u;
    unsigned r, c;
    const char *f;
    int rc = OWF_OK, bad = 0;
    if (!tsv_parse(text, length, &t))
        return length ? OWF_ERR_MEMORY : OWF_ERR_UNSUPPORTED;
    if (!paste_target(core, t.rows, t.cols, &target, t.rows == 1 && t.cols == 1)) {
        tsv_free(&t);
        return OWF_ERR_UNSUPPORTED;
    }
    u = oc_undo_new(core, OC_UNDO_CELLS);
    if (u && oc_undo_save_range(core, u, core->sheet, &target, 0) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    oc_undo_push(core, u);
    if (t.rows == 1 && t.cols == 1) {                    /* one value: into every cell of the target */
        for (r = target.first_row; r <= target.last_row && rc == OWF_OK; r++)
            for (c = target.first_col; c <= target.last_col; c++) {
                int one = owf_calc_enter(core->calc, core->sheet, r, c, t.buf);
                if (one == OWF_ERR_FORMAT)
                    bad = 1;
                else if (one != OWF_OK) {
                    rc = one;
                    break;
                }
            }
    } else {
        f = t.buf;
        for (r = 0; r < t.rows && rc == OWF_OK; r++)
            for (c = 0; c < t.cols; c++) {
                int one;
                const char *in = c < t.row_fields[r] ? f : "";
                one = owf_calc_enter(core->calc, core->sheet, target.first_row + r, target.first_col + c, in);
                if (c < t.row_fields[r])
                    f += strlen(f) + 1;
                if (one == OWF_ERR_FORMAT)
                    bad = 1;
                else if (one != OWF_OK) {
                    rc = one;
                    break;
                }
            }
    }
    tsv_free(&t);
    oc_changed(core);
    if (oc_recalc(core, 0) == OWF_ERR_MEMORY)
        rc = OWF_ERR_MEMORY;
    paste_done(core, &target);
    return rc == OWF_OK && bad ? OWF_ERR_FORMAT : rc;
}

int oc_core_paste(oc_core *core, const char *text, size_t length)
{
    if (core->edit_mode) {                               /* typing: the text goes in at the caret, one line */
        size_t n = 0;
        while (text && n < length && text[n] != '\n' && text[n] != '\r')
            n++;
        return text ? oc_edit_type(core, text, n) : OWF_OK;
    }
    if (core->clip.valid && (!text || (length == core->clip.length && !memcmp(text, core->clip.text, length))))
        return paste_cells(core);
    if (!text)
        return OWF_ERR_UNSUPPORTED;
    return paste_text(core, text, length);
}

/* ---- The Amiga clipboard's text: ISO-8859-1 ---- */

char *oc_text_to_latin1(const char *utf8, size_t length, size_t *out_length)
{
    const unsigned char *s = (const unsigned char *)utf8;
    char *out = owf_malloc(length + 1);
    size_t i = 0, n = 0;
    if (!out)
        return NULL;
    while (i < length) {
        unsigned c = s[i], cp, need, k;
        if (c < 0x80) {
            out[n++] = (char)c;
            i++;
            continue;
        }
        need = c >= 0xF0 && c < 0xF8 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : 0;
        if (c >= 0xF8)
            need = 0;
        cp = need == 1 ? c & 0x1F : need == 2 ? c & 0x0F : c & 0x07;
        for (k = 1; k <= need; k++) {
            if (i + k >= length || (s[i + k] & 0xC0) != 0x80)
                break;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if (!need || k <= need) {                        /* broken: one byte, one '?' */
            out[n++] = '?';
            i++;
            continue;
        }
        out[n++] = cp <= 0xFF ? (char)cp : '?';
        i += need + 1;
    }
    out[n] = 0;
    if (out_length)
        *out_length = n;
    return out;
}

char *oc_text_from_latin1(const char *latin1, size_t length, size_t *out_length)
{
    const unsigned char *s = (const unsigned char *)latin1;
    char *out = owf_malloc(length * 2 + 1);
    size_t i, n = 0;
    if (!out)
        return NULL;
    for (i = 0; i < length; i++) {
        if (s[i] < 0x80)
            out[n++] = (char)s[i];
        else {
            out[n++] = (char)(0xC0 | (s[i] >> 6));
            out[n++] = (char)(0x80 | (s[i] & 0x3F));
        }
    }
    out[n] = 0;
    if (out_length)
        *out_length = n;
    return out;
}

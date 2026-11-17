/* GFOpenCalc's portable core: spelling through a worksheet's text cells
 * (opencalc_core.h, "Spelling"), with GFOffice's one spell checker
 * (owfspell.library, libowf/include/owf_spell.h). Only typed text is
 * checked: not numbers, not formulas and not what a formula shows.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "oc_internal.h"

#include <stdio.h>
#include <string.h>

static const char *spell_text(const owf_cell *cell)
{
    return cell->type == OWF_VALUE_TEXT && !cell->formula && cell->text ? cell->text : NULL;
}

static void spell_hit(oc_spell_hit *hit, const owf_cell *cell, size_t a, size_t b)
{
    size_t m = b - a < sizeof hit->word - 1 ? b - a : sizeof hit->word - 1;
    hit->row = cell->row;
    hit->col = cell->col;
    hit->start = a;
    hit->end = b;
    memcpy(hit->word, cell->text + a, m);
    hit->word[m] = 0;
}

int oc_core_spell_next(oc_core *core, owf_spell *spell, size_t from, oc_spell_hit *hit)
{
    oc_sheetstate *state;
    owf_sheet *sheet;
    int at, k, first;
    size_t a, b;
    if (!core || !spell || !hit) return -1;
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    state = oc_state(core);
    sheet = oc_sheet(core);
    if (!sheet->ncells) return 0;
    /* the first cell at or after the active one, in the sheet's order (rows, then columns) */
    for (first = 0; first < sheet->ncells; ++first) {
        const owf_cell *c = &sheet->cells[first];
        if (c->row > state->sel.row || (c->row == state->sel.row && c->col >= state->sel.col)) break;
    }
    for (k = 0; k <= sheet->ncells; ++k) {
        const owf_cell *cell;
        const char *text;
        size_t start = 0, n;
        at = (first + k) % sheet->ncells;
        cell = &sheet->cells[at];
        if (!(text = spell_text(cell))) continue;
        n = strlen(text);
        if (cell->row == state->sel.row && cell->col == state->sel.col) {
            if (k == 0) start = from;               /* the active cell, after the word already found */
            else if (from) n = from;                /* back round: its start, up to there */
            else continue;
        }
        if (start < n && owf_spell_scan(spell, text, start, n, &a, &b)) {
            spell_hit(hit, cell, a, b);
            oc_collapse_to(core, cell->row, cell->col);
            oc_view_show_cell(core, cell->row, cell->col);
            return 1;
        }
    }
    return 0;
}

int oc_core_spell_replace(oc_core *core, const oc_spell_hit *hit, const char *with)
{
    const owf_cell *cell;
    char *input;
    size_t n, w;
    int rc;
    if (!core || !hit || !with) return OWF_ERR_FORMAT;
    cell = oc_find(core, core->sheet, hit->row, hit->col);
    if (!cell || !spell_text(cell)) return OWF_ERR_UNSUPPORTED;
    n = strlen(cell->text);
    w = strlen(with);
    if (hit->start > hit->end || hit->end > n || strncmp(cell->text + hit->start, hit->word, hit->end - hit->start))
        return OWF_ERR_UNSUPPORTED;            /* the cell changed since the word was found */
    if (!(input = (char *)owf_malloc(n - (hit->end - hit->start) + w + 2))) return OWF_ERR_MEMORY;
    input[0] = '\'';                            /* text stays text, whatever it looks like */
    memcpy(input + 1, cell->text, hit->start);
    memcpy(input + 1 + hit->start, with, w);
    strcpy(input + 1 + hit->start + w, cell->text + hit->end);
    rc = oc_core_set_input(core, core->sheet, hit->row, hit->col, input);
    owf_free(input);
    return rc;
}

/* Every misspelt word of the sheet's text cells: how many; with out, "A1 word" lines while they fit. */
static int spell_walk(oc_core *core, owf_spell *spell, char *out, size_t size)
{
    const owf_sheet *sheet = oc_sheet(core);
    size_t used = 0;
    int n = 0, i, full = 0;
    for (i = 0; i < sheet->ncells; ++i) {
        const owf_cell *cell = &sheet->cells[i];
        const char *text = spell_text(cell);
        size_t from = 0, len, a, b;
        if (!text) continue;
        len = strlen(text);
        while (from < len && owf_spell_scan(spell, text, from, len, &a, &b)) {
            from = b;
            n++;
            if (out && !full) {
                char where[16];
                size_t need;
                oc_cell_name(cell->row, cell->col, where);
                need = (used ? 1 : 0) + strlen(where) + 1 + (b - a);
                if (used + need + 1 > size) {
                    full = 1;
                    continue;
                }
                used += (size_t)snprintf(out + used, size - used, "%s%s %.*s", used ? "\n" : "", where, (int)(b - a),
                                         text + a);
            }
        }
    }
    return n;
}

static int rx_word(const char *s, const char *word)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != word[i]) return 0;
    }
    return !s[n] || s[n] == ' ' || s[n] == '\t';
}

int oc_spell_rexx(oc_core *core, owf_spell *spell, const char *args, char *out, size_t size)
{
    int rc;
    if (!out || size < 2) return 10;
    out[0] = 0;
    if (!spell) {
        snprintf(out, size, "No dictionary");
        return 5;
    }
    if (!args) args = "";
    while (*args == ' ' || *args == '\t') args++;
    if (!*args || rx_word(args, "COUNT") || rx_word(args, "LIST")) {
        int list = *args && rx_word(args, "LIST"), n;
        if (!core) {
            snprintf(out, size, "No workbook");
            return 10;
        }
        n = spell_walk(core, spell, list ? out : NULL, size);
        if (!list) snprintf(out, size, "%d", n);
        return 0;
    }
    /* CHECK, SUGGEST, ADD and IGNORE: every program's (owf_spell_command) */
    if ((rc = owf_spell_command(spell, args, out, size)) >= 0) return rc;
    snprintf(out, size, "SPELL takes COUNT, LIST, CHECK, SUGGEST, ADD or IGNORE");
    return 10;
}

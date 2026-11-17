/*
 * GFOpenProject: spelling through a plan's task names and notes (openproject_spell.h), with GFOffice's one spell
 * checker (owfspell.library, libowf/include/owf_spell.h).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_spell.h"

#include <stdio.h>
#include <string.h>

static const char *field_text(const opj_doc *doc, int row, int field)
{
    const owf_ptask *t = &doc->plan->tasks[row];
    const char *s = field == OPJ_SPELL_NAME ? t->name : t->notes;
    return s ? s : "";
}

int opj_spell_next(const opj_doc *doc, owf_spell *spell, int row, int field, size_t from, opj_spell_hit *hit)
{
    int n, k, total;
    if (!doc || !doc->plan || !spell || !hit) return -1;
    n = doc->plan->ntasks;
    if (!n) return 0;
    if (row < 0 || row >= n || (field != OPJ_SPELL_NAME && field != OPJ_SPELL_NOTES)) {
        row = 0;
        field = OPJ_SPELL_NAME;
        from = 0;
    }
    total = 2 * n;
    for (k = 0; k <= total; ++k) {
        int at = (2 * row + field + k) % total, r = at / 2, f = at % 2;
        const char *text = field_text(doc, r, f);
        size_t start = 0, len = strlen(text), a, b;
        if (k == 0) start = from;                       /* the field given, after the word already found */
        else if (k == total) {                          /* back round: its start, up to there */
            if (!from) break;
            if (from < len) len = from;
        }
        if (start < len && owf_spell_scan(spell, text, start, len, &a, &b)) {
            size_t m = b - a < sizeof hit->word - 1 ? b - a : sizeof hit->word - 1;
            hit->row = r;
            hit->field = f;
            hit->start = a;
            hit->end = b;
            memcpy(hit->word, text + a, m);
            hit->word[m] = 0;
            return 1;
        }
    }
    return 0;
}

int opj_spell_replace(opj_doc *doc, const opj_spell_hit *hit, const char *with)
{
    const char *text;
    char *changed;
    size_t n, w;
    int rc;
    if (!doc || !doc->plan || !hit || !with || hit->row < 0 || hit->row >= doc->plan->ntasks ||
        (hit->field != OPJ_SPELL_NAME && hit->field != OPJ_SPELL_NOTES))
        return OWF_ERR_FORMAT;
    text = field_text(doc, hit->row, hit->field);
    n = strlen(text);
    w = strlen(with);
    if (hit->start > hit->end || hit->end > n || strlen(hit->word) != hit->end - hit->start ||
        memcmp(text + hit->start, hit->word, hit->end - hit->start))
        return OWF_ERR_UNSUPPORTED;                     /* the text changed since the word was found */
    if (!(changed = (char *)owf_malloc(n - (hit->end - hit->start) + w + 1))) return OWF_ERR_MEMORY;
    memcpy(changed, text, hit->start);
    memcpy(changed + hit->start, with, w);
    strcpy(changed + hit->start + w, text + hit->end);
    rc = hit->field == OPJ_SPELL_NAME ? opj_set_cell(doc, hit->row, OPJ_COL_NAME, changed)
                                      : opj_set_notes(doc, hit->row, changed);
    owf_free(changed);
    return rc;
}

/* Every misspelt word of the names and notes: how many; with out, "id NAME word" lines while they fit. */
static int spell_walk(const opj_doc *doc, owf_spell *spell, char *out, size_t size)
{
    size_t used = 0;
    int n = 0, row, f, full = 0;
    for (row = 0; row < doc->plan->ntasks; ++row)
        for (f = OPJ_SPELL_NAME; f <= OPJ_SPELL_NOTES; ++f) {
            const char *text = field_text(doc, row, f);
            size_t from = 0, len = strlen(text), a, b;
            while (from < len && owf_spell_scan(spell, text, from, len, &a, &b)) {
                char where[32];
                size_t need;
                from = b;
                n++;
                if (!out || full) continue;
                snprintf(where, sizeof where, "%d %s", row + 1, f == OPJ_SPELL_NAME ? "NAME" : "NOTES");
                need = (used ? 1 : 0) + strlen(where) + 1 + (b - a);
                if (used + need + 1 > size) {
                    full = 1;
                    continue;
                }
                used += (size_t)snprintf(out + used, size - used, "%s%s %.*s", used ? "\n" : "", where,
                                         (int)(b - a), text + a);
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

int opj_spell_rexx(const opj_doc *doc, owf_spell *spell, const char *args, char *out, size_t size)
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
        if (!doc || !doc->plan) {
            snprintf(out, size, "No plan");
            return 10;
        }
        n = spell_walk(doc, spell, list ? out : NULL, size);
        if (!list) snprintf(out, size, "%d", n);
        return 0;
    }
    /* CHECK, SUGGEST, ADD and IGNORE: every program's (owf_spell_command) */
    if ((rc = owf_spell_command(spell, args, out, size)) >= 0) return rc;
    snprintf(out, size, "SPELL takes COUNT, LIST, CHECK, SUGGEST, ADD or IGNORE");
    return 10;
}

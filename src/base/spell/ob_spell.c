/*
 * GFOpenBase: spelling through a form's own words (openbase_spell.h), with GFOffice's one spell checker
 * (owfspell.library, libowf/include/owf_spell.h). Kept out of src/base so the core's own tests need no libowf.
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openbase_spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_words(const ob_fitem *it)
{
    return it->kind == OB_FI_LABEL || it->kind == OB_FI_BOX || it->kind == OB_FI_BUTTON;
}

int ob_spell_next(const ob_form *f, owf_spell *spell, int item, size_t from, ob_spell_hit *hit)
{
    int n, k;
    if (!f || !spell || !hit) return -1;
    n = f->nitems;
    if (n <= 0) return 0;
    if (item < 0 || item >= n) {
        item = 0;
        from = 0;
    }
    for (k = 0; k <= n; ++k) {
        int i = (item + k) % n;
        const ob_fitem *it = &f->items[i];
        size_t start = 0, len, a, b;
        if (!has_words(it)) continue;
        len = strlen(it->text);
        if (k == 0) start = from;                       /* the item given, after the word already found */
        else if (k == n) {                              /* back round: its start, up to there */
            if (!from) break;
            if (from < len) len = from;
        }
        if (start < len && owf_spell_scan(spell, it->text, start, len, &a, &b)) {
            hit->item = i;
            hit->start = a;
            hit->end = b;
            memcpy(hit->word, it->text + a, b - a);     /* b - a < OB_LABEL_MAX: it is in a label */
            hit->word[b - a] = 0;
            return 1;
        }
    }
    return 0;
}

int ob_spell_replace(ob_form *f, const ob_spell_hit *hit, const char *with)
{
    ob_fitem *it;
    char buf[OB_LABEL_MAX];
    size_t n, w;
    if (!f || !hit || !with || hit->item < 0 || hit->item >= f->nitems) return OWF_ERR_FORMAT;
    it = &f->items[hit->item];
    if (!has_words(it)) return OWF_ERR_UNSUPPORTED;
    n = strlen(it->text);
    w = strlen(with);
    if (hit->start > hit->end || hit->end > n || strlen(hit->word) != hit->end - hit->start ||
        memcmp(it->text + hit->start, hit->word, hit->end - hit->start))
        return OWF_ERR_UNSUPPORTED;                     /* the item changed since the word was found */
    if (n - (hit->end - hit->start) + w >= sizeof buf) return OWF_ERR_FORMAT;
    memcpy(buf, it->text, hit->start);
    memcpy(buf + hit->start, with, w);
    strcpy(buf + hit->start + w, it->text + hit->end);
    strcpy(it->text, buf);
    return OWF_OK;
}

/* Every misspelt word of the saved forms: how many (-1 no memory or a form unread); with out, "form item word"
 * lines while they fit. */
static int spell_walk(ob_project *p, owf_spell *spell, char *out, size_t size)
{
    ob_form *f = (ob_form *)malloc(sizeof *f);          /* a form is too big for the stack */
    size_t used = 0;
    int n = 0, nforms, k, i, full = 0;
    if (!f) return -1;
    nforms = ob_form_count(p);
    for (k = 0; k < nforms; ++k) {
        const char *name = ob_form_name(p, k);
        char copy[OB_NAME_MAX];
        if (!name) continue;
        snprintf(copy, sizeof copy, "%s", name);        /* the name's buffer may be the project's own */
        if (!ob_form_get(p, copy, f)) continue;
        for (i = 0; i < f->nitems; ++i) {
            const ob_fitem *it = &f->items[i];
            size_t from = 0, len = strlen(it->text), a, b;
            if (!has_words(it)) continue;
            while (from < len && owf_spell_scan(spell, it->text, from, len, &a, &b)) {
                char where[OB_NAME_MAX + 16];
                size_t need;
                from = b;
                n++;
                if (!out || full) continue;
                snprintf(where, sizeof where, "%s %d", f->name, i + 1);
                need = (used ? 1 : 0) + strlen(where) + 1 + (b - a);
                if (used + need + 1 > size) {
                    full = 1;
                    continue;
                }
                used += (size_t)snprintf(out + used, size - used, "%s%s %.*s", used ? "\n" : "", where,
                                         (int)(b - a), it->text + a);
            }
        }
    }
    free(f);
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

int ob_spell_rexx(ob_project *p, owf_spell *spell, const char *args, char *out, size_t size)
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
        if (!p) {
            snprintf(out, size, "No project");
            return 10;
        }
        n = spell_walk(p, spell, list ? out : NULL, size);
        if (n < 0) {
            out[0] = 0;
            snprintf(out, size, "Not enough memory");
            return 20;
        }
        if (!list) snprintf(out, size, "%d", n);
        return 0;
    }
    /* CHECK, SUGGEST, ADD and IGNORE: every program's (owf_spell_command) */
    if ((rc = owf_spell_command(spell, args, out, size)) >= 0) return rc;
    snprintf(out, size, "SPELL takes COUNT, LIST, CHECK, SUGGEST, ADD or IGNORE");
    return 10;
}

/*
 * GFOpenDiag: spelling through a page's labels (opendiag_spell.h), with GFOffice's one spell checker
 * (owfspell.library, libowf/include/owf_spell.h). The program's, not owfdiag.library's
 * (scripts/owfdiag-sources.sh leaves it out).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_spell.h"

#include <stdio.h>
#include <string.h>

static int label_html(const owf_dcell *c)
{
    char buf[8];
    return c->style && owf_dstyle_get(c->style, "html", buf, sizeof buf) && !strcmp(buf, "1");
}

/* A shape's or connector's label as it shows (owf_malloc'd), NULL when it has none: not a layer's name. */
static char *cell_text(const owf_dcell *c, size_t *length)
{
    if (c->kind == OWF_DCELL_PLAIN || !c->value || !c->value[0]) return NULL;
    return owf_dlabel_text(c->value, label_html(c), length);
}

static int find_cell(const owf_dpage *p, const char *id)
{
    int i;
    if (id)
        for (i = 0; i < p->ncells; ++i)
            if (!strcmp(p->cells[i].id, id)) return i;
    return -1;
}

static void put_hit(odg_spell_hit *hit, const owf_dcell *c, const char *text, size_t a, size_t b)
{
    size_t m = b - a < sizeof hit->word - 1 ? b - a : sizeof hit->word - 1;
    snprintf(hit->id, sizeof hit->id, "%s", c->id);
    hit->start = a;
    hit->end = b;
    memcpy(hit->word, text + a, m);
    hit->word[m] = 0;
}

int odg_spell_next(const odg_doc *doc, owf_spell *spell, const char *id, size_t from, odg_spell_hit *hit)
{
    const owf_dpage *p;
    int first, k;
    if (!doc || !spell || !hit || !(p = odg_page(doc))) return -1;
    if (!p->ncells) return 0;
    first = find_cell(p, id);
    if (first < 0) {
        first = 0;
        from = 0;
    }
    for (k = 0; k <= p->ncells; ++k) {
        const owf_dcell *c = &p->cells[(first + k) % p->ncells];
        size_t start = 0, n, a, b;
        char *text;
        int found;
        if (k == p->ncells && !from) break;         /* back round to a cell already checked whole */
        if (!(text = cell_text(c, &n))) {
            if (c->kind != OWF_DCELL_PLAIN && c->value && c->value[0]) return -1;   /* no memory */
            continue;
        }
        if (k == 0) start = from;                   /* the cell given, after the word already found */
        else if (k == p->ncells) n = from < n ? from : n;   /* back round: its start, up to there */
        found = start < n && owf_spell_scan(spell, text, start, n, &a, &b);
        if (found) put_hit(hit, c, text, a, b);
        owf_free(text);
        if (found) return 1;
    }
    return 0;
}

/* How many times word is in text[0, n). */
static int count_in(const char *text, size_t n, const char *word, size_t w)
{
    size_t i;
    int count = 0;
    for (i = 0; w && i + w <= n; ++i)
        if (!memcmp(text + i, word, w)) {
            count++;
            i += w - 1;
        }
    return count;
}

/* The byte of raw HTML where the k-th (from 0) word outside the tags starts; with k < 0, how many there are. */
static long html_find(const char *raw, const char *word, size_t w, int k)
{
    size_t i = 0, n = strlen(raw);
    long count = 0;
    while (i < n) {
        if (raw[i] == '<') {
            while (i < n && raw[i] != '>') i++;
            i++;
            continue;
        }
        if (i + w <= n && !memcmp(raw + i, word, w) && !memchr(raw + i, '<', w)) {
            if (count == k) return (long)i;
            count++;
            i += w;
            continue;
        }
        i++;
    }
    return k < 0 ? count : -1;
}

int odg_spell_replace(odg_doc *doc, const odg_spell_hit *hit, const char *with)
{
    owf_dpage *p;
    const owf_dcell *c;
    char *text, *value;
    size_t n, w, rest, at;
    int i, rc;
    if (!doc || !hit || !with || !(p = odg_page(doc))) return OWF_ERR_FORMAT;
    if ((i = find_cell(p, hit->id)) < 0) return OWF_ERR_FORMAT;
    c = &p->cells[i];
    if (!(text = cell_text(c, &n))) return c->value && c->value[0] && c->kind != OWF_DCELL_PLAIN ? OWF_ERR_MEMORY
                                                                                              : OWF_ERR_UNSUPPORTED;
    w = hit->end - hit->start;
    if (hit->start > hit->end || hit->end > n || strlen(hit->word) != w || memcmp(text + hit->start, hit->word, w)) {
        owf_free(text);
        return OWF_ERR_UNSUPPORTED;             /* the label changed since the word was found */
    }
    if (!label_html(c)) {
        if (strlen(c->value) != n || memcmp(c->value, text, n)) {
            owf_free(text);
            return OWF_ERR_UNSUPPORTED;         /* a plain label shows as it is written */
        }
        at = hit->start;
        rest = n - hit->end;
    } else {
        /* the same word outside the tags, as many times before it: the one shown is the one changed */
        int k = count_in(text, hit->start, hit->word, w);
        long pos;
        if (html_find(c->value, hit->word, w, -1) != count_in(text, n, hit->word, w) ||
            (pos = html_find(c->value, hit->word, w, k)) < 0 || strpbrk(with, "<>&")) {
            owf_free(text);
            return OWF_ERR_UNSUPPORTED;         /* an entity in the word, or markup in the change */
        }
        at = (size_t)pos;
        rest = strlen(c->value) - at - w;
    }
    owf_free(text);
    if (!(value = (char *)owf_malloc(at + strlen(with) + rest + 1))) return OWF_ERR_MEMORY;
    memcpy(value, c->value, at);
    strcpy(value + at, with);
    strcpy(value + at + strlen(with), c->value + at + w);
    rc = odg_set_value(doc, hit->id, value);
    owf_free(value);
    return rc;
}

/* Every misspelt word of the page's labels: how many (-1 no memory); with out, "id word" lines while they fit. */
static int spell_walk(const odg_doc *doc, owf_spell *spell, char *out, size_t size)
{
    const owf_dpage *p = odg_page(doc);
    size_t used = 0;
    int n = 0, i, full = 0;
    for (i = 0; p && i < p->ncells; ++i) {
        const owf_dcell *c = &p->cells[i];
        size_t from = 0, len, a, b;
        char *text = cell_text(c, &len);
        if (!text) {
            if (c->kind != OWF_DCELL_PLAIN && c->value && c->value[0]) return -1;
            continue;
        }
        while (from < len && owf_spell_scan(spell, text, from, len, &a, &b)) {
            size_t need = (used ? 1 : 0) + strlen(c->id) + 1 + (b - a);
            from = b;
            n++;
            if (!out || full) continue;
            if (used + need + 1 > size) {
                full = 1;
                continue;
            }
            used += (size_t)snprintf(out + used, size - used, "%s%s %.*s", used ? "\n" : "", c->id, (int)(b - a),
                                     text + a);
        }
        owf_free(text);
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

int odg_spell_rexx(odg_doc *doc, owf_spell *spell, const char *args, char *out, size_t size)
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
        if (!doc || !odg_page(doc)) {
            snprintf(out, size, "No diagram");
            return 10;
        }
        if (list) out[0] = 0;
        n = spell_walk(doc, spell, list ? out : NULL, size);
        if (n < 0) {
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

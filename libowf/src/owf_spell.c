/*
 * GFOffice's spelling (owf_spell.h): the dictionary and its word rules for
 * every GFOffice program, on the Amiga as owfspell.library. Moved here from
 * GFOpenWrite's src/core/ow_spell_dict.c (17 Nov 2026) so that no program
 * carries a copy of its own.
 *
 * A word list is loaded once and shared by every owf_spell that opens the
 * same file (in owfspell.library, by every program: the list is about
 * 1.7 MB in memory for en_GB). The user's words and the ignored ones are
 * each owf_spell's own. Inside the library the shared lists are behind an
 * Exec semaphore; on the PC there is one task.
 *
 * Nothing of a C library beyond what the libraries' runtime has: no stdio,
 * qsort, bsearch or ctype (files through owf_sys_read_file and
 * owf_sys_write_file, the lock owf_sys_shared_lock).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_spell.h"
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the shared lists' lock and a list's key: owf_sys.c, and owf_sys_dos.c in the libraries */
#define spell_lock owf_sys_shared_lock
#define spell_unlock owf_sys_shared_unlock
#define spell_key owf_sys_full_name

/* ---- ASCII letters: the word rules' (bytes from 0x80 are letters too) ---- */

static int sp_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int sp_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
static int sp_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static int sp_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; }

static void lower_word(char *out, size_t out_size, const char *s, size_t n)
{
    size_t i, m = n < out_size - 1 ? n : out_size - 1;
    for (i = 0; i < m; ++i) out[i] = (char)owf_lower((unsigned char)s[i]);
    out[m] = 0;
}

/* ---- sorted word lists ---- */

/* The place of word in words[0, n): *found 1 when it is there. */
static int list_find(char *const *words, int n, const char *word, int *found)
{
    int lo = 0, hi = n;
    *found = 0;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2, c = strcmp(words[mid], word);
        if (!c) {
            *found = 1;
            return mid;
        }
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static int list_has(char *const *words, int n, const char *word)
{
    int found;
    if (n <= 0) return 0;
    list_find(words, n, word, &found);
    return found;
}

/* Heap sort: only for a word list that isn't sorted already (en_GB.words is). */
static void sift(char **a, int root, int n)
{
    for (;;) {
        int child = 2 * root + 1;
        char *t;
        if (child >= n) return;
        if (child + 1 < n && strcmp(a[child], a[child + 1]) < 0) ++child;
        if (strcmp(a[root], a[child]) >= 0) return;
        t = a[root]; a[root] = a[child]; a[child] = t;
        root = child;
    }
}

static void sort_words(char **a, int n)
{
    int i;
    for (i = 1; i < n; ++i)
        if (strcmp(a[i - 1], a[i]) > 0) break;
    if (i >= n) return;
    for (i = n / 2 - 1; i >= 0; --i) sift(a, i, n);
    for (i = n - 1; i > 0; --i) {
        char *t = a[0]; a[0] = a[i]; a[i] = t;
        sift(a, 0, i);
    }
}

/* Its own copy of word, in place in the sorted list; 1 when it is there afterwards. */
static int list_add(char ***list, int *n, int *cap, const char *word)
{
    int at, found;
    char *copy;
    at = list_find(*list, *n, word, &found);
    if (found) return 1;
    if (*n == *cap) {
        int want = *cap ? *cap * 2 : 32;
        char **u = (char **)realloc(*list, (size_t)want * sizeof(*u));
        if (!u) return 0;
        *list = u;
        *cap = want;
    }
    copy = (char *)malloc(strlen(word) + 1);
    if (!copy) return 0;
    strcpy(copy, word);
    memmove(*list + at + 1, *list + at, (size_t)(*n - at) * sizeof(**list));
    (*list)[at] = copy;
    ++*n;
    return 1;
}

static void list_free(char **list, int n)
{
    int i;
    for (i = 0; i < n; ++i) free(list[i]);
    free(list);
}

/* ---- the shared dictionaries ---- */

typedef struct spell_dict {
    struct spell_dict *next;
    int refs;
    char *blob;
    char **words;
    int nwords;
    char key[256];
} spell_dict;

static spell_dict *spell_dicts;   /* behind spell_lock() */

/* The lines of data (owned, NUL after its bytes, so the last line ends there) as a sorted, lower case list into d. */
static int dict_parse(spell_dict *d, char *data, size_t size)
{
    char *p = data, *end = data + size, **words = NULL;
    int n = 0, cap = 0;
    while (p < end) {
        char *line = p, *q;
        while (p < end && *p != '\n' && *p != '\r') ++p;
        while (p < end && (*p == '\n' || *p == '\r')) *p++ = 0;
        while (*line && sp_space((unsigned char)*line)) ++line;
        for (q = line; *q && *q != '/' && *q != '\n' && *q != '\r'; ++q) *q = (char)owf_lower((unsigned char)*q);
        *q = 0;
        while (q > line && sp_space((unsigned char)q[-1])) *--q = 0;
        if (!*line || sp_digit((unsigned char)*line)) continue;
        if (n == cap) {
            int want = cap ? cap * 2 : 4096;
            char **nw = (char **)realloc(words, (size_t)want * sizeof(*nw));
            if (!nw) {
                free(words);
                return 0;
            }
            words = nw;
            cap = want;
        }
        words[n++] = line;
    }
    sort_words(words, n);
    d->blob = data;
    d->words = words;
    d->nwords = n;
    return 1;
}

static spell_dict *dict_get(const char *path)
{
    char key[256];
    spell_dict *d;
    unsigned char *data;
    size_t size;
    spell_key(path, key, sizeof key);
    spell_lock();
    for (d = spell_dicts; d; d = d->next)
        if (!strcmp(d->key, key)) {
            d->refs++;
            spell_unlock();
            return d;
        }
    /* loaded with the lock held: a second program opening the same list waits for this one */
    d = (spell_dict *)calloc(1, sizeof(*d));
    if (!d || owf_sys_read_file(path, &data, &size) != OWF_OK) {
        free(d);
        spell_unlock();
        return NULL;
    }
    if (!dict_parse(d, (char *)data, size)) {
        free(data);
        free(d);
        spell_unlock();
        return NULL;
    }
    snprintf(d->key, sizeof d->key, "%s", key);
    d->refs = 1;
    d->next = spell_dicts;
    spell_dicts = d;
    spell_unlock();
    return d;
}

static void dict_put(spell_dict *d)
{
    spell_dict **at;
    if (!d) return;
    spell_lock();
    if (--d->refs > 0) {
        spell_unlock();
        return;
    }
    for (at = &spell_dicts; *at; at = &(*at)->next)
        if (*at == d) {
            *at = d->next;
            break;
        }
    spell_unlock();
    free(d->words);
    free(d->blob);
    free(d);
}

/* ---- an owf_spell ---- */

struct owf_spell {
    spell_dict *dict;
    char **user;
    int nuser, capuser;
    char **ignored;   /* Ignore All: this owf_spell only, never saved */
    int nignored, capignored;
    char user_path[256];
};

static void load_user(owf_spell *s)
{
    unsigned char *data;
    size_t size;
    char *p, *end;
    if (!s->user_path[0] || owf_sys_read_file(s->user_path, &data, &size) != OWF_OK) return;
    p = (char *)data;
    end = p + size;
    while (p < end) {
        char *line = p, w[OWF_SPELL_WORD * 2];
        size_t n;
        while (p < end && *p != '\n' && *p != '\r') ++p;
        n = (size_t)(p - line);
        while (p < end && (*p == '\n' || *p == '\r')) ++p;
        if (!n) continue;
        lower_word(w, sizeof w, line, n);
        list_add(&s->user, &s->nuser, &s->capuser, w);
    }
    free(data);
}

owf_spell *owf_spell_open(const char *primary_path, const char *user_path, char *status, size_t status_size)
{
    owf_spell *s;
    if (!primary_path || !*primary_path) {
        if (status && status_size) snprintf(status, status_size, "Dictionary not found: ");
        return NULL;
    }
    s = (owf_spell *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (!(s->dict = dict_get(primary_path))) {
        if (status && status_size) snprintf(status, status_size, "Dictionary not found: %s", primary_path);
        free(s);
        return NULL;
    }
    if (user_path) snprintf(s->user_path, sizeof s->user_path, "%s", user_path);
    load_user(s);
    if (status && status_size) snprintf(status, status_size, "%d dictionary words loaded.", s->dict->nwords);
    return s;
}

owf_spell *owf_spell_open_default(char *status, size_t status_size)
{
    static const char *const paths[] = { "PROGDIR:Dictionaries/en_GB.words", "PROGDIR:Dictionaries/en_US.words",
                                         "SYS:Dictionaries/en_GB.words", NULL };
    int i;
    for (i = 0; paths[i]; ++i) {
        owf_spell *s = owf_spell_open(paths[i], OWF_SPELL_USER_PATH, status, status_size);
        if (s) return s;
    }
    if (status && status_size)
        snprintf(status, status_size, "No dictionary found: install Dictionaries/en_GB.words beside the program.");
    return NULL;
}

void owf_spell_close(owf_spell *s)
{
    if (!s) return;
    list_free(s->user, s->nuser);
    list_free(s->ignored, s->nignored);
    dict_put(s->dict);
    free(s);
}

int owf_spell_count(const owf_spell *s) { return s && s->dict ? s->dict->nwords : 0; }

static int spell_has(const owf_spell *s, const char *word)
{
    if (!s || !word || !*word) return 1;
    return list_has(s->dict->words, s->dict->nwords, word) || list_has(s->user, s->nuser, word) ||
           list_has(s->ignored, s->nignored, word);
}

static int word_char(unsigned char c) { return sp_alpha(c) || c >= 0x80 || c == '\''; }

static int skip_candidate(const char *s, size_t n)
{
    size_t i;
    int letters = 0, uppers = 0;
    if (n < 2) return 1;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (sp_digit(c)) return 1;
        if (sp_alpha(c)) {
            ++letters;
            if (sp_upper(c)) ++uppers;
        }
    }
    return letters == uppers && letters <= 5;
}

int owf_spell_word_ok(owf_spell *s, const char *word, size_t length)
{
    char lower[96];
    if (!word || !length || skip_candidate(word, length)) return 1;
    lower_word(lower, sizeof lower, word, length);
    return spell_has(s, lower);
}

int owf_spell_scan(owf_spell *s, const char *text, size_t from, size_t to, size_t *start, size_t *end)
{
    size_t p = from;
    if (!s || !text) return 0;
    while (p < to) {
        size_t a, b;
        while (p < to && !word_char((unsigned char)text[p])) ++p;
        a = p;
        while (p < to && word_char((unsigned char)text[p])) ++p;
        b = p;
        while (a < b && text[a] == '\'') ++a;
        while (b > a && text[b - 1] == '\'') --b;
        if (b <= a || owf_spell_word_ok(s, text + a, b - a)) continue;
        if (start) *start = a;
        if (end) *end = b;
        return 1;
    }
    return 0;
}

int owf_spell_words(owf_spell *s, const char *text, size_t length, owf_spell_range *ranges, int max_ranges)
{
    size_t from = 0, a, b;
    int n = 0;
    if (!s || !text) return 0;
    while (from < length && owf_spell_scan(s, text, from, length, &a, &b)) {
        if (ranges && n < max_ranges) {
            ranges[n].start = a;
            ranges[n].end = b;
        }
        n++;
        from = b;
    }
    return n;
}

/* The user's file with word added at its end, reread first so that a word
 * another program added meanwhile stays. */
static int user_file_append(const char *path, const char *word)
{
    unsigned char *old = NULL, *buf;
    size_t n = 0, w = strlen(word);
    int rc;
    if (owf_sys_read_file(path, &old, &n) != OWF_OK) {
        old = NULL;
        n = 0;
    }
    buf = (unsigned char *)malloc(n + w + 2);
    if (!buf) {
        free(old);
        return 0;
    }
    if (n) memcpy(buf, old, n);
    if (n && buf[n - 1] != '\n') buf[n++] = '\n';
    memcpy(buf + n, word, w);
    buf[n + w] = '\n';
    rc = owf_sys_write_file(path, buf, n + w + 1);
    free(buf);
    free(old);
    return rc == OWF_OK;
}

int owf_spell_add(owf_spell *s, const char *word)
{
    char lower[96];
    if (!s || !word || !*word) return 0;
    lower_word(lower, sizeof lower, word, strlen(word));
    /* an ignored word is still saved when it is added */
    if (list_has(s->dict->words, s->dict->nwords, lower) || list_has(s->user, s->nuser, lower)) return 1;
    if (!list_add(&s->user, &s->nuser, &s->capuser, lower)) return 0;
    if (!s->user_path[0]) return 1;
    return user_file_append(s->user_path, lower);
}

int owf_spell_ignore(owf_spell *s, const char *word)
{
    char lower[96];
    if (!s || !word || !*word) return 0;
    lower_word(lower, sizeof lower, word, strlen(word));
    return spell_has(s, lower) || list_add(&s->ignored, &s->nignored, &s->capignored, lower);
}

static int edit_distance_le3(const char *a, const char *b)
{
    int prev[48], cur[48], i, j, na = (int)strlen(a), nb = (int)strlen(b);
    if (na >= 47 || nb >= 47 || abs(na - nb) > 3) return 99;
    for (j = 0; j <= nb; ++j) prev[j] = j;
    for (i = 1; i <= na; ++i) {
        int rowmin;
        cur[0] = i;
        rowmin = cur[0];
        for (j = 1; j <= nb; ++j) {
            int del = prev[j] + 1, ins = cur[j - 1] + 1, sub = prev[j - 1] + (a[i - 1] != b[j - 1]);
            int v = del < ins ? del : ins;
            if (sub < v) v = sub;
            cur[j] = v;
            if (v < rowmin) rowmin = v;
        }
        if (rowmin > 3) return 99;
        for (j = 0; j <= nb; ++j) prev[j] = cur[j];
    }
    return prev[nb];
}

int owf_spell_suggest(owf_spell *s, const char *word, char *slots, int max)
{
    char lower[96];
    int i, n = 0, dist, best = 4, len;
    if (!s || !word || !slots || max <= 0) return 0;
    lower_word(lower, sizeof lower, word, strlen(word));
    len = (int)strlen(lower);
    for (i = 0; i < s->dict->nwords; ++i) {
        const char *w = s->dict->words[i];
        if (w[0] != lower[0] || abs((int)strlen(w) - len) > 2) continue;
        dist = edit_distance_le3(lower, w);
        if (dist > 2) continue;
        if (dist < best) {
            best = dist;
            n = 0;
        }
        if (dist == best && n < max) {
            snprintf(slots + (size_t)n * OWF_SPELL_WORD, OWF_SPELL_WORD, "%s", w);
            ++n;
        }
        if (n == max && best == 1) break;
    }
    return n;
}

/* ---- ARexx: the word commands every program's SPELL shares ---- */

/* 1 when s starts with word (any case) followed by a space or its end; *rest after the spaces. */
static int command_word(const char *s, const char *word, const char **rest)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; i++)
        if (owf_upper((unsigned char)s[i]) != word[i]) return 0;
    if (s[n] && s[n] != ' ' && s[n] != '\t') return 0;
    s += n;
    while (*s == ' ' || *s == '\t') s++;
    *rest = s;
    return 1;
}

/* The word after a command: "in quotes" or up to the next space. */
static void command_arg(const char *s, char *out, size_t size)
{
    size_t k = 0;
    if (*s == '"' || *s == '\'') {
        char q = *s++;
        for (; *s && *s != q; s++)
            if (k + 1 < size) out[k++] = *s;
    } else {
        for (; *s && *s != ' ' && *s != '\t'; s++)
            if (k + 1 < size) out[k++] = *s;
    }
    out[k] = 0;
}

int owf_spell_command(owf_spell *s, const char *args, char *out, size_t size)
{
    static const char *const verbs[] = { "CHECK", "SUGGEST", "ADD", "IGNORE" };
    const char *rest = "";
    char word[96];
    int v;
    if (!args) args = "";
    while (*args == ' ' || *args == '\t') args++;
    for (v = 0; v < 4 && !command_word(args, verbs[v], &rest); v++) {}
    if (v == 4) return -1;
    if (!out || size < 2) return 10;
    out[0] = 0;
    if (!s) {
        snprintf(out, size, "No dictionary");
        return 5;
    }
    command_arg(rest, word, sizeof word);
    if (!word[0]) {
        snprintf(out, size, "SPELL %s needs a word", verbs[v]);
        return 10;
    }
    if (v == 0) {
        snprintf(out, size, "%d", owf_spell_word_ok(s, word, strlen(word)));
    } else if (v == 1) {
        char sugg[8][OWF_SPELL_WORD];
        int n = owf_spell_suggest(s, word, sugg[0], 8), i;
        size_t used = 0;
        for (i = 0; i < n; i++) {
            size_t m = strlen(sugg[i]);
            if (used + m + 2 > size) break;
            if (used) out[used++] = ' ';
            memcpy(out + used, sugg[i], m);
            used += m;
            out[used] = 0;
        }
    } else if (v == 2) {
        if (!owf_spell_add(s, word)) {
            snprintf(out, size, "The word could not be saved");
            return 10;
        }
    } else if (!owf_spell_ignore(s, word)) {
        snprintf(out, size, "Not enough memory");
        return 20;
    }
    return 0;
}

#include "ow_spell.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ow_spell {
    char *blob;
    char **words;
    int nwords;
    char **user;
    int nuser, capuser;
    char user_path[256];
};

static int cmp_word_ptr(const void *aa, const void *bb)
{
    const char *const *a = (const char *const *)aa;
    const char *const *b = (const char *const *)bb;
    return strcmp(*a, *b);
}

static void lower_word(char *out, size_t out_size, const char *s, size_t n)
{
    size_t i, m = n < out_size - 1 ? n : out_size - 1;
    for (i = 0; i < m; ++i) {
        unsigned char c = (unsigned char)s[i];
        out[i] = c < 128 ? (char)tolower(c) : (char)c;
    }
    out[m] = 0;
}

static int dict_has(char **words, int n, const char *word)
{
    char *key = (char *)word;
    return n > 0 && bsearch(&key, words, (size_t)n, sizeof(words[0]), cmp_word_ptr) != NULL;
}

static int load_words_file(const char *path, char **blob_out, char ***words_out)
{
    FILE *f;
    char *blob, **words = NULL;
    long size;
    int n = 0, cap = 0;
    char *p, *end;
    if (!path || !(f = fopen(path, "rb"))) return -1;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f); return -1;
    }
    blob = (char *)malloc((size_t)size + 2);
    if (!blob) { fclose(f); return -1; }
    if (size && fread(blob, 1, (size_t)size, f) != (size_t)size) {
        free(blob); fclose(f); return -1;
    }
    fclose(f);
    blob[size] = '\n'; blob[size + 1] = 0;
    p = blob; end = blob + size + 1;
    while (p < end) {
        char *line = p, *slash;
        while (p < end && *p != '\n' && *p != '\r') ++p;
        if (p < end) *p++ = 0;
        while (p < end && (*p == '\n' || *p == '\r')) *p++ = 0;
        while (*line && isspace((unsigned char)*line)) ++line;
        slash = strchr(line, '/'); if (slash) *slash = 0;
        if (!*line || isdigit((unsigned char)*line)) continue;
        if (n == cap) {
            int want = cap ? cap * 2 : 4096;
            char **nw = (char **)realloc(words, (size_t)want * sizeof(*nw));
            if (!nw) { free(blob); free(words); return -1; }
            words = nw; cap = want;
        }
        {
            char *q = line;
            while (*q) { unsigned char c=(unsigned char)*q; if(c<128)*q=(char)tolower(c); ++q; }
        }
        words[n++] = line;
    }
    qsort(words, (size_t)n, sizeof(words[0]), cmp_word_ptr);
    *blob_out = blob;
    *words_out = words;
    return n;
}

static int user_add_mem(ow_spell *s, const char *word)
{
    char **u, *copy;
    if (s->nuser == s->capuser) {
        int cap = s->capuser ? s->capuser * 2 : 32;
        u = (char **)realloc(s->user, (size_t)cap * sizeof(*u));
        if (!u) return 0;
        s->user = u; s->capuser = cap;
    }
    copy = (char *)malloc(strlen(word) + 1);
    if (!copy) return 0;
    strcpy(copy, word);
    s->user[s->nuser++] = copy;
    qsort(s->user, (size_t)s->nuser, sizeof(s->user[0]), cmp_word_ptr);
    return 1;
}

static void load_user(ow_spell *s)
{
    FILE *f;
    char line[128];
    if (!s->user_path[0] || !(f = fopen(s->user_path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        size_t n = strcspn(line, "\r\n");
        char w[128];
        if (!n) continue;
        lower_word(w, sizeof w, line, n);
        if (!dict_has(s->user, s->nuser, w)) user_add_mem(s, w);
    }
    fclose(f);
}

ow_spell *ow_spell_open(const char *primary_path, const char *user_path,
                        char *status, size_t status_size)
{
    ow_spell *s = (ow_spell *)calloc(1, sizeof(*s));
    int n;
    if (!s) return NULL;
    n = load_words_file(primary_path, &s->blob, &s->words);
    if (n < 0) {
        if (status && status_size)
            snprintf(status, status_size, "Dictionary not found: %s", primary_path ? primary_path : "");
        free(s); return NULL;
    }
    s->nwords = n;
    if (user_path) snprintf(s->user_path, sizeof s->user_path, "%s", user_path);
    load_user(s);
    if (status && status_size)
        snprintf(status, status_size, "%d dictionary words loaded.", s->nwords);
    return s;
}

void ow_spell_close(ow_spell *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < s->nuser; ++i) free(s->user[i]);
    free(s->user);
    free(s->words);
    free(s->blob);
    free(s);
}

static int spell_has(ow_spell *s, const char *word)
{
    if (!s || !word || !*word) return 1;
    return dict_has(s->words, s->nwords, word) || dict_has(s->user, s->nuser, word);
}

static int word_char(unsigned char c)
{
    return isalpha(c) || c >= 0x80 || c == '\'';
}

static int skip_candidate(const char *s, size_t n)
{
    size_t i;
    int letters = 0, uppers = 0;
    if (n < 2) return 1;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (isdigit(c)) return 1;
        if (isalpha(c)) { ++letters; if (isupper(c)) ++uppers; }
    }
    return letters == uppers && letters <= 5;
}

static int scan_run(ow_spell *s, ow_editor *editor, int pi, int ri,
                    size_t from, size_t to, char *word, size_t word_size)
{
    const owf_doc *doc = ow_editor_document_const(editor);
    const char *text = doc->body.paras[pi].runs[ri].text;
    size_t n = text ? strlen(text) : 0, p = from;
    if (to > n) to = n;
    while (p < to) {
        size_t a, b;
        char lower[96];
        while (p < to && !word_char((unsigned char)text[p])) ++p;
        a = p;
        while (p < to && word_char((unsigned char)text[p])) ++p;
        b = p;
        while (a < b && text[a] == '\'') ++a;
        while (b > a && text[b - 1] == '\'') --b;
        if (b <= a || skip_candidate(text + a, b - a)) continue;
        lower_word(lower, sizeof lower, text + a, b - a);
        if (!spell_has(s, lower)) {
            ow_selection sel;
            sel.anchor.paragraph = sel.focus.paragraph = pi;
            sel.anchor.run = sel.focus.run = ri;
            sel.anchor.byte_offset = a; sel.focus.byte_offset = b;
            ow_editor_set_selection(editor, &sel);
            if (word && word_size) {
                size_t m = b - a < word_size - 1 ? b - a : word_size - 1;
                memcpy(word, text + a, m); word[m] = 0;
            }
            return 1;
        }
    }
    return 0;
}

int ow_spell_next(ow_spell *s, ow_editor *editor, char *word,
                  size_t word_size, int wrap)
{
    const owf_doc *doc;
    ow_selection sel;
    int pass, pi, ri;
    if (!s || !editor || !(doc = ow_editor_document_const(editor))) return -1;
    sel = ow_editor_selection(editor);
    for (pass = 0; pass < (wrap ? 2 : 1); ++pass) {
        int pstart = pass ? 0 : sel.focus.paragraph;
        int pend = pass ? sel.focus.paragraph + 1 : doc->body.nparas;
        for (pi = pstart; pi < pend && pi < doc->body.nparas; ++pi) {
            const owf_para *p = &doc->body.paras[pi];
            for (ri = 0; ri < p->nruns; ++ri) {
                const owf_run *r = &p->runs[ri];
                size_t from = 0, to;
                if (r->kind != OWF_RUN_TEXT || !r->text) continue;
                to = strlen(r->text);
                if (!pass && pi == sel.focus.paragraph && ri < sel.focus.run) continue;
                if (!pass && pi == sel.focus.paragraph && ri == sel.focus.run)
                    from = sel.focus.byte_offset;
                if (pass && pi == sel.focus.paragraph) {
                    if (ri > sel.focus.run) continue;
                    if (ri == sel.focus.run) to = sel.focus.byte_offset;
                }
                if (scan_run(s, editor, pi, ri, from, to, word, word_size)) return 1;
            }
        }
    }
    return 0;
}

int ow_spell_add(ow_spell *s, const char *word)
{
    char lower[96];
    FILE *f;
    if (!s || !word || !*word) return 0;
    lower_word(lower, sizeof lower, word, strlen(word));
    if (spell_has(s, lower)) return 1;
    if (!user_add_mem(s, lower)) return 0;
    if (!s->user_path[0]) return 1;
    f = fopen(s->user_path, "a");
    if (!f) return 0;
    fprintf(f, "%s\n", lower);
    fclose(f);
    return 1;
}

static int edit_distance_le3(const char *a, const char *b)
{
    int prev[48], cur[48], i, j, na=(int)strlen(a), nb=(int)strlen(b);
    if (na >= 47 || nb >= 47 || abs(na - nb) > 3) return 99;
    for (j=0;j<=nb;++j) prev[j]=j;
    for (i=1;i<=na;++i) {
        int rowmin;
        cur[0]=i; rowmin=cur[0];
        for (j=1;j<=nb;++j) {
            int del=prev[j]+1, ins=cur[j-1]+1, sub=prev[j-1]+(a[i-1]!=b[j-1]);
            int v=del<ins?del:ins; if(sub<v)v=sub; cur[j]=v; if(v<rowmin)rowmin=v;
        }
        if(rowmin>3)return 99;
        for(j=0;j<=nb;++j)prev[j]=cur[j];
    }
    return prev[nb];
}

int ow_spell_suggest(ow_spell *s, const char *word,
                     char suggestions[][48], int max_suggestions)
{
    char lower[96];
    int i, n=0, dist, best=4;
    if(!s||!word||max_suggestions<=0)return 0;
    lower_word(lower,sizeof lower,word,strlen(word));
    for(i=0;i<s->nwords;++i){
        const char *w=s->words[i];
        if(w[0]!=lower[0] || abs((int)strlen(w)-(int)strlen(lower))>2)continue;
        dist=edit_distance_le3(lower,w);
        if(dist>2)continue;
        if(dist<best){best=dist;n=0;}
        if(dist==best && n<max_suggestions){
            snprintf(suggestions[n],48,"%s",w);++n;
        }
        if(n==max_suggestions && best==1)break;
    }
    return n;
}

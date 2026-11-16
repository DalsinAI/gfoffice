/*
 * libowf: Markdown in, from CommonMark 0.31 (a subset) and GitHub's tables
 * and strike-through. Headings (# and underlined), paragraphs, hard line
 * breaks, bold, italic, strike-through, code spans, links and autolinks,
 * backslash escapes and character references, bullet and numbered lists,
 * block quotes, fenced and indented code, thematic breaks and pipe tables.
 * Lists keep their bullets and numbers as text with a tab, like DOCX's and
 * ODT's; code is in a monospaced font; a quote is indented; a thematic
 * break is an empty paragraph with a line below it. Pictures keep their
 * description; raw HTML and link reference definitions stay as text.
 * A file that is not valid UTF-8 is taken as ISO-8859-1. A .md or .markdown
 * file opens as Markdown by its name (owf_import_file); by its bytes alone
 * it is plain text.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MD_MAX_DEPTH 16
#define MD_MAX_COLS 64
#define MD_INDENT_LIST 360
#define MD_INDENT_QUOTE 720

/* ---- Inlines ---- */

enum { T_TEXT, T_DELIM, T_BREAK };

typedef struct {
    int kind;
    size_t start, length;    /* T_TEXT: its bytes in md.tx */
    long href;               /* its link's offset in md.hrefs, -1: none */
    int mono;
    unsigned flags;          /* OWF_BOLD ... from the emphasis round it */
    char ch;                 /* T_DELIM: '*', '_' or '~' */
    int count, orig;         /* T_DELIM: characters left, and at first */
    int can_open, can_close, active;
} md_tok;

typedef struct {
    int marker_col, content_col;
    int ordered;
    char ch;                 /* '-', '+', '*' or the number's '.' or ')' */
    long number;
} md_level;

typedef struct {
    owf_doc *doc;
    owf_report *report;
    owf_builder b;
    int mono_font;
    int failed;

    md_tok *toks;
    int ntoks, captoks;
    owf_buf tx, hrefs;

    /* blocks */
    owf_buf raw;             /* the paragraph's lines, '\n' between them */
    int in_para, para_lines;
    owf_parafmt para_fmt;
    char para_prefix[24];
    int quote;               /* quote depth of the open blocks */
    int blank;               /* the line before was blank */
    md_level lv[MD_MAX_DEPTH];
    int depth;
    char item_prefix[24];    /* an item's bullet or number not yet on a paragraph */
    int item_prefix_set;

    int fence;               /* in fenced code: its character's count */
    char fence_ch;
    int fence_col;           /* the fence's column: up to this many spaces come off its lines */
    int fence_quote, fence_depth;
    int icode;               /* in indented code */
    int icode_blanks;        /* blank lines held back in it */

    int table;               /* in a table */
    int table_id, next_table, table_row, table_cols;
    owf_align table_align[MD_MAX_COLS];

    int said_html, said_pictures;
} md;

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

static int is_punct(int c)
{
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

static md_tok *new_tok(md *m, int kind)
{
    md_tok *t;
    if (m->ntoks == m->captoks) {
        int cap = m->captoks ? m->captoks * 2 : 64;
        md_tok *n = (md_tok *)realloc(m->toks, (size_t)cap * sizeof *n);
        if (!n) {
            m->failed = 1;
            return NULL;
        }
        m->toks = n;
        m->captoks = cap;
    }
    t = &m->toks[m->ntoks++];
    memset(t, 0, sizeof *t);
    t->kind = kind;
    t->href = -1;
    t->start = m->tx.length;
    return t;
}

/* Literal text, joined to the text before it when that has the same look. */
static void put_text(md *m, const char *s, size_t n, long href, int mono, int first)
{
    md_tok *t = m->ntoks > first ? &m->toks[m->ntoks - 1] : NULL;
    if (!n)
        return;
    if (!t || t->kind != T_TEXT || t->href != href || t->mono != mono || t->start + t->length != m->tx.length) {
        t = new_tok(m, T_TEXT);
        if (!t)
            return;
        t->href = href;
        t->mono = mono;
    }
    owf_buf_put(&m->tx, s, n);
    t->length += n;
}

static void put_cp(md *m, unsigned long c, long href, int first)
{
    owf_buf u;
    owf_buf_init(&u);
    owf_buf_put_utf8(&u, c ? c : 0xFFFD);
    if (u.failed)
        m->failed = 1;
    else
        put_text(m, (const char *)u.data, u.length, href, 0, first);
    owf_buf_free(&u);
}

static const struct { const char *name; unsigned long cp; } entities[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 }, { "hellip", 0x2026 }, { "mdash", 0x2014 },
    { "ndash", 0x2013 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D },
    { "euro", 0x20AC }, { "pound", 0xA3 }, { "yen", 0xA5 }, { "cent", 0xA2 }, { "sect", 0xA7 }, { "deg", 0xB0 },
    { "plusmn", 0xB1 }, { "times", 0xD7 }, { "divide", 0xF7 }, { "middot", 0xB7 }, { "bull", 0x2022 },
    { "laquo", 0xAB }, { "raquo", 0xBB }, { "para", 0xB6 }, { "shy", 0xAD }, { "iexcl", 0xA1 },
    { "iquest", 0xBF }, { "eacute", 0xE9 }, { "egrave", 0xE8 }, { "agrave", 0xE0 }, { "uuml", 0xFC },
    { "ouml", 0xF6 }, { "auml", 0xE4 }, { "szlig", 0xDF }, { "ccedil", 0xE7 }
};

/* A character reference at s (s[0] is '&'): its length, and its character in *cp; 0: none. */
static size_t entity(const char *s, size_t n, unsigned long *cp)
{
    size_t i = 1;
    unsigned long v = 0;
    if (n < 3)
        return 0;
    if (s[1] == '#') {
        int hex = n > 2 && (s[2] == 'x' || s[2] == 'X');
        size_t digits = 0;
        for (i = hex ? 3 : 2; i < n && digits < 7; i++, digits++) {
            int c = (unsigned char)s[i], d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            v = v * (hex ? 16 : 10) + (unsigned long)d;
        }
        if (!digits || i >= n || s[i] != ';')
            return 0;
        *cp = v && v <= 0x10FFFF && !(v >= 0xD800 && v <= 0xDFFF) ? v : 0xFFFD;
        return i + 1;
    }
    while (i < n && i < 12 && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9')))
        i++;
    if (i >= n || s[i] != ';' || i == 1)
        return 0;
    for (v = 0; v < sizeof entities / sizeof entities[0]; v++)
        if (strlen(entities[v].name) == i - 1 && !memcmp(entities[v].name, s + 1, i - 1)) {
            *cp = entities[v].cp;
            return i + 1;
        }
    return 0;
}

/* A link's destination and title after "](" at s[0] = '(': the bytes to the ')' after it, 0 if it is not one. */
static size_t link_tail(const char *s, size_t n, size_t *dest, size_t *dest_len)
{
    size_t i = 1;
    int depth = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
    if (i < n && s[i] == '<') {
        *dest = ++i;
        while (i < n && s[i] != '>' && s[i] != '\n' && s[i] != '<') i += s[i] == '\\' && i + 1 < n ? 2 : 1;
        if (i >= n || s[i] != '>')
            return 0;
        *dest_len = i - *dest;
        i++;
    } else {
        *dest = i;
        while (i < n && !is_space((unsigned char)s[i]) && (unsigned char)s[i] >= 0x20) {
            if (s[i] == '\\' && i + 1 < n) { i += 2; continue; }
            if (s[i] == '(') depth++;
            else if (s[i] == ')') { if (!depth) break; depth--; }
            i++;
        }
        if (depth)
            return 0;
        *dest_len = i - *dest;
    }
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
    if (i < n && (s[i] == '"' || s[i] == '\'' || s[i] == '(') && i > *dest + *dest_len) {
        char close = s[i] == '(' ? ')' : s[i];
        i++;
        while (i < n && s[i] != close) i += s[i] == '\\' && i + 1 < n ? 2 : 1;
        if (i >= n)
            return 0;
        i++;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
    }
    return i < n && s[i] == ')' ? i + 1 : 0;
}

/* The ']' that closes the '[' at s[0], or 0. */
static size_t close_bracket(const char *s, size_t n)
{
    size_t i;
    int depth = 0;
    for (i = 1; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) { i++; continue; }
        if (s[i] == '`') {                           /* a code span's brackets are its own */
            size_t k = i, run, j;
            while (k < n && s[k] == '`') k++;
            run = k - i;
            for (j = k; j < n; j++)
                if (s[j] == '`') {
                    size_t e = j;
                    while (e < n && s[e] == '`') e++;
                    if (e - j == run) break;
                    j = e - 1;
                }
            if (j < n) { i = j + run - 1; continue; }
            i = k - 1;
            continue;
        }
        if (s[i] == '[') depth++;
        else if (s[i] == ']') { if (!depth) return i; depth--; }
    }
    return 0;
}

static long add_href(md *m, const char *s, size_t n, const char *prefix)
{
    long at = (long)m->hrefs.length;
    size_t i;
    if (prefix)
        owf_buf_puts(&m->hrefs, prefix);
    for (i = 0; i < n; i++) {
        unsigned long cp;
        size_t e;
        if (s[i] == '\\' && i + 1 < n && is_punct((unsigned char)s[i + 1])) {
            owf_buf_putc(&m->hrefs, s[++i]);
        } else if (s[i] == '&' && (e = entity(s + i, n - i, &cp)) > 0) {
            owf_buf_put_utf8(&m->hrefs, cp);
            i += e - 1;
        } else if (s[i] == ' ') {
            owf_buf_puts(&m->hrefs, "%20");
        } else {
            owf_buf_putc(&m->hrefs, s[i]);
        }
    }
    owf_buf_putc(&m->hrefs, 0);
    if (m->hrefs.failed)
        m->failed = 1;
    return at;
}

/* An autolink at s[0] = '<': its length, and where its address is; 0: not one. */
static size_t autolink(const char *s, size_t n, int *email)
{
    size_t i = 1, scheme;
    *email = 0;
    while (i < n && i < 34 && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                               (i > 1 && ((s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '.' || s[i] == '-'))))
        i++;
    scheme = i - 1;
    if (scheme >= 2 && i < n && s[i] == ':') {
        for (i++; i < n && s[i] != '>'; i++)
            if ((unsigned char)s[i] <= ' ' || s[i] == '<')
                return 0;
        return i < n ? i + 1 : 0;
    }
    for (i = 1; i < n && s[i] != '>'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c <= ' ' || c == '<' || c == '\\' || c >= 0x80)
            return 0;
        if (c == '@') *email = 1;
    }
    return *email && i < n && i > 3 ? i + 1 : 0;
}

static void inline_range(md *m, const char *s, size_t n, long href, int nest);

/* CommonMark's process emphasis, over the tokens from first on. */
static void emphasis(md *m, int first)
{
    int closer, bottom[3][2][3];
    int a, b, c;
    for (a = 0; a < 3; a++) for (b = 0; b < 2; b++) for (c = 0; c < 3; c++) bottom[a][b][c] = first - 1;
    for (closer = first; closer < m->ntoks; closer++) {
        md_tok *cl = &m->toks[closer];
        int which, op, found = -1;
        if (cl->kind != T_DELIM || !cl->active || !cl->can_close || !cl->count)
            continue;
        which = cl->ch == '*' ? 0 : cl->ch == '_' ? 1 : 2;
        for (op = closer - 1; op > bottom[which][cl->can_open][cl->orig % 3]; op--) {
            md_tok *o = &m->toks[op];
            if (o->kind != T_DELIM || !o->active || !o->can_open || o->ch != cl->ch || !o->count)
                continue;
            if (cl->ch == '~') {
                if (o->orig != cl->orig || o->orig > 2) continue;
            } else if ((o->can_close || cl->can_open) && (o->orig + cl->orig) % 3 == 0 &&
                       !(o->orig % 3 == 0 && cl->orig % 3 == 0)) {
                continue;
            }
            found = op;
            break;
        }
        if (found < 0) {
            bottom[which][cl->can_open][cl->orig % 3] = closer - 1;
            if (!cl->can_open)
                cl->active = 0;
            continue;
        }
        {
            md_tok *o = &m->toks[found];
            int use = cl->ch == '~' ? o->count : (o->count >= 2 && cl->count >= 2 ? 2 : 1);
            unsigned flag = cl->ch == '~' ? OWF_STRIKE : use == 2 ? OWF_BOLD : OWF_ITALIC;
            int k;
            for (k = found + 1; k < closer; k++) {
                m->toks[k].flags |= flag;
                if (m->toks[k].kind == T_DELIM)
                    m->toks[k].active = 0;
            }
            o->count -= use;
            cl->count -= use;
            if (!o->count) o->active = 0;
            if (cl->count) closer--;                 /* the same closer again */
            else cl->active = 0;
        }
    }
}

static void delim_run(md *m, const char *s, size_t n, size_t i, size_t run, long href)
{
    int before = i ? (unsigned char)s[i - 1] : ' ';
    int after = i + run < n ? (unsigned char)s[i + run] : ' ';
    int bws = is_space(before), aws = is_space(after);
    int left = !aws && (!is_punct(after) || bws || is_punct(before));
    int right = !bws && (!is_punct(before) || aws || is_punct(after));
    md_tok *t;
    t = new_tok(m, T_DELIM);
    if (!t)
        return;
    t->ch = s[i];
    t->count = t->orig = (int)run;
    t->href = href;
    t->active = 1;
    if (s[i] == '_') {
        t->can_open = left && (!right || is_punct(before));
        t->can_close = right && (!left || is_punct(after));
    } else {
        t->can_open = left;
        t->can_close = right;
    }
}

static void inline_range(md *m, const char *s, size_t n, long href, int nest)
{
    size_t i = 0, k;
    int first = m->ntoks;
    while (i < n && !m->failed) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' && i + 1 < n && s[i + 1] == '\n') {
            if (new_tok(m, T_BREAK)) m->toks[m->ntoks - 1].href = href;
            i += 2;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        } else if (c == '\\' && i + 1 < n && is_punct((unsigned char)s[i + 1])) {
            put_text(m, s + i + 1, 1, href, 0, first);
            i += 2;
        } else if (c == '`') {
            size_t run, j, end = 0;
            for (k = i; k < n && s[k] == '`'; k++) ;
            run = k - i;
            for (j = k; j < n; j++)
                if (s[j] == '`') {
                    size_t e = j;
                    while (e < n && s[e] == '`') e++;
                    if (e - j == run) { end = j; break; }
                    j = e - 1;
                }
            if (!end) {
                put_text(m, s + i, run, href, 0, first);
                i = k;
                continue;
            }
            {
                size_t a = k, z = end, q;
                int all_space = 1;
                for (q = a; q < z; q++) if (s[q] != ' ' && s[q] != '\n') all_space = 0;
                if (!all_space && z - a >= 2 && (s[a] == ' ' || s[a] == '\n') && (s[z - 1] == ' ' || s[z - 1] == '\n')) {
                    a++;
                    z--;
                }
                for (q = a; q < z; q++)
                    put_text(m, s[q] == '\n' ? " " : s + q, 1, href, 1, first);
                if (a == z) {                        /* `` `` holds nothing to show */
                    md_tok *t = new_tok(m, T_TEXT);
                    if (t) { t->href = href; t->mono = 1; }
                }
            }
            i = end + run;
        } else if (c == '*' || c == '_' || c == '~') {
            for (k = i; k < n && s[k] == s[i]; k++) ;
            delim_run(m, s, n, i, k - i, href);
            i = k;
        } else if ((c == '[' || (c == '!' && i + 1 < n && s[i + 1] == '[')) && nest < 8) {
            size_t lb = c == '!' ? i + 1 : i, rb = close_bracket(s + lb, n - lb), dest = 0, dlen = 0, tail = 0;
            if (rb && lb + rb + 1 < n && s[lb + rb + 1] == '(')
                tail = link_tail(s + lb + rb + 1, n - lb - rb - 1, &dest, &dlen);
            if (!tail) {
                put_text(m, s + i, lb - i + 1, href, 0, first);
                i = lb + 1;
                continue;
            }
            dest += lb + rb + 1;
            if (c == '!') {
                inline_range(m, s + lb + 1, rb - 1, href, nest + 1);   /* a picture: its description */
                if (!m->said_pictures++)
                    owf_report_add(m->report, OWF_NOTE_LOST, "Pictures in Markdown are kept as their description");
            } else if (href >= 0) {
                inline_range(m, s + lb + 1, rb - 1, href, nest + 1);   /* no links in links */
            } else {
                inline_range(m, s + lb + 1, rb - 1, add_href(m, s + dest, dlen, NULL), nest + 1);
            }
            i = lb + rb + 1 + tail;
        } else if (c == '<') {
            int email;
            size_t len = autolink(s + i, n - i, &email);
            if (len && href < 0) {
                long h = add_href(m, s + i + 1, len - 2, email ? "mailto:" : NULL);
                put_text(m, s + i + 1, len - 2, h, 0, first);
                i += len;
            } else {
                if (i + 1 < n && (s[i + 1] == '/' || s[i + 1] == '!' || (s[i + 1] | 32) >= 'a') && (s[i + 1] | 32) <= 'z' &&
                    !m->said_html++)
                    owf_report_add(m->report, OWF_NOTE_APPROX, "HTML in Markdown is kept as text");
                put_text(m, s + i, 1, href, 0, first);
                i++;
            }
        } else if (c == '&') {
            unsigned long cp;
            size_t len = entity(s + i, n - i, &cp);
            if (len) {
                put_cp(m, cp, href, first);
                i += len;
            } else {
                put_text(m, s + i, 1, href, 0, first);
                i++;
            }
        } else if (c == '\n') {
            /* two spaces before a line's end break the line; otherwise it is a space */
            md_tok *t = m->ntoks > first ? &m->toks[m->ntoks - 1] : NULL;
            size_t spaces = 0;
            while (i > spaces && s[i - 1 - spaces] == ' ') spaces++;
            if (t && t->kind == T_TEXT && !t->mono && t->start + t->length == m->tx.length) {
                size_t cut = 0;
                while (cut < spaces && cut < t->length && m->tx.data[m->tx.length - 1 - cut] == ' ') cut++;
                t->length -= cut;
                m->tx.length -= cut;
            }
            if (spaces >= 2) {
                if (new_tok(m, T_BREAK)) m->toks[m->ntoks - 1].href = href;
            } else {
                put_text(m, " ", 1, href, 0, first);
            }
            i++;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        } else {
            for (k = i + 1; k < n; k++) {
                unsigned char d = (unsigned char)s[k];
                if (d == '\\' || d == '`' || d == '*' || d == '_' || d == '~' || d == '[' || d == '!' || d == '<' ||
                    d == '&' || d == '\n')
                    break;
            }
            put_text(m, s + i, k - i, href, 0, first);
            i = k;
        }
    }
    emphasis(m, first);
    for (k = (size_t)first; k < (size_t)m->ntoks; k++)  /* what is left of them is text: none matches outside */
        m->toks[k].active = 0;
}

/* Text with inlines into the builder's paragraph, at extra (OWF_BOLD...) besides its own. */
static void emit_inline(md *m, const char *s, size_t n, unsigned extra)
{
    int i;
    m->ntoks = 0;
    m->tx.length = 0;
    m->hrefs.length = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n')) n--;
    inline_range(m, s, n, -1, 0);
    if (m->tx.failed || m->hrefs.failed)
        m->failed = 1;
    for (i = 0; i < m->ntoks && !m->failed; i++) {
        const md_tok *t = &m->toks[i];
        unsigned flags = t->flags | extra;
        int font = t->kind == T_TEXT && t->mono ? m->mono_font : -1;
        if (m->b.charfmt.flags != flags || m->b.charfmt.font != font) {
            owf_builder_flush(&m->b);
            m->b.charfmt.flags = flags;
            m->b.charfmt.font = font;
        }
        if (t->kind == T_BREAK) {
            owf_builder_special(&m->b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
        } else if (t->kind == T_DELIM) {
            int left = t->count;
            while (left > 0) {
                char lit[64];
                int k, c = left < 64 ? left : 64;
                for (k = 0; k < c; k++) lit[k] = t->ch;
                if (t->href >= 0)
                    owf_builder_link_text(&m->b, lit, (size_t)c, (const char *)m->hrefs.data + t->href);
                else
                    owf_builder_text(&m->b, lit, (size_t)c);
                left -= c;
            }
        } else if (t->href >= 0) {
            if (t->length)
                owf_builder_link_text(&m->b, (const char *)m->tx.data + t->start, t->length,
                                      (const char *)m->hrefs.data + t->href);
        } else if (t->length) {
            owf_builder_text(&m->b, (const char *)m->tx.data + t->start, t->length);
        }
    }
    owf_builder_flush(&m->b);
    owf_charfmt_init(&m->b.charfmt);
}

/* ---- Blocks ---- */

static void base_fmt(md *m, owf_parafmt *f, int depth, int prefixed)
{
    owf_parafmt_init(f);
    f->indent_left = MD_INDENT_QUOTE * m->quote + MD_INDENT_LIST * depth;
    f->indent_right = MD_INDENT_QUOTE * m->quote;  /* a quote is in from both sides; a list from the left */
    if (prefixed)
        f->indent_first = -MD_INDENT_LIST;
}

static void start_para(md *m, const owf_parafmt *f)
{
    owf_builder_flush(&m->b);
    m->b.para = NULL;
    m->b.parafmt = *f;
    owf_charfmt_init(&m->b.charfmt);
}

static void end_para(md *m)
{
    owf_builder_end_para(&m->b);
}

static void put_prefix(md *m, const char *prefix)
{
    if (!prefix || !*prefix)
        return;
    owf_builder_text(&m->b, prefix, strlen(prefix));
    owf_builder_special(&m->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
}

/* An item whose bullet or number is on no paragraph yet: a paragraph of its own. */
static void flush_item(md *m)
{
    owf_parafmt f;
    if (!m->item_prefix_set)
        return;
    base_fmt(m, &f, m->depth, 1);
    start_para(m, &f);
    put_prefix(m, m->item_prefix);
    end_para(m);
    m->item_prefix_set = 0;
}

static void close_para(md *m)
{
    if (!m->in_para)
        return;
    start_para(m, &m->para_fmt);
    put_prefix(m, m->para_prefix);
    emit_inline(m, (const char *)m->raw.data, m->raw.length, 0);
    end_para(m);
    m->in_para = 0;
    m->para_lines = 0;
    m->raw.length = 0;
}

static void open_para(md *m, const char *s, size_t n)
{
    int prefixed = m->item_prefix_set;
    base_fmt(m, &m->para_fmt, m->depth, prefixed);
    m->para_prefix[0] = 0;
    if (prefixed) {
        strcpy(m->para_prefix, m->item_prefix);
        m->item_prefix_set = 0;
    }
    m->raw.length = 0;
    owf_buf_put(&m->raw, s, n);
    m->in_para = 1;
    m->para_lines = 1;
}

static void code_line(md *m, const char *s, size_t n)
{
    owf_parafmt f;
    size_t i, start = 0;
    base_fmt(m, &f, m->depth, 0);
    if (m->item_prefix_set) {
        flush_item(m);
    }
    start_para(m, &f);
    m->b.charfmt.font = m->mono_font;
    for (i = 0; i <= n; i++)
        if (i == n || s[i] == '\t') {
            if (i > start)
                owf_builder_text(&m->b, s + start, i - start);
            if (i < n)
                owf_builder_special(&m->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
            start = i + 1;
        }
    end_para(m);
    owf_charfmt_init(&m->b.charfmt);
}

static void end_icode(md *m)
{
    m->icode = 0;
    m->icode_blanks = 0;
}

static void end_table(md *m)
{
    m->table = 0;
}

static int split_cells(const char *s, size_t n, size_t *starts, size_t *lens, int max)
{
    size_t i = 0, cell;
    int count = 0, has_pipe = 0;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i < n && s[i] == '|') { i++; has_pipe = 1; }
    if (n > i && s[n - 1] == '|' && !(n >= 2 && s[n - 2] == '\\')) { n--; has_pipe = 1; }
    cell = i;
    for (; i <= n; i++) {
        if (i < n && s[i] == '\\' && i + 1 < n) { i++; continue; }
        if (i == n || s[i] == '|') {
            size_t a = cell, z = i;
            if (i < n) has_pipe = 1;
            while (a < z && (s[a] == ' ' || s[a] == '\t')) a++;
            while (z > a && (s[z - 1] == ' ' || s[z - 1] == '\t')) z--;
            if (count < max) {
                starts[count] = a;
                lens[count] = z - a;
            }
            count++;
            cell = i + 1;
        }
    }
    return has_pipe ? count : 0;
}

static int delimiter_row(const char *s, size_t n, owf_align *align, int max)
{
    size_t st[MD_MAX_COLS], ln[MD_MAX_COLS];
    int count = split_cells(s, n, st, ln, MD_MAX_COLS), k;
    if (count < 1 || count > max)
        return 0;
    for (k = 0; k < count; k++) {
        const char *c = s + st[k];
        size_t l = ln[k], j, a = 0, z = l;
        if (!l) return 0;
        if (c[0] == ':') a = 1;
        if (l > a && c[l - 1] == ':') z = l - 1;
        if (z <= a) return 0;
        for (j = a; j < z; j++) if (c[j] != '-') return 0;
        align[k] = a && z < l ? OWF_ALIGN_CENTRE : z < l ? OWF_ALIGN_RIGHT : OWF_ALIGN_LEFT;
    }
    return count;
}

static void table_row(md *m, const char *s, size_t n)
{
    size_t st[MD_MAX_COLS], ln[MD_MAX_COLS];
    int count = split_cells(s, n, st, ln, MD_MAX_COLS), k;
    for (k = 0; k < m->table_cols && !m->failed; k++) {
        owf_parafmt f;
        owf_para *p;
        base_fmt(m, &f, m->depth, 0);
        f.align = m->table_align[k];
        start_para(m, &f);
        if (k < count)
            emit_inline(m, s + st[k], ln[k], m->table_row ? 0 : OWF_BOLD);
        end_para(m);
        if (!m->b.story->nparas)
            continue;
        p = &m->b.story->paras[m->b.story->nparas - 1];
        p->table_id = m->table_id;
        p->table_row = m->table_row;
        p->table_col = k;
        p->table_cols = m->table_cols;
    }
    m->table_row++;
}

static size_t lead_spaces(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i] == ' ') i++;
    return i;
}

static int is_blank(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) if (!is_space((unsigned char)s[i])) return 0;
    return 1;
}

/* "***", "- - -", "___": a thematic break (s has no leading spaces). */
static int thematic(const char *s, size_t n)
{
    size_t i;
    int count = 0;
    char c = n ? s[0] : 0;
    if (c != '*' && c != '-' && c != '_')
        return 0;
    for (i = 0; i < n; i++) {
        if (s[i] == c) count++;
        else if (s[i] != ' ' && s[i] != '\t') return 0;
    }
    return count >= 3;
}

/* A setext underline: 1 for '=', 2 for '-', 0: none. */
static int setext(const char *s, size_t n)
{
    size_t i = 0;
    char c = n ? s[0] : 0;
    if (c != '=' && c != '-')
        return 0;
    while (i < n && s[i] == c) i++;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    return i == n ? (c == '=' ? 1 : 2) : 0;
}

/* An ATX heading: its level, and its text in *a, *z; 0: none. */
static int atx(const char *s, size_t n, size_t *a, size_t *z)
{
    size_t i = 0, e, level;
    while (i < n && s[i] == '#') i++;
    level = i;
    if (!i || i > 6 || (i < n && s[i] != ' ' && s[i] != '\t'))
        return 0;
    e = n;
    while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
    {
        size_t h = e;
        while (h > i && s[h - 1] == '#') h--;
        if (h < e && (h == i || s[h - 1] == ' ' || s[h - 1] == '\t'))
            e = h;
    }
    while (i < e && (s[i] == ' ' || s[i] == '\t')) i++;
    while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
    *a = i;
    *z = e;
    return (int)level;
}

/* A list item's marker: its width to the item's text (0: not one), and what it is. */
static size_t list_marker(const char *s, size_t n, int *ordered, char *ch, long *number, int *empty)
{
    size_t i = 0, spaces;
    *ordered = 0;
    *number = 0;
    if (n && (s[0] == '-' || s[0] == '+' || s[0] == '*')) {
        *ch = s[0];
        i = 1;
    } else {
        while (i < n && i < 9 && s[i] >= '0' && s[i] <= '9') {
            *number = *number * 10 + (s[i] - '0');
            i++;
        }
        if (!i || i >= n || (s[i] != '.' && s[i] != ')'))
            return 0;
        *ch = s[i++];
        *ordered = 1;
    }
    if (i < n && s[i] != ' ' && s[i] != '\t')
        return 0;
    *empty = is_blank(s + i, n - i);
    if (*empty)
        return i + 1;
    spaces = lead_spaces(s + i, n - i);
    return i + (spaces >= 5 ? 1 : spaces);
}

static void line(md *m, const char *s, size_t n)
{
    size_t i = 0, ind, a, z, cc = 0;
    int q = 0, d, level;

    /* the quotes it is in */
    for (;;) {
        size_t sp = lead_spaces(s + i, n - i);
        if (sp > 3 || i + sp >= n || s[i + sp] != '>')
            break;
        i += sp + 1;
        if (i < n && s[i] == ' ') i++;
        q++;
    }
    s += i;
    n -= i;

    if (m->fence) {
        size_t sp = lead_spaces(s, n), k;
        int closes = q < m->fence_quote ||
                     (m->fence_depth && !is_blank(s, n) && sp < (size_t)m->lv[m->fence_depth - 1].content_col);
        if (!closes) {
            for (k = sp; k < n && s[k] == m->fence_ch; k++) ;
            if (sp <= (size_t)m->fence_col + 3 && k - sp >= (size_t)m->fence && is_blank(s + k, n - k)) {
                m->fence = 0;
                m->blank = 0;
                return;
            }
            sp = sp < (size_t)m->fence_col ? sp : (size_t)m->fence_col;
            code_line(m, s + sp, n - sp);
            return;
        }
        m->fence = 0;
    }

    if (q != m->quote) {
        if (q < m->quote && m->in_para && !m->blank && !is_blank(s, n) && !m->table) {
            size_t sp = lead_spaces(s, n);
            int o, e;
            char c;
            long num;
            if (!atx(s + sp, n - sp, &a, &z) && !thematic(s + sp, n - sp) && !list_marker(s + sp, n - sp, &o, &c, &num, &e)) {
                owf_buf_putc(&m->raw, '\n');           /* a lazy line of the quote's paragraph */
                owf_buf_put(&m->raw, s + sp, n - sp);
                m->para_lines++;
                return;
            }
        }
        close_para(m);
        flush_item(m);
        end_icode(m);
        end_table(m);
        m->depth = 0;
        m->quote = q;
    }

    if (is_blank(s, n)) {
        close_para(m);
        flush_item(m);
        end_table(m);
        if (m->icode)
            m->icode_blanks++;
        m->blank = 1;
        return;
    }

    ind = lead_spaces(s, n);

    /* the lists it is in */
    {
        int ordered, empty;
        char ch;
        long number;
        size_t width = 0;
        size_t top = m->depth ? (size_t)m->lv[m->depth - 1].content_col : 0;
        int can_start = ind < 4 || (m->depth && (ind < top || ind - top < 4));
        if (!thematic(s + ind, n - ind) && !(m->in_para && setext(s + ind, n - ind) == 2 && ind < 4) && can_start &&
            !(m->icode && m->depth == 0 && ind >= 4))
            width = list_marker(s + ind, n - ind, &ordered, &ch, &number, &empty);
        if (width && m->in_para && !m->depth && !m->blank && (empty || (ordered && number != 1)))
            width = 0;                               /* it can't break a paragraph */
        if (width) {
            int sibling = 0;
            d = m->depth;
            while (d > 0 && ind < (size_t)m->lv[d - 1].content_col) {
                int parent = d >= 2 ? m->lv[d - 2].content_col : 0;
                if ((int)ind >= parent) { sibling = 1; break; }
                d--;
            }
            close_para(m);
            flush_item(m);
            end_icode(m);
            end_table(m);
            if (d >= MD_MAX_DEPTH)
                d = MD_MAX_DEPTH - 1, sibling = 1;
            if (sibling && m->lv[d - 1].ordered == ordered && m->lv[d - 1].ch == ch) {
                m->lv[d - 1].number++;
            } else {
                if (!sibling) d++;
                m->lv[d - 1].ordered = ordered;
                m->lv[d - 1].ch = ch;
                m->lv[d - 1].number = ordered ? number : 0;
            }
            m->lv[d - 1].marker_col = (int)ind;
            m->lv[d - 1].content_col = (int)(ind + width);
            m->depth = d;
            if (ordered)
                snprintf(m->item_prefix, sizeof m->item_prefix, "%ld%c", m->lv[d - 1].number % 1000000000L, ch);
            else
                strcpy(m->item_prefix, "\xE2\x80\xA2");   /* U+2022 */
            m->item_prefix_set = 1;
            m->blank = 0;
            if (!empty) {
                size_t at = ind + width;
                if (at > n) at = n;
                open_para(m, s + at, n - at);
            }
            return;
        }
        d = m->depth;
        while (d > 0 && ind < (size_t)m->lv[d - 1].content_col)
            d--;
        if (d < m->depth) {
            if (m->in_para && !m->blank && !m->table && !atx(s + ind, n - ind, &a, &z) && !thematic(s + ind, n - ind) &&
                !(ind < 4 && ((s[ind] == '`' && n - ind >= 3 && !memcmp(s + ind, "```", 3)) ||
                              (s[ind] == '~' && n - ind >= 3 && !memcmp(s + ind, "~~~", 3))))) {
                owf_buf_putc(&m->raw, '\n');           /* a lazy line of the item's paragraph */
                owf_buf_put(&m->raw, s + ind, n - ind);
                m->para_lines++;
                m->blank = 0;
                return;
            }
            close_para(m);
            flush_item(m);
            end_icode(m);
            end_table(m);
            m->depth = d;
        }
        if (d) {
            cc = (size_t)m->lv[d - 1].content_col;
            s += cc;
            n -= cc;
            ind -= cc;
        }
    }

    if (m->table) {
        if (ind < 4 && memchr(s, '|', n)) {
            table_row(m, s, n);
            m->blank = 0;
            return;
        }
        end_table(m);
    }

    if (ind >= 4 && !m->in_para) {
        while (m->icode && m->icode_blanks) {
            code_line(m, "", 0);
            m->icode_blanks--;
        }
        m->icode = 1;
        m->icode_blanks = 0;
        code_line(m, s + 4, n - 4);
        m->blank = 0;
        return;
    }
    end_icode(m);
    m->blank = 0;
    s += ind;
    n -= ind;

    if ((level = atx(s, n, &a, &z)) > 0) {
        owf_parafmt f;
        close_para(m);
        base_fmt(m, &f, m->depth, m->item_prefix_set);
        f.heading = level;
        start_para(m, &f);
        if (m->item_prefix_set) {
            put_prefix(m, m->item_prefix);
            m->item_prefix_set = 0;
        }
        emit_inline(m, s + a, z - a, 0);
        end_para(m);
        return;
    }
    if (n >= 3 && (s[0] == '`' || s[0] == '~')) {
        size_t k = 0;
        while (k < n && s[k] == s[0]) k++;
        if (k >= 3 && !(s[0] == '`' && memchr(s + k, '`', n - k))) {
            close_para(m);
            flush_item(m);
            m->fence = (int)k;
            m->fence_ch = s[0];
            m->fence_col = (int)(ind + cc);
            m->fence_quote = m->quote;
            m->fence_depth = m->depth;
            return;
        }
    }
    if (m->in_para && (level = setext(s, n)) > 0) {
        m->para_fmt.heading = level;
        close_para(m);
        return;
    }
    if (m->in_para && m->para_lines == 1 && memchr(m->raw.data, '|', m->raw.length)) {
        owf_align align[MD_MAX_COLS];
        size_t st[MD_MAX_COLS], ln[MD_MAX_COLS];
        int cols = delimiter_row(s, n, align, MD_MAX_COLS);
        if (cols && memchr(s, '|', n) &&
            split_cells((const char *)m->raw.data, m->raw.length, st, ln, MD_MAX_COLS) == cols) {
            owf_buf head;
            owf_buf_init(&head);
            owf_buf_put(&head, m->raw.data, m->raw.length);
            m->in_para = 0;
            m->raw.length = 0;
            flush_item(m);
            m->table = 1;
            m->table_id = m->next_table++;
            m->table_row = 0;
            m->table_cols = cols;
            memcpy(m->table_align, align, sizeof align);
            if (!head.failed)
                table_row(m, (const char *)head.data, head.length);
            else
                m->failed = 1;
            owf_buf_free(&head);
            return;
        }
    }
    if (thematic(s, n)) {
        owf_parafmt f;
        close_para(m);
        flush_item(m);
        base_fmt(m, &f, m->depth, 0);
        f.borders = OWF_BORDER_BOTTOM;
        start_para(m, &f);
        end_para(m);
        return;
    }
    if (m->in_para) {
        owf_buf_putc(&m->raw, '\n');
        owf_buf_put(&m->raw, s, n);
        m->para_lines++;
    } else {
        open_para(m, s, n);
    }
}

static int detect_md(const unsigned char *data, size_t length)
{
    size_t i, n = length < 4096 ? length : 4096;
    for (i = 0; i < n; i++) {
        unsigned c = data[i];
        if (c == 0 || (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f'))
            return 0;
    }
    return 5;  /* below plain text's: Markdown by the file's name, never by its bytes alone */
}

static int import_md(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    md m;
    owf_buf conv, ln;
    const char *s;
    size_t n, i, start;
    int utf8, result;

    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        length -= 3;
    }
    memset(&m, 0, sizeof m);
    m.doc = doc;
    m.report = report;
    owf_buf_init(&conv);
    owf_buf_init(&ln);
    owf_buf_init(&m.tx);
    owf_buf_init(&m.hrefs);
    owf_buf_init(&m.raw);
    utf8 = owf_utf8_valid(data, length);
    if (utf8) {
        s = (const char *)data;
        n = length;
    } else {
        owf_buf_put_latin1(&conv, data, length);
        if (conv.failed) {
            owf_buf_free(&conv);
            return OWF_ERR_MEMORY;
        }
        s = (const char *)conv.data;
        n = conv.length;
    }
    owf_report_add(report, OWF_NOTE_INFO, utf8 ? "Read Markdown (UTF-8)" : "Read Markdown (Amiga ISO-8859-1 text)");
    m.mono_font = owf_doc_font(doc, "Courier", OWF_FONT_MONO);
    if (m.mono_font < 0)
        m.failed = 1;
    owf_builder_init(&m.b, doc, &doc->body);
    start = 0;
    for (i = 0; i <= n && !m.failed; i++) {
        if (i < n && s[i] != '\n' && s[i] != '\r')
            continue;
        /* the line, its tabs as spaces to the next multiple of four where they indent it */
        {
            size_t k, col = 0;
            int lead = 1;
            ln.length = 0;
            for (k = start; k < i; k++) {
                char c = s[k];
                if (c == '\t' && lead) {
                    do { owf_buf_putc(&ln, ' '); col++; } while (col % 4);
                    continue;
                }
                if (c == '\f') c = ' ';
                if (c != ' ' && c != '>') lead = 0;
                owf_buf_putc(&ln, c);
                col++;
            }
            if (ln.failed) {
                m.failed = 1;
                break;
            }
            line(&m, ln.length ? (const char *)ln.data : "", ln.length);
        }
        if (i < n && s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
            i++;
        start = i + 1;
    }
    close_para(&m);
    flush_item(&m);
    result = owf_builder_done(&m.b);
    if (m.failed || m.tx.failed || m.hrefs.failed || m.raw.failed)
        result = OWF_ERR_MEMORY;
    free(m.toks);
    owf_buf_free(&m.tx);
    owf_buf_free(&m.hrefs);
    owf_buf_free(&m.raw);
    owf_buf_free(&ln);
    owf_buf_free(&conv);
    return result;
}

extern int owf_export_md(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report);

const owf_format owf_format_markdown = {
    "markdown", "Markdown (CommonMark, with GitHub's tables)", "md markdown", detect_md, import_md, owf_export_md
};

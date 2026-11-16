/*
 * libowf 1.16: an IF field's form, as Word writes it after "IF": two operands, an operator, the text shown
 * when the comparison holds and the text shown when it doesn't, as in
 *     {MERGEFIELD Gender} = "F" "Dear Ms" "Dear Mr"
 * An operand in braces is a field of its own (its code without the braces), the rest are text. Shared by
 * the DOCX and ODT readers and writers and GFOpenWrite's editor (which works the field's value out), so
 * header-only: no library call.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_IFFORM_H
#define OWF_IFFORM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char a[128], b[128];     /* the operands: text, or (a_field, b_field) a field's code */
    int a_field, b_field;
    char op[3];              /* = <> < > <= >= */
    char yes[128], no[128];  /* shown when it holds, when it doesn't */
} owf_if_form;

/* One token from *s: {a field}, "text" (Word's \" and \\ in it), an operator, or a word; 0 at the end.
 * *kind: 1 a field, 2 text, 3 an operator. */
static inline int owf_if_token(const char **s, char *out, size_t size, int *kind)
{
    const char *p = *s;
    size_t n = 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) { *s = p; return 0; }
    if (*p == '{') {
        int depth = 1;
        *kind = 1;
        for (++p; *p && depth; ++p) {
            if (*p == '{') ++depth;
            else if (*p == '}' && !--depth) break;
            if (n + 1 < size) out[n++] = *p;
        }
        if (*p == '}') ++p;
        while (n && out[n - 1] == ' ') --n;      /* the code without its spaces either side */
        out[n] = 0;
        n = strspn(out, " ");
        if (n) memmove(out, out + n, strlen(out + n) + 1);
    } else if (*p == '"') {
        *kind = 2;
        for (++p; *p && *p != '"'; ++p) {
            if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) ++p;
            if (n + 1 < size) out[n++] = *p;
        }
        if (*p == '"') ++p;
        out[n] = 0;
    } else if (*p == '=' || *p == '<' || *p == '>') {
        *kind = 3;
        out[n++] = *p++;
        if ((out[0] == '<' && (*p == '>' || *p == '=')) || (out[0] == '>' && *p == '=')) out[n++] = *p++;
        out[n] = 0;
    } else {
        *kind = 2;
        while (*p && *p != ' ' && *p != '\t' && *p != '=' && *p != '<' && *p != '>' && *p != '"') {
            if (n + 1 < size) out[n++] = *p;
            ++p;
        }
        out[n] = 0;
    }
    *s = p;
    return 1;
}

/* The form read: 1, or 0 when it isn't one (no operator, or no text to show when it holds) */
static inline int owf_if_parse(const char *s, owf_if_form *f)
{
    char op[8];
    int kind = 0;
    memset(f, 0, sizeof *f);
    if (!s) return 0;
    if (!owf_if_token(&s, f->a, sizeof f->a, &kind) || kind == 3) return 0;
    f->a_field = kind == 1;
    if (!owf_if_token(&s, op, sizeof op, &kind) || kind != 3) return 0;
    memcpy(f->op, op, sizeof f->op - 1);
    if (!owf_if_token(&s, f->b, sizeof f->b, &kind) || kind == 3) return 0;
    f->b_field = kind == 1;
    if (!owf_if_token(&s, f->yes, sizeof f->yes, &kind) || kind == 3) return 0;
    if (owf_if_token(&s, f->no, sizeof f->no, &kind) && kind == 3) return 0;
    return 1;
}

static inline size_t owf_if_put_quoted(char *out, size_t size, size_t n, const char *s)
{
    if (n + 1 < size) out[n++] = '"';
    for (; *s && n + 3 < size; ++s) {
        if (*s == '"' || *s == '\\') out[n++] = '\\';
        out[n++] = *s;
    }
    if (n + 1 < size) out[n++] = '"';
    out[n < size ? n : size - 1] = 0;
    return n;
}

/* The form written back, Word's way */
static inline void owf_if_format(const owf_if_form *f, char *out, size_t size)
{
    size_t n = 0;
    if (!size) return;
    out[0] = 0;
    if (f->a_field) n += (size_t)snprintf(out + n, size - n, "{%s}", f->a);
    else n = owf_if_put_quoted(out, size, n, f->a);
    if (n < size) n += (size_t)snprintf(out + n, size - n, " %s ", f->op);
    if (n >= size) return;
    if (f->b_field) n += (size_t)snprintf(out + n, size - n, "{%s}", f->b);
    else n = owf_if_put_quoted(out, size, n, f->b);
    if (n + 1 < size) out[n++] = ' ';
    n = owf_if_put_quoted(out, size, n, f->yes);
    if (n + 1 < size) out[n++] = ' ';
    owf_if_put_quoted(out, size, n, f->no);
}

/* Word's * and ? in p against s: iterative, going back only to the last * (with the text one on from where
 * it last matched), so no pattern takes more than about len(s) * len(p) steps (Budgie on #376: the recursive
 * one took 37 s on six stars) */
static inline int owf_if_wild(const char *s, const char *p)
{
    const char *star = NULL, *from = NULL;
    while (*s) {
        if (*p == '*') { star = p++; from = s; }
        else if (*p == '?' || *p == *s) { ++p; ++s; }
        else if (star) { p = star + 1; s = ++from; }
        else return 0;
    }
    while (*p == '*') ++p;
    return !*p;
}

/* A number as a comparison takes one: digits with a sign, a point and an exponent (not strtod's nan, inf or
 * hex, Budgie) */
static inline int owf_if_number(const char *s, double *v)
{
    const char *p = s;
    char *end;
    int digits = 0;
    if (*p == '+' || *p == '-') ++p;
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') digits = 1;
        else if (*p != '.' && *p != 'e' && *p != 'E' && *p != '+' && *p != '-') return 0;
    }
    if (!digits) return 0;
    *v = strtod(s, &end);
    return !*end;
}

/* Whether the comparison holds between the operands' values: numbers as numbers, else text; = and <> take
 * Word's * and ? in the second */
static inline int owf_if_holds(const char *a, const char *op, const char *b)
{
    double x = 0, y = 0;
    int c, numbers = owf_if_number(a, &x) && owf_if_number(b, &y);
    if (!strcmp(op, "=")) return numbers ? x == y : owf_if_wild(a, b);
    if (!strcmp(op, "<>")) return numbers ? x != y : !owf_if_wild(a, b);
    c = numbers ? (x > y) - (x < y) : strcmp(a, b);
    if (!strcmp(op, "<")) return c < 0;
    if (!strcmp(op, ">")) return c > 0;
    if (!strcmp(op, "<=")) return c <= 0;
    if (!strcmp(op, ">=")) return c >= 0;
    return 0;
}

#endif

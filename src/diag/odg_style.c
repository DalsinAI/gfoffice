/*
 * odg_style.c: GFOpenDiag's Style window's sums (opendiag_style.h).
 * Cleanroom: written from the design (amicradle-teams designs/GFOPENDIAG-20261113.md), draw.io's own style keys as
 * its files carry them, and this repository's core; no other program's code was read for it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <stdio.h>
#include "opendiag_style.h"

static const struct { const char *key, *title; odg_prop_kind kind; } props[ODG_NPROPS] = {
    { "fillColor", "Fill", ODG_KIND_COLOUR },
    { "strokeColor", "Line", ODG_KIND_COLOUR },
    { "fontColor", "Text colour", ODG_KIND_COLOUR },
    { "strokeWidth", "Line width", ODG_KIND_NUMBER },
    { "fontSize", "Text size", ODG_KIND_NUMBER },
    { "dashed", "Dashed", ODG_KIND_SWITCH },
    { "rounded", "Rounded", ODG_KIND_SWITCH }
};

const char *odg_prop_key(odg_prop p) { return p >= 0 && p < ODG_NPROPS ? props[p].key : ""; }
const char *odg_prop_title(odg_prop p) { return p >= 0 && p < ODG_NPROPS ? props[p].title : ""; }
odg_prop_kind odg_prop_kind_of(odg_prop p) { return p >= 0 && p < ODG_NPROPS ? props[p].kind : ODG_KIND_NUMBER; }

static int hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void put(char *out, size_t size, const char *s)
{
    if (size)
        snprintf(out, size, "%s", s);
}

int odg_prop_check(odg_prop p, const char *text, char *out, size_t size)
{
    size_t n, i;
    if (p < 0 || p >= ODG_NPROPS || !text)
        return 0;
    while (*text == ' ' || *text == '\t')
        text++;
    n = strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t'))
        n--;
    if (!n) {
        put(out, size, "");
        return 1;
    }
    switch (props[p].kind) {
    case ODG_KIND_COLOUR: {
        char c[8];
        if (n == 4 && (text[0] | 32) == 'n' && (text[1] | 32) == 'o' && (text[2] | 32) == 'n' && (text[3] | 32) == 'e') {
            put(out, size, "none");
            return 1;
        }
        if (text[0] != '#' || (n != 4 && n != 7))
            return 0;
        for (i = 1; i < n; i++)
            if (hex((unsigned char)text[i]) < 0)
                return 0;
        c[0] = '#';
        for (i = 0; i < 6; i++) {
            int ch = n == 4 ? text[1 + i / 2] : text[1 + i];
            c[1 + i] = (char)(ch >= 'A' && ch <= 'F' ? ch + 32 : ch);
        }
        c[7] = 0;
        put(out, size, c);
        return 1;
    }
    case ODG_KIND_NUMBER: {
        size_t dot = n, digits = 0;
        owf_dnum v = 0;
        char buf[24];
        for (i = 0; i < n; i++) {
            if (text[i] == '.') {
                if (dot != n) return 0;
                dot = i;
            } else if (text[i] >= '0' && text[i] <= '9') {
                digits++;
                if (dot == n) {
                    if (v >= OWF_DNUM(100)) return 0;     /* past 999 */
                    v = v * 10 + OWF_DNUM(text[i] - '0');
                } else if (i - dot <= 2) {
                    v += (text[i] - '0') * (i - dot == 1 ? 10 : 1);
                } else if (text[i] != '0') {
                    return 0;                              /* a third decimal that isn't 0 */
                }
            } else {
                return 0;
            }
        }
        if (!digits || v <= 0)
            return 0;
        owf_dnum_format(v, buf, sizeof buf);
        put(out, size, buf);
        return 1;
    }
    case ODG_KIND_SWITCH:
        if (n == 1 && (text[0] == '0' || text[0] == '1')) {
            put(out, size, text[0] == '1' ? "1" : "0");
            return 1;
        }
        return 0;
    }
    return 0;
}

/* p's value in one style, as odg_style_common gives it */
static void value_of(const char *style, odg_prop p, char *out, size_t size)
{
    char buf[64];
    if (!owf_dstyle_get(style, props[p].key, buf, sizeof buf)) {
        /* a switch off when not set; "rounded" also as draw.io's bare word */
        put(out, size, props[p].kind == ODG_KIND_SWITCH ? "0" : "");
        return;
    }
    if (props[p].kind == ODG_KIND_SWITCH)
        put(out, size, !strcmp(buf, "1") || !buf[0] ? "1" : "0");
    else
        put(out, size, buf);
}

int odg_style_common(const odg_doc *doc, const char *const *ids, int n, odg_prop p, char *out, size_t size)
{
    const owf_dpage *pg = odg_page(doc);
    char first[64], v[64];
    int i, seen = 0;
    put(out, size, "");
    if (p < 0 || p >= ODG_NPROPS)
        return 0;
    for (i = 0; i < n; i++) {
        const owf_dcell *c = ids[i] ? owf_dpage_find(pg, ids[i]) : NULL;
        if (!c)
            continue;
        value_of(c->style, p, seen ? v : first, sizeof first);
        if (seen && strcmp(v, first))
            return 0;
        seen = 1;
    }
    if (!seen)
        return 0;
    put(out, size, first);
    return 1;
}

int odg_style_apply_all(odg_doc *doc, const char *const *ids, int n, const char *const *texts, int *bad)
{
    char values[ODG_NPROPS][24];
    const char *keys[ODG_NPROPS], *vals[ODG_NPROPS];
    int p, k = 0;
    if (bad) *bad = -1;
    for (p = 0; p < ODG_NPROPS; p++) {
        if (!texts[p])
            continue;
        if (!odg_prop_check((odg_prop)p, texts[p], values[p], sizeof values[p])) {
            if (bad) *bad = p;
            return OWF_ERR_FORMAT;
        }
        if (props[p].kind == ODG_KIND_SWITCH && !strcmp(values[p], "0"))
            values[p][0] = 0;              /* a switch off is the key taken off, as draw.io leaves it */
        keys[k] = props[p].key;
        vals[k++] = values[p][0] ? values[p] : NULL;
    }
    return k ? odg_set_styles(doc, ids, n, keys, vals, k) : OWF_OK;
}

int odg_style_field(const odg_doc *doc, const char *const *ids, int n, odg_prop p, char *out, size_t size)
{
    char v[64];
    put(out, size, "");
    if (!odg_style_common(doc, ids, n, p, v, sizeof v) || !v[0])
        return 0;
    if (!odg_prop_check(p, v, out, size)) {
        put(out, size, "");
        return 0;
    }
    return 1;
}

int odg_style_apply(odg_doc *doc, const char *const *ids, int n, odg_prop p, const char *text)
{
    char value[24];
    if (!odg_prop_check(p, text, value, sizeof value))
        return OWF_ERR_FORMAT;
    /* a switch off is the key taken off, as draw.io leaves it */
    if (props[p].kind == ODG_KIND_SWITCH && !strcmp(value, "0"))
        value[0] = 0;
    return odg_set_style(doc, ids, n, props[p].key, value[0] ? value : NULL);
}

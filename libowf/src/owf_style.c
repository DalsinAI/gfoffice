/*
 * libowf: named styles with inheritance, for the readers of formats that
 * have them (ODT, DOCX). A style sets some properties and leaves the rest
 * to its parent; the masks say which.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

void owf_charprops_apply(owf_charfmt *to, const owf_charprops *p)
{
    static const struct { unsigned mask, flag; } bits[] = {
        { OWF_CP_BOLD, OWF_BOLD }, { OWF_CP_ITALIC, OWF_ITALIC }, { OWF_CP_UNDERLINE, OWF_UNDERLINE },
        { OWF_CP_STRIKE, OWF_STRIKE }, { OWF_CP_POSITION, OWF_SUPER | OWF_SUB }
    };
    size_t i;
    for (i = 0; i < sizeof bits / sizeof bits[0]; i++)
        if (p->mask & bits[i].mask)
            to->flags = (to->flags & ~bits[i].flag) | (p->fmt.flags & bits[i].flag);
    if (p->mask & OWF_CP_FONT)
        to->font = p->fmt.font;
    if (p->mask & OWF_CP_SIZE)
        to->size = p->fmt.size;
    if (p->mask & OWF_CP_COLOUR)
        to->colour = p->fmt.colour;
}

void owf_paraprops_apply(owf_parafmt *to, const owf_paraprops *p)
{
    const owf_parafmt *f = &p->fmt;
    if (p->mask & OWF_PP_ALIGN) to->align = f->align;
    if (p->mask & OWF_PP_LEFT) to->indent_left = f->indent_left;
    if (p->mask & OWF_PP_RIGHT) to->indent_right = f->indent_right;
    if (p->mask & OWF_PP_FIRST) to->indent_first = f->indent_first;
    if (p->mask & OWF_PP_BEFORE) to->space_before = f->space_before;
    if (p->mask & OWF_PP_AFTER) to->space_after = f->space_after;
    if (p->mask & OWF_PP_LINE) to->line_spacing = f->line_spacing;
    if (p->mask & OWF_PP_BREAK) to->page_break_before = f->page_break_before;
    if (p->mask & OWF_PP_HEADING) to->heading = f->heading;
    if (p->mask & OWF_PP_TABS) {
        to->ntabs = f->ntabs;
        memcpy(to->tabs, f->tabs, sizeof to->tabs);
    }
}

owf_style *owf_styles_add(owf_styles *s, const char *name, int family)
{
    owf_style *st;
    if (s->count == s->capacity) {
        int want = s->capacity ? s->capacity * 2 : 32;
        owf_style *items = realloc(s->items, (size_t)want * sizeof *items);
        if (!items)
            return NULL;
        s->items = items;
        s->capacity = want;
    }
    st = &s->items[s->count++];
    memset(st, 0, sizeof *st);
    st->name = name;
    st->family = family;
    st->list_level = -1;
    return st;
}

const owf_style *owf_styles_find(const owf_styles *s, const char *name, int family)
{
    int i;
    if (!name)
        return NULL;
    for (i = s->count - 1; i >= 0; i--)
        if (s->items[i].family == family && strcmp(s->items[i].name, name) == 0)
            return &s->items[i];
    return NULL;
}

void owf_styles_free(owf_styles *s)
{
    free(s->items);
    s->items = NULL;
    s->count = s->capacity = 0;
}

/* Applies a style and its parents (parents first), looking in each table in
 * turn; depth stops a style that is its own ancestor. */
static void apply(const owf_styles *const *tables, int ntables, const char *name, int family,
                  owf_parafmt *para, owf_charfmt *chr, int depth)
{
    const owf_style *st = NULL;
    int i;
    if (!name || depth > 16)
        return;
    for (i = 0; i < ntables && !st; i++)
        st = owf_styles_find(tables[i], name, family);
    if (!st)
        return;
    if (st->parent && strcmp(st->parent, name) != 0)
        apply(tables, ntables, st->parent, family, para, chr, depth + 1);
    if (para)
        owf_paraprops_apply(para, &st->pp);
    if (chr)
        owf_charprops_apply(chr, &st->cp);
}

void owf_styles_resolve(const owf_styles *const *tables, int ntables, const char *name, int family,
                        owf_parafmt *para, owf_charfmt *chr)
{
    apply(tables, ntables, name, family, para, chr, 0);
}

const owf_style *owf_styles_lookup(const owf_styles *const *tables, int ntables, const char *name, int family)
{
    int i;
    for (i = 0; i < ntables; i++) {
        const owf_style *st = owf_styles_find(tables[i], name, family);
        if (st)
            return st;
    }
    return NULL;
}

/* "1, 2, 3", "a, b, c", "I, II, III"... */
void owf_format_number(owf_buf *out, int n, owf_number_format format)
{
    static const char *roman[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
    static const int values[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    int i;

    if (n < 1)
        n = 1;
    switch (format) {
    case OWF_NUMBER_LOWER_LETTER:
    case OWF_NUMBER_UPPER_LETTER: {
        char letters[8];
        int len = 0;
        while (n > 0 && len < 7) {
            n--;
            letters[len++] = (char)((format == OWF_NUMBER_UPPER_LETTER ? 'A' : 'a') + n % 26);
            n /= 26;
        }
        while (len)
            owf_buf_putc(out, letters[--len]);
        break;
    }
    case OWF_NUMBER_LOWER_ROMAN:
    case OWF_NUMBER_UPPER_ROMAN:
        if (n > 3999)
            n = 3999;
        for (i = 0; i < 13; i++)
            while (n >= values[i]) {
                const char *r = roman[i];
                for (; *r; r++)
                    owf_buf_putc(out, format == OWF_NUMBER_UPPER_ROMAN ? *r - 'a' + 'A' : *r);
                n -= values[i];
            }
        break;
    default:
        owf_buf_printf(out, "%d", n);
        break;
    }
}

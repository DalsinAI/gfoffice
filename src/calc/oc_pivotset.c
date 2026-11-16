/*
 * GFOpenCalc's portable core: the active sheet's pivot table changed by a
 * line of words, as ARexx's PIVOTSET and the window give them: a field's
 * items sorted, single items hidden or shown again, a field's numbers or
 * dates grouped, items put together by hand under a name, and how a data
 * field's values are shown. Fields are named by their heading (case aside)
 * or by their number in the source (1 the first column). Each change lays
 * the table out again as one Undo step (oc_core_pivot_set_layout).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORDS 32
#define WORD 128

static int same_word(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return !*a && !*b;
}

/* The line's words: spaces between them, "double quotes" round one with spaces ("" inside is one "). */
static int split_words(const char *p, char words[WORDS][WORD])
{
    int n = 0;
    for (;;) {
        size_t k = 0;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            return n;
        if (n == WORDS)
            return -1;
        if (*p == '"') {
            for (p++; *p; p++) {
                if (*p == '"') {
                    if (p[1] != '"')
                        break;
                    p++;
                }
                if (k + 1 >= WORD)
                    return -1;
                words[n][k++] = *p;
            }
            if (*p != '"')
                return -1;
            p++;
        } else {
            while (*p && *p != ' ' && *p != '\t') {
                if (k + 1 >= WORD)
                    return -1;
                words[n][k++] = *p++;
            }
        }
        words[n++][k] = 0;
    }
}

int oc_core_pivot_field(const oc_core *core, const owf_pivotdef *def, const char *name)
{
    unsigned n, i;
    char heading[WORD], *end;
    unsigned long number;
    if (!core || !def || !name || !*name)
        return -1;
    n = def->source.last_col - def->source.first_col + 1u;
    for (i = 0; i < n; i++) {
        oc_core_pivot_heading(core, def, i, heading, sizeof heading);
        if (same_word(heading, name))
            return (int)i;
    }
    number = strtoul(name, &end, 10);
    if (!*end && number >= 1 && number <= n)
        return (int)number - 1;
    return -1;
}

static int why(char *out, size_t size, int rc, const char *text)
{
    if (out && size)
        snprintf(out, size, "%s", text);
    return rc;
}

/* An item of field as typed: "(blank)" the empty one, else as oc_core_pivot_item reads it. */
static void item_of(const oc_core *core, const owf_pivotdef *def, unsigned field, const char *text, owf_pivot_page *item)
{
    memset(item, 0, sizeof *item);
    item->field = field;
    if (same_word(text, "(blank)"))
        item->kind = OWF_PIVOT_BLANK;
    else
        oc_core_pivot_item(core, def, field, text, item);
}

static int number_word(const char *s, double *v)
{
    char *end;
    *v = strtod(s, &end);
    return end != s && !*end;
}

int oc_core_pivot_command(oc_core *core, const char *text, char *out, size_t size)
{
    char w[WORDS][WORD];
    owf_pivotdef def;
    int n, field = -1, k;
    if (out && size)
        out[0] = 0;
    if (!core || !text)
        return OWF_ERR_FORMAT;
    if (!oc_core_pivot_get(core, &def))
        return why(out, size, OWF_ERR_UNSUPPORTED, "The active sheet has no pivot table.");
    n = split_words(text, w);
    if (n < 2)
        return why(out, size, OWF_ERR_FORMAT, "SORT, HIDE, SHOW, GROUP, NAME, UNNAME or SHOWAS, then a field.");
    if (!same_word(w[0], "SHOWAS") && (field = oc_core_pivot_field(core, &def, w[1])) < 0)
        return why(out, size, OWF_ERR_FORMAT, "No field of the pivot table's source has that heading.");

    if (same_word(w[0], "SORT")) {
        /* SORT field ASCENDING | DESCENDING | SOURCE */
        int order = n != 3 ? -1
                    : same_word(w[2], "ASCENDING") ? OWF_PIVOT_SORT_ASCENDING
                    : same_word(w[2], "DESCENDING") ? OWF_PIVOT_SORT_DESCENDING
                    : same_word(w[2], "SOURCE") ? OWF_PIVOT_SORT_SOURCE : -1;
        if (order < 0)
            return why(out, size, OWF_ERR_FORMAT, "SORT field ASCENDING, DESCENDING or SOURCE.");
        if (!owf_pivot_set_sort(&def, (unsigned)field, order))
            return why(out, size, OWF_ERR_UNSUPPORTED, "A pivot table sorts up to 16 fields.");
    } else if (same_word(w[0], "HIDE") || same_word(w[0], "SHOW")) {
        /* HIDE field item [item...] | SHOW field item [item...] | SHOW field ALL */
        int hide = same_word(w[0], "HIDE");
        if (n < 3)
            return why(out, size, OWF_ERR_FORMAT, "HIDE or SHOW, a field, then its items.");
        if (!hide && n == 3 && same_word(w[2], "ALL")) {
            unsigned i, kept = 0;
            for (i = 0; i < def.nhide; i++)
                if (def.hide[i].field != (unsigned)field)
                    def.hide[kept++] = def.hide[i];
            def.nhide = kept;
        } else {
            for (k = 2; k < n; k++) {
                owf_pivot_page item;
                item_of(core, &def, (unsigned)field, w[k], &item);
                if (!owf_pivot_set_hidden(&def, &item, hide))
                    return why(out, size, OWF_ERR_UNSUPPORTED, "A pivot table hides up to 32 items.");
            }
        }
    } else if (same_word(w[0], "GROUP")) {
        /* GROUP field YEARS | QUARTERS | MONTHS | NONE | GROUP field start end step */
        owf_pivot_group g;
        memset(&g, 0, sizeof g);
        g.field = (unsigned)field;
        if (n == 3 && same_word(w[2], "YEARS"))
            g.by = OWF_PIVOT_GROUP_YEARS;
        else if (n == 3 && same_word(w[2], "QUARTERS"))
            g.by = OWF_PIVOT_GROUP_QUARTERS;
        else if (n == 3 && same_word(w[2], "MONTHS"))
            g.by = OWF_PIVOT_GROUP_MONTHS;
        else if (n == 3 && same_word(w[2], "NONE"))
            g.by = OWF_PIVOT_GROUP_NONE;
        else if (n == 5 && number_word(w[2], &g.start) && number_word(w[3], &g.end) && number_word(w[4], &g.step))
            g.by = OWF_PIVOT_GROUP_RANGE;
        else
            return why(out, size, OWF_ERR_FORMAT, "GROUP field YEARS, QUARTERS, MONTHS or NONE, or start end step.");
        if (g.by == OWF_PIVOT_GROUP_RANGE && (g.step <= 0 || g.end < g.start))
            return why(out, size, OWF_ERR_FORMAT, "A range group's step is above 0 and its end not below its start.");
        if (g.by != OWF_PIVOT_GROUP_NONE)
            for (k = 0; k < (int)def.nnamed; k++)
                if (def.named[k].field == (unsigned)field)
                    return why(out, size, OWF_ERR_UNSUPPORTED,
                               "The field has groups made by hand: UNNAME their items first.");
        if (!owf_pivot_set_group(&def, &g))
            return why(out, size, OWF_ERR_UNSUPPORTED, "A pivot table groups up to 8 fields.");
    } else if (same_word(w[0], "NAME") || same_word(w[0], "UNNAME")) {
        /* NAME field group item [item...] | UNNAME field item [item...] */
        int name = same_word(w[0], "NAME"), first = name ? 3 : 2;
        if (n <= first)
            return why(out, size, OWF_ERR_FORMAT, "NAME field group items, or UNNAME field items.");
        if (name && (!w[2][0] || strlen(w[2]) >= OWF_PIVOT_NAME))
            return why(out, size, OWF_ERR_FORMAT, "A group's name is 1 to 47 bytes.");
        if (name && owf_pivot_group_of(&def, (unsigned)field))
            return why(out, size, OWF_ERR_UNSUPPORTED, "The field is grouped: GROUP field NONE first.");
        for (k = first; k < n; k++) {
            owf_pivot_page item;
            item_of(core, &def, (unsigned)field, w[k], &item);
            if (!owf_pivot_set_named(&def, &item, name ? w[2] : NULL))
                return why(out, size, OWF_ERR_UNSUPPORTED, "A pivot table has up to 8 groups of 24 items made by hand.");
        }
    } else if (same_word(w[0], "SHOWAS")) {
        /* SHOWAS data NORMAL | ROW | COLUMN | TOTAL | INDEX: data is the data field's place (1 the first) or
           its source field */
        int show = -1, at = -1;
        char *end;
        unsigned long place = strtoul(w[1], &end, 10);
        if (n == 3)
            show = same_word(w[2], "NORMAL") ? OWF_PIVOT_SHOW_NORMAL
                   : same_word(w[2], "ROW") ? OWF_PIVOT_SHOW_ROW
                   : same_word(w[2], "COLUMN") ? OWF_PIVOT_SHOW_COLUMN
                   : same_word(w[2], "TOTAL") ? OWF_PIVOT_SHOW_TOTAL
                   : same_word(w[2], "INDEX") ? OWF_PIVOT_SHOW_INDEX : -1;
        if (show < 0)
            return why(out, size, OWF_ERR_FORMAT, "SHOWAS data field, then NORMAL, ROW, COLUMN, TOTAL or INDEX.");
        if (!*end && place >= 1 && place <= def.ndata)
            at = (int)place - 1;
        else if ((field = oc_core_pivot_field(core, &def, w[1])) >= 0)
            for (k = 0; k < (int)def.ndata && at < 0; k++)
                if (def.data[k].field == (unsigned)field)
                    at = k;
        if (at < 0)
            return why(out, size, OWF_ERR_FORMAT, "That is not one of the pivot table's data fields.");
        def.data[at].show = show;
    } else {
        return why(out, size, OWF_ERR_FORMAT, "SORT, HIDE, SHOW, GROUP, NAME, UNNAME or SHOWAS, then a field.");
    }
    k = oc_core_pivot_set_layout(core, &def);
    if (k == OWF_ERR_UNSUPPORTED)
        return why(out, size, k, "The pivot table would be too large.");
    if (k == OWF_ERR_FORMAT)
        return why(out, size, k, "The pivot table's source or fields no longer fit (Refresh it first).");
    return k;
}

/*
 * GFOpenCalc's portable core: the active sheet's pivot table changed by a
 * line of words, as ARexx's PIVOTSET and the layout window give them: a field's
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
    if (name[0] == '#') {                          /* "#2": the number only, never a heading */
        number = strtoul(name + 1, &end, 10);
        return name[1] && !*end && number >= 1 && number <= n ? (int)number - 1 : -1;
    }
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

/* The words applied to def, which is not laid out. */
static int apply_words(const oc_core *core, owf_pivotdef *defp, const char *text, char *out, size_t size)
{
    char w[WORDS][WORD];
    int n, field = -1, k;
#define def (*defp)
    n = split_words(text, w);
    if (n < 0)
        return why(out, size, OWF_ERR_FORMAT, "Too many words, or one over 127 bytes: up to 30 items at a time.");
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
#undef def
    return OWF_OK;
}

int oc_core_pivot_words(const oc_core *core, owf_pivotdef *def, const char *text, char *out, size_t size)
{
    owf_pivotdef work;
    int rc;
    if (out && size)
        out[0] = 0;
    if (!core || !def || !text)
        return OWF_ERR_FORMAT;
    work = *def;                               /* all or nothing */
    rc = apply_words(core, &work, text, out, size);
    if (rc == OWF_OK)
        *def = work;
    return rc;
}

int oc_core_pivot_command(oc_core *core, const char *text, char *out, size_t size)
{
    owf_pivotdef def;
    int k;
    if (out && size)
        out[0] = 0;
    if (!core || !text)
        return OWF_ERR_FORMAT;
    if (!oc_core_pivot_get(core, &def))
        return why(out, size, OWF_ERR_UNSUPPORTED, "The active sheet has no pivot table.");
    if ((k = oc_core_pivot_words(core, &def, text, out, size)) != OWF_OK)
        return k;
    k = oc_core_pivot_set_layout(core, &def);
    if (k == OWF_ERR_UNSUPPORTED)
        return why(out, size, k, "The pivot table would be too large.");
    if (k == OWF_ERR_FORMAT)
        return why(out, size, k, "The pivot table's source or fields no longer fit (Refresh it first).");
    return k;
}

/* A word as split_words reads it back: in "double quotes" (with "" for ") when it has a space, a quote or
   nothing. Returns the length it would have; out is cut, ended, at size. */
static size_t put_word(char *out, size_t size, size_t at, const char *word)
{
    int quote = !*word || strpbrk(word, " \t\"") != NULL;
    const char *p;
#define PUT(c) do { if (at + 1 < size) out[at] = (c); at++; } while (0)
    if (at)
        PUT(' ');
    if (quote)
        PUT('"');
    for (p = word; *p; p++) {
        if (*p == '"')
            PUT('"');
        PUT(*p);
    }
    if (quote)
        PUT('"');
#undef PUT
    if (size)
        out[at < size ? at : size - 1] = 0;
    return at;
}

static void number_text(double v, char *out, size_t size)
{
    snprintf(out, size, "%.15g", v);
}

int oc_core_pivot_field_words(const owf_pivotdef *def, unsigned field, int what, char *out, size_t size)
{
    size_t at = 0;
    unsigned i;
    if (!out || !size)
        return 0;
    out[0] = 0;
    if (!def)
        return 0;
    if (what == OC_PIVOT_WORDS_GROUP) {
        const owf_pivot_group *g = owf_pivot_group_of(def, field);
        char a[40], b[40], c[40];
        if (!g)
            return 1;
        switch (g->by) {
        case OWF_PIVOT_GROUP_YEARS: at = put_word(out, size, 0, "YEARS"); break;
        case OWF_PIVOT_GROUP_QUARTERS: at = put_word(out, size, 0, "QUARTERS"); break;
        case OWF_PIVOT_GROUP_MONTHS: at = put_word(out, size, 0, "MONTHS"); break;
        case OWF_PIVOT_GROUP_RANGE:
            number_text(g->start, a, sizeof a);
            number_text(g->end, b, sizeof b);
            number_text(g->step, c, sizeof c);
            at = put_word(out, size, put_word(out, size, put_word(out, size, 0, a), b), c);
            break;
        default: break;
        }
    } else if (what == OC_PIVOT_WORDS_HIDDEN) {
        for (i = 0; i < def->nhide; i++) {
            char item[OWF_PIVOT_ITEM + 8];
            if (def->hide[i].field != field)
                continue;
            if (def->hide[i].kind == OWF_PIVOT_BLANK)
                snprintf(item, sizeof item, "(blank)");
            else
                owf_pivot_item_text(&def->hide[i], item, sizeof item);
            at = put_word(out, size, at, item);
        }
    } else
        return 0;
    return at < size;
}

int oc_core_pivot_field_choices(const oc_core *core, owf_pivotdef *def, unsigned field, int sort, int show,
                                const char *group, const char *hidden, char *out, size_t size)
{
    static const char *const sorts[] = { "SOURCE", "ASCENDING", "DESCENDING" };
    static const char *const shows[] = { "NORMAL", "ROW", "COLUMN", "TOTAL", "INDEX" };
    owf_pivotdef work;
    char line[WORDS * WORD + 64], had[WORDS * WORD];
    unsigned k;
    int rc;
    if (out && size)
        out[0] = 0;
    if (!core || !def || field > def->source.last_col - def->source.first_col)
        return OWF_ERR_FORMAT;
    work = *def;
#define RUN(...) do { snprintf(line, sizeof line, __VA_ARGS__); \
                      if ((rc = apply_words(core, &work, line, out, size)) != OWF_OK) return rc; } while (0)
    if (sort >= OWF_PIVOT_SORT_SOURCE && sort <= OWF_PIVOT_SORT_DESCENDING && sort != owf_pivot_sort_of(&work, field))
        RUN("SORT #%u %s", field + 1, sorts[sort]);
    if (group) {
        oc_core_pivot_field_words(&work, field, OC_PIVOT_WORDS_GROUP, had, sizeof had);
        while (*group == ' ')
            group++;
        if (strcmp(had, group))
            RUN("GROUP #%u %s", field + 1, *group ? group : "NONE");
    }
    if (hidden) {
        oc_core_pivot_field_words(&work, field, OC_PIVOT_WORDS_HIDDEN, had, sizeof had);
        while (*hidden == ' ')
            hidden++;
        if (strcmp(had, hidden)) {
            RUN("SHOW #%u ALL", field + 1);
            if (*hidden)
                RUN("HIDE #%u %s", field + 1, hidden);
        }
    }
    if (show >= OWF_PIVOT_SHOW_NORMAL && show <= OWF_PIVOT_SHOW_LAST)
        for (k = 0; k < work.ndata; k++)
            if (work.data[k].field == field && work.data[k].show != show)
                RUN("SHOWAS %u %s", k + 1, shows[show]);
#undef RUN
    *def = work;
    return OWF_OK;
}

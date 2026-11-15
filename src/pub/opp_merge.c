/* GFOpenPublisher's mail merge on the shared page model (include/openpublisher_merge.h).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "openpublisher_merge.h"

#include <stdio.h>
#include <string.h>

static char *text_copy(const char *s, size_t n)
{
    char *c = (char *)owf_malloc(n + 1);
    if (!c) return NULL;
    memcpy(c, s, n);
    c[n] = 0;
    return c;
}

static int is_merge(const owf_run *r)
{
    return r->kind == OWF_RUN_FIELD && r->field == OWF_FIELD_MERGE;
}

/* What a field shows: the record's value, or its column's name in guillemets (owf_malloc'd) */
static char *shown(const ow_merge_table *t, int record, const char *column)
{
    const char *v = t && record >= 0 ? ow_merge_value(t, record, column ? column : "") : NULL;
    char *out;
    size_t n;
    if (v) return text_copy(v, strlen(v));
    n = (column ? strlen(column) : 0) + 5;
    if (!(out = (char *)owf_malloc(n))) return NULL;
    snprintf(out, n, "\xC2\xAB%s\xC2\xBB", column ? column : "");
    return out;
}

/* Calls fn on every object's text on the pages (and the masters when masters is set); stops at its first nonzero */
typedef int (*story_fn)(owf_story *story, void *ud);

static int each_story(owf_deck *deck, int masters, story_fn fn, void *ud)
{
    int s, o, rc;
    for (s = 0; s < deck->nslides; ++s)
        for (o = 0; o < deck->slides[s].nobjs; ++o)
            if ((rc = fn(&deck->slides[s].objs[o].text, ud)) != 0) return rc;
    if (masters)
        for (s = 0; s < deck->nmasters; ++s)
            for (o = 0; o < deck->masters[s].nobjs; ++o)
                if ((rc = fn(&deck->masters[s].objs[o].text, ud)) != 0) return rc;
    return 0;
}

int opp_merge_insert_field(owf_obj *obj, int para, const char *column, const owf_charfmt *fmt)
{
    owf_para *p;
    owf_charfmt cf;
    owf_run *r;
    char *href, *text;
    if (!obj || !column || !*column) return OWF_ERR_FORMAT;
    if (para < -1 || para >= obj->text.nparas) return OWF_ERR_FORMAT;
    if (fmt) cf = *fmt;
    else owf_charfmt_init(&cf);
    if (!obj->text.nparas) {
        owf_parafmt pf;
        owf_parafmt_init(&pf);
        if (!owf_story_add(&obj->text, &pf)) return OWF_ERR_MEMORY;
    }
    p = &obj->text.paras[para < 0 ? obj->text.nparas - 1 : para];
    if (!(href = text_copy(column, strlen(column)))) return OWF_ERR_MEMORY;
    if (!(text = shown(NULL, -1, column))) { owf_free(href); return OWF_ERR_MEMORY; }
    if (owf_para_add_special(p, &cf, OWF_RUN_FIELD, OWF_FIELD_MERGE) < 0) {
        owf_free(href);
        owf_free(text);
        return OWF_ERR_MEMORY;
    }
    r = &p->runs[p->nruns - 1];
    r->href = href;
    r->text = text;
    return OWF_OK;
}

typedef struct {
    const ow_merge_table *t;
    int record, fields, missing, set;
} merge_walk;

static int count_story(owf_story *story, void *ud)
{
    merge_walk *w = (merge_walk *)ud;
    int i, j;
    for (i = 0; i < story->nparas; ++i)
        for (j = 0; j < story->paras[i].nruns; ++j) {
            const owf_run *r = &story->paras[i].runs[j];
            if (!is_merge(r)) continue;
            ++w->fields;
            if (w->t && ow_merge_column(w->t, r->href ? r->href : "") < 0) ++w->missing;
        }
    return 0;
}

int opp_merge_fields(const owf_deck *deck, const ow_merge_table *t, int *missing)
{
    merge_walk w;
    memset(&w, 0, sizeof w);
    w.t = t;
    if (deck) each_story((owf_deck *)deck, 1, count_story, &w);   /* count_story changes nothing */
    if (missing) *missing = w.missing;
    return w.fields;
}

static int show_story(owf_story *story, void *ud)
{
    merge_walk *w = (merge_walk *)ud;
    int i, j;
    for (i = 0; i < story->nparas; ++i)
        for (j = 0; j < story->paras[i].nruns; ++j) {
            owf_run *r = &story->paras[i].runs[j];
            char *v;
            if (!is_merge(r)) continue;
            if (!(v = shown(w->t, w->record, r->href))) return OWF_ERR_MEMORY;
            owf_free(r->text);
            r->text = v;
            ++w->set;
        }
    return 0;
}

int opp_merge_show(owf_deck *deck, const ow_merge_table *t, int record, int *set)
{
    merge_walk w;
    int rc;
    if (set) *set = 0;
    if (!deck) return OWF_ERR_FORMAT;
    if (record < -1 || (record >= 0 && (!t || record >= t->nrows))) return OWF_ERR_UNSUPPORTED;
    memset(&w, 0, sizeof w);
    w.t = t;
    w.record = record;
    rc = each_story(deck, 1, show_story, &w);
    if (set) *set = w.set;
    return rc ? rc : OWF_OK;
}

/* A paragraph's merge fields made plain text of record's values (a line break in a value a line break run);
 * 0, or OWF_ERR_MEMORY with the paragraph as it was */
static int para_final(owf_para *p, const ow_merge_table *t, int record)
{
    owf_run *runs;
    unsigned char *made;                             /* runs whose text is made here */
    int j, o, k, cap, any = 0;
    for (j = 0; j < p->nruns; ++j) any |= is_merge(&p->runs[j]);
    if (!any) return 0;
    for (j = 0, cap = 0; j < p->nruns; ++j) {        /* room: a value's lines each take two runs */
        const char *v;
        ++cap;
        if (!is_merge(&p->runs[j]) || !t) continue;
        v = ow_merge_value(t, record, p->runs[j].href ? p->runs[j].href : "");
        while (v && (v = strchr(v, '\n')) != NULL) { cap += 2; ++v; }
    }
    runs = (owf_run *)owf_calloc((size_t)cap, sizeof *runs);
    made = (unsigned char *)owf_calloc((size_t)cap, 1);
    if (!runs || !made) { owf_free(runs); owf_free(made); return OWF_ERR_MEMORY; }
    for (j = 0, o = 0; j < p->nruns; ++j) {
        const owf_run *r = &p->runs[j];
        char *v;
        const char *s, *nl;
        if (!is_merge(r)) { runs[o++] = *r; continue; }
        if (!(v = shown(t, record, r->href))) goto fail;
        for (s = v;; s = nl + 1) {
            size_t n = (nl = strchr(s, '\n')) != NULL ? (size_t)(nl - s) : strlen(s);
            if (n && s[n - 1] == '\r') --n;          /* a CRLF in a quoted cell */
            memset(&runs[o], 0, sizeof runs[o]);
            runs[o].kind = OWF_RUN_TEXT;
            runs[o].fmt = r->fmt;
            runs[o].revision = r->revision;
            if (n && !(runs[o].text = text_copy(s, n))) { owf_free(v); goto fail; }
            made[o++] = 1;
            if (!nl) break;
            memset(&runs[o], 0, sizeof runs[o]);
            runs[o].kind = OWF_RUN_LINEBREAK;
            runs[o].fmt = r->fmt;
            ++o;
        }
        owf_free(v);
    }
    for (j = 0; j < p->nruns; ++j)                   /* only now: the old fields' own text goes */
        if (is_merge(&p->runs[j])) { owf_free(p->runs[j].text); owf_free(p->runs[j].href); }
    owf_free(p->runs);
    owf_free(made);
    p->runs = runs;
    p->nruns = o;
    p->capruns = cap;
    return 0;
fail:
    for (k = 0; k < o; ++k) if (made[k]) owf_free(runs[k].text);
    owf_free(made);
    owf_free(runs);
    return OWF_ERR_MEMORY;
}

static int obj_final(owf_obj *obj, const ow_merge_table *t, int record)
{
    int i;
    for (i = 0; i < obj->text.nparas; ++i)
        if (para_final(&obj->text.paras[i], t, record)) return OWF_ERR_MEMORY;
    return 0;
}

int opp_merge_pages(owf_deck *deck, const ow_merge_table *t, int first, int last)
{
    int pages, base = 0, records, r, i, o;
    if (!deck || !t || t->nrows <= 0 || deck->nslides <= 0) return OWF_ERR_UNSUPPORTED;
    if (first == -1) first = 0;
    if (last == -1) last = t->nrows - 1;
    if (first < 0 || last < first || last >= t->nrows) return OWF_ERR_UNSUPPORTED;
    pages = deck->nslides;
    records = last - first + 1;
    for (i = 0; i < pages; ++i)                      /* each record's flows get numbers of their own */
        for (o = 0; o < deck->slides[i].nobjs; ++o)
            if (deck->slides[i].objs[o].flow_story >= base) base = deck->slides[i].objs[o].flow_story + 1;
    if (records > (0x7FFFFFFF - pages) / pages || (base && records > 0x7FFFFFFF / base - 1))
        return OWF_ERR_UNSUPPORTED;
    for (r = 0; r < records; ++r)
        for (i = 0; i < pages; ++i) {
            owf_slide *copy = owf_deck_duplicate_slide(deck, i, -1);
            if (!copy) goto undo;
            for (o = 0; o < copy->nobjs; ++o) {
                owf_obj *obj = &copy->objs[o];
                if (obj_final(obj, t, first + r)) goto undo;
                if (obj->flow_story >= 0) obj->flow_story += (r + 1) * base;
            }
        }
    while (deck->nslides > records * pages)          /* the pages merged from go */
        owf_deck_delete_slide(deck, 0);
    for (i = 0; i < deck->nslides; ++i)
        for (o = 0; o < deck->slides[i].nobjs; ++o)
            if (deck->slides[i].objs[o].flow_story >= 0) deck->slides[i].objs[o].flow_story -= base;
    return OWF_OK;
undo:
    while (deck->nslides > pages) owf_deck_delete_slide(deck, deck->nslides - 1);
    return OWF_ERR_MEMORY;
}

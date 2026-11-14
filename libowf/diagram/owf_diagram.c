/*
 * owf_diagram.c: GFOpenDiag's diagram model (owf_diagram.h): pages, cells, their attributes and
 * styles, and numbers in hundredths. Integer arithmetic only.
 * The model is mxGraph's as its published API documentation describes it (mxGraphModel, mxCell,
 * mxGeometry; Apache 2.0, by name only). No other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <stdio.h>
#include <string.h>

#define MAX_REMOVE_PASSES 4096

static char *dup_s(const char *s)
{
    size_t n;
    char *p;
    if (!s)
        return NULL;
    n = strlen(s);
    p = owf_malloc(n + 1);
    if (p)
        memcpy(p, s, n + 1);
    return p;
}

/* ---- Attributes ---- */

const char *owf_dattrs_get(const owf_dattrs *a, const char *name)
{
    int i;
    for (i = 0; i < a->n; i++)
        if (!strcmp(a->v[i].name, name))
            return a->v[i].value;
    return NULL;
}

int owf_dattrs_set(owf_dattrs *a, const char *name, const char *value)
{
    int i;
    char *v;
    for (i = 0; i < a->n; i++)
        if (!strcmp(a->v[i].name, name))
            break;
    if (!value) {
        if (i < a->n) {
            owf_free(a->v[i].name);
            owf_free(a->v[i].value);
            memmove(a->v + i, a->v + i + 1, (size_t)(a->n - i - 1) * sizeof *a->v);
            a->n--;
        }
        return OWF_OK;
    }
    v = dup_s(value);
    if (!v)
        return OWF_ERR_MEMORY;
    if (i < a->n) {
        owf_free(a->v[i].value);
        a->v[i].value = v;
        return OWF_OK;
    }
    if (a->n == a->cap) {
        int cap = a->cap ? a->cap * 2 : 4;
        owf_dattr *grown = owf_realloc(a->v, (size_t)cap * sizeof *grown);
        if (!grown) {
            owf_free(v);
            return OWF_ERR_MEMORY;
        }
        a->v = grown;
        a->cap = cap;
    }
    a->v[a->n].name = dup_s(name);
    if (!a->v[a->n].name) {
        owf_free(v);
        return OWF_ERR_MEMORY;
    }
    a->v[a->n].value = v;
    a->n++;
    return OWF_OK;
}

void owf_dattrs_free(owf_dattrs *a)
{
    int i;
    for (i = 0; i < a->n; i++) {
        owf_free(a->v[i].name);
        owf_free(a->v[i].value);
    }
    owf_free(a->v);
    a->v = NULL;
    a->n = a->cap = 0;
}

/* ---- Cells and pages ---- */

static void cell_free(owf_dcell *c)
{
    owf_free(c->id);
    owf_free(c->parent);
    owf_free(c->value);
    owf_free(c->style);
    owf_free(c->source);
    owf_free(c->target);
    owf_dattrs_free(&c->data);
    owf_dattrs_free(&c->extra);
    owf_free(c->geo.points);
    owf_dattrs_free(&c->geo.extra);
    memset(c, 0, sizeof *c);
}

void owf_dpage_free(owf_dpage *p)
{
    int i;
    for (i = 0; i < p->ncells; i++)
        cell_free(&p->cells[i]);
    owf_free(p->cells);
    owf_free(p->id);
    owf_free(p->name);
    owf_dattrs_free(&p->model);
    memset(p, 0, sizeof *p);
}

void owf_diagram_init(owf_diagram *d)
{
    memset(d, 0, sizeof *d);
}

void owf_diagram_free(owf_diagram *d)
{
    int i;
    for (i = 0; i < d->npages; i++)
        owf_dpage_free(&d->pages[i]);
    owf_free(d->pages);
    owf_dattrs_free(&d->file);
    memset(d, 0, sizeof *d);
}

owf_dpage *owf_diagram_bare_page(owf_diagram *d)
{
    owf_dpage *p;
    if (d->npages == d->cappages) {
        int cap = d->cappages ? d->cappages * 2 : 2;
        owf_dpage *grown = owf_realloc(d->pages, (size_t)cap * sizeof *grown);
        if (!grown)
            return NULL;
        d->pages = grown;
        d->cappages = cap;
    }
    p = &d->pages[d->npages++];
    memset(p, 0, sizeof *p);
    return p;
}

owf_dpage *owf_diagram_add_page(owf_diagram *d, const char *id, const char *name)
{
    owf_dpage *p = owf_diagram_bare_page(d);
    if (!p)
        return NULL;
    if ((id && !(p->id = dup_s(id))) || (name && !(p->name = dup_s(name))) ||
        !owf_dpage_add(p, "0", NULL, OWF_DCELL_PLAIN) || !owf_dpage_add(p, "1", "0", OWF_DCELL_PLAIN)) {
        owf_dpage_free(p);
        d->npages--;
        return NULL;
    }
    return p;
}

static int copy_attrs(owf_dattrs *dst, const owf_dattrs *src)
{
    int i;
    for (i = 0; i < src->n; i++)
        if (owf_dattrs_set(dst, src->v[i].name, src->v[i].value) != OWF_OK)
            return OWF_ERR_MEMORY;
    return OWF_OK;
}

static int copy_cell(owf_dcell *d, const owf_dcell *s)
{
    memset(d, 0, sizeof *d);
    d->kind = s->kind;
    d->wrapper = s->wrapper;
    d->geo.flags = s->geo.flags;
    d->geo.x = s->geo.x;
    d->geo.y = s->geo.y;
    d->geo.width = s->geo.width;
    d->geo.height = s->geo.height;
    d->geo.source_point = s->geo.source_point;
    d->geo.target_point = s->geo.target_point;
    d->geo.offset = s->geo.offset;
    memcpy(d->geo.alternate, s->geo.alternate, sizeof d->geo.alternate);
    if ((s->id && !(d->id = dup_s(s->id))) || (s->parent && !(d->parent = dup_s(s->parent))) ||
        (s->value && !(d->value = dup_s(s->value))) || (s->style && !(d->style = dup_s(s->style))) ||
        (s->source && !(d->source = dup_s(s->source))) || (s->target && !(d->target = dup_s(s->target))) ||
        copy_attrs(&d->data, &s->data) || copy_attrs(&d->extra, &s->extra) ||
        copy_attrs(&d->geo.extra, &s->geo.extra))
        return OWF_ERR_MEMORY;
    if (s->geo.npoints) {
        d->geo.points = owf_malloc((size_t)s->geo.npoints * sizeof *d->geo.points);
        if (!d->geo.points)
            return OWF_ERR_MEMORY;
        memcpy(d->geo.points, s->geo.points, (size_t)s->geo.npoints * sizeof *d->geo.points);
        d->geo.npoints = d->geo.cappoints = s->geo.npoints;
    }
    return OWF_OK;
}

int owf_dpage_copy(owf_dpage *dst, const owf_dpage *src)
{
    int i;
    owf_dpage_free(dst);
    dst->compressed = src->compressed;
    if ((src->id && !(dst->id = dup_s(src->id))) || (src->name && !(dst->name = dup_s(src->name))) ||
        copy_attrs(&dst->model, &src->model))
        goto fail;
    if (src->ncells) {
        dst->cells = owf_calloc((size_t)src->ncells, sizeof *dst->cells);
        if (!dst->cells)
            goto fail;
        dst->capcells = src->ncells;
        for (i = 0; i < src->ncells; i++) {
            dst->ncells = i + 1;
            if (copy_cell(&dst->cells[i], &src->cells[i]) != OWF_OK)
                goto fail;
        }
    }
    return OWF_OK;
fail:
    owf_dpage_free(dst);
    return OWF_ERR_MEMORY;
}

int owf_dpage_index(const owf_dpage *p, const char *id)
{
    int i;
    if (!id)
        return -1;
    for (i = 0; i < p->ncells; i++)
        if (!strcmp(p->cells[i].id, id))
            return i;
    return -1;
}

owf_dcell *owf_dpage_find(const owf_dpage *p, const char *id)
{
    int i = owf_dpage_index(p, id);
    return i < 0 ? NULL : &p->cells[i];
}

void owf_dpage_new_id(const owf_dpage *p, char *buf, size_t size)
{
    unsigned long n = (unsigned long)p->ncells;
    do
        snprintf(buf, size, "%lu", n++);
    while (owf_dpage_index(p, buf) >= 0);
}

owf_dcell *owf_dpage_add(owf_dpage *p, const char *id, const char *parent, int kind)
{
    char fresh[24];
    owf_dcell *c;
    if (!id) {
        owf_dpage_new_id(p, fresh, sizeof fresh);
        id = fresh;
    } else if (owf_dpage_index(p, id) >= 0)
        return NULL;
    if (p->ncells == p->capcells) {
        int cap = p->capcells ? p->capcells * 2 : 8;
        owf_dcell *grown = owf_realloc(p->cells, (size_t)cap * sizeof *grown);
        if (!grown)
            return NULL;
        p->cells = grown;
        p->capcells = cap;
    }
    c = &p->cells[p->ncells];
    memset(c, 0, sizeof *c);
    c->kind = kind;
    c->id = dup_s(id);
    if (!c->id || (parent && !(c->parent = dup_s(parent)))) {
        cell_free(c);
        return NULL;
    }
    p->ncells++;
    return c;
}

/* 1 when the cell is to go: marked, inside a marked cell, or an edge joined to one. */
static int doomed(const owf_dpage *p, const unsigned char *mark, int i)
{
    const owf_dcell *c = &p->cells[i];
    int j;
    if (mark[i])
        return 0;
    j = owf_dpage_index(p, c->parent);
    if (j >= 0 && mark[j])
        return 1;
    if (c->kind == OWF_DCELL_EDGE) {
        j = owf_dpage_index(p, c->source);
        if (j >= 0 && mark[j])
            return 1;
        j = owf_dpage_index(p, c->target);
        if (j >= 0 && mark[j])
            return 1;
    }
    return 0;
}

int owf_dpage_remove(owf_dpage *p, const char *id)
{
    int i = owf_dpage_index(p, id), j, n = 0, changed, passes = 0;
    unsigned char *mark;
    if (i < 0)
        return 0;
    mark = owf_calloc((size_t)p->ncells, 1);
    if (!mark)
        return 0;
    mark[i] = 1;
    do {
        changed = 0;
        for (j = 0; j < p->ncells; j++)
            if (doomed(p, mark, j)) {
                mark[j] = 1;
                changed = 1;
            }
    } while (changed && ++passes < MAX_REMOVE_PASSES);
    for (i = j = 0; i < p->ncells; i++) {
        if (mark[i]) {
            cell_free(&p->cells[i]);
            n++;
        } else
            p->cells[j++] = p->cells[i];
    }
    p->ncells = j;
    owf_free(mark);
    return n;
}

int owf_dcell_set(char **field, const char *value)
{
    char *v = NULL;
    if (value && !(v = dup_s(value)))
        return OWF_ERR_MEMORY;
    owf_free(*field);
    *field = v;
    return OWF_OK;
}

int owf_dgeo_add_point(owf_dgeometry *g, owf_dnum x, owf_dnum y)
{
    if (g->npoints == g->cappoints) {
        int cap = g->cappoints ? g->cappoints * 2 : 4;
        owf_dpoint *grown = owf_realloc(g->points, (size_t)cap * sizeof *grown);
        if (!grown)
            return OWF_ERR_MEMORY;
        g->points = grown;
        g->cappoints = cap;
    }
    g->points[g->npoints].x = x;
    g->points[g->npoints].y = y;
    g->npoints++;
    g->flags |= OWF_DGEO_POINTS;
    return OWF_OK;
}

/* ---- Styles: "key=value;key2=value2;word;" ---- */

/* The next entry from *s: its key (key_n bytes) and value (NULL for a bare word). */
static int style_next(const char **s, const char **key, size_t *key_n, const char **val, size_t *val_n)
{
    const char *p = *s, *end, *eq;
    while (*p == ';')
        p++;
    if (!*p)
        return 0;
    end = strchr(p, ';');
    if (!end)
        end = p + strlen(p);
    eq = memchr(p, '=', (size_t)(end - p));
    *key = p;
    if (eq) {
        *key_n = (size_t)(eq - p);
        *val = eq + 1;
        *val_n = (size_t)(end - eq - 1);
    } else {
        *key_n = (size_t)(end - p);
        *val = NULL;
        *val_n = 0;
    }
    *s = end;
    return 1;
}

int owf_dstyle_get(const char *style, const char *key, char *buf, size_t size)
{
    const char *k, *v;
    size_t kn, vn, klen = strlen(key);
    if (size)
        buf[0] = 0;
    if (!style)
        return 0;
    while (style_next(&style, &k, &kn, &v, &vn))
        if (kn == klen && !memcmp(k, key, kn)) {
            if (size) {
                if (vn >= size)
                    vn = size - 1;
                if (v)
                    memcpy(buf, v, vn);
                buf[v ? vn : 0] = 0;
            }
            return 1;
        }
    return 0;
}

int owf_dstyle_set(char **style, const char *key, const char *value)
{
    const char *s = *style ? *style : "", *k, *v;
    size_t kn, vn, klen = strlen(key), cap = strlen(s) + klen + (value ? strlen(value) : 0) + 4, at = 0;
    int done = 0;
    char *out = owf_malloc(cap);
    if (!out)
        return OWF_ERR_MEMORY;
    while (style_next(&s, &k, &kn, &v, &vn)) {
        if (kn == klen && !memcmp(k, key, kn)) {
            if (done || !value)
                continue;
            done = 1;
            memcpy(out + at, key, klen);
            at += klen;
            out[at++] = '=';
            memcpy(out + at, value, strlen(value));
            at += strlen(value);
        } else {
            size_t whole = kn + (v ? vn + 1 : 0);
            memcpy(out + at, k, whole);
            at += whole;
        }
        out[at++] = ';';
    }
    if (!done && value) {
        memcpy(out + at, key, klen);
        at += klen;
        out[at++] = '=';
        memcpy(out + at, value, strlen(value));
        at += strlen(value);
        out[at++] = ';';
    }
    out[at] = 0;
    owf_free(*style);
    *style = out;
    return OWF_OK;
}

/* ---- Numbers ---- */

#define DNUM_MAX 200000000L              /* 2 million pixels: bigger is cut to this */
#define MILLI_MAX 2000000000L            /* the same in thousandths, which a 32-bit long holds */

owf_dnum owf_dnum_parse(const char *s)
{
    long whole = 0, frac = 0;
    int neg = 0, digits = 0, fdigits = 0, exp = 0, eneg = 0;
    if (!s)
        return 0;
    while (*s == ' ')
        s++;
    if (*s == '-' || *s == '+')
        neg = (*s++ == '-');
    for (; *s >= '0' && *s <= '9'; s++, digits++)
        if (whole <= MILLI_MAX / 10)
            whole = whole * 10 + (*s - '0');
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++, digits++)
            if (fdigits < 3) {
                frac = frac * 10 + (*s - '0');
                fdigits++;
            }
    if (!digits)
        return 0;
    while (fdigits < 3) {
        frac *= 10;
        fdigits++;
    }
    /* thousandths now; an exponent moves the point */
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-' || *s == '+')
            eneg = (*s++ == '-');
        for (; *s >= '0' && *s <= '9'; s++)
            if (exp < 100)
                exp = exp * 10 + (*s - '0');
    }
    {
        long v = whole > MILLI_MAX / 1000 ? MILLI_MAX : whole * 1000 + frac;
        for (; exp > 0; exp--) {
            if (eneg)
                v /= 10;
            else
                v = v > MILLI_MAX / 10 ? MILLI_MAX : v * 10;
        }
        v = (v + 5) / 10;                /* to hundredths, half away from zero */
        if (v > DNUM_MAX)
            v = DNUM_MAX;
        return neg ? -v : v;
    }
}

void owf_dnum_format(owf_dnum n, char *buf, size_t size)
{
    unsigned long a = n < 0 ? (unsigned long)(-(n + 1)) + 1 : (unsigned long)n;
    unsigned long whole = a / 100, frac = a % 100;
    const char *sign = n < 0 ? "-" : "";
    if (!frac)
        snprintf(buf, size, "%s%lu", sign, whole);
    else if (frac % 10 == 0)
        snprintf(buf, size, "%s%lu.%lu", sign, whole, frac / 10);
    else
        snprintf(buf, size, "%s%lu.%02lu", sign, whole, frac);
}

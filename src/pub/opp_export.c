/* GFOpenPressmaster save: the suite's deck/page model back into libowf's
 * Publisher model (owf_pub), which owf_pub_write writes as a .pub. The
 * inverse of opp_import.c: pages and masters, frames in their order with
 * their groups, boxes, looks, text (one story for each text flow), tables
 * and pictures. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "openpressmaster_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU_TWIP 635L

static char *owf_copy(const char *s)
{
    size_t n;
    char *p;
    if (!s) return NULL;
    n = strlen(s);
    p = owf_malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

/* The text, its paragraphs' alignment and cells and its runs; a deck's own
 * paragraph extras (styles, bookmarks) have no place in a .pub yet. */
static int copy_story(owf_story *to, const owf_story *from)
{
    int i, j;
    for (i = 0; i < from->nparas; i++) {
        const owf_para *p = &from->paras[i];
        owf_parafmt fmt = p->fmt;
        owf_para *q = owf_story_add(to, &fmt);
        if (!q) return -1;
        q->table_id = p->table_id;
        q->table_row = p->table_row;
        q->table_col = p->table_col;
        q->table_cols = p->table_cols;
        for (j = 0; j < p->nruns; j++) {
            const owf_run *r = &p->runs[j];
            int rc = 0;
            if (r->kind == OWF_RUN_TEXT && r->text)
                rc = r->href ? owf_para_add_link_text(q, &r->fmt, r->text, strlen(r->text), r->href)
                             : owf_para_add_text(q, &r->fmt, r->text, strlen(r->text));
            else if (r->kind == OWF_RUN_TAB || r->kind == OWF_RUN_LINEBREAK)
                rc = owf_para_add_special(q, &r->fmt, r->kind, r->field);
            if (rc < 0) return -1;
        }
    }
    return 0;
}

static int copy_resources(owf_doc *to, const owf_doc *from)
{
    int i;
    to->base = from->base;
    for (i = 0; i < from->nfonts; i++)
        if (owf_doc_font(to, from->fonts[i].name, from->fonts[i].kind) != i) return -1;
    for (i = 0; i < from->nimages; i++) {
        const owf_image *im = &from->images[i];
        if (owf_doc_add_image(to, im->name, im->mime, im->data, im->length,
                              im->width, im->height, im->alt) != i) return -1;
    }
    return !from->title || owf_doc_set_title(to, from->title) == 0 ? 0 : -1;
}

/* [MS-ODRAW] 2.4.24's shape numbers for the deck's shapes. */
static int shape_number(const owf_obj *o)
{
    switch (o->kind) {
    case OWF_OBJ_TEXT: return 202;
    case OWF_OBJ_PICTURE: return 75;
    case OWF_OBJ_TABLE: case OWF_OBJ_GROUP: return -1;
    case OWF_OBJ_SHAPE:
        switch (o->shape) {
        case OWF_SHAPE_ROUNDRECT: return 2;
        case OWF_SHAPE_ELLIPSE: return 3;
        case OWF_SHAPE_DIAMOND: return 4;
        case OWF_SHAPE_TRIANGLE: return 5;
        case OWF_SHAPE_ARROW: return 13;
        case OWF_SHAPE_LINE: return 20;
        default: return 1;
        }
    default: return 1;
    }
}

static owf_pub_kind frame_kind(const owf_obj *o)
{
    switch (o->kind) {
    case OWF_OBJ_TEXT: return OWF_PUB_TEXT;
    case OWF_OBJ_PICTURE: return OWF_PUB_PICTURE;
    case OWF_OBJ_TABLE: return OWF_PUB_TABLE;
    case OWF_OBJ_SHAPE: return o->shape == OWF_SHAPE_LINE ? OWF_PUB_LINE : OWF_PUB_SHAPE;
    case OWF_OBJ_GROUP: return OWF_PUB_GROUP;
    default: return OWF_PUB_OTHER;
    }
}

typedef struct {
    owf_pub *pub;
    int *flow;                   /* flow_story value -> pub story; -1: none yet */
    int nflow;
} exporter;

static int new_story(exporter *x, const owf_story *text)
{
    owf_pub *pub = x->pub;
    if (pub->nstories == pub->capstories) {
        int cap = pub->capstories ? 2 * pub->capstories : 8;
        owf_story *grown = owf_realloc(pub->stories, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        pub->stories = grown;
        pub->capstories = cap;
    }
    memset(&pub->stories[pub->nstories], 0, sizeof pub->stories[0]);
    pub->nstories++;
    if (copy_story(&pub->stories[pub->nstories - 1], text) < 0) return -1;
    return pub->nstories - 1;
}

/* A text frame's story: frames with the same flow_story share one, its
 * text taken from the first of them (opp_import.c gives each frame a copy). */
static int story_of(exporter *x, const owf_obj *o)
{
    if (o->kind == OWF_OBJ_PICTURE || o->kind == OWF_OBJ_GROUP || (!o->text.nparas && o->flow_story < 0))
        return -1;
    if (o->flow_story >= 0 && o->flow_story < x->nflow) {
        if (x->flow[o->flow_story] < 0)
            x->flow[o->flow_story] = new_story(x, &o->text);
        return x->flow[o->flow_story];
    }
    return new_story(x, &o->text);
}

static void fill_frame(exporter *x, owf_pub_frame *f, const owf_obj *o, int index, int count, const owf_pub *pub)
{
    int k;
    memset(f, 0, sizeof *f);
    f->kind = frame_kind(o);
    f->parent = o->parent >= 0 && o->parent < index && o->parent < count ? o->parent : -1;
    f->x = o->x;
    f->y = o->y;
    f->width = o->width;
    f->height = o->height;
    f->left = (long)o->x * EMU_TWIP;
    f->top = (long)o->y * EMU_TWIP;
    f->right = (long)(o->x + o->width) * EMU_TWIP;
    f->bottom = (long)(o->y + o->height) * EMU_TWIP;
    f->chunk = -1;
    f->shape_type = shape_number(o);
    f->text_id = -1;
    f->image = o->kind == OWF_OBJ_PICTURE && o->image >= 0 && pub->res && o->image < pub->res->nimages ? o->image : -1;
    f->story = story_of(x, o);
    if (o->kind == OWF_OBJ_TABLE) {
        int i, rows = 0;
        for (i = 0; i < o->text.nparas; i++)
            if (o->text.paras[i].table_row + 1 > rows)
                rows = o->text.paras[i].table_row + 1;
        f->rows = rows > 0 ? rows : 1;
        f->cols = o->ncols > 0 ? o->ncols : 1;
    }
    if (o->kind == OWF_OBJ_GROUP) {
        f->filled = f->lined = -1;
        f->line_width = -1;
    } else {
        f->filled = OWF_SHADE_SET(o->look.fill) ? 1 : 0;
        f->fill = OWF_SHADE_RGB(o->look.fill);
        f->lined = OWF_SHADE_SET(o->look.line) ? 1 : 0;
        f->line = OWF_SHADE_RGB(o->look.line);
        f->line_width = f->lined ? (long)o->look.line_width * EMU_TWIP : -1;
    }
    for (k = 0; k < 4; k++)
        f->inset[k] = o->kind == OWF_OBJ_TEXT ? (long)o->inset[k] * EMU_TWIP : -1;
}

static int fill_page(exporter *x, owf_pub_page *page, const owf_obj *objs, int nobjs)
{
    int i;
    if (nobjs > 0) {
        page->frames = owf_calloc((size_t)nobjs, sizeof *page->frames);
        if (!page->frames) return -1;
        page->capframes = nobjs;
    }
    for (i = 0; i < nobjs; i++) {
        fill_frame(x, &page->frames[i], &objs[i], i, nobjs, x->pub);
        page->nframes = i + 1;
        if (objs[i].kind != OWF_OBJ_PICTURE && objs[i].kind != OWF_OBJ_GROUP &&
            (objs[i].text.nparas || objs[i].flow_story >= 0) && page->frames[i].story < 0)
            return -1;           /* new_story failed */
    }
    return 0;
}

int opp_deck_to_pub(const owf_deck *deck, owf_pub **out, owf_report *report)
{
    exporter x;
    owf_pub *pub;
    int i, most = -1;
    (void)report;
    if (!out) return OWF_ERR_FORMAT;
    *out = NULL;
    if (!deck || !deck->res || deck->width <= 0 || deck->height <= 0 || deck->nslides < 0 || deck->nmasters < 0)
        return OWF_ERR_FORMAT;
    memset(&x, 0, sizeof x);
    pub = owf_calloc(1, sizeof *pub);
    if (!pub) return OWF_ERR_MEMORY;
    x.pub = pub;
    pub->version = OWF_PUB_2007;
    pub->build_mark = -1;
    pub->width = deck->width;
    pub->height = deck->height;
    pub->width_emu = (long)deck->width * EMU_TWIP;
    pub->height_emu = (long)deck->height * EMU_TWIP;
    if (!(pub->res = owf_doc_new()) || copy_resources(pub->res, deck->res) < 0) goto memory;
    /* the flow numbers the deck's frames carry */
    for (i = 0; i < deck->nmasters; i++) {
        int j;
        for (j = 0; j < deck->masters[i].nobjs; j++)
            if (deck->masters[i].objs[j].flow_story > most) most = deck->masters[i].objs[j].flow_story;
    }
    for (i = 0; i < deck->nslides; i++) {
        int j;
        for (j = 0; j < deck->slides[i].nobjs; j++)
            if (deck->slides[i].objs[j].flow_story > most) most = deck->slides[i].objs[j].flow_story;
    }
    if (most >= 0) {
        x.nflow = most + 1;
        if (!(x.flow = owf_malloc((size_t)x.nflow * sizeof *x.flow))) goto memory;
        for (i = 0; i < x.nflow; i++) x.flow[i] = -1;
    }
    if (deck->nmasters) {
        if (!(pub->masters = owf_calloc((size_t)deck->nmasters, sizeof *pub->masters))) goto memory;
        pub->capmasters = deck->nmasters;
    }
    for (i = 0; i < deck->nmasters; i++) {
        owf_pub_page *m = &pub->masters[i];
        pub->nmasters = i + 1;
        m->master = -1;
        if (!(m->name = owf_copy(deck->masters[i].name ? deck->masters[i].name : "Master Page")) ||
            fill_page(&x, m, deck->masters[i].objs, deck->masters[i].nobjs) < 0)
            goto memory;
    }
    if (deck->nslides) {
        if (!(pub->pages = owf_calloc((size_t)deck->nslides, sizeof *pub->pages))) goto memory;
        pub->cappages = deck->nslides;
    }
    for (i = 0; i < deck->nslides; i++) {
        owf_pub_page *p = &pub->pages[i];
        pub->npages = i + 1;
        p->master = deck->slides[i].master >= 0 && deck->slides[i].master < deck->nmasters ? deck->slides[i].master : -1;
        if (fill_page(&x, p, deck->slides[i].objs, deck->slides[i].nobjs) < 0) goto memory;
    }
    owf_free(x.flow);
    *out = pub;
    return OWF_OK;
memory:
    owf_free(x.flow);
    owf_pub_free(pub);
    return OWF_ERR_MEMORY;
}

int opp_export_pub(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report)
{
    owf_pub *pub = NULL;
    int rc;
    if (!data || !length) return OWF_ERR_FORMAT;
    *data = NULL;
    *length = 0;
    rc = opp_deck_to_pub(deck, &pub, report);
    if (rc == OWF_OK)
        rc = owf_pub_write(pub, data, length, report);
    owf_pub_free(pub);
    return rc;
}

int opp_export_pub_file(const owf_deck *deck, const char *path, owf_report *report)
{
    unsigned char *data = NULL;
    size_t length = 0;
    FILE *f;
    int rc;
    if (!path) return OWF_ERR_FORMAT;
    rc = opp_export_pub(deck, &data, &length, report);
    if (rc != OWF_OK) return rc;
    f = fopen(path, "wb");
    if (!f || fwrite(data, 1, length, f) != length) rc = OWF_ERR_IO;
    if (f && fclose(f) != 0) rc = OWF_ERR_IO;
    owf_free(data);
    return rc;
}

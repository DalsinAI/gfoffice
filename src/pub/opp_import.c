/* GFOpenPublisher P1a: .pub pages onto the suite's shared deck/page model. */
#include "openpublisher_core.h"

#include <stdlib.h>
#include <string.h>

static char *text_copy(const char *s)
{
    size_t n;
    char *p;
    if (!s) return NULL;
    n = strlen(s);
    p = malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static int copy_story(owf_story *to, const owf_story *from)
{
    int i, j;
    for (i = 0; i < from->nparas; i++) {
        const owf_para *p = &from->paras[i];
        owf_para *q = owf_story_add(to, &p->fmt);
        if (!q) return -1;
        q->table_id = p->table_id;
        q->table_row = p->table_row;
        q->table_col = p->table_col;
        q->table_cols = p->table_cols;
        q->cell_shading = p->cell_shading;
        q->section_columns = p->section_columns;
        q->section_gap = p->section_gap;
        q->section_break = p->section_break;
        q->style = text_copy(p->style);
        q->bookmarks = text_copy(p->bookmarks);
        if ((p->style && !q->style) || (p->bookmarks && !q->bookmarks)) return -1;
        for (j = 0; j < p->nruns; j++) {
            const owf_run *r = &p->runs[j];
            int rc;
            if (r->kind == OWF_RUN_TEXT && r->text)
                rc = r->href ? owf_para_add_link_text(q, &r->fmt, r->text, strlen(r->text), r->href)
                             : owf_para_add_text(q, &r->fmt, r->text, strlen(r->text));
            else if (r->kind == OWF_RUN_IMAGE)
                rc = owf_para_add_image(q, &r->fmt, r->image);
            else if (r->kind == OWF_RUN_FRAME)
                rc = owf_para_add_frame(q, &r->fmt, r->image);
            else
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

static owf_obj_kind object_kind(owf_pub_kind kind)
{
    switch (kind) {
    case OWF_PUB_TEXT: return OWF_OBJ_TEXT;
    case OWF_PUB_PICTURE: return OWF_OBJ_PICTURE;
    case OWF_PUB_TABLE: return OWF_OBJ_TABLE;
    case OWF_PUB_LINE: case OWF_PUB_SHAPE: return OWF_OBJ_SHAPE;
    case OWF_PUB_GROUP: return OWF_OBJ_GROUP;
    default: return OWF_OBJ_OTHER;
    }
}

static int fill_object(owf_obj *o, const owf_pub_frame *f, const owf_pub *pub, int index, int count)
{
    o->id = f->chunk;
    o->parent = f->parent >= 0 && f->parent < count && f->parent != index ? f->parent : -1;
    o->x = f->x;
    o->y = f->y;
    o->width = f->width > 0 ? f->width : 1;
    o->height = f->height > 0 ? f->height : 1;
    o->image = f->image >= 0 && f->image < pub->res->nimages ? f->image : -1;
    if (f->kind == OWF_PUB_LINE) {
        o->shape = OWF_SHAPE_LINE;
        o->look.line = OWF_SHADE(0x000000);
    } else if (f->kind == OWF_PUB_SHAPE) {
        o->shape = OWF_SHAPE_RECT;
        o->look.line = OWF_SHADE(0x000000);
    }
    if (f->kind == OWF_PUB_TABLE) o->ncols = f->cols > 0 ? f->cols : 1;
    if (f->story >= 0 && f->story < pub->nstories && copy_story(&o->text, &pub->stories[f->story]) < 0)
        return -1;
    return 0;
}

static int fill_objects(owf_obj *(*add)(void *, owf_obj_kind), void *owner,
                        const owf_pub_page *page, const owf_pub *pub)
{
    int i;
    for (i = 0; i < page->nframes; i++) {
        owf_obj *o = add(owner, object_kind(page->frames[i].kind));
        if (!o || fill_object(o, &page->frames[i], pub, i, page->nframes) < 0) return -1;
    }
    return 0;
}

static owf_obj *add_slide_object(void *owner, owf_obj_kind kind)
{
    return owf_slide_add((owf_slide *)owner, kind);
}

static owf_obj *add_master_object(void *owner, owf_obj_kind kind)
{
    return owf_master_add((owf_master *)owner, kind);
}

int opp_pub_to_deck(const owf_pub *pub, owf_deck **out, owf_report *report)
{
    owf_deck *deck;
    int i, masters;
    if (!pub || !out || pub->width <= 0 || pub->height <= 0) return OWF_ERR_FORMAT;
    *out = NULL;
    deck = owf_deck_new_empty();
    if (!deck) return OWF_ERR_MEMORY;
    deck->width = pub->width;
    deck->height = pub->height;
    if (copy_resources(deck->res, pub->res) < 0) goto memory;
    masters = pub->nmasters > 0 ? pub->nmasters : 1;
    for (i = 0; i < masters; i++) {
        const owf_pub_page *p = i < pub->nmasters ? &pub->masters[i] : NULL;
        owf_master *m = owf_deck_add_master(deck, p && p->name ? p->name : "Default");
        if (!m || (p && fill_objects(add_master_object, m, p, pub) < 0)) goto memory;
    }
    for (i = 0; i < pub->npages; i++) {
        const owf_pub_page *p = &pub->pages[i];
        owf_slide *s = owf_deck_add_slide(deck, -1, -1);
        if (!s) goto memory;
        s->master = p->master >= 0 && p->master < masters ? p->master : 0;
        if (fill_objects(add_slide_object, s, p, pub) < 0) goto memory;
    }
    if (report)
        owf_report_add(report, OWF_NOTE_APPROX,
                       "Publisher frames use plain styling until the formatting records are decoded");
    *out = deck;
    return OWF_OK;
memory:
    owf_deck_free(deck);
    return OWF_ERR_MEMORY;
}

int opp_import_pub_file(const char *path, owf_deck **deck, owf_report *report, int *version)
{
    owf_pub *pub = NULL;
    int rc;
    if (!deck) return OWF_ERR_FORMAT;
    *deck = NULL;
    rc = owf_pub_read_file(path, &pub, report);
    if (rc == OWF_OK) {
        if (version) *version = pub->version;
        rc = opp_pub_to_deck(pub, deck, report);
    }
    owf_pub_free(pub);
    return rc;
}

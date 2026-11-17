/* GFOpenPressmaster's built-in publication templates (libowf/pub/opp_template.c). MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openpressmaster_template.h"
#include "openpressmaster_merge.h"

static int checks, failed;

#define CHECK(c) do { ++checks; if (!(c)) { ++failed; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* Every object on the page, and every group's children inside their group */
static int all_inside(const owf_deck *d)
{
    int i, o;
    for (i = 0; i < d->nslides; ++i)
        for (o = 0; o < d->slides[i].nobjs; ++o) {
            const owf_obj *b = &d->slides[i].objs[o];
            if (b->x < 0 || b->y < 0 || b->width <= 0 || b->height <= 0 || b->x + b->width > d->width ||
                b->y + b->height > d->height) {
                fprintf(stderr, "  page %d object %d outside: %d %d %d %d\n", i + 1, o, b->x, b->y, b->width, b->height);
                return 0;
            }
            if (b->parent >= 0) {
                const owf_obj *g;
                if (b->parent >= d->slides[i].nobjs) return 0;
                g = &d->slides[i].objs[b->parent];
                if (g->kind != OWF_OBJ_GROUP || b->x < g->x || b->y < g->y || b->x + b->width > g->x + g->width ||
                    b->y + b->height > g->y + g->height) return 0;
            }
        }
    return 1;
}

static int texts(const owf_deck *d)
{
    int i, o, n = 0;
    for (i = 0; i < d->nslides; ++i)
        for (o = 0; o < d->slides[i].nobjs; ++o) n += d->slides[i].objs[o].kind == OWF_OBJ_TEXT;
    return n;
}

static int flow_frames(const owf_deck *d)
{
    int i, o, n = 0;
    for (i = 0; i < d->nslides; ++i)
        for (o = 0; o < d->slides[i].nobjs; ++o) n += d->slides[i].objs[o].flow_story == 0;
    return n;
}

static void each_template(void)
{
    int i, n = opp_template_count();
    CHECK(n >= 8);
    for (i = 0; i < n; ++i) {
        const opp_template_info *t = opp_template_info_at(i);
        owf_deck *d = (owf_deck *)1;
        CHECK(t && t->name && *t->name && t->about && *t->about);
        if (!t) continue;
        CHECK(opp_template_find(t->name) == i);
        CHECK(opp_template_new(i, &d) == OWF_OK && d != NULL);
        if (!d) continue;
        CHECK(d->width == t->width && d->height == t->height);
        CHECK(d->nslides == t->pages);
        CHECK(all_inside(d));
        if (i > 0) CHECK(texts(d) > 0);                /* all but the blank page have frames to type in */
        owf_deck_free(d);
    }
    CHECK(opp_template_info_at(-1) == NULL && opp_template_info_at(n) == NULL);
}

static void find_and_refuse(void)
{
    owf_deck *d = (owf_deck *)1;
    CHECK(opp_template_find("flyer") == opp_template_find("FLYER") && opp_template_find("Flyer") >= 0);
    CHECK(opp_template_find("No Such") == -1 && opp_template_find(NULL) == -1);
    CHECK(opp_template_new(-1, &d) == OWF_ERR_UNSUPPORTED && d == NULL);
    CHECK(opp_template_new(opp_template_count(), &d) == OWF_ERR_UNSUPPORTED && d == NULL);
    CHECK(opp_template_new(0, NULL) == OWF_ERR_UNSUPPORTED);
}

static void sizes(void)
{
    const opp_template_info *a4 = opp_template_info_at(opp_template_find("Blank A4"));
    const opp_template_info *bc = opp_template_info_at(opp_template_find("Business Card"));
    const opp_template_info *pc = opp_template_info_at(opp_template_find("Postcard"));
    CHECK(a4 && a4->width == 11905 && a4->height == 16837);   /* 210 x 297 mm in twips */
    CHECK(bc && bc->width == 4818 && bc->height == 3118);     /* 85 x 55 mm */
    CHECK(pc && pc->width > pc->height);                      /* landscape */
}

static void newsletter_flow(void)
{
    owf_deck *d = NULL;
    CHECK(opp_template_new(opp_template_find("Newsletter"), &d) == OWF_OK && d);
    if (!d) return;
    CHECK(flow_frames(d) == 4);                    /* two columns on each of two pages, one story */
    owf_deck_free(d);
}

/* The catalog page's first item is what Catalog Merge repeats */
static void catalog_item(void)
{
    owf_deck *d = NULL;
    const char csv[] = "Name,Price\nA,1\nB,2\nC,3\nD,4\nE,5\n";
    int memory = 0, made = 0;
    ow_merge_table *t = ow_merge_table_from_text(csv, strlen(csv), &memory);
    opp_catalog c;
    const owf_obj *g;
    CHECK(t && opp_template_new(opp_template_find("Catalog Page"), &d) == OWF_OK && d);
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); return; }
    g = &d->slides[0].objs[1];
    CHECK(g->kind == OWF_OBJ_GROUP);
    c.x = g->x; c.y = g->y; c.width = g->width; c.height = g->height;
    c.across = 2; c.down = 2;
    CHECK(opp_merge_catalog(d, t, 0, &c, -1, -1, &made) == OWF_OK && made == 2 && d->nslides == 2);
    CHECK(d->slides[0].nobjs == 5 + 3 * 4);        /* the title, then 4 items of a group and 3 children */
    CHECK(d->slides[1].nobjs == 5);                /* the fifth record alone */
    owf_deck_free(d);
    ow_merge_table_free(t);
}

/* Each template saves as ODP and comes back with its pages and size */
static void odp_round_trip(void)
{
    const owf_deck_format *odp = owf_deck_format_named("odp");
    int i;
    CHECK(odp && odp->export);
    if (!odp || !odp->export) return;
    for (i = 0; i < opp_template_count(); ++i) {
        owf_deck *d = NULL, *back = NULL;
        unsigned char *data = NULL;
        size_t length = 0;
        CHECK(opp_template_new(i, &d) == OWF_OK);
        if (!d) continue;
        CHECK(odp->export(d, &data, &length, NULL) == OWF_OK && data && length);
        if (data) {
            CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back);
            if (back) {
                CHECK(back->nslides == d->nslides);
                CHECK(abs(back->width - d->width) <= 2 && abs(back->height - d->height) <= 2);
                CHECK(texts(back) == texts(d));
                owf_deck_free(back);
            }
            free(data);
        }
        owf_deck_free(d);
    }
}

int main(void)
{
    each_template();
    find_and_refuse();
    sizes();
    newsletter_flow();
    catalog_item();
    odp_round_trip();
    if (failed) { fprintf(stderr, "publisher templates: %d of %d checks failed\n", failed, checks); return 1; }
    printf("publisher template tests passed (%d checks)\n", checks);
    return 0;
}

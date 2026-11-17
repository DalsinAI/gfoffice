/* GFOpenPressmaster's mail merge on the page model (src/pub/opp_merge.c). MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include "openpressmaster_merge.h"

static int checks, failed;

#define CHECK(c) do { ++checks; if (!(c)) { ++failed; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static ow_merge_table *table(const char *text)
{
    int memory = 0;
    ow_merge_table *t = ow_merge_table_from_text(text, strlen(text), &memory);
    CHECK(t && !memory);
    return t;
}

static owf_obj *text_frame(owf_slide *slide, const char *text)
{
    owf_obj *o = owf_slide_add(slide, OWF_OBJ_TEXT);
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    if (!o) return NULL;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    if (!(p = owf_story_add(&o->text, &pf))) return NULL;
    if (text && owf_para_add_text(p, &cf, text, strlen(text)) != OWF_OK) return NULL;
    return o;
}

/* A frame's text as one line: line breaks as '|' */
static const char *frame_text(const owf_obj *o)
{
    static char out[512];
    size_t n = 0;
    int i, j;
    out[0] = 0;
    for (i = 0; i < o->text.nparas; ++i) {
        if (i && n + 1 < sizeof out) out[n++] = '/';
        for (j = 0; j < o->text.paras[i].nruns; ++j) {
            const owf_run *r = &o->text.paras[i].runs[j];
            const char *s = r->kind == OWF_RUN_LINEBREAK ? "|" : r->text ? r->text : "";
            if (r->kind == OWF_RUN_FIELD && n + 1 < sizeof out) out[n++] = '{';
            while (*s && n + 1 < sizeof out) out[n++] = *s++;
            if (r->kind == OWF_RUN_FIELD && n + 1 < sizeof out) out[n++] = '}';
        }
    }
    out[n] = 0;
    return out;
}

static int same(const char *a, const char *b)
{
    if (strcmp(a, b)) { fprintf(stderr, "  got \"%s\", want \"%s\"\n", a, b); return 0; }
    return 1;
}

static int field_runs(const owf_deck *d)
{
    return opp_merge_fields(d, NULL, NULL);
}

/* A letter: page 1 "Dear <Name>," + a flow of two frames, page 2 the town; a master with a field too */
static owf_deck *letter(void)
{
    owf_deck *d = owf_deck_new_empty();
    owf_slide *s;
    owf_obj *o;
    owf_master *m;
    if (!d) return NULL;
    if (!(s = owf_deck_add_slide(d, -1, -1))) goto bad;
    if (!(o = text_frame(s, "Dear "))) goto bad;
    if (opp_merge_insert_field(o, -1, "Name", NULL) != OWF_OK) goto bad;
    if (owf_para_add_text(&o->text.paras[0], &o->text.paras[0].runs[0].fmt, ",", 1) != OWF_OK) goto bad;
    if (!(o = text_frame(s, "flow one"))) goto bad;
    o->flow_story = 0;
    if (!(o = text_frame(s, "flow two"))) goto bad;
    o->flow_story = 0;
    if (!(s = owf_deck_add_slide(d, -1, -1))) goto bad;
    if (!(o = text_frame(s, NULL))) goto bad;
    if (opp_merge_insert_field(o, 0, "Town", NULL) != OWF_OK) goto bad;
    if (!(o = text_frame(s, "no fields"))) goto bad;
    o->flow_story = 3;
    if (!(m = owf_deck_add_master(d, "Master"))) goto bad;
    if (!(o = owf_master_add(m, OWF_OBJ_TEXT))) goto bad;
    if (opp_merge_insert_field(o, -1, "Name", NULL) != OWF_OK) goto bad;   /* adds the paragraph */
    return d;
bad:
    owf_deck_free(d);
    return NULL;
}

static void insert_and_count(void)
{
    owf_deck *d = letter();
    ow_merge_table *t = table("Name,Post\nAl,x\n");
    int missing = -1;
    CHECK(d != NULL);
    if (!d) return;
    CHECK(same(frame_text(&d->slides[0].objs[0]), "Dear {\xC2\xABName\xC2\xBB},"));
    CHECK(same(frame_text(&d->masters[0].objs[0]), "{\xC2\xABName\xC2\xBB}"));
    CHECK(same(d->slides[1].objs[0].text.paras[0].runs[0].href, "Town"));
    CHECK(opp_merge_fields(d, t, &missing) == 3 && missing == 1);   /* no Town */
    CHECK(opp_merge_fields(d, NULL, &missing) == 3 && missing == 0);
    CHECK(opp_merge_fields(NULL, t, &missing) == 0 && missing == 0);
    CHECK(opp_merge_insert_field(&d->slides[0].objs[0], 5, "Name", NULL) == OWF_ERR_FORMAT);
    CHECK(opp_merge_insert_field(&d->slides[0].objs[0], 0, "", NULL) == OWF_ERR_FORMAT);
    CHECK(opp_merge_insert_field(NULL, 0, "Name", NULL) == OWF_ERR_FORMAT);
    ow_merge_table_free(t);
    owf_deck_free(d);
}

static void show(void)
{
    owf_deck *d = letter();
    ow_merge_table *t = table("name,Town\nJo Smith,Leeds\nAl,\n");
    int set = -1;
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); CHECK(0); return; }
    CHECK(opp_merge_show(d, t, 0, &set) == OWF_OK && set == 3);
    CHECK(same(frame_text(&d->slides[0].objs[0]), "Dear {Jo Smith},"));   /* the column's case does not matter */
    CHECK(same(frame_text(&d->slides[1].objs[0]), "{Leeds}"));
    CHECK(same(frame_text(&d->masters[0].objs[0]), "{Jo Smith}"));
    CHECK(opp_merge_show(d, t, 1, &set) == OWF_OK && set == 3);
    CHECK(same(frame_text(&d->slides[1].objs[0]), "{}"));                 /* an empty cell */
    CHECK(opp_merge_show(d, t, -1, &set) == OWF_OK);
    CHECK(same(frame_text(&d->slides[0].objs[0]), "Dear {\xC2\xABName\xC2\xBB},"));
    CHECK(opp_merge_show(d, t, 2, &set) == OWF_ERR_UNSUPPORTED && set == 0);
    CHECK(opp_merge_show(d, t, -2, NULL) == OWF_ERR_UNSUPPORTED);
    CHECK(opp_merge_show(d, NULL, 0, NULL) == OWF_ERR_UNSUPPORTED);
    CHECK(opp_merge_show(d, NULL, -1, &set) == OWF_OK && set == 3);
    CHECK(opp_merge_show(NULL, t, 0, NULL) == OWF_ERR_FORMAT);
    CHECK(field_runs(d) == 3);                                           /* showing keeps them fields */
    ow_merge_table_free(t);
    owf_deck_free(d);
}

static void pages(void)
{
    owf_deck *d = letter();
    ow_merge_table *t = table("Name,Town\nJo,Leeds\nAl,\"Upper\r\nDale\"\nKim,York\n");
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); CHECK(0); return; }
    CHECK(opp_merge_pages(d, t, -1, -1) == OWF_OK);
    CHECK(d->nslides == 6);
    CHECK(same(frame_text(&d->slides[0].objs[0]), "Dear Jo,"));
    CHECK(same(frame_text(&d->slides[1].objs[0]), "Leeds"));
    CHECK(same(frame_text(&d->slides[2].objs[0]), "Dear Al,"));
    CHECK(same(frame_text(&d->slides[3].objs[0]), "Upper|Dale"));         /* the line break, CR left out */
    CHECK(d->slides[3].objs[0].text.paras[0].nruns == 3);
    CHECK(d->slides[3].objs[0].text.paras[0].runs[1].kind == OWF_RUN_LINEBREAK);
    CHECK(same(frame_text(&d->slides[5].objs[0]), "York"));
    CHECK(same(frame_text(&d->slides[4].objs[1]), "flow one"));
    CHECK(opp_merge_fields(d, NULL, NULL) == 1);                         /* the master's is left */
    /* each record's flows are flows of their own, numbered from the first's */
    CHECK(d->slides[0].objs[1].flow_story == 0 && d->slides[0].objs[2].flow_story == 0);
    CHECK(d->slides[1].objs[1].flow_story == 3);
    CHECK(d->slides[2].objs[1].flow_story == 4 && d->slides[2].objs[2].flow_story == 4);
    CHECK(d->slides[3].objs[1].flow_story == 7);
    CHECK(d->slides[4].objs[1].flow_story == 8 && d->slides[5].objs[1].flow_story == 11);
    CHECK(d->slides[0].objs[0].flow_story == -1);
    owf_deck_free(d);

    d = letter();
    CHECK(d && opp_merge_pages(d, t, 1, 1) == OWF_OK && d->nslides == 2);
    if (d) CHECK(same(frame_text(&d->slides[0].objs[0]), "Dear Al,"));
    if (d) CHECK(d->slides[0].objs[1].flow_story == 0 && d->slides[1].objs[1].flow_story == 3);
    owf_deck_free(d);

    d = letter();
    CHECK(d && opp_merge_pages(d, t, 2, 1) == OWF_ERR_UNSUPPORTED);
    CHECK(d && opp_merge_pages(d, t, 0, 3) == OWF_ERR_UNSUPPORTED);
    CHECK(d && opp_merge_pages(d, t, -2, 0) == OWF_ERR_UNSUPPORTED);
    CHECK(d && opp_merge_pages(d, NULL, -1, -1) == OWF_ERR_UNSUPPORTED);
    CHECK(d && d->nslides == 2 && field_runs(d) == 3);                   /* refused: as it was */
    ow_merge_table_free(t);
    t = table("Name\n");                                                 /* no records */
    CHECK(d && opp_merge_pages(d, t, -1, -1) == OWF_ERR_UNSUPPORTED);
    owf_deck_free(d);
    d = owf_deck_new_empty();                                            /* no pages */
    CHECK(d && opp_merge_pages(d, t, -1, -1) == OWF_ERR_UNSUPPORTED);
    owf_deck_free(d);
    ow_merge_table_free(t);
}

static void missing_column(void)
{
    owf_deck *d = letter();
    ow_merge_table *t = table("Name\nJo\n");                             /* no Town */
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); CHECK(0); return; }
    CHECK(opp_merge_pages(d, t, -1, -1) == OWF_OK && d->nslides == 2);
    CHECK(same(frame_text(&d->slides[1].objs[0]), "\xC2\xABTown\xC2\xBB")); /* shows its name, as plain text */
    CHECK(d->slides[1].objs[0].text.paras[0].runs[0].kind == OWF_RUN_TEXT);
    CHECK(d->slides[1].objs[0].text.paras[0].runs[0].href == NULL);
    ow_merge_table_free(t);
    owf_deck_free(d);

    /* a column name with line breaks (a quoted header, or a field from another source) shows as its name */
    d = letter();
    t = table("Name\nAnn\nBob\n");
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); CHECK(0); return; }
    {
        int k;
        for (k = 0; k < 4; ++k) CHECK(opp_merge_insert_field(&d->slides[1].objs[0], -1, "a\nb\nc\nd\ne", NULL) == OWF_OK);
    }
    CHECK(opp_merge_pages(d, t, -1, -1) == OWF_OK && d->nslides == 4);
    CHECK(same(frame_text(&d->slides[1].objs[0]),
               "\xC2\xABTown\xC2\xBB"
               "\xC2\xAB" "a|b|c|d|e\xC2\xBB\xC2\xAB" "a|b|c|d|e\xC2\xBB\xC2\xAB" "a|b|c|d|e\xC2\xBB\xC2\xAB" "a|b|c|d|e\xC2\xBB"));
    CHECK(d->slides[1].objs[0].text.paras[0].nruns == 1 + 4 * 9);
    ow_merge_table_free(t);
    owf_deck_free(d);
}

static void insert_at(void)
{
    owf_deck *d = owf_deck_new_empty();
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    owf_obj *o = s ? text_frame(s, "Dear friend") : NULL;
    owf_obj *empty = s ? owf_slide_add(s, OWF_OBJ_TEXT) : NULL;
    CHECK(o != NULL && empty != NULL);
    if (!o || !empty) { owf_deck_free(d); return; }
    o = &d->slides[0].objs[0];
    empty = &d->slides[0].objs[1];
    o->text.paras[0].runs[0].fmt.flags = OWF_BOLD;
    CHECK(opp_merge_insert_field_at(o, 0, 5, "Name") == OWF_OK);          /* inside the text: split */
    CHECK(same(frame_text(o), "Dear {\xC2\xABName\xC2\xBB}friend"));
    CHECK(o->text.paras[0].nruns == 3 && o->text.paras[0].runs[1].fmt.flags == OWF_BOLD);
    CHECK(o->text.paras[0].runs[2].fmt.flags == OWF_BOLD && same(o->text.paras[0].runs[2].text, "friend"));
    CHECK(opp_merge_insert_field_at(o, 0, 5, "Town") == OWF_OK);          /* the same place: after that field */
    CHECK(same(frame_text(o), "Dear {\xC2\xABName\xC2\xBB}{\xC2\xABTown\xC2\xBB}friend"));
    CHECK(opp_merge_insert_field_at(o, 0, 0, "A") == OWF_OK);             /* the start */
    CHECK(same(frame_text(o), "{\xC2\xAB" "A\xC2\xBB}Dear {\xC2\xABName\xC2\xBB}{\xC2\xABTown\xC2\xBB}friend"));
    CHECK(opp_merge_insert_field_at(o, 0, 11, "Z") == OWF_OK);            /* the end */
    CHECK(same(frame_text(o), "{\xC2\xAB" "A\xC2\xBB}Dear {\xC2\xABName\xC2\xBB}{\xC2\xABTown\xC2\xBB}friend{\xC2\xABZ\xC2\xBB}"));
    CHECK(opp_merge_insert_field_at(o, 0, 12, "Z") == OWF_ERR_FORMAT);    /* past the end */
    CHECK(opp_merge_insert_field_at(o, 1, 0, "Z") == OWF_ERR_FORMAT);
    CHECK(opp_merge_insert_field_at(o, 0, 0, "") == OWF_ERR_FORMAT);
    CHECK(opp_merge_insert_field_at(empty, 0, 0, "Name") == OWF_OK);      /* a box with no text yet */
    CHECK(same(frame_text(empty), "{\xC2\xABName\xC2\xBB}"));
    CHECK(opp_merge_fields(d, NULL, NULL) == 5);
    owf_deck_free(d);
}

static void load_file(void)
{
    const char *path = "build/host/test-pub-merge.csv";
    ow_merge_table *t = (ow_merge_table *)1;
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    fputs("Name;Town\r\nJo;Leeds\r\n", f);
    fclose(f);
    CHECK(opp_merge_load_file(path, &t) == OWF_OK && t && t->ncols == 2 && t->nrows == 1);
    if (t) CHECK(same(ow_merge_value(t, 0, "Town"), "Leeds"));
    ow_merge_table_free(t);
    f = fopen(path, "wb");
    if (f) fclose(f);                                                    /* empty: no columns */
    CHECK(opp_merge_load_file(path, &t) == OWF_ERR_FORMAT && t == NULL);
    remove(path);
    CHECK(opp_merge_load_file(path, &t) == OWF_ERR_IO && t == NULL);
    CHECK(opp_merge_load_file(NULL, &t) == OWF_ERR_IO);
    CHECK(opp_merge_load_file(path, NULL) == OWF_ERR_FORMAT);
}

/* A catalog: page 1 a cover, page 2 a heading with Name (outside the area) and an item of a group holding a
 * Name frame and a Town frame with a flow of its own, page 3 a back page */
static owf_deck *catalog(void)
{
    owf_deck *d = owf_deck_new_empty();
    owf_slide *s;
    owf_obj *o;
    if (!d) return NULL;
    if (!(s = owf_deck_add_slide(d, -1, -1)) || !(o = text_frame(s, "Cover"))) goto bad;
    o->flow_story = 0;
    if (!(s = owf_deck_add_slide(d, -1, -1))) goto bad;
    if (!(o = text_frame(s, "From "))) goto bad;               /* 0: the heading */
    o->x = 0; o->y = 0; o->width = 9000; o->height = 500;
    if (opp_merge_insert_field(o, -1, "Name", NULL) != OWF_OK) goto bad;
    if (!(o = owf_slide_add(s, OWF_OBJ_GROUP))) goto bad;      /* 1: the item's group */
    o->x = 1000; o->y = 1000; o->width = 3000; o->height = 1000;
    if (!(o = text_frame(s, NULL))) goto bad;                  /* 2 */
    o->x = 1000; o->y = 1000; o->width = 3000; o->height = 400; o->parent = 1;
    if (opp_merge_insert_field(o, 0, "Name", NULL) != OWF_OK) goto bad;
    if (!(o = text_frame(s, "in "))) goto bad;                 /* 3 */
    o->x = 1000; o->y = 1500; o->width = 3000; o->height = 400; o->parent = 1; o->flow_story = 1;
    if (opp_merge_insert_field(o, 0, "Town", NULL) != OWF_OK) goto bad;
    if (!(o = text_frame(s, "Wide"))) goto bad;                /* 4: crosses the box's edge, so not the item */
    o->x = 3000; o->y = 1000; o->width = 3000; o->height = 400;
    if (!(s = owf_deck_add_slide(d, -1, -1)) || !text_frame(s, "Back")) goto bad;
    return d;
bad:
    owf_deck_free(d);
    return NULL;
}

static void catalog_merge(void)
{
    ow_merge_table *t = table("Name,Town\nAnn,Ayr\nBob,Bath\nCy,Cork\nDi,Derby\nEd,Ely\n");
    owf_deck *d = catalog();
    opp_catalog c;
    int made = -1, i, o, seen[64];
    if (!d || !t) { owf_deck_free(d); ow_merge_table_free(t); CHECK(0); return; }
    c.x = 1000; c.y = 1000; c.width = 4000; c.height = 1500; c.across = 2; c.down = 1;
    CHECK(opp_merge_catalog(d, t, 1, &c, -1, -1, &made) == OWF_OK && made == 3);
    CHECK(d->nslides == 5);                                    /* cover, 3 catalog pages, back */
    CHECK(same(frame_text(&d->slides[0].objs[0]), "Cover") && same(frame_text(&d->slides[4].objs[0]), "Back"));
    CHECK(field_runs(d) == 0);
    /* page 1: Ann then Bob; the heading and the wide frame once, with the page's first record */
    CHECK(d->slides[1].nobjs == 5 + 3);
    CHECK(same(frame_text(&d->slides[1].objs[0]), "From Ann"));
    CHECK(same(frame_text(&d->slides[1].objs[2]), "Ann") && same(frame_text(&d->slides[1].objs[3]), "in Ayr"));
    CHECK(same(frame_text(&d->slides[1].objs[4]), "Wide") && d->slides[1].objs[4].x == 3000);
    CHECK(d->slides[1].objs[5].kind == OWF_OBJ_GROUP && d->slides[1].objs[5].x == 5000 && d->slides[1].objs[5].y == 1000);
    CHECK(same(frame_text(&d->slides[1].objs[6]), "Bob") && same(frame_text(&d->slides[1].objs[7]), "in Bath"));
    CHECK(d->slides[1].objs[6].x == 5000 && d->slides[1].objs[7].y == 1500);
    CHECK(d->slides[1].objs[6].parent == 5 && d->slides[1].objs[7].parent == 5 && d->slides[1].objs[2].parent == 1);
    CHECK(same(frame_text(&d->slides[2].objs[0]), "From Cy") && same(frame_text(&d->slides[2].objs[6]), "Di"));
    /* page 3: Ed alone, the second place empty */
    CHECK(d->slides[3].nobjs == 5 && same(frame_text(&d->slides[3].objs[2]), "Ed"));
    CHECK(same(frame_text(&d->slides[3].objs[3]), "in Ely"));
    /* every item's flow is its own, and none is the cover's */
    memset(seen, 0, sizeof seen);
    for (i = 1; i <= 3; ++i)
        for (o = 0; o < d->slides[i].nobjs; ++o) {
            int f = d->slides[i].objs[o].flow_story;
            if (f < 0) continue;
            CHECK(f > 0 && f < 64);
            if (f > 0 && f < 64) { CHECK(!seen[f]); seen[f] = 1; }
        }
    CHECK(d->slides[0].objs[0].flow_story == 0);
    owf_deck_free(d);

    d = catalog();                                             /* a range, two down */
    c.across = 1; c.down = 2;
    CHECK(d && opp_merge_catalog(d, t, 1, &c, 1, 2, &made) == OWF_OK && made == 1 && d->nslides == 3);
    if (d && d->nslides == 3) {
        CHECK(same(frame_text(&d->slides[1].objs[0]), "From Bob") && same(frame_text(&d->slides[1].objs[6]), "Cy"));
        CHECK(d->slides[1].objs[6].x == 1000 && d->slides[1].objs[6].y == 2500);
    }
    owf_deck_free(d);

    d = catalog();                                             /* what cannot be */
    CHECK(d != NULL);
    c.across = 2; c.down = 1;
    CHECK(opp_merge_catalog(d, NULL, 1, &c, -1, -1, &made) == OWF_ERR_UNSUPPORTED && made == 0);
    CHECK(opp_merge_catalog(d, t, 3, &c, -1, -1, NULL) == OWF_ERR_UNSUPPORTED);
    CHECK(opp_merge_catalog(d, t, 1, NULL, -1, -1, NULL) == OWF_ERR_UNSUPPORTED);
    CHECK(opp_merge_catalog(d, t, 1, &c, 3, 1, NULL) == OWF_ERR_UNSUPPORTED);
    CHECK(opp_merge_catalog(d, t, 1, &c, 0, 5, NULL) == OWF_ERR_UNSUPPORTED);
    c.across = 0;
    CHECK(opp_merge_catalog(d, t, 1, &c, -1, -1, NULL) == OWF_ERR_UNSUPPORTED);
    c.across = OPP_CATALOG_MAX + 1;
    CHECK(opp_merge_catalog(d, t, 1, &c, -1, -1, NULL) == OWF_ERR_UNSUPPORTED);
    c.across = 2; c.width = 0x40000000;
    CHECK(opp_merge_catalog(d, t, 1, &c, -1, -1, NULL) == OWF_ERR_UNSUPPORTED);
    c.x = 8000; c.width = 500;                                 /* nothing inside the box */
    CHECK(opp_merge_catalog(d, t, 1, &c, -1, -1, NULL) == OWF_ERR_FORMAT);
    CHECK(opp_merge_catalog(d, t, 2, &c, -1, -1, NULL) == OWF_ERR_FORMAT);
    CHECK(d->nslides == 3 && field_runs(d) == 3);              /* the deck as it was */
    owf_deck_free(d);
    ow_merge_table_free(t);
}

int main(void)
{
    insert_and_count();
    show();
    pages();
    missing_column();
    insert_at();
    load_file();
    catalog_merge();
    if (failed) { fprintf(stderr, "publisher merge: %d of %d checks failed\n", failed, checks); return 1; }
    printf("publisher merge tests passed (%d checks)\n", checks);
    return 0;
}

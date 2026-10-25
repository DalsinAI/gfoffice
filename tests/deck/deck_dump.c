/*
 * deck_dump FILE [OUT]: reads a deck with libowf and prints everything the
 * model holds, one item a line, for test_decks.py to check and compare;
 * with OUT, writes the deck there too (by OUT's extension). Fonts are
 * printed by name and pictures by size and checksum, so that two readings
 * of the same deck print the same even when their tables are in another
 * order. Exit 0 when the file was read (and written), 10 when it was
 * refused (with the reason), never a crash, cut short or not.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_deck.h"

#include <stdio.h>
#include <string.h>

static const owf_deck *deck;

static void put_string(const char *s)
{
    if (!s) {
        printf("-");
        return;
    }
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            printf("\\%c", *s);
        else if (*s == '\n')
            printf("\\n");
        else if ((unsigned char)*s < 0x20)
            printf("\\x%02x", (unsigned char)*s);
        else
            putchar(*s);
    }
    putchar('"');
}

static void put_colour(unsigned long shade)
{
    if (OWF_SHADE_SET(shade))
        printf("%06lX", OWF_SHADE_RGB(shade));
    else
        printf("-");
}

static unsigned long checksum(const unsigned char *d, size_t n)
{
    unsigned long h = 5381;
    size_t i;
    for (i = 0; i < n; i++)
        h = (h * 33 + d[i]) & 0xFFFFFFFFUL;
    return h;
}

static void put_font(int font)
{
    if (font >= 0 && font < deck->res->nfonts)
        put_string(deck->res->fonts[font].name);
    else
        printf("-");
}

static void put_charfmt(const owf_charfmt *f)
{
    printf("font=");
    put_font(f->font);
    printf(" size=%d flags=%u colour=", f->size, f->flags);
    if (f->colour == OWF_COLOUR_AUTO)
        printf("auto");
    else
        printf("%06lX", f->colour & 0xFFFFFFUL);
}

static void put_parafmt(const owf_parafmt *f)
{
    int i;
    printf("align=%d left=%d right=%d first=%d before=%d after=%d line=%d shade=", f->align, f->indent_left,
           f->indent_right, f->indent_first, f->space_before, f->space_after, f->line_spacing);
    put_colour(f->shading);
    printf(" borders=%d tabs=", f->borders);
    for (i = 0; i < f->ntabs; i++)
        printf("%s%d/%d", i ? "," : "", f->tabs[i].position, f->tabs[i].kind);
}

static void put_level(const char *indent, const char *what, const owf_textlevel *l)
{
    printf("%slevel %s ", indent, what);
    put_charfmt(&l->chars);
    printf(" ");
    put_parafmt(&l->para);
    printf(" bullet=%lX bfont=", l->bullet);
    put_font(l->bullet_font);
    printf(" bsize=%d bcolour=", l->bullet_size);
    put_colour(l->bullet_colour);
    printf(" start=%d prefix=", l->start);
    put_string(l->prefix);
    printf(" suffix=");
    put_string(l->suffix);
    printf("\n");
}

static void put_image(int index)
{
    const owf_image *im;
    if (index < 0 || index >= deck->res->nimages) {
        printf("-");
        return;
    }
    im = &deck->res->images[index];
    printf("%s/%lu/%08lX", im->mime ? im->mime : "?", (unsigned long)im->length, checksum(im->data, im->length));
}

static void put_story(const char *indent, const owf_story *story, const owf_obj *obj)
{
    int i, j;
    for (i = 0; i < story->nparas; i++) {
        const owf_para *p = &story->paras[i];
        printf("%spara level=%d ", indent, obj ? owf_obj_level(obj, i) : 0);
        put_parafmt(&p->fmt);
        if (p->table_id >= 0)
            printf(" cell=%d,%d/%d shade=", p->table_row, p->table_col, p->table_cols), put_colour(p->cell_shading);
        printf("\n");
        for (j = 0; j < p->nruns; j++) {
            const owf_run *r = &p->runs[j];
            printf("%s  run %d ", indent, (int)r->kind);
            put_charfmt(&r->fmt);
            if (r->kind == OWF_RUN_TEXT) {
                printf(" ");
                put_string(r->text);
            } else if (r->kind == OWF_RUN_FIELD) {
                printf(" field=%d", (int)r->field);
            }
            if (r->href) {
                printf(" href=");
                put_string(r->href);
            }
            printf("\n");
        }
    }
}

static void put_objs(const char *indent, const owf_obj *objs, int n)
{
    int i, k;
    char deeper[32];
    snprintf(deeper, sizeof deeper, "%s    ", indent);
    for (i = 0; i < n; i++) {
        const owf_obj *o = &objs[i];
        printf("%sobj %d kind=%d parent=%d box=%d,%d,%d,%d rot=%d flags=%u ph=%d/%d", indent, i, (int)o->kind,
               o->parent, o->x, o->y, o->width, o->height, o->rotation, o->flags, (int)o->placeholder, o->ph_index);
        printf(" fill=");
        put_colour(o->look.fill);
        printf(" line=");
        put_colour(o->look.line);
        printf("/%d/%d/%d", o->look.line_width, o->look.line_style, o->look.arrows);
        if (o->kind == OWF_OBJ_SHAPE) {
            printf(" shape=%d radius=%d points=", (int)o->shape, o->radius);
            for (k = 0; k < o->npoints; k++)
                printf("%s%d,%d", k ? " " : "", o->points[k * 2], o->points[k * 2 + 1]);
        }
        printf(" valign=%d autofit=%d inset=%d,%d,%d,%d", o->valign, o->autofit, o->inset[0], o->inset[1],
               o->inset[2], o->inset[3]);
        if (o->kind == OWF_OBJ_PICTURE || o->image >= 0) {
            printf(" image=");
            put_image(o->image);
            printf(" crop=%d,%d,%d,%d", o->crop[0], o->crop[1], o->crop[2], o->crop[3]);
        }
        if (o->kind == OWF_OBJ_TABLE) {
            printf(" cols=%d widths=", o->ncols);
            for (k = 0; o->col_widths && k < o->ncols; k++)
                printf("%s%d", k ? "," : "", o->col_widths[k]);
        }
        printf(" id=%ld xmlid=", o->id);
        put_string(o->xml_id);
        printf(" name=");
        put_string(o->name);
        printf(" alt=");
        put_string(o->alt);
        if (o->keep >= 0 && o->keep < deck->nkept)
            printf(" keep=%d/%08lX", (int)deck->kept[o->keep].kind,
                   checksum((const unsigned char *)deck->kept[o->keep].xml, strlen(deck->kept[o->keep].xml)));
        printf("\n");
        if (o->list)
            for (k = 0; k < OWF_TEXT_LEVELS; k++) {
                char what[16];
                snprintf(what, sizeof what, "list%d", k + 1);
                put_level(deeper, what, &o->list[k]);
            }
        put_story(deeper, &o->text, o);
    }
}

static const char *const trans[] = { "none", "fade", "fade-black", "wipe", "push", "cover", "uncover", "split",
                                     "blinds", "dissolve", "other" };

int main(int argc, char **argv)
{
    owf_report *report = owf_report_new();
    owf_deck *d = NULL;
    const owf_deck_format *used = NULL;
    int result, i, k;

    if (argc < 2 || argc > 3 || !report) {
        fprintf(stderr, "usage: deck_dump FILE [OUT]\n");
        return 20;
    }
    result = owf_import_deck_file(argv[1], NULL, &d, report, &used);
    for (i = 0; i < owf_report_count(report); i++) {
        owf_note_level level;
        int times;
        const char *note = owf_report_note(report, i, &level, &times);
        printf("note %d ", (int)level);
        put_string(note);
        printf("\n");
    }
    if (result != OWF_OK) {
        printf("error %s\n", owf_error_text(result));
        owf_report_free(report);
        return 10;
    }
    deck = d;
    printf("deck format=%s size=%d,%d notes=%d,%d title=", used->name, d->width, d->height, d->notes_page.width,
           d->notes_page.height);
    put_string(d->res->title);
    printf(" base=");
    put_charfmt(&d->res->base);
    printf(" slides=%d masters=%d layouts=%d\n", d->nslides, d->nmasters, d->nlayouts);
    for (i = 0; i < d->nmasters; i++) {
        const owf_master *m = &d->masters[i];
        printf("master %d ", i);
        put_string(m->name);
        printf(" background=");
        put_colour(m->background);
        printf(" image=");
        put_image(m->background_image);
        printf("\n");
        put_level("  ", "title", &m->title);
        put_level("  ", "subtitle", &m->subtitle);
        for (k = 0; k < OWF_TEXT_LEVELS; k++) {
            char what[16];
            snprintf(what, sizeof what, "body%d", k + 1);
            put_level("  ", what, &m->body[k]);
        }
        put_level("  ", "notes", &m->notes);
        put_level("  ", "other", &m->other);
        put_objs("  ", m->objs, m->nobjs);
    }
    for (i = 0; i < d->nlayouts; i++) {
        printf("layout %d ", i);
        put_string(d->layouts[i].name);
        printf("\n");
        put_objs("  ", d->layouts[i].objs, d->layouts[i].nobjs);
    }
    for (i = 0; i < d->nslides; i++) {
        const owf_slide *s = &d->slides[i];
        const owf_transition *t = &s->transition;
        printf("slide %d name=", i);
        put_string(s->name);
        printf(" master=%d layout=%d background=", s->master, s->layout);
        put_colour(s->background);
        printf(" image=");
        put_image(s->background_image);
        printf(" hidden=%d show=%u footer=", s->hidden, s->show);
        put_string(s->footer);
        printf(" date=");
        put_string(s->date_text);
        printf(" transition=%s/%d/%d/%d", trans[t->kind <= OWF_TRANS_OTHER ? t->kind : 0], t->direction, t->duration,
               t->advance_after);
        if (t->keep >= 0 && t->keep < d->nkept)
            printf(" tkeep=%s", d->kept[t->keep].xml);
        if (s->keep_timing >= 0 && s->keep_timing < d->nkept)
            printf(" timing=%08lX", checksum((const unsigned char *)d->kept[s->keep_timing].xml,
                                             strlen(d->kept[s->keep_timing].xml)));
        printf("\n");
        put_objs("  ", s->objs, s->nobjs);
        if (s->notes.nparas) {
            printf("  notes\n");
            put_story("    ", &s->notes, NULL);
        }
    }
    {
        int counts[7] = { 0, 0, 0, 0, 0, 0, 0 };
        for (i = 0; i < d->nkept; i++)
            if ((unsigned)d->kept[i].kind < 7)
                counts[d->kept[i].kind]++;
        printf("kept objects=%d timing=%d styles=%d definitions=%d attributes=%d settings=%d graphic=%d\n", counts[0],
               counts[1], counts[2], counts[3], counts[4], counts[5], counts[6]);
    }
    for (i = 0; i < d->nparts; i++) {
        printf("part ");
        put_string(d->parts[i].name);
        printf(" %s %lu %08lX\n", d->parts[i].mime ? d->parts[i].mime : "-", (unsigned long)d->parts[i].length,
               d->parts[i].data ? checksum(d->parts[i].data, d->parts[i].length) : 0UL);
    }
    if (argc == 3) {
        owf_report *out_report = owf_report_new();
        result = owf_export_deck_file(d, argv[2], NULL, out_report);
        for (i = 0; i < owf_report_count(out_report); i++) {
            owf_note_level level;
            int times;
            const char *note = owf_report_note(out_report, i, &level, &times);
            printf("save-note %d ", (int)level);
            put_string(note);
            printf("\n");
        }
        owf_report_free(out_report);
        if (result != OWF_OK) {
            printf("save-error %s\n", owf_error_text(result));
            owf_deck_free(d);
            owf_report_free(report);
            return 10;
        }
    }
    owf_deck_free(d);
    owf_report_free(report);
    return 0;
}

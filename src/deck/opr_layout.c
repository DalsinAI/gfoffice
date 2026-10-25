/*
 * OpenPresentation's core: a slide laid out by OpenLayout (see
 * openpresentation_core.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "openpresentation_core.h"
#include "ow_layout.h"
#include "owf_pdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TW(v) ((ol_unit)(v) * OL_UNITS_PER_TWIP)
#define MIN_SCALE 60                 /* shrink to fit stops here */
#define MAX_SIZE 20000               /* twips: 1000 points, Impress's own largest is 999.9 */
#define MAX_SPAN 200000              /* twips: an indent or space wider than any slide */

static int bound(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- Fonts and measuring ---- */

static int contains(const char *s, const char *word)
{
    size_t n = strlen(word);
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < n && s[i]; i++) {
            int a = s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i];
            if (a != word[i])
                break;
        }
        if (i == n)
            return 1;
    }
    return 0;
}

owf_font_kind opr_font_kind(const owf_deck *deck, int font)
{
    const owf_font *f;
    owf_font_kind kind = OWF_FONT_SANS;
    if (!deck || font < 0 || font >= deck->res->nfonts)
        return OWF_FONT_SANS;
    f = &deck->res->fonts[font];
    if (f->kind != OWF_FONT_ANY)
        return f->kind;
    if (owf_font_modern(f->name, &kind))
        return kind;
    if (contains(f->name, "mono") || contains(f->name, "courier") || contains(f->name, "consol") ||
        contains(f->name, "typewriter"))
        return OWF_FONT_MONO;
    if (contains(f->name, "serif") && !contains(f->name, "sans"))
        return OWF_FONT_SERIF;
    if (contains(f->name, "times") || contains(f->name, "roman") || contains(f->name, "georgia") ||
        contains(f->name, "garamond") || contains(f->name, "cambria") || contains(f->name, "palatino") ||
        contains(f->name, "book"))
        return OWF_FONT_SERIF;
    return OWF_FONT_SANS;
}

static unsigned style_flags(uint32_t text)
{
    unsigned f = 0;
    if (text & OL_TEXT_BOLD) f |= OWF_BOLD;
    if (text & OL_TEXT_ITALIC) f |= OWF_ITALIC;
    return f;
}

int opr_measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    const owf_deck *deck = (const owf_deck *)userdata;
    int font = OW_LAYOUT_FONT_INDEX(OL_TEXT_FONT_OF(style->text_flags));
    owf_font_kind kind = (style->text_flags & OL_TEXT_MONO) ? OWF_FONT_MONO : opr_font_kind(deck, font);
    int size = style->font_size / OL_UNITS_PER_TWIP;
    m->width = TW(owf_pdf_text_width(utf8, length, kind, style_flags(style->text_flags), size));
    /* Liberation Sans' (and Arial's) ascender and descender, of the em */
    m->ascent = (ol_unit)((long long)style->font_size * 905 / 1000);
    m->descent = (ol_unit)((long long)style->font_size * 212 / 1000);
    return 1;
}

/* ---- A frame's text, made ready for OpenWrite's paragraph code ---- */

typedef struct {
    owf_story story;             /* paragraphs sharing the object's text */
    owf_run **runs;              /* the ones made here, to free */
    char (*labels)[40];          /* bullets and numbers */
    int n;
} frame_text;

static void frame_text_free(frame_text *t)
{
    int i;
    for (i = 0; i < t->n; i++)
        free(t->runs[i]);
    free(t->runs);
    free(t->labels);
    free(t->story.paras);
    memset(t, 0, sizeof *t);
}

static void roman(int n, int lower, char *out, size_t size)
{
    static const int value[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const upper[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
    size_t used = 0;
    int i;
    out[0] = 0;
    if (n <= 0 || n >= 4000) {
        snprintf(out, size, "%d", n);
        return;
    }
    for (i = 0; i < 13; i++)
        while (n >= value[i]) {
            const char *s;
            for (s = upper[i]; *s && used + 1 < size; s++)
                out[used++] = lower ? (char)(*s - 'A' + 'a') : *s;
            n -= value[i];
        }
    out[used] = 0;
}

/* A list level's label: its bullet, or its number in its format. */
static void label(const owf_textlevel *l, int number, char *out, size_t size)
{
    char num[24];
    if (!(l->bullet & OWF_BULLET_NUMBER)) {
        unsigned long c = l->bullet;
        /* a symbol font's private character (StarBats', Wingdings'): a plain bullet */
        if (c >= 0xE000 && c <= 0xF8FF)
            c = 0x2022;
        size_t n = 0;
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out[n++] = (char)(0xE0 | (c >> 12));
            out[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[n++] = '?';
        }
        out[n] = 0;
        return;
    }
    switch ((char)(l->bullet & 0xFF)) {
    case 'a': case 'A':
        if (number > 0) {
            int k = 0;
            char c = (char)(((l->bullet & 0xFF) == 'a' ? 'a' : 'A') + (number - 1) % 26);
            int times = (number - 1) / 26 + 1;
            for (k = 0; k < times && k < 8; k++)
                num[k] = c;
            num[k] = 0;
        } else {
            snprintf(num, sizeof num, "%d", number);
        }
        break;
    case 'i': roman(number, 1, num, sizeof num); break;
    case 'I': roman(number, 0, num, sizeof num); break;
    default: snprintf(num, sizeof num, "%d", number); break;
    }
    snprintf(out, size, "%s%s%s", l->prefix, num, l->suffix);
}

/* The object's paragraphs with every run's font, size and colour set
 * (from its level when it has none), sizes at scale percent, bullets and
 * numbers in front, and fields filled in. */
static int prepare_text(const owf_deck *deck, const owf_master *m, const owf_obj *obj, const owf_story *src,
                        int with_levels, int scale, const char *const fields[4], frame_text *t)
{
    int i, counters[OWF_TEXT_LEVELS + 1];
    unsigned long auto_colour = OWF_COLOUR_AUTO;
    memset(t, 0, sizeof *t);
    /* automatic text on a dark fill is white, as Impress draws it */
    if (OWF_SHADE_SET(obj->look.fill)) {
        unsigned long rgb = OWF_SHADE_RGB(obj->look.fill);
        unsigned long y = ((rgb >> 16) & 255) * 299 + ((rgb >> 8) & 255) * 587 + (rgb & 255) * 114;
        if (y < 128000UL)
            auto_colour = 0xFFFFFF;
    }
    memset(counters, 0, sizeof counters);
    if (!src->nparas)
        return 1;
    t->story.paras = calloc((size_t)src->nparas, sizeof *t->story.paras);
    t->runs = calloc((size_t)src->nparas, sizeof *t->runs);
    t->labels = calloc((size_t)src->nparas, sizeof *t->labels);
    if (!t->story.paras || !t->runs || !t->labels) {
        frame_text_free(t);
        return 0;
    }
    t->n = src->nparas;
    t->story.nparas = t->story.capparas = src->nparas;
    for (i = 0; i < src->nparas; i++) {
        const owf_para *p = &src->paras[i];
        owf_para *q = &t->story.paras[i];
        int level = with_levels ? owf_obj_level(obj, i) : 0, j, k = 0;
        const owf_textlevel *lv = owf_deck_para_level(deck, m, obj, with_levels ? i : -1);
        owf_charfmt first;
        owf_run *runs;

        if (!lv) {
            static owf_textlevel plain;
            owf_textlevel_init(&plain);
            plain.chars.size = deck->res->base.size;
            lv = &plain;
        }
        *q = *p;
        runs = calloc((size_t)p->nruns + 3, sizeof *runs);
        if (!runs) {
            frame_text_free(t);
            return 0;
        }
        t->runs[i] = runs;
        /* each run's format, whole */
        for (j = 0; j < p->nruns; j++) {
            owf_run *r = &runs[j + 2];
            *r = p->runs[j];
            if (r->fmt.font < 0) r->fmt.font = lv->chars.font;
            if (r->fmt.size <= 0) r->fmt.size = lv->chars.size > 0 ? lv->chars.size : deck->res->base.size;
            if (r->fmt.size > MAX_SIZE) r->fmt.size = MAX_SIZE;
            if (r->fmt.colour == OWF_COLOUR_AUTO) r->fmt.colour = lv->chars.colour;
            if (r->fmt.colour == OWF_COLOUR_AUTO) r->fmt.colour = auto_colour;
            r->fmt.size = (int)((long long)r->fmt.size * scale / 100);
            if (r->kind == OWF_RUN_FIELD) {
                r->kind = OWF_RUN_TEXT;
                r->text = (char *)(fields && fields[r->field <= OWF_FIELD_TIME ? r->field : 0]
                                   ? fields[r->field <= OWF_FIELD_TIME ? r->field : 0] : "");
                r->href = NULL;
            }
            if (r->kind == OWF_RUN_IMAGE) {
                /* pictures inside slide text aren't drawn yet */
                r->kind = OWF_RUN_TEXT;
                r->text = (char *)"";
            }
        }
        first = p->nruns ? runs[2].fmt : lv->chars;
        if (!p->nruns) {
            if (first.colour == OWF_COLOUR_AUTO) first.colour = auto_colour;
            if (first.font < 0) first.font = lv->chars.font;
            if (first.size <= 0) first.size = lv->chars.size > 0 ? lv->chars.size : deck->res->base.size;
            if (first.size > MAX_SIZE) first.size = MAX_SIZE;
            first.size = (int)((long long)first.size * scale / 100);
        }
        /* the bullet or number, and a tab to the text */
        if (level > 0 && lv->bullet) {
            int d;
            counters[level]++;
            for (d = level + 1; d <= OWF_TEXT_LEVELS; d++)
                counters[d] = 0;
            label(lv, (lv->start > 0 ? lv->start - 1 : 0) + counters[level], t->labels[i], sizeof t->labels[i]);
            runs[k].kind = OWF_RUN_TEXT;
            runs[k].text = t->labels[i];
            runs[k].fmt = first;
            runs[k].fmt.flags &= ~(unsigned)(OWF_UNDERLINE | OWF_STRIKE);
            if (lv->bullet_font >= 0 && !(lv->bullet & OWF_BULLET_NUMBER))
                runs[k].fmt.font = lv->bullet_font;
            if (lv->bullet_size > 0)
                runs[k].fmt.size = (int)((long long)first.size * bound(lv->bullet_size, 1, 1000) / 100);
            if (OWF_SHADE_SET(lv->bullet_colour))
                runs[k].fmt.colour = OWF_SHADE_RGB(lv->bullet_colour);
            runs[k].href = NULL;
            k++;
            runs[k].kind = OWF_RUN_TAB;
            runs[k].fmt = first;
            k++;
        } else if (level == 0) {
            int d;
            for (d = 1; d <= OWF_TEXT_LEVELS; d++)
                counters[d] = 0;
        }
        if (k < 2)
            memmove(&runs[k], &runs[2], (size_t)p->nruns * sizeof *runs);
        q->runs = runs;
        q->nruns = q->capruns = p->nruns + k;
        if (!q->nruns) {
            /* an empty paragraph is as tall as its level's text */
            runs[0].kind = OWF_RUN_TEXT;
            runs[0].text = (char *)"";
            runs[0].fmt = first;
            q->nruns = 1;
        }
        /* nothing past a page, so the sums below stay in 32 bits */
        q->fmt.indent_left = bound(q->fmt.indent_left, -MAX_SPAN, MAX_SPAN);
        q->fmt.indent_right = bound(q->fmt.indent_right, -MAX_SPAN, MAX_SPAN);
        q->fmt.indent_first = bound(q->fmt.indent_first, -MAX_SPAN, MAX_SPAN);
        q->fmt.line_spacing = bound(q->fmt.line_spacing, 0, 1000);
        for (j = 0; j < q->fmt.ntabs && j < OWF_MAX_TABS; j++)
            q->fmt.tabs[j].position = bound(q->fmt.tabs[j].position, 0, MAX_SPAN);
        q->fmt.space_before = (int)((long long)bound(q->fmt.space_before, 0, MAX_SPAN) * scale / 100);
        q->fmt.space_after = (int)((long long)bound(q->fmt.space_after, 0, MAX_SPAN) * scale / 100);
    }
    return 1;
}

/* ---- Frames ---- */

static uint32_t argb(unsigned long shade)
{
    return 0xff000000u | (uint32_t)OWF_SHADE_RGB(shade);
}

typedef struct {
    const owf_deck *deck;
    const owf_slide *slide;
    int index;
    const owf_master *master;
    opr_slide *out;
    int capframes;
    const int *scales;           /* per frame, from the last pass; NULL: 100 */
    const char *fields[4];
    char number[16], pages[16], date[64];
} builder;

/* One after the newest node: node ids count up from the root's. */
static ol_id last_node(const builder *b)
{
    ol_id id = b->out->frames[b->out->nframes - 1].node;
    while (ol_node_by_id(b->out->doc, id))
        id++;
    return id;
}

static opr_frame *add_frame(builder *b, const owf_obj *obj, int from_master)
{
    opr_frame *f;
    if (b->out->nframes == b->capframes) {
        int want = b->capframes ? b->capframes * 2 : 16;
        f = realloc(b->out->frames, (size_t)want * sizeof *f);
        if (!f)
            return NULL;
        b->out->frames = f;
        b->capframes = want;
    }
    f = &b->out->frames[b->out->nframes++];
    memset(f, 0, sizeof *f);
    f->obj = obj;
    f->from_master = from_master;
    f->scale = b->scales ? b->scales[b->out->nframes - 1] : 100;
    if (obj)
        f->box = *obj;
    return f;
}

static int is_shape_drawn_by_us(const owf_obj *o)
{
    return o && o->kind == OWF_OBJ_SHAPE && o->shape != OWF_SHAPE_RECT;
}

/* A frame's block at its box, with its fill and line when OpenLayout draws
 * them (rectangles and text frames). */
static ol_node *frame_node(builder *b, opr_frame *f, int x, int y, int w, int h, const owf_obj *o)
{
    ol_style s;
    ol_box_style box;
    ol_node *n = ol_node_append(b->out->doc, NULL, OL_ROLE_BLOCK, NULL);
    if (!n)
        return NULL;
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    ol_box_style_init(&box);
    box.flags = OL_BOX_ABSOLUTE;
    box.frame.x = TW(x);
    box.frame.y = TW(y);
    box.frame.width = TW(w > 0 ? w : 1);
    box.frame.height = TW(h > 0 ? h : 1);
    if (o && o->kind != OWF_OBJ_PICTURE && o->kind != OWF_OBJ_GROUP) {
        s.padding_left = TW(o->inset[0]);
        s.padding_top = TW(o->inset[1]);
        s.padding_right = TW(o->inset[2]);
        s.padding_bottom = TW(o->inset[3]);
    }
    if (o && !is_shape_drawn_by_us(o) && o->kind != OWF_OBJ_PICTURE) {
        if (OWF_SHADE_SET(o->look.fill))
            s.background = argb(o->look.fill);
        if (OWF_SHADE_SET(o->look.line)) {
            box.border_width = TW(o->look.line_width > 0 ? o->look.line_width : 10);
            box.border_argb = argb(o->look.line);
            box.border_sides = OL_BORDER_ALL;
        }
    }
    ol_node_set_style(n, &s);
    ol_node_set_box(n, &box);
    f->node = ol_node_id(n);
    return n;
}

static int add_text(builder *b, opr_frame *f, ol_node *n, const owf_obj *o, const owf_story *story, int with_levels)
{
    frame_text t;
    int ok;
    if (!story->nparas)
        return 1;
    if (!prepare_text(b->deck, b->master, o, story, with_levels, f->scale, b->fields, &t))
        return 0;
    ok = ow_layout_add_story(b->out->doc, n, b->deck->res, &t.story, b->fields);
    frame_text_free(&t);
    return ok;
}

/* A table's column widths onto its grid. */
static void table_columns(builder *b, const opr_frame *f, const owf_obj *o)
{
    ol_id id;
    ol_unit widths[OL_MAX_COLUMNS];
    int i, n;
    if (!o->col_widths || o->ncols <= 0)
        return;
    n = o->ncols < OL_MAX_COLUMNS ? o->ncols : OL_MAX_COLUMNS;
    for (i = 0; i < n; i++)
        widths[i] = TW(o->col_widths[i]);
    for (id = f->first; id < f->end; id++) {
        const ol_node *node = ol_node_by_id(b->out->doc, id);
        if (node && ol_node_role(node) == OL_ROLE_TABLE) {
            /* ours: built just now in this document */
            ol_node_set_columns((ol_node *)node, n, widths);
            break;
        }
    }
}

/* A picture's image node, filling the frame. */
static int add_picture(builder *b, ol_node *n, int image, int w, int h)
{
    ol_node *img;
    ol_style s;
    if (image < 0 || image >= b->deck->res->nimages)
        return 1;
    img = ol_node_append(b->out->doc, n, OL_ROLE_IMAGE, NULL);
    if (!img)
        return 0;
    ol_style_init(&s);
    s.display = OL_DISPLAY_INLINE;
    ol_node_set_style(img, &s);
    ol_node_set_image(img, image, TW(w > 0 ? w : 1), TW(h > 0 ? h : 1));
    return 1;
}

/* What an object kept but not drawn is called on its box. */
static const char *other_label(const owf_deck *deck, const owf_obj *o)
{
    const char *x = o->keep >= 0 && o->keep < deck->nkept ? deck->kept[o->keep].xml : "";
    if (strstr(x, "<draw:object") || strstr(x, "<draw:object-ole"))
        return "Embedded object";
    if (strstr(x, "<draw:plugin"))
        return "Sound or video";
    if (strstr(x, "<draw:connector"))
        return "Connector";
    if (strstr(x, "<dr3d:"))
        return "3-D object";
    return "Shape";
}

static int add_label(builder *b, ol_node *n, const char *text)
{
    owf_para p;
    owf_run r;
    owf_story s;
    memset(&p, 0, sizeof p);
    memset(&r, 0, sizeof r);
    owf_parafmt_init(&p.fmt);
    p.fmt.align = OWF_ALIGN_CENTRE;
    p.table_id = -1;
    r.kind = OWF_RUN_TEXT;
    r.text = (char *)text;
    owf_charfmt_init(&r.fmt);
    r.fmt.font = b->deck->res->base.font;
    r.fmt.size = 12 * OWF_TWIPS_PER_POINT;
    r.fmt.colour = 0x606060;
    p.runs = &r;
    p.nruns = p.capruns = 1;
    s.paras = &p;
    s.nparas = s.capparas = 1;
    return ow_layout_add_story(b->out->doc, n, b->deck->res, &s, NULL);
}

/* One object's frame and its content. */
static int add_object(builder *b, const owf_obj *o, int from_master, int slide_index, int obj_index)
{
    opr_frame *f;
    ol_node *n;
    owf_obj box = *o;

    if (o->kind == OWF_OBJ_GROUP || (o->flags & OWF_OBJ_HIDDEN))
        return 1;
    if (!from_master && slide_index >= 0)
        owf_deck_resolve(b->deck, slide_index, obj_index, &box, NULL);
    f = add_frame(b, o, from_master);
    if (!f)
        return 0;
    f->box = box;
    n = frame_node(b, f, box.x, box.y, box.width, box.height, o);
    if (!n)
        return 0;
    f->first = ol_node_id(n) + 1;
    switch (o->kind) {
    case OWF_OBJ_PICTURE:
        if (!add_picture(b, n, o->image, box.width, box.height))
            return 0;
        break;
    case OWF_OBJ_OTHER:
    case OWF_OBJ_CHART:
        if (o->image >= 0) {
            if (!add_picture(b, n, o->image, box.width, box.height))
                return 0;
        } else if (!OWF_SHADE_SET(o->look.fill) && !OWF_SHADE_SET(o->look.line) && !o->text.nparas) {
            /* a labelled box */
            ol_style s;
            ol_box_style bs;
            const ol_node *cn = ol_node_by_id(b->out->doc, f->node);
            s = *ol_node_style(cn);
            s.background = 0xfff0f0f0u;
            ol_node_set_style(n, &s);
            bs = *ol_node_box(cn);
            bs.border_width = TW(10);
            bs.border_argb = 0xff909090u;
            bs.border_sides = OL_BORDER_ALL;
            ol_node_set_box(n, &bs);
            if (!add_label(b, n, other_label(b->deck, o)))
                return 0;
        } else if (!add_text(b, f, n, o, &o->text, 1)) {
            return 0;
        }
        break;
    case OWF_OBJ_TABLE:
        if (!add_text(b, f, n, o, &o->text, 0))
            return 0;
        f->end = last_node(b);
        table_columns(b, f, o);
        break;
    default:
        if (!(o->flags & OWF_OBJ_EMPTY) && !add_text(b, f, n, o, &o->text, 1))
            return 0;
        break;
    }
    f->end = last_node(b);
    return 1;
}

/* A master's footer, date or number box, with the slide's text in it. */
static int add_field(builder *b, const owf_obj *o, const char *text)
{
    owf_obj copy = *o;
    owf_story story;
    owf_para para;
    owf_run run;
    opr_frame *f;
    ol_node *n;
    int ok;

    f = add_frame(b, o, 1);
    if (!f)
        return 0;
    n = frame_node(b, f, o->x, o->y, o->width, o->height, o);
    if (!n)
        return 0;
    f->first = ol_node_id(n) + 1;
    memset(&para, 0, sizeof para);
    memset(&run, 0, sizeof run);
    if (o->text.nparas) {
        para = o->text.paras[0];
        if (para.nruns)
            run.fmt = para.runs[0].fmt;
        else
            owf_charfmt_init(&run.fmt);
    } else {
        owf_parafmt_init(&para.fmt);
        owf_charfmt_init(&run.fmt);
        para.table_id = -1;
    }
    run.kind = OWF_RUN_TEXT;
    run.text = (char *)text;
    para.runs = &run;
    para.nruns = para.capruns = 1;
    story.paras = &para;
    story.nparas = story.capparas = 1;
    copy.levels = NULL;
    copy.caplevels = 0;
    ok = add_text(b, f, n, &copy, &story, 0);
    f->end = last_node(b);
    return ok;
}

static int build(builder *b)
{
    const owf_deck *d = b->deck;
    const owf_slide *s = b->slide;
    const owf_master *m = b->master;
    opr_frame *f;
    ol_node *n;
    unsigned long bg = s->background;
    int bg_image = s->background_image, i;

    b->out->nframes = 0;
    /* the background: the slide's, else its master's, else white */
    if (!OWF_SHADE_SET(bg) && bg_image < 0 && m) {
        bg = m->background;
        bg_image = m->background_image;
    }
    if (!OWF_SHADE_SET(bg))
        bg = OWF_SHADE(0xFFFFFF);
    f = add_frame(b, NULL, 1);
    if (!f)
        return 0;
    n = frame_node(b, f, 0, 0, d->width, d->height, NULL);
    if (!n)
        return 0;
    {
        ol_style st = *ol_node_style(n);
        st.background = argb(bg);
        ol_node_set_style(n, &st);
    }
    f->first = ol_node_id(n) + 1;
    if (bg_image >= 0 && !add_picture(b, n, bg_image, d->width, d->height))
        return 0;
    f->end = f->first + (bg_image >= 0 ? 1 : 0);

    /* the master's art, and the footer fields the slide shows */
    for (i = 0; m && i < m->nobjs; i++) {
        const owf_obj *o = &m->objs[i];
        int ok = 1;
        switch (o->placeholder) {
        case OWF_PH_NONE:
            if (o->kind != OWF_OBJ_GROUP)
                ok = add_object(b, o, 1, -1, i);
            break;
        case OWF_PH_FOOTER:
            if ((s->show & OWF_SHOW_FOOTER) && s->footer)
                ok = add_field(b, o, s->footer);
            break;
        case OWF_PH_DATE:
            if (s->show & OWF_SHOW_DATE)
                ok = add_field(b, o, s->date_text ? s->date_text : b->date);
            break;
        case OWF_PH_NUMBER:
            if (s->show & OWF_SHOW_NUMBER)
                ok = add_field(b, o, b->number);
            break;
        default:
            break;               /* the other placeholders are the slides' */
        }
        if (!ok)
            return 0;
    }
    for (i = 0; i < s->nobjs; i++)
        if (!add_object(b, &s->objs[i], 0, b->index, i))
            return 0;
    return 1;
}

/* How tall a frame's content is, and how tall it may be. */
static void content_height(const opr_slide *s, const opr_frame *f, ol_unit *content, ol_unit *room)
{
    const ol_node *frame = ol_node_by_id(s->doc, f->node);
    const ol_box_style *box = ol_node_box(frame);
    const ol_style *st = ol_node_style(frame);
    ol_rect fb = ol_node_bounds(frame);
    ol_unit top = fb.y + st->padding_top, bottom = top;
    ol_id id;
    for (id = f->first; id < f->end; id++) {
        const ol_node *n = ol_node_by_id(s->doc, id);
        ol_rect r;
        if (!n || ol_node_parent(n) != frame)
            continue;
        r = ol_node_bounds(n);
        if (r.y + r.height + ol_node_style(n)->margin_bottom > bottom)
            bottom = r.y + r.height + ol_node_style(n)->margin_bottom;
    }
    *content = bottom - top;
    *room = box->frame.height - st->padding_top - st->padding_bottom;
}

static int lay_out(opr_slide *s, ol_measure_text_fn measure, void *userdata)
{
    ol_page page;
    memset(&page, 0, sizeof page);
    page.width = TW(s->deck->width);
    page.height = TW(s->deck->height);
    return ol_layout_pages(s->doc, &page, measure, userdata);
}

void opr_slide_free(opr_slide *s)
{
    if (!s)
        return;
    ol_document_free(s->doc);
    free(s->frames);
    free(s);
}

opr_slide *opr_slide_layout(const owf_deck *deck, int slide, ol_measure_text_fn measure, void *userdata,
                            const char *today)
{
    builder b;
    opr_slide *s;
    int *scales = NULL, pass, i;

    if (!deck || slide < 0 || slide >= deck->nslides || !deck->nmasters)
        return NULL;
    if (!measure) {
        measure = opr_measure;
        userdata = (void *)deck;
    }
    s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->deck = deck;
    s->slide = slide;
    memset(&b, 0, sizeof b);
    b.deck = deck;
    b.slide = &deck->slides[slide];
    b.index = slide;
    b.master = owf_deck_slide_master(deck, slide);
    b.out = s;
    snprintf(b.number, sizeof b.number, "%d", slide + 1);
    if (today) {
        snprintf(b.date, sizeof b.date, "%s", today);
    } else {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        if (t)
            snprintf(b.date, sizeof b.date, "%d/%d/%04d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
    }
    snprintf(b.pages, sizeof b.pages, "%d", deck->nslides);
    b.fields[0] = b.number;
    b.fields[1] = b.pages;
    b.fields[2] = b.date;
    b.fields[3] = NULL;

    /* lay out; then again while text that shrinks to fit doesn't (four
       times at most), then once more for text anchored lower in its box */
    for (pass = 0; pass < 6; pass++) {
        int again = 0;
        ol_document_free(s->doc);
        s->doc = ol_document_new();
        if (!s->doc || !build(&b) || !lay_out(s, measure, userdata))
            goto fail;
        if (!scales) {
            scales = malloc((size_t)(s->nframes > 0 ? s->nframes : 1) * sizeof *scales);
            if (!scales)
                goto fail;
            for (i = 0; i < s->nframes; i++)
                scales[i] = 100;
            b.scales = scales;
        }
        if (pass >= 4)
            break;
        for (i = 0; i < s->nframes; i++) {
            const opr_frame *f = &s->frames[i];
            ol_unit content, room;
            if (!f->obj || f->obj->autofit != OWF_AUTOFIT_SHRINK || !f->obj->text.nparas)
                continue;
            content_height(s, f, &content, &room);
            if (content > room && scales[i] > MIN_SCALE) {
                scales[i] = scales[i] * 9 / 10;
                if (scales[i] < MIN_SCALE)
                    scales[i] = MIN_SCALE;
                again = 1;
            }
        }
        if (!again)
            break;
    }
    /* anchored to the middle or the bottom: the room left, above the text */
    {
        int moved = 0;
        for (i = 0; i < s->nframes; i++) {
            const opr_frame *f = &s->frames[i];
            ol_unit content, room;
            const ol_node *frame;
            ol_style st;
            if (!f->obj || f->obj->valign == OWF_VALIGN_TOP || f->first >= f->end)
                continue;
            content_height(s, f, &content, &room);
            if (content >= room)
                continue;
            frame = ol_node_by_id(s->doc, f->node);
            st = *ol_node_style(frame);
            st.padding_top += f->obj->valign == OWF_VALIGN_BOTTOM ? room - content : (room - content) / 2;
            /* ours: built just now in this document */
            ol_node_set_style((ol_node *)frame, &st);
            moved = 1;
        }
        if (moved && !lay_out(s, measure, userdata))
            goto fail;
    }
    s->overflow = ol_page_count(s->doc) > 1;
    free(scales);
    return s;
fail:
    free(scales);
    opr_slide_free(s);
    return NULL;
}

int opr_slide_frame_of(const opr_slide *s, const ol_display_op *op)
{
    ol_id id;
    const ol_node *n, *root;
    int i;
    if (!s || !op)
        return -1;
    switch (op->type) {
    case OL_OP_TEXT: id = op->u.text.node_id; break;
    case OL_OP_FILL_RECT: id = op->u.fill_rect.node_id; break;
    case OL_OP_IMAGE: id = op->u.image.node_id; break;
    case OL_OP_RULE: id = op->u.rule.node_id; break;
    default: return -1;
    }
    root = ol_document_root_const(s->doc);
    n = ol_node_by_id(s->doc, id);
    while (n && ol_node_parent(n) && ol_node_parent(n) != root)
        n = ol_node_parent(n);
    if (!n)
        return -1;
    id = ol_node_id(n);
    for (i = 0; i < s->nframes; i++)
        if (s->frames[i].node == id)
            return i;
    return -1;
}

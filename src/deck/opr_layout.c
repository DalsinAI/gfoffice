/*
 * GFOpenPresentation's core: a slide laid out by GFOpenLayout (see
 * openpresentation_core.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
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

/* ---- A frame's text, made ready for GFOpenWrite's paragraph code ---- */

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
    ol_measure_text_fn measure;
    void *measure_userdata;
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
    f->text_overflow = -1;
    f->obj = obj;
    f->from_master = from_master;
    /* text PowerPoint shrank to fit starts at the size it shrank it to */
    f->scale = b->scales ? b->scales[b->out->nframes - 1]
             : obj && obj->autofit == OWF_AUTOFIT_SHRINK && obj->font_scale >= 10 && obj->font_scale < 100
               ? obj->font_scale : 100;
    if (obj)
        f->box = *obj;
    return f;
}

static int is_openlayout_shape(const owf_obj *o)
{
    return o && o->kind == OWF_OBJ_SHAPE;
}

static unsigned shape_stroke_style(unsigned char line)
{
    switch (line) {
    case OWF_LINE_DOTTED:
        return OL_STROKE_DOTTED;
    case OWF_LINE_DASH_DOT:
    case OWF_LINE_MEDIUM_DASH_DOT:
    case OWF_LINE_DASH_DOT_DOT:
    case OWF_LINE_MEDIUM_DASH_DOT_DOT:
    case OWF_LINE_SLANTED_DASH_DOT:
        return OL_STROKE_DASH_DOT;
    case OWF_LINE_DASHED:
    case OWF_LINE_MEDIUM_DASHED:
        return OL_STROKE_DASHED;
    default:
        return OL_STROKE_SOLID;
    }
}

/* Put the deck's shape into the shared, normalized OpenLayout geometry.
 * Screen, PDF and every deck consumer then see exactly the same path. */
static int set_shape(ol_node *node, const owf_obj *o)
{
    ol_shape_style s;
    ol_point points[OL_MAX_SHAPE_POINTS];
    int count = 0, k, w, h;
    static const ol_point triangle[] = {{500, 0}, {1000, 1000}, {0, 1000}};
    static const ol_point diamond[] = {{500, 0}, {1000, 500}, {500, 1000}, {0, 500}};
    static const ol_point arrow[] = {
        {0, 250}, {750, 250}, {750, 0}, {1000, 500}, {750, 1000}, {750, 750}, {0, 750}
    };

    if (!is_openlayout_shape(o))
        return 1;
    ol_shape_style_init(&s);
    s.fill_argb = OWF_SHADE_SET(o->look.fill) ? argb(o->look.fill) : 0;
    s.stroke_argb = OWF_SHADE_SET(o->look.line) ? argb(o->look.line) : 0;
    s.stroke_width = TW(o->look.line_width > 0 ? o->look.line_width : 5);
    s.stroke_style = shape_stroke_style(o->look.line_style);
    s.rotation_tenths = o->rotation;
    if (o->look.arrows & OWF_ARROW_START) s.flags |= OL_SHAPE_ARROW_START;
    if (o->look.arrows & OWF_ARROW_END) s.flags |= OL_SHAPE_ARROW_END;
    w = o->width > 0 ? o->width : 1;
    h = o->height > 0 ? o->height : 1;
    switch (o->shape) {
    case OWF_SHAPE_RECT:
        s.type = OL_SHAPE_RECT;
        break;
    case OWF_SHAPE_ROUNDRECT:
        s.type = OL_SHAPE_ROUND_RECT;
        s.radius_permille = bound((int)((long long)o->radius * OL_SHAPE_COORDS / (w < h ? w : h)), 0, 500);
        break;
    case OWF_SHAPE_ELLIPSE:
        s.type = OL_SHAPE_ELLIPSE;
        break;
    case OWF_SHAPE_TRIANGLE:
        s.type = OL_SHAPE_POLYGON;
        memcpy(points, triangle, sizeof triangle);
        count = (int)(sizeof triangle / sizeof *triangle);
        break;
    case OWF_SHAPE_DIAMOND:
        s.type = OL_SHAPE_POLYGON;
        memcpy(points, diamond, sizeof diamond);
        count = (int)(sizeof diamond / sizeof *diamond);
        break;
    case OWF_SHAPE_ARROW:
        s.type = OL_SHAPE_POLYGON;
        memcpy(points, arrow, sizeof arrow);
        count = (int)(sizeof arrow / sizeof *arrow);
        break;
    case OWF_SHAPE_LINE:
        s.type = OL_SHAPE_LINE;
        points[0].x = (o->flags & OWF_OBJ_FLIP_H) ? OL_SHAPE_COORDS : 0;
        points[0].y = (o->flags & OWF_OBJ_FLIP_V) ? OL_SHAPE_COORDS : 0;
        points[1].x = OL_SHAPE_COORDS - points[0].x;
        points[1].y = OL_SHAPE_COORDS - points[0].y;
        count = 2;
        break;
    case OWF_SHAPE_POLYGON:
        if (!o->points || o->npoints < 3)
            return 1;
        s.type = OL_SHAPE_POLYGON;
        count = o->npoints < OL_MAX_SHAPE_POINTS ? o->npoints : OL_MAX_SHAPE_POINTS;
        for (k = 0; k < count; k++) {
            points[k].x = (int32_t)((long long)o->points[k * 2] * OL_SHAPE_COORDS / w);
            points[k].y = (int32_t)((long long)o->points[k * 2 + 1] * OL_SHAPE_COORDS / h);
        }
        break;
    default:
        return 1;
    }
    if (o->shape != OWF_SHAPE_LINE) {
        for (k = 0; k < count; k++) {
            if (o->flags & OWF_OBJ_FLIP_H) points[k].x = OL_SHAPE_COORDS - points[k].x;
            if (o->flags & OWF_OBJ_FLIP_V) points[k].y = OL_SHAPE_COORDS - points[k].y;
        }
    }
    return ol_node_set_shape(node, &s, count, count ? points : NULL);
}

/* A frame's block at its box, with its fill and line when GFOpenLayout draws
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
    if (o && o->kind == OWF_OBJ_PICTURE &&
        (o->crop[0] || o->crop[1] || o->crop[2] || o->crop[3]) &&
        o->crop[0] >= 0 && o->crop[1] >= 0 && o->crop[2] >= 0 && o->crop[3] >= 0 &&
        o->crop[0] + o->crop[2] < OL_SHAPE_COORDS &&
        o->crop[1] + o->crop[3] < OL_SHAPE_COORDS)
        box.flags |= OL_BOX_CLIP_CONTENT;
    if (o && o->kind == OWF_OBJ_TEXT && o->flow_story >= 0)
        box.flags |= OL_BOX_CLIP_CONTENT;
    if (o && o->text_columns >= 2 && o->text_columns <= 16 && o->text_column_gap >= 0) {
        box.text_columns = o->text_columns;
        box.text_column_gap = TW(o->text_column_gap);
        box.flags |= OL_BOX_CLIP_CONTENT;
    }
    if (o && o->kind != OWF_OBJ_PICTURE && o->kind != OWF_OBJ_GROUP) {
        s.padding_left = TW(o->inset[0]);
        s.padding_top = TW(o->inset[1]);
        s.padding_right = TW(o->inset[2]);
        s.padding_bottom = TW(o->inset[3]);
    }
    if (o && !is_openlayout_shape(o) && o->kind != OWF_OBJ_PICTURE) {
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

/* ---- Linked publication stories ----
 *
 * Publisher stores one story once and points several frames at it. The
 * imported deck deliberately keeps a copy in each object for ordinary deck
 * editing, plus flow_story as their shared identity. The adapter below uses
 * GFOpenLayout's real page cut to split the prepared story; it does not
 * estimate characters or reproduce line breaking. */

static void owned_story_free(owf_story *story)
{
    int i, j;
    if (!story) return;
    for (i = 0; i < story->nparas; ++i) {
        owf_para *p = &story->paras[i];
        for (j = 0; j < p->nruns; ++j) {
            free(p->runs[j].text);
            free(p->runs[j].href);
        }
        free(p->runs);
        free(p->style);
        free(p->bookmarks);
    }
    free(story->paras);
    memset(story, 0, sizeof *story);
}

static int copy_run_part(owf_para *to, const owf_run *r, size_t first, size_t last)
{
    size_t length = r->kind == OWF_RUN_TEXT && r->text ? strlen(r->text) : 0;
    if (r->kind == OWF_RUN_TEXT) {
        if (first > length) first = length;
        if (last > length) last = length;
        if (last < first) last = first;
        return r->href ? owf_para_add_link_text(to, &r->fmt, r->text ? r->text + first : "",
                                                last - first, r->href)
                       : owf_para_add_text(to, &r->fmt, r->text ? r->text + first : "", last - first);
    }
    if (first) return 0;
    if (r->kind == OWF_RUN_IMAGE) return owf_para_add_image(to, &r->fmt, r->image);
    if (r->kind == OWF_RUN_FRAME) return owf_para_add_frame(to, &r->fmt, r->image);
    return owf_para_add_special(to, &r->fmt, r->kind, r->field);
}

static int copy_para_shell(owf_story *to, const owf_para *from, owf_para **out)
{
    owf_para *p = owf_story_add(to, &from->fmt);
    if (!p) return 0;
    p->table_id = from->table_id;
    p->table_row = from->table_row;
    p->table_col = from->table_col;
    p->table_cols = from->table_cols;
    p->cell_shading = from->cell_shading;
    p->section_columns = from->section_columns;
    p->section_gap = from->section_gap;
    p->section_break = from->section_break;
    *out = p;
    return 1;
}

static int story_copy_all(owf_story *to, const owf_story *from)
{
    int i, j;
    memset(to, 0, sizeof *to);
    for (i = 0; i < from->nparas; ++i) {
        owf_para *p;
        if (!copy_para_shell(to, &from->paras[i], &p)) goto fail;
        for (j = 0; j < from->paras[i].nruns; ++j) {
            const owf_run *r = &from->paras[i].runs[j];
            size_t n = r->kind == OWF_RUN_TEXT && r->text ? strlen(r->text) : 1;
            if (copy_run_part(p, r, 0, n) < 0) goto fail;
        }
    }
    return 1;
fail:
    owned_story_free(to);
    return 0;
}

static int position_ends_para(const owf_story *story, const ow_position *at)
{
    const owf_para *p;
    const owf_run *r;
    size_t n;
    if (!story || !at || at->paragraph < 0 || at->paragraph >= story->nparas) return 1;
    p = &story->paras[at->paragraph];
    if (at->run < 0 || at->run >= p->nruns) return p->nruns == 0;
    if (at->run + 1 < p->nruns) return 0;
    r = &p->runs[at->run];
    n = r->kind == OWF_RUN_TEXT && r->text ? strlen(r->text) : 1;
    return at->byte_offset >= n;
}

/* Split at an exclusive position. Both results own all their storage. */
static int story_split(const owf_story *story, const ow_position *at,
                       owf_story *prefix, owf_story *suffix)
{
    int i, j, suffix_first;
    memset(prefix, 0, sizeof *prefix);
    memset(suffix, 0, sizeof *suffix);
    if (!story || !at || at->paragraph < 0 || at->paragraph >= story->nparas) return 0;
    for (i = 0; i <= at->paragraph; ++i) {
        const owf_para *src = &story->paras[i];
        owf_para *dst;
        int last_run = i == at->paragraph ? at->run : src->nruns - 1;
        if (!copy_para_shell(prefix, src, &dst)) goto fail;
        for (j = 0; j < src->nruns && j <= last_run; ++j) {
            const owf_run *r = &src->runs[j];
            size_t n = r->kind == OWF_RUN_TEXT && r->text ? strlen(r->text) : 1;
            size_t end = i == at->paragraph && j == at->run ? at->byte_offset : n;
            if (copy_run_part(dst, r, 0, end) < 0) goto fail;
        }
    }
    suffix_first = at->paragraph + (position_ends_para(story, at) ? 1 : 0);
    for (i = suffix_first; i < story->nparas; ++i) {
        const owf_para *src = &story->paras[i];
        owf_para *dst;
        int first_run = i == at->paragraph ? at->run : 0;
        if (!copy_para_shell(suffix, src, &dst)) goto fail;
        for (j = first_run; j < src->nruns; ++j) {
            const owf_run *r = &src->runs[j];
            size_t n = r->kind == OWF_RUN_TEXT && r->text ? strlen(r->text) : 1;
            size_t start = i == at->paragraph && j == at->run ? at->byte_offset : 0;
            if (copy_run_part(dst, r, start, n) < 0) goto fail;
        }
    }
    return 1;
fail:
    owned_story_free(prefix);
    owned_story_free(suffix);
    return 0;
}

static ow_position story_end(const owf_story *story)
{
    ow_position p;
    const owf_para *para;
    const owf_run *run;
    memset(&p, 0, sizeof p);
    if (!story || story->nparas <= 0) return p;
    p.paragraph = story->nparas - 1;
    para = &story->paras[p.paragraph];
    if (para->nruns <= 0) { p.run = -1; return p; }
    p.run = para->nruns - 1;
    run = &para->runs[p.run];
    p.byte_offset = run->kind == OWF_RUN_TEXT && run->text ? strlen(run->text) : 1;
    return p;
}

static int flow_fit_end(const builder *b, const owf_story *story, const owf_obj *frame,
                        ow_position *end, int *fits)
{
    owf_doc doc;
    ow_layout *layout;
    int width = frame->width - frame->inset[0] - frame->inset[2];
    int height = frame->height - frame->inset[1] - frame->inset[3];
    int columns = frame->text_columns >= 2 && frame->text_columns <= 16 ? frame->text_columns : 1;
    int gap = frame->text_column_gap > 0 ? frame->text_column_gap : 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (columns > 1 && width > gap * (columns - 1))
        width = (width - gap * (columns - 1)) / columns;
    else
        columns = 1;
    doc = *b->deck->res;
    doc.body = *story;
    memset(&doc.header, 0, sizeof doc.header);
    memset(&doc.footer, 0, sizeof doc.footer);
    memset(&doc.header_left, 0, sizeof doc.header_left);
    memset(&doc.footer_left, 0, sizeof doc.footer_left);
    doc.nframes = doc.capframes = 0;
    doc.frames = NULL;
    memset(&doc.page, 0, sizeof doc.page);
    doc.page.width = width;
    doc.page.height = height;
    layout = ow_layout_new(&doc, b->measure, b->measure_userdata);
    if (!layout) return 0;
    if (fits) *fits = ow_layout_page_count(layout) <= columns;
    if (ow_layout_page_count(layout) <= columns)
        *end = story_end(story);
    else if (!ow_layout_page_end(layout, columns - 1, end)) {
        ow_layout_free(layout);
        return 0;
    }
    ow_layout_free(layout);
    return 1;
}

/* The part of prepared that belongs in this object. */
static int linked_story_slice(const builder *b, int slide_index, int obj_index,
                              const owf_obj *object, const owf_story *prepared,
                              owf_story *out, int *linked)
{
    owf_story remaining;
    int page, index, total = 0;
    *linked = 0;
    memset(out, 0, sizeof *out);
    if (!object || object->flow_story < 0 || slide_index < 0) return 1;
    for (page = 0; page < b->deck->nslides; ++page)
        for (index = 0; index < b->deck->slides[page].nobjs; ++index) {
            const owf_obj *o = &b->deck->slides[page].objs[index];
            if (o->kind == OWF_OBJ_TEXT && o->flow_story == object->flow_story) total++;
        }
    if (total < 2) return 1;
    *linked = 1;
    if (!story_copy_all(&remaining, prepared)) return 0;
    for (page = 0; page < b->deck->nslides; ++page) {
        const owf_slide *slide = &b->deck->slides[page];
        for (index = 0; index < slide->nobjs; ++index) {
            const owf_obj *o = &slide->objs[index];
            ow_position end;
            owf_story prefix, suffix;
            int is_target, later = 0, p, k;
            if (o->kind != OWF_OBJ_TEXT || o->flow_story != object->flow_story) continue;
            is_target = page == slide_index && index == obj_index;
            for (p = page; p < b->deck->nslides && !later; ++p) {
                int from = p == page ? index + 1 : 0;
                for (k = from; k < b->deck->slides[p].nobjs; ++k) {
                    const owf_obj *q = &b->deck->slides[p].objs[k];
                    if (q->kind == OWF_OBJ_TEXT && q->flow_story == object->flow_story) { later = 1; break; }
                }
            }
            if (is_target && !later) { *out = remaining; return 1; }
            if (!remaining.nparas) {
                if (is_target) return 1;
                continue;
            }
            if (!flow_fit_end(b, &remaining, o, &end, NULL) ||
                !story_split(&remaining, &end, &prefix, &suffix)) {
                owned_story_free(&remaining);
                return 0;
            }
            owned_story_free(&remaining);
            if (is_target) {
                owned_story_free(&suffix);
                *out = prefix;
                return 1;
            }
            owned_story_free(&prefix);
            remaining = suffix;
        }
    }
    owned_story_free(&remaining);
    return 0;
}

static const owf_story *linked_story_source(const builder *b, const owf_obj *object,
                                            const owf_story *own)
{
    int page, index;
    if (!object || object->flow_story < 0 || own->nparas) return own;
    for (page = 0; page < b->deck->nslides; ++page)
        for (index = 0; index < b->deck->slides[page].nobjs; ++index) {
            const owf_obj *o = &b->deck->slides[page].objs[index];
            if (o->kind == OWF_OBJ_TEXT && o->flow_story == object->flow_story && o->text.nparas)
                return &o->text;
        }
    return own;
}

static int add_text(builder *b, opr_frame *f, ol_node *n, const owf_obj *o, const owf_story *story, int with_levels,
                    int slide_index, int obj_index)
{
    frame_text t;
    owf_story slice;
    const owf_story *draw;
    int linked = 0;
    int ok;
    story = linked_story_source(b, o, story);
    if (!story->nparas)
        return 1;
    if (!prepare_text(b->deck, b->master, o, story, with_levels, f->scale, b->fields, &t))
        return 0;
    if (!linked_story_slice(b, slide_index, obj_index, o, &t.story, &slice, &linked)) {
        frame_text_free(&t);
        return 0;
    }
    draw = linked ? &slice : &t.story;
    ok = ow_layout_add_story(b->out->doc, n, b->deck->res, draw, b->fields);
    if (linked) owned_story_free(&slice);
    frame_text_free(&t);
    return ok;
}

int opr_frame_text_overflow(opr_slide *s, int frame,
                            ol_measure_text_fn measure, void *userdata)
{
    opr_frame *f;
    builder b;
    frame_text prepared;
    owf_story slice;
    const owf_story *story, *draw;
    ow_position end;
    char number[24], pages[24];
    int linked = 0, fits = 1, obj_index;
    if (!s || frame < 0 || frame >= s->nframes)
        return -1;
    f = &s->frames[frame];
    if (f->text_overflow >= 0)
        return f->text_overflow;
    if (!f->obj || f->from_master || f->obj->kind != OWF_OBJ_TEXT) {
        f->text_overflow = 0;
        return 0;
    }
    if (!measure) {
        measure = opr_measure;
        userdata = (void *)s->deck;
    }
    memset(&b, 0, sizeof b);
    b.deck = s->deck;
    b.slide = &s->deck->slides[s->slide];
    b.index = s->slide;
    b.master = owf_deck_slide_master(s->deck, s->slide);
    b.out = s;
    b.measure = measure;
    b.measure_userdata = userdata;
    snprintf(number, sizeof number, "%d", s->slide + 1);
    snprintf(pages, sizeof pages, "%d", s->deck->nslides);
    b.fields[0] = number;
    b.fields[1] = pages;
    b.fields[2] = "";
    b.fields[3] = NULL;
    obj_index = (int)(f->obj - b.slide->objs);
    story = linked_story_source(&b, f->obj, &f->obj->text);
    if (!story->nparas) {
        f->text_overflow = 0;
        return 0;
    }
    if (!prepare_text(b.deck, b.master, f->obj, story, 1, f->scale, b.fields, &prepared))
        return -1;
    if (!linked_story_slice(&b, s->slide, obj_index, f->obj, &prepared.story, &slice, &linked)) {
        frame_text_free(&prepared);
        return -1;
    }
    draw = linked ? &slice : &prepared.story;
    if (!flow_fit_end(&b, draw, &f->box, &end, &fits)) {
        if (linked) owned_story_free(&slice);
        frame_text_free(&prepared);
        return -1;
    }
    if (linked) owned_story_free(&slice);
    frame_text_free(&prepared);
    f->text_overflow = !fits;
    return f->text_overflow;
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
static int add_picture(builder *b, ol_node *n, int image, const int *crop, int w, int h)
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
    if (crop && (crop[0] || crop[1] || crop[2] || crop[3])) {
        ol_image_crop c;
        c.left = crop[0];
        c.top = crop[1];
        c.right = crop[2];
        c.bottom = crop[3];
        if (!ol_node_set_image_crop(img, &c))
            return 0;
    }
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
    if (!set_shape(n, &box))
        return 0;
    f->first = ol_node_id(n) + 1;
    switch (o->kind) {
    case OWF_OBJ_PICTURE:
        if (!add_picture(b, n, o->image, o->crop, box.width, box.height))
            return 0;
        break;
    case OWF_OBJ_OTHER:
    case OWF_OBJ_CHART:
        if (o->image >= 0) {
            if (!add_picture(b, n, o->image, o->crop, box.width, box.height))
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
        } else if (!add_text(b, f, n, o, &o->text, 1, slide_index, obj_index)) {
            return 0;
        }
        break;
    case OWF_OBJ_TABLE:
        if (!add_text(b, f, n, o, &o->text, 0, slide_index, obj_index))
            return 0;
        f->end = last_node(b);
        table_columns(b, f, o);
        break;
    default:
        if (!(o->flags & OWF_OBJ_EMPTY) && !add_text(b, f, n, o, &o->text, 1, slide_index, obj_index))
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
    ok = add_text(b, f, n, &copy, &story, 0, -1, -1);
    f->end = last_node(b);
    return ok;
}

static int frame_accepts_exclusions(const owf_obj *o)
{
    return o && (o->kind == OWF_OBJ_TEXT ||
                 (o->kind == OWF_OBJ_SHAPE && o->shape != OWF_SHAPE_LINE && o->text.nparas));
}

/* GFOpenLayout owns both the line fitting and, from 0.17, closed-shape
 * contour bands. The deck adapter only translates page coordinates into
 * each affected text frame; the object remains one display-list frame. */
static int apply_exclusions(builder *b)
{
    ol_exclusion *items;
    int target, obstacle;
    if (b->out->nframes <= 0)
        return 1;
    items = malloc((size_t)OL_MAX_EXCLUSIONS * sizeof *items);
    if (!items)
        return 0;
    for (target = 0; target < b->out->nframes; ++target) {
        const opr_frame *tf = &b->out->frames[target];
        ol_node *node;
        int count = 0;
        if (!frame_accepts_exclusions(tf->obj))
            continue;
        node = (ol_node *)ol_node_by_id(b->out->doc, tf->node);
        if (!node) {
            free(items);
            return 0;
        }
        for (obstacle = 0; obstacle < b->out->nframes; ++obstacle) {
            const opr_frame *of = &b->out->frames[obstacle];
            const owf_obj *o = &of->box;
            long left, top, right, bottom;
            ol_exclusion *e;
            if (obstacle == target || !of->obj || o->wrap == OWF_WRAP_NONE)
                continue;
            if (o->wrap == OWF_WRAP_TIGHT && o->kind == OWF_OBJ_SHAPE &&
                o->shape != OWF_SHAPE_LINE) {
                const ol_node *shape = ol_node_by_id(b->out->doc, of->node);
                ol_rect bounds;
                ol_unit gap[4];
                ol_exclusion contour[OL_CONTOUR_BANDS_DEFAULT];
                int made, k;
                bounds.x = TW(o->x - tf->box.x);
                bounds.y = TW(o->y - tf->box.y);
                bounds.width = TW(o->width > 0 ? o->width : 1);
                bounds.height = TW(o->height > 0 ? o->height : 1);
                for (k = 0; k < 4; ++k) gap[k] = TW(o->wrap_gap[k]);
                made = ol_node_shape_exclusions(shape, &bounds, gap,
                                                 OL_CONTOUR_BANDS_DEFAULT,
                                                 contour,
                                                 OL_CONTOUR_BANDS_DEFAULT);
                if (made > 0) {
                    for (k = 0; k < made; ++k) {
                        const ol_exclusion *ce = &contour[k];
                        int64_t x0 = (int64_t)ce->bounds.x - ce->gap[3];
                        int64_t y0 = (int64_t)ce->bounds.y - ce->gap[0];
                        int64_t x1 = (int64_t)ce->bounds.x + ce->bounds.width + ce->gap[1];
                        int64_t y1 = (int64_t)ce->bounds.y + ce->bounds.height + ce->gap[2];
                        if (x1 <= 0 || y1 <= 0 || x0 >= TW(tf->box.width) ||
                            y0 >= TW(tf->box.height))
                            continue;
                        if (count >= OL_MAX_EXCLUSIONS) {
                            free(items);
                            return 0;
                        }
                        items[count++] = *ce;
                    }
                    continue;
                }
                /* Unknown or unusable geometry falls back to the safe
                 * square exclusion below. */
            }
            left = (long)o->x - o->wrap_gap[3];
            top = (long)o->y - o->wrap_gap[0];
            right = (long)o->x + o->width + o->wrap_gap[1];
            bottom = (long)o->y + o->height + o->wrap_gap[2];
            if (right <= tf->box.x || bottom <= tf->box.y ||
                left >= (long)tf->box.x + tf->box.width ||
                top >= (long)tf->box.y + tf->box.height)
                continue;
            if (count >= OL_MAX_EXCLUSIONS) {
                free(items);
                return 0;
            }
            e = &items[count++];
            memset(e, 0, sizeof *e);
            e->bounds.x = TW(o->x - tf->box.x);
            e->bounds.y = TW(o->y - tf->box.y);
            e->bounds.width = TW(o->width > 0 ? o->width : 1);
            e->bounds.height = TW(o->height > 0 ? o->height : 1);
            e->wrap = o->wrap == OWF_WRAP_TOP_BOTTOM ? OL_WRAP_TOP_BOTTOM : OL_WRAP_SQUARE;
            e->gap[0] = TW(o->wrap_gap[0]);
            e->gap[1] = TW(o->wrap_gap[1]);
            e->gap[2] = TW(o->wrap_gap[2]);
            e->gap[3] = TW(o->wrap_gap[3]);
        }
        if (count && !ol_node_set_exclusions(node, count, items)) {
            free(items);
            return 0;
        }
    }
    free(items);
    return 1;
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
    if (bg_image >= 0 && !add_picture(b, n, bg_image, NULL, d->width, d->height))
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
    return apply_exclusions(b);
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
    b.measure = measure;
    b.measure_userdata = userdata;
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
                scales[i] = s->frames[i].scale;
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
    case OL_OP_SHAPE: id = op->u.fill_rect.node_id; break;
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

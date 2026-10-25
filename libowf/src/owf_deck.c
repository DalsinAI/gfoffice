/*
 * libowf: decks (owf_deck.h): building, changing and freeing them, what an
 * object takes from its layout and master, and the deck formats' table.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_deck_int.h"

#include <stdlib.h>
#include <string.h>

/* ---- Small helpers ---- */

char *owf_deck_strdup(const char *s, size_t length)
{
    char *out = malloc(length + 1);
    if (!out)
        return NULL;
    memcpy(out, s, length);
    out[length] = 0;
    return out;
}

int owf_deck_set_text(char **field, const char *utf8)
{
    char *copy = NULL;
    if (utf8 && !(copy = owf_deck_strdup(utf8, strlen(utf8))))
        return -1;
    free(*field);
    *field = copy;
    return 0;
}

void owf_deck_free_story(owf_story *story)
{
    int i, j;
    for (i = 0; i < story->nparas; i++) {
        for (j = 0; j < story->paras[i].nruns; j++) {
            free(story->paras[i].runs[j].text);
            free(story->paras[i].runs[j].href);
        }
        free(story->paras[i].runs);
    }
    free(story->paras);
    story->paras = NULL;
    story->nparas = story->capparas = 0;
}

/* Grows an array of count items to hold one more; 0 when memory runs out
 * or the limit is reached. */
static int grow(void **items, int *count, int *capacity, size_t size, int limit)
{
    void *p;
    int want;
    if (*count >= limit)
        return 0;
    if (*count < *capacity)
        return 1;
    want = *capacity ? *capacity * 2 : 8;
    if (want > limit)
        want = limit;
    p = realloc(*items, (size_t)want * size);
    if (!p)
        return 0;
    *items = p;
    *capacity = want;
    return 1;
}

/* ---- Objects ---- */

void owf_textlevel_init(owf_textlevel *level)
{
    memset(level, 0, sizeof *level);
    owf_charfmt_init(&level->chars);
    owf_parafmt_init(&level->para);
    level->bullet_font = -1;
}

void owf_obj_init(owf_obj *obj, owf_obj_kind kind)
{
    memset(obj, 0, sizeof *obj);
    obj->kind = kind;
    obj->parent = -1;
    obj->image = -1;
    obj->keep = -1;
    obj->valign = OWF_VALIGN_TOP;
    /* ODF's and Impress's default text inset: 0.25 cm left and right, 0.125 cm top and bottom */
    obj->inset[0] = obj->inset[2] = 142;
    obj->inset[1] = obj->inset[3] = 71;
}

void owf_obj_clear(owf_obj *obj)
{
    owf_deck_free_story(&obj->text);
    free(obj->levels);
    free(obj->list);
    free(obj->points);
    free(obj->col_widths);
    free(obj->name);
    free(obj->alt);
    free(obj->xml_id);
    memset(obj, 0, sizeof *obj);
    obj->parent = -1;
    obj->image = -1;
    obj->keep = -1;
}

static owf_obj *add_obj(owf_obj **objs, int *n, int *cap, owf_obj_kind kind)
{
    owf_obj *obj;
    if (!grow((void **)objs, n, cap, sizeof **objs, OWF_DECK_MAX_OBJS))
        return NULL;
    obj = &(*objs)[(*n)++];
    owf_obj_init(obj, kind);
    return obj;
}

owf_obj *owf_slide_add(owf_slide *slide, owf_obj_kind kind)
{
    return slide ? add_obj(&slide->objs, &slide->nobjs, &slide->capobjs, kind) : NULL;
}

owf_obj *owf_master_add(owf_master *master, owf_obj_kind kind)
{
    return master ? add_obj(&master->objs, &master->nobjs, &master->capobjs, kind) : NULL;
}

owf_obj *owf_layout_add(owf_slide_layout *layout, owf_obj_kind kind)
{
    return layout ? add_obj(&layout->objs, &layout->nobjs, &layout->capobjs, kind) : NULL;
}

int owf_slide_delete(owf_slide *slide, int index)
{
    int i, end;
    if (!slide || index < 0 || index >= slide->nobjs)
        return -1;
    /* a group goes with its objects, which follow it */
    for (end = index + 1; end < slide->nobjs; end++) {
        int p = slide->objs[end].parent;
        while (p >= 0 && p != index && p < slide->nobjs)
            p = slide->objs[p].parent;
        if (p != index)
            break;
    }
    for (i = index; i < end; i++)
        owf_obj_clear(&slide->objs[i]);
    memmove(&slide->objs[index], &slide->objs[end], (size_t)(slide->nobjs - end) * sizeof *slide->objs);
    slide->nobjs -= end - index;
    for (i = 0; i < slide->nobjs; i++) {
        if (slide->objs[i].parent >= end)
            slide->objs[i].parent -= end - index;
        else if (slide->objs[i].parent >= index)
            slide->objs[i].parent = -1;
    }
    return 0;
}

int owf_obj_level(const owf_obj *obj, int para)
{
    if (!obj || !obj->levels || para < 0 || para >= obj->text.nparas || para >= obj->caplevels)
        return 0;
    return obj->levels[para];
}

int owf_obj_set_level(owf_obj *obj, int para, int level)
{
    if (!obj || para < 0 || para >= obj->text.nparas || level < 0 || level > OWF_TEXT_LEVELS)
        return -1;
    if (para >= obj->caplevels) {
        int want = obj->caplevels ? obj->caplevels : 16;
        unsigned char *p;
        if (!level)
            return 0;
        while (want <= para)
            want *= 2;
        p = realloc(obj->levels, (size_t)want);
        if (!p)
            return -1;
        memset(p + obj->caplevels, 0, (size_t)(want - obj->caplevels));
        obj->levels = p;
        obj->caplevels = want;
    }
    obj->levels[para] = (unsigned char)level;
    return 0;
}

/* ---- Slides, masters, layouts ---- */

static void slide_init(owf_slide *s)
{
    memset(s, 0, sizeof *s);
    s->layout = -1;
    s->background_image = -1;
    s->keep_timing = -1;
    s->transition.advance_after = -1;
    s->transition.keep = -1;
}

static void slide_clear(owf_slide *s)
{
    int i;
    for (i = 0; i < s->nobjs; i++)
        owf_obj_clear(&s->objs[i]);
    free(s->objs);
    owf_deck_free_story(&s->notes);
    free(s->name);
    free(s->footer);
    free(s->date_text);
}

static void master_clear(owf_master *m)
{
    int i;
    for (i = 0; i < m->nobjs; i++)
        owf_obj_clear(&m->objs[i]);
    free(m->objs);
    free(m->name);
}

static void layout_clear(owf_slide_layout *l)
{
    int i;
    for (i = 0; i < l->nobjs; i++)
        owf_obj_clear(&l->objs[i]);
    free(l->objs);
    free(l->name);
}

owf_deck *owf_deck_new_empty(void)
{
    owf_deck *deck = calloc(1, sizeof *deck);
    if (!deck)
        return NULL;
    deck->res = owf_doc_new();
    if (!deck->res) {
        free(deck);
        return NULL;
    }
    deck->res->base.size = 18 * OWF_TWIPS_PER_POINT;
    /* Impress's default: 16:9, 28 x 15.75 cm */
    deck->width = 15874;
    deck->height = 8929;
    /* notes and handouts: A4 portrait, 2 cm margins */
    deck->notes_page.width = 11906;
    deck->notes_page.height = 16838;
    deck->notes_page.margin_top = deck->notes_page.margin_bottom = 1134;
    deck->notes_page.margin_left = deck->notes_page.margin_right = 1134;
    deck->notes_page.start_page = 1;
    return deck;
}

void owf_deck_free(owf_deck *deck)
{
    int i;
    if (!deck)
        return;
    for (i = 0; i < deck->nslides; i++)
        slide_clear(&deck->slides[i]);
    free(deck->slides);
    for (i = 0; i < deck->nmasters; i++)
        master_clear(&deck->masters[i]);
    free(deck->masters);
    for (i = 0; i < deck->nlayouts; i++)
        layout_clear(&deck->layouts[i]);
    free(deck->layouts);
    for (i = 0; i < deck->nkept; i++)
        free(deck->kept[i].xml);
    free(deck->kept);
    for (i = 0; i < deck->nparts; i++) {
        free(deck->parts[i].name);
        free(deck->parts[i].mime);
        free(deck->parts[i].data);
    }
    free(deck->parts);
    owf_doc_free(deck->res);
    free(deck);
}

owf_master *owf_deck_add_master(owf_deck *deck, const char *name)
{
    owf_master *m;
    int i;
    if (!deck || !grow((void **)&deck->masters, &deck->nmasters, &deck->capmasters, sizeof *m, OWF_DECK_MAX_MASTERS))
        return NULL;
    m = &deck->masters[deck->nmasters];
    memset(m, 0, sizeof *m);
    m->background_image = -1;
    if (name && owf_deck_set_text(&m->name, name) < 0)
        return NULL;
    owf_textlevel_init(&m->title);
    owf_textlevel_init(&m->subtitle);
    owf_textlevel_init(&m->notes);
    owf_textlevel_init(&m->other);
    for (i = 0; i < OWF_TEXT_LEVELS; i++)
        owf_textlevel_init(&m->body[i]);
    deck->nmasters++;
    return m;
}

owf_slide_layout *owf_deck_add_layout(owf_deck *deck, const char *name, int master)
{
    owf_slide_layout *l;
    if (!deck || !grow((void **)&deck->layouts, &deck->nlayouts, &deck->caplayouts, sizeof *l, OWF_DECK_MAX_LAYOUTS))
        return NULL;
    l = &deck->layouts[deck->nlayouts];
    memset(l, 0, sizeof *l);
    l->master = master;
    if (name && owf_deck_set_text(&l->name, name) < 0)
        return NULL;
    deck->nlayouts++;
    return l;
}

owf_slide *owf_deck_add_slide(owf_deck *deck, int at, int layout)
{
    owf_slide *s;
    int i;
    if (!deck || layout >= deck->nlayouts || layout < -1)
        return NULL;
    if (at < 0 || at > deck->nslides)
        at = deck->nslides;
    if (!grow((void **)&deck->slides, &deck->nslides, &deck->capslides, sizeof *s, OWF_DECK_MAX_SLIDES))
        return NULL;
    memmove(&deck->slides[at + 1], &deck->slides[at], (size_t)(deck->nslides - at) * sizeof *s);
    deck->nslides++;
    s = &deck->slides[at];
    slide_init(s);
    s->layout = layout;
    if (layout >= 0) {
        const owf_slide_layout *l = &deck->layouts[layout];
        s->master = l->master >= 0 && l->master < deck->nmasters ? l->master : 0;
        /* the layout's placeholders, empty, taking their boxes from it */
        for (i = 0; i < l->nobjs; i++) {
            const owf_obj *ph = &l->objs[i];
            owf_obj *o;
            if (ph->placeholder == OWF_PH_NONE)
                continue;
            o = owf_slide_add(s, ph->kind == OWF_OBJ_SHAPE ? OWF_OBJ_TEXT : ph->kind);
            if (!o)
                break;
            o->placeholder = ph->placeholder;
            o->ph_index = ph->ph_index;
            o->flags = OWF_OBJ_INHERIT_BOX | OWF_OBJ_EMPTY;
            o->x = ph->x;
            o->y = ph->y;
            o->width = ph->width;
            o->height = ph->height;
            o->valign = ph->valign;
            o->autofit = ph->autofit;
            memcpy(o->inset, ph->inset, sizeof o->inset);
        }
    }
    return s;
}

int owf_deck_delete_slide(owf_deck *deck, int index)
{
    if (!deck || index < 0 || index >= deck->nslides)
        return -1;
    slide_clear(&deck->slides[index]);
    memmove(&deck->slides[index], &deck->slides[index + 1],
            (size_t)(deck->nslides - index - 1) * sizeof *deck->slides);
    deck->nslides--;
    return 0;
}

int owf_deck_move_slide(owf_deck *deck, int from, int to)
{
    owf_slide s;
    if (!deck || from < 0 || from >= deck->nslides || to < 0 || to >= deck->nslides)
        return -1;
    if (from == to)
        return 0;
    s = deck->slides[from];
    if (from < to)
        memmove(&deck->slides[from], &deck->slides[from + 1], (size_t)(to - from) * sizeof s);
    else
        memmove(&deck->slides[to + 1], &deck->slides[to], (size_t)(from - to) * sizeof s);
    deck->slides[to] = s;
    return 0;
}

int owf_deck_keep(owf_deck *deck, owf_kept_kind kind, const char *xml, size_t length)
{
    char *copy;
    if (!deck || !xml || deck->kept_bytes + length > OWF_DECK_KEEP_LIMIT)
        return -1;
    if (!grow((void **)&deck->kept, &deck->nkept, &deck->capkept, sizeof *deck->kept, 1 << 20))
        return -1;
    copy = owf_deck_strdup(xml, length);
    if (!copy)
        return -1;
    deck->kept[deck->nkept].kind = kind;
    deck->kept[deck->nkept].xml = copy;
    deck->kept_bytes += length;
    return deck->nkept++;
}

int owf_deck_add_part(owf_deck *deck, const char *name, const char *mime, const void *data, size_t length)
{
    owf_deck_part *p;
    int i;
    if (!deck || !name)
        return -1;
    for (i = 0; i < deck->nparts; i++)
        if (!strcmp(deck->parts[i].name, name))
            return i;
    if (deck->kept_bytes + length > OWF_DECK_KEEP_LIMIT)
        return -1;
    if (!grow((void **)&deck->parts, &deck->nparts, &deck->capparts, sizeof *p, 4096))
        return -1;
    p = &deck->parts[deck->nparts];
    memset(p, 0, sizeof *p);
    p->name = owf_deck_strdup(name, strlen(name));
    p->mime = owf_deck_strdup(mime ? mime : "", mime ? strlen(mime) : 0);
    if (data && length) {
        p->data = malloc(length);
        if (p->data)
            memcpy(p->data, data, length);
        p->length = length;
    }
    if (!p->name || !p->mime || (length && data && !p->data)) {
        free(p->name);
        free(p->mime);
        free(p->data);
        return -1;
    }
    deck->kept_bytes += length;
    return deck->nparts++;
}

/* ---- What an object takes from its layout and master ---- */

const owf_master *owf_deck_slide_master(const owf_deck *deck, int slide)
{
    int m;
    if (!deck || !deck->nmasters)
        return NULL;
    m = slide >= 0 && slide < deck->nslides ? deck->slides[slide].master : 0;
    return &deck->masters[m >= 0 && m < deck->nmasters ? m : 0];
}

/* The levels a placeholder kind takes from a master. */
static const owf_textlevel *master_levels(const owf_master *m, owf_placeholder ph, int *count)
{
    *count = 1;
    switch (ph) {
    case OWF_PH_TITLE: return &m->title;
    case OWF_PH_SUBTITLE: return &m->subtitle;
    case OWF_PH_NOTES: return &m->notes;
    case OWF_PH_BODY:
    case OWF_PH_TEXT:
    case OWF_PH_OBJECT:
        *count = OWF_TEXT_LEVELS;
        return m->body;
    default: return &m->other;
    }
}

static const owf_obj *find_placeholder(const owf_obj *objs, int n, owf_placeholder ph, int index)
{
    int i, any = -1;
    for (i = 0; i < n; i++)
        if (objs[i].placeholder == ph) {
            if (objs[i].ph_index == index)
                return &objs[i];
            if (any < 0)
                any = i;
        }
    /* a body placeholder can stand for a text or object one, and the other way */
    if (any < 0 && (ph == OWF_PH_TEXT || ph == OWF_PH_OBJECT))
        return find_placeholder(objs, n, OWF_PH_BODY, index);
    return any >= 0 ? &objs[any] : NULL;
}

int owf_deck_resolve(const owf_deck *deck, int slide, int obj, owf_obj *box, const owf_textlevel **levels)
{
    const owf_slide *s;
    const owf_obj *o;
    const owf_master *m;
    int count = 1;

    if (!deck || slide < 0 || slide >= deck->nslides)
        return -1;
    s = &deck->slides[slide];
    if (obj < 0 || obj >= s->nobjs)
        return -1;
    o = &s->objs[obj];
    m = owf_deck_slide_master(deck, slide);
    if (box) {
        *box = *o;
        if ((o->flags & OWF_OBJ_INHERIT_BOX) && o->placeholder != OWF_PH_NONE) {
            const owf_obj *from = NULL;
            if (s->layout >= 0 && s->layout < deck->nlayouts)
                from = find_placeholder(deck->layouts[s->layout].objs, deck->layouts[s->layout].nobjs,
                                        o->placeholder, o->ph_index);
            if (!from && m)
                from = find_placeholder(m->objs, m->nobjs, o->placeholder, o->ph_index);
            if (from && from->width > 0 && from->height > 0) {
                box->x = from->x;
                box->y = from->y;
                box->width = from->width;
                box->height = from->height;
            }
        }
    }
    if (m) {
        const owf_textlevel *l = master_levels(m, o->placeholder, &count);
        if (levels)
            *levels = l;
    } else if (levels) {
        *levels = NULL;
    }
    return count;
}

const owf_textlevel *owf_deck_para_level(const owf_deck *deck, const owf_master *master, const owf_obj *obj, int para)
{
    int level = owf_obj_level(obj, para), count;
    const owf_textlevel *l;
    (void)deck;
    if (obj->list && level > 0)
        return &obj->list[level - 1];
    if (!master)
        return NULL;
    l = master_levels(master, obj->placeholder, &count);
    if (count > 1)
        return &l[level > 0 ? level - 1 : 0];
    return l;
}

owf_para *owf_deck_add_para(const owf_deck *deck, const owf_master *master, owf_obj *obj, int level)
{
    const owf_textlevel *lv = NULL;
    owf_para *p;
    int count;
    (void)deck;
    if (!obj || level < 0 || level > OWF_TEXT_LEVELS)
        return NULL;
    if (obj->list && level > 0)
        lv = &obj->list[level - 1];
    else if (master) {
        lv = master_levels(master, obj->placeholder, &count);
        if (count > 1)
            lv = &lv[level > 0 ? level - 1 : 0];
    }
    p = owf_story_add(&obj->text, lv ? &lv->para : NULL);
    if (p && owf_obj_set_level(obj, obj->text.nparas - 1, level) < 0) {
        obj->text.nparas--;
        return NULL;
    }
    return p;
}

/* ---- A new deck ---- */

void owf_deck_default_levels(owf_master *m, int font)
{
    static const int body_size[OWF_TEXT_LEVELS] = { 32, 28, 24, 20, 20, 20, 20, 20, 20 };
    static const unsigned long body_bullet[OWF_TEXT_LEVELS] = {
        0x25CF, 0x2013, 0x25CF, 0x2013, 0x25CF, 0x25CF, 0x25CF, 0x25CF, 0x25CF
    };
    int i;

    owf_textlevel_init(&m->title);
    m->title.chars.font = font;
    m->title.chars.size = 44 * OWF_TWIPS_PER_POINT;
    m->title.para.align = OWF_ALIGN_CENTRE;
    owf_textlevel_init(&m->subtitle);
    m->subtitle.chars.font = font;
    m->subtitle.chars.size = 32 * OWF_TWIPS_PER_POINT;
    m->subtitle.para.align = OWF_ALIGN_CENTRE;
    for (i = 0; i < OWF_TEXT_LEVELS; i++) {
        owf_textlevel *l = &m->body[i];
        owf_textlevel_init(l);
        l->chars.font = font;
        l->chars.size = body_size[i] * OWF_TWIPS_PER_POINT;
        /* the bullet half an inch further in each level, the text 0.3 inch after it */
        l->para.indent_left = i * 720 + 432;
        l->para.indent_first = -432;
        l->para.space_before = i ? 113 : 283;
        l->bullet = body_bullet[i];
        l->bullet_size = 45 + (i ? 30 : 0);
    }
    owf_textlevel_init(&m->notes);
    m->notes.chars.font = font;
    m->notes.chars.size = 20 * OWF_TWIPS_PER_POINT;
    owf_textlevel_init(&m->other);
    m->other.chars.font = font;
    m->other.chars.size = 18 * OWF_TWIPS_PER_POINT;
}

/* A placeholder at a share of the slide, in thousandths. */
static owf_obj *placeholder(owf_obj *o, int w, int h, owf_placeholder ph, int index, int x, int y, int bw, int bh)
{
    if (!o)
        return NULL;
    o->placeholder = ph;
    o->ph_index = index;
    o->x = (int)((long)w * x / 1000);
    o->y = (int)((long)h * y / 1000);
    o->width = (int)((long)w * bw / 1000);
    o->height = (int)((long)h * bh / 1000);
    if (ph == OWF_PH_TITLE || ph == OWF_PH_SUBTITLE)
        o->valign = OWF_VALIGN_CENTRE;
    if (ph == OWF_PH_BODY)
        o->autofit = OWF_AUTOFIT_SHRINK;
    return o;
}

owf_deck *owf_deck_new(int width, int height)
{
    owf_deck *deck = owf_deck_new_empty();
    owf_master *m;
    owf_slide_layout *l;
    int font, w, h, ok = 1;

    if (!deck)
        return NULL;
    if (width > 0 && height > 0) {
        deck->width = width;
        deck->height = height;
    }
    w = deck->width;
    h = deck->height;
    font = owf_doc_font(deck->res, "Liberation Sans", OWF_FONT_SANS);
    deck->res->base.font = font;
    m = owf_deck_add_master(deck, "Default");
    if (font < 0 || !m) {
        owf_deck_free(deck);
        return NULL;
    }
    owf_deck_default_levels(m, font);
    m->background = OWF_SHADE(0xFFFFFF);
    ok &= placeholder(owf_master_add(m, OWF_OBJ_TEXT), w, h, OWF_PH_TITLE, 0, 50, 40, 900, 167) != NULL;
    ok &= placeholder(owf_master_add(m, OWF_OBJ_TEXT), w, h, OWF_PH_BODY, 0, 50, 233, 900, 580) != NULL;
    ok &= placeholder(owf_master_add(m, OWF_OBJ_TEXT), w, h, OWF_PH_DATE, 0, 50, 911, 233, 69) != NULL;
    ok &= placeholder(owf_master_add(m, OWF_OBJ_TEXT), w, h, OWF_PH_FOOTER, 0, 342, 911, 317, 69) != NULL;
    ok &= placeholder(owf_master_add(m, OWF_OBJ_TEXT), w, h, OWF_PH_NUMBER, 0, 717, 911, 233, 69) != NULL;

    ok &= owf_deck_add_layout(deck, "Blank Slide", 0) != NULL;
    if ((l = owf_deck_add_layout(deck, "Title Slide", 0))) {
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_TITLE, 0, 50, 200, 900, 250) != NULL;
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_SUBTITLE, 0, 50, 500, 900, 300) != NULL;
    } else ok = 0;
    if ((l = owf_deck_add_layout(deck, "Title, Content", 0))) {
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_TITLE, 0, 50, 40, 900, 167) != NULL;
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_BODY, 0, 50, 233, 900, 580) != NULL;
    } else ok = 0;
    if ((l = owf_deck_add_layout(deck, "Title and 2 Content", 0))) {
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_TITLE, 0, 50, 40, 900, 167) != NULL;
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_BODY, 0, 50, 233, 439, 580) != NULL;
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_BODY, 1, 511, 233, 439, 580) != NULL;
    } else ok = 0;
    if ((l = owf_deck_add_layout(deck, "Title Only", 0)))
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_TITLE, 0, 50, 40, 900, 167) != NULL;
    else ok = 0;
    if ((l = owf_deck_add_layout(deck, "Centered Text", 0)))
        ok &= placeholder(owf_layout_add(l, OWF_OBJ_TEXT), w, h, OWF_PH_SUBTITLE, 0, 50, 233, 900, 580) != NULL;
    else ok = 0;
    if (!ok) {
        owf_deck_free(deck);
        return NULL;
    }
    return deck;
}

/* ---- Deck formats ---- */

static const owf_deck_format *const deck_formats[] = {
    &owf_deck_format_odp,
    &owf_deck_format_fodp,
};

#define NDECKFORMATS ((int)(sizeof deck_formats / sizeof deck_formats[0]))

int owf_deck_format_count(void)
{
    return NDECKFORMATS;
}

const owf_deck_format *owf_deck_format_at(int index)
{
    return index >= 0 && index < NDECKFORMATS ? deck_formats[index] : NULL;
}

const owf_deck_format *owf_deck_format_named(const char *name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < NDECKFORMATS; i++)
        if (owf_same_word(deck_formats[i]->name, name))
            return deck_formats[i];
    return NULL;
}

const owf_deck_format *owf_deck_format_for_filename(const char *filename)
{
    const char *dot = filename ? strrchr(filename, '.') : NULL;
    int i;
    if (!dot || strchr(dot, '/') || strchr(dot, ':'))
        return NULL;
    for (i = 0; i < NDECKFORMATS; i++)
        if (deck_formats[i]->export && owf_ext_in_list(deck_formats[i]->extensions, dot + 1))
            return deck_formats[i];
    return NULL;
}

const owf_deck_format *owf_deck_format_detect(const unsigned char *data, size_t length)
{
    const owf_deck_format *f = NULL;
    int i, best = 0;
    for (i = 0; i < NDECKFORMATS; i++) {
        int score;
        if (!deck_formats[i]->detect || !deck_formats[i]->import)
            continue;
        score = deck_formats[i]->detect(data, length);
        if (score > best) {
            best = score;
            f = deck_formats[i];
        }
    }
    return f;
}

int owf_import_deck_memory(const unsigned char *data, size_t length, const char *format,
                           owf_deck **deck_out, owf_report *report, const owf_deck_format **used)
{
    const owf_deck_format *f;
    owf_deck *deck;
    int result;

    *deck_out = NULL;
    if (format) {
        f = owf_deck_format_named(format);
        if (!f || !f->import)
            return OWF_ERR_UNSUPPORTED;
    } else {
        f = owf_deck_format_detect(data, length);
        if (!f)
            return OWF_ERR_FORMAT;
    }
    deck = owf_deck_new_empty();
    if (!deck)
        return OWF_ERR_MEMORY;
    result = f->import(data, length, deck, report);
    if (result != OWF_OK) {
        owf_deck_free(deck);
        return result;
    }
    if (used)
        *used = f;
    *deck_out = deck;
    return OWF_OK;
}

int owf_import_deck_file(const char *path, const char *format, owf_deck **deck,
                         owf_report *report, const owf_deck_format **used)
{
    unsigned char *data;
    size_t size;
    int result;

    *deck = NULL;
    result = owf_sys_read_file(path, &data, &size);
    if (result != OWF_OK)
        return result;
    result = owf_import_deck_memory(data, size, format, deck, report, used);
    free(data);
    return result;
}

int owf_export_deck_file(const owf_deck *deck, const char *path, const char *format, owf_report *report)
{
    const owf_deck_format *f = format ? owf_deck_format_named(format) : owf_deck_format_for_filename(path);
    unsigned char *data;
    size_t length;
    int result;

    if (!f)
        return format ? OWF_ERR_UNSUPPORTED : OWF_ERR_FORMAT;
    if (!f->export)
        return OWF_ERR_UNSUPPORTED;
    result = f->export(deck, &data, &length, report);
    if (result != OWF_OK)
        return result;
    result = owf_sys_write_file(path, data, length);
    free(data);
    return result;
}

/* ---- Pictures' own sizes ---- */

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

int owf_deck_image_size(const owf_image *im, int *width, int *height)
{
    const unsigned char *d = im ? im->data : NULL;
    size_t n = im ? im->length : 0, i;
    unsigned long w = 0, h = 0, dpi_x = 96, dpi_y = 96;

    if (!d)
        return 0;
    if (n >= 24 && !memcmp(d, "\211PNG", 4) && !memcmp(d + 12, "IHDR", 4)) {
        w = be32(d + 16);
        h = be32(d + 20);
        /* pHYs: pixels a metre, when its unit is the metre */
        for (i = 8; i + 12 <= n;) {
            unsigned long len = be32(d + i);
            if (len > n - i - 12)
                break;
            if (!memcmp(d + i + 4, "pHYs", 4) && len >= 9 && d[i + 16] == 1) {
                unsigned long px = be32(d + i + 8), py = be32(d + i + 12);
                if (px >= 1000 && px < 400000 && py >= 1000 && py < 400000) {
                    dpi_x = (px * 254 + 5000) / 10000;
                    dpi_y = (py * 254 + 5000) / 10000;
                }
                break;
            }
            if (!memcmp(d + i + 4, "IDAT", 4))
                break;
            i += 12 + len;
        }
    } else if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8) {
        for (i = 2; i + 9 < n;) {
            unsigned m, len;
            if (d[i] != 0xFF)
                break;
            m = d[i + 1];
            len = ((unsigned)d[i + 2] << 8) | d[i + 3];
            if (m == 0xE0 && len >= 14 && i + 16 < n && !memcmp(d + i + 4, "JFIF", 4)) {
                unsigned units = d[i + 11], dx = ((unsigned)d[i + 12] << 8) | d[i + 13],
                         dy = ((unsigned)d[i + 14] << 8) | d[i + 15];
                if (units && dx >= 10 && dy >= 10 && dx < 10000 && dy < 10000) {
                    dpi_x = units == 2 ? (dx * 254 + 50) / 100 : dx;
                    dpi_y = units == 2 ? (dy * 254 + 50) / 100 : dy;
                }
            }
            if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) ||
                (m >= 0xCD && m <= 0xCF)) {
                h = ((unsigned)d[i + 5] << 8) | d[i + 6];
                w = ((unsigned)d[i + 7] << 8) | d[i + 8];
                break;
            }
            i += 2 + len;
        }
    }
    if (!w || !h || w > 65535 || h > 65535 || !dpi_x || !dpi_y)
        return 0;
    *width = (int)(w * 1440 / dpi_x);
    *height = (int)(h * 1440 / dpi_y);
    return *width > 0 && *height > 0;
}

void owf_deck_base64(owf_buf *out, const unsigned char *d, size_t n)
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    for (i = 0; i + 2 < n; i += 3) {
        unsigned long v = ((unsigned long)d[i] << 16) | ((unsigned long)d[i + 1] << 8) | d[i + 2];
        owf_buf_putc(out, digits[(v >> 18) & 63]);
        owf_buf_putc(out, digits[(v >> 12) & 63]);
        owf_buf_putc(out, digits[(v >> 6) & 63]);
        owf_buf_putc(out, digits[v & 63]);
        if (i % 57 == 54)
            owf_buf_putc(out, '\n');
    }
    if (i < n) {
        unsigned long v = (unsigned long)d[i] << 16;
        if (i + 1 < n)
            v |= (unsigned long)d[i + 1] << 8;
        owf_buf_putc(out, digits[(v >> 18) & 63]);
        owf_buf_putc(out, digits[(v >> 12) & 63]);
        owf_buf_putc(out, i + 1 < n ? digits[(v >> 6) & 63] : '=');
        owf_buf_putc(out, '=');
    }
}

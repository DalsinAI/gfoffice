/*
 * OpenWrite's pages on OpenLayout (ow_layout.h): the owf_doc as
 * OpenLayout's tree, laid out into pages, and positions both ways.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ow_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TW(v) ((ol_unit)(v) * OL_UNITS_PER_TWIP)
#define DEFAULT_SIZE 240            /* twips: 12 point */
#define BORDER_WIDTH OL_POINT(1) / 2
#define WRAP_GAP 180                /* twips: Word's distL and distR, 114300 EMU */

/* What an OpenLayout node is in the document: run -1 is the paragraph's
 * own block; paragraph -1 is a table, row or cell. box: the body's (0) or
 * a text box's (k, doc->frames[k - 1]), 2.0.15. */
typedef struct {
    int paragraph;
    int run;
    int box;
} node_ref;

struct ow_layout {
    const owf_doc *doc;
    ol_measure_text_fn measure;
    void *measure_userdata;
    ol_document *body;
    ol_page page;
    node_ref *refs;             /* by node id */
    size_t caprefs;
    ol_id *blocks;              /* each body paragraph's block; its runs follow it */
    /* 2.0.15: the text boxes, for the caret in them: each frame's node (0: not laid out) and each text box's
     * paragraphs' blocks, by frame (doc->frames[k - 1] is box k) */
    int nboxes;
    ol_id *box_frame;
    ol_id **box_blocks;
    /* 2.0.17: how far each text box in the line is moved from its paragraph's corner onto its room there (by
     * frame; NULL: not yet) */
    const ol_unit *inline_dx, *inline_dy;
};

/* The text a field shows. */
typedef struct {
    char page[32];
    char pages[32];
    const char *date;
    const char *time;
    /* 2.0.16: a header's or footer's frames too, its own top left on the page at ox, oy (their places from the
     * page and its margins are made the header's own) */
    int frames;
    ol_unit ox, oy;
} field_values;

static int remember(ow_layout *l, ol_node *node, int box, int paragraph, int run)
{
    ol_id id = ol_node_id(node);
    if (!l) return 1;
    if (id >= l->caprefs) {
        size_t cap = l->caprefs ? l->caprefs : 256, i;
        node_ref *p;
        while (cap <= id) cap *= 2;
        p = (node_ref *)realloc(l->refs, cap * sizeof(*p));
        if (!p) return 0;
        for (i = l->caprefs; i < cap; ++i) {
            p[i].paragraph = -1;
            p[i].run = -1;
            p[i].box = 0;
        }
        l->refs = p;
        l->caprefs = cap;
    }
    l->refs[id].paragraph = paragraph;
    l->refs[id].run = run;
    l->refs[id].box = box;
    return 1;
}

static int base_size(const owf_doc *doc)
{
    return doc->base.size > 0 ? doc->base.size : DEFAULT_SIZE;
}

static uint32_t argb(unsigned long rgb)
{
    return 0xff000000u | (uint32_t)(rgb & 0xffffffUL);
}

static void run_style(const owf_doc *doc, const owf_charfmt *fmt, int line_spacing, ol_style *s)
{
    int font = fmt->font >= 0 ? fmt->font : doc->base.font;
    int size = fmt->size > 0 ? fmt->size : base_size(doc);
    unsigned flags = fmt->flags;
    unsigned long colour = fmt->colour;
    uint32_t text = 0;

    ol_style_init(s);
    s->display = OL_DISPLAY_INLINE;
    s->font_size = TW(size);
    if (flags & (OWF_SUPER | OWF_SUB)) s->font_size = s->font_size * 2 / 3;
    s->line_height = line_spacing > 100 ? (ol_unit)((long)TW(size) * 12 / 10 * line_spacing / 100) : 0;
    if (OWF_LINE_IS_HEIGHT(line_spacing)) s->line_height = TW(OWF_LINE_HEIGHT(line_spacing));   /* 2.0.18 */
    if (colour == OWF_COLOUR_AUTO && doc->base.colour != OWF_COLOUR_AUTO) colour = doc->base.colour;
    s->foreground = colour == OWF_COLOUR_AUTO ? 0xff000000u : argb(colour);
    if (flags & OWF_BOLD) text |= OL_TEXT_BOLD;
    if (flags & OWF_ITALIC) text |= OL_TEXT_ITALIC;
    if (flags & OWF_UNDERLINE) text |= OL_TEXT_UNDERLINE;
    if (flags & OWF_STRIKE) text |= OL_TEXT_STRIKE;
    if (flags & OWF_SUPER) text |= OL_TEXT_SUPER;
    if (flags & OWF_SUB) text |= OL_TEXT_SUB;
    if (font >= 0 && font < doc->nfonts) {
        text |= OL_TEXT_FONT(OW_LAYOUT_FONT(font));
        if (doc->fonts[font].kind == OWF_FONT_MONO) text |= OL_TEXT_MONO;
    }
    s->text_flags = text;
}

static void para_style(const owf_doc *doc, const owf_para *p, ol_style *s, ol_box_style *b)
{
    const owf_parafmt *f = &p->fmt;
    int spacing = f->line_spacing > 100 ? f->line_spacing : 100;

    ol_style_init(s);
    s->display = OL_DISPLAY_BLOCK;
    s->margin_top = TW(f->space_before);
    s->margin_bottom = TW(f->space_after);
    s->margin_left = TW(f->indent_left);
    s->margin_right = TW(f->indent_right);
    switch (f->align) {
    case OWF_ALIGN_CENTRE: s->text_align = OL_ALIGN_CENTER; break;
    case OWF_ALIGN_RIGHT: s->text_align = OL_ALIGN_END; break;
    case OWF_ALIGN_JUSTIFY: s->text_align = OL_ALIGN_JUSTIFY; break;
    default: s->text_align = OL_ALIGN_START; break;
    }
    /* an empty paragraph's line: the size of the first run, or the default */
    s->font_size = TW(p->nruns && p->runs[0].fmt.size > 0 ? p->runs[0].fmt.size : base_size(doc));
    s->line_height = (ol_unit)((long)s->font_size * 12 / 10 * spacing / 100);
    if (OWF_SHADE_SET(f->shading)) s->background = argb(OWF_SHADE_RGB(f->shading));

    ol_box_style_init(b);
    b->flags = OL_BOX_EMPTY_LINE;
    if (OWF_LINE_IS_HEIGHT(f->line_spacing)) {
        /* 2.0.18: Word's exact line spacing, every line so high (OpenLayout 0.7), or its at least */
        ol_unit h = TW(OWF_LINE_HEIGHT(f->line_spacing));
        if (OWF_LINE_IS_EXACT(f->line_spacing)) {
            s->line_height = h;
            b->flags |= OL_BOX_EXACT_LINES;
        } else if (h > s->line_height) {
            s->line_height = h;
        }
    }
    if (f->page_break_before) b->flags |= OL_BOX_PAGE_BREAK_BEFORE;
    if (f->heading > 0) b->flags |= OL_BOX_KEEP_WITH_NEXT;
    b->text_indent = TW(f->indent_first);
    if (f->borders) {
        b->border_width = BORDER_WIDTH;
        b->border_argb = OWF_SHADE_SET(f->border_colour) ? argb(OWF_SHADE_RGB(f->border_colour))
                                                         : 0xff000000u;
        b->border_sides = 0;
        if (f->borders & OWF_BORDER_TOP) b->border_sides |= OL_BORDER_TOP;
        if (f->borders & OWF_BORDER_RIGHT) b->border_sides |= OL_BORDER_RIGHT;
        if (f->borders & OWF_BORDER_BOTTOM) b->border_sides |= OL_BORDER_BOTTOM;
        if (f->borders & OWF_BORDER_LEFT) b->border_sides |= OL_BORDER_LEFT;
    }
}

static int add_story(ow_layout *l, const owf_doc *doc, ol_document *od, ol_node *parent, const owf_story *story,
                     int record, const field_values *fv);

/* 2.0.10: where a frame goes, across (x) or down (y): OpenLayout 0.4's anchor (from the page, its margins or
 * the paragraph) and the offset from there, its alignment worked out against the area it is aligned in. */
static ol_unit frame_at(const ol_page *pg, const owf_frame *fr, int across, uint32_t *anchor)
{
    int from = across ? fr->x_from : fr->y_from, place = across ? fr->x_place : fr->y_place;
    ol_unit size = TW(across ? fr->width : fr->height), offset = TW(across ? fr->x : fr->y);
    ol_unit page = across ? pg->width : pg->height;
    ol_unit lead = across ? pg->margin_left : pg->margin_top, trail = across ? pg->margin_right : pg->margin_bottom;
    ol_unit start = 0, area;
    unsigned kind = OL_ANCHOR_PAGE;
    switch (from) {
    case OWF_FROM_PAGE: area = page; break;
    case OWF_FROM_MARGIN: kind = OL_ANCHOR_MARGIN; area = page - lead - trail; break;
    case OWF_FROM_LEFT_MARGIN: case OWF_FROM_TOP_MARGIN: area = lead; break;
    case OWF_FROM_RIGHT_MARGIN: case OWF_FROM_BOTTOM_MARGIN: start = page - trail; area = trail; break;
    default:                                         /* the paragraph: its corner; down, it has no height */
        kind = OL_ANCHOR_BLOCK;
        area = across ? page - lead - trail : 0;
        break;
    }
    *anchor |= across ? OL_ANCHOR_X(kind) : OL_ANCHOR_Y(kind);
    switch (place) {
    case OWF_PLACE_START: return start;
    case OWF_PLACE_CENTRE: return start + (area - size) / 2;
    case OWF_PLACE_END: return start + area - size;
    default: return start + offset;
    }
}

/* 2.0.10: a picture or a text box placed on the page (owf_frame, Word's wp:anchor) as OpenLayout's anchored
 * frame, in the paragraph it is anchored to: behind the text or in front of it, its picture filling it, a
 * text box's paragraphs inside its insets. 2.0.14: the body's text goes round it as Word's wrap says
 * (OpenLayout 0.6): square and tight beside it, top and bottom above and below it, Word's usual eighth of an
 * inch from its sides; with no wrap (behind or in front of the text), it moves no line. */
static int add_frame(const ow_layout *l, const owf_doc *doc, ol_document *od, ol_node *block, const owf_frame *fr,
                     const field_values *fv)
{
    ol_style s;
    ol_box_style b;
    ol_node *f = ol_node_append(od, block, OL_ROLE_BLOCK, NULL), *n;
    if (!f) return 0;
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    if (fr->story.nparas) {
        s.padding_top = TW(fr->inset[0]);
        s.padding_right = TW(fr->inset[1]);
        s.padding_bottom = TW(fr->inset[2]);
        s.padding_left = TW(fr->inset[3]);
    }
    if (OWF_SHADE_SET(fr->fill)) s.background = argb(OWF_SHADE_RGB(fr->fill));
    ol_node_set_style(f, &s);
    ol_box_style_init(&b);
    b.flags = OL_BOX_ABSOLUTE;
    b.frame.width = TW(fr->width > 0 ? fr->width : OWF_TWIPS_PER_INCH);
    b.frame.height = TW(fr->height > 0 ? fr->height : 0);
    b.frame.x = frame_at(&l->page, fr, 1, &b.anchor);
    b.frame.y = frame_at(&l->page, fr, 0, &b.anchor);
    if (fr->in_line && l && l->inline_dx && fr >= doc->frames && fr < doc->frames + doc->nframes) {
        b.frame.x += l->inline_dx[fr - doc->frames];  /* 2.0.17: onto its room in the line */
        b.frame.y += l->inline_dy[fr - doc->frames];
    }
    if (fr->behind) b.anchor |= OL_ANCHOR_BEHIND;
    else if (!b.anchor) b.anchor = OL_ANCHOR_ON;     /* from the paragraph both ways, in front */
    switch (fr->wrap) {
    case OWF_WRAP_SQUARE: case OWF_WRAP_TIGHT: b.wrap = OL_WRAP_SQUARE; break;
    case OWF_WRAP_TOP_BOTTOM: b.wrap = OL_WRAP_TOP_BOTTOM; break;
    default: b.wrap = OL_WRAP_NONE; break;
    }
    if (b.wrap != OL_WRAP_NONE) b.wrap_gap[1] = b.wrap_gap[3] = TW(WRAP_GAP);
    if (fv) {
        /* 2.0.16: a header's or footer's frame, laid out with its story (ol_layout, its corner the galley's) and
         * drawn where the story goes: from the page or its margins, from the story's top left instead; from its
         * paragraph as it is. Its story's text doesn't wrap round it. */
        unsigned ax = OL_ANCHOR_X_OF(b.anchor), ay = OL_ANCHOR_Y_OF(b.anchor);
        if (ax == OL_ANCHOR_MARGIN) b.frame.x += l->page.margin_left;
        if (ax != OL_ANCHOR_BLOCK) b.frame.x -= fv->ox;
        if (ay == OL_ANCHOR_MARGIN) b.frame.y += l->page.margin_top;
        if (ay != OL_ANCHOR_BLOCK) b.frame.y -= fv->oy;
        b.anchor = OL_ANCHOR_X(ax == OL_ANCHOR_BLOCK ? OL_ANCHOR_BLOCK : OL_ANCHOR_PAGE) |
                   OL_ANCHOR_Y(ay == OL_ANCHOR_BLOCK ? OL_ANCHOR_BLOCK : OL_ANCHOR_PAGE) |
                   (fr->behind ? OL_ANCHOR_BEHIND : 0u);
        if (!b.anchor) b.anchor = OL_ANCHOR_ON;
        b.wrap = OL_WRAP_NONE;
    }
    if (OWF_SHADE_SET(fr->line)) {
        b.border_width = BORDER_WIDTH;
        b.border_argb = argb(OWF_SHADE_RGB(fr->line));
        b.border_sides = OL_BORDER_ALL;
    } else {
        b.border_sides = 0;
    }
    ol_node_set_box(f, &b);
    if (fr->image >= 0 && fr->image < doc->nimages) {
        n = ol_node_append(od, f, OL_ROLE_IMAGE, NULL);
        if (!n) return 0;
        ol_style_init(&s);
        s.display = OL_DISPLAY_INLINE;
        s.font_size = OL_POINT(1);                   /* no line height but the picture's */
        s.line_height = b.frame.height;
        ol_node_set_style(n, &s);
        ol_node_set_image(n, fr->image, b.frame.width, b.frame.height);
    }
    /* its paragraphs, recorded as text box k's (2.0.15: the caret goes into them); a frame in them is left
     * out */
    if (!fv && l && fr >= doc->frames && fr < doc->frames + doc->nframes && l->box_frame)
        l->box_frame[fr - doc->frames] = ol_node_id(f);
    return !fr->story.nparas ||
           add_story((ow_layout *)l, doc, od, f, &fr->story, l && !fv ? (int)(fr - doc->frames) + 2 : 0, NULL);
}

static const char *field_text(const field_values *fv, owf_field field)
{
    switch (field) {
    case OWF_FIELD_PAGE: return fv ? fv->page : "1";
    case OWF_FIELD_PAGES: return fv ? fv->pages : "1";
    case OWF_FIELD_DATE: return fv && fv->date ? fv->date : "";
    case OWF_FIELD_TIME: return fv && fv->time ? fv->time : "";
    }
    return "";
}

/* One paragraph: its block, then one node a run, in order, so that run j
 * is node block + 1 + j. */
static ol_node *add_paragraph(ow_layout *l, int record, const owf_doc *doc, ol_document *od, ol_node *parent,
                              const owf_para *p, int index, const field_values *fv)
{
    ol_style s;
    ol_box_style b;
    ol_node *block = ol_node_append(od, parent, p->fmt.heading > 0 ? OL_ROLE_HEADING
                                                                   : OL_ROLE_PARAGRAPH, NULL);
    int j, spacing = p->fmt.line_spacing;

    if (!block) return NULL;
    para_style(doc, p, &s, &b);
    if (l && l->measure && spacing > 0 && spacing <= 100) {
        /* 2.0.11: a single spaced empty line (the one a cover's pictures and text boxes sit in) as tall as a
         * line of text in its first run's font, as the measure gives it, not 1.2 times the size: typing in it
         * then moves nothing below it, nor what is anchored there */
        ol_style ms;
        ol_text_metrics m;
        memset(&m, 0, sizeof m);
        run_style(doc, p->nruns ? &p->runs[0].fmt : &doc->base, spacing, &ms);
        if (l->measure(l->measure_userdata, "x", 1, &ms, &m) && m.ascent + m.descent > 0)
            s.line_height = m.ascent + m.descent;
    }
    ol_node_set_style(block, &s);
    ol_node_set_box(block, &b);
    if (p->fmt.ntabs > 0) {
        /* left, centred, right and decimal stops, with their leaders (OpenLayout 0.3) */
        ol_tab_stop stops[OL_MAX_TABS];
        int k, n = p->fmt.ntabs < OL_MAX_TABS ? p->fmt.ntabs : OL_MAX_TABS;
        for (k = 0; k < n; ++k) {
            const owf_tab *t = &p->fmt.tabs[k];
            stops[k].position = TW(t->position);
            stops[k].align = t->kind == OWF_TAB_CENTRE ? OL_TAB_CENTER : t->kind == OWF_TAB_RIGHT ? OL_TAB_RIGHT
                           : t->kind == OWF_TAB_DECIMAL ? OL_TAB_DECIMAL : OL_TAB_LEFT;
            stops[k].leader = t->leader == OWF_LEADER_DOT ? OL_LEADER_DOT
                            : t->leader == OWF_LEADER_HYPHEN ? OL_LEADER_HYPHEN
                            : t->leader == OWF_LEADER_UNDERSCORE ? OL_LEADER_UNDERSCORE
                            : t->leader == OWF_LEADER_MIDDLE_DOT ? OL_LEADER_MIDDLE_DOT
                            : t->leader == OWF_LEADER_HEAVY ? OL_LEADER_HEAVY : OL_LEADER_NONE;
        }
        if (!ol_node_set_tab_stops(block, n, stops)) return NULL;
    }
    if (!remember(record ? l : NULL, block, record - 1, index, -1)) return NULL;

    for (j = 0; j < p->nruns; ++j) {
        const owf_run *r = &p->runs[j];
        ol_node *n;
        ol_style rs;
        switch (r->kind) {
        case OWF_RUN_TEXT:
            n = ol_node_append(od, block, r->href ? OL_ROLE_LINK : OL_ROLE_TEXT, r->text ? r->text : "");
            if (n && r->href && !ol_node_set_href(n, r->href)) return NULL;
            break;
        case OWF_RUN_TAB: n = ol_node_append(od, block, OL_ROLE_TEXT, "\t"); break;
        case OWF_RUN_LINEBREAK: n = ol_node_append(od, block, OL_ROLE_TEXT, "\n"); break;
        case OWF_RUN_FIELD: n = ol_node_append(od, block, OL_ROLE_TEXT, field_text(fv, r->field)); break;
        case OWF_RUN_IMAGE:
            n = ol_node_append(od, block, OL_ROLE_IMAGE, NULL);
            if (n) {
                const owf_image *im = r->image >= 0 && r->image < doc->nimages ? &doc->images[r->image] : NULL;
                ol_node_set_image(n, r->image, TW(im && im->width > 0 ? im->width : OWF_TWIPS_PER_INCH),
                                  TW(im && im->height > 0 ? im->height : OWF_TWIPS_PER_INCH));
            }
            break;
        case OWF_RUN_FRAME:
            if (r->image >= 0 && r->image < doc->nframes && doc->frames[r->image].in_line) {
                /* 2.0.17: a text box in the line: its room there, a picture of nothing its size (the box itself
                 * is drawn over it, as a frame from the paragraph moved onto it, add_frame) */
                const owf_frame *fr = &doc->frames[r->image];
                n = ol_node_append(od, block, OL_ROLE_IMAGE, NULL);
                if (n) ol_node_set_image(n, OW_LAYOUT_NO_IMAGE, TW(fr->width > 0 ? fr->width : OWF_TWIPS_PER_INCH),
                                         TW(fr->height > 0 ? fr->height : 240));
                break;
            }
            n = ol_node_append(od, block, OL_ROLE_TEXT, "");
            break;
        default:
            n = ol_node_append(od, block, OL_ROLE_TEXT, "");
            break;
        }
        if (!n) return NULL;
        run_style(doc, &r->fmt, spacing, &rs);
        ol_node_set_style(n, &rs);
        if (!remember(record ? l : NULL, n, record - 1, index, j)) return NULL;
    }
    /* 2.0.10: the frames anchored here (an empty run each above), after the runs, so run j stays node
     * block + 1 + j; the body's only (record 1: l, box 0), so a text box's own frames and a header's are
     * left out */
    for (j = 0; ((record == 1 && l) || (l && fv && fv->frames)) && j < p->nruns; ++j)
        if (p->runs[j].kind == OWF_RUN_FRAME && p->runs[j].image >= 0 && p->runs[j].image < doc->nframes &&
            !add_frame(l, doc, od, block, &doc->frames[p->runs[j].image], record == 1 ? NULL : fv))
            return NULL;
    return block;
}

static ol_node *add_cell(ol_document *od, ol_node *row, const owf_para *p)
{
    ol_style s;
    ol_box_style b;
    ol_node *cell = ol_node_append(od, row, OL_ROLE_CELL, NULL);
    if (!cell) return NULL;
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    s.padding_left = s.padding_right = OL_POINT(5) + OL_POINT(2) / 5;   /* Word's 0.19 cm */
    if (p && OWF_SHADE_SET(p->cell_shading)) s.background = argb(OWF_SHADE_RGB(p->cell_shading));
    ol_node_set_style(cell, &s);
    ol_box_style_init(&b);
    b.border_width = BORDER_WIDTH;
    ol_node_set_box(cell, &b);
    return cell;
}

/* Paragraphs first..end-1 share a table id: rows in the order they come,
 * a cell for every column, each paragraph into its cell. */
static int add_table(ow_layout *l, const owf_doc *doc, ol_document *od, ol_node *parent, const owf_story *story,
                     int first, int end, int record, const field_values *fv)
{
    ol_style s;
    ol_node *table, *row = NULL, *cell = NULL;
    int cols = 1, i, row_value = 0, have_row = 0, col = -1;

    for (i = first; i < end; ++i)
        if (story->paras[i].table_cols > cols) cols = story->paras[i].table_cols;
    if (cols > OL_MAX_COLUMNS) cols = OL_MAX_COLUMNS;
    table = ol_node_append(od, parent, OL_ROLE_TABLE, NULL);
    if (!table) return 0;
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    s.margin_bottom = OL_POINT(6);
    ol_node_set_style(table, &s);

    for (i = first; i < end; ++i) {
        const owf_para *p = &story->paras[i];
        int c = p->table_col < 0 ? 0 : (p->table_col >= cols ? cols - 1 : p->table_col);
        if (!have_row || p->table_row != row_value) {
            /* the last row's missing cells, then a new row */
            while (have_row && col < cols - 1) {
                if (!add_cell(od, row, NULL)) return 0;
                ++col;
            }
            row = ol_node_append(od, table, OL_ROLE_ROW, NULL);
            if (!row) return 0;
            ol_style_init(&s);
            s.display = OL_DISPLAY_BLOCK;
            ol_node_set_style(row, &s);
            row_value = p->table_row;
            have_row = 1;
            col = -1;
            cell = NULL;
        }
        while (col < c) {
            cell = add_cell(od, row, p);
            if (!cell) return 0;
            ++col;
        }
        if (!add_paragraph(l, record, doc, od, cell, p, i, fv)) return 0;
    }
    while (have_row && col < cols - 1) {
        if (!add_cell(od, row, NULL)) return 0;
        ++col;
    }
    return 1;
}

static int add_story(ow_layout *l, const owf_doc *doc, ol_document *od, ol_node *parent, const owf_story *story,
                     int record, const field_values *fv)
{
    int i = 0;
    while (i < story->nparas) {
        const owf_para *p = &story->paras[i];
        if (p->table_id >= 0) {
            int end = i + 1;
            while (end < story->nparas && story->paras[end].table_id == p->table_id) ++end;
            if (!add_table(l, doc, od, parent, story, i, end, record, fv)) return 0;
            i = end;
        } else {
            if (!add_paragraph(l, record, doc, od, parent, p, i, fv)) return 0;
            ++i;
        }
    }
    return 1;
}

/* 2.0.22: the ops one story's paragraphs make, about: OpenLayout makes a text op a word (with the spaces after
 * it), one more where a run ends inside a word, one a tab, a field and a picture, a frame's few, and a little
 * a paragraph (an empty one's line, a fill and borders) and a table's cell (its fill and rules). */
static size_t story_ops(const owf_story *st)
{
    size_t n = 0;
    int i, j;
    for (i = 0; i < st->nparas; ++i) {
        const owf_para *p = &st->paras[i];
        int in_word = 0;
        n += 1;
        if (p->table_id >= 0) n += 4;
        if (OWF_SHADE_SET(p->fmt.shading) || p->fmt.borders) n += 5;
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            if (r->kind == OWF_RUN_TEXT && r->text) {
                const unsigned char *t = (const unsigned char *)r->text;
                if (*t && in_word && !(*t == ' ' || *t == '\t')) ++n;   /* a word cut by the run's end */
                for (; *t; ++t) {
                    int space = *t == ' ' || *t == '\t' || *t == '\n' || *t == '\r';
                    if (!space && !in_word) ++n;
                    in_word = !space;
                }
            } else {
                n += r->kind == OWF_RUN_FRAME ? 3 : 1;
                in_word = 0;
            }
        }
    }
    return n;
}

size_t ow_layout_estimate_ops(const owf_doc *doc)
{
    size_t n;
    int k;
    if (!doc) return 0;
    n = story_ops(&doc->body);
    for (k = 0; k < doc->nframes; ++k)
        n += 3 + story_ops(&doc->frames[k].story);
    return n + n / 8 + 32;
}

ow_layout *ow_layout_new(const owf_doc *doc, ol_measure_text_fn measure, void *measure_userdata)
{
    return ow_layout_new_reserved(doc, measure, measure_userdata, 0);
}

static ow_layout *layout_with(const owf_doc *doc, ol_measure_text_fn measure, void *measure_userdata, size_t ops,
                              const ol_unit *inline_dx, const ol_unit *inline_dy);

/* 2.0.17: where each text box in the line went, against its room in the line: how far to move it there, by
 * frame (dx, dy, nframes long). 1 when one needs moving; the boxes whose room is on another page stay. */
static int inline_moves(const ow_layout *l, ol_unit *dx, ol_unit *dy)
{
    const owf_doc *doc = l->doc;
    int i, j, moves = 0;
    for (i = 0; i < doc->body.nparas; ++i) {
        const owf_para *p = &doc->body.paras[i];
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            const ol_node *frame;
            ol_id room = l->blocks[i] + 1 + (ol_id)j;
            size_t first = 0, count = 0, k;
            ol_rect at;
            int fp, rp;
            if (r->kind != OWF_RUN_FRAME || r->image < 0 || r->image >= doc->nframes || !doc->frames[r->image].in_line ||
                !l->box_frame || !l->box_frame[r->image])
                continue;
            frame = ol_node_by_id(l->body, l->box_frame[r->image]);
            if (!frame || !ol_frame_place(l->body, frame, &fp, &at)) continue;
            for (rp = 0; rp < ol_page_count(l->body); ++rp) {
                if (!ol_display_page_range(l->body, rp, &first, &count)) continue;
                for (k = first; k < first + count; ++k) {
                    const ol_display_op *op = ol_display_get(l->body, k);
                    if (op->type != OL_OP_IMAGE || op->u.image.node_id != room) continue;
                    if (rp == fp && (op->u.image.bounds.x != at.x || op->u.image.bounds.y != at.y)) {
                        dx[r->image] += op->u.image.bounds.x - at.x;
                        dy[r->image] += op->u.image.bounds.y - at.y;
                        moves = 1;
                    }
                    rp = ol_page_count(l->body);
                    break;
                }
            }
        }
    }
    return moves;
}

ow_layout *ow_layout_new_reserved(const owf_doc *doc, ol_measure_text_fn measure, void *measure_userdata,
                                  size_t ops)
{
    ow_layout *l;
    ol_unit *dx, *dy;
    int i, in_line = 0;
    if (!doc) return NULL;
    /* 2.0.22: the first layout's room from a guess (ow_layout_estimate_ops), so its display list is made once,
     * not doubled from 64 up (eight blocks a 20-page document, each copied into the next) */
    if (!ops) ops = ow_layout_estimate_ops(doc);
    l = layout_with(doc, measure, measure_userdata, ops, NULL, NULL);
    for (i = 0; l && !in_line && i < doc->nframes; ++i) in_line = doc->frames[i].in_line;
    if (!in_line) return l;
    /* 2.0.17: a text box in the line goes from its paragraph's corner onto its room in the line: laid out again
     * with it moved there (it moves no line: it doesn't wrap) */
    dx = (ol_unit *)calloc((size_t)doc->nframes, sizeof(*dx));
    dy = (ol_unit *)calloc((size_t)doc->nframes, sizeof(*dy));
    if (dx && dy && inline_moves(l, dx, dy)) {
        ow_layout *moved;
        ow_layout_free(l);
        moved = layout_with(doc, measure, measure_userdata, ops, dx, dy);
        l = moved;
    }
    if (l && l->inline_dx == dx) {              /* the moved layout keeps its offsets */
        dx = dy = NULL;
    }
    free(dx);
    free(dy);
    return l;
}

static ow_layout *layout_with(const owf_doc *doc, ol_measure_text_fn measure, void *measure_userdata, size_t ops,
                              const ol_unit *inline_dx, const ol_unit *inline_dy)
{
    ow_layout *l;
    ol_document *body;
    const owf_page *pg;
    size_t i;
    int k;

    if (!doc) return NULL;
    /* 2.0.13: the pages' display list before anything else (OpenLayout 0.5's ol_document_new_reserved) */
    body = ops ? ol_document_new_reserved(ops) : ol_document_new();
    if (!body) return NULL;
    l = (ow_layout *)calloc(1, sizeof(*l));
    if (!l) {
        ol_document_free(body);
        return NULL;
    }
    l->body = body;
    l->doc = doc;
    l->measure = measure;
    l->measure_userdata = measure_userdata;
    l->inline_dx = inline_dx;
    l->inline_dy = inline_dy;
    pg = &doc->page;                                 /* 2.0.10: first, for the frames placed on it */
    l->page.width = TW(pg->width > 0 ? pg->width : 11906);
    l->page.height = TW(pg->height > 0 ? pg->height : 16838);
    l->page.margin_top = TW(pg->margin_top);
    l->page.margin_right = TW(pg->margin_right);
    l->page.margin_bottom = TW(pg->margin_bottom);
    l->page.margin_left = TW(pg->margin_left);
    if (l->page.width - l->page.margin_left - l->page.margin_right <= 0 ||
        l->page.height - l->page.margin_top - l->page.margin_bottom <= 0) {
        l->page.margin_top = l->page.margin_bottom = TW(OWF_TWIPS_PER_INCH);
        l->page.margin_left = l->page.margin_right = TW(OWF_TWIPS_PER_INCH);
    }
    l->blocks = (ol_id *)calloc((size_t)(doc->body.nparas ? doc->body.nparas : 1), sizeof(*l->blocks));
    if (doc->nframes > 0) {                          /* 2.0.15: the text boxes' */
        l->nboxes = doc->nframes;
        l->box_frame = (ol_id *)calloc((size_t)doc->nframes, sizeof(*l->box_frame));
        l->box_blocks = (ol_id **)calloc((size_t)doc->nframes, sizeof(*l->box_blocks));
        if (!l->box_frame || !l->box_blocks) {
            ow_layout_free(l);
            return NULL;
        }
        for (k = 0; k < doc->nframes; ++k)
            if (doc->frames[k].story.nparas &&
                !(l->box_blocks[k] = (ol_id *)calloc((size_t)doc->frames[k].story.nparas, sizeof(ol_id)))) {
                ow_layout_free(l);
                return NULL;
            }
    }
    if (!l->body || !l->blocks || !add_story(l, doc, l->body, NULL, &doc->body, 1, NULL)) {
        ow_layout_free(l);
        return NULL;
    }
    /* each paragraph's block: the node remembered with run -1 */
    for (i = 0; i < l->caprefs; ++i)
        if (l->refs[i].paragraph >= 0 && l->refs[i].run < 0) {
            int b = l->refs[i].box;
            if (!b) l->blocks[l->refs[i].paragraph] = (ol_id)i;
            else if (b <= l->nboxes && l->box_blocks[b - 1] && l->refs[i].paragraph < doc->frames[b - 1].story.nparas)
                l->box_blocks[b - 1][l->refs[i].paragraph] = (ol_id)i;
        }
    for (k = 0; k < doc->body.nparas; ++k)
        if (!l->blocks[k]) {
            ow_layout_free(l);
            return NULL;
        }
    if (!ol_layout_pages(l->body, &l->page, measure, measure_userdata)) {
        ow_layout_free(l);
        return NULL;
    }
    return l;
}

void ow_layout_free(ow_layout *l)
{
    int k;
    if (!l) return;
    ol_document_free(l->body);
    free(l->refs);
    free(l->blocks);
    for (k = 0; l->box_blocks && k < l->nboxes; ++k) free(l->box_blocks[k]);
    free(l->box_blocks);
    free(l->box_frame);
    free((void *)l->inline_dx);              /* 2.0.17: the moved layout's own */
    free((void *)l->inline_dy);
    free(l);
}

/* 2.0.15: a position's story, and its paragraphs' blocks: the body's, or text box box's; NULL when it has none
 * laid out. */
static const owf_story *pos_story(const ow_layout *l, int box, const ol_id **blocks)
{
    if (!box) {
        *blocks = l->blocks;
        return &l->doc->body;
    }
    if (box < 0 || box > l->nboxes || !l->box_blocks || !l->box_blocks[box - 1] || !l->box_frame[box - 1]) return NULL;
    *blocks = l->box_blocks[box - 1];
    return &l->doc->frames[box - 1].story;
}

ol_document *ow_layout_body(const ow_layout *l)
{
    return l ? l->body : NULL;
}

int ow_layout_page_count(const ow_layout *l)
{
    return l ? ol_page_count(l->body) : 0;
}

const ol_page *ow_layout_page(const ow_layout *l)
{
    return l ? &l->page : NULL;
}

int ow_layout_to_text(const ow_layout *l, ow_position pos, ol_text_position *out)
{
    const owf_para *p;
    const owf_story *story;
    const ol_id *blocks = NULL;
    if (!l || !out || !(story = pos_story(l, pos.box, &blocks)) || pos.paragraph < 0 ||
        pos.paragraph >= story->nparas)
        return 0;
    p = &story->paras[pos.paragraph];
    if (pos.run < 0 || pos.run >= p->nruns) {
        out->node_id = blocks[pos.paragraph];
        out->offset = 0;
        return 1;
    }
    out->node_id = blocks[pos.paragraph] + 1 + (ol_id)pos.run;
    out->offset = 0;
    if (p->runs[pos.run].kind == OWF_RUN_TEXT && p->runs[pos.run].text) {
        size_t len = strlen(p->runs[pos.run].text);
        out->offset = pos.byte_offset < len ? pos.byte_offset : len;
    }
    return 1;
}

/* The editor keeps positions in text runs: next to a tab, a field or a
 * picture, the nearest text run's end or start. */
/* The editor keeps positions in text runs: a place before a tab, field or
 * image is the end of the text run ahead of it, a place after one the start
 * of the text run behind it. */
static int text_run_before(const owf_para *p, int run, ow_position *out)
{
    int j;
    for (j = run - 1; j >= 0; --j)
        if (p->runs[j].kind == OWF_RUN_TEXT) {
            out->run = j;
            out->byte_offset = p->runs[j].text ? strlen(p->runs[j].text) : 0;
            return 1;
        }
    return 0;
}

static int text_run_after(const owf_para *p, int run, ow_position *out)
{
    int j;
    for (j = run + 1; j < p->nruns; ++j)
        if (p->runs[j].kind == OWF_RUN_TEXT) {
            out->run = j;
            out->byte_offset = 0;
            return 1;
        }
    return 0;
}

static void into_text_run(const owf_para *p, int run, size_t offset, ow_position *out)
{
    if (run >= 0 && run < p->nruns && p->runs[run].kind == OWF_RUN_TEXT) {
        out->run = run;
        out->byte_offset = offset;
        return;
    }
    if (offset == 0 && run >= 0 ? text_run_before(p, run, out) || text_run_after(p, run, out)
                                : text_run_after(p, run, out) || text_run_before(p, run, out))
        return;
    out->run = 0;
    out->byte_offset = 0;
}

int ow_layout_from_text(const ow_layout *l, const ol_text_position *in, ow_position *out)
{
    node_ref r;
    const owf_story *story;
    const ol_id *blocks = NULL;
    if (!l || !in || !out || in->node_id >= l->caprefs) return 0;
    r = l->refs[in->node_id];
    if (!(story = pos_story(l, r.box, &blocks)) || r.paragraph < 0 || r.paragraph >= story->nparas) return 0;
    out->paragraph = r.paragraph;
    out->box = r.box;
    into_text_run(&story->paras[r.paragraph], r.run < 0 ? -1 : r.run, r.run < 0 ? 0 : in->offset, out);
    return 1;
}

int ow_layout_node_ref(const ow_layout *l, ol_id node_id, int *box, int *paragraph, int *run)
{
    node_ref r;
    const owf_story *story;
    const ol_id *blocks = NULL;
    if (!l || node_id >= l->caprefs) return 0;
    r = l->refs[node_id];
    if (!(story = pos_story(l, r.box, &blocks)) || r.paragraph < 0 || r.paragraph >= story->nparas) return 0;
    if (box) *box = r.box;
    if (paragraph) *paragraph = r.paragraph;
    if (run) *run = r.run;
    return 1;
}

int ow_layout_node_run(const ow_layout *l, ol_id node_id, int *paragraph, int *run)
{
    int box = 0;
    return ow_layout_node_ref(l, node_id, &box, paragraph, run) && !box;
}

int ow_layout_caret(const ow_layout *l, ow_position pos, int *page, ol_rect *box)
{
    ol_text_position t;
    const ol_id *blocks = NULL;
    if (!ow_layout_to_text(l, pos, &t)) return 0;
    if (ol_caret_box(l->body, &t, l->measure, l->measure_userdata, page, box)) return 1;
    pos_story(l, pos.box, &blocks);
    if (pos.box) {
        /* 2.0.15: in a text box, an empty paragraph's corner in its frame, where the frame went (the frame's
         * nodes are where it was laid out, at its own corner) */
        const ol_node *frame = ol_node_by_id(l->body, l->box_frame[pos.box - 1]);
        const ol_node *block = ol_node_by_id(l->body, blocks[pos.paragraph]);
        ol_rect at, b;
        int fp;
        if (!frame || !block || !ol_frame_place(l->body, frame, &fp, &at)) return 0;
        b = ol_node_bounds(block);
        box->x = at.x + b.x + ol_node_style(block)->padding_left;
        box->y = at.y + b.y + ol_node_style(block)->padding_top;
        box->width = 0;
        box->height = ol_node_style(block)->line_height > 0 ? ol_node_style(block)->line_height : OL_POINT(12);
        if (page) *page = fp;
        return 1;
    }
    /* a run with nothing laid out (empty text): its paragraph's start */
    t.node_id = blocks[pos.paragraph];
    t.offset = 0;
    return ol_caret_box(l->body, &t, l->measure, l->measure_userdata, page, box);
}

/* A paragraph with no text to lay out (its caret has no line of its own). */
static int para_is_empty(const owf_para *p)
{
    int j;
    for (j = 0; j < p->nruns; ++j)
        if (p->runs[j].kind != OWF_RUN_TEXT || (p->runs[j].text && p->runs[j].text[0])) return 0;
    return 1;
}

static ol_unit gap_to(ol_unit v, ol_unit lo, ol_unit len)
{
    if (v < lo) return lo - v;
    if (v >= lo + len) return v - (lo + len) + 1;
    return 0;
}

/* 2.0.15: the text box in front of the text under a point on a page, the front one first (the later frames are
 * drawn over the earlier); in it, the character nearest the point, as ol_hit_text finds one in the body (which
 * leaves frames out): on the nearest line, before or after the character under x. An empty box: its start. */
static int hit_box(const ow_layout *l, int page, ol_unit x, ol_unit y, ow_position *pos)
{
    int k;
    for (k = l->nboxes; k >= 1; --k) {
        const owf_frame *fr = &l->doc->frames[k - 1];
        const ol_node *frame;
        ol_rect at;
        size_t first = 0, count = 0, i, best = (size_t)-1, c, best_c = 0;
        ol_unit best_dy = 0, best_dx = 0, best_d;
        ol_text_position t;
        const ol_display_op *op;
        const char *text;
        int fp;
        if (!fr->story.nparas || fr->behind || !l->box_frame[k - 1] || !l->box_blocks[k - 1]) continue;
        frame = ol_node_by_id(l->body, l->box_frame[k - 1]);
        /* inside it: not on its left or right edge, where the line's caret before and after a box in the line is */
        if (!frame || !ol_frame_place(l->body, frame, &fp, &at) || fp != page || x <= at.x || x >= at.x + at.width ||
            y < at.y || y >= at.y + at.height)
            continue;
        if (!ol_display_page_range(l->body, page, &first, &count)) return 0;
        for (i = first; i < first + count; ++i) {
            ol_unit dy, dx;
            op = ol_display_get(l->body, i);
            if (op->type != OL_OP_TEXT || op->u.text.node_id >= l->caprefs || l->refs[op->u.text.node_id].box != k)
                continue;
            dy = gap_to(y, op->u.text.bounds.y, op->u.text.bounds.height);
            dx = gap_to(x, op->u.text.bounds.x, op->u.text.bounds.width);
            if (best == (size_t)-1 || dy < best_dy || (dy == best_dy && dx < best_dx)) {
                best = i;
                best_dy = dy;
                best_dx = dx;
            }
        }
        if (best == (size_t)-1 || best_dy > 0) {
            /* no line of text under the point: an empty paragraph's line there (their blocks are where the frame
             * was laid out, at its own corner), or, with no text at all, the box's first paragraph */
            int pi;
            for (pi = 0; pi < fr->story.nparas; ++pi) {
                const ol_node *b = ol_node_by_id(l->body, l->box_blocks[k - 1][pi]);
                ol_rect r;
                if (!b) continue;
                r = ol_node_bounds(b);
                if (y >= at.y + r.y && y < at.y + r.y + r.height) {
                    t.node_id = l->box_blocks[k - 1][pi];
                    t.offset = 0;
                    if (best == (size_t)-1 || para_is_empty(&fr->story.paras[pi]))
                        return ow_layout_from_text(l, &t, pos);
                }
            }
            if (best == (size_t)-1) {
                t.node_id = l->box_blocks[k - 1][0];
                t.offset = 0;
                return ow_layout_from_text(l, &t, pos);
            }
        }
        op = ol_display_get(l->body, best);
        text = ol_node_text(ol_node_by_id(l->body, op->u.text.node_id));
        if (!text) return 0;
        text += op->u.text.text_offset;
        best_d = x > op->u.text.bounds.x ? x - op->u.text.bounds.x : op->u.text.bounds.x - x;
        for (c = 1; c <= op->u.text.text_length; ++c) {
            ol_text_metrics m;
            ol_unit edge, d;
            if (c < op->u.text.text_length && ((unsigned char)text[c] & 0xc0u) == 0x80u) continue;
            memset(&m, 0, sizeof m);
            if (op->u.text.text_length == 1 && text[0] == '\t') m.width = op->u.text.bounds.width;
            else if (!l->measure || !l->measure(l->measure_userdata, text, c, &op->u.text.style, &m)) return 0;
            edge = op->u.text.bounds.x + m.width;
            d = x > edge ? x - edge : edge - x;
            if (d < best_d) {
                best_d = d;
                best_c = c;
            }
            if (edge >= x) break;
        }
        t.node_id = op->u.text.node_id;
        t.offset = op->u.text.text_offset + best_c;
        return ow_layout_from_text(l, &t, pos);
    }
    return 0;
}

int ow_layout_hit(const ow_layout *l, int page, ol_unit x, ol_unit y, ow_position *pos)
{
    ol_text_position t;
    const ol_node *n;
    if (!l || !pos) return 0;
    memset(pos, 0, sizeof(*pos));
    if (hit_box(l, page, x, y, pos)) return 1;      /* 2.0.15: a text box in front of the text */
    if (ol_hit_text(l->body, page, x, y, l->measure, l->measure_userdata, &t))
        return ow_layout_from_text(l, &t, pos);
    /* a page with no text (empty paragraphs): the paragraph under the point */
    n = ol_hit_test_page(l->body, page, x, y);
    if (!n) return 0;
    t.node_id = ol_node_id(n);
    t.offset = 0;
    return ow_layout_from_text(l, &t, pos);
}

static void roman(int n, int lower, char *out, size_t size)
{
    static const int value[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const upper[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
    size_t used = 0;
    int i;
    if (size == 0) return;
    out[0] = 0;
    if (n <= 0 || n >= 4000) {
        snprintf(out, size, "%d", n);
        return;
    }
    for (i = 0; i < 13; ++i)
        while (n >= value[i]) {
            const char *s = upper[i];
            for (; *s && used + 1 < size; ++s)
                out[used++] = lower ? (char)(*s - 'A' + 'a') : *s;
            n -= value[i];
        }
    out[used] = 0;
}

void ow_layout_page_number(const owf_page *page, int number, char *out, size_t size)
{
    owf_pagenum_style style = page ? page->pagenum_style : OWF_PAGENUM_ARABIC;
    if (!out || !size) return;
    switch (style) {
    case OWF_PAGENUM_ROMAN_UPPER: roman(number, 0, out, size); break;
    case OWF_PAGENUM_ROMAN_LOWER: roman(number, 1, out, size); break;
    case OWF_PAGENUM_LETTER_UPPER:
    case OWF_PAGENUM_LETTER_LOWER:
        if (number > 0) {
            /* as Word writes them: A to Z, then AA to ZZ, then AAA */
            char c = (char)((style == OWF_PAGENUM_LETTER_UPPER ? 'A' : 'a') + (number - 1) % 26);
            size_t times = (size_t)(number - 1) / 26 + 1, i;
            if (times >= size) times = size - 1;
            for (i = 0; i < times; ++i) out[i] = c;
            out[times] = 0;
            break;
        }
        /* fall through */
    default:
        snprintf(out, size, "%d", number);
        break;
    }
}

/* How far down a laid-out story's ops reach. */
/* Whether a story's paragraphs hold frames. */
static int story_has_frames(const owf_story *story)
{
    int i, j;
    for (i = 0; i < story->nparas; ++i)
        for (j = 0; j < story->paras[i].nruns; ++j)
            if (story->paras[i].runs[j].kind == OWF_RUN_FRAME) return 1;
    return 0;
}

static ol_unit story_height(const ol_document *od)
{
    size_t i, n = ol_display_count(od);
    ol_unit bottom = 0, b;
    for (i = 0; i < n; ++i) {
        const ol_display_op *op = ol_display_get(od, i);
        switch (op->type) {
        case OL_OP_TEXT: b = op->u.text.bounds.y + op->u.text.bounds.height; break;
        case OL_OP_FILL_RECT: b = op->u.fill_rect.bounds.y + op->u.fill_rect.bounds.height; break;
        case OL_OP_IMAGE: b = op->u.image.bounds.y + op->u.image.bounds.height; break;
        case OL_OP_RULE: b = (op->u.rule.y1 > op->u.rule.y2 ? op->u.rule.y1 : op->u.rule.y2) +
                             op->u.rule.thickness; break;
        default: b = 0; break;
        }
        if (b > bottom) bottom = b;
    }
    return bottom;
}

ol_document *ow_layout_story(const ow_layout *l, ow_story_kind which, int page,
                             const char *date, const char *time, ol_unit *x, ol_unit *y)
{
    const owf_story *story;
    ol_document *od;
    field_values fv;
    int first = l ? (l->doc->page.start_page > 0 ? l->doc->page.start_page : 1) : 1;
    int head = which == OW_STORY_HEADER || which == OW_STORY_HEADER_LEFT;

    if (!l) return NULL;
    /* 2.0.23: an even-numbered page (as the document numbers it) is a left one, with left pages' own when
     * there are; OW_STORY_HEADER_LEFT and OW_STORY_FOOTER_LEFT ask for those on any page */
    if (which == OW_STORY_HEADER_LEFT || which == OW_STORY_FOOTER_LEFT)
        story = head ? &l->doc->header_left : &l->doc->footer_left;
    else if (!((first + page) & 1) && (head ? l->doc->header_left.nparas : l->doc->footer_left.nparas))
        story = head ? &l->doc->header_left : &l->doc->footer_left;
    else
        story = head ? &l->doc->header : &l->doc->footer;
    if (!story->nparas) return NULL;
    if (page == 0 && !(head ? l->doc->page.header_on_first : l->doc->page.footer_on_first))
        return NULL;
    ow_layout_page_number(&l->doc->page, first + page, fv.page, sizeof fv.page);
    ow_layout_page_number(&l->doc->page, first + ow_layout_page_count(l) - 1, fv.pages, sizeof fv.pages);
    fv.date = date;
    fv.time = time;
    fv.frames = 0;
    fv.ox = fv.oy = 0;
    od = ol_document_new();
    if (!od) return NULL;
    if (!add_story((ow_layout *)l, l->doc, od, NULL, story, 0, &fv) ||
        !ol_layout(od, l->page.width - l->page.margin_left - l->page.margin_right,
                   l->measure, l->measure_userdata)) {
        ol_document_free(od);
        return NULL;
    }
    /* half way into the top margin; the footer ends half way into the bottom one */
    fv.ox = l->page.margin_left;
    fv.oy = head ? l->page.margin_top / 2
                                     : l->page.height - l->page.margin_bottom / 2 - story_height(od);
    if (x) *x = fv.ox;
    if (y) *y = fv.oy;
    if (story_has_frames(story)) {
        /* 2.0.16: laid out again with its frames, now that its place on the page is known */
        ol_document_free(od);
        fv.frames = 1;
        od = ol_document_new();
        if (!od) return NULL;
        if (!add_story((ow_layout *)l, l->doc, od, NULL, story, 0, &fv) ||
            !ol_layout(od, l->page.width - l->page.margin_left - l->page.margin_right,
                       l->measure, l->measure_userdata)) {
            ol_document_free(od);
            return NULL;
        }
    }
    return od;
}

/* Shared E5: any story into any node (a slide's frame, a report's band). */
int ow_layout_add_story(ol_document *od, ol_node *parent, const owf_doc *doc, const owf_story *story,
                        const char *const fields[4])
{
    field_values fv;
    if (!od || !doc || !story) return 0;
    snprintf(fv.page, sizeof fv.page, "%s", fields && fields[0] ? fields[0] : "1");
    snprintf(fv.pages, sizeof fv.pages, "%s", fields && fields[1] ? fields[1] : "1");
    fv.date = fields ? fields[2] : NULL;
    fv.time = fields ? fields[3] : NULL;
    fv.frames = 0;
    fv.ox = fv.oy = 0;
    return add_story(NULL, doc, od, parent, story, 0, &fv);
}

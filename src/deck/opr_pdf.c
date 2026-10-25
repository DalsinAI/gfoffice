/*
 * OpenPresentation's core: the deck as a PDF, a page a slide, each slide
 * drawn from its OpenLayout display list into the page PDF writer
 * (owf_pdf.h), with what OpenLayout 0.2 can't draw (shapes other than
 * rectangles, a picture's crop) drawn at its frame's box.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "openpresentation_core.h"
#include "ow_layout.h"
#include "owf_pdf.h"

#include <stdlib.h>
#include <string.h>

#define U2T(v) ((int)((v) / OL_UNITS_PER_TWIP))

static unsigned long shade_of(uint32_t argb)
{
    return (argb >> 24) ? OWF_SHADE(argb & 0xFFFFFFu) : 0;
}

static long isqrt(long v)
{
    long r = 0, bit = 1L << 30;
    if (v <= 0)
        return 0;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* An arrowhead at (x2, y2), pointing away from (x1, y1). */
static void arrowhead(owf_pdf *pdf, int x1, int y1, int x2, int y2, int width, unsigned long colour)
{
    long dx = x2 - x1, dy = y2 - y1, len = isqrt(dx * dx + dy * dy), size = width * 3 > 120 ? width * 3 : 120;
    int pts[6];
    if (len <= 0)
        return;
    /* back along the line by size, out to each side by size / 2 */
    pts[0] = x2;
    pts[1] = y2;
    pts[2] = (int)(x2 - dx * size / len - dy * size / (2 * len));
    pts[3] = (int)(y2 - dy * size / len + dx * size / (2 * len));
    pts[4] = (int)(x2 - dx * size / len + dy * size / (2 * len));
    pts[5] = (int)(y2 - dy * size / len - dx * size / (2 * len));
    owf_pdf_path(pdf, pts, 3, 1, colour, 0, 0);
}

static int dashed(const owf_look *l)
{
    return l->line_style == OWF_LINE_DASHED || l->line_style == OWF_LINE_DOTTED ||
           l->line_style == OWF_LINE_MEDIUM_DASHED || l->line_style >= OWF_LINE_DASH_DOT;
}

/* A shape OpenLayout 0.2 doesn't draw, at its box. */
static void draw_shape(owf_pdf *pdf, const owf_obj *o)
{
    int x = o->x, y = o->y, w = o->width, h = o->height, pts[16], k;
    unsigned long fill = o->look.fill, line = o->look.line;
    int lw = o->look.line_width;
    owf_pdf_dash(pdf, dashed(&o->look));
    switch (o->shape) {
    case OWF_SHAPE_ROUNDRECT:
        owf_pdf_round_rect(pdf, x, y, w, h, o->radius, fill, line, lw);
        break;
    case OWF_SHAPE_ELLIPSE:
        owf_pdf_ellipse(pdf, x, y, w, h, fill, line, lw);
        break;
    case OWF_SHAPE_TRIANGLE:
        pts[0] = x + w / 2; pts[1] = y;
        pts[2] = x + w; pts[3] = y + h;
        pts[4] = x; pts[5] = y + h;
        if (o->flags & OWF_OBJ_FLIP_V) {
            pts[1] = y + h;
            pts[3] = pts[5] = y;
        }
        owf_pdf_path(pdf, pts, 3, 1, fill, line, lw);
        break;
    case OWF_SHAPE_DIAMOND:
        pts[0] = x + w / 2; pts[1] = y;
        pts[2] = x + w; pts[3] = y + h / 2;
        pts[4] = x + w / 2; pts[5] = y + h;
        pts[6] = x; pts[7] = y + h / 2;
        owf_pdf_path(pdf, pts, 4, 1, fill, line, lw);
        break;
    case OWF_SHAPE_ARROW: {
        /* a block arrow pointing right (left when flipped) */
        static const int ax[7] = { 0, 750, 750, 1000, 750, 750, 0 };
        static const int ay[7] = { 250, 250, 0, 500, 1000, 750, 750 };
        for (k = 0; k < 7; k++) {
            int px = (int)((long)w * ax[k] / 1000);
            pts[k * 2] = (o->flags & OWF_OBJ_FLIP_H) ? x + w - px : x + px;
            pts[k * 2 + 1] = y + (int)((long)h * ay[k] / 1000);
        }
        owf_pdf_path(pdf, pts, 7, 1, fill, line, lw);
        break;
    }
    case OWF_SHAPE_LINE: {
        int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
        if (o->flags & OWF_OBJ_FLIP_H) { x1 = x + w; x2 = x; }
        if (o->flags & OWF_OBJ_FLIP_V) { y1 = y + h; y2 = y; }
        pts[0] = x1; pts[1] = y1; pts[2] = x2; pts[3] = y2;
        owf_pdf_path(pdf, pts, 2, 0, 0, line, lw);
        owf_pdf_dash(pdf, 0);
        if (OWF_SHADE_SET(line) && (o->look.arrows & OWF_ARROW_END))
            arrowhead(pdf, x1, y1, x2, y2, lw, line);
        if (OWF_SHADE_SET(line) && (o->look.arrows & OWF_ARROW_START))
            arrowhead(pdf, x2, y2, x1, y1, lw, line);
        break;
    }
    case OWF_SHAPE_POLYGON: {
        int *xy;
        if (o->npoints < 2)
            break;
        xy = malloc((size_t)o->npoints * 2 * sizeof *xy);
        if (!xy)
            break;
        for (k = 0; k < o->npoints; k++) {
            xy[k * 2] = x + o->points[k * 2];
            xy[k * 2 + 1] = y + o->points[k * 2 + 1];
        }
        owf_pdf_path(pdf, xy, o->npoints, 1, fill, line, lw);
        free(xy);
        break;
    }
    default:
        break;
    }
    owf_pdf_dash(pdf, 0);
}

/* A text piece with something to see in it. */
static int visible(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r')
            return 1;
    return 0;
}

typedef struct {
    owf_pdf *pdf;
    const opr_slide *slide;
    const owf_deck *deck;
    int next_frame;              /* the first frame whose shape is still to draw */
    int failed;
} drawing;

static void shapes_up_to(drawing *d, int frame)
{
    for (; d->next_frame <= frame && d->next_frame < d->slide->nframes; d->next_frame++) {
        const opr_frame *f = &d->slide->frames[d->next_frame];
        if (f->obj && f->obj->kind == OWF_OBJ_SHAPE && f->obj->shape != OWF_SHAPE_RECT)
            draw_shape(d->pdf, &f->box);
    }
}

static void draw_op(void *userdata, const ol_device_op *dop)
{
    drawing *d = (drawing *)userdata;
    const ol_display_op *op = dop->op;
    int frame = opr_slide_frame_of(d->slide, op);
    shapes_up_to(d, frame);
    switch (op->type) {
    case OL_OP_FILL_RECT:
        owf_pdf_rect(d->pdf, dop->box.x, dop->box.y, dop->box.width, dop->box.height,
                     shade_of(op->u.fill_rect.argb), 0, 0);
        break;
    case OL_OP_RULE: {
        int pts[4];
        pts[0] = dop->x1;
        pts[1] = dop->y1;
        pts[2] = dop->x2;
        pts[3] = dop->y2;
        owf_pdf_path(d->pdf, pts, 2, 0, 0, shade_of(op->u.rule.argb), dop->thickness);
        break;
    }
    case OL_OP_TEXT: {
        const ol_node *n = ol_node_by_id(d->slide->doc, op->u.text.node_id);
        const char *text = n ? ol_node_text(n) : NULL;
        const ol_style *st = &op->u.text.style;
        int font = OW_LAYOUT_FONT_INDEX(OL_TEXT_FONT_OF(st->text_flags));
        owf_font_kind kind = (st->text_flags & OL_TEXT_MONO) ? OWF_FONT_MONO : opr_font_kind(d->deck, font);
        unsigned flags = 0;
        int baseline = dop->baseline, size = U2T(st->font_size);
        if (!text || !visible(text + op->u.text.text_offset, op->u.text.text_length))
            break;
        if (st->text_flags & OL_TEXT_BOLD) flags |= OWF_BOLD;
        if (st->text_flags & OL_TEXT_ITALIC) flags |= OWF_ITALIC;
        if (st->text_flags & OL_TEXT_UNDERLINE) flags |= OWF_UNDERLINE;
        if (st->text_flags & OL_TEXT_STRIKE) flags |= OWF_STRIKE;
        if (st->text_flags & OL_TEXT_SUPER) baseline -= size / 2;
        if (st->text_flags & OL_TEXT_SUB) baseline += size / 4;
        /* trailing spaces measured into the piece don't widen its glyphs */
        {
            size_t len = op->u.text.text_length;
            const char *t = text + op->u.text.text_offset;
            int width = dop->box.width;
            while (len && (t[len - 1] == ' ' || t[len - 1] == '\t' || t[len - 1] == '\n'))
                len--;
            if (len < op->u.text.text_length)
                width = owf_pdf_text_width(t, len, kind, flags & (OWF_BOLD | OWF_ITALIC), size) * width /
                        (owf_pdf_text_width(t, op->u.text.text_length, kind, flags & (OWF_BOLD | OWF_ITALIC), size) + 1);
            if (owf_pdf_text(d->pdf, dop->box.x, baseline, width, t, len, kind, flags, size,
                             st->foreground & 0xFFFFFFu) < 0)
                d->failed = 1;
        }
        if (ol_node_role(n) == OL_ROLE_LINK && ol_node_href(n))
            owf_pdf_link(d->pdf, dop->box.x, dop->box.y, dop->box.width, dop->box.height, ol_node_href(n));
        break;
    }
    case OL_OP_IMAGE: {
        int image = op->u.image.image_id;
        const owf_obj *o = frame >= 0 ? d->slide->frames[frame].obj : NULL;
        if (image < 0 || image >= d->deck->res->nimages)
            break;
        if (o && o->kind == OWF_OBJ_PICTURE && (o->crop[0] || o->crop[1] || o->crop[2] || o->crop[3]) &&
            o->crop[0] + o->crop[2] < 1000 && o->crop[1] + o->crop[3] < 1000) {
            /* the whole picture, larger, shown only inside its box */
            int clip[4], w, h;
            clip[0] = dop->box.x;
            clip[1] = dop->box.y;
            clip[2] = dop->box.width;
            clip[3] = dop->box.height;
            w = (int)((long)clip[2] * 1000 / (1000 - o->crop[0] - o->crop[2]));
            h = (int)((long)clip[3] * 1000 / (1000 - o->crop[1] - o->crop[3]));
            owf_pdf_image(d->pdf, &d->deck->res->images[image], clip[0] - (int)((long)w * o->crop[0] / 1000),
                          clip[1] - (int)((long)h * o->crop[1] / 1000), w, h, clip);
        } else {
            owf_pdf_image(d->pdf, &d->deck->res->images[image], dop->box.x, dop->box.y, dop->box.width,
                          dop->box.height, NULL);
        }
        break;
    }
    default:
        break;
    }
}

int opr_export_pdf(const owf_deck *deck, const opr_pdf_options *options, unsigned char **data, size_t *length,
                   owf_report *report)
{
    owf_pdf *pdf;
    ol_scale scale;
    int i, result, overflow = 0;

    *data = NULL;
    *length = 0;
    if (!deck || !deck->nmasters)
        return OWF_ERR_FORMAT;
    pdf = owf_pdf_new();
    if (!pdf)
        return OWF_ERR_MEMORY;
    /* the display list in twips: a unit is a quarter of one */
    memset(&scale, 0, sizeof scale);
    ol_scale_set(&scale, 1, OL_UNITS_PER_TWIP);
    for (i = 0; i < deck->nslides; i++) {
        const owf_slide *s = &deck->slides[i];
        opr_slide *laid;
        drawing d;
        if (s->hidden && !(options && options->with_hidden))
            continue;
        laid = opr_slide_layout(deck, i, NULL, NULL, options ? options->today : NULL);
        if (!laid || owf_pdf_page(pdf, deck->width, deck->height) < 0) {
            opr_slide_free(laid);
            owf_pdf_free(pdf);
            return OWF_ERR_MEMORY;
        }
        memset(&d, 0, sizeof d);
        d.pdf = pdf;
        d.slide = laid;
        d.deck = deck;
        ol_draw_page(laid->doc, 0, &scale, NULL, draw_op, &d);
        shapes_up_to(&d, laid->nframes);
        overflow |= laid->overflow;
        opr_slide_free(laid);
        if (d.failed) {
            owf_pdf_free(pdf);
            return OWF_ERR_MEMORY;
        }
    }
    if (overflow)
        owf_report_add(report, OWF_NOTE_APPROX, "Text that runs below a slide's bottom edge is cut off there");
    result = owf_pdf_finish(pdf, deck->res->title, data, length, report);
    owf_pdf_free(pdf);
    return result;
}

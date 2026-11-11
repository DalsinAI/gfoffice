/*
 * GFOpenPresentation's core: the deck as a PDF, a page a slide, each slide
 * drawn from its GFOpenLayout display list into the page PDF writer
 * (owf_pdf.h). OpenLayout owns the shared shape and crop geometry.
 * MIT, Copyright (c) 2026 AmiCradle Team.
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

static long long isqrt(long long v)
{
    long long r = 0, bit = 1LL << 62;
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
    long long dx = (long long)x2 - x1, dy = (long long)y2 - y1, len = isqrt(dx * dx + dy * dy),
              size = width * 3 > 120 ? width * 3 : 120;
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

static int dashed(const ol_shape_style *s)
{
    return s->stroke_style != OL_STROKE_SOLID;
}

/* Draw the normalized OpenLayout shape into a PDF page. */
static int draw_shape(owf_pdf *pdf, const ol_device_op *dop)
{
    const ol_graphic *g = dop->op->graphic;
    const ol_shape_style *s;
    ol_rect bounds;
    ol_point path[OL_MAX_SHAPE_POINTS];
    int xy[OL_MAX_SHAPE_POINTS * 2], count, k, closed = 0, rc = 0;
    int x = dop->box.x, y = dop->box.y, w = dop->box.width, h = dop->box.height;
    unsigned long fill, line;
    if (!g || g->shape.type == OL_SHAPE_NONE)
        return 0;
    s = &g->shape;
    fill = shade_of(s->fill_argb);
    line = shade_of(s->stroke_argb);
    owf_pdf_dash(pdf, dashed(s));
    if (!s->rotation_tenths) {
        if (s->type == OL_SHAPE_RECT) {
            rc = owf_pdf_rect(pdf, x, y, w, h, fill, line, dop->thickness);
            goto done;
        }
        if (s->type == OL_SHAPE_ROUND_RECT) {
            rc = owf_pdf_round_rect(pdf, x, y, w, h,
                                    (w < h ? w : h) * s->radius_permille / OL_SHAPE_COORDS,
                                    fill, line, dop->thickness);
            goto done;
        }
        if (s->type == OL_SHAPE_ELLIPSE) {
            rc = owf_pdf_ellipse(pdf, x, y, w, h, fill, line, dop->thickness);
            goto done;
        }
    }
    bounds.x = x;
    bounds.y = y;
    bounds.width = w;
    bounds.height = h;
    count = ol_shape_path(g, &bounds, path, OL_MAX_SHAPE_POINTS, &closed);
    for (k = 0; k < count; k++) {
        xy[k * 2] = path[k].x;
        xy[k * 2 + 1] = path[k].y;
    }
    if (count > 0)
        rc = owf_pdf_path(pdf, xy, count, closed, closed ? fill : 0, line, dop->thickness);
    if (count >= 2 && OWF_SHADE_SET(line) && (s->flags & OL_SHAPE_ARROW_END))
        arrowhead(pdf, xy[(count - 2) * 2], xy[(count - 2) * 2 + 1],
                  xy[(count - 1) * 2], xy[(count - 1) * 2 + 1], dop->thickness, line);
    if (count >= 2 && OWF_SHADE_SET(line) && (s->flags & OL_SHAPE_ARROW_START))
        arrowhead(pdf, xy[2], xy[3], xy[0], xy[1], dop->thickness, line);
done:
    owf_pdf_dash(pdf, 0);
    return rc;
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
    int scaled;                  /* drawn smaller than its page: text at the scale's size */
    int failed;
} drawing;

static void draw_op(void *userdata, const ol_device_op *dop)
{
    drawing *d = (drawing *)userdata;
    const ol_display_op *op = dop->op;
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
        int baseline = dop->baseline, size = d->scaled ? dop->font_pixels : U2T(st->font_size);
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
        const ol_image_crop *crop = op->graphic ? &op->graphic->crop : NULL;
        if (image < 0 || image >= d->deck->res->nimages)
            break;
        if (crop && (crop->left || crop->top || crop->right || crop->bottom)) {
            /* the whole picture, larger, shown only inside its box */
            int clip[4], w, h;
            clip[0] = dop->box.x;
            clip[1] = dop->box.y;
            clip[2] = dop->box.width;
            clip[3] = dop->box.height;
            w = (int)((long long)clip[2] * OL_SHAPE_COORDS /
                      (OL_SHAPE_COORDS - crop->left - crop->right));
            h = (int)((long long)clip[3] * OL_SHAPE_COORDS /
                      (OL_SHAPE_COORDS - crop->top - crop->bottom));
            owf_pdf_image(d->pdf, &d->deck->res->images[image],
                          clip[0] - (int)((long long)w * crop->left / OL_SHAPE_COORDS),
                          clip[1] - (int)((long long)h * crop->top / OL_SHAPE_COORDS), w, h, clip);
        } else {
            owf_pdf_image(d->pdf, &d->deck->res->images[image], dop->box.x, dop->box.y, dop->box.width,
                          dop->box.height, NULL);
        }
        break;
    }
    case OL_OP_SHAPE:
        if (draw_shape(d->pdf, dop) < 0)
            d->failed = 1;
        break;
    default:
        break;
    }
}

/* One laid-out page drawn through scale; 0, or -1 when memory ran out. */
static int draw_laid(owf_pdf *pdf, const owf_deck *deck, const opr_slide *laid, const ol_scale *scale, int scaled)
{
    drawing d;
    memset(&d, 0, sizeof d);
    d.pdf = pdf;
    d.slide = laid;
    d.deck = deck;
    d.scaled = scaled;
    ol_draw_page(laid->doc, 0, scale, NULL, draw_op, &d);
    return d.failed ? -1 : 0;
}

/* The slide drawn small into box (twips), with a hairline round it. */
static int draw_slide_in(owf_pdf *pdf, const owf_deck *deck, int slide, const int box[4], const char *today,
                         int *overflow)
{
    ol_scale scale;
    opr_slide *laid = opr_slide_layout(deck, slide, NULL, NULL, today);
    int rc;
    if (!laid)
        return -1;
    memset(&scale, 0, sizeof scale);
    ol_scale_set(&scale, box[2], (int32_t)deck->width * OL_UNITS_PER_TWIP);
    scale.origin_x = box[0];
    scale.origin_y = box[1];
    rc = draw_laid(pdf, deck, laid, &scale, 1);
    *overflow |= laid->overflow;
    opr_slide_free(laid);
    if (rc == 0)
        rc = owf_pdf_rect(pdf, box[0], box[1], box[2], box[3], 0, OWF_SHADE(0x808080), 10) < 0 ? -1 : 0;
    return rc;
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
    if (options && options->pages == OPR_PDF_HANDOUTS) {
        int per = options->handout_slides, k = 0, w, h, box[4];
        if (per != 1 && per != 2 && per != 3 && per != 4 && per != 9)
            per = 6;
        opr_notes_page(deck, &w, &h);
        for (i = 0; i < deck->nslides; i++) {
            if (deck->slides[i].hidden && !options->with_hidden)
                continue;
            if (k == 0 && owf_pdf_page(pdf, w, h) < 0)
                goto memory;
            opr_handout_box(deck, per, k, box);
            if (draw_slide_in(pdf, deck, i, box, options->today, &overflow) < 0)
                goto memory;
            k = (k + 1) % per;
        }
    } else if (options && options->pages == OPR_PDF_NOTES) {
        int w, h, box[4];
        opr_notes_page(deck, &w, &h);
        opr_notes_boxes(deck, box, NULL);
        for (i = 0; i < deck->nslides; i++) {
            opr_slide *laid;
            if (deck->slides[i].hidden && !options->with_hidden)
                continue;
            if (owf_pdf_page(pdf, w, h) < 0)
                goto memory;
            /* the page's white and the notes, then the slide over the white */
            laid = opr_notes_layout(deck, i, NULL, NULL);
            if (!laid)
                goto memory;
            if (draw_laid(pdf, deck, laid, &scale, 0) < 0) {
                opr_slide_free(laid);
                goto memory;
            }
            overflow |= laid->overflow;
            opr_slide_free(laid);
            if (draw_slide_in(pdf, deck, i, box, options->today, &overflow) < 0)
                goto memory;
        }
    } else {
        for (i = 0; i < deck->nslides; i++) {
            const owf_slide *s = &deck->slides[i];
            opr_slide *laid;
            if (s->hidden && !(options && options->with_hidden))
                continue;
            laid = opr_slide_layout(deck, i, NULL, NULL, options ? options->today : NULL);
            if (!laid || owf_pdf_page(pdf, deck->width, deck->height) < 0) {
                opr_slide_free(laid);
                goto memory;
            }
            if (draw_laid(pdf, deck, laid, &scale, 0) < 0) {
                opr_slide_free(laid);
                goto memory;
            }
            overflow |= laid->overflow;
            opr_slide_free(laid);
        }
    }
    if (overflow)
        owf_report_add(report, OWF_NOTE_APPROX, "Text that runs below a slide's bottom edge is cut off there");
    result = owf_pdf_finish(pdf, deck->res->title, data, length, report);
    owf_pdf_free(pdf);
    return result;
memory:
    owf_pdf_free(pdf);
    return OWF_ERR_MEMORY;
}

/*
 * GFOpenPresentation's core: a deck saved as PowerPoint (PPTX, Office Open
 * XML, ECMA-376). Written from the model as GFOpenPresentation keeps it:
 * every slide with its objects placed where they are drawn (boxes resolved
 * from layouts and masters), text with its runs' look and its outline
 * levels, pictures, shapes, a background, hidden slides, a transition and
 * the speaker notes; one slide master a deck master, each with one layout
 * holding its title and body boxes, and the text levels as the master's
 * text styles. What PPTX cannot hold from the model is listed in the report.
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define EMU(twips) ((long)(twips) * 635L)

#define NS_A "http://schemas.openxmlformats.org/drawingml/2006/main"
#define NS_P "http://schemas.openxmlformats.org/presentationml/2006/main"
#define NS_R "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define REL "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
#define CT "application/vnd.openxmlformats-officedocument.presentationml."

typedef struct {
    const owf_deck *deck;
    owf_report *report;
    owf_zip *zip;
    int failed;
    int *image_used;             /* per res image: its media number, 0: not written yet */
    int nmedia;
    int lost_tables, lost_groups, lost_other, lost_transition;
} pptxw;

/* ---- Small pieces ---- */

static void add_part(pptxw *w, const char *name, owf_buf *b)
{
    if (b->failed || owf_zip_add(w->zip, name, b->data ? b->data : (const unsigned char *)"", b->length, 1) < 0)
        w->failed = 1;
    owf_buf_free(b);
}

static void xml_head(owf_buf *b)
{
    owf_buf_puts(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
}

static const char *pic_ext(const owf_image *im)
{
    const char *m = im->mime ? im->mime : "";
    if (!strcmp(m, "image/png")) return "png";
    if (!strcmp(m, "image/jpeg")) return "jpeg";
    if (!strcmp(m, "image/gif")) return "gif";
    if (!strcmp(m, "image/bmp")) return "bmp";
    if (!strcmp(m, "image/svg+xml")) return "svg";
    if (!strcmp(m, "image/x-emf")) return "emf";
    if (!strcmp(m, "image/x-wmf")) return "wmf";
    return NULL;                 /* not a picture PowerPoint takes (IFF, unknown) */
}

static void put_colour(owf_buf *b, unsigned long rgb)
{
    owf_buf_printf(b, "<a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", rgb & 0xFFFFFFUL);
}

static const char *font_name(const owf_deck *d, int font)
{
    if (!d->res || font < 0 || font >= d->res->nfonts || !d->res->fonts[font].name)
        return NULL;
    return d->res->fonts[font].name;
}

/* A run's (or a level's) character look; level: what it inherits, NULL none. */
static void put_rpr(owf_buf *b, const char *element, const owf_deck *d, const owf_charfmt *f, int full)
{
    const char *face = font_name(d, f->font);
    owf_buf_printf(b, "<a:%s lang=\"en-GB\"", element);
    if (f->size > 0) owf_buf_printf(b, " sz=\"%d\"", f->size * 5);
    if (full || (f->flags & OWF_BOLD)) owf_buf_printf(b, " b=\"%d\"", (f->flags & OWF_BOLD) ? 1 : 0);
    if (full || (f->flags & OWF_ITALIC)) owf_buf_printf(b, " i=\"%d\"", (f->flags & OWF_ITALIC) ? 1 : 0);
    if (f->flags & OWF_UNDERLINE) owf_buf_puts(b, " u=\"sng\"");
    if (f->flags & OWF_STRIKE) owf_buf_puts(b, " strike=\"sngStrike\"");
    if (f->flags & OWF_SUPER) owf_buf_puts(b, " baseline=\"30000\"");
    else if (f->flags & OWF_SUB) owf_buf_puts(b, " baseline=\"-25000\"");
    if (f->colour == OWF_COLOUR_AUTO && !face) {
        owf_buf_puts(b, "/>");
        return;
    }
    owf_buf_puts(b, ">");
    if (f->colour != OWF_COLOUR_AUTO)
        put_colour(b, f->colour);
    if (face) {
        owf_buf_puts(b, "<a:latin typeface=\"");
        owf_buf_puts_xml(b, face);
        owf_buf_puts(b, "\"/>");
    }
    owf_buf_printf(b, "</a:%s>", element);
}

static const char *align_name(owf_align a)
{
    switch (a) {
    case OWF_ALIGN_CENTRE: return "ctr";
    case OWF_ALIGN_RIGHT: return "r";
    case OWF_ALIGN_JUSTIFY: return "just";
    default: return "l";
    }
}

static void put_bullet(owf_buf *b, const owf_deck *d, const owf_textlevel *lv)
{
    if (!lv->bullet) {
        owf_buf_puts(b, "<a:buNone/>");
        return;
    }
    if (OWF_SHADE_SET(lv->bullet_colour)) {
        owf_buf_puts(b, "<a:buClr>");
        owf_buf_printf(b, "<a:srgbClr val=\"%06lX\"/>", OWF_SHADE_RGB(lv->bullet_colour));
        owf_buf_puts(b, "</a:buClr>");
    }
    if (lv->bullet_size > 0)
        owf_buf_printf(b, "<a:buSzPct val=\"%d\"/>", lv->bullet_size * 1000);
    if (font_name(d, lv->bullet_font)) {
        owf_buf_puts(b, "<a:buFont typeface=\"");
        owf_buf_puts_xml(b, font_name(d, lv->bullet_font));
        owf_buf_puts(b, "\"/>");
    }
    if (lv->bullet & OWF_BULLET_NUMBER) {
        int c = (int)(lv->bullet & 0xFF);
        const char *kind = c == 'a' ? "alphaLc" : c == 'A' ? "alphaUc" : c == 'i' ? "romanLc" : c == 'I' ? "romanUc"
                         : "arabic";
        const char *tail = !strcmp(lv->suffix, ")") ? (lv->prefix[0] == '(' ? "ParenBoth" : "ParenR")
                         : !strcmp(lv->suffix, ".") ? "Period" : "Plain";
        owf_buf_printf(b, "<a:buAutoNum type=\"%s%s\"", kind, tail);
        if (lv->start > 1) owf_buf_printf(b, " startAt=\"%d\"", lv->start);
        owf_buf_puts(b, "/>");
    } else {
        owf_buf_puts(b, "<a:buChar char=\"");
        owf_buf_put_utf8(b, lv->bullet);
        owf_buf_puts(b, "\"/>");
    }
}

/* A text level as a master's style level (a:lvlNpPr). */
static void put_level(owf_buf *b, const owf_deck *d, const owf_textlevel *lv, int n)
{
    owf_buf_printf(b, "<a:lvl%dpPr marL=\"%ld\" indent=\"%ld\" algn=\"%s\">", n, EMU(lv->para.indent_left),
                   EMU(lv->para.indent_first), align_name(lv->para.align));
    if (lv->para.space_before > 0)
        owf_buf_printf(b, "<a:spcBef><a:spcPts val=\"%d\"/></a:spcBef>", lv->para.space_before * 5);
    put_bullet(b, d, lv);
    put_rpr(b, "defRPr", d, &lv->chars, 0);
    owf_buf_printf(b, "</a:lvl%dpPr>", n);
}

/* ---- Text ---- */

static void put_text_run(owf_buf *b, const owf_deck *d, const owf_run *r)
{
    switch (r->kind) {
    case OWF_RUN_TEXT:
        if (!r->text || !*r->text)
            return;
        owf_buf_puts(b, "<a:r>");
        put_rpr(b, "rPr", d, &r->fmt, 0);
        owf_buf_puts(b, "<a:t>");
        owf_buf_puts_xml(b, r->text);
        owf_buf_puts(b, "</a:t></a:r>");
        return;
    case OWF_RUN_TAB:
        owf_buf_puts(b, "<a:r>");
        put_rpr(b, "rPr", d, &r->fmt, 0);
        owf_buf_puts(b, "<a:t>\t</a:t></a:r>");
        return;
    case OWF_RUN_LINEBREAK:
        owf_buf_puts(b, "<a:br>");
        put_rpr(b, "rPr", d, &r->fmt, 0);
        owf_buf_puts(b, "</a:br>");
        return;
    case OWF_RUN_FIELD:
        if (r->field == OWF_FIELD_PAGE) {
            owf_buf_puts(b, "<a:fld id=\"{B6F15528-21DE-4FAA-801E-634DDDAF4B2B}\" type=\"slidenum\">");
            put_rpr(b, "rPr", d, &r->fmt, 0);
            owf_buf_puts(b, "<a:t>#</a:t></a:fld>");
        } else if (r->field == OWF_FIELD_DATE || r->field == OWF_FIELD_TIME) {
            owf_buf_printf(b, "<a:fld id=\"{B6F15528-21DE-4FAA-801E-634DDDAF4B2C}\" type=\"%s\">",
                           r->field == OWF_FIELD_DATE ? "datetime1" : "datetime10");
            put_rpr(b, "rPr", d, &r->fmt, 0);
            owf_buf_puts(b, "<a:t>-</a:t></a:fld>");
        }
        return;
    default:
        return;
    }
}

static void put_body(owf_buf *b, const owf_deck *d, const owf_obj *o, const owf_story *story, int in_shape)
{
    int i;
    owf_buf_puts(b, in_shape ? "<p:txBody>" : "<a:txBody>");
    owf_buf_printf(b, "<a:bodyPr wrap=\"square\" lIns=\"%ld\" tIns=\"%ld\" rIns=\"%ld\" bIns=\"%ld\"", EMU(o->inset[0]),
                   EMU(o->inset[1]), EMU(o->inset[2]), EMU(o->inset[3]));
    if (o->text_columns >= 2)
        owf_buf_printf(b, " numCol=\"%d\" spcCol=\"%ld\"", o->text_columns, EMU(o->text_column_gap));
    owf_buf_printf(b, " anchor=\"%s\">", o->valign == OWF_VALIGN_CENTRE ? "ctr" : o->valign == OWF_VALIGN_BOTTOM ? "b"
                                                                                                        : "t");
    if (o->autofit == OWF_AUTOFIT_SHRINK) {
        if (o->font_scale >= 10 && o->font_scale < 100)
            owf_buf_printf(b, "<a:normAutofit fontScale=\"%d\"/>", o->font_scale * 1000);
        else
            owf_buf_puts(b, "<a:normAutofit/>");
    } else if (o->autofit == OWF_AUTOFIT_GROW) {
        owf_buf_puts(b, "<a:spAutoFit/>");
    }
    owf_buf_puts(b, "</a:bodyPr><a:lstStyle/>");
    if ((o->flags & OWF_OBJ_EMPTY) || !story->nparas) {
        owf_buf_puts(b, "<a:p><a:endParaRPr lang=\"en-GB\"/></a:p>");
    } else {
        for (i = 0; i < story->nparas; i++) {
            const owf_para *p = &story->paras[i];
            int level = owf_obj_level(o, i), j;
            owf_buf_puts(b, "<a:p><a:pPr");
            if (level > 1) owf_buf_printf(b, " lvl=\"%d\"", level - 1);
            if (p->fmt.align != OWF_ALIGN_LEFT) owf_buf_printf(b, " algn=\"%s\"", align_name(p->fmt.align));
            if (p->fmt.indent_left || p->fmt.indent_first)
                owf_buf_printf(b, " marL=\"%ld\" indent=\"%ld\"", EMU(p->fmt.indent_left), EMU(p->fmt.indent_first));
            owf_buf_puts(b, ">");
            if (p->fmt.line_spacing > 0 && p->fmt.line_spacing != 100 && p->fmt.line_spacing < 1000)
                owf_buf_printf(b, "<a:lnSpc><a:spcPct val=\"%d\"/></a:lnSpc>", p->fmt.line_spacing * 1000);
            if (p->fmt.space_before > 0)
                owf_buf_printf(b, "<a:spcBef><a:spcPts val=\"%d\"/></a:spcBef>", p->fmt.space_before * 5);
            if (p->fmt.space_after > 0)
                owf_buf_printf(b, "<a:spcAft><a:spcPts val=\"%d\"/></a:spcAft>", p->fmt.space_after * 5);
            if (!level && (o->placeholder == OWF_PH_BODY || o->placeholder == OWF_PH_OBJECT))
                owf_buf_puts(b, "<a:buNone/>");
            owf_buf_puts(b, "</a:pPr>");
            for (j = 0; j < p->nruns; j++)
                put_text_run(b, d, &p->runs[j]);
            owf_buf_puts(b, "<a:endParaRPr lang=\"en-GB\"/></a:p>");
        }
    }
    owf_buf_puts(b, in_shape ? "</p:txBody>" : "</a:txBody>");
}

/* ---- Objects ---- */

static const char *ph_type(owf_placeholder ph)
{
    switch (ph) {
    case OWF_PH_TITLE: return "title";
    case OWF_PH_SUBTITLE: return "subTitle";
    case OWF_PH_BODY: return "body";
    case OWF_PH_OBJECT: return "obj";
    case OWF_PH_PICTURE: return "pic";
    case OWF_PH_TABLE: return "tbl";
    case OWF_PH_CHART: return "chart";
    case OWF_PH_DATE: return "dt";
    case OWF_PH_FOOTER: return "ftr";
    case OWF_PH_NUMBER: return "sldNum";
    case OWF_PH_NOTES: return "body";
    case OWF_PH_SLIDE_IMAGE: return "sldImg";
    case OWF_PH_HEADER: return "hdr";
    case OWF_PH_TEXT: return "body";
    default: return NULL;
    }
}

static void put_xfrm(owf_buf *b, const char *ns, const owf_obj *box)
{
    owf_buf_printf(b, "<%s:xfrm", ns);
    if (box->rotation) {
        /* the model's tenths anticlockwise; DrawingML's 60000ths clockwise */
        long rot = (3600L - (box->rotation % 3600 + 3600) % 3600) % 3600 * 6000L;
        owf_buf_printf(b, " rot=\"%ld\"", rot);
    }
    if (box->flags & OWF_OBJ_FLIP_H) owf_buf_puts(b, " flipH=\"1\"");
    if (box->flags & OWF_OBJ_FLIP_V) owf_buf_puts(b, " flipV=\"1\"");
    owf_buf_printf(b, "><a:off x=\"%ld\" y=\"%ld\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></%s:xfrm>", EMU(box->x), EMU(box->y),
                   EMU(box->width > 0 ? box->width : 1), EMU(box->height > 0 ? box->height : 1), ns);
}

static void put_line(owf_buf *b, const owf_look *l)
{
    if (!OWF_SHADE_SET(l->line)) {
        owf_buf_puts(b, "<a:ln><a:noFill/></a:ln>");
        return;
    }
    owf_buf_printf(b, "<a:ln w=\"%ld\">", EMU(l->line_width > 0 ? l->line_width : 10));
    put_colour(b, OWF_SHADE_RGB(l->line));
    if (l->arrows & OWF_ARROW_START) owf_buf_puts(b, "<a:headEnd type=\"triangle\"/>");
    if (l->arrows & OWF_ARROW_END) owf_buf_puts(b, "<a:tailEnd type=\"triangle\"/>");
    owf_buf_puts(b, "</a:ln>");
}

static const char *preset(const owf_obj *o)
{
    switch (o->shape) {
    case OWF_SHAPE_ROUNDRECT: return "roundRect";
    case OWF_SHAPE_ELLIPSE: return "ellipse";
    case OWF_SHAPE_TRIANGLE: return "triangle";
    case OWF_SHAPE_DIAMOND: return "diamond";
    case OWF_SHAPE_LINE: return "line";
    case OWF_SHAPE_ARROW: return "rightArrow";
    default: return "rect";
    }
}

/* A picture's media number, written the first time it is used. */
static int media_of(pptxw *w, int image)
{
    const owf_image *im;
    const char *ext;
    char name[64];
    if (image < 0 || image >= w->deck->res->nimages)
        return 0;
    if (w->image_used[image])
        return w->image_used[image];
    im = &w->deck->res->images[image];
    ext = pic_ext(im);
    if (!ext || !im->data || !im->length) {
        owf_report_add(w->report, OWF_NOTE_LOST, "A picture PowerPoint can't show (%s) is left out",
                       im->mime ? im->mime : "unknown kind");
        w->image_used[image] = -1;
        return -1;
    }
    w->image_used[image] = ++w->nmedia;
    snprintf(name, sizeof name, "ppt/media/image%d.%s", w->nmedia, ext);
    if (owf_zip_add(w->zip, name, im->data, im->length, 0) < 0)
        w->failed = 1;
    return w->nmedia;
}

/* The pictures a part uses, for its relationships: rId (first_rid + k) is media[k]. */
typedef struct {
    int nmedia, cap;
    int *media;
} rels;

static int rel_of(rels *r, int media, int first_rid)
{
    int i;
    for (i = 0; i < r->nmedia; i++)
        if (r->media[i] == media)
            return first_rid + i;
    if (r->nmedia == r->cap) {
        int want = r->cap ? r->cap * 2 : 8, *m = realloc(r->media, (size_t)want * sizeof *m);
        if (!m)
            return -1;
        r->media = m;
        r->cap = want;
    }
    r->media[r->nmedia++] = media;
    return first_rid + r->nmedia - 1;
}

static void put_rels_media(pptxw *w, owf_buf *b, const rels *r, int first_rid)
{
    int i, k;
    for (i = 0; i < r->nmedia; i++) {
        const char *ext = "png";
        for (k = 0; k < w->deck->res->nimages; k++)
            if (w->image_used[k] == r->media[i]) {
                ext = pic_ext(&w->deck->res->images[k]);
                break;
            }
        owf_buf_printf(b, "<Relationship Id=\"rId%d\" Type=\"" REL "image\" Target=\"../media/image%d.%s\"/>",
                       first_rid + i, r->media[i], ext);
    }
}

static void put_nv(owf_buf *b, const char *kind, int id, const owf_obj *o, const char *def_name)
{
    owf_buf_printf(b, "<p:nv%sPr><p:cNvPr id=\"%d\" name=\"", kind, id);
    owf_buf_puts_xml(b, o && o->name ? o->name : def_name);
    owf_buf_puts(b, "\"");
    if (o && o->alt) {
        owf_buf_puts(b, " descr=\"");
        owf_buf_puts_xml(b, o->alt);
        owf_buf_puts(b, "\"");
    }
    owf_buf_puts(b, "/>");
}

static void put_object(pptxw *w, owf_buf *b, const owf_obj *o, const owf_obj *box, int id, rels *r, int first_rid,
                       int on_master)
{
    const owf_deck *d = w->deck;
    const char *ph = on_master || o->placeholder != OWF_PH_NONE ? ph_type(o->placeholder) : NULL;
    char name[48];
    if (o->kind == OWF_OBJ_PICTURE || ((o->kind == OWF_OBJ_OTHER || o->kind == OWF_OBJ_CHART) && o->image >= 0)) {
        int media = media_of(w, o->image), rid;
        if (media <= 0 || (rid = rel_of(r, media, first_rid)) < 0)
            return;
        snprintf(name, sizeof name, "Picture %d", id);
        owf_buf_puts(b, "<p:pic>");
        put_nv(b, "Pic", id, o, name);
        owf_buf_puts(b, "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>");
        owf_buf_printf(b, "<p:blipFill><a:blip r:embed=\"rId%d\"/>", rid);
        if (o->crop[0] || o->crop[1] || o->crop[2] || o->crop[3])
            owf_buf_printf(b, "<a:srcRect l=\"%d\" t=\"%d\" r=\"%d\" b=\"%d\"/>", o->crop[0] * 100, o->crop[1] * 100,
                           o->crop[2] * 100, o->crop[3] * 100);
        owf_buf_puts(b, "<a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>");
        put_xfrm(b, "a", box);
        owf_buf_puts(b, "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>");
        if (OWF_SHADE_SET(o->look.line)) put_line(b, &o->look);
        owf_buf_puts(b, "</p:spPr></p:pic>");
        return;
    }
    if (o->kind == OWF_OBJ_TABLE) w->lost_tables++;
    if (o->kind == OWF_OBJ_OTHER || o->kind == OWF_OBJ_CHART) {
        if (!o->text.nparas) {
            w->lost_other++;
            return;
        }
    }
    snprintf(name, sizeof name, "%s %d", o->kind == OWF_OBJ_SHAPE ? "Shape" : "TextBox", id);
    owf_buf_puts(b, "<p:sp>");
    put_nv(b, "Sp", id, o, name);
    owf_buf_printf(b, "<p:cNvSpPr%s/><p:nvPr>", o->kind == OWF_OBJ_TEXT && !ph ? " txBox=\"1\"" : "");
    if (ph) {
        owf_buf_printf(b, "<p:ph type=\"%s\"", ph);
        if (o->ph_index > 0) owf_buf_printf(b, " idx=\"%d\"", o->ph_index);
        owf_buf_puts(b, "/>");
    }
    owf_buf_puts(b, "</p:nvPr></p:nvSpPr><p:spPr>");
    put_xfrm(b, "a", box);
    if (o->kind == OWF_OBJ_SHAPE) {
        owf_buf_printf(b, "<a:prstGeom prst=\"%s\"><a:avLst/></a:prstGeom>", preset(o));
        if (OWF_SHADE_SET(o->look.fill) && o->shape != OWF_SHAPE_LINE) put_colour(b, OWF_SHADE_RGB(o->look.fill));
        else owf_buf_puts(b, "<a:noFill/>");
        put_line(b, &o->look);
    } else {
        owf_buf_puts(b, "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>");
        if (OWF_SHADE_SET(o->look.fill)) put_colour(b, OWF_SHADE_RGB(o->look.fill));
        else owf_buf_puts(b, "<a:noFill/>");
        if (OWF_SHADE_SET(o->look.line)) put_line(b, &o->look);
    }
    owf_buf_puts(b, "</p:spPr>");
    if (o->shape != OWF_SHAPE_LINE || o->kind != OWF_OBJ_SHAPE)
        put_body(b, d, o, &o->text, 1);
    owf_buf_puts(b, "</p:sp>");
}

static void put_background(pptxw *w, owf_buf *b, unsigned long colour, int image, rels *r, int first_rid)
{
    int media, rid;
    if (image >= 0 && (media = media_of(w, image)) > 0 && (rid = rel_of(r, media, first_rid)) >= 0) {
        owf_buf_printf(b, "<p:bg><p:bgPr><a:blipFill><a:blip r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch>"
                          "</a:blipFill><a:effectLst/></p:bgPr></p:bg>", rid);
        return;
    }
    if (OWF_SHADE_SET(colour)) {
        owf_buf_puts(b, "<p:bg><p:bgPr>");
        put_colour(b, OWF_SHADE_RGB(colour));
        owf_buf_puts(b, "<a:effectLst/></p:bgPr></p:bg>");
    }
}

static void put_tree_start(owf_buf *b)
{
    owf_buf_puts(b, "<p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                    "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/>"
                    "<a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>");
}

static void put_transition(pptxw *w, owf_buf *b, const owf_transition *t)
{
    const char *dir = t->direction == OWF_DIR_RIGHT ? "r" : t->direction == OWF_DIR_UP ? "u"
                    : t->direction == OWF_DIR_DOWN ? "d" : "l";
    const char *spd = t->duration > 0 && t->duration < 600 ? "fast" : t->duration >= 1200 ? "slow" : "med";
    if (t->kind == OWF_TRANS_NONE && t->advance_after < 0)
        return;
    owf_buf_printf(b, "<p:transition spd=\"%s\"", spd);
    if (t->advance_after >= 0) owf_buf_printf(b, " advTm=\"%d\"", t->advance_after);
    owf_buf_puts(b, ">");
    switch (t->kind) {
    case OWF_TRANS_FADE: owf_buf_puts(b, "<p:fade/>"); break;
    case OWF_TRANS_FADE_BLACK: owf_buf_puts(b, "<p:fade thruBlk=\"1\"/>"); break;
    case OWF_TRANS_WIPE: owf_buf_printf(b, "<p:wipe dir=\"%s\"/>", dir); break;
    case OWF_TRANS_PUSH: owf_buf_printf(b, "<p:push dir=\"%s\"/>", dir); break;
    case OWF_TRANS_COVER: owf_buf_printf(b, "<p:cover dir=\"%s\"/>", dir); break;
    case OWF_TRANS_UNCOVER: owf_buf_printf(b, "<p:pull dir=\"%s\"/>", dir); break;
    case OWF_TRANS_SPLIT: owf_buf_puts(b, "<p:split/>"); break;
    case OWF_TRANS_BLINDS: owf_buf_puts(b, "<p:blinds/>"); break;
    case OWF_TRANS_DISSOLVE: owf_buf_puts(b, "<p:dissolve/>"); break;
    case OWF_TRANS_OTHER: w->lost_transition++; owf_buf_puts(b, "<p:fade/>"); break;
    default: break;
    }
    owf_buf_puts(b, "</p:transition>");
}

/* ---- Parts ---- */

static void put_slide(pptxw *w, int index)
{
    const owf_deck *d = w->deck;
    const owf_slide *s = &d->slides[index];
    owf_buf b, rb;
    rels r;
    char name[64];
    int i, id = 2, master = s->master >= 0 && s->master < d->nmasters ? s->master : 0;

    memset(&r, 0, sizeof r);
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<p:sld xmlns:a=\"" NS_A "\" xmlns:r=\"" NS_R "\" xmlns:p=\"" NS_P "\"");
    if (s->hidden) owf_buf_puts(&b, " show=\"0\"");
    owf_buf_puts(&b, "><p:cSld");
    if (s->name) {
        owf_buf_puts(&b, " name=\"");
        owf_buf_puts_xml(&b, s->name);
        owf_buf_puts(&b, "\"");
    }
    owf_buf_puts(&b, ">");
    put_background(w, &b, s->background, s->background_image, &r, 3);
    put_tree_start(&b);
    for (i = 0; i < s->nobjs; i++) {
        const owf_obj *o = &s->objs[i];
        owf_obj box;
        if (o->kind == OWF_OBJ_GROUP) {
            w->lost_groups++;
            continue;
        }
        if (o->flags & OWF_OBJ_HIDDEN)
            continue;
        box = *o;
        owf_deck_resolve(d, index, i, &box, NULL);
        put_object(w, &b, o, &box, id++, &r, 3, 0);
    }
    owf_buf_puts(&b, "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>");
    put_transition(w, &b, &s->transition);
    owf_buf_puts(&b, "</p:sld>");
    snprintf(name, sizeof name, "ppt/slides/slide%d.xml", index + 1);
    add_part(w, name, &b);

    owf_buf_init(&rb);
    xml_head(&rb);
    owf_buf_puts(&rb, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    owf_buf_printf(&rb, "<Relationship Id=\"rId1\" Type=\"" REL "slideLayout\" Target=\"../slideLayouts/slideLayout%d.xml\"/>",
                   master + 1);
    owf_buf_printf(&rb, "<Relationship Id=\"rId2\" Type=\"" REL "notesSlide\" Target=\"../notesSlides/notesSlide%d.xml\"/>",
                   index + 1);
    put_rels_media(w, &rb, &r, 3);
    owf_buf_puts(&rb, "</Relationships>");
    snprintf(name, sizeof name, "ppt/slides/_rels/slide%d.xml.rels", index + 1);
    add_part(w, name, &rb);
    free(r.media);
}

static void put_notes_slide(pptxw *w, int index)
{
    const owf_deck *d = w->deck;
    const owf_slide *s = &d->slides[index];
    owf_buf b;
    char name[64];
    int sb[4], tb[4];
    owf_obj box = s->notes;

    opr_notes_boxes(d, sb, tb);
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<p:notes xmlns:a=\"" NS_A "\" xmlns:r=\"" NS_R "\" xmlns:p=\"" NS_P "\"><p:cSld>");
    put_tree_start(&b);
    owf_buf_puts(&b, "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Slide Image\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" "
                     "noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"sldImg\"/></p:nvPr></p:nvSpPr>"
                     "<p:spPr>");
    box.x = sb[0]; box.y = sb[1]; box.width = sb[2]; box.height = sb[3]; box.rotation = 0; box.flags = 0;
    put_xfrm(&b, "a", &box);
    owf_buf_puts(&b, "</p:spPr></p:sp>");
    owf_buf_puts(&b, "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notes\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
                     "<p:nvPr><p:ph type=\"body\" idx=\"1\"/></p:nvPr></p:nvSpPr><p:spPr>");
    box = s->notes;
    box.x = tb[0]; box.y = tb[1]; box.width = tb[2]; box.height = tb[3]; box.rotation = 0; box.flags = 0;
    put_xfrm(&b, "a", &box);
    owf_buf_puts(&b, "</p:spPr>");
    put_body(&b, d, &s->notes, &s->notes.text, 1);
    owf_buf_puts(&b, "</p:sp></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:notes>");
    snprintf(name, sizeof name, "ppt/notesSlides/notesSlide%d.xml", index + 1);
    add_part(w, name, &b);

    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    owf_buf_puts(&b, "<Relationship Id=\"rId1\" Type=\"" REL "notesMaster\" Target=\"../notesMasters/notesMaster1.xml\"/>");
    owf_buf_printf(&b, "<Relationship Id=\"rId2\" Type=\"" REL "slide\" Target=\"../slides/slide%d.xml\"/>", index + 1);
    owf_buf_puts(&b, "</Relationships>");
    snprintf(name, sizeof name, "ppt/notesSlides/_rels/notesSlide%d.xml.rels", index + 1);
    add_part(w, name, &b);
}

static const char *const CLR_MAP = "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" "
    "accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" "
    "hlink=\"hlink\" folHlink=\"folHlink\"/>";

static void put_master(pptxw *w, int index)
{
    const owf_deck *d = w->deck;
    const owf_master *m = &d->masters[index];
    owf_buf b;
    rels r;
    char name[64];
    int i, id = 2;

    memset(&r, 0, sizeof r);
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<p:sldMaster xmlns:a=\"" NS_A "\" xmlns:r=\"" NS_R "\" xmlns:p=\"" NS_P "\"><p:cSld");
    if (m->name) {
        owf_buf_puts(&b, " name=\"");
        owf_buf_puts_xml(&b, m->name);
        owf_buf_puts(&b, "\"");
    }
    owf_buf_puts(&b, ">");
    if (OWF_SHADE_SET(m->background) || m->background_image >= 0)
        put_background(w, &b, m->background, m->background_image, &r, 3);
    else
        owf_buf_puts(&b, "<p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg>");
    put_tree_start(&b);
    for (i = 0; i < m->nobjs; i++) {
        const owf_obj *o = &m->objs[i];
        if (o->kind == OWF_OBJ_GROUP || (o->flags & OWF_OBJ_HIDDEN))
            continue;
        put_object(w, &b, o, o, id++, &r, 3, 0);
    }
    owf_buf_puts(&b, "</p:spTree></p:cSld>");
    owf_buf_puts(&b, CLR_MAP);
    owf_buf_printf(&b, "<p:sldLayoutIdLst><p:sldLayoutId id=\"%lu\" r:id=\"rId1\"/></p:sldLayoutIdLst>",
                   2147483649UL + (unsigned long)index * 2UL);
    owf_buf_puts(&b, "<p:txStyles><p:titleStyle>");
    put_level(&b, d, &m->title, 1);
    owf_buf_puts(&b, "</p:titleStyle><p:bodyStyle>");
    for (i = 0; i < OWF_TEXT_LEVELS; i++)
        put_level(&b, d, &m->body[i], i + 1);
    owf_buf_puts(&b, "</p:bodyStyle><p:otherStyle>");
    put_level(&b, d, &m->other, 1);
    owf_buf_puts(&b, "</p:otherStyle></p:txStyles></p:sldMaster>");
    snprintf(name, sizeof name, "ppt/slideMasters/slideMaster%d.xml", index + 1);
    add_part(w, name, &b);

    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    owf_buf_printf(&b, "<Relationship Id=\"rId1\" Type=\"" REL "slideLayout\" Target=\"../slideLayouts/slideLayout%d.xml\"/>",
                   index + 1);
    owf_buf_puts(&b, "<Relationship Id=\"rId2\" Type=\"" REL "theme\" Target=\"../theme/theme1.xml\"/>");
    put_rels_media(w, &b, &r, 3);
    owf_buf_puts(&b, "</Relationships>");
    snprintf(name, sizeof name, "ppt/slideMasters/_rels/slideMaster%d.xml.rels", index + 1);
    add_part(w, name, &b);
    free(r.media);

    /* its one layout: the master's title and body places, nothing of its own */
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<p:sldLayout xmlns:a=\"" NS_A "\" xmlns:r=\"" NS_R "\" xmlns:p=\"" NS_P "\" type=\"obj\" "
                     "preserve=\"1\"><p:cSld name=\"Title and Content\">");
    put_tree_start(&b);
    owf_buf_puts(&b, "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>");
    snprintf(name, sizeof name, "ppt/slideLayouts/slideLayout%d.xml", index + 1);
    add_part(w, name, &b);
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    owf_buf_printf(&b, "<Relationship Id=\"rId1\" Type=\"" REL "slideMaster\" Target=\"../slideMasters/slideMaster%d.xml\"/>",
                   index + 1);
    owf_buf_puts(&b, "</Relationships>");
    snprintf(name, sizeof name, "ppt/slideLayouts/_rels/slideLayout%d.xml.rels", index + 1);
    add_part(w, name, &b);
}

static void put_theme(pptxw *w, const char *part)
{
    owf_buf b;
    const char *face = w->deck->res && w->deck->res->base.font >= 0 ? font_name(w->deck, w->deck->res->base.font) : NULL;
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<a:theme xmlns:a=\"" NS_A "\" name=\"GFOffice\"><a:themeElements>"
        "<a:clrScheme name=\"GFOffice\"><a:dk1><a:srgbClr val=\"000000\"/></a:dk1><a:lt1><a:srgbClr val=\"FFFFFF\"/></a:lt1>"
        "<a:dk2><a:srgbClr val=\"1F2A44\"/></a:dk2><a:lt2><a:srgbClr val=\"E7E6E6\"/></a:lt2>"
        "<a:accent1><a:srgbClr val=\"4472C4\"/></a:accent1><a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>"
        "<a:accent3><a:srgbClr val=\"A5A5A5\"/></a:accent3><a:accent4><a:srgbClr val=\"FFC000\"/></a:accent4>"
        "<a:accent5><a:srgbClr val=\"5B9BD5\"/></a:accent5><a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>"
        "<a:hlink><a:srgbClr val=\"0563C1\"/></a:hlink><a:folHlink><a:srgbClr val=\"954F72\"/></a:folHlink></a:clrScheme>"
        "<a:fontScheme name=\"GFOffice\"><a:majorFont><a:latin typeface=\"");
    owf_buf_puts_xml(&b, face ? face : "Liberation Sans");
    owf_buf_puts(&b, "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont><a:minorFont><a:latin typeface=\"");
    owf_buf_puts_xml(&b, face ? face : "Liberation Sans");
    owf_buf_puts(&b, "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont></a:fontScheme>"
        "<a:fmtScheme name=\"GFOffice\"><a:fillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "</a:fillStyleLst><a:lnStyleLst><a:ln w=\"6350\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln>"
        "<a:ln w=\"12700\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln>"
        "<a:ln w=\"19050\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln></a:lnStyleLst>"
        "<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle>"
        "<a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst><a:bgFillStyleLst>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>"
        "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:bgFillStyleLst></a:fmtScheme>"
        "</a:themeElements></a:theme>");
    add_part(w, part, &b);
}

static void put_notes_master(pptxw *w)
{
    owf_buf b;
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<p:notesMaster xmlns:a=\"" NS_A "\" xmlns:r=\"" NS_R "\" xmlns:p=\"" NS_P "\"><p:cSld>"
                     "<p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg>");
    put_tree_start(&b);
    owf_buf_puts(&b, "</p:spTree></p:cSld>");
    owf_buf_puts(&b, CLR_MAP);
    owf_buf_puts(&b, "</p:notesMaster>");
    add_part(w, "ppt/notesMasters/notesMaster1.xml", &b);
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                     "<Relationship Id=\"rId1\" Type=\"" REL "theme\" Target=\"../theme/theme2.xml\"/></Relationships>");
    add_part(w, "ppt/notesMasters/_rels/notesMaster1.xml.rels", &b);
    put_theme(w, "ppt/theme/theme2.xml");
}

static void put_presentation(pptxw *w)
{
    const owf_deck *d = w->deck;
    owf_buf b;
    int i, nw, nh, rid = 1;
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<p:presentation xmlns:a=\"" NS_A "\" xmlns:r=\"" NS_R "\" xmlns:p=\"" NS_P "\" "
                     "saveSubsetFonts=\"1\"><p:sldMasterIdLst>");
    for (i = 0; i < d->nmasters; i++)
        owf_buf_printf(&b, "<p:sldMasterId id=\"%lu\" r:id=\"rId%d\"/>", 2147483648UL + (unsigned long)i * 2UL, rid++);
    owf_buf_printf(&b, "</p:sldMasterIdLst><p:notesMasterIdLst><p:notesMasterId r:id=\"rId%d\"/></p:notesMasterIdLst>",
                   rid++);
    owf_buf_puts(&b, "<p:sldIdLst>");
    for (i = 0; i < d->nslides; i++)
        owf_buf_printf(&b, "<p:sldId id=\"%d\" r:id=\"rId%d\"/>", 256 + i, rid++);
    opr_notes_page(d, &nw, &nh);
    owf_buf_printf(&b, "</p:sldIdLst><p:sldSz cx=\"%ld\" cy=\"%ld\"/><p:notesSz cx=\"%ld\" cy=\"%ld\"/>", EMU(d->width),
                   EMU(d->height), EMU(nw), EMU(nh));
    owf_buf_puts(&b, "<p:defaultTextStyle>");
    if (d->nmasters)
        for (i = 0; i < OWF_TEXT_LEVELS; i++)
            put_level(&b, d, &d->masters[0].other, i + 1);
    owf_buf_puts(&b, "</p:defaultTextStyle></p:presentation>");
    add_part(w, "ppt/presentation.xml", &b);

    rid = 1;
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (i = 0; i < d->nmasters; i++)
        owf_buf_printf(&b, "<Relationship Id=\"rId%d\" Type=\"" REL "slideMaster\" Target=\"slideMasters/slideMaster%d.xml\"/>",
                       rid++, i + 1);
    owf_buf_printf(&b, "<Relationship Id=\"rId%d\" Type=\"" REL "notesMaster\" Target=\"notesMasters/notesMaster1.xml\"/>",
                   rid++);
    for (i = 0; i < d->nslides; i++)
        owf_buf_printf(&b, "<Relationship Id=\"rId%d\" Type=\"" REL "slide\" Target=\"slides/slide%d.xml\"/>", rid++, i + 1);
    owf_buf_printf(&b, "<Relationship Id=\"rId%d\" Type=\"" REL "theme\" Target=\"theme/theme1.xml\"/>", rid++);
    owf_buf_puts(&b, "</Relationships>");
    add_part(w, "ppt/_rels/presentation.xml.rels", &b);
}

static void put_package(pptxw *w)
{
    const owf_deck *d = w->deck;
    owf_buf b;
    int i;
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/><Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"gif\" ContentType=\"image/gif\"/><Default Extension=\"bmp\" ContentType=\"image/bmp\"/>"
        "<Default Extension=\"svg\" ContentType=\"image/svg+xml\"/><Default Extension=\"emf\" ContentType=\"image/x-emf\"/>"
        "<Default Extension=\"wmf\" ContentType=\"image/x-wmf\"/>"
        "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"" CT "presentation.main+xml\"/>"
        "<Override PartName=\"/ppt/notesMasters/notesMaster1.xml\" ContentType=\"" CT "notesMaster+xml\"/>"
        "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
        "<Override PartName=\"/ppt/theme/theme2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>");
    for (i = 0; i < d->nmasters; i++) {
        owf_buf_printf(&b, "<Override PartName=\"/ppt/slideMasters/slideMaster%d.xml\" ContentType=\"" CT "slideMaster+xml\"/>",
                       i + 1);
        owf_buf_printf(&b, "<Override PartName=\"/ppt/slideLayouts/slideLayout%d.xml\" ContentType=\"" CT "slideLayout+xml\"/>",
                       i + 1);
    }
    for (i = 0; i < d->nslides; i++) {
        owf_buf_printf(&b, "<Override PartName=\"/ppt/slides/slide%d.xml\" ContentType=\"" CT "slide+xml\"/>", i + 1);
        owf_buf_printf(&b, "<Override PartName=\"/ppt/notesSlides/notesSlide%d.xml\" ContentType=\"" CT "notesSlide+xml\"/>",
                       i + 1);
    }
    owf_buf_puts(&b, "</Types>");
    add_part(w, "[Content_Types].xml", &b);

    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"" REL "officeDocument\" Target=\"ppt/presentation.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
        "Target=\"docProps/core.xml\"/>"
        "<Relationship Id=\"rId3\" Type=\"" REL "extended-properties\" Target=\"docProps/app.xml\"/></Relationships>");
    add_part(w, "_rels/.rels", &b);

    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_puts(&b, "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                     "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>");
    if (d->res && d->res->title) owf_buf_puts_xml(&b, d->res->title);
    owf_buf_puts(&b, "</dc:title></cp:coreProperties>");
    add_part(w, "docProps/core.xml", &b);
    owf_buf_init(&b);
    xml_head(&b);
    owf_buf_printf(&b, "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                       "<Application>GFOpenPresentation</Application><Slides>%d</Slides></Properties>", d->nslides);
    add_part(w, "docProps/app.xml", &b);
}

int opr_export_pptx(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report)
{
    pptxw w;
    owf_buf out;
    int i;
    if (!data || !length)
        return OWF_ERR_FORMAT;
    *data = NULL;
    *length = 0;
    if (!deck || !deck->nmasters || !deck->res)
        return OWF_ERR_FORMAT;
    memset(&w, 0, sizeof w);
    w.deck = deck;
    w.report = report;
    w.image_used = calloc((size_t)(deck->res->nimages > 0 ? deck->res->nimages : 1), sizeof *w.image_used);
    owf_buf_init(&out);
    w.zip = w.image_used ? owf_zip_new(&out) : NULL;
    if (!w.zip) {
        free(w.image_used);
        return OWF_ERR_MEMORY;
    }
    put_package(&w);
    put_presentation(&w);
    put_theme(&w, "ppt/theme/theme1.xml");
    for (i = 0; i < deck->nmasters; i++)
        put_master(&w, i);
    put_notes_master(&w);
    for (i = 0; i < deck->nslides; i++) {
        put_slide(&w, i);
        put_notes_slide(&w, i);
    }
    if (owf_zip_finish(w.zip) < 0)
        w.failed = 1;
    free(w.image_used);
    if (w.failed || out.failed) {
        owf_buf_free(&out);
        return OWF_ERR_MEMORY;
    }
    if (w.lost_tables)
        owf_report_add(report, OWF_NOTE_APPROX, "Tables are saved as text boxes in PPTX for now (%d)", w.lost_tables);
    if (w.lost_groups)
        owf_report_add(report, OWF_NOTE_APPROX, "Groups are saved as their objects, ungrouped (%d)", w.lost_groups);
    if (w.lost_other)
        owf_report_add(report, OWF_NOTE_LOST, "Objects PowerPoint can't take are left out (%d)", w.lost_other);
    if (w.lost_transition)
        owf_report_add(report, OWF_NOTE_APPROX, "Transitions PPTX has no name for are saved as fades (%d)",
                       w.lost_transition);
    if (deck->nkept && deck->kept_format != OWF_KEPT_PPTX)
        owf_report_add(report, OWF_NOTE_LOST, "Animations and other parts kept from the ODP file are not saved in PPTX");
    return owf_buf_take(&out, data, length) < 0 ? OWF_ERR_MEMORY : OWF_OK;
}

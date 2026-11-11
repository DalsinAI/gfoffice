/*
 * libowf: DOCX out (Office Open XML, ECMA-376 transitional): the content
 * types, the relationships, word/document.xml, word/styles.xml, a header
 * and a footer part, and the document's properties. Formatting is written
 * on each paragraph and run, as Word itself does for direct formatting.
 * Elements inside w:pPr and w:rPr follow the schema's order, which Word
 * insists on. Tracked changes (1.5) are w:ins and w:del round each run and
 * in w:pPr's w:rPr for paragraph marks; settings' w:trackRevisions records.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"
#include "owf_comment.h"

#include <stdio.h>
#include <string.h>

#define W_NS "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" " \
             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" " \
             "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" " \
             "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" " \
             "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\" " \
             "xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" " \
             "xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\""

static const char *face_name(const owf_doc *doc, int font, owf_report *report)
{
    const char *modern = owf_font_modern(doc->fonts[font].name, NULL);
    if (modern) {
        owf_report_add(report, OWF_NOTE_APPROX, "The font %s is saved as %s", doc->fonts[font].name, modern);
        return modern;
    }
    return doc->fonts[font].name;
}

static void put_fonts(owf_buf *out, const char *name)
{
    owf_buf_puts(out, "<w:rFonts w:ascii=\"");
    owf_buf_puts_xml(out, name);
    owf_buf_puts(out, "\" w:hAnsi=\"");
    owf_buf_puts_xml(out, name);
    owf_buf_puts(out, "\" w:cs=\"");
    owf_buf_puts_xml(out, name);
    owf_buf_puts(out, "\"/>");
}

static void put_rpr(owf_buf *out, const owf_doc *doc, const owf_charfmt *f, owf_report *report)
{
    if (!f->flags && f->font < 0 && !f->size && f->colour == OWF_COLOUR_AUTO)
        return;
    owf_buf_puts(out, "<w:rPr>");
    if (f->font >= 0 && f->font < doc->nfonts)
        put_fonts(out, face_name(doc, f->font, report));
    if (f->flags & OWF_BOLD)
        owf_buf_puts(out, "<w:b/>");
    if (f->flags & OWF_ITALIC)
        owf_buf_puts(out, "<w:i/>");
    if (f->flags & OWF_STRIKE)
        owf_buf_puts(out, "<w:strike/>");
    if (f->colour != OWF_COLOUR_AUTO)
        owf_buf_printf(out, "<w:color w:val=\"%06lX\"/>", f->colour & 0xFFFFFFUL);
    if (f->size)
        owf_buf_printf(out, "<w:sz w:val=\"%d\"/>", (f->size + 5) / 10);   /* half-points */
    if (f->flags & OWF_UNDERLINE)
        owf_buf_puts(out, "<w:u w:val=\"single\"/>");
    if (f->flags & OWF_SUPER)
        owf_buf_puts(out, "<w:vertAlign w:val=\"superscript\"/>");
    else if (f->flags & OWF_SUB)
        owf_buf_puts(out, "<w:vertAlign w:val=\"subscript\"/>");
    owf_buf_puts(out, "</w:rPr>");
}

static void put_section_columns(owf_buf *out, const owf_doc *doc,
                                const owf_para *section, int break_kind)
{
    static const char *type[] = { "continuous", "nextPage", "oddPage", "evenPage" };
    int columns = section && section->section_columns > 0 ? section->section_columns : 1;
    int gap = section && section->section_gap > 0 ? section->section_gap : 720;
    const owf_page *pg = &doc->page;
    if (break_kind >= OWF_SECTION_CONTINUOUS && break_kind <= OWF_SECTION_EVEN_PAGE)
        owf_buf_printf(out, "<w:type w:val=\"%s\"/>", type[break_kind]);
    owf_buf_printf(out, "<w:pgSz w:w=\"%d\" w:h=\"%d\"/>", pg->width, pg->height);
    owf_buf_printf(out, "<w:pgMar w:top=\"%d\" w:right=\"%d\" w:bottom=\"%d\" w:left=\"%d\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/>",
                   pg->margin_top, pg->margin_right, pg->margin_bottom, pg->margin_left);
    owf_buf_printf(out, "<w:cols w:num=\"%d\" w:space=\"%d\"/>", columns, gap);
}

static void put_change_attrs(owf_buf *out, const owf_revision *r, long id);

static void put_ppr(owf_buf *out, const owf_doc *doc, const owf_parafmt *f, const char *style,
                    int section_end, const owf_para *section, int next_break,
                    const owf_revision *mark, long mark_id)
{
    static const char *tab_val[] = { "left", "center", "right", "decimal" };
    static const char *jc[] = { "left", "center", "right", "both" };
    int i, spacing = f->space_before || f->space_after || (f->line_spacing && f->line_spacing != 100);
    int ind = f->indent_left || f->indent_right || f->indent_first;

    if (!style && !section_end && !f->page_break_before && !f->ntabs && !spacing && !ind && f->align == OWF_ALIGN_LEFT &&
        !f->borders && !OWF_SHADE_SET(f->shading) && !mark)
        return;
    owf_buf_puts(out, "<w:pPr>");
    if (style)
        owf_buf_printf(out, "<w:pStyle w:val=\"%s\"/>", style);
    if (f->page_break_before)
        owf_buf_puts(out, "<w:pageBreakBefore/>");
    if (f->borders) {
        static const char *side[] = { "top", "left", "bottom", "right" };
        char col[8];
        int k;
        if (OWF_SHADE_SET(f->border_colour)) snprintf(col, sizeof col, "%06lX", OWF_SHADE_RGB(f->border_colour));
        else strcpy(col, "auto");
        owf_buf_puts(out, "<w:pBdr>");
        for (k = 0; k < 4; k++)
            if (f->borders & (1 << k))
                owf_buf_printf(out, "<w:%s w:val=\"single\" w:sz=\"4\" w:space=\"4\" w:color=\"%s\"/>", side[k], col);
        owf_buf_puts(out, "</w:pBdr>");
    }
    if (OWF_SHADE_SET(f->shading))
        owf_buf_printf(out, "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>", OWF_SHADE_RGB(f->shading));
    if (f->ntabs) {
        owf_buf_puts(out, "<w:tabs>");
        /* Word measures tab stops from the page's margin, not the indent. */
        for (i = 0; i < f->ntabs; i++)
        {
            static const char *const leader[] = { NULL, "dot", "hyphen", "underscore", "middleDot", "heavy" };
            unsigned l = (unsigned)f->tabs[i].leader;
            owf_buf_printf(out, "<w:tab w:val=\"%s\"", tab_val[f->tabs[i].kind]);
            if (l > 0 && l < sizeof leader / sizeof leader[0])
                owf_buf_printf(out, " w:leader=\"%s\"", leader[l]);
            owf_buf_printf(out, " w:pos=\"%d\"/>", f->tabs[i].position + f->indent_left);
        }
        owf_buf_puts(out, "</w:tabs>");
    }
    if (spacing || style) {
        /* 1.3.1: a heading's before and after even when 0: our Heading styles have their own (build_styles), which
         * came back in their place */
        owf_buf_puts(out, "<w:spacing");
        if (f->space_before || style)
            owf_buf_printf(out, " w:before=\"%d\"", f->space_before);
        if (f->space_after || style)
            owf_buf_printf(out, " w:after=\"%d\"", f->space_after);
        if (OWF_LINE_IS_HEIGHT(f->line_spacing))     /* 2.0.18 */
            owf_buf_printf(out, " w:line=\"%d\" w:lineRule=\"%s\"", OWF_LINE_HEIGHT(f->line_spacing),
                           OWF_LINE_IS_EXACT(f->line_spacing) ? "exact" : "atLeast");
        else if (f->line_spacing && f->line_spacing != 100)
            owf_buf_printf(out, " w:line=\"%d\" w:lineRule=\"auto\"", (f->line_spacing * 240 + 50) / 100);
        owf_buf_puts(out, "/>");
    }
    if (ind) {
        owf_buf_puts(out, "<w:ind");
        if (f->indent_left)
            owf_buf_printf(out, " w:left=\"%d\"", f->indent_left);
        if (f->indent_right)
            owf_buf_printf(out, " w:right=\"%d\"", f->indent_right);
        if (f->indent_first > 0)
            owf_buf_printf(out, " w:firstLine=\"%d\"", f->indent_first);
        else if (f->indent_first < 0)
            owf_buf_printf(out, " w:hanging=\"%d\"", -f->indent_first);
        owf_buf_puts(out, "/>");
    }
    if (f->align != OWF_ALIGN_LEFT)
        owf_buf_printf(out, "<w:jc w:val=\"%s\"/>", jc[f->align]);
    if (mark) {
        /* 1.5: the paragraph mark inserted or deleted */
        owf_buf_puts(out, mark->kind == OWF_REV_DELETE ? "<w:rPr><w:del" : "<w:rPr><w:ins");
        put_change_attrs(out, mark, mark_id);
        owf_buf_puts(out, "/></w:rPr>");
    }
    if (section_end) {
        owf_buf_puts(out, "<w:sectPr>");
        put_section_columns(out, doc, section, next_break);
        owf_buf_puts(out, "</w:sectPr>");
    }
    owf_buf_puts(out, "</w:pPr>");
}

static int body_link_id(const owf_doc *doc, const char *href)
{
    int i,j,id=10;
    if(!doc||!href)return 0;
    for(i=0;i<doc->body.nparas;++i)for(j=0;j<doc->body.paras[i].nruns;++j){
        const owf_run *r=&doc->body.paras[i].runs[j];
        int p,q,seen=0;
        if(r->kind!=OWF_RUN_TEXT||!r->href)continue;
        for(p=0;p<i&&!seen;++p)for(q=0;q<doc->body.paras[p].nruns;++q){const owf_run *x=&doc->body.paras[p].runs[q];if(x->kind==OWF_RUN_TEXT&&x->href&&!strcmp(x->href,r->href)){seen=1;break;}}
        if(!seen)for(q=0;q<j;++q){const owf_run *x=&doc->body.paras[i].runs[q];if(x->kind==OWF_RUN_TEXT&&x->href&&!strcmp(x->href,r->href)){seen=1;break;}}
        if(seen)continue;
        if(!strcmp(r->href,href))return id;
        ++id;
    }
    return 0;
}

static const char *docx_image_ext(const owf_image *im)
{
    if(im&&im->mime){if(!strcmp(im->mime,"image/png"))return "png";if(!strcmp(im->mime,"image/jpeg"))return "jpg";if(!strcmp(im->mime,"image/gif"))return "gif";if(!strcmp(im->mime,"image/webp"))return "webp";if(!strcmp(im->mime,"image/bmp"))return "bmp";}return "bin";
}

static void put_drawing(owf_buf *out,const owf_doc *doc,int index)
{
    const owf_image *im;if(index<0||index>=doc->nimages)return;im=&doc->images[index];
    owf_buf_printf(out,"<w:drawing><wp:inline><wp:extent cx=\"%ld\" cy=\"%ld\"/><wp:docPr id=\"%d\" name=\"Picture %d\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\"%d\" name=\"Picture %d\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing>",(long)im->width*635L,(long)im->height*635L,index+1,index+1,index+1,index+1,100+index,(long)im->width*635L,(long)im->height*635L);
}

static void put_para(owf_buf *out, const owf_doc *doc, const owf_story *story,
                     const owf_para *para, owf_report *report,
                     int section_end, const owf_para *section, int next_break);

/* 1.3: wp:positionH or wp:positionV for a frame: from where, and at an offset or aligned. */
static void put_frame_position(owf_buf *out, const owf_frame *fr, int across)
{
    static const char *const from_h[] = { "page", "margin", "column", "leftMargin", "rightMargin", "page", "page" };
    static const char *const from_v[] = { "page", "margin", "paragraph", "topMargin", "bottomMargin", "topMargin",
                                          "bottomMargin" };
    static const char *const align_h[] = { "", "left", "center", "right" };
    static const char *const align_v[] = { "", "top", "center", "bottom" };
    unsigned from = across ? fr->x_from : fr->y_from, place = across ? fr->x_place : fr->y_place;
    if (from > OWF_FROM_BOTTOM_MARGIN) from = OWF_FROM_MARGIN;
    owf_buf_printf(out, "<wp:position%s relativeFrom=\"%s\">", across ? "H" : "V", across ? from_h[from] : from_v[from]);
    if (place >= OWF_PLACE_START && place <= OWF_PLACE_END)
        owf_buf_printf(out, "<wp:align>%s</wp:align>", across ? align_h[place] : align_v[place]);
    else
        owf_buf_printf(out, "<wp:posOffset>%ld</wp:posOffset>", (long)(across ? fr->x : fr->y) * 635L);
    owf_buf_printf(out, "</wp:position%s>", across ? "H" : "V");
}

/* 1.3: a DrawingML colour fill (OWF_SHADE), or none. */
static void put_frame_fill(owf_buf *out, unsigned long shade)
{
    if (OWF_SHADE_SET(shade))
        owf_buf_printf(out, "<a:solidFill><a:srgbClr val=\"%06lX\"/></a:solidFill>", OWF_SHADE_RGB(shade));
    else
        owf_buf_puts(out, "<a:noFill/>");
}

/* 1.3: a frame (a picture or a text box placed on the page) as Word's wp:anchor: a picture as pic:pic, a text
 * box as a wps shape inside mc:AlternateContent (Word 2010's; its VML fallback is not written). The body's
 * frames only: a header's pictures are its own part's, and Word allows no text box in a text box. */
static void put_frame(owf_buf *out, const owf_doc *doc, int index, owf_report *report)
{
    const owf_frame *fr;
    long cx, cy;
    int k, box, id = 1000 + index;
    if (index < 0 || index >= doc->nframes) return;
    fr = &doc->frames[index];
    box = fr->story.nparas > 0 || fr->image < 0 || fr->image >= doc->nimages;
    cx = (long)(fr->width > 0 ? fr->width : 1440) * 635L;
    cy = (long)(fr->height > 0 ? fr->height : 1440) * 635L;
    if (box) owf_buf_puts(out, "<mc:AlternateContent><mc:Choice Requires=\"wps\">");
    if (fr->in_line) {                                   /* 2.0.17: a text box in the line, as Word's wp:inline */
        owf_buf_printf(out, "<w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
                            "<wp:extent cx=\"%ld\" cy=\"%ld\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>", cx, cy);
        goto drawn_place;
    }
    owf_buf_printf(out, "<w:drawing><wp:anchor distT=\"0\" distB=\"0\" distL=\"114300\" distR=\"114300\" simplePos=\"0\" "
                        "relativeHeight=\"%ld\" behindDoc=\"%d\" locked=\"0\" layoutInCell=\"1\" allowOverlap=\"1\">"
                        "<wp:simplePos x=\"0\" y=\"0\"/>", 251658240L + index, fr->behind ? 1 : 0);
    put_frame_position(out, fr, 1);
    put_frame_position(out, fr, 0);
    owf_buf_printf(out, "<wp:extent cx=\"%ld\" cy=\"%ld\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>", cx, cy);
    switch (fr->wrap) {
    case OWF_WRAP_SQUARE: owf_buf_puts(out, "<wp:wrapSquare wrapText=\"bothSides\"/>"); break;
    case OWF_WRAP_TIGHT:
        owf_buf_puts(out, "<wp:wrapTight wrapText=\"bothSides\"><wp:wrapPolygon edited=\"0\"><wp:start x=\"0\" y=\"0\"/>"
                          "<wp:lineTo x=\"0\" y=\"21600\"/><wp:lineTo x=\"21600\" y=\"21600\"/><wp:lineTo x=\"21600\" y=\"0\"/>"
                          "<wp:lineTo x=\"0\" y=\"0\"/></wp:wrapPolygon></wp:wrapTight>");
        break;
    case OWF_WRAP_TOP_BOTTOM: owf_buf_puts(out, "<wp:wrapTopAndBottom/>"); break;
    default: owf_buf_puts(out, "<wp:wrapNone/>"); break;
    }
drawn_place:
    owf_buf_printf(out, "<wp:docPr id=\"%d\" name=\"%s %d\"/><wp:cNvGraphicFramePr/><a:graphic>", id,
                   box ? "Text Box" : "Picture", index + 1);
    if (!box) {
        owf_buf_printf(out, "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic>"
                            "<pic:nvPicPr><pic:cNvPr id=\"%d\" name=\"Picture %d\"/><pic:cNvPicPr/></pic:nvPicPr>"
                            "<pic:blipFill><a:blip r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
                            "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%ld\" cy=\"%ld\"/></a:xfrm>"
                            "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData>",
                       id, index + 1, 100 + fr->image, cx, cy);
    } else {
        static const char *const ins[4] = { "tIns", "rIns", "bIns", "lIns" };
        owf_buf_printf(out, "<a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\">"
                            "<wps:wsp><wps:cNvSpPr txBox=\"1\"/><wps:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
                            "<a:ext cx=\"%ld\" cy=\"%ld\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>", cx, cy);
        if (fr->image >= 0 && fr->image < doc->nimages)
            owf_buf_printf(out, "<a:blipFill><a:blip r:embed=\"rId%d\"/><a:stretch><a:fillRect/></a:stretch></a:blipFill>",
                           100 + fr->image);
        else
            put_frame_fill(out, fr->fill);
        owf_buf_puts(out, "<a:ln w=\"9525\">");
        put_frame_fill(out, fr->line);
        owf_buf_puts(out, "</a:ln></wps:spPr>");
        if (fr->story.nparas) {
            owf_buf_puts(out, "<wps:txbx><w:txbxContent>");
            for (k = 0; k < fr->story.nparas; ++k)
                put_para(out, doc, &fr->story, &fr->story.paras[k], report, 0, NULL, 0);
            owf_buf_puts(out, "</w:txbxContent></wps:txbx>");
        }
        owf_buf_puts(out, "<wps:bodyPr rot=\"0\" vert=\"horz\" wrap=\"square\"");
        for (k = 0; k < 4; ++k) owf_buf_printf(out, " %s=\"%ld\"", ins[k], (long)fr->inset[k] * 635L);
        owf_buf_puts(out, " anchor=\"t\" anchorCtr=\"0\"><a:noAutofit/></wps:bodyPr></wps:wsp></a:graphicData>");
    }
    owf_buf_puts(out, fr->in_line ? "</a:graphic></wp:inline></w:drawing>" : "</a:graphic></wp:anchor></w:drawing>");
    if (box) owf_buf_puts(out, "</mc:Choice><mc:Fallback/></mc:AlternateContent>");
}

/* 1.4: a paragraph style of the document's own that we write (TOC1, Title...): a plain id, and none of ours
 * (build_styles writes Normal, Heading1 to 6, Header and Footer itself). */
static int own_style(const char *id)
{
    const char *c;
    if (!id || !*id || strlen(id) > 64) return 0;
    for (c = id; *c; ++c)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-'))
            return 0;
    return strcmp(id, "Normal") && strcmp(id, "Header") && strcmp(id, "Footer") && strncmp(id, "Heading", 7);
}

/* 1.4: a bookmark's w:id, the same each time and none twice: the story (body, header, footer, a frame's), the
 * paragraph's place in it, the bookmark's in the paragraph. */
static long bookmark_id(const owf_doc *doc, const owf_story *story, const owf_para *para, int k)
{
    long base = 0;
    int i;
    if (story == &doc->header) base = 1000000L;
    else if (story == &doc->footer) base = 2000000L;
    else if (story == &doc->header_left) base = 1500000L;   /* 2.0.23 */
    else if (story == &doc->footer_left) base = 2500000L;
    else if (story != &doc->body)
        for (i = 0; i < doc->nframes; ++i)
            if (&doc->frames[i].story == story) base = 3000000L + (long)i * 10000L;
    return base + (long)(para - story->paras) * 16L + k + 1;
}

/* 2.0.21: a paragraph's bookmarks as they are written: each one's id, and whether it is begun (1) or ended (2) */
typedef struct {
    owf_mark m[16];
    long id[16];
    unsigned char state[16];
    int n;
} docx_marks;

/* The bookmarks at text offset at: the ones begun before that end here, then (starts: text follows, or the
 * paragraph ends) the ones that begin here, an empty one ended where it begins. */
static void put_marks(owf_buf *out, docx_marks *mk, size_t at, int starts)
{
    int i;
    for (i = 0; i < mk->n; ++i)
        if (mk->state[i] == 1 && mk->m[i].end <= at) {
            owf_buf_printf(out, "<w:bookmarkEnd w:id=\"%ld\"/>", mk->id[i]);
            mk->state[i] = 2;
        }
    if (!starts)
        return;
    for (i = 0; i < mk->n; ++i)
        if (mk->state[i] == 0 && mk->m[i].start <= at) {
            owf_buf_printf(out, "<w:bookmarkStart w:id=\"%ld\" w:name=\"", mk->id[i]);
            owf_buf_put_xml(out, mk->m[i].name, mk->m[i].length);
            owf_buf_puts(out, "\"/>");
            mk->state[i] = 1;
            if (mk->m[i].end <= at) {
                owf_buf_printf(out, "<w:bookmarkEnd w:id=\"%ld\"/>", mk->id[i]);
                mk->state[i] = 2;
            }
        }
}

/* Where a text run [at, at + len) is cut next after done, for a bookmark that begins or ends inside it: len
 * when none does. At a character's start. */
static size_t next_cut(const docx_marks *mk, const char *text, size_t at, size_t done, size_t len)
{
    size_t next = len;
    int i;
    for (i = 0; i < mk->n; ++i) {
        if (mk->state[i] == 0 && mk->m[i].start > at + done && mk->m[i].start < at + len && mk->m[i].start - at < next)
            next = mk->m[i].start - at;
        if (mk->state[i] != 2 && mk->m[i].end > at + done && mk->m[i].end < at + len && mk->m[i].end - at < next)
            next = mk->m[i].end - at;
    }
    while (next < len && ((unsigned char)text[next] & 0xC0) == 0x80)
        ++next;
    return next;
}

/* 1.5: a change's w:id, w:author and w:date */
static void put_change_attrs(owf_buf *out, const owf_revision *r, long id)
{
    owf_buf_printf(out, " w:id=\"%ld\" w:author=\"", id);
    owf_buf_puts_xml(out, r->author ? r->author : "Unknown");
    owf_buf_puts(out, "\"");
    if (r->date) {
        /* Word wants the time zone: ours are local, as Word writes them, with Z */
        size_t n = strlen(r->date);
        owf_buf_puts(out, " w:date=\"");
        owf_buf_puts_xml(out, r->date);
        if (n == 19)
            owf_buf_putc(out, 'Z');
        owf_buf_puts(out, "\"");
    }
}

/* 1.5: a change's w:id, the same each time and none twice (nor a bookmark's): from the story, the runs and
 * paragraph ends before it in the story, and the run (j = nruns for the paragraph's end); four each, for the
 * pieces bookmarks cut a run into. */
static long change_id(const owf_doc *doc, const owf_story *story, const owf_para *para, int j)
{
    long base = 10000000L, before = 0;
    int i, k = 1;
    const owf_para *q;
    if (story == &doc->header) k = 2;
    else if (story == &doc->footer) k = 3;
    else if (story == &doc->header_left) k = 4;
    else if (story == &doc->footer_left) k = 5;
    else if (story != &doc->body)
        for (i = 0; i < doc->nframes; ++i)
            if (&doc->frames[i].story == story) k = 6 + i;
    if (k > 900) k = 900;
    for (q = story->paras; q < para; ++q)
        before += q->nruns + 1;
    return base + (long)k * 2000000L + (before + j) * 4L;    /* +1 to +3: a run's later pieces */
}

static const owf_revision *run_change(const owf_doc *doc, int number)
{
    return OWF_DOC_REVISION(doc, number);
}

/* 1.6: comments: w:id 5000000 + the comment's index (the document's annotations share one set of ids, and
 * bookmarks and changes have theirs below and above), the last paragraph of its text w14:paraId 10000000 + it */
#define COMMENT_ID(index) (5000000L + (long)(index))
#define COMMENT_PARA_ID(index) (0x10000000UL + (unsigned long)(index))

static int comment_ranged(const owf_doc *doc, int index)
{
    owf_story *st;
    int n, i, j;
    for (n = 0; (st = owf_doc_story((owf_doc *)doc, n)) != NULL; ++n)
        for (i = 0; i < st->nparas; ++i)
            for (j = 0; j < st->paras[i].nruns; ++j)
                if (st->paras[i].runs[j].comment == -(index + 1))
                    return 1;
    return 0;
}

/* A comment's place: where its range starts, or its end and reference */
static void put_comment_mark(owf_buf *out, const owf_doc *doc, int mark)
{
    int index = mark > 0 ? mark - 1 : -mark - 1;
    if (index < 0 || index >= doc->ncomments)
        return;
    if (mark < 0) {
        owf_buf_printf(out, "<w:commentRangeStart w:id=\"%ld\"/>", COMMENT_ID(index));
        return;
    }
    if (comment_ranged(doc, index))
        owf_buf_printf(out, "<w:commentRangeEnd w:id=\"%ld\"/>", COMMENT_ID(index));
    owf_buf_printf(out, "<w:r><w:commentReference w:id=\"%ld\"/></w:r>", COMMENT_ID(index));
}

/* Whether any comment in the document answers another or is done: commentsExtended.xml then */
static int comments_extended(const owf_doc *doc)
{
    int i;
    for (i = 0; i < doc->ncomments; ++i)
        if ((doc->comments[i].parent >= 0 || doc->comments[i].done) && owf_comment_place(doc, i, NULL, NULL, NULL))
            return 1;
    return 0;
}

static void build_comments(owf_buf *out, const owf_doc *doc)
{
    int i;
    owf_buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:comments " W_NS " "
                      "xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\" mc:Ignorable=\"w14\">");
    for (i = 0; i < doc->ncomments; ++i) {
        const owf_annotation *c = &doc->comments[i];
        const char *t, *nl;
        if (!owf_comment_place(doc, i, NULL, NULL, NULL))
            continue;
        owf_buf_printf(out, "<w:comment w:id=\"%ld\" w:author=\"", COMMENT_ID(i));
        owf_buf_puts_xml(out, c->author ? c->author : "");
        owf_buf_puts(out, "\"");
        if (c->date) {
            /* Word wants a time zone: a date without one is UTC */
            owf_buf_puts(out, " w:date=\"");
            owf_buf_puts_xml(out, c->date);
            if (strlen(c->date) == 19) owf_buf_putc(out, 'Z');
            owf_buf_puts(out, "\"");
        }
        if (c->initials) {
            owf_buf_puts(out, " w:initials=\"");
            owf_buf_puts_xml(out, c->initials);
            owf_buf_puts(out, "\"");
        }
        owf_buf_puts(out, ">");
        for (t = c->text ? c->text : "", nl = NULL; ; t = nl + 1) {
            int first = t == (c->text ? c->text : "");
            nl = strchr(t, '\n');
            if (nl) owf_buf_puts(out, "<w:p>");
            else owf_buf_printf(out, "<w:p w14:paraId=\"%08lX\">", COMMENT_PARA_ID(i));
            if (first) owf_buf_puts(out, "<w:r><w:annotationRef/></w:r>");
            owf_buf_puts(out, "<w:r><w:t xml:space=\"preserve\">");
            owf_buf_put_xml(out, t, nl ? (size_t)(nl - t) : strlen(t));
            owf_buf_puts(out, "</w:t></w:r></w:p>");
            if (!nl)
                break;
        }
        owf_buf_puts(out, "</w:comment>");
    }
    owf_buf_puts(out, "</w:comments>\n");
}

static void build_comments_extended(owf_buf *out, const owf_doc *doc)
{
    int i;
    owf_buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                      "<w15:commentsEx xmlns:w15=\"http://schemas.microsoft.com/office/word/2012/wordml\">");
    for (i = 0; i < doc->ncomments; ++i) {
        const owf_annotation *c = &doc->comments[i];
        if (!owf_comment_place(doc, i, NULL, NULL, NULL))
            continue;
        owf_buf_printf(out, "<w15:commentEx w15:paraId=\"%08lX\"", COMMENT_PARA_ID(i));
        if (c->parent >= 0 && c->parent < doc->ncomments && owf_comment_place(doc, c->parent, NULL, NULL, NULL))
            owf_buf_printf(out, " w15:paraIdParent=\"%08lX\"", COMMENT_PARA_ID(c->parent));
        owf_buf_printf(out, " w15:done=\"%d\"/>", c->done ? 1 : 0);
    }
    owf_buf_puts(out, "</w15:commentsEx>\n");
}

/* A field's code (1.7: with its argument) and the value shown for it */
static void field_instr(const owf_run *run, char *out, size_t size)
{
    static const char *const word[] = { "PAGE", "NUMPAGES", "DATE", "TIME", "TITLE", "AUTHOR", "SUBJECT", "FILENAME",
                                         "NUMWORDS", "STYLEREF", "DOCVARIABLE", "REF", "PAGEREF" };
    const char *arg = run->href ? run->href : "";
    int f = (int)run->field >= 0 && (int)run->field <= OWF_FIELD_LAST ? (int)run->field : 0;
    if (f == OWF_FIELD_CHAPTER) snprintf(out, size, " STYLEREF 1 ");
    else if (f == OWF_FIELD_USER) snprintf(out, size, " DOCVARIABLE \"%s\" ", arg);
    else if (f == OWF_FIELD_REF || f == OWF_FIELD_REF_PAGE) snprintf(out, size, " %s %s \\h ", word[f], arg);
    else snprintf(out, size, " %s ", word[f]);
}

static const char *field_shown(const owf_run *run)
{
    if (run->text) return run->text;
    return run->field == OWF_FIELD_PAGE || run->field == OWF_FIELD_PAGES ? "1" : "";
}

static void put_para(owf_buf *out, const owf_doc *doc, const owf_story *story,
                      const owf_para *para, owf_report *report,
                      int section_end, const owf_para *section, int next_break)
{
    char instr[160];
    char style[16];
    const char *pstyle = NULL;
    docx_marks mk;
    size_t at = 0;
    int j;
    if (para->fmt.heading >= 1 && para->fmt.heading <= 6) {
        snprintf(style, sizeof style, "Heading%d", para->fmt.heading); pstyle = style;
    } else if (own_style(para->style)) {
        pstyle = para->style;                        /* 1.4: TOC1 and the like, so Word's contents update */
    }
    owf_buf_puts(out, "<w:p>");
    put_ppr(out, doc, &para->fmt, pstyle, section_end, section, next_break,
            para < story->paras + story->nparas - 1 ? run_change(doc, para->mark_revision) : NULL,
            change_id(doc, story, para, para->nruns));
    /* 1.4: its bookmarks, where the contents' links go; 2.0.21: each where it is in the text */
    mk.n = para->bookmarks ? owf_marks_parse(para->bookmarks, mk.m, 16) : 0;
    for (j = 0; j < mk.n; ++j) {
        mk.id[j] = bookmark_id(doc, story, para, j);
        mk.state[j] = 0;
    }
    for (j = 0; j < para->nruns; ++j) {
        const owf_run *run=&para->runs[j];
        int link = run->kind == OWF_RUN_TEXT && run->href && story == &doc->body;
        if (run->comment) {                          /* 1.6 */
            put_comment_mark(out, doc, run->comment);
            continue;
        }
        /* 1.5: a tracked change's run inside w:ins or w:del (a picture or frame is never one) */
        const owf_revision *chg = run->kind != OWF_RUN_IMAGE && run->kind != OWF_RUN_FRAME ? run_change(doc, run->revision) : NULL;
        int del = chg && chg->kind == OWF_REV_DELETE;
        const char *open_chg = chg ? (del ? "<w:del" : "<w:ins") : NULL, *close_chg = del ? "</w:del>" : "</w:ins>";
        put_marks(out, &mk, at, run->kind == OWF_RUN_TEXT);
        if(run->kind==OWF_RUN_FIELD)field_instr(run,instr,sizeof instr);
        if(run->kind==OWF_RUN_FIELD&&chg){
            /* a changed field as Word's complex field, its runs inside the change */
            owf_buf_puts(out,open_chg);put_change_attrs(out,chg,change_id(doc,story,para,j));owf_buf_puts(out,">");
            owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);owf_buf_puts(out,"<w:fldChar w:fldCharType=\"begin\"/></w:r>");
            owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);owf_buf_puts(out,del?"<w:delInstrText xml:space=\"preserve\">":"<w:instrText xml:space=\"preserve\">");owf_buf_puts_xml(out,instr);owf_buf_puts(out,del?"</w:delInstrText></w:r>":"</w:instrText></w:r>");
            owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);owf_buf_puts(out,"<w:fldChar w:fldCharType=\"separate\"/></w:r>");
            owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);owf_buf_puts(out,del?"<w:delText xml:space=\"preserve\">":"<w:t xml:space=\"preserve\">");owf_buf_puts_xml(out,field_shown(run));owf_buf_puts(out,del?"</w:delText></w:r>":"</w:t></w:r>");
            owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);owf_buf_puts(out,"<w:fldChar w:fldCharType=\"end\"/></w:r>");
            owf_buf_puts(out,close_chg);
            continue;
        }
        if(run->kind==OWF_RUN_FIELD){owf_buf_puts(out,"<w:fldSimple w:instr=\"");owf_buf_puts_xml(out,instr);owf_buf_puts(out,"\"><w:r>");put_rpr(out,doc,&run->fmt,report);owf_buf_puts(out,"<w:t xml:space=\"preserve\">");owf_buf_puts_xml(out,field_shown(run));owf_buf_puts(out,"</w:t></w:r></w:fldSimple>");continue;}
        if(link){
            /* 1.3.1: a link inside the document ("#_Toc1", from w:anchor) stays w:anchor, with no relationship */
            if(run->href[0]=='#'){owf_buf_puts(out,"<w:hyperlink w:anchor=\"");owf_buf_puts_xml(out,run->href+1);owf_buf_puts(out,"\">");}
            else owf_buf_printf(out,"<w:hyperlink r:id=\"rId%d\">",body_link_id(doc,run->href));
        }
        if (run->kind == OWF_RUN_TEXT) {
            /* in pieces where bookmarks begin or end inside it */
            const char *text = run->text ? run->text : "";
            size_t len = strlen(text), done = 0;
            long piece = 0;
            do {
                size_t next = next_cut(&mk, text, at, done, len);
                if (chg) {
                    owf_buf_puts(out, open_chg);
                    put_change_attrs(out, chg, change_id(doc, story, para, j) + (piece < 3 ? piece : 3));
                    ++piece;
                    owf_buf_puts(out, ">");
                }
                owf_buf_puts(out, "<w:r>");
                put_rpr(out, doc, &run->fmt, report);
                owf_buf_puts(out, del ? "<w:delText xml:space=\"preserve\">" : "<w:t xml:space=\"preserve\">");
                owf_buf_put_xml(out, text + done, next - done);
                owf_buf_puts(out, del ? "</w:delText></w:r>" : "</w:t></w:r>");
                if (chg)
                    owf_buf_puts(out, close_chg);
                done = next;
                if (done < len)
                    put_marks(out, &mk, at + done, 1);
            } while (done < len);
            at += len;
        } else {
            if(chg){owf_buf_puts(out,open_chg);put_change_attrs(out,chg,change_id(doc,story,para,j));owf_buf_puts(out,">");}
            owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);
            switch(run->kind){case OWF_RUN_TAB:owf_buf_puts(out,"<w:tab/>");break;case OWF_RUN_LINEBREAK:owf_buf_puts(out,"<w:br/>");break;case OWF_RUN_IMAGE:put_drawing(out,doc,run->image);break;
                case OWF_RUN_FRAME:
                    /* 2.0.16: a header's and a footer's too (their parts' relationships have the pictures' ids) */
                    if (story == &doc->body || story == &doc->header || story == &doc->footer ||
                        story == &doc->header_left || story == &doc->footer_left) {
                        put_frame(out, doc, run->image, report);
                    } else {
                        owf_report_add(report, OWF_NOTE_LOST, "Pictures and text boxes placed in a text box are left out");
                    }
                    break;
                default:
                    break;
            }
            owf_buf_puts(out,"</w:r>");
            if(chg)owf_buf_puts(out,close_chg);
        }
        if(link)owf_buf_puts(out,"</w:hyperlink>");
    }
    put_marks(out, &mk, at, 1);                      /* what is left: at the paragraph's end */
    put_marks(out, &mk, (size_t)-1, 0);
    owf_buf_puts(out,"</w:p>");
}

static void put_story(owf_buf *out, const owf_doc *doc, const owf_story *story, owf_report *report)
{
    const owf_para *section = NULL;
    int i = 0, body = story == &doc->body;
    while (i < story->nparas) {
        const owf_para *p = &story->paras[i];
        if (body && p->section_columns > 0) section = p;
        if (body && p->table_id >= 0) {
            int id = p->table_id, row = -1;
            owf_buf_puts(out,"<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders><w:top w:val=\"single\" w:sz=\"4\"/><w:left w:val=\"single\" w:sz=\"4\"/><w:bottom w:val=\"single\" w:sz=\"4\"/><w:right w:val=\"single\" w:sz=\"4\"/><w:insideH w:val=\"single\" w:sz=\"4\"/><w:insideV w:val=\"single\" w:sz=\"4\"/></w:tblBorders></w:tblPr>");
            while (i < story->nparas && story->paras[i].table_id == id) {
                p=&story->paras[i];if(p->table_row!=row){if(row>=0)owf_buf_puts(out,"</w:tr>");owf_buf_puts(out,"<w:tr>");row=p->table_row;}
                owf_buf_puts(out,"<w:tc><w:tcPr><w:tcW w:w=\"0\" w:type=\"auto\"/>");
                if(OWF_SHADE_SET(p->cell_shading))owf_buf_printf(out,"<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>",OWF_SHADE_RGB(p->cell_shading));
                owf_buf_puts(out,"</w:tcPr>");put_para(out,doc,story,p,report,0,NULL,0);owf_buf_puts(out,"</w:tc>");++i;
            }
            if(row>=0)owf_buf_puts(out,"</w:tr>");
            owf_buf_puts(out,"</w:tbl>");
            /* Word cannot put a section break inside a table cell. Put an
             * empty boundary paragraph after the table when the next section starts. */
            if (i < story->nparas && story->paras[i].section_columns > 0) {
                owf_parafmt empty;
                owf_parafmt_init(&empty);
                owf_buf_puts(out, "<w:p>");
                put_ppr(out, doc, &empty, NULL, 1, section, story->paras[i].section_break, NULL, 0);
                owf_buf_puts(out, "</w:p>");
            }
        } else {
            int boundary = body && i + 1 < story->nparas && story->paras[i + 1].section_columns > 0;
            int next_break = boundary ? story->paras[i + 1].section_break : 0;
            put_para(out, doc, story, p, report, boundary, section, next_break);
            ++i;
        }
    }
}

/* 1.3: a part's relationships to the document's pictures, rId100 on (a header's or a footer's) */
static void put_image_rels(owf_buf *part, const owf_doc *doc)
{
    int i;
    owf_buf_puts(part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (i = 0; i < doc->nimages; ++i)
        owf_buf_printf(part, "<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
                             "relationships/image\" Target=\"media/image%d.%s\"/>", 100 + i, i + 1,
                       docx_image_ext(&doc->images[i]));
    owf_buf_puts(part, "</Relationships>\n");
}

static int add_part(owf_zip *zip, owf_buf *part, const char *name)
{
    int result = part->failed ? OWF_ERR_MEMORY : owf_zip_add(zip, name, part->data, part->length, 1);
    part->length = 0;
    return result;
}

/* 1.4: the name Word gives a style of its own by id (it knows its contents levels by "toc 1" to "toc 9"); any
 * other style is named as its id. */
static void put_style_name(owf_buf *out, const char *id)
{
    static const char *const known[][2] = {
        { "TOCHeading", "TOC Heading" }, { "ListParagraph", "List Paragraph" }, { "NoSpacing", "No Spacing" },
        { "Caption", "caption" }, { "IntenseQuote", "Intense Quote" }, { "Title", "Title" }, { "Subtitle", "Subtitle" },
        { "Quote", "Quote" }, { "BodyText", "Body Text" }, { "NormalWeb", "Normal (Web)" }
    };
    size_t i;
    if (!strncmp(id, "TOC", 3) && id[3] >= '1' && id[3] <= '9' && !id[4]) {
        owf_buf_printf(out, "<w:name w:val=\"toc %c\"/>", id[3]);
        return;
    }
    for (i = 0; i < sizeof known / sizeof known[0]; ++i)
        if (!strcmp(id, known[i][0])) {
            owf_buf_printf(out, "<w:name w:val=\"%s\"/>", known[i][1]);
            return;
        }
    owf_buf_printf(out, "<w:name w:val=\"%s\"/>", id);
}

/* 1.4: the paragraph styles the document's paragraphs name (own_style), each once, with no format of their own
 * (each paragraph's is written with it). */
static void put_own_styles(owf_buf *out, const owf_doc *doc)
{
    const char *seen[64];
    int nseen = 0, s, i, k;
    for (s = 0; s < 5 + doc->nframes; ++s) {
        const owf_story *st = s == 0 ? &doc->body : s == 1 ? &doc->header : s == 2 ? &doc->footer
                            : s == 3 ? &doc->header_left : s == 4 ? &doc->footer_left
                            : &doc->frames[s - 5].story;
        for (i = 0; i < st->nparas && nseen < 64; ++i) {
            const char *id = st->paras[i].style;
            if (st->paras[i].fmt.heading >= 1 && st->paras[i].fmt.heading <= 6) continue;
            if (!own_style(id)) continue;
            for (k = 0; k < nseen && strcmp(seen[k], id); ++k)
                ;
            if (k < nseen) continue;
            seen[nseen++] = id;
            owf_buf_printf(out, "<w:style w:type=\"paragraph\" w:styleId=\"%s\">", id);
            put_style_name(out, id);
            owf_buf_puts(out, "<w:basedOn w:val=\"Normal\"/><w:qFormat/></w:style>");
        }
    }
}

static void build_styles(owf_buf *out, const owf_doc *doc)
{
    static const int heading_half_points[] = { 0, 36, 32, 28, 26, 24, 24 };
    int i;

    owf_buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles " W_NS ">"
                      "<w:docDefaults><w:rPrDefault><w:rPr>");
    if (doc->base.font >= 0 && doc->base.font < doc->nfonts)
        put_fonts(out, face_name(doc, doc->base.font, NULL));
    else
        put_fonts(out, "Liberation Serif");
    owf_buf_printf(out, "<w:sz w:val=\"%d\"/><w:szCs w:val=\"%d\"/></w:rPr></w:rPrDefault>"
                        "<w:pPrDefault><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
                        "</w:docDefaults>",
                   ((doc->base.size ? doc->base.size : 240) + 5) / 10, ((doc->base.size ? doc->base.size : 240) + 5) / 10);
    owf_buf_puts(out, "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>");
    for (i = 1; i <= 6; i++)
        owf_buf_printf(out, "<w:style w:type=\"paragraph\" w:styleId=\"Heading%d\"><w:name w:val=\"heading %d\"/>"
                            "<w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:qFormat/>"
                            "<w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"120\"/><w:outlineLvl w:val=\"%d\"/></w:pPr>"
                            "<w:rPr><w:b/><w:sz w:val=\"%d\"/></w:rPr></w:style>",
                       i, i, i - 1, heading_half_points[i]);
    owf_buf_puts(out, "<w:style w:type=\"paragraph\" w:styleId=\"Header\"><w:name w:val=\"header\"/><w:basedOn w:val=\"Normal\"/></w:style>"
                      "<w:style w:type=\"paragraph\" w:styleId=\"Footer\"><w:name w:val=\"footer\"/><w:basedOn w:val=\"Normal\"/></w:style>");
    put_own_styles(out, doc);
    owf_buf_puts(out, "</w:styles>\n");
}

static void build_document(owf_buf *out, const owf_doc *doc, owf_report *report)
{
    static const char *num_fmt[] = { "decimal", "upperRoman", "lowerRoman", "upperLetter", "lowerLetter" };
    const owf_page *pg = &doc->page;
    int title_page = ((doc->header.nparas || doc->header_left.nparas) && !pg->header_on_first) ||
                     ((doc->footer.nparas || doc->footer_left.nparas) && !pg->footer_on_first);
    int even = doc->header_left.nparas || doc->footer_left.nparas;
    int first_left = !(pg->start_page & 1);
    int columns = 1, column_gap = 720, i;

    for (i = 0; i < doc->body.nparas; ++i)
        if (doc->body.paras[i].section_columns > 0) {
            columns = doc->body.paras[i].section_columns;
            column_gap = doc->body.paras[i].section_gap > 0 ? doc->body.paras[i].section_gap : 720;
        }

    owf_buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document " W_NS "><w:body>");
    put_story(out, doc, &doc->body, report);
    owf_buf_puts(out, "<w:sectPr>");
    /* With a title page, Word shows the "first" header there: the same part
     * when it is wanted on the first page, nothing when it is not. 2.0.23:
     * with left pages' own (settings' w:evenAndOddHeaders), Word shows the
     * "even" one on even-numbered pages: header2.xml, or the same part as
     * the others' when only the footer differs; the first page's is the one
     * its number gives it. */
    if (doc->header.nparas) {
        owf_buf_puts(out, "<w:headerReference w:type=\"default\" r:id=\"rId2\"/>");
    }
    if (doc->header_left.nparas || (even && doc->header.nparas))
        owf_buf_printf(out, "<w:headerReference w:type=\"even\" r:id=\"rId%d\"/>", doc->header_left.nparas ? 4 : 2);
    if (title_page && pg->header_on_first && (doc->header.nparas || doc->header_left.nparas))
        owf_buf_printf(out, "<w:headerReference w:type=\"first\" r:id=\"rId%d\"/>",
                       first_left && doc->header_left.nparas ? 4 : 2);
    if (doc->footer.nparas) {
        owf_buf_puts(out, "<w:footerReference w:type=\"default\" r:id=\"rId3\"/>");
    }
    if (doc->footer_left.nparas || (even && doc->footer.nparas))
        owf_buf_printf(out, "<w:footerReference w:type=\"even\" r:id=\"rId%d\"/>", doc->footer_left.nparas ? 5 : 3);
    if (title_page && pg->footer_on_first && (doc->footer.nparas || doc->footer_left.nparas))
        owf_buf_printf(out, "<w:footerReference w:type=\"first\" r:id=\"rId%d\"/>",
                       first_left && doc->footer_left.nparas ? 5 : 3);
    owf_buf_printf(out, "<w:pgSz w:w=\"%d\" w:h=\"%d\"/>", pg->width, pg->height);
    owf_buf_printf(out, "<w:pgMar w:top=\"%d\" w:right=\"%d\" w:bottom=\"%d\" w:left=\"%d\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/>",
                   pg->margin_top, pg->margin_right, pg->margin_bottom, pg->margin_left);
    owf_buf_printf(out, "<w:cols w:num=\"%d\" w:space=\"%d\"/>", columns, column_gap);
    owf_buf_printf(out, "<w:pgNumType w:fmt=\"%s\"", num_fmt[pg->pagenum_style]);
    if (pg->start_page != 1)
        owf_buf_printf(out, " w:start=\"%d\"", pg->start_page);
    owf_buf_puts(out, "/>");
    if (title_page)
        owf_buf_puts(out, "<w:titlePg/>");
    owf_buf_puts(out, "</w:sectPr></w:body></w:document>\n");
}

static int export_docx(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    owf_buf out, part;
    owf_zip *zip;
    int result;
    int header = doc->header.nparas > 0, footer = doc->footer.nparas > 0;
    int header_left = doc->header_left.nparas > 0, footer_left = doc->footer_left.nparas > 0;   /* 2.0.23 */
    int comments = owf_doc_has_comments(doc), comments_ext = comments && comments_extended(doc);   /* 1.6 */

    owf_buf_init(&out);
    owf_buf_init(&part);
    zip = owf_zip_new(&out);
    if (!zip)
        return OWF_ERR_MEMORY;

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
                        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>");
    if (header)
        owf_buf_puts(&part, "<Override PartName=\"/word/header1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>");
    if (footer)
        owf_buf_puts(&part, "<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>");
    if (header_left)
        owf_buf_puts(&part, "<Override PartName=\"/word/header2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>");
    if (footer_left)
        owf_buf_puts(&part, "<Override PartName=\"/word/footer2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>");
    if (header_left || footer_left || doc->track_changes || doc->nuserfields)
        owf_buf_puts(&part, "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>");
    if (comments) {
        owf_buf_puts(&part, "<Override PartName=\"/word/comments.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml\"/>");
        if (comments_ext)
            owf_buf_puts(&part, "<Override PartName=\"/word/commentsExtended.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.commentsExtended+xml\"/>");
    }
    { int i; for(i=0;i<doc->nimages;++i){owf_buf_printf(&part,"<Override PartName=\"/word/media/image%d.%s\" ContentType=\"",i+1,docx_image_ext(&doc->images[i]));owf_buf_puts_xml(&part,doc->images[i].mime?doc->images[i].mime:"application/octet-stream");owf_buf_puts(&part,"\"/>");} }
    owf_buf_puts(&part, "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
                        "</Types>\n");
    result = add_part(zip, &part, "[Content_Types].xml");

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
                        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
                        "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
                        "</Relationships>\n");
    if (result == OWF_OK)
        result = add_part(zip, &part, "_rels/.rels");

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
    if (header)
        owf_buf_puts(&part, "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" Target=\"header1.xml\"/>");
    if (footer)
        owf_buf_puts(&part, "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer1.xml\"/>");
    if (header_left)
        owf_buf_puts(&part, "<Relationship Id=\"rId4\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" Target=\"header2.xml\"/>");
    if (footer_left)
        owf_buf_puts(&part, "<Relationship Id=\"rId5\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" Target=\"footer2.xml\"/>");
    if (header_left || footer_left || doc->track_changes || doc->nuserfields)
        owf_buf_puts(&part, "<Relationship Id=\"rId6\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>");
    if (comments) {                                  /* 1.6 */
        owf_buf_puts(&part, "<Relationship Id=\"rId7\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments\" Target=\"comments.xml\"/>");
        if (comments_ext)
            owf_buf_puts(&part, "<Relationship Id=\"rId8\" Type=\"http://schemas.microsoft.com/office/2011/relationships/commentsExtended\" Target=\"commentsExtended.xml\"/>");
    }
    {
        int i,j;
        for(i=0;i<doc->body.nparas;++i)for(j=0;j<doc->body.paras[i].nruns;++j){
            const owf_run *run=&doc->body.paras[i].runs[j];
            int id,p,q,seen=0;
            if(run->kind!=OWF_RUN_TEXT||!run->href||run->href[0]=='#')continue;
            for(p=0;p<i&&!seen;++p)for(q=0;q<doc->body.paras[p].nruns;++q){const owf_run*x=&doc->body.paras[p].runs[q];if(x->kind==OWF_RUN_TEXT&&x->href&&!strcmp(x->href,run->href)){seen=1;break;}}
            if(!seen)for(q=0;q<j;++q){const owf_run*x=&doc->body.paras[i].runs[q];if(x->kind==OWF_RUN_TEXT&&x->href&&!strcmp(x->href,run->href)){seen=1;break;}}
            if(seen)continue;
            id=body_link_id(doc,run->href);
            owf_buf_printf(&part,"<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"",id);
            owf_buf_puts_xml(&part,run->href);
            owf_buf_puts(&part,"\" TargetMode=\"External\"/>");
        }
    }
    { int i; for(i=0;i<doc->nimages;++i)owf_buf_printf(&part,"<Relationship Id=\"rId%d\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/image%d.%s\"/>",100+i,i+1,docx_image_ext(&doc->images[i])); }
    owf_buf_puts(&part, "</Relationships>\n");
    if (result == OWF_OK)
        result = add_part(zip, &part, "word/_rels/document.xml.rels");

    build_document(&part, doc, report);
    if (result == OWF_OK)
        result = add_part(zip, &part, "word/document.xml");

    build_styles(&part, doc);
    if (result == OWF_OK)
        result = add_part(zip, &part, "word/styles.xml");

    if (comments) {                                  /* 1.6 */
        build_comments(&part, doc);
        if (result == OWF_OK)
            result = add_part(zip, &part, "word/comments.xml");
        if (comments_ext) {
            build_comments_extended(&part, doc);
            if (result == OWF_OK)
                result = add_part(zip, &part, "word/commentsExtended.xml");
        }
    }

    /* 1.3: a header's and a footer's pictures: their parts' own relationships, the same ids as the body's */
    {
        static const char *const rels_part[4] = { "word/_rels/header1.xml.rels", "word/_rels/footer1.xml.rels",
                                                  "word/_rels/header2.xml.rels", "word/_rels/footer2.xml.rels" };
        const int has[4] = { header, footer, header_left, footer_left };
        int k;
        for (k = 0; k < 4; ++k) {
            if (!has[k] || !doc->nimages) continue;
            put_image_rels(&part, doc);
            if (result == OWF_OK)
                result = add_part(zip, &part, rels_part[k]);
        }
    }
    if (header) {
        owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:hdr " W_NS ">");
        put_story(&part, doc, &doc->header, report);
        owf_buf_puts(&part, "</w:hdr>\n");
        if (result == OWF_OK)
            result = add_part(zip, &part, "word/header1.xml");
    }
    if (footer) {
        owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:ftr " W_NS ">");
        put_story(&part, doc, &doc->footer, report);
        owf_buf_puts(&part, "</w:ftr>\n");
        if (result == OWF_OK)
            result = add_part(zip, &part, "word/footer1.xml");
    }
    /* 2.0.23: left pages' own, and the setting that has Word use them */
    if (header_left) {
        owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:hdr " W_NS ">");
        put_story(&part, doc, &doc->header_left, report);
        owf_buf_puts(&part, "</w:hdr>\n");
        if (result == OWF_OK)
            result = add_part(zip, &part, "word/header2.xml");
    }
    if (footer_left) {
        owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:ftr " W_NS ">");
        put_story(&part, doc, &doc->footer_left, report);
        owf_buf_puts(&part, "</w:ftr>\n");
        if (result == OWF_OK)
            result = add_part(zip, &part, "word/footer2.xml");
    }
    if (header_left || footer_left || doc->track_changes || doc->nuserfields) {
        /* settings in the schema's order: w:trackRevisions comes before w:evenAndOddHeaders */
        owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                            "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
        if (doc->track_changes)
            owf_buf_puts(&part, "<w:trackRevisions/>");
        if (header_left || footer_left)
            owf_buf_puts(&part, "<w:evenAndOddHeaders/>");
        if (doc->nuserfields) {                      /* 1.7: the user fields, after them in the schema */
            int k;
            owf_buf_puts(&part, "<w:docVars>");
            for (k = 0; k < doc->nuserfields; ++k) {
                owf_buf_puts(&part, "<w:docVar w:name=\"");
                owf_buf_puts_xml(&part, doc->userfields[k].name);
                owf_buf_puts(&part, "\" w:val=\"");
                owf_buf_puts_xml(&part, doc->userfields[k].value);
                owf_buf_puts(&part, "\"/>");
            }
            owf_buf_puts(&part, "</w:docVars>");
        }
        owf_buf_puts(&part, "</w:settings>\n");
        if (result == OWF_OK)
            result = add_part(zip, &part, "word/settings.xml");
    }

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">");
    if (doc->title) {
        owf_buf_puts(&part, "<dc:title>");
        owf_buf_puts_xml(&part, doc->title);
        owf_buf_puts(&part, "</dc:title>");
    }
    if (doc->subject) {                              /* 1.7 */
        owf_buf_puts(&part, "<dc:subject>");
        owf_buf_puts_xml(&part, doc->subject);
        owf_buf_puts(&part, "</dc:subject>");
    }
    if (doc->author) {
        owf_buf_puts(&part, "<dc:creator>");
        owf_buf_puts_xml(&part, doc->author);
        owf_buf_puts(&part, "</dc:creator>");
    }
    owf_buf_puts(&part, "</cp:coreProperties>\n");
    if (result == OWF_OK)
        result = add_part(zip, &part, "docProps/core.xml");

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                        "<Application>GFOpenWrite libowf " OWF_VERSION "</Application></Properties>\n");
    if (result == OWF_OK)
        result = add_part(zip, &part, "docProps/app.xml");
    if(result==OWF_OK){int i;char name[96];for(i=0;i<doc->nimages&&result==OWF_OK;++i){snprintf(name,sizeof name,"word/media/image%d.%s",i+1,docx_image_ext(&doc->images[i]));result=owf_zip_add(zip,name,doc->images[i].data,doc->images[i].length,1);}}

    owf_buf_free(&part);
    if (owf_zip_finish(zip) != OWF_OK && result == OWF_OK)
        result = OWF_ERR_MEMORY;
    if (result != OWF_OK) {
        owf_buf_free(&out);
        return result;
    }
    return owf_buf_take(&out, data, length);
}

const owf_format owf_format_docx = {
    "docx", "Word document (Office Open XML)", "docx dotx docm", owf_detect_docx, owf_import_docx, export_docx
};

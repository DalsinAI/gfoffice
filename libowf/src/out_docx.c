/*
 * libowf: DOCX out (Office Open XML, ECMA-376 transitional): the content
 * types, the relationships, word/document.xml, word/styles.xml, a header
 * and a footer part, and the document's properties. Formatting is written
 * on each paragraph and run, as Word itself does for direct formatting.
 * Elements inside w:pPr and w:rPr follow the schema's order, which Word
 * insists on.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

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

static void put_ppr(owf_buf *out, const owf_parafmt *f, const char *style)
{
    static const char *tab_val[] = { "left", "center", "right", "decimal" };
    static const char *jc[] = { "left", "center", "right", "both" };
    int i, spacing = f->space_before || f->space_after || (f->line_spacing && f->line_spacing != 100);
    int ind = f->indent_left || f->indent_right || f->indent_first;

    if (!style && !f->page_break_before && !f->ntabs && !spacing && !ind && f->align == OWF_ALIGN_LEFT &&
        !f->borders && !OWF_SHADE_SET(f->shading))
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
        if (f->line_spacing && f->line_spacing != 100)
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
                     const owf_para *para, owf_report *report);

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
                put_para(out, doc, &fr->story, &fr->story.paras[k], report);
            owf_buf_puts(out, "</w:txbxContent></wps:txbx>");
        }
        owf_buf_puts(out, "<wps:bodyPr rot=\"0\" vert=\"horz\" wrap=\"square\"");
        for (k = 0; k < 4; ++k) owf_buf_printf(out, " %s=\"%ld\"", ins[k], (long)fr->inset[k] * 635L);
        owf_buf_puts(out, " anchor=\"t\" anchorCtr=\"0\"><a:noAutofit/></wps:bodyPr></wps:wsp></a:graphicData>");
    }
    owf_buf_puts(out, "</a:graphic></wp:anchor></w:drawing>");
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
    else if (story != &doc->body)
        for (i = 0; i < doc->nframes; ++i)
            if (&doc->frames[i].story == story) base = 3000000L + (long)i * 10000L;
    return base + (long)(para - story->paras) * 16L + k + 1;
}

static void put_para(owf_buf *out, const owf_doc *doc, const owf_story *story,
                     const owf_para *para, owf_report *report)
{
    static const char *field_instr[] = { " PAGE ", " NUMPAGES ", " DATE ", " TIME " };
    static const char *field_shown[] = { "1", "1", "", "" };
    char style[16];
    const char *pstyle = NULL;
    int j;
    if (para->fmt.heading >= 1 && para->fmt.heading <= 6) {
        snprintf(style, sizeof style, "Heading%d", para->fmt.heading); pstyle = style;
    } else if (own_style(para->style)) {
        pstyle = para->style;                        /* 1.4: TOC1 and the like, so Word's contents update */
    }
    owf_buf_puts(out, "<w:p>"); put_ppr(out, &para->fmt, pstyle);
    if (para->bookmarks) {
        /* 1.4: its bookmarks, where the contents' links go */
        const char *b = para->bookmarks;
        int k = 0;
        while (*b && k < 16) {
            size_t n = strcspn(b, " ");
            long id = bookmark_id(doc, story, para, k++);
            owf_buf_printf(out, "<w:bookmarkStart w:id=\"%ld\" w:name=\"", id);
            owf_buf_put_xml(out, b, n);
            owf_buf_printf(out, "\"/><w:bookmarkEnd w:id=\"%ld\"/>", id);
            b += n;
            while (*b == ' ') ++b;
        }
    }
    for (j = 0; j < para->nruns; ++j) {
        const owf_run *run=&para->runs[j];
        if(run->kind==OWF_RUN_FIELD){owf_buf_printf(out,"<w:fldSimple w:instr=\"%s\"><w:r>",field_instr[run->field]);put_rpr(out,doc,&run->fmt,report);owf_buf_printf(out,"<w:t>%s</w:t></w:r></w:fldSimple>",field_shown[run->field]);continue;}
        if(run->kind==OWF_RUN_TEXT&&run->href&&story==&doc->body){
            /* 1.3.1: a link inside the document ("#_Toc1", from w:anchor) stays w:anchor, with no relationship */
            if(run->href[0]=='#'){owf_buf_puts(out,"<w:hyperlink w:anchor=\"");owf_buf_puts_xml(out,run->href+1);owf_buf_puts(out,"\">");}
            else owf_buf_printf(out,"<w:hyperlink r:id=\"rId%d\">",body_link_id(doc,run->href));
        }
        owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);
        switch(run->kind){case OWF_RUN_TEXT:owf_buf_puts(out,"<w:t xml:space=\"preserve\">");owf_buf_puts_xml(out,run->text);owf_buf_puts(out,"</w:t>");break;case OWF_RUN_TAB:owf_buf_puts(out,"<w:tab/>");break;case OWF_RUN_LINEBREAK:owf_buf_puts(out,"<w:br/>");break;case OWF_RUN_IMAGE:put_drawing(out,doc,run->image);break;
            case OWF_RUN_FRAME:if(story==&doc->body)put_frame(out,doc,run->image,report);else owf_report_add(report,OWF_NOTE_LOST,"Pictures and text boxes placed on the page are kept in the body only");break;default:break;}
        owf_buf_puts(out,"</w:r>");if(run->kind==OWF_RUN_TEXT&&run->href&&story==&doc->body)owf_buf_puts(out,"</w:hyperlink>");
    }
    owf_buf_puts(out,"</w:p>");
}

static void put_story(owf_buf *out, const owf_doc *doc, const owf_story *story, owf_report *report)
{
    int i=0;
    while(i<story->nparas){
        const owf_para *p=&story->paras[i];
        if(story==&doc->body&&p->table_id>=0){
            int id=p->table_id,row=-1;
            owf_buf_puts(out,"<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders><w:top w:val=\"single\" w:sz=\"4\"/><w:left w:val=\"single\" w:sz=\"4\"/><w:bottom w:val=\"single\" w:sz=\"4\"/><w:right w:val=\"single\" w:sz=\"4\"/><w:insideH w:val=\"single\" w:sz=\"4\"/><w:insideV w:val=\"single\" w:sz=\"4\"/></w:tblBorders></w:tblPr>");
            while(i<story->nparas&&story->paras[i].table_id==id){
                p=&story->paras[i];if(p->table_row!=row){if(row>=0)owf_buf_puts(out,"</w:tr>");owf_buf_puts(out,"<w:tr>");row=p->table_row;}
                owf_buf_puts(out,"<w:tc><w:tcPr><w:tcW w:w=\"0\" w:type=\"auto\"/>");
                if(OWF_SHADE_SET(p->cell_shading))owf_buf_printf(out,"<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%06lX\"/>",OWF_SHADE_RGB(p->cell_shading));
                owf_buf_puts(out,"</w:tcPr>");put_para(out,doc,story,p,report);owf_buf_puts(out,"</w:tc>");++i;
            }
            if(row>=0)owf_buf_puts(out,"</w:tr>");
            owf_buf_puts(out,"</w:tbl>");
        }else{put_para(out,doc,story,p,report);++i;}
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
    for (s = 0; s < 3 + doc->nframes; ++s) {
        const owf_story *st = s == 0 ? &doc->body : s == 1 ? &doc->header : s == 2 ? &doc->footer
                            : &doc->frames[s - 3].story;
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
    int title_page = (doc->header.nparas && !pg->header_on_first) || (doc->footer.nparas && !pg->footer_on_first);

    owf_buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:document " W_NS "><w:body>");
    put_story(out, doc, &doc->body, report);
    owf_buf_puts(out, "<w:sectPr>");
    /* With a title page, Word shows the "first" header there: the same part
     * when it is wanted on the first page, nothing when it is not. */
    if (doc->header.nparas) {
        owf_buf_puts(out, "<w:headerReference w:type=\"default\" r:id=\"rId2\"/>");
        if (title_page && pg->header_on_first)
            owf_buf_puts(out, "<w:headerReference w:type=\"first\" r:id=\"rId2\"/>");
    }
    if (doc->footer.nparas) {
        owf_buf_puts(out, "<w:footerReference w:type=\"default\" r:id=\"rId3\"/>");
        if (title_page && pg->footer_on_first)
            owf_buf_puts(out, "<w:footerReference w:type=\"first\" r:id=\"rId3\"/>");
    }
    owf_buf_printf(out, "<w:pgSz w:w=\"%d\" w:h=\"%d\"/>", pg->width, pg->height);
    owf_buf_printf(out, "<w:pgMar w:top=\"%d\" w:right=\"%d\" w:bottom=\"%d\" w:left=\"%d\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/>",
                   pg->margin_top, pg->margin_right, pg->margin_bottom, pg->margin_left);
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

    /* 1.3: a header's and a footer's pictures: their parts' own relationships, the same ids as the body's */
    {
        int k;
        for (k = 0; k < 2; ++k) {
            if (!(k ? footer : header) || !doc->nimages) continue;
            put_image_rels(&part, doc);
            if (result == OWF_OK)
                result = add_part(zip, &part, k ? "word/_rels/footer1.xml.rels" : "word/_rels/header1.xml.rels");
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

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">");
    if (doc->title) {
        owf_buf_puts(&part, "<dc:title>");
        owf_buf_puts_xml(&part, doc->title);
        owf_buf_puts(&part, "</dc:title>");
    }
    owf_buf_puts(&part, "</cp:coreProperties>\n");
    if (result == OWF_OK)
        result = add_part(zip, &part, "docProps/core.xml");

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                        "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                        "<Application>OpenWrite libowf " OWF_VERSION "</Application></Properties>\n");
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

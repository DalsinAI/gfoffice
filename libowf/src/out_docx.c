/*
 * libowf: DOCX out (Office Open XML, ECMA-376 transitional): the content
 * types, the relationships, word/document.xml, word/styles.xml, a header
 * and a footer part, and the document's properties. Formatting is written
 * on each paragraph and run, as Word itself does for direct formatting.
 * Elements inside w:pPr and w:rPr follow the schema's order, which Word
 * insists on.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <string.h>

#define W_NS "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" " \
             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" " \
             "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" " \
             "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" " \
             "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\""

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
            owf_buf_printf(out, "<w:tab w:val=\"%s\" w:pos=\"%d\"/>", tab_val[f->tabs[i].kind],
                           f->tabs[i].position + f->indent_left);
        owf_buf_puts(out, "</w:tabs>");
    }
    if (spacing) {
        owf_buf_puts(out, "<w:spacing");
        if (f->space_before)
            owf_buf_printf(out, " w:before=\"%d\"", f->space_before);
        if (f->space_after)
            owf_buf_printf(out, " w:after=\"%d\"", f->space_after);
        if (f->line_spacing && f->line_spacing != 100)
            owf_buf_printf(out, " w:line=\"%d\" w:lineRule=\"auto\"", f->line_spacing * 240 / 100);
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
                     const owf_para *para, owf_report *report)
{
    static const char *field_instr[] = { " PAGE ", " NUMPAGES ", " DATE ", " TIME " };
    static const char *field_shown[] = { "1", "1", "", "" };
    char style[16];
    const char *pstyle = NULL;
    int j;
    if (para->fmt.heading >= 1 && para->fmt.heading <= 6) {
        sprintf(style, "Heading%d", para->fmt.heading); pstyle = style;
    }
    owf_buf_puts(out, "<w:p>"); put_ppr(out, &para->fmt, pstyle);
    for (j = 0; j < para->nruns; ++j) {
        const owf_run *run=&para->runs[j];
        if(run->kind==OWF_RUN_FIELD){owf_buf_printf(out,"<w:fldSimple w:instr=\"%s\"><w:r>",field_instr[run->field]);put_rpr(out,doc,&run->fmt,report);owf_buf_printf(out,"<w:t>%s</w:t></w:r></w:fldSimple>",field_shown[run->field]);continue;}
        if(run->kind==OWF_RUN_TEXT&&run->href&&story==&doc->body)owf_buf_printf(out,"<w:hyperlink r:id=\"rId%d\">",body_link_id(doc,run->href));
        owf_buf_puts(out,"<w:r>");put_rpr(out,doc,&run->fmt,report);
        switch(run->kind){case OWF_RUN_TEXT:owf_buf_puts(out,"<w:t xml:space=\"preserve\">");owf_buf_puts_xml(out,run->text);owf_buf_puts(out,"</w:t>");break;case OWF_RUN_TAB:owf_buf_puts(out,"<w:tab/>");break;case OWF_RUN_LINEBREAK:owf_buf_puts(out,"<w:br/>");break;case OWF_RUN_IMAGE:put_drawing(out,doc,run->image);break;default:break;}
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

static int add_part(owf_zip *zip, owf_buf *part, const char *name)
{
    int result = part->failed ? OWF_ERR_MEMORY : owf_zip_add(zip, name, part->data, part->length, 1);
    part->length = 0;
    return result;
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
                      "<w:style w:type=\"paragraph\" w:styleId=\"Footer\"><w:name w:val=\"footer\"/><w:basedOn w:val=\"Normal\"/></w:style>"
                      "</w:styles>\n");
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
            if(run->kind!=OWF_RUN_TEXT||!run->href)continue;
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

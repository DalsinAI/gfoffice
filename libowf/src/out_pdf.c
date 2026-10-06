/*
 * libowf: lightweight PDF export for OpenWrite.
 * Base-14 fonts keep output searchable and the 68000/68020 path FPU-free.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PDF_MAX_LINE_PIECES 192

typedef struct {
    const char *text;
    size_t len;
    owf_charfmt fmt;
    int font_id;
    int size;
    int width;
} pdf_piece;

typedef struct {
    pdf_piece p[PDF_MAX_LINE_PIECES];
    int n, width, max_size;
} pdf_line;

typedef struct {
    owf_buf *pages;
    int npages, cappages, current;
    int page_w, page_h, left, right, top, bottom;
    int y;
    const owf_doc *doc;
    owf_report *report;
} pdf_layout;

static int pdf_points(int twips)
{
    if (twips >= 0) return (twips + 10) / 20;
    return -((-twips + 10) / 20);
}

static int pdf_heading_size(const owf_doc *doc, const owf_para *p, const owf_charfmt *f)
{
    static const int h[] = { 0, 18, 16, 14, 13, 12, 12 };
    int size = f->size ? pdf_points(f->size) : pdf_points(doc->base.size ? doc->base.size : 240);
    if (!f->size && p->fmt.heading >= 1 && p->fmt.heading <= 6 && size < h[p->fmt.heading])
        size = h[p->fmt.heading];
    if (size < 6) size = 6;
    if (size > 72) size = 72;
    return size;
}

static owf_font_kind pdf_family(const owf_doc *doc, const owf_charfmt *f)
{
    int ix = f->font >= 0 ? f->font : doc->base.font;
    if (ix >= 0 && ix < doc->nfonts) return doc->fonts[ix].kind;
    return OWF_FONT_SERIF;
}

/* F1-F4 Helvetica, F5-F8 Times, F9-F12 Courier; normal,bold,italic,bolditalic. */
static int pdf_font_id(const owf_doc *doc, const owf_charfmt *f)
{
    int base, variant = 0;
    owf_font_kind k = pdf_family(doc, f);
    if (k == OWF_FONT_MONO) base = 9;
    else if (k == OWF_FONT_SANS) base = 1;
    else base = 5;
    if (f->flags & OWF_BOLD) variant += 1;
    if (f->flags & OWF_ITALIC) variant += 2;
    return base + variant;
}

static int pdf_char_count(const char *s, size_t len)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + len;
    int n = 0;
    while (p < end) { (void)owf_utf8_next(&p, end); ++n; }
    return n;
}

static int pdf_piece_width(const owf_doc *doc, const char *s, size_t len,
                           const owf_charfmt *f, int size)
{
    int chars = pdf_char_count(s, len);
    owf_font_kind k = pdf_family(doc, f);
    int tenths = k == OWF_FONT_MONO ? 6 : k == OWF_FONT_SANS ? 5 : 5;
    return (chars * size * tenths + 5) / 10;
}

static void pdf_put_encoded(owf_buf *out, const char *s, size_t len)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + len;
    owf_buf_putc(out, '(');
    while (p < end) {
        unsigned long c = owf_utf8_next(&p, end);
        unsigned v = '?';
        if (c >= 32 && c <= 126) v = (unsigned)c;
        else if (c == 0x2022) v = 0x95;
        else if (c == 0x2018 || c == 0x2019) v = c == 0x2018 ? 0x91 : 0x92;
        else if (c == 0x201c || c == 0x201d) v = c == 0x201c ? 0x93 : 0x94;
        else if (c == 0x2013) v = 0x96;
        else if (c == 0x2014) v = 0x97;
        else if (c >= 160 && c <= 255) v = (unsigned)c;
        if (v == '(' || v == ')' || v == '\\') {
            owf_buf_putc(out, '\\'); owf_buf_putc(out, (int)v);
        } else if (v < 32 || v >= 127) {
            owf_buf_printf(out, "\\%03o", v & 255);
        } else owf_buf_putc(out, (int)v);
    }
    owf_buf_putc(out, ')');
}

static void pdf_colour(owf_buf *out, unsigned long colour)
{
    unsigned r, g, b;
    if (colour == OWF_COLOUR_AUTO) { owf_buf_puts(out, "0 g\n"); return; }
    r = (unsigned)((colour >> 16) & 255); g = (unsigned)((colour >> 8) & 255); b = (unsigned)(colour & 255);
    owf_buf_printf(out, "%u.%03u %u.%03u %u.%03u rg\n", r/255, (r%255)*1000/255,
                   g/255, (g%255)*1000/255, b/255, (b%255)*1000/255);
}

static int pdf_new_page(pdf_layout *l)
{
    owf_buf *grown;
    int want;
    if (l->npages == l->cappages) {
        want = l->cappages ? l->cappages * 2 : 8;
        grown = (owf_buf *)realloc(l->pages, (size_t)want * sizeof(*grown));
        if (!grown) return 0;
        l->pages = grown;
        l->cappages = want;
    }
    owf_buf_init(&l->pages[l->npages]);
    l->current = l->npages++;
    l->y = l->page_h - l->top;
    return 1;
}

static void pdf_story_line(owf_buf *out, const owf_doc *doc, const owf_story *story,
                           int x, int y, int page_num, int total_pages)
{
    int i, j, cx = x;
    if (!story || !story->nparas) return;
    for (i = 0; i < story->nparas && i < 1; ++i) {
        const owf_para *p = &story->paras[i];
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            char field[32]; const char *text = NULL; size_t n = 0;
            int size = r->fmt.size ? pdf_points(r->fmt.size) : 9;
            int fid = pdf_font_id(doc, &r->fmt);
            if (r->kind == OWF_RUN_TEXT && r->text) { text = r->text; n = strlen(text); }
            else if (r->kind == OWF_RUN_FIELD) {
                if (r->field == OWF_FIELD_PAGE) snprintf(field,sizeof field,"%d",page_num);
                else if (r->field == OWF_FIELD_PAGES) snprintf(field,sizeof field,"%d",total_pages);
                else snprintf(field,sizeof field,"%s",r->field==OWF_FIELD_DATE?"date":"time");
                text=field;n=strlen(field);
            }
            if (!text || !n) continue;
            pdf_colour(out,r->fmt.colour);
            owf_buf_printf(out,"BT /F%d %d Tf %d %d Td ",fid,size,cx,y);
            pdf_put_encoded(out,text,n);owf_buf_puts(out," Tj ET\n");
            cx += pdf_piece_width(doc,text,n,&r->fmt,size);
        }
    }
}

static void pdf_add_headers_footers(pdf_layout *l)
{
    int i;
    for (i = 0; i < l->npages; ++i) {
        owf_buf *out = &l->pages[i];
        int shown = l->doc->page.start_page + i;
        if (shown < 1) shown = i + 1;
        if (l->doc->header.nparas && (i || l->doc->page.header_on_first))
            pdf_story_line(out,l->doc,&l->doc->header,l->left,l->page_h-(l->top/2),shown,l->npages);
        if (l->doc->footer.nparas && (i || l->doc->page.footer_on_first))
            pdf_story_line(out,l->doc,&l->doc->footer,l->left,l->bottom/2,shown,l->npages);
    }
}

static void pdf_line_reset(pdf_line *line)
{
    memset(line, 0, sizeof(*line));
}

static int pdf_line_add(pdf_line *line, const char *text, size_t len,
                        const owf_charfmt *fmt, int font_id, int size, int width)
{
    pdf_piece *p;
    if (!len) return 1;
    if (line->n >= PDF_MAX_LINE_PIECES) return 0;
    p = &line->p[line->n++];
    p->text = text; p->len = len; p->fmt = *fmt; p->font_id = font_id; p->size = size; p->width = width;
    line->width += width;
    if (size > line->max_size) line->max_size = size;
    return 1;
}

static int pdf_flush_line(pdf_layout *l, const owf_para *para, pdf_line *line,
                          int first_line)
{
    owf_buf *out; int i, x, avail, line_height, extra = 0, spaces = 0;
    if (!line->n) line->max_size = line->max_size ? line->max_size : 12;
    line_height = (line->max_size * 6 + 4) / 5;
    if (para->fmt.line_spacing > 0) line_height = line_height * para->fmt.line_spacing / 100;
    if (line_height < line->max_size + 1) line_height = line->max_size + 1;
    if (l->y - line_height < l->bottom) {
        if (!pdf_new_page(l)) return 0;
    }
    out = &l->pages[l->current];
    x = l->left + pdf_points(para->fmt.indent_left) + (first_line ? pdf_points(para->fmt.indent_first) : 0);
    avail = l->page_w - l->right - pdf_points(para->fmt.indent_right) - x;
    if (para->fmt.align == OWF_ALIGN_CENTRE && line->width < avail) x += (avail-line->width)/2;
    else if (para->fmt.align == OWF_ALIGN_RIGHT && line->width < avail) x += avail-line->width;
    else if (para->fmt.align == OWF_ALIGN_JUSTIFY && line->width < avail) {
        for (i=0;i<line->n;++i) if (line->p[i].len==1 && line->p[i].text[0]==' ') ++spaces;
        if (spaces) extra=(avail-line->width)/spaces;
    }
    for (i=0;i<line->n;++i) {
        pdf_piece *p=&line->p[i]; int baseline=l->y-line->max_size;
        pdf_colour(out,p->fmt.colour);
        owf_buf_printf(out,"BT /F%d %d Tf %d %d Td ",p->font_id,p->size,x,baseline);
        pdf_put_encoded(out,p->text,p->len);owf_buf_puts(out," Tj ET\n");
        if (p->fmt.flags & (OWF_UNDERLINE|OWF_STRIKE)) {
            int uy=baseline-(p->fmt.flags&OWF_UNDERLINE?2:-(p->size/3));
            owf_buf_printf(out,"0 G 0.5 w %d %d m %d %d l S\n",x,uy,x+p->width,uy);
        }
        x += p->width;
        if (extra && p->len==1 && p->text[0]==' ') x += extra;
    }
    l->y -= line_height;
    pdf_line_reset(line);
    return 1;
}

static int pdf_layout_text_run(pdf_layout *l, const owf_para *para, const owf_run *run,
                               pdf_line *line, int *first_line)
{
    const char *s=run->text?run->text:""; size_t len=strlen(s), at=0;
    int fid=pdf_font_id(l->doc,&run->fmt), size=pdf_heading_size(l->doc,para,&run->fmt);
    while (at < len) {
        size_t start=at,end; int is_space=0,width,avail,xbase;
        if (s[at]=='\n') { if(!pdf_flush_line(l,para,line,*first_line))return 0;*first_line=0;++at;continue; }
        if (s[at]==' ' || s[at]=='\t') { start=at; while(at<len&&(s[at]==' '||s[at]=='\t'))++at; end=at; is_space=1; }
        else { start=at; while(at<len&&s[at]!=' '&&s[at]!='\t'&&s[at]!='\n')++at; end=at; }
        width=pdf_piece_width(l->doc,s+start,end-start,&run->fmt,size);
        if(is_space && line->n==0)continue;
        xbase=l->left+pdf_points(para->fmt.indent_left)+(*first_line?pdf_points(para->fmt.indent_first):0);
        avail=l->page_w-l->right-pdf_points(para->fmt.indent_right)-xbase;
        if(line->n && line->width+width>avail){if(!pdf_flush_line(l,para,line,*first_line))return 0;*first_line=0;if(is_space)continue;}
        if(!pdf_line_add(line,s+start,end-start,&run->fmt,fid,size,width)){if(!pdf_flush_line(l,para,line,*first_line))return 0;*first_line=0;if(!pdf_line_add(line,s+start,end-start,&run->fmt,fid,size,width))return 0;}
    }
    return 1;
}

static int pdf_layout_document(pdf_layout *l)
{
    int i,j;
    if(!pdf_new_page(l))return 0;
    for(i=0;i<l->doc->body.nparas;++i){
        const owf_para *p=&l->doc->body.paras[i]; pdf_line line; int first=1;
        if(p->fmt.page_break_before && (i || l->y < l->page_h-l->top) && !pdf_new_page(l))return 0;
        l->y-=pdf_points(p->fmt.space_before);
        pdf_line_reset(&line);
        for(j=0;j<p->nruns;++j){
            const owf_run *r=&p->runs[j];
            if(r->kind==OWF_RUN_TEXT){if(!pdf_layout_text_run(l,p,r,&line,&first))return 0;}
            else if(r->kind==OWF_RUN_TAB){owf_run fake=*r;fake.kind=OWF_RUN_TEXT;fake.text=(char *)"    ";if(!pdf_layout_text_run(l,p,&fake,&line,&first))return 0;}
            else if(r->kind==OWF_RUN_LINEBREAK){if(!pdf_flush_line(l,p,&line,first))return 0;first=0;}
            else if(r->kind==OWF_RUN_FIELD){char field[8]="#";owf_run fake=*r;fake.kind=OWF_RUN_TEXT;fake.text=field;if(!pdf_layout_text_run(l,p,&fake,&line,&first))return 0;}
        }
        if(line.n || !p->nruns){if(!pdf_flush_line(l,p,&line,first))return 0;}
        l->y-=pdf_points(p->fmt.space_after);
    }
    pdf_add_headers_footers(l);
    return 1;
}

static const char *pdf_font_name(int id)
{
    static const char *n[]={NULL,
        "Helvetica","Helvetica-Bold","Helvetica-Oblique","Helvetica-BoldOblique",
        "Times-Roman","Times-Bold","Times-Italic","Times-BoldItalic",
        "Courier","Courier-Bold","Courier-Oblique","Courier-BoldOblique"};
    return id>=1&&id<=12?n[id]:"Times-Roman";
}

static int export_pdf(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    pdf_layout l; owf_buf out; unsigned long *offs=NULL,xref; int i,obj_count,result=OWF_OK;
    memset(&l,0,sizeof l);l.doc=doc;l.report=report;
    l.page_w=pdf_points(doc->page.width);l.page_h=pdf_points(doc->page.height);
    l.left=pdf_points(doc->page.margin_left);l.right=pdf_points(doc->page.margin_right);
    l.top=pdf_points(doc->page.margin_top);l.bottom=pdf_points(doc->page.margin_bottom);
    if(l.page_w<72||l.page_h<72)return OWF_ERR_FORMAT;
    if(!pdf_layout_document(&l)){result=OWF_ERR_MEMORY;goto done;}
    obj_count=14+l.npages*2;
    offs=(unsigned long *)calloc((size_t)obj_count+1,sizeof(*offs));if(!offs){result=OWF_ERR_MEMORY;goto done;}
    owf_buf_init(&out);owf_buf_puts(&out,"%PDF-1.4\n% OpenWrite PDF\n");
#define OBJ(N) do{offs[(N)]=(unsigned long)out.length;owf_buf_printf(&out,"%d 0 obj\n",(N));}while(0)
    OBJ(1);owf_buf_puts(&out,"<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    OBJ(2);owf_buf_printf(&out,"<< /Type /Pages /Count %d /Kids [",l.npages);for(i=0;i<l.npages;++i)owf_buf_printf(&out," %d 0 R",16+i*2);owf_buf_puts(&out," ] >>\nendobj\n");
    for(i=1;i<=12;++i){OBJ(2+i);owf_buf_printf(&out,"<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>\nendobj\n",pdf_font_name(i));}
    for(i=0;i<l.npages;++i){int co=15+i*2,po=co+1,j;OBJ(co);owf_buf_printf(&out,"<< /Length %lu >>\nstream\n",(unsigned long)l.pages[i].length);owf_buf_put(&out,l.pages[i].data,l.pages[i].length);owf_buf_puts(&out,"endstream\nendobj\n");OBJ(po);owf_buf_printf(&out,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << /Font <<",l.page_w,l.page_h);for(j=1;j<=12;++j)owf_buf_printf(&out," /F%d %d 0 R",j,2+j);owf_buf_printf(&out," >> >> /Contents %d 0 R >>\nendobj\n",co);}
    xref=(unsigned long)out.length;owf_buf_printf(&out,"xref\n0 %d\n0000000000 65535 f \n",obj_count+1);for(i=1;i<=obj_count;++i)owf_buf_printf(&out,"%010lu 00000 n \n",offs[i]);owf_buf_printf(&out,"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n",obj_count+1,xref);
#undef OBJ
    if(out.failed){owf_buf_free(&out);result=OWF_ERR_MEMORY;goto done;}
    result=owf_buf_take(&out,data,length);
done:
    free(offs);for(i=0;i<l.npages;++i)owf_buf_free(&l.pages[i]);free(l.pages);return result;
}

const owf_format owf_format_pdf = {
    "pdf", "Portable Document Format", "pdf", NULL, NULL, export_pdf
};

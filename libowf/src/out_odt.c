/*
 * libowf: ODT out (OpenDocument Text 1.3, ISO/IEC 26300): a zip with
 * mimetype first and stored, the manifest, meta.xml, styles.xml (named
 * styles, the page, headers and footers) and content.xml (the body).
 * Formatting becomes automatic styles: P1, P2... for paragraphs and T1,
 * T2... for text, one per distinct format.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define NS "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" " \
           "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" " \
           "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" " \
           "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" " \
           "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" " \
           "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" " \
           "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" " \
           "xmlns:xlink=\"http://www.w3.org/1999/xlink\" " \
           "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" " \
           "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" " \
           "office:version=\"1.3\""

/* The distinct formats of one XML file's stories. */
typedef struct {
    const owf_parafmt **paras;
    int nparas, cappara;
    const owf_charfmt **chars;
    int nchars, capchar;
    int failed;
} styles;

static int same_para(const owf_parafmt *a, const owf_parafmt *b)
{
    int i;
    if (a->heading != b->heading || a->align != b->align || a->indent_left != b->indent_left ||
        a->indent_right != b->indent_right || a->indent_first != b->indent_first ||
        a->space_before != b->space_before || a->space_after != b->space_after ||
        a->line_spacing != b->line_spacing || a->page_break_before != b->page_break_before ||
        a->ntabs != b->ntabs || a->shading != b->shading || a->borders != b->borders ||
        a->border_colour != b->border_colour)
        return 0;
    for (i = 0; i < a->ntabs; i++)
        if (a->tabs[i].position != b->tabs[i].position || a->tabs[i].kind != b->tabs[i].kind ||
            a->tabs[i].leader != b->tabs[i].leader)
            return 0;
    return 1;
}

static int plain_char(const owf_charfmt *f)
{
    return !f->flags && f->font < 0 && !f->size && f->colour == OWF_COLOUR_AUTO;
}

static int same_char(const owf_charfmt *a, const owf_charfmt *b)
{
    return a->flags == b->flags && a->font == b->font && a->size == b->size && a->colour == b->colour;
}

/* The style number (1, 2...) of a paragraph format, added if new. */
static int para_style(styles *s, const owf_parafmt *f)
{
    int i;
    for (i = 0; i < s->nparas; i++)
        if (same_para(s->paras[i], f))
            return i + 1;
    if (s->nparas == s->cappara) {
        int want = s->cappara ? s->cappara * 2 : 16;
        const owf_parafmt **p = realloc((void *)s->paras, (size_t)want * sizeof *p);
        if (!p) {
            s->failed = 1;
            return 1;
        }
        s->paras = p;
        s->cappara = want;
    }
    s->paras[s->nparas++] = f;
    return s->nparas;
}

/* The style number of a text format, 0 for plain text. */
static int char_style(styles *s, const owf_charfmt *f)
{
    int i;
    if (plain_char(f))
        return 0;
    for (i = 0; i < s->nchars; i++)
        if (same_char(s->chars[i], f))
            return i + 1;
    if (s->nchars == s->capchar) {
        int want = s->capchar ? s->capchar * 2 : 16;
        const owf_charfmt **c = realloc((void *)s->chars, (size_t)want * sizeof *c);
        if (!c) {
            s->failed = 1;
            return 0;
        }
        s->chars = c;
        s->capchar = want;
    }
    s->chars[s->nchars++] = f;
    return s->nchars;
}

static void collect(styles *s, const owf_story *story)
{
    int i, j;
    for (i = 0; i < story->nparas; i++) {
        para_style(s, &story->paras[i].fmt);
        for (j = 0; j < story->paras[i].nruns; j++)
            char_style(s, &story->paras[i].runs[j].fmt);
    }
}

static void free_styles(styles *s)
{
    free((void *)s->paras);
    free((void *)s->chars);
}

/* The name a font has in the file: today's equivalent of an Amiga font. */
static const char *face_name(const owf_doc *doc, int font, owf_report *report)
{
    const char *modern = owf_font_modern(doc->fonts[font].name, NULL);
    if (modern) {
        owf_report_add(report, OWF_NOTE_APPROX, "The font %s is saved as %s", doc->fonts[font].name, modern);
        return modern;
    }
    return doc->fonts[font].name;
}

static void put_font_faces(owf_buf *out, const owf_doc *doc, owf_report *report)
{
    int i, j;
    static const char *generic[] = { "roman", "roman", "swiss", "modern" };

    owf_buf_puts(out, "<office:font-face-decls>");
    owf_buf_puts(out, "<style:font-face style:name=\"Liberation Serif\" svg:font-family=\"'Liberation Serif'\" "
                      "style:font-family-generic=\"roman\" style:font-pitch=\"variable\"/>");
    for (i = 0; i < doc->nfonts; i++) {
        const char *name = face_name(doc, i, report);
        int seen = strcmp(name, "Liberation Serif") == 0;
        for (j = 0; j < i && !seen; j++)
            seen = strcmp(face_name(doc, j, NULL), name) == 0;
        if (seen)
            continue;
        owf_buf_puts(out, "<style:font-face style:name=\"");
        owf_buf_puts_xml(out, name);
        owf_buf_puts(out, "\" svg:font-family=\"'");
        owf_buf_puts_xml(out, name);
        owf_buf_printf(out, "'\" style:font-family-generic=\"%s\" style:font-pitch=\"%s\"/>",
                       generic[doc->fonts[i].kind], doc->fonts[i].kind == OWF_FONT_MONO ? "fixed" : "variable");
    }
    owf_buf_puts(out, "</office:font-face-decls>");
}

static void put_length_attr(owf_buf *out, const char *name, int twips)
{
    owf_buf_printf(out, " %s=\"", name);
    owf_put_points(out, twips);
    owf_buf_puts(out, "\"");
}

static void put_text_props(owf_buf *out, const owf_doc *doc, const owf_charfmt *f)
{
    owf_buf_puts(out, "<style:text-properties");
    if (f->font >= 0 && f->font < doc->nfonts) {
        owf_buf_puts(out, " style:font-name=\"");
        owf_buf_puts_xml(out, face_name(doc, f->font, NULL));
        owf_buf_puts(out, "\"");
    }
    if (f->size)
        put_length_attr(out, "fo:font-size", f->size);
    if (f->flags & OWF_BOLD)
        owf_buf_puts(out, " fo:font-weight=\"bold\"");
    if (f->flags & OWF_ITALIC)
        owf_buf_puts(out, " fo:font-style=\"italic\"");
    if (f->flags & OWF_UNDERLINE)
        owf_buf_puts(out, " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\""
                          " style:text-underline-color=\"font-color\"");
    if (f->flags & OWF_STRIKE)
        owf_buf_puts(out, " style:text-line-through-style=\"solid\"");
    if (f->flags & OWF_SUPER)
        owf_buf_puts(out, " style:text-position=\"super 58%\"");
    else if (f->flags & OWF_SUB)
        owf_buf_puts(out, " style:text-position=\"sub 58%\"");
    if (f->colour != OWF_COLOUR_AUTO)
        owf_buf_printf(out, " fo:color=\"#%06lx\"", f->colour & 0xFFFFFFUL);
    owf_buf_puts(out, "/>");
}

void owf_odf_put_leader(owf_buf *out, owf_tab_leader leader)
{
    switch (leader) {
    case OWF_LEADER_DOT: owf_buf_puts(out, " style:leader-style=\"dotted\" style:leader-text=\".\""); break;
    case OWF_LEADER_HYPHEN: owf_buf_puts(out, " style:leader-style=\"solid\" style:leader-text=\"-\""); break;
    case OWF_LEADER_UNDERSCORE:
    case OWF_LEADER_HEAVY: owf_buf_puts(out, " style:leader-style=\"solid\" style:leader-text=\"_\""); break;
    case OWF_LEADER_MIDDLE_DOT: owf_buf_puts(out, " style:leader-style=\"dotted\" style:leader-text=\"\xc2\xb7\""); break;
    default: break;
    }
}

static void put_para_props(owf_buf *out, const owf_parafmt *f)
{
    static const char *align[] = { "start", "center", "end", "justify" };
    static const char *tab_type[] = { "left", "center", "right", "char" };
    int i;

    owf_buf_printf(out, "<style:paragraph-properties fo:text-align=\"%s\"", align[f->align]);
    if (f->indent_left)
        put_length_attr(out, "fo:margin-left", f->indent_left);
    if (f->indent_right)
        put_length_attr(out, "fo:margin-right", f->indent_right);
    if (f->indent_first)
        put_length_attr(out, "fo:text-indent", f->indent_first);
    if (f->space_before)
        put_length_attr(out, "fo:margin-top", f->space_before);
    if (f->space_after)
        put_length_attr(out, "fo:margin-bottom", f->space_after);
    if (f->line_spacing && f->line_spacing != 100)
        owf_buf_printf(out, " fo:line-height=\"%d%%\"", f->line_spacing);
    if (f->page_break_before)
        owf_buf_puts(out, " fo:break-before=\"page\"");
    if (OWF_SHADE_SET(f->shading))
        owf_buf_printf(out, " fo:background-color=\"#%06lx\"", OWF_SHADE_RGB(f->shading));
    if (f->borders) {
        static const char *side[] = { "top", "left", "bottom", "right" };
        unsigned long col = OWF_SHADE_SET(f->border_colour) ? OWF_SHADE_RGB(f->border_colour) : 0;
        int k;
        for (k = 0; k < 4; k++)
            if (f->borders & (1 << k))
                owf_buf_printf(out, " fo:border-%s=\"0.5pt solid #%06lx\" fo:padding-%s=\"0.1cm\"", side[k], col, side[k]);
    }
    if (!f->ntabs) {
        owf_buf_puts(out, "/>");
        return;
    }
    owf_buf_puts(out, "><style:tab-stops>");
    for (i = 0; i < f->ntabs; i++) {
        owf_buf_puts(out, "<style:tab-stop");
        put_length_attr(out, "style:position", f->tabs[i].position);
        owf_buf_printf(out, " style:type=\"%s\"", tab_type[f->tabs[i].kind]);
        if (f->tabs[i].kind == OWF_TAB_DECIMAL)
            owf_buf_puts(out, " style:char=\".\"");
        owf_odf_put_leader(out, f->tabs[i].leader);
        owf_buf_puts(out, "/>");
    }
    owf_buf_puts(out, "</style:tab-stops></style:paragraph-properties>");
}

static void put_frame_styles(owf_buf *out, const owf_doc *doc);

static void put_automatic_styles(owf_buf *out, const owf_doc *doc, const styles *s)
{
    int i;
    /* a table-cell style for each cell colour the body uses (Cell<RRGGBB>) */
    for (i = 0; i < doc->body.nparas; i++) {
        unsigned long c = doc->body.paras[i].cell_shading;
        int j, seen = 0;
        if (doc->body.paras[i].table_id < 0 || !OWF_SHADE_SET(c)) continue;
        for (j = 0; j < i && !seen; j++) seen = doc->body.paras[j].table_id >= 0 && doc->body.paras[j].cell_shading == c;
        if (!seen)
            owf_buf_printf(out, "<style:style style:name=\"Cell%06lX\" style:family=\"table-cell\">"
                                "<style:table-cell-properties fo:background-color=\"#%06lx\"/></style:style>",
                           OWF_SHADE_RGB(c), OWF_SHADE_RGB(c));
    }
    for (i = 0; i < s->nparas; i++) {
        const owf_parafmt *f = s->paras[i];
        owf_buf_printf(out, "<style:style style:name=\"P%d\" style:family=\"paragraph\" style:parent-style-name=\"", i + 1);
        if (f->heading >= 1 && f->heading <= 6)
            owf_buf_printf(out, "Heading_20_%d", f->heading);
        else
            owf_buf_puts(out, "Standard");
        owf_buf_puts(out, "\">");
        put_para_props(out, f);
        owf_buf_puts(out, "</style:style>");
    }
    put_frame_styles(out, doc);
    for (i = 0; i < s->nchars; i++) {
        owf_buf_printf(out, "<style:style style:name=\"T%d\" style:family=\"text\">", i + 1);
        put_text_props(out, doc, s->chars[i]);
        owf_buf_puts(out, "</style:style>");
    }
}

/* Text: ODF collapses spaces, so the second and later of a row, and one at
 * the start, are written as <text:s/>. */
static void put_text(owf_buf *out, const char *text, int *after_space)
{
    const char *p = text, *start = text;
    while (*p) {
        if (*p == ' ') {
            int n = 0;
            owf_buf_put_xml(out, start, (size_t)(p - start));
            if (!*after_space) {
                owf_buf_putc(out, ' ');
                p++;
            }
            while (p[n] == ' ')
                n++;
            if (n)
                owf_buf_printf(out, n > 1 ? "<text:s text:c=\"%d\"/>" : "<text:s/>", n);
            p += n;
            start = p;
            *after_space = 1;
        } else {
            p++;
            *after_space = 0;
        }
    }
    owf_buf_put_xml(out, start, (size_t)(p - start));
}

static const char *odt_image_ext(const owf_image *im)
{
    if(im&&im->mime){if(!strcmp(im->mime,"image/png"))return "png";if(!strcmp(im->mime,"image/jpeg"))return "jpg";if(!strcmp(im->mime,"image/gif"))return "gif";if(!strcmp(im->mime,"image/webp"))return "webp";if(!strcmp(im->mime,"image/iff"))return "iff";}return "bin";
}

static void put_para_odt(owf_buf *out, const owf_doc *doc, const owf_para *para, styles *s);

/* 1.3: where a frame goes across (x) or down (y) in ODF: its position, what it is measured from, and its
 * offset (svg:x, svg:y) when it is at one. A frame from a margin's own area is placed from the page. */
static void odt_frame_place(const owf_doc *doc, const owf_frame *fr, int across, const char **pos, const char **rel,
                            int *at, int *has_at)
{
    static const char *const pos_h[] = { "from-left", "left", "center", "right" };
    static const char *const pos_v[] = { "from-top", "top", "middle", "bottom" };
    unsigned from = across ? fr->x_from : fr->y_from, place = across ? fr->x_place : fr->y_place;
    int size = across ? fr->width : fr->height, start = 0, area;
    int page = across ? doc->page.width : doc->page.height;
    int lead = across ? doc->page.margin_left : doc->page.margin_top;
    int trail = across ? doc->page.margin_right : doc->page.margin_bottom;
    if (place > OWF_PLACE_END) place = OWF_PLACE_AT;
    *pos = across ? pos_h[place] : pos_v[place];
    *at = across ? fr->x : fr->y;
    *has_at = place == OWF_PLACE_AT;
    switch (from) {
    case OWF_FROM_PAGE: *rel = "page"; return;
    case OWF_FROM_MARGIN: *rel = "page-content"; return;
    case OWF_FROM_PARAGRAPH: *rel = "paragraph"; return;
    case OWF_FROM_LEFT_MARGIN: case OWF_FROM_TOP_MARGIN: area = lead; break;
    default: start = page - trail; area = trail; break;
    }
    *rel = "page";
    *pos = across ? pos_h[0] : pos_v[0];
    *has_at = 1;
    *at = place == OWF_PLACE_START ? start : place == OWF_PLACE_CENTRE ? start + (area - size) / 2
        : place == OWF_PLACE_END ? start + area - size : start + *at;
}

/* 1.3: each frame's graphic style (Fr1, Fr2...): where it goes, under or over the text, its fill, line and
 * insets. */
static void put_frame_styles(owf_buf *out, const owf_doc *doc)
{
    int i;
    for (i = 0; i < doc->nframes; i++) {
        const owf_frame *fr = &doc->frames[i];
        const char *pos, *rel;
        int at, has_at;
        static const char *const wrap[] = { "run-through", "parallel", "parallel", "none" };
        owf_buf_printf(out, "<style:style style:name=\"Fr%d\" style:family=\"graphic\"><style:graphic-properties "
                            "style:wrap=\"%s\"", i + 1, wrap[fr->wrap <= OWF_WRAP_TOP_BOTTOM ? fr->wrap : 0]);
        if (fr->wrap == OWF_WRAP_NONE)
            owf_buf_printf(out, " style:run-through=\"%s\"", fr->behind ? "background" : "foreground");
        odt_frame_place(doc, fr, 1, &pos, &rel, &at, &has_at);
        owf_buf_printf(out, " style:horizontal-pos=\"%s\" style:horizontal-rel=\"%s\"", pos, rel);
        odt_frame_place(doc, fr, 0, &pos, &rel, &at, &has_at);
        owf_buf_printf(out, " style:vertical-pos=\"%s\" style:vertical-rel=\"%s\"", pos, rel);
        if (OWF_SHADE_SET(fr->fill))
            owf_buf_printf(out, " fo:background-color=\"#%06lx\" draw:fill=\"solid\" draw:fill-color=\"#%06lx\"",
                           OWF_SHADE_RGB(fr->fill), OWF_SHADE_RGB(fr->fill));
        else
            owf_buf_puts(out, " fo:background-color=\"transparent\" draw:fill=\"none\"");
        if (OWF_SHADE_SET(fr->line))
            owf_buf_printf(out, " fo:border=\"0.75pt solid #%06lx\" draw:stroke=\"solid\" svg:stroke-color=\"#%06lx\"",
                           OWF_SHADE_RGB(fr->line), OWF_SHADE_RGB(fr->line));
        else
            owf_buf_puts(out, " fo:border=\"none\" draw:stroke=\"none\"");
        if (fr->story.nparas) {
            put_length_attr(out, "fo:padding-top", fr->inset[0]);
            put_length_attr(out, "fo:padding-right", fr->inset[1]);
            put_length_attr(out, "fo:padding-bottom", fr->inset[2]);
            put_length_attr(out, "fo:padding-left", fr->inset[3]);
        }
        owf_buf_puts(out, "/></style:style>");
    }
}

static void put_para_odt(owf_buf *out, const owf_doc *doc, const owf_para *para, styles *s);

/* 1.3: a frame (a picture or a text box placed on the page) anchored to its paragraph, placed by its graphic
 * style (Fr<n>): a picture, or a text box with its paragraphs. A text box's own frames are left out (the
 * body's paragraphs only write them). */
static void put_frame_odt(owf_buf *out, const owf_doc *doc, int index, styles *s)
{
    const owf_frame *fr;
    const char *pos, *rel;
    int k, at, has_at;
    if (!doc || index < 0 || index >= doc->nframes) return;
    fr = &doc->frames[index];
    /* 2.0.17: a text box in the line goes as a character (as-char), with no place of its own */
    owf_buf_printf(out, "<draw:frame draw:style-name=\"Fr%d\" draw:name=\"Frame%d\" text:anchor-type=\"%s\"",
                   index + 1, index + 1, fr->in_line ? "as-char" : "paragraph");
    if (!fr->in_line) {
        odt_frame_place(doc, fr, 1, &pos, &rel, &at, &has_at);
        if (has_at) put_length_attr(out, "svg:x", at);
        odt_frame_place(doc, fr, 0, &pos, &rel, &at, &has_at);
        if (has_at) put_length_attr(out, "svg:y", at);
    }
    put_length_attr(out, "svg:width", fr->width);
    put_length_attr(out, "svg:height", fr->height);
    owf_buf_printf(out, " draw:z-index=\"%d\">", fr->behind ? 0 : index + 1);
    if (fr->image >= 0 && fr->image < doc->nimages && !fr->story.nparas)
        owf_buf_printf(out, "<draw:image xlink:href=\"Pictures/image%d.%s\" xlink:type=\"simple\" xlink:show=\"embed\" "
                       "xlink:actuate=\"onLoad\"/>", fr->image + 1, odt_image_ext(&doc->images[fr->image]));
    else {
        owf_buf_puts(out, "<draw:text-box>");
        for (k = 0; k < fr->story.nparas; k++)
            put_para_odt(out, doc, &fr->story.paras[k], s);
        owf_buf_puts(out, "</draw:text-box>");
    }
    owf_buf_puts(out, "</draw:frame>");
}

static void put_para_odt(owf_buf *out, const owf_doc *doc, const owf_para *para, styles *s)
{
    static const char *fields[] = {
        "<text:page-number text:select-page=\"current\">1</text:page-number>",
        "<text:page-count>1</text:page-count>", "<text:date/>", "<text:time/>" };
    int heading=para->fmt.heading>=1&&para->fmt.heading<=6,after_space=1,j;
    if(heading)owf_buf_printf(out,"<text:h text:style-name=\"P%d\" text:outline-level=\"%d\">",para_style(s,&para->fmt),para->fmt.heading);
    else owf_buf_printf(out,"<text:p text:style-name=\"P%d\">",para_style(s,&para->fmt));
    if(para->bookmarks){                             /* 1.4: its bookmarks, where links into the document go */
        const char *b=para->bookmarks;
        while(*b){size_t n=strcspn(b," ");owf_buf_puts(out,"<text:bookmark text:name=\"");owf_buf_put_xml(out,b,n);owf_buf_puts(out,"\"/>");b+=n;while(*b==' ')++b;}
    }
    for(j=0;j<para->nruns;++j){const owf_run *run=&para->runs[j];int t=char_style(s,&run->fmt);
        if(run->kind==OWF_RUN_TEXT&&run->href){owf_buf_puts(out,"<text:a xlink:href=\"");owf_buf_puts_xml(out,run->href);owf_buf_puts(out,"\">");}
        if(t)owf_buf_printf(out,"<text:span text:style-name=\"T%d\">",t);
        switch(run->kind){
        case OWF_RUN_TEXT:put_text(out,run->text,&after_space);break;
        case OWF_RUN_TAB:owf_buf_puts(out,"<text:tab/>");after_space=1;break;
        case OWF_RUN_LINEBREAK:owf_buf_puts(out,"<text:line-break/>");after_space=1;break;
        case OWF_RUN_FIELD:owf_buf_puts(out,fields[run->field]);after_space=0;break;
        case OWF_RUN_FRAME:
            if(doc&&para>=doc->body.paras&&para<doc->body.paras+doc->body.nparas){put_frame_odt(out,doc,run->image,s);after_space=0;}
            break;
        case OWF_RUN_IMAGE:
            if(doc&&run->image>=0&&run->image<doc->nimages){const owf_image *im=&doc->images[run->image];owf_buf_puts(out,"<draw:frame text:anchor-type=\"as-char\" svg:width=\"");owf_put_points(out,im->width);owf_buf_puts(out,"\" svg:height=\"");owf_put_points(out,im->height);owf_buf_printf(out,"\"><draw:image xlink:href=\"Pictures/image%d.%s\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame>",run->image+1,odt_image_ext(im));}
            break;
        }
        if(t)owf_buf_puts(out,"</text:span>");
        if(run->kind==OWF_RUN_TEXT&&run->href)owf_buf_puts(out,"</text:a>");
    }
    owf_buf_puts(out,heading?"</text:h>":"</text:p>");
}

static void put_story(owf_buf *out, const owf_doc *doc, const owf_story *story, styles *s)
{
    int i=0;
    while(i<story->nparas){const owf_para *p=&story->paras[i];
        if(p->table_id>=0){int id=p->table_id,row=-1;owf_buf_printf(out,"<table:table table:name=\"Table%d\">",id+1);
            while(i<story->nparas&&story->paras[i].table_id==id){p=&story->paras[i];if(p->table_row!=row){if(row>=0)owf_buf_puts(out,"</table:table-row>");owf_buf_puts(out,"<table:table-row>");row=p->table_row;}if(OWF_SHADE_SET(p->cell_shading))owf_buf_printf(out,"<table:table-cell table:style-name=\"Cell%06lX\" office:value-type=\"string\">",OWF_SHADE_RGB(p->cell_shading));else owf_buf_puts(out,"<table:table-cell office:value-type=\"string\">");put_para_odt(out,doc,p,s);owf_buf_puts(out,"</table:table-cell>");++i;}
            if(row>=0)owf_buf_puts(out,"</table:table-row>");
            owf_buf_puts(out,"</table:table>");
        }else{put_para_odt(out,doc,p,s);++i;}
    }
}

static void put_named_styles(owf_buf *out, const owf_doc *doc)
{
    static const int heading_size[] = { 0, 18, 16, 14, 13, 12, 12 };
    int i;

    owf_buf_puts(out, "<office:styles>");
    owf_buf_puts(out, "<style:default-style style:family=\"paragraph\">");
    {
        owf_charfmt base = doc->base;
        if (base.font < 0 || base.font >= doc->nfonts)
            base.font = -1;
        owf_buf_puts(out, "<style:text-properties");
        if (base.font >= 0) {
            owf_buf_puts(out, " style:font-name=\"");
            owf_buf_puts_xml(out, face_name(doc, base.font, NULL));
            owf_buf_puts(out, "\"");
        } else {
            owf_buf_puts(out, " style:font-name=\"Liberation Serif\"");
        }
        put_length_attr(out, "fo:font-size", base.size ? base.size : 240);
        owf_buf_puts(out, "/>");
    }
    owf_buf_puts(out, "</style:default-style>");
    owf_buf_puts(out, "<style:style style:name=\"Standard\" style:family=\"paragraph\" style:class=\"text\"/>");
    for (i = 1; i <= 6; i++)
        owf_buf_printf(out, "<style:style style:name=\"Heading_20_%d\" style:display-name=\"Heading %d\" "
                            "style:family=\"paragraph\" style:parent-style-name=\"Standard\" "
                            "style:next-style-name=\"Standard\" style:default-outline-level=\"%d\" style:class=\"text\">"
                            "<style:paragraph-properties fo:margin-top=\"12pt\" fo:margin-bottom=\"6pt\" fo:keep-with-next=\"always\"/>"
                            "<style:text-properties fo:font-size=\"%dpt\" fo:font-weight=\"bold\"/></style:style>",
                       i, i, i, heading_size[i]);
    owf_buf_puts(out, "<style:style style:name=\"Header\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:class=\"extra\"/>");
    owf_buf_puts(out, "<style:style style:name=\"Footer\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" style:class=\"extra\"/>");
    owf_buf_puts(out, "</office:styles>");
}

static int build_styles_xml(const owf_doc *doc, owf_buf *out)
{
    static const char *num_format[] = { "1", "I", "i", "A", "a" };
    const owf_page *pg = &doc->page;
    styles s;

    memset(&s, 0, sizeof s);
    collect(&s, &doc->header);
    collect(&s, &doc->footer);

    owf_buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles " NS ">");
    put_font_faces(out, doc, NULL);
    put_named_styles(out, doc);
    owf_buf_puts(out, "<office:automatic-styles>");
    put_automatic_styles(out, doc, &s);
    owf_buf_printf(out, "<style:page-layout style:name=\"pm1\"><style:page-layout-properties style:num-format=\"%s\"",
                   num_format[pg->pagenum_style]);
    put_length_attr(out, "fo:page-width", pg->width);
    put_length_attr(out, "fo:page-height", pg->height);
    put_length_attr(out, "fo:margin-top", pg->margin_top);
    put_length_attr(out, "fo:margin-bottom", pg->margin_bottom);
    put_length_attr(out, "fo:margin-left", pg->margin_left);
    put_length_attr(out, "fo:margin-right", pg->margin_right);
    owf_buf_puts(out, "/><style:header-style><style:header-footer-properties fo:min-height=\"0pt\" fo:margin-bottom=\"6pt\"/></style:header-style>"
                      "<style:footer-style><style:header-footer-properties fo:min-height=\"0pt\" fo:margin-top=\"6pt\"/></style:footer-style>"
                      "</style:page-layout>");
    owf_buf_puts(out, "</office:automatic-styles>");
    owf_buf_puts(out, "<office:master-styles><style:master-page style:name=\"Standard\" style:page-layout-name=\"pm1\">");
    if (doc->header.nparas) {
        owf_buf_puts(out, "<style:header>");
        put_story(out, doc, &doc->header, &s);
        owf_buf_puts(out, "</style:header>");
        if (!pg->header_on_first)
            owf_buf_puts(out, "<style:header-first/>");
    }
    if (doc->footer.nparas) {
        owf_buf_puts(out, "<style:footer>");
        put_story(out, doc, &doc->footer, &s);
        owf_buf_puts(out, "</style:footer>");
        if (!pg->footer_on_first)
            owf_buf_puts(out, "<style:footer-first/>");
    }
    owf_buf_puts(out, "</style:master-page></office:master-styles></office:document-styles>\n");
    free_styles(&s);
    return s.failed ? OWF_ERR_MEMORY : OWF_OK;
}

static int build_content_xml(const owf_doc *doc, owf_buf *out, owf_report *report)
{
    styles s;
    owf_buf body;

    memset(&s, 0, sizeof s);
    owf_buf_init(&body);
    /* The body first, so the styles it uses are known. */
    put_story(&body, doc, &doc->body, &s);
    if (doc->page.start_page != 1)
        owf_report_add(report, OWF_NOTE_APPROX, "Page numbers start at 1");

    owf_buf_puts(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content " NS ">");
    put_font_faces(out, doc, report);
    owf_buf_puts(out, "<office:automatic-styles>");
    put_automatic_styles(out, doc, &s);
    owf_buf_puts(out, "</office:automatic-styles><office:body><office:text>");
    if (body.length)
        owf_buf_put(out, body.data, body.length);
    else
        owf_buf_puts(out, "<text:p/>");
    owf_buf_puts(out, "</office:text></office:body></office:document-content>\n");
    if (body.failed)
        out->failed = 1;
    owf_buf_free(&body);
    free_styles(&s);
    return s.failed ? OWF_ERR_MEMORY : OWF_OK;
}

static int export_odt(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    static const char mimetype[] = "application/vnd.oasis.opendocument.text";
    owf_buf out, part;
    owf_zip *zip;
    int result = OWF_OK;

    owf_buf_init(&out);
    owf_buf_init(&part);
    zip = owf_zip_new(&out);
    if (!zip)
        return OWF_ERR_MEMORY;

    owf_zip_add(zip, "mimetype", mimetype, sizeof mimetype - 1, 0);

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
                        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
                        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
                        "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
                        "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>");
    { int i; for(i=0;i<doc->nimages;++i){const owf_image *im=&doc->images[i];owf_buf_printf(&part,"<manifest:file-entry manifest:full-path=\"Pictures/image%d.%s\" manifest:media-type=\"",i+1,odt_image_ext(im));owf_buf_puts_xml(&part,im->mime?im->mime:"application/octet-stream");owf_buf_puts(&part,"\"/>");} }
    owf_buf_puts(&part, "</manifest:manifest>\n");
    if (part.failed)
        result = OWF_ERR_MEMORY;
    else
        result = owf_zip_add(zip, "META-INF/manifest.xml", part.data, part.length, 1);
    part.length = 0;

    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta " NS "><office:meta>"
                        "<meta:generator>OpenWrite/" OWF_VERSION " libowf</meta:generator>");
    if (doc->title) {
        owf_buf_puts(&part, "<dc:title>");
        owf_buf_puts_xml(&part, doc->title);
        owf_buf_puts(&part, "</dc:title>");
    }
    owf_buf_puts(&part, "</office:meta></office:document-meta>\n");
    if (result == OWF_OK)
        result = part.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, "meta.xml", part.data, part.length, 1);
    part.length = 0;

    if (result == OWF_OK)
        result = build_styles_xml(doc, &part);
    if (result == OWF_OK)
        result = part.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, "styles.xml", part.data, part.length, 1);
    part.length = 0;

    if (result == OWF_OK)
        result = build_content_xml(doc, &part, report);
    if (result == OWF_OK)
        result = part.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, "content.xml", part.data, part.length, 1);
    if(result==OWF_OK){int i;char name[96];for(i=0;i<doc->nimages&&result==OWF_OK;++i){snprintf(name,sizeof name,"Pictures/image%d.%s",i+1,odt_image_ext(&doc->images[i]));result=owf_zip_add(zip,name,doc->images[i].data,doc->images[i].length,1);}}

    owf_buf_free(&part);
    if (owf_zip_finish(zip) != OWF_OK && result == OWF_OK)
        result = OWF_ERR_MEMORY;
    if (result != OWF_OK) {
        owf_buf_free(&out);
        return result;
    }
    return owf_buf_take(&out, data, length);
}

const owf_format owf_format_odt = {
    "odt", "OpenDocument Text", "odt ott", owf_detect_odt, owf_import_odt, export_odt
};

/*
 * libowf: ODT in (OpenDocument Text, ISO/IEC 26300), zipped (.odt, .ott)
 * or as one flat XML file (.fodt).
 *
 * Styles are resolved through their parents and the default style;
 * automatic styles are looked up before common ones, in the file the text
 * is in (content.xml for the body, styles.xml for headers and footers).
 * Lists keep their numbers and bullets as text, tables become rows of text
 * with tabs between the cells, and notes go to the end; the import report
 * says so.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define NS_OFFICE "urn:oasis:names:tc:opendocument:xmlns:office:1.0"
#define NS_STYLE "urn:oasis:names:tc:opendocument:xmlns:style:1.0"
#define NS_TEXT "urn:oasis:names:tc:opendocument:xmlns:text:1.0"
#define NS_XLINK "http://www.w3.org/1999/xlink"
#define NS_TABLE "urn:oasis:names:tc:opendocument:xmlns:table:1.0"
#define NS_FO "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
#define NS_SVG "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
#define NS_DRAW "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
#define NS_DC "http://purl.org/dc/elements/1.1/"

#define MAX_FACES 128
#define MAX_LISTS 64

typedef struct {
    owf_doc *doc;
    owf_report *report;
    owf_styles common, autos_content, autos_styles;
    const owf_styles *tables[2];     /* automatic, then common */
    struct { const char *name, *family; owf_font_kind kind; } faces[MAX_FACES];
    int nfaces;
    owf_list_style lists[MAX_LISTS];
    int nlists;
    owf_parafmt base_para;
    owf_charfmt base_char;           /* the default style's text, with real font and size */
    owf_builder *b;
    int space;                       /* the last character was white space */
    int counters[OWF_LIST_LEVELS];
    owf_buf notes;                   /* notes' text, "\n" between notes */
    int nnotes;
    owf_buf scratch;
    int failed;
    const char *current_href;
    int next_table_id;
    const owf_unzip *zip;
} odt;

/* ---- Styles ---- */

static void read_text_props(odt *o, const owf_xml_node *n, owf_charprops *cp)
{
    const char *v;
    int twips, i;

    if (!n)
        return;
    if ((v = owf_xml_attr(n, NS_STYLE, "font-name"))) {
        for (i = 0; i < o->nfaces; i++)
            if (strcmp(o->faces[i].name, v) == 0) {
                int font = owf_doc_font(o->doc, o->faces[i].family, o->faces[i].kind);
                if (font >= 0) {
                    cp->fmt.font = font;
                    cp->mask |= OWF_CP_FONT;
                }
                break;
            }
    } else if ((v = owf_xml_attr(n, NS_FO, "font-family"))) {
        char name[64];
        size_t len = strlen(v);
        if (len && (v[0] == '\'' || v[0] == '"')) {
            v++;
            len -= len > 1 ? 2 : 1;
        }
        if (len >= sizeof name)
            len = sizeof name - 1;
        memcpy(name, v, len);
        name[len] = 0;
        cp->fmt.font = owf_doc_font(o->doc, name, OWF_FONT_ANY);
        if (cp->fmt.font >= 0)
            cp->mask |= OWF_CP_FONT;
    }
    if (owf_parse_length(owf_xml_attr(n, NS_FO, "font-size"), &twips) && twips > 0) {
        cp->fmt.size = twips;
        cp->mask |= OWF_CP_SIZE;
    }
    if ((v = owf_xml_attr(n, NS_FO, "font-weight"))) {
        int bold = !strcmp(v, "bold") || atoi(v) >= 600;
        cp->fmt.flags = bold ? cp->fmt.flags | OWF_BOLD : cp->fmt.flags & ~OWF_BOLD;
        cp->mask |= OWF_CP_BOLD;
    }
    if ((v = owf_xml_attr(n, NS_FO, "font-style"))) {
        int italic = !strcmp(v, "italic") || !strcmp(v, "oblique");
        cp->fmt.flags = italic ? cp->fmt.flags | OWF_ITALIC : cp->fmt.flags & ~OWF_ITALIC;
        cp->mask |= OWF_CP_ITALIC;
    }
    if ((v = owf_xml_attr(n, NS_STYLE, "text-underline-style"))) {
        cp->fmt.flags = strcmp(v, "none") ? cp->fmt.flags | OWF_UNDERLINE : cp->fmt.flags & ~OWF_UNDERLINE;
        cp->mask |= OWF_CP_UNDERLINE;
    }
    if ((v = owf_xml_attr(n, NS_STYLE, "text-line-through-style"))) {
        cp->fmt.flags = strcmp(v, "none") ? cp->fmt.flags | OWF_STRIKE : cp->fmt.flags & ~OWF_STRIKE;
        cp->mask |= OWF_CP_STRIKE;
    }
    if ((v = owf_xml_attr(n, NS_STYLE, "text-position"))) {
        cp->fmt.flags &= ~(OWF_SUPER | OWF_SUB);
        if (!strncmp(v, "super", 5) || (v[0] != '-' && atoi(v) > 0))
            cp->fmt.flags |= OWF_SUPER;
        else if (!strncmp(v, "sub", 3) || atoi(v) < 0)
            cp->fmt.flags |= OWF_SUB;
        cp->mask |= OWF_CP_POSITION;
    }
    if ((v = owf_xml_attr(n, NS_STYLE, "use-window-font-color")) && !strcmp(v, "true")) {
        cp->fmt.colour = OWF_COLOUR_AUTO;
        cp->mask |= OWF_CP_COLOUR;
    } else if (owf_parse_colour(owf_xml_attr(n, NS_FO, "color"), &cp->fmt.colour)) {
        cp->mask |= OWF_CP_COLOUR;
    }
}

static void read_para_props(const owf_xml_node *n, owf_paraprops *pp)
{
    const char *v;
    const owf_xml_node *tabs, *t;

    if (!n)
        return;
    if ((v = owf_xml_attr(n, NS_FO, "text-align"))) {
        if (!strcmp(v, "center")) pp->fmt.align = OWF_ALIGN_CENTRE;
        else if (!strcmp(v, "end") || !strcmp(v, "right")) pp->fmt.align = OWF_ALIGN_RIGHT;
        else if (!strcmp(v, "justify")) pp->fmt.align = OWF_ALIGN_JUSTIFY;
        else pp->fmt.align = OWF_ALIGN_LEFT;
        pp->mask |= OWF_PP_ALIGN;
    }
    if (owf_parse_length(owf_xml_attr(n, NS_FO, "margin-left"), &pp->fmt.indent_left))
        pp->mask |= OWF_PP_LEFT;
    if (owf_parse_length(owf_xml_attr(n, NS_FO, "margin-right"), &pp->fmt.indent_right))
        pp->mask |= OWF_PP_RIGHT;
    if (owf_parse_length(owf_xml_attr(n, NS_FO, "text-indent"), &pp->fmt.indent_first))
        pp->mask |= OWF_PP_FIRST;
    if (owf_parse_length(owf_xml_attr(n, NS_FO, "margin-top"), &pp->fmt.space_before))
        pp->mask |= OWF_PP_BEFORE;
    if (owf_parse_length(owf_xml_attr(n, NS_FO, "margin-bottom"), &pp->fmt.space_after))
        pp->mask |= OWF_PP_AFTER;
    if ((v = owf_xml_attr(n, NS_FO, "line-height"))) {
        size_t len = strlen(v);
        pp->fmt.line_spacing = (len && v[len - 1] == '%') ? atoi(v) : 100;
        if (pp->fmt.line_spacing < 50 || pp->fmt.line_spacing > 500)
            pp->fmt.line_spacing = 100;
        pp->mask |= OWF_PP_LINE;
    }
    if ((v = owf_xml_attr(n, NS_FO, "break-before"))) {
        pp->fmt.page_break_before = !strcmp(v, "page");
        pp->mask |= OWF_PP_BREAK;
    }
    if ((tabs = owf_xml_child(n, NS_STYLE, "tab-stops"))) {
        pp->fmt.ntabs = 0;
        for (t = owf_xml_child(tabs, NS_STYLE, "tab-stop"); t && pp->fmt.ntabs < OWF_MAX_TABS;
             t = owf_xml_next(t, NS_STYLE, "tab-stop")) {
            owf_tab *tab = &pp->fmt.tabs[pp->fmt.ntabs];
            const char *type = owf_xml_attr(t, NS_STYLE, "type");
            if (!owf_parse_length(owf_xml_attr(t, NS_STYLE, "position"), &tab->position))
                continue;
            tab->kind = OWF_TAB_LEFT;
            if (type && !strcmp(type, "center")) tab->kind = OWF_TAB_CENTRE;
            else if (type && !strcmp(type, "right")) tab->kind = OWF_TAB_RIGHT;
            else if (type && !strcmp(type, "char")) tab->kind = OWF_TAB_DECIMAL;
            pp->fmt.ntabs++;
        }
        pp->mask |= OWF_PP_TABS;
    }
}

static void read_font_faces(odt *o, const owf_xml_node *decls)
{
    const owf_xml_node *f;
    for (f = owf_xml_child(decls, NS_STYLE, "font-face"); f && o->nfaces < MAX_FACES;
         f = owf_xml_next(f, NS_STYLE, "font-face")) {
        const char *name = owf_xml_attr(f, NS_STYLE, "name");
        const char *family = owf_xml_attr(f, NS_SVG, "font-family");
        const char *generic = owf_xml_attr(f, NS_STYLE, "font-family-generic");
        const char *pitch = owf_xml_attr(f, NS_STYLE, "font-pitch");
        char *unquoted;
        if (!name)
            continue;
        if (!family)
            family = name;
        /* svg:font-family may be quoted: 'Liberation Serif' (in place: the
         * text is ours). */
        unquoted = (char *)family;
        if (unquoted[0] == '\'' || unquoted[0] == '"') {
            size_t len = strlen(unquoted);
            if (len >= 2 && unquoted[len - 1] == unquoted[0]) {
                unquoted[len - 1] = 0;
                unquoted++;
            }
        }
        o->faces[o->nfaces].name = name;
        o->faces[o->nfaces].family = unquoted;
        o->faces[o->nfaces].kind = (pitch && !strcmp(pitch, "fixed")) ? OWF_FONT_MONO
                                 : (generic && !strcmp(generic, "swiss")) ? OWF_FONT_SANS
                                 : (generic && !strcmp(generic, "modern")) ? OWF_FONT_MONO
                                 : (generic && !strcmp(generic, "roman")) ? OWF_FONT_SERIF : OWF_FONT_ANY;
        o->nfaces++;
    }
}

static owf_number_format number_format(const char *f)
{
    if (!f || !*f) return OWF_NUMBER_NONE;
    if (!strcmp(f, "a")) return OWF_NUMBER_LOWER_LETTER;
    if (!strcmp(f, "A")) return OWF_NUMBER_UPPER_LETTER;
    if (!strcmp(f, "i")) return OWF_NUMBER_LOWER_ROMAN;
    if (!strcmp(f, "I")) return OWF_NUMBER_UPPER_ROMAN;
    return OWF_NUMBER_DECIMAL;
}

static void read_list_style(odt *o, const owf_xml_node *n)
{
    owf_list_style *ls;
    const owf_xml_node *l;

    if (o->nlists >= MAX_LISTS || !owf_xml_attr(n, NS_STYLE, "name"))
        return;
    ls = &o->lists[o->nlists++];
    memset(ls, 0, sizeof *ls);
    ls->name = owf_xml_attr(n, NS_STYLE, "name");
    for (l = n->first; l; l = l->next) {
        const char *lv = owf_xml_attr(l, NS_TEXT, "level");
        int level = lv ? atoi(lv) - 1 : -1;
        owf_list_level *ll;
        if (l->type != OWF_XML_ELEMENT || level < 0 || level >= OWF_LIST_LEVELS)
            continue;
        ll = &ls->levels[level];
        ll->start = 1;
        if (owf_xml_is(l, NS_TEXT, "list-level-style-bullet")) {
            const char *c = owf_xml_attr(l, NS_TEXT, "bullet-char");
            ll->format = OWF_NUMBER_BULLET;
            strncpy(ll->text, c && *c ? c : "\xe2\x80\xa2", sizeof ll->text - 1);
        } else if (owf_xml_is(l, NS_TEXT, "list-level-style-number")) {
            const char *pre = owf_xml_attr(l, NS_STYLE, "num-prefix");
            const char *suf = owf_xml_attr(l, NS_STYLE, "num-suffix");
            const char *start = owf_xml_attr(l, NS_TEXT, "start-value");
            const char *shown = owf_xml_attr(l, NS_TEXT, "display-levels");
            int show = shown ? atoi(shown) : 1, k, len = 0;
            ll->format = number_format(owf_xml_attr(l, NS_STYLE, "num-format"));
            if (start)
                ll->start = atoi(start);
            len += snprintf(ll->text + len, sizeof ll->text - (size_t)len, "%s", pre ? pre : "");
            if (show < 1)
                show = 1;
            for (k = level - show + 1; k <= level && len < (int)sizeof ll->text - 4; k++)
                if (k >= 0)
                    len += snprintf(ll->text + len, sizeof ll->text - (size_t)len, k < level ? "%%%d." : "%%%d", k + 1);
            if (len < (int)sizeof ll->text)
                snprintf(ll->text + len, sizeof ll->text - (size_t)len, "%s", suf ? suf : "");
        } else {
            ll->format = OWF_NUMBER_NONE;
        }
    }
}

static void read_styles(odt *o, const owf_xml_node *container, owf_styles *into)
{
    const owf_xml_node *n;
    for (n = container ? container->first : NULL; n; n = n->next) {
        if (owf_xml_is(n, NS_STYLE, "style")) {
            const char *family = owf_xml_attr(n, NS_STYLE, "family");
            const char *name = owf_xml_attr(n, NS_STYLE, "name");
            const char *outline = owf_xml_attr(n, NS_STYLE, "default-outline-level");
            int fam = family && !strcmp(family, "paragraph") ? OWF_FAMILY_PARAGRAPH
                    : family && !strcmp(family, "text") ? OWF_FAMILY_TEXT : 0;
            owf_style *st;
            if (!fam || !name || !(st = owf_styles_add(into, name, fam)))
                continue;
            st->parent = owf_xml_attr(n, NS_STYLE, "parent-style-name");
            st->list = owf_xml_attr(n, NS_STYLE, "list-style-name");
            read_para_props(owf_xml_child(n, NS_STYLE, "paragraph-properties"), &st->pp);
            read_text_props(o, owf_xml_child(n, NS_STYLE, "text-properties"), &st->cp);
            if (outline && fam == OWF_FAMILY_PARAGRAPH) {
                st->pp.fmt.heading = atoi(outline);
                if (st->pp.fmt.heading > 6)
                    st->pp.fmt.heading = 6;
                st->pp.mask |= OWF_PP_HEADING;
            }
        } else if (owf_xml_is(n, NS_STYLE, "default-style")) {
            const char *family = owf_xml_attr(n, NS_STYLE, "family");
            if (family && !strcmp(family, "paragraph")) {
                owf_paraprops pp;
                owf_charprops cp;
                memset(&pp, 0, sizeof pp);
                memset(&cp, 0, sizeof cp);
                read_para_props(owf_xml_child(n, NS_STYLE, "paragraph-properties"), &pp);
                read_text_props(o, owf_xml_child(n, NS_STYLE, "text-properties"), &cp);
                owf_paraprops_apply(&o->base_para, &pp);
                owf_charprops_apply(&o->base_char, &cp);
            }
        } else if (owf_xml_is(n, NS_TEXT, "list-style")) {
            read_list_style(o, n);
        }
    }
}

static const owf_list_style *find_list(const odt *o, const char *name)
{
    int i;
    if (!name)
        return NULL;
    for (i = o->nlists - 1; i >= 0; i--)
        if (!strcmp(o->lists[i].name, name))
            return &o->lists[i];
    return NULL;
}

/* ---- Text ---- */

static void set_fmt(owf_builder *b, const owf_charfmt *fmt)
{
    if (memcmp(fmt, &b->charfmt, sizeof *fmt) != 0) {
        owf_builder_flush(b);
        b->charfmt = *fmt;
    }
}

/* The model keeps -1 and 0 for "the document's font and size". */
static owf_charfmt relative(const odt *o, owf_charfmt f)
{
    if (f.font == o->base_char.font)
        f.font = -1;
    if (f.size == o->base_char.size)
        f.size = 0;
    return f;
}

/* Text with ODF's white space rule: runs of white space are one space. */
static void put_text(odt *o, const char *text, size_t length, const owf_charfmt *fmt)
{
    size_t i, start = 0;
    owf_charfmt f = relative(o, *fmt);

    set_fmt(o->b, &f);
    for (i = 0; i < length; i++) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            owf_builder_text(o->b, text + start, i - start);
            if (!o->space)
                owf_builder_text(o->b, " ", 1);
            o->space = 1;
            start = i + 1;
        } else {
            o->space = 0;
        }
    }
    owf_builder_text(o->b, text + start, length - start);
}

static void plain_text(const owf_xml_node *n, owf_buf *out)
{
    const owf_xml_node *c;
    for (c = n->first; c; c = c->next) {
        if (c->type == OWF_XML_TEXT)
            owf_buf_put(out, c->name, c->length);
        else if (owf_xml_is(c, NS_TEXT, "s"))
            owf_buf_putc(out, ' ');
        else if (owf_xml_is(c, NS_TEXT, "tab"))
            owf_buf_putc(out, '\t');
        else if (owf_xml_is(c, NS_TEXT, "p") || owf_xml_is(c, NS_TEXT, "h")) {
            if (out->length && out->data[out->length - 1] != ' ')
                owf_buf_putc(out, ' ');
            plain_text(c, out);
        } else
            plain_text(c, out);
    }
}

static void inline_children(odt *o, const owf_xml_node *n, const owf_charfmt *fmt);

static const char *image_mime(const char *name)
{
    const char *e=name?strrchr(name,'.'):NULL;if(!e)return "application/octet-stream";
    if(!strcasecmp(e,".png"))return "image/png";
    if(!strcasecmp(e,".jpg")||!strcasecmp(e,".jpeg"))return "image/jpeg";
    if(!strcasecmp(e,".gif"))return "image/gif";
    if(!strcasecmp(e,".webp"))return "image/webp";
    if(!strcasecmp(e,".ilbm")||!strcasecmp(e,".iff"))return "image/iff";
    return "application/octet-stream";
}

static void inline_node(odt *o, const owf_xml_node *c, const owf_charfmt *fmt)
{
    owf_charfmt f;
    owf_charfmt rel;

    if (c->type == OWF_XML_TEXT) {
        if (o->current_href) {
            owf_charfmt rel = relative(o, *fmt);
            set_fmt(o->b, &rel);
            owf_builder_link_text(o->b, c->name, c->length, o->current_href);
        } else put_text(o, c->name, c->length, fmt);
        return;
    }
    rel = relative(o, *fmt);
    if (owf_xml_is(c, NS_TEXT, "a")) {
        const char *old_href = o->current_href;
        const char *href = owf_xml_attr(c, NS_XLINK, "href");
        o->current_href = href;
        inline_children(o, c, fmt);
        o->current_href = old_href;
    } else if (owf_xml_is(c, NS_TEXT, "span")) {
        f = *fmt;
        owf_styles_resolve(o->tables, 2, owf_xml_attr(c, NS_TEXT, "style-name"), OWF_FAMILY_TEXT, NULL, &f);
        inline_children(o, c, &f);
    } else if (owf_xml_is(c, NS_TEXT, "s")) {
        const char *count = owf_xml_attr(c, NS_TEXT, "c");
        int i, n = count ? atoi(count) : 1;
        set_fmt(o->b, &rel);
        for (i = 0; i < n && i < 1000; i++)
            owf_builder_text(o->b, " ", 1);
        o->space = 1;
    } else if (owf_xml_is(c, NS_TEXT, "tab")) {
        set_fmt(o->b, &rel);
        owf_builder_special(o->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
        o->space = 1;
    } else if (owf_xml_is(c, NS_TEXT, "line-break")) {
        set_fmt(o->b, &rel);
        owf_builder_special(o->b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
        o->space = 1;
    } else if (owf_xml_is(c, NS_TEXT, "page-number") || owf_xml_is(c, NS_TEXT, "page-count") ||
               owf_xml_is(c, NS_TEXT, "date") || owf_xml_is(c, NS_TEXT, "time")) {
        owf_field field = owf_xml_is(c, NS_TEXT, "page-number") ? OWF_FIELD_PAGE
                        : owf_xml_is(c, NS_TEXT, "page-count") ? OWF_FIELD_PAGES
                        : owf_xml_is(c, NS_TEXT, "date") ? OWF_FIELD_DATE : OWF_FIELD_TIME;
        set_fmt(o->b, &rel);
        owf_builder_special(o->b, OWF_RUN_FIELD, field);
        o->space = 0;
    } else if (owf_xml_is(c, NS_TEXT, "note")) {
        /* The mark stays as a superscript number; the note goes to the end. */
        const owf_xml_node *body = owf_xml_child(c, NS_TEXT, "note-body");
        char mark[16];
        f = *fmt;
        f.flags |= OWF_SUPER;
        o->nnotes++;
        snprintf(mark, sizeof mark, "%d", o->nnotes);
        f = relative(o, f);
        set_fmt(o->b, &f);
        owf_builder_text(o->b, mark, strlen(mark));
        if (body) {
            size_t start;
            owf_buf_printf(&o->notes, "%d\t", o->nnotes);
            start = o->notes.length;
            plain_text(body, &o->notes);
            owf_buf_trim_from(&o->notes, start);
            owf_buf_putc(&o->notes, '\n');
        }
        o->space = 0;
        owf_report_add(o->report, OWF_NOTE_APPROX, "Footnotes and endnotes are listed at the end of the document");
    } else if (owf_xml_is(c, NS_OFFICE, "annotation")) {
        owf_report_add(o->report, OWF_NOTE_LOST, "Comments were left out");
    } else if (owf_xml_is(c, NS_DRAW, "frame")) {
        const owf_xml_node *img=owf_xml_child(c,NS_DRAW,"image");
        const char *href=img?owf_xml_attr(img,NS_XLINK,"href"):NULL;
        if(img&&href&&o->zip){unsigned char *data=NULL;size_t length=0;int w=4320,h=2880,idx;const char *wv=owf_xml_attr(c,NS_SVG,"width"),*hv=owf_xml_attr(c,NS_SVG,"height");owf_parse_length(wv,&w);owf_parse_length(hv,&h);if(owf_unzip_read(o->zip,href,&data,&length)==OWF_OK){idx=owf_doc_add_image(o->doc,href,image_mime(href),data,length,w,h,owf_xml_attr(c,NS_DRAW,"name"));free(data);if(idx>=0){set_fmt(o->b,&rel);owf_builder_image(o->b,idx);}else o->failed=1;}else owf_report_add(o->report,OWF_NOTE_LOST,"An embedded picture could not be read");}
        else if(img)owf_report_add(o->report,OWF_NOTE_LOST,"An embedded picture was not available in this document form");
        else owf_report_add(o->report, OWF_NOTE_LOST, "Text boxes and drawings were left out");
    } else if (owf_xml_is(c, NS_TEXT, "bookmark") || owf_xml_is(c, NS_TEXT, "bookmark-start") ||
               owf_xml_is(c, NS_TEXT, "bookmark-end") || owf_xml_is(c, NS_TEXT, "soft-page-break") ||
               owf_xml_is(c, NS_TEXT, "change") || owf_xml_is(c, NS_TEXT, "change-start") ||
               owf_xml_is(c, NS_TEXT, "change-end") || owf_xml_is(c, NS_OFFICE, "annotation-end") ||
               owf_xml_is(c, NS_TEXT, "reference-mark-start") || owf_xml_is(c, NS_TEXT, "reference-mark-end") ||
               owf_xml_is(c, NS_TEXT, "toc-mark") || owf_xml_is(c, NS_TEXT, "alphabetical-index-mark")) {
        /* marks without text */
    } else {
        /* Links, fields and anything else: their text comes across. */
        inline_children(o, c, fmt);
    }
}

static void inline_children(odt *o, const owf_xml_node *n, const owf_charfmt *fmt)
{
    const owf_xml_node *c;
    for (c = n->first; c; c = c->next)
        inline_node(o, c, fmt);
}

/* A paragraph's formats: the default style, then its style. */
static void para_formats(odt *o, const owf_xml_node *p, owf_parafmt *pf, owf_charfmt *cf)
{
    *pf = o->base_para;
    *cf = o->base_char;
    owf_styles_resolve(o->tables, 2, owf_xml_attr(p, NS_TEXT, "style-name"), OWF_FAMILY_PARAGRAPH, pf, cf);
}

static void paragraph(odt *o, const owf_xml_node *p, const char *prefix, int list_level)
{
    owf_parafmt pf;
    owf_charfmt cf;

    para_formats(o, p, &pf, &cf);
    if (owf_xml_is(p, NS_TEXT, "h")) {
        const char *level = owf_xml_attr(p, NS_TEXT, "outline-level");
        pf.heading = level ? atoi(level) : 1;
        if (pf.heading < 1) pf.heading = 1;
        if (pf.heading > 6) pf.heading = 6;
    } else {
        pf.heading = 0;
    }
    if (list_level >= 0 && !pf.indent_left) {
        pf.indent_left = 360 * (list_level + 1);
        pf.indent_first = -360;
    }
    if (o->b->para || o->b->text.length)
        owf_builder_end_para(o->b);
    o->b->parafmt = pf;
    o->space = 1;
    if (prefix) {
        owf_charfmt rel = relative(o, cf);
        set_fmt(o->b, &rel);
        owf_builder_text(o->b, prefix, strlen(prefix));
        owf_builder_special(o->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
    }
    inline_children(o, p, &cf);
    owf_builder_end_para(o->b);
}

static void blocks(odt *o, const owf_xml_node *n, int list_level, const char *list_name);

static void list_prefix(odt *o, const owf_list_style *ls, int level, owf_buf *out)
{
    const owf_list_level *ll;
    const char *t;

    out->length = 0;
    if (!ls) {
        owf_buf_puts(out, "\xe2\x80\xa2");
        return;
    }
    ll = &ls->levels[level];
    if (ll->format == OWF_NUMBER_BULLET) {
        owf_buf_puts(out, ll->text);
        return;
    }
    if (ll->format == OWF_NUMBER_NONE)
        return;
    for (t = ll->text; *t; t++) {
        if (t[0] == '%' && t[1] >= '1' && t[1] <= '9') {
            int k = t[1] - '1';
            owf_format_number(out, o->counters[k], ls->levels[k].format == OWF_NUMBER_BULLET ||
                                                   ls->levels[k].format == OWF_NUMBER_NONE
                                                       ? OWF_NUMBER_DECIMAL : ls->levels[k].format);
            t++;
        } else {
            owf_buf_putc(out, *t);
        }
    }
}

static void list(odt *o, const owf_xml_node *n, int level, const char *inherited)
{
    const char *name = owf_xml_attr(n, NS_TEXT, "style-name");
    const owf_list_style *ls;
    const owf_xml_node *item;
    const char *cont = owf_xml_attr(n, NS_TEXT, "continue-numbering");
    int k;

    if (!name)
        name = inherited;
    ls = find_list(o, name);
    if (level >= OWF_LIST_LEVELS)
        level = OWF_LIST_LEVELS - 1;
    if (level == 0 && !(cont && !strcmp(cont, "true")) && !owf_xml_attr(n, NS_TEXT, "continue-list"))
        for (k = 0; k < OWF_LIST_LEVELS; k++)
            o->counters[k] = 0;
    owf_report_add(o->report, OWF_NOTE_APPROX, "Lists are kept as text with their numbers and bullets");
    for (item = n->first; item; item = item->next) {
        const owf_xml_node *c;
        int first = 1;
        if (!owf_xml_is(item, NS_TEXT, "list-item") && !owf_xml_is(item, NS_TEXT, "list-header"))
            continue;
        if (owf_xml_is(item, NS_TEXT, "list-item")) {
            const char *start = owf_xml_attr(item, NS_TEXT, "start-value");
            if (start)
                o->counters[level] = atoi(start) - 1;
            else if (!o->counters[level] && ls)
                o->counters[level] = ls->levels[level].start - 1;
            o->counters[level]++;
            for (k = level + 1; k < OWF_LIST_LEVELS; k++)
                o->counters[k] = 0;
        } else {
            first = 0;
        }
        for (c = item->first; c; c = c->next) {
            if (owf_xml_is(c, NS_TEXT, "p") || owf_xml_is(c, NS_TEXT, "h")) {
                if (first) {
                    list_prefix(o, ls, level, &o->scratch);
                    paragraph(o, c, o->scratch.failed ? "" : (const char *)o->scratch.data, level);
                    first = 0;
                } else {
                    paragraph(o, c, NULL, level);
                }
            } else if (owf_xml_is(c, NS_TEXT, "list")) {
                list(o, c, level + 1, name);
            } else {
                blocks(o, c, level, name);
            }
        }
    }
}

/* A table: one paragraph per row, tabs between the cells. */
static void cell_text(odt *o, const owf_xml_node *n, int *first)
{
    const owf_xml_node *c;
    for (c = n->first; c; c = c->next) {
        if (owf_xml_is(c, NS_TEXT, "p") || owf_xml_is(c, NS_TEXT, "h")) {
            owf_parafmt pf;
            owf_charfmt cf;
            para_formats(o, c, &pf, &cf);
            if (!*first) {
                owf_charfmt rel = relative(o, cf);
                set_fmt(o->b, &rel);
                owf_builder_text(o->b, " ", 1);
            }
            *first = 0;
            o->space = 1;
            inline_children(o, c, &cf);
        } else if (c->type == OWF_XML_ELEMENT) {
            cell_text(o, c, first);
        }
    }
}

static void table_rows_id(odt *o, const owf_xml_node *n, int table_id, int *row_index)
{
    const owf_xml_node *r,*c;
    for(r=n->first;r;r=r->next){
        if(owf_xml_is(r,NS_TABLE,"table-row")){
            int cols=0,col=0;
            for(c=r->first;c;c=c->next)if(owf_xml_is(c,NS_TABLE,"table-cell"))++cols;
            for(c=r->first;c;c=c->next){int first=1,before;owf_para *p;if(!owf_xml_is(c,NS_TABLE,"table-cell"))continue;
                if(o->b->para||o->b->text.length)owf_builder_end_para(o->b);
                o->b->parafmt=o->base_para;before=o->b->story->nparas;cell_text(o,c,&first);owf_builder_end_para(o->b);
                if(o->b->story->nparas>before){p=&o->b->story->paras[o->b->story->nparas-1];p->table_id=table_id;p->table_row=*row_index;p->table_col=col;p->table_cols=cols;}++col;
            }++*row_index;
        }else if(owf_xml_is(r,NS_TABLE,"table-header-rows")||owf_xml_is(r,NS_TABLE,"table-rows")||owf_xml_is(r,NS_TABLE,"table-row-group"))table_rows_id(o,r,table_id,row_index);
    }
}

static void table_rows(odt *o, const owf_xml_node *n)
{
    int row=0,id=o->next_table_id++;
    table_rows_id(o,n,id,&row);
}

static void blocks(odt *o, const owf_xml_node *n, int list_level, const char *list_name)
{
    const owf_xml_node *c;
    for (c = n->first; c; c = c->next) {
        if (c->type != OWF_XML_ELEMENT)
            continue;
        if (owf_xml_is(c, NS_TEXT, "p") || owf_xml_is(c, NS_TEXT, "h")) {
            paragraph(o, c, NULL, -1);
        } else if (owf_xml_is(c, NS_TEXT, "list")) {
            list(o, c, list_level < 0 ? 0 : list_level, list_name);
        } else if (owf_xml_is(c, NS_TABLE, "table")) {
            table_rows(o, c);
        } else if (owf_xml_is(c, NS_TEXT, "tracked-changes") || owf_xml_is(c, NS_TEXT, "sequence-decls") ||
                   owf_xml_is(c, NS_TEXT, "variable-decls") || owf_xml_is(c, NS_TEXT, "user-field-decls") ||
                   owf_xml_is(c, NS_OFFICE, "forms") || owf_xml_is(c, NS_TEXT, "index-title-template") ||
                   owf_xml_is(c, NS_TEXT, "table-of-content-source")) {
            /* nothing to show */
        } else if (owf_xml_is(c, NS_DRAW, "frame")) {
            owf_report_add(o->report, OWF_NOTE_LOST, "Pictures are not brought across yet");
        } else {
            /* Sections, indexes, the table of contents: their paragraphs. */
            blocks(o, c, list_level, list_name);
        }
    }
}

/* ---- The page, headers and footers ---- */

static void read_page(odt *o, const owf_xml_node *styles_root, const owf_xml_node *autos)
{
    const owf_xml_node *master = owf_xml_child(owf_xml_child(styles_root, NS_OFFICE, "master-styles"), NS_STYLE, "master-page");
    const owf_xml_node *m, *layout = NULL, *props;
    const char *layout_name, *v;
    owf_page *pg = &o->doc->page;
    owf_builder b;

    for (m = master; m; m = owf_xml_next(m, NS_STYLE, "master-page")) {
        const char *name = owf_xml_attr(m, NS_STYLE, "name");
        if (name && !strcmp(name, "Standard")) {
            master = m;
            break;
        }
    }
    if (!master)
        return;
    layout_name = owf_xml_attr(master, NS_STYLE, "page-layout-name");
    for (m = owf_xml_child(autos, NS_STYLE, "page-layout"); m && layout_name; m = owf_xml_next(m, NS_STYLE, "page-layout")) {
        const char *name = owf_xml_attr(m, NS_STYLE, "name");
        if (name && !strcmp(name, layout_name)) {
            layout = m;
            break;
        }
    }
    if ((props = owf_xml_child(layout, NS_STYLE, "page-layout-properties"))) {
        owf_parse_length(owf_xml_attr(props, NS_FO, "page-width"), &pg->width);
        owf_parse_length(owf_xml_attr(props, NS_FO, "page-height"), &pg->height);
        owf_parse_length(owf_xml_attr(props, NS_FO, "margin-top"), &pg->margin_top);
        owf_parse_length(owf_xml_attr(props, NS_FO, "margin-bottom"), &pg->margin_bottom);
        owf_parse_length(owf_xml_attr(props, NS_FO, "margin-left"), &pg->margin_left);
        owf_parse_length(owf_xml_attr(props, NS_FO, "margin-right"), &pg->margin_right);
        if ((v = owf_xml_attr(props, NS_STYLE, "num-format"))) {
            switch (number_format(v)) {
            case OWF_NUMBER_UPPER_ROMAN: pg->pagenum_style = OWF_PAGENUM_ROMAN_UPPER; break;
            case OWF_NUMBER_LOWER_ROMAN: pg->pagenum_style = OWF_PAGENUM_ROMAN_LOWER; break;
            case OWF_NUMBER_UPPER_LETTER: pg->pagenum_style = OWF_PAGENUM_LETTER_UPPER; break;
            case OWF_NUMBER_LOWER_LETTER: pg->pagenum_style = OWF_PAGENUM_LETTER_LOWER; break;
            default: pg->pagenum_style = OWF_PAGENUM_ARABIC; break;
            }
        }
    }
    if ((m = owf_xml_child(master, NS_STYLE, "header"))) {
        owf_builder_init(&b, o->doc, &o->doc->header);
        o->b = &b;
        blocks(o, m, -1, NULL);
        if (owf_builder_done(&b) != OWF_OK)
            o->failed = 1;
    }
    if ((m = owf_xml_child(master, NS_STYLE, "footer"))) {
        owf_builder_init(&b, o->doc, &o->doc->footer);
        o->b = &b;
        blocks(o, m, -1, NULL);
        if (owf_builder_done(&b) != OWF_OK)
            o->failed = 1;
    }
    /* A first-page header with nothing in it: no header on the first page. */
    if ((m = owf_xml_child(master, NS_STYLE, "header-first"))) {
        if (owf_xml_child(m, NS_TEXT, "p") || owf_xml_child(m, NS_TEXT, "h"))
            owf_report_add(o->report, OWF_NOTE_APPROX, "A different header or footer on the first page became the usual one");
        else
            o->doc->page.header_on_first = 0;
    }
    if ((m = owf_xml_child(master, NS_STYLE, "footer-first"))) {
        if (owf_xml_child(m, NS_TEXT, "p") || owf_xml_child(m, NS_TEXT, "h"))
            owf_report_add(o->report, OWF_NOTE_APPROX, "A different header or footer on the first page became the usual one");
        else
            o->doc->page.footer_on_first = 0;
    }
}

/* ---- Reading ---- */

static int read_document(odt *o, const owf_xml_node *content_root, const owf_xml_node *styles_root,
                         const owf_xml_node *meta_root)
{
    const owf_xml_node *body, *text, *t;
    owf_builder b;
    int result;

    owf_parafmt_init(&o->base_para);
    owf_charfmt_init(&o->base_char);
    o->base_char.size = 12 * OWF_TWIPS_PER_POINT;
    owf_buf_init(&o->notes);
    owf_buf_init(&o->scratch);

    /* Fonts and styles: styles.xml's, then content.xml's own. */
    read_font_faces(o, owf_xml_child(styles_root, NS_OFFICE, "font-face-decls"));
    if (content_root != styles_root)
        read_font_faces(o, owf_xml_child(content_root, NS_OFFICE, "font-face-decls"));
    read_styles(o, owf_xml_child(styles_root, NS_OFFICE, "styles"), &o->common);
    read_styles(o, owf_xml_child(styles_root, NS_OFFICE, "automatic-styles"), &o->autos_styles);
    if (content_root != styles_root)
        read_styles(o, owf_xml_child(content_root, NS_OFFICE, "automatic-styles"), &o->autos_content);

    if (o->base_char.font >= 0) {
        o->doc->base.font = o->base_char.font;
    } else {
        o->base_char.font = owf_doc_font(o->doc, "Liberation Serif", OWF_FONT_SERIF);
        o->doc->base.font = o->base_char.font;
    }
    o->doc->base.size = o->base_char.size;

    /* Headers and footers use styles.xml's automatic styles. */
    o->tables[0] = &o->autos_styles;
    o->tables[1] = &o->common;
    read_page(o, styles_root, owf_xml_child(styles_root, NS_OFFICE, "automatic-styles"));

    /* The body uses content.xml's. */
    o->tables[0] = content_root != styles_root ? &o->autos_content : &o->autos_styles;
    body = owf_xml_child(content_root, NS_OFFICE, "body");
    text = owf_xml_child(body, NS_OFFICE, "text");
    if (!text)
        return OWF_ERR_FORMAT;
    owf_builder_init(&b, o->doc, &o->doc->body);
    o->b = &b;
    blocks(o, text, -1, NULL);

    /* Notes at the end. */
    if (o->nnotes && !o->notes.failed) {
        const char *p = (const char *)o->notes.data, *end = p + o->notes.length;
        if (b.para || b.text.length)
            owf_builder_end_para(&b);
        owf_parafmt_init(&b.parafmt);
        b.parafmt.heading = 2;
        owf_charfmt_init(&b.charfmt);
        owf_builder_text(&b, "Notes", 5);
        owf_builder_end_para(&b);
        owf_parafmt_init(&b.parafmt);
        while (p < end) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            const char *tab = memchr(p, '\t', (size_t)((nl ? nl : end) - p));
            if (!nl)
                nl = end;
            if (tab) {
                owf_builder_text(&b, p, (size_t)(tab - p));
                owf_builder_special(&b, OWF_RUN_TAB, OWF_FIELD_PAGE);
                p = tab + 1;
            }
            owf_builder_text(&b, p, (size_t)(nl - p));
            owf_builder_end_para(&b);
            p = nl + 1;
        }
    }
    result = owf_builder_done(&b);

    if (meta_root && (t = owf_xml_child(owf_xml_child(meta_root, NS_OFFICE, "meta"), NS_DC, "title")) &&
        t->first && t->first->type == OWF_XML_TEXT) {
        owf_buf title;
        owf_buf_init(&title);
        owf_buf_put(&title, t->first->name, t->first->length);
        if (!title.failed)
            owf_doc_set_title(o->doc, (const char *)title.data);
        owf_buf_free(&title);
    }
    if (o->notes.failed || o->scratch.failed || o->failed)
        result = OWF_ERR_MEMORY;
    owf_buf_free(&o->notes);
    owf_buf_free(&o->scratch);
    return result;
}

static void odt_free(odt *o)
{
    owf_styles_free(&o->common);
    owf_styles_free(&o->autos_content);
    owf_styles_free(&o->autos_styles);
    free(o);
}

static int parse_part(const owf_unzip *zip, const char *name, owf_xml **xml, int required)
{
    unsigned char *data;
    size_t length;
    int result = owf_unzip_read(zip, name, &data, &length), error;

    *xml = NULL;
    if (result != OWF_OK)
        return required ? (result == OWF_ERR_FORMAT ? OWF_ERR_CORRUPT : result) : OWF_OK;
    *xml = owf_xml_parse((const char *)data, length, &error);
    free(data);
    return *xml ? OWF_OK : (required ? error : OWF_OK);
}

static int import_odt(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_unzip zip;
    owf_xml *content = NULL, *styles = NULL, *meta = NULL;
    odt *o;
    int result;

    if (!owf_unzip_open(&zip, data, length))
        return OWF_ERR_FORMAT;
    result = parse_part(&zip, "content.xml", &content, 1);
    if (result == OWF_OK)
        result = parse_part(&zip, "styles.xml", &styles, 0);
    if (result == OWF_OK)
        result = parse_part(&zip, "meta.xml", &meta, 0);
    if (result == OWF_OK) {
        o = calloc(1, sizeof *o);
        if (!o) {
            result = OWF_ERR_MEMORY;
        } else {
            o->doc = doc;
            o->report = report;
            o->zip = &zip;
            result = read_document(o, owf_xml_root(content), styles ? owf_xml_root(styles) : owf_xml_root(content),
                                   meta ? owf_xml_root(meta) : NULL);
            odt_free(o);
        }
    }
    owf_xml_free(content);
    owf_xml_free(styles);
    owf_xml_free(meta);
    return result;
}

static int import_fodt(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_xml *xml;
    odt *o;
    int error, result;

    xml = owf_xml_parse((const char *)data, length, &error);
    if (!xml)
        return error;
    if (!owf_xml_is(owf_xml_root(xml), NS_OFFICE, "document")) {
        owf_xml_free(xml);
        return OWF_ERR_FORMAT;
    }
    o = calloc(1, sizeof *o);
    if (!o) {
        owf_xml_free(xml);
        return OWF_ERR_MEMORY;
    }
    o->doc = doc;
    o->report = report;
    result = read_document(o, owf_xml_root(xml), owf_xml_root(xml), owf_xml_root(xml));
    odt_free(o);
    owf_xml_free(xml);
    return result;
}

static int detect_odt(const unsigned char *data, size_t length)
{
    owf_unzip zip;
    static const char mime[] = "application/vnd.oasis.opendocument.text";
    /* ODF puts "mimetype" first and stored, so its text is at byte 38. */
    if (length > 38 + sizeof mime && !memcmp(data, "PK\3\4", 4) && !memcmp(data + 30, "mimetype", 8) &&
        !memcmp(data + 38, mime, sizeof mime - 1))
        return 100;
    if (owf_unzip_open(&zip, data, length) && owf_unzip_has(&zip, "content.xml") && owf_unzip_has(&zip, "styles.xml"))
        return 80;
    return 0;
}

static int detect_fodt(const unsigned char *data, size_t length)
{
    size_t n = length < 4096 ? length : 4096;
    size_t i;
    /* "<office:document" and the text mimetype near the start. */
    for (i = 0; i + 16 < n; i++)
        if (data[i] == '<' && !memcmp(data + i, "<office:document", 16) &&
            (i + 17 > n || data[i + 16] == ' ' || data[i + 16] == '\n' || data[i + 16] == '\r' || data[i + 16] == '\t')) {
            size_t j;
            for (j = i; j + 39 < n; j++)
                if (!memcmp(data + j, "application/vnd.oasis.opendocument.text", 39))
                    return 90;
            return 0;
        }
    return 0;
}

const owf_format owf_format_fodt = {
    "fodt", "OpenDocument Text, flat XML", "fodt", detect_fodt, import_fodt, NULL
};

int owf_import_odt(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    return import_odt(data, length, doc, report);
}

int owf_detect_odt(const unsigned char *data, size_t length)
{
    return detect_odt(data, length);
}

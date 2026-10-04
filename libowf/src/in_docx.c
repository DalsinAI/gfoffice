/*
 * libowf: DOCX in (Office Open XML, ECMA-376, transitional and strict).
 *
 * The parts are found through the relationships, as Word does. Formatting
 * comes from the document defaults, the paragraph's style and its parents,
 * the run's character style, then the direct formatting. Theme fonts
 * (Calibri, Cambria...) are looked up in the theme. Lists keep their
 * numbers and bullets as text; fields PAGE, NUMPAGES, DATE and TIME become
 * fields and the others show their last result; tracked insertions are
 * kept and deletions dropped; tables become rows of text with tabs between
 * the cells; footnotes and endnotes go to the end.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_W "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
#define NS_W_STRICT "http://purl.oclc.org/ooxml/wordprocessingml/main"
#define NS_PKG_REL "http://schemas.openxmlformats.org/package/2006/relationships"
#define NS_A "http://schemas.openxmlformats.org/drawingml/2006/main"
#define NS_A_STRICT "http://purl.oclc.org/ooxml/drawingml/main"
#define NS_MATH "http://schemas.openxmlformats.org/officeDocument/2006/math"
#define NS_MC "http://schemas.openxmlformats.org/markup-compatibility/2006"

#define MAX_LISTS 128
#define MAX_FIELDS 8

typedef struct {
    char *name, *type, *target;
} rel;

typedef struct {
    rel *items;
    int count;
    owf_xml *xml;
} rels;

typedef struct {
    int code;                /* reading the field's code */
    int hide;                /* hiding its result (a field we write ourselves) */
    owf_field field;
    int special;             /* PAGE, NUMPAGES, DATE or TIME */
    char instr[32];
} field_state;

typedef struct {
    owf_doc *doc;
    owf_report *report;
    const char *w;           /* the WordprocessingML namespace in use */
    const owf_unzip *zip;
    char base[64];           /* the main part's folder, "word/" */
    rels doc_rels;
    owf_styles styles;
    const owf_styles *tables[1];
    const char *default_para;   /* the default paragraph style's id */
    owf_parafmt base_para;
    owf_charfmt base_char;
    char minor_font[64], major_font[64];
    owf_list_style lists[MAX_LISTS];
    int counters[MAX_LISTS][OWF_LIST_LEVELS];
    int nlists;
    owf_xml *numbering, *styles_xml, *theme, *footnotes, *endnotes;
    owf_builder *b;
    field_state fields[MAX_FIELDS];
    int nfields;
    int break_next;          /* a page break waits for the next paragraph */
    owf_buf notes, scratch;
    int nnotes, failed;
} docx;

/* ---- Small helpers ---- */

static const char *attr_any(const owf_xml_node *n, const char *name)
{
    int i;
    if (!n)
        return NULL;
    for (i = 0; i < n->nattrs; i++)
        if (strcmp(n->attrs[i].name, name) == 0)
            return n->attrs[i].value;
    return NULL;
}

static const char *wval(const docx *d, const owf_xml_node *n)
{
    return owf_xml_attr(n, d->w, "val");
}

static int is_w(const docx *d, const owf_xml_node *n, const char *name)
{
    return owf_xml_is(n, d->w, name);
}

static const owf_xml_node *wchild(const docx *d, const owf_xml_node *n, const char *name)
{
    return owf_xml_child(n, d->w, name);
}

/* <w:b/> is on; w:val "0", "false", "off" or "none" is off. */
static int on_off(const docx *d, const owf_xml_node *n)
{
    const char *v = wval(d, n);
    return !v || !(strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "off") == 0 || strcmp(v, "none") == 0);
}

static int wint(const docx *d, const owf_xml_node *n, const char *name, int *value)
{
    const char *v = owf_xml_attr(n, d->w, name);
    if (!v)
        return 0;
    *value = atoi(v);
    return 1;
}

/* A part's name from a relationship's target, relative to the source. */
static void part_name(char *out, size_t size, const char *base, const char *target)
{
    if (target[0] == '/')
        snprintf(out, size, "%s", target + 1);
    else
        snprintf(out, size, "%s%s", base, target);
}

static owf_xml *read_xml(const owf_unzip *zip, const char *name)
{
    unsigned char *data;
    size_t length;
    owf_xml *xml;
    int error;

    if (owf_unzip_read(zip, name, &data, &length) != OWF_OK)
        return NULL;
    xml = owf_xml_parse((const char *)data, length, &error);
    free(data);
    return xml;
}

static void read_rels(const owf_unzip *zip, const char *name, rels *r)
{
    const owf_xml_node *n;
    int count = 0;

    memset(r, 0, sizeof *r);
    r->xml = read_xml(zip, name);
    if (!r->xml)
        return;
    for (n = owf_xml_root(r->xml)->first; n; n = n->next)
        if (n->type == OWF_XML_ELEMENT)
            count++;
    r->items = calloc((size_t)count + 1, sizeof *r->items);
    if (!r->items)
        return;
    for (n = owf_xml_root(r->xml)->first; n; n = n->next) {
        if (n->type != OWF_XML_ELEMENT || strcmp(n->name, "Relationship"))
            continue;
        r->items[r->count].name = (char *)attr_any(n, "Id");
        r->items[r->count].type = (char *)attr_any(n, "Type");
        r->items[r->count].target = (char *)attr_any(n, "Target");
        if (r->items[r->count].name && r->items[r->count].type && r->items[r->count].target)
            r->count++;
    }
}

static void free_rels(rels *r)
{
    free(r->items);
    owf_xml_free(r->xml);
    memset(r, 0, sizeof *r);
}

/* The target of the first relationship whose type ends in kind. */
static const char *rel_by_type(const rels *r, const char *kind)
{
    int i;
    size_t k = strlen(kind);
    for (i = 0; i < r->count; i++) {
        size_t n = strlen(r->items[i].type);
        if (n >= k && strcmp(r->items[i].type + n - k, kind) == 0)
            return r->items[i].target;
    }
    return NULL;
}

static const char *rel_by_id(const rels *r, const char *id)
{
    int i;
    for (i = 0; id && i < r->count; i++)
        if (strcmp(r->items[i].name, id) == 0)
            return r->items[i].target;
    return NULL;
}

static owf_xml *read_related(docx *d, const char *kind)
{
    const char *target = rel_by_type(&d->doc_rels, kind);
    char name[256];
    if (!target)
        return NULL;
    part_name(name, sizeof name, d->base, target);
    return read_xml(d->zip, name);
}

/* ---- Properties ---- */

static void font_props(docx *d, const owf_xml_node *f, owf_charprops *cp)
{
    const char *name = owf_xml_attr(f, d->w, "ascii");
    const char *theme = owf_xml_attr(f, d->w, "asciiTheme");
    owf_font_kind kind = OWF_FONT_ANY;
    int font;

    if (!name)
        name = owf_xml_attr(f, d->w, "hAnsi");
    if (!name && theme) {
        if (!strncmp(theme, "major", 5) && d->major_font[0])
            name = d->major_font;
        else if (!strncmp(theme, "minor", 5) && d->minor_font[0])
            name = d->minor_font;
    }
    if (!name)
        return;
    owf_font_modern(name, &kind);
    font = owf_doc_font(d->doc, name, kind);
    if (font < 0) {
        d->failed = 1;
        return;
    }
    cp->fmt.font = font;
    cp->mask |= OWF_CP_FONT;
}

static void set_flag(owf_charprops *cp, unsigned mask, unsigned flag, int on)
{
    cp->fmt.flags = on ? cp->fmt.flags | flag : cp->fmt.flags & ~flag;
    cp->mask |= mask;
}

static void read_rpr(docx *d, const owf_xml_node *rpr, owf_charprops *cp)
{
    const owf_xml_node *n;
    for (n = rpr ? rpr->first : NULL; n; n = n->next) {
        if (n->type != OWF_XML_ELEMENT || strcmp(n->ns, d->w))
            continue;
        if (!strcmp(n->name, "rFonts")) {
            font_props(d, n, cp);
        } else if (!strcmp(n->name, "b")) {
            set_flag(cp, OWF_CP_BOLD, OWF_BOLD, on_off(d, n));
        } else if (!strcmp(n->name, "i")) {
            set_flag(cp, OWF_CP_ITALIC, OWF_ITALIC, on_off(d, n));
        } else if (!strcmp(n->name, "strike") || !strcmp(n->name, "dstrike")) {
            set_flag(cp, OWF_CP_STRIKE, OWF_STRIKE, on_off(d, n));
        } else if (!strcmp(n->name, "u")) {
            const char *v = wval(d, n);
            set_flag(cp, OWF_CP_UNDERLINE, OWF_UNDERLINE, !v || strcmp(v, "none") != 0);
        } else if (!strcmp(n->name, "vertAlign")) {
            const char *v = wval(d, n);
            cp->fmt.flags &= ~(OWF_SUPER | OWF_SUB);
            if (v && !strcmp(v, "superscript"))
                cp->fmt.flags |= OWF_SUPER;
            else if (v && !strcmp(v, "subscript"))
                cp->fmt.flags |= OWF_SUB;
            cp->mask |= OWF_CP_POSITION;
        } else if (!strcmp(n->name, "sz")) {
            const char *v = wval(d, n);
            if (v && atoi(v) > 0) {
                cp->fmt.size = atoi(v) * 10;   /* half-points */
                cp->mask |= OWF_CP_SIZE;
            }
        } else if (!strcmp(n->name, "color")) {
            const char *v = wval(d, n);
            if (v && !strcmp(v, "auto")) {
                cp->fmt.colour = OWF_COLOUR_AUTO;
                cp->mask |= OWF_CP_COLOUR;
            } else if (owf_parse_colour(v, &cp->fmt.colour)) {
                if (cp->fmt.colour == 0)
                    cp->fmt.colour = OWF_COLOUR_AUTO;   /* black is the default */
                cp->mask |= OWF_CP_COLOUR;
            }
        }
    }
}

/* Paragraph properties. Tab positions stay measured from the margin until
 * the paragraph's indent is known. */
static void read_ppr(docx *d, const owf_xml_node *ppr, owf_paraprops *pp, const char **num_id, int *num_level)
{
    const owf_xml_node *n, *t;
    int v;

    for (n = ppr ? ppr->first : NULL; n; n = n->next) {
        if (n->type != OWF_XML_ELEMENT || strcmp(n->ns, d->w))
            continue;
        if (!strcmp(n->name, "jc")) {
            const char *j = wval(d, n);
            pp->fmt.align = !j ? OWF_ALIGN_LEFT
                          : !strcmp(j, "center") ? OWF_ALIGN_CENTRE
                          : (!strcmp(j, "right") || !strcmp(j, "end")) ? OWF_ALIGN_RIGHT
                          : (!strcmp(j, "both") || !strcmp(j, "distribute")) ? OWF_ALIGN_JUSTIFY : OWF_ALIGN_LEFT;
            pp->mask |= OWF_PP_ALIGN;
        } else if (!strcmp(n->name, "ind")) {
            if (wint(d, n, "left", &v) || wint(d, n, "start", &v)) {
                pp->fmt.indent_left = v;
                pp->mask |= OWF_PP_LEFT;
            }
            if (wint(d, n, "right", &v) || wint(d, n, "end", &v)) {
                pp->fmt.indent_right = v;
                pp->mask |= OWF_PP_RIGHT;
            }
            if (wint(d, n, "hanging", &v)) {
                pp->fmt.indent_first = -v;
                pp->mask |= OWF_PP_FIRST;
            } else if (wint(d, n, "firstLine", &v)) {
                pp->fmt.indent_first = v;
                pp->mask |= OWF_PP_FIRST;
            }
        } else if (!strcmp(n->name, "spacing")) {
            const char *rule = owf_xml_attr(n, d->w, "lineRule");
            if (wint(d, n, "before", &v)) {
                pp->fmt.space_before = v;
                pp->mask |= OWF_PP_BEFORE;
            }
            if (wint(d, n, "after", &v)) {
                pp->fmt.space_after = v;
                pp->mask |= OWF_PP_AFTER;
            }
            if (wint(d, n, "line", &v)) {
                pp->fmt.line_spacing = (!rule || !strcmp(rule, "auto")) ? v * 100 / 240 : 100;
                if (pp->fmt.line_spacing < 50 || pp->fmt.line_spacing > 500)
                    pp->fmt.line_spacing = 100;
                pp->mask |= OWF_PP_LINE;
            }
        } else if (!strcmp(n->name, "pageBreakBefore")) {
            pp->fmt.page_break_before = on_off(d, n);
            pp->mask |= OWF_PP_BREAK;
        } else if (!strcmp(n->name, "outlineLvl")) {
            const char *l = wval(d, n);
            int level = l ? atoi(l) + 1 : 0;
            if (level >= 1 && level <= 6) {
                pp->fmt.heading = level;
                pp->mask |= OWF_PP_HEADING;
            }
        } else if (!strcmp(n->name, "tabs")) {
            pp->fmt.ntabs = 0;
            for (t = n->first; t && pp->fmt.ntabs < OWF_MAX_TABS; t = t->next) {
                const char *type;
                owf_tab *tab;
                if (!is_w(d, t, "tab") || !wint(d, t, "pos", &v))
                    continue;
                type = wval(d, t);
                if (type && (!strcmp(type, "clear") || !strcmp(type, "bar")))
                    continue;
                tab = &pp->fmt.tabs[pp->fmt.ntabs++];
                tab->position = v;
                tab->kind = !type ? OWF_TAB_LEFT
                          : !strcmp(type, "center") ? OWF_TAB_CENTRE
                          : (!strcmp(type, "right") || !strcmp(type, "end")) ? OWF_TAB_RIGHT
                          : !strcmp(type, "decimal") ? OWF_TAB_DECIMAL : OWF_TAB_LEFT;
            }
            pp->mask |= OWF_PP_TABS;
        } else if (!strcmp(n->name, "numPr") && num_id) {
            const owf_xml_node *id = wchild(d, n, "numId"), *lvl = wchild(d, n, "ilvl");
            if (id)
                *num_id = wval(d, id);
            if (lvl && wval(d, lvl))
                *num_level = atoi(wval(d, lvl));
        }
    }
}

static void read_styles(docx *d)
{
    const owf_xml_node *root, *n, *defaults;

    if (!d->styles_xml)
        return;
    root = owf_xml_root(d->styles_xml);
    if ((defaults = wchild(d, root, "docDefaults"))) {
        owf_charprops cp;
        owf_paraprops pp;
        memset(&cp, 0, sizeof cp);
        memset(&pp, 0, sizeof pp);
        read_rpr(d, wchild(d, wchild(d, defaults, "rPrDefault"), "rPr"), &cp);
        read_ppr(d, wchild(d, wchild(d, defaults, "pPrDefault"), "pPr"), &pp, NULL, NULL);
        owf_charprops_apply(&d->base_char, &cp);
        owf_paraprops_apply(&d->base_para, &pp);
    }
    for (n = root->first; n; n = n->next) {
        const char *type, *id, *def;
        const owf_xml_node *name, *based;
        owf_style *st;
        int family;
        if (!is_w(d, n, "style"))
            continue;
        type = owf_xml_attr(n, d->w, "type");
        id = owf_xml_attr(n, d->w, "styleId");
        family = type && !strcmp(type, "paragraph") ? OWF_FAMILY_PARAGRAPH
               : type && !strcmp(type, "character") ? OWF_FAMILY_TEXT : 0;
        if (!family || !id || !(st = owf_styles_add(&d->styles, id, family))) {
            continue;
        }
        if ((based = wchild(d, n, "basedOn")))
            st->parent = wval(d, based);
        read_rpr(d, wchild(d, n, "rPr"), &st->cp);
        read_ppr(d, wchild(d, n, "pPr"), &st->pp, &st->list, &st->list_level);
        /* "heading 1" to "heading 6" are headings, whatever their outline. */
        if (family == OWF_FAMILY_PARAGRAPH && (name = wchild(d, n, "name")) && wval(d, name)) {
            const char *v = wval(d, name);
            if (!strncmp(v, "heading ", 8) && v[8] >= '1' && v[8] <= '6' && !v[9]) {
                st->pp.fmt.heading = v[8] - '0';
                st->pp.mask |= OWF_PP_HEADING;
            }
        }
        def = owf_xml_attr(n, d->w, "default");
        if (family == OWF_FAMILY_PARAGRAPH && def && on_off(d, n) && (!strcmp(def, "1") || !strcmp(def, "true")))
            d->default_para = id;
    }
}

static void read_theme(docx *d)
{
    const owf_xml_node *root, *scheme, *n;
    const char *a;

    if (!d->theme)
        return;
    root = owf_xml_root(d->theme);
    a = root->ns;    /* transitional or strict DrawingML */
    scheme = owf_xml_child(owf_xml_child(root, a, "themeElements"), a, "fontScheme");
    if ((n = owf_xml_child(owf_xml_child(scheme, a, "minorFont"), a, "latin")) && attr_any(n, "typeface"))
        snprintf(d->minor_font, sizeof d->minor_font, "%s", attr_any(n, "typeface"));
    if ((n = owf_xml_child(owf_xml_child(scheme, a, "majorFont"), a, "latin")) && attr_any(n, "typeface"))
        snprintf(d->major_font, sizeof d->major_font, "%s", attr_any(n, "typeface"));
}

static owf_number_format number_format(const char *f)
{
    if (!f) return OWF_NUMBER_DECIMAL;
    if (!strcmp(f, "bullet")) return OWF_NUMBER_BULLET;
    if (!strcmp(f, "lowerLetter")) return OWF_NUMBER_LOWER_LETTER;
    if (!strcmp(f, "upperLetter")) return OWF_NUMBER_UPPER_LETTER;
    if (!strcmp(f, "lowerRoman")) return OWF_NUMBER_LOWER_ROMAN;
    if (!strcmp(f, "upperRoman")) return OWF_NUMBER_UPPER_ROMAN;
    if (!strcmp(f, "none")) return OWF_NUMBER_NONE;
    return OWF_NUMBER_DECIMAL;
}

static void read_numbering(docx *d)
{
    const owf_xml_node *root, *num, *abs, *lvl;

    if (!d->numbering)
        return;
    root = owf_xml_root(d->numbering);
    for (num = wchild(d, root, "num"); num && d->nlists < MAX_LISTS; num = owf_xml_next(num, d->w, "num")) {
        const char *id = owf_xml_attr(num, d->w, "numId");
        const owf_xml_node *ref = wchild(d, num, "abstractNumId");
        const char *abs_id = ref ? wval(d, ref) : NULL;
        owf_list_style *ls;
        if (!id || !abs_id)
            continue;
        for (abs = wchild(d, root, "abstractNum"); abs; abs = owf_xml_next(abs, d->w, "abstractNum")) {
            const char *aid = owf_xml_attr(abs, d->w, "abstractNumId");
            if (aid && !strcmp(aid, abs_id))
                break;
        }
        if (!abs)
            continue;
        ls = &d->lists[d->nlists++];
        memset(ls, 0, sizeof *ls);
        ls->name = id;
        for (lvl = wchild(d, abs, "lvl"); lvl; lvl = owf_xml_next(lvl, d->w, "lvl")) {
            const char *il = owf_xml_attr(lvl, d->w, "ilvl");
            int level = il ? atoi(il) : -1, v;
            owf_list_level *ll;
            const owf_xml_node *n;
            if (level < 0 || level >= OWF_LIST_LEVELS)
                continue;
            ll = &ls->levels[level];
            ll->start = 1;
            if ((n = wchild(d, lvl, "start")) && wval(d, n))
                ll->start = atoi(wval(d, n));
            ll->format = number_format((n = wchild(d, lvl, "numFmt")) ? wval(d, n) : NULL);
            if ((n = wchild(d, lvl, "lvlText")) && wval(d, n))
                snprintf(ll->text, sizeof ll->text, "%s", wval(d, n));
            if (ll->format == OWF_NUMBER_BULLET) {
                /* Symbol and Wingdings bullets live at U+F0xx: show a plain one. */
                const unsigned char *t = (const unsigned char *)ll->text;
                if (!t[0] || (t[0] == 0xEF && (t[1] == 0x80 || t[1] == 0x81 || t[1] == 0x82 || t[1] == 0x83)))
                    snprintf(ll->text, sizeof ll->text, "%s", "\xe2\x80\xa2");
            }
            if ((n = wchild(d, wchild(d, lvl, "pPr"), "ind")) && wint(d, n, "left", &v))
                ll->indent = v;
        }
    }
}

static int list_index(const docx *d, const char *id)
{
    int i;
    for (i = 0; id && i < d->nlists; i++)
        if (!strcmp(d->lists[i].name, id))
            return i;
    return -1;
}

/* ---- Text ---- */

static owf_charfmt relative(const docx *d, owf_charfmt f)
{
    if (f.font == d->base_char.font)
        f.font = -1;
    if (f.size == d->base_char.size)
        f.size = 0;
    return f;
}

static void set_fmt(docx *d, const owf_charfmt *fmt)
{
    owf_charfmt f = relative(d, *fmt);
    if (memcmp(&f, &d->b->charfmt, sizeof f) != 0) {
        owf_builder_flush(d->b);
        d->b->charfmt = f;
    }
}

static int hiding(const docx *d)
{
    int i;
    for (i = 0; i < d->nfields; i++)
        if (d->fields[i].code || d->fields[i].hide)
            return 1;
    return 0;
}

/* PAGE, NUMPAGES, DATE and TIME are our fields; 1 if instr is one. */
static int special_field(const char *instr, owf_field *field)
{
    char word[16];
    size_t n = 0;
    while (*instr == ' ')
        instr++;
    while (*instr && *instr != ' ' && *instr != '\\' && n < sizeof word - 1)
        word[n++] = (char)toupper((unsigned char)*instr++);
    word[n] = 0;
    if (!strcmp(word, "PAGE")) *field = OWF_FIELD_PAGE;
    else if (!strcmp(word, "NUMPAGES") || !strcmp(word, "SECTIONPAGES")) *field = OWF_FIELD_PAGES;
    else if (!strcmp(word, "DATE") || !strcmp(word, "CREATEDATE") || !strcmp(word, "SAVEDATE") ||
             !strcmp(word, "PRINTDATE")) *field = OWF_FIELD_DATE;
    else if (!strcmp(word, "TIME")) *field = OWF_FIELD_TIME;
    else return 0;
    return 1;
}

static void note_text(docx *d, const owf_xml_node *n, owf_buf *out)
{
    const owf_xml_node *c;
    for (c = n->first; c; c = c->next) {
        if (is_w(d, c, "t") && c->first && c->first->type == OWF_XML_TEXT)
            owf_buf_put(out, c->first->name, c->first->length);
        else if (is_w(d, c, "tab"))
            owf_buf_putc(out, ' ');
        else if (is_w(d, c, "p") && out->length && out->data[out->length - 1] != '\t' && out->data[out->length - 1] != ' ')
            (owf_buf_putc(out, ' '), note_text(d, c, out));
        else if (c->type == OWF_XML_ELEMENT && !is_w(d, c, "del") && !is_w(d, c, "footnoteRef") && !is_w(d, c, "endnoteRef"))
            note_text(d, c, out);
    }
}

static void note(docx *d, const owf_xml_node *ref, owf_xml *part, const char *element, const owf_charfmt *fmt)
{
    const char *id = owf_xml_attr(ref, d->w, "id");
    const owf_xml_node *n;
    owf_charfmt f = *fmt;
    char mark[16];

    d->nnotes++;
    snprintf(mark, sizeof mark, "%d", d->nnotes);
    f.flags |= OWF_SUPER;
    set_fmt(d, &f);
    owf_builder_text(d->b, mark, strlen(mark));
    owf_report_add(d->report, OWF_NOTE_APPROX, "Footnotes and endnotes are listed at the end of the document");
    if (!part || !id)
        return;
    for (n = owf_xml_root(part)->first; n; n = n->next) {
        const char *nid = owf_xml_attr(n, d->w, "id");
        if (is_w(d, n, element) && nid && !strcmp(nid, id)) {
            size_t start;
            owf_buf_printf(&d->notes, "%d\t", d->nnotes);
            start = d->notes.length;
            note_text(d, n, &d->notes);
            owf_buf_trim_from(&d->notes, start);
            owf_buf_putc(&d->notes, '\n');
            break;
        }
    }
}

static void run_children(docx *d, const owf_xml_node *r, const owf_charfmt *fmt);
static void start_para(docx *d, const owf_parafmt *pf);

static void run_child(docx *d, const owf_xml_node *c, const owf_charfmt *fmt)
{
    if (c->type != OWF_XML_ELEMENT)
        return;
    if (owf_xml_is(c, NS_MC, "AlternateContent")) {
        /* The same content twice: newer markup, and a fallback. */
        const owf_xml_node *fb = owf_xml_child(c, NS_MC, "Fallback");
        if (fb)
            run_children(d, fb, fmt);
        return;
    }
    if (strcmp(c->ns, d->w)) {
        if (!strcmp(c->ns, NS_MATH) && !strcmp(c->name, "t") && c->first && c->first->type == OWF_XML_TEXT) {
            set_fmt(d, fmt);
            owf_builder_text(d->b, c->first->name, c->first->length);
            owf_report_add(d->report, OWF_NOTE_APPROX, "Equations are kept as plain text");
        } else if (!strcmp(c->name, "drawing") || !strcmp(c->name, "pict") || !strcmp(c->name, "object")) {
            owf_report_add(d->report, OWF_NOTE_LOST, "Pictures and drawings are not brought across yet");
        } else {
            run_children(d, c, fmt);
        }
        return;
    }
    if (!strcmp(c->name, "t")) {
        if (!hiding(d) && c->first && c->first->type == OWF_XML_TEXT) {
            set_fmt(d, fmt);
            owf_builder_text(d->b, c->first->name, c->first->length);
        }
    } else if (!strcmp(c->name, "instrText")) {
        if (d->nfields && d->fields[d->nfields - 1].code && c->first && c->first->type == OWF_XML_TEXT) {
            field_state *f = &d->fields[d->nfields - 1];
            size_t have = strlen(f->instr), n = c->first->length;
            if (have + n >= sizeof f->instr)
                n = sizeof f->instr - 1 - have;
            memcpy(f->instr + have, c->first->name, n);
            f->instr[have + n] = 0;
        }
    } else if (!strcmp(c->name, "fldChar")) {
        const char *type = owf_xml_attr(c, d->w, "fldCharType");
        if (type && !strcmp(type, "begin")) {
            if (d->nfields < MAX_FIELDS) {
                memset(&d->fields[d->nfields], 0, sizeof d->fields[0]);
                d->fields[d->nfields].code = 1;
            }
            d->nfields++;
        } else if (type && !strcmp(type, "separate") && d->nfields && d->nfields <= MAX_FIELDS) {
            field_state *f = &d->fields[d->nfields - 1];
            f->code = 0;
            if (special_field(f->instr, &f->field)) {
                f->hide = 1;
                if (!hiding(d) || d->nfields == 1) {
                    set_fmt(d, fmt);
                    owf_builder_special(d->b, OWF_RUN_FIELD, f->field);
                }
            }
        } else if (type && !strcmp(type, "end") && d->nfields) {
            if (d->nfields <= MAX_FIELDS) {
                field_state *f = &d->fields[d->nfields - 1];
                /* A field without a result: show ours if it is one. */
                if (f->code && special_field(f->instr, &f->field)) {
                    d->nfields--;
                    if (!hiding(d)) {
                        set_fmt(d, fmt);
                        owf_builder_special(d->b, OWF_RUN_FIELD, f->field);
                    }
                    return;
                }
            }
            d->nfields--;
        }
    } else if (hiding(d)) {
        /* inside a field's code or our field's result */
    } else if (!strcmp(c->name, "tab") || !strcmp(c->name, "ptab")) {
        set_fmt(d, fmt);
        owf_builder_special(d->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
    } else if (!strcmp(c->name, "br")) {
        const char *type = owf_xml_attr(c, d->w, "type");
        if (type && !strcmp(type, "page")) {
            owf_parafmt pf;
            if (d->b->para || d->b->text.length) {
                pf = d->b->para ? d->b->para->fmt : d->b->parafmt;
                owf_builder_end_para(d->b);
                pf.page_break_before = 1;
                start_para(d, &pf);
            } else {
                d->b->parafmt.page_break_before = 1;
            }
        } else {
            set_fmt(d, fmt);
            owf_builder_special(d->b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
        }
    } else if (!strcmp(c->name, "cr")) {
        set_fmt(d, fmt);
        owf_builder_special(d->b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
    } else if (!strcmp(c->name, "noBreakHyphen")) {
        set_fmt(d, fmt);
        owf_builder_text(d->b, "\xe2\x80\x91", 3);
    } else if (!strcmp(c->name, "sym")) {
        const char *ch = owf_xml_attr(c, d->w, "char");
        const char *font = owf_xml_attr(c, d->w, "font");
        unsigned long u = ch ? strtoul(ch, NULL, 16) : 0;
        if (u) {
            if (font && !strcmp(font, "Symbol"))
                u = owf_symbol_char((unsigned)u);
            else if (u >= 0xF000 && u <= 0xF0FF) {
                u = 0xFFFD;
                owf_report_add(d->report, OWF_NOTE_APPROX, "Characters from picture fonts (Wingdings and others) became %s", "\xef\xbf\xbd");
            }
            set_fmt(d, fmt);
            d->scratch.length = 0;
            owf_buf_put_utf8(&d->scratch, u);
            if (!d->scratch.failed)
                owf_builder_text(d->b, (const char *)d->scratch.data, d->scratch.length);
        }
    } else if (!strcmp(c->name, "footnoteReference")) {
        note(d, c, d->footnotes, "footnote", fmt);
    } else if (!strcmp(c->name, "endnoteReference")) {
        note(d, c, d->endnotes, "endnote", fmt);
    } else if (!strcmp(c->name, "commentReference")) {
        owf_report_add(d->report, OWF_NOTE_LOST, "Comments were left out");
    } else if (!strcmp(c->name, "drawing") || !strcmp(c->name, "pict") || !strcmp(c->name, "object")) {
        owf_report_add(d->report, OWF_NOTE_LOST, "Pictures and drawings are not brought across yet");
    } else if (!strcmp(c->name, "rPr") || !strcmp(c->name, "delText") || !strcmp(c->name, "delInstrText") ||
               !strcmp(c->name, "lastRenderedPageBreak") || !strcmp(c->name, "softHyphen") ||
               !strcmp(c->name, "footnoteRef") || !strcmp(c->name, "endnoteRef") || !strcmp(c->name, "separator") ||
               !strcmp(c->name, "continuationSeparator")) {
        /* nothing to show */
    } else {
        run_children(d, c, fmt);
    }
}

static void run_children(docx *d, const owf_xml_node *r, const owf_charfmt *fmt)
{
    const owf_xml_node *c;
    for (c = r->first; c; c = c->next)
        run_child(d, c, fmt);
}

static void para_content(docx *d, const owf_xml_node *p, const owf_charfmt *para_char);

static void run(docx *d, const owf_xml_node *r, const owf_charfmt *para_char)
{
    owf_charfmt f = *para_char;
    const owf_xml_node *rpr = wchild(d, r, "rPr");
    const owf_xml_node *rstyle = wchild(d, rpr, "rStyle");
    owf_charprops cp;

    if (rstyle)
        owf_styles_resolve(d->tables, 1, wval(d, rstyle), OWF_FAMILY_TEXT, NULL, &f);
    memset(&cp, 0, sizeof cp);
    read_rpr(d, rpr, &cp);
    owf_charprops_apply(&f, &cp);
    run_children(d, r, &f);
}

/* Runs and the containers runs live in. */
static void para_content(docx *d, const owf_xml_node *p, const owf_charfmt *para_char)
{
    const owf_xml_node *c;
    for (c = p->first; c; c = c->next) {
        if (c->type != OWF_XML_ELEMENT)
            continue;
        if (is_w(d, c, "r")) {
            run(d, c, para_char);
        } else if (is_w(d, c, "pPr") || is_w(d, c, "del") || is_w(d, c, "moveFrom") ||
                   is_w(d, c, "bookmarkStart") || is_w(d, c, "bookmarkEnd") || is_w(d, c, "proofErr") ||
                   is_w(d, c, "permStart") || is_w(d, c, "permEnd") || is_w(d, c, "commentRangeStart") ||
                   is_w(d, c, "commentRangeEnd")) {
            /* deleted text and marks */
        } else if (is_w(d, c, "fldSimple")) {
            owf_field field;
            const char *instr = owf_xml_attr(c, d->w, "instr");
            if (instr && special_field(instr, &field)) {
                set_fmt(d, para_char);
                owf_builder_special(d->b, OWF_RUN_FIELD, field);
            } else {
                para_content(d, c, para_char);
            }
        } else if (!strcmp(c->ns, NS_MATH)) {
            run_children(d, c, para_char);
        } else if (owf_xml_is(c, NS_MC, "AlternateContent")) {
            const owf_xml_node *fb = owf_xml_child(c, NS_MC, "Fallback");
            if (fb)
                para_content(d, fb, para_char);
        } else {
            /* hyperlink, ins, moveTo, smartTag, sdt, sdtContent, customXml... */
            para_content(d, c, para_char);
        }
    }
}

static void start_para(docx *d, const owf_parafmt *pf)
{
    if (d->b->para || d->b->text.length)
        owf_builder_end_para(d->b);
    d->b->parafmt = *pf;
    if (d->break_next) {
        d->b->parafmt.page_break_before = 1;
        d->break_next = 0;
    }
}

static void list_prefix(docx *d, int li, int level, owf_buf *out)
{
    const owf_list_style *ls = &d->lists[li];
    const owf_list_level *ll = &ls->levels[level];
    const char *t;

    out->length = 0;
    if (ll->format == OWF_NUMBER_BULLET) {
        owf_buf_puts(out, ll->text);
        return;
    }
    if (ll->format == OWF_NUMBER_NONE)
        return;
    for (t = ll->text; *t; t++) {
        if (t[0] == '%' && t[1] >= '1' && t[1] <= '9') {
            int k = t[1] - '1';
            owf_number_format f = ls->levels[k].format;
            owf_format_number(out, d->counters[li][k], f == OWF_NUMBER_BULLET || f == OWF_NUMBER_NONE ? OWF_NUMBER_DECIMAL : f);
            t++;
        } else {
            owf_buf_putc(out, *t);
        }
    }
}

static void paragraph(docx *d, const owf_xml_node *p)
{
    const owf_xml_node *ppr = wchild(d, p, "pPr");
    const owf_xml_node *pstyle = wchild(d, ppr, "pStyle");
    const char *style = pstyle ? wval(d, pstyle) : d->default_para;
    const char *num_id = NULL;
    const owf_style *st;
    int num_level = 0, i;
    owf_parafmt pf = d->base_para;
    owf_charfmt cf = d->base_char;
    owf_paraprops pp;

    owf_styles_resolve(d->tables, 1, style, OWF_FAMILY_PARAGRAPH, &pf, &cf);
    /* A list from the style, then from the paragraph. */
    for (st = owf_styles_lookup(d->tables, 1, style, OWF_FAMILY_PARAGRAPH), i = 0; st && i < 16;
         st = owf_styles_lookup(d->tables, 1, st->parent, OWF_FAMILY_PARAGRAPH), i++)
        if (st->list) {
            num_id = st->list;
            if (st->list_level >= 0)
                num_level = st->list_level;
            break;
        }
    memset(&pp, 0, sizeof pp);
    read_ppr(d, ppr, &pp, &num_id, &num_level);
    owf_paraprops_apply(&pf, &pp);

    {
        int li = list_index(d, num_id);
        if (num_id && strcmp(num_id, "0") && li >= 0) {
            int k;
            if (num_level < 0 || num_level >= OWF_LIST_LEVELS)
                num_level = 0;
            if (!d->counters[li][num_level])
                d->counters[li][num_level] = d->lists[li].levels[num_level].start - 1;
            d->counters[li][num_level]++;
            for (k = num_level + 1; k < OWF_LIST_LEVELS; k++)
                d->counters[li][k] = 0;
            if (!(pp.mask & OWF_PP_LEFT) && d->lists[li].levels[num_level].indent)
                pf.indent_left = d->lists[li].levels[num_level].indent;
            list_prefix(d, li, num_level, &d->scratch);
            start_para(d, &pf);
            if (!d->scratch.failed && d->scratch.length) {
                set_fmt(d, &cf);
                owf_builder_text(d->b, (const char *)d->scratch.data, d->scratch.length);
                owf_builder_special(d->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
            }
            owf_report_add(d->report, OWF_NOTE_APPROX, "Lists are kept as text with their numbers and bullets");
        } else {
            start_para(d, &pf);
        }
    }
    /* Word measures tabs from the margin; we measure from the indent. */
    for (i = 0; i < d->b->parafmt.ntabs; i++) {
        d->b->parafmt.tabs[i].position -= d->b->parafmt.indent_left;
        if (d->b->parafmt.tabs[i].position < 0)
            d->b->parafmt.tabs[i].position = 0;
    }
    para_content(d, p, &cf);
    owf_builder_end_para(d->b);
    if (wchild(d, ppr, "sectPr")) {
        d->break_next = 1;
        owf_report_add(d->report, OWF_NOTE_APPROX, "Section breaks became page breaks");
    }
}

static void blocks(docx *d, const owf_xml_node *n);

static void cell_content(docx *d, const owf_xml_node *n, int *first)
{
    const owf_xml_node *c;
    for (c = n->first; c; c = c->next) {
        if (is_w(d, c, "p")) {
            const owf_xml_node *pstyle = wchild(d, wchild(d, c, "pPr"), "pStyle");
            owf_parafmt pf = d->base_para;
            owf_charfmt cf = d->base_char;
            owf_styles_resolve(d->tables, 1, pstyle ? wval(d, pstyle) : d->default_para, OWF_FAMILY_PARAGRAPH, &pf, &cf);
            if (!*first) {
                set_fmt(d, &cf);
                owf_builder_text(d->b, " ", 1);
            }
            *first = 0;
            para_content(d, c, &cf);
        } else if (c->type == OWF_XML_ELEMENT && !is_w(d, c, "tcPr")) {
            cell_content(d, c, first);
        }
    }
}

static void table(docx *d, const owf_xml_node *t)
{
    const owf_xml_node *row, *cell;
    owf_report_add(d->report, OWF_NOTE_APPROX, "Tables are kept as rows of text with tabs between the cells");
    for (row = t->first; row; row = row->next) {
        int ncell = 0;
        if (!is_w(d, row, "tr")) {
            if (is_w(d, row, "sdt") || is_w(d, row, "customXml"))
                table(d, wchild(d, row, "sdtContent") ? wchild(d, row, "sdtContent") : row);
            continue;
        }
        start_para(d, &d->base_para);
        for (cell = row->first; cell; cell = cell->next) {
            int first = 1;
            if (!is_w(d, cell, "tc"))
                continue;
            if (ncell++)
                owf_builder_special(d->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
            cell_content(d, cell, &first);
        }
        owf_builder_end_para(d->b);
    }
}

static void blocks(docx *d, const owf_xml_node *n)
{
    const owf_xml_node *c;
    for (c = n->first; c; c = c->next) {
        if (is_w(d, c, "p"))
            paragraph(d, c);
        else if (is_w(d, c, "tbl"))
            table(d, c);
        else if (is_w(d, c, "sdt") || is_w(d, c, "sdtContent") || is_w(d, c, "customXml") ||
                 is_w(d, c, "ins") || is_w(d, c, "moveTo"))
            blocks(d, c);
    }
}

static void story(docx *d, const char *id, owf_story *into)
{
    const char *target = rel_by_id(&d->doc_rels, id);
    char name[256];
    owf_xml *part;
    owf_builder b;

    if (!target)
        return;
    part_name(name, sizeof name, d->base, target);
    part = read_xml(d->zip, name);
    if (!part)
        return;
    owf_builder_init(&b, d->doc, into);
    d->b = &b;
    blocks(d, owf_xml_root(part));
    if (owf_builder_done(&b) != OWF_OK)
        d->failed = 1;
    owf_xml_free(part);
}

static void section(docx *d, const owf_xml_node *sect)
{
    owf_page *pg = &d->doc->page;
    const owf_xml_node *n;
    const char *head_default = NULL, *head_first = NULL, *foot_default = NULL, *foot_first = NULL;
    int v, title_page = 0;

    if (!sect)
        return;
    if ((n = wchild(d, sect, "pgSz"))) {
        if (wint(d, n, "w", &v) && v > 0) pg->width = v;
        if (wint(d, n, "h", &v) && v > 0) pg->height = v;
    }
    if ((n = wchild(d, sect, "pgMar"))) {
        if (wint(d, n, "top", &v)) pg->margin_top = v < 0 ? -v : v;
        if (wint(d, n, "bottom", &v)) pg->margin_bottom = v < 0 ? -v : v;
        if (wint(d, n, "left", &v) || wint(d, n, "start", &v)) pg->margin_left = v;
        if (wint(d, n, "right", &v) || wint(d, n, "end", &v)) pg->margin_right = v;
    }
    if ((n = wchild(d, sect, "pgNumType"))) {
        const char *fmt = owf_xml_attr(n, d->w, "fmt");
        if (wint(d, n, "start", &v))
            pg->start_page = v;
        switch (number_format(fmt)) {
        case OWF_NUMBER_UPPER_ROMAN: pg->pagenum_style = OWF_PAGENUM_ROMAN_UPPER; break;
        case OWF_NUMBER_LOWER_ROMAN: pg->pagenum_style = OWF_PAGENUM_ROMAN_LOWER; break;
        case OWF_NUMBER_UPPER_LETTER: pg->pagenum_style = OWF_PAGENUM_LETTER_UPPER; break;
        case OWF_NUMBER_LOWER_LETTER: pg->pagenum_style = OWF_PAGENUM_LETTER_LOWER; break;
        default: pg->pagenum_style = OWF_PAGENUM_ARABIC; break;
        }
    }
    if ((n = wchild(d, sect, "titlePg")))
        title_page = on_off(d, n);
    for (n = sect->first; n; n = n->next) {
        const char *type = owf_xml_attr(n, d->w, "type");
        const char *id = attr_any(n, "id");
        int head = is_w(d, n, "headerReference");
        if (!head && !is_w(d, n, "footerReference"))
            continue;
        if (!type || !strcmp(type, "default")) {
            if (head) head_default = id; else foot_default = id;
        } else if (!strcmp(type, "first")) {
            if (head) head_first = id; else foot_first = id;
        } else {
            owf_report_add(d->report, OWF_NOTE_APPROX, "Different headers and footers on left and right pages became one");
        }
    }
    story(d, head_default, &d->doc->header);
    story(d, foot_default, &d->doc->footer);
    if (title_page) {
        const char *hd = rel_by_id(&d->doc_rels, head_default), *hf = rel_by_id(&d->doc_rels, head_first);
        const char *fd = rel_by_id(&d->doc_rels, foot_default), *ff = rel_by_id(&d->doc_rels, foot_first);
        pg->header_on_first = hf && hd && !strcmp(hf, hd);
        pg->footer_on_first = ff && fd && !strcmp(ff, fd);
        if ((hf && hd && strcmp(hf, hd)) || (ff && fd && strcmp(ff, fd)))
            owf_report_add(d->report, OWF_NOTE_APPROX, "A different header or footer on the first page was left out");
    }
}

/* ---- Reading ---- */

static int read_docx(docx *d)
{
    rels package;
    const char *main_target, *slash;
    char main_name[256], rels_name[300];
    owf_xml *document;
    const owf_xml_node *root, *body, *n;
    owf_builder b;
    int result;

    read_rels(d->zip, "_rels/.rels", &package);
    main_target = rel_by_type(&package, "/officeDocument");
    snprintf(main_name, sizeof main_name, "%s", main_target ? (main_target[0] == '/' ? main_target + 1 : main_target)
                                                             : "word/document.xml");
    free_rels(&package);
    slash = strrchr(main_name, '/');
    snprintf(d->base, sizeof d->base, "%.*s", slash ? (int)(slash - main_name + 1) : 0, main_name);
    snprintf(rels_name, sizeof rels_name, "%.63s_rels/%.200s.rels", d->base, slash ? slash + 1 : main_name);

    document = read_xml(d->zip, main_name);
    if (!document)
        return OWF_ERR_CORRUPT;
    root = owf_xml_root(document);
    d->w = root->ns;
    if (strcmp(root->name, "document") || (strcmp(d->w, NS_W) && strcmp(d->w, NS_W_STRICT))) {
        owf_xml_free(document);
        return OWF_ERR_FORMAT;
    }
    read_rels(d->zip, rels_name, &d->doc_rels);
    d->styles_xml = read_related(d, "/styles");
    d->theme = read_related(d, "/theme");
    d->numbering = read_related(d, "/numbering");
    d->footnotes = read_related(d, "/footnotes");
    d->endnotes = read_related(d, "/endnotes");

    owf_parafmt_init(&d->base_para);
    owf_charfmt_init(&d->base_char);
    d->base_char.size = 10 * OWF_TWIPS_PER_POINT;    /* Word's own default */
    read_theme(d);
    read_styles(d);
    read_numbering(d);
    d->tables[0] = &d->styles;
    if (d->base_char.font < 0) {
        const char *name = d->minor_font[0] ? d->minor_font : "Times New Roman";
        owf_font_kind kind = OWF_FONT_ANY;
        owf_font_modern(name, &kind);
        d->base_char.font = owf_doc_font(d->doc, name, kind);
    }
    d->doc->base.font = d->base_char.font;
    d->doc->base.size = d->base_char.size;
    owf_buf_init(&d->notes);
    owf_buf_init(&d->scratch);

    body = wchild(d, root, "body");
    owf_builder_init(&b, d->doc, &d->doc->body);
    d->b = &b;
    if (body)
        blocks(d, body);
    for (n = body ? body->first : NULL; n; n = n->next)
        if (is_w(d, n, "sectPr"))
            break;
    if (d->nnotes && !d->notes.failed) {
        const char *p = (const char *)d->notes.data, *end = p + d->notes.length;
        owf_parafmt pf;
        owf_parafmt_init(&pf);
        pf.heading = 2;
        start_para(d, &pf);
        owf_charfmt_init(&b.charfmt);
        owf_builder_text(&b, "Notes", 5);
        owf_builder_end_para(&b);
        owf_parafmt_init(&b.parafmt);
        while (p < end) {
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            const char *tab;
            if (!nl)
                nl = end;
            tab = memchr(p, '\t', (size_t)(nl - p));
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
    section(d, n);    /* headers and footers use their own builders */

    if (d->failed || d->notes.failed || d->scratch.failed)
        result = OWF_ERR_MEMORY;
    owf_buf_free(&d->notes);
    owf_buf_free(&d->scratch);
    owf_xml_free(document);
    return result;
}

static int import_docx(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_unzip zip;
    docx *d;
    int result;

    if (!owf_unzip_open(&zip, data, length))
        return OWF_ERR_FORMAT;
    d = calloc(1, sizeof *d);
    if (!d)
        return OWF_ERR_MEMORY;
    d->doc = doc;
    d->report = report;
    d->zip = &zip;
    result = read_docx(d);
    owf_styles_free(&d->styles);
    free_rels(&d->doc_rels);
    owf_xml_free(d->styles_xml);
    owf_xml_free(d->theme);
    owf_xml_free(d->numbering);
    owf_xml_free(d->footnotes);
    owf_xml_free(d->endnotes);
    free(d);
    return result;
}

static int detect_docx(const unsigned char *data, size_t length)
{
    owf_unzip zip;
    if (!owf_unzip_open(&zip, data, length))
        return 0;
    if (owf_unzip_has(&zip, "word/document.xml"))
        return 95;
    if (owf_unzip_has(&zip, "[Content_Types].xml") && owf_unzip_has(&zip, "_rels/.rels"))
        return 50;   /* some other Office file, or a DOCX with its parts elsewhere */
    return 0;
}

int owf_import_docx(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    return import_docx(data, length, doc, report);
}

int owf_detect_docx(const unsigned char *data, size_t length)
{
    return detect_docx(data, length);
}

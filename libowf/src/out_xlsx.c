/*
 * libowf: Excel workbooks in Office Open XML (.xlsx), written to ECMA-376
 * (transitional), as Excel 2007 and later, LibreOffice and the others read
 * them. Everything the model holds goes in: the sheets, their cells with
 * values and formulas (Excel's newer functions marked "_xlfn.", array
 * formulas over their ranges, other books through externalLink parts),
 * the styles (fonts, number formats, alignment, wrap, fills, borders),
 * column widths, row heights, hidden rows, columns and sheets, merged
 * cells, frozen panes, defined names, the active sheet and the 1904 date
 * system. Text goes in the shared strings, once each.
 *
 * Each sheet's XML is made and packed in turn, so only one sheet's text is
 * in memory beside the file.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_MAIN "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
#define NS_R "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
#define REL "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
#define CT "application/vnd.openxmlformats-officedocument.spreadsheetml."
#define XML_HEAD "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"

#define MAX_ROWS 1048576UL
#define MAX_COLS 16384UL
#define MAX_TEXT 32767UL                 /* characters in a cell, Excel's limit */

/* ---- XML text ---- */

enum { X_TEXT, X_CELL, X_ATTR };

/* UTF-8 text for XML: &, <, > (and " in attributes) escaped; carriage
 * returns kept as a character reference; characters XML 1.0 cannot carry
 * as Excel's "_xHHHH_" in cell text (whose own "_x" look-alikes get
 * "_x005F_"), and dropped elsewhere; tabs and line feeds in attributes as
 * references; bad UTF-8 as U+FFFD. */
static void put_text(owf_buf *b, const char *s, size_t n, int mode)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + n;
    while (p < end) {
        unsigned c = *p;
        if (c >= 0x80) {
            const unsigned char *start = p;
            unsigned long cp = owf_utf8_next(&p, end);
            if (cp == 0xFFFD && !(p - start == 3 && start[0] == 0xEF && start[1] == 0xBF && start[2] == 0xBD))
                owf_buf_puts(b, "\xEF\xBF\xBD");
            else if (cp == 0xFFFE || cp == 0xFFFF)
                ;                        /* not characters XML allows */
            else
                owf_buf_put(b, start, (size_t)(p - start));
            continue;
        }
        p++;
        switch (c) {
        case '&': owf_buf_puts(b, "&amp;"); break;
        case '<': owf_buf_puts(b, "&lt;"); break;
        case '>': owf_buf_puts(b, "&gt;"); break;
        case '"':
            owf_buf_puts(b, mode == X_ATTR ? "&quot;" : "\"");
            break;
        case '\r': owf_buf_puts(b, mode == X_CELL ? "_x000D_" : "&#13;"); break;
        case '\t':
        case '\n':
            if (mode == X_ATTR)
                owf_buf_printf(b, "&#%u;", c);
            else
                owf_buf_putc(b, (int)c);
            break;
        case '_':
            if (mode == X_CELL && end - p >= 6 && p[0] == 'x' && p[5] == '_') {
                int k, hex = 1;
                for (k = 1; k < 5; k++)
                    hex &= (p[k] >= '0' && p[k] <= '9') || (p[k] >= 'a' && p[k] <= 'f') || (p[k] >= 'A' && p[k] <= 'F');
                if (hex) {
                    owf_buf_puts(b, "_x005F_");
                    break;
                }
            }
            owf_buf_putc(b, '_');
            break;
        default:
            if (c < 0x20) {
                if (mode == X_CELL)
                    owf_buf_printf(b, "_x%04X_", c);
            } else {
                owf_buf_putc(b, (int)c);
            }
            break;
        }
    }
}

static void put_xml(owf_buf *b, const char *s, int mode)
{
    put_text(b, s, strlen(s), mode);
}

/* Cell text to Excel's limit: whole characters, at most 32767. */
static size_t text_length(const char *s, int *cut)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned long chars = 0;
    *cut = 0;
    while (*p) {
        unsigned long units = *p >= 0xF0 ? 2 : 1;   /* as UTF-16 counts them */
        const unsigned char *next = p + 1;
        while (*next >= 0x80 && *next < 0xC0)
            next++;
        if (chars + units > MAX_TEXT) {
            *cut = 1;
            break;
        }
        chars += units;
        p = next;
    }
    return (size_t)(p - (const unsigned char *)s);
}

static void put_ref(owf_buf *b, unsigned long row, unsigned long col)
{
    char name[8];
    owf_column_name((unsigned)col, name);
    owf_buf_printf(b, "%s%lu", name, row + 1);
}

static void put_number(owf_buf *b, double v)
{
    char num[40];
    owf_number_exact(v, num);
    owf_buf_puts(b, num);
}

/* ---- The writer's state ---- */

typedef struct {
    int font;                    /* the book's, or -1 */
    int size;
    unsigned flags;
    unsigned long colour;
} xfont;

typedef struct {
    unsigned char line[4];
    unsigned long colour[4];
} xborder;

typedef struct {
    const owf_book *book;
    owf_report *report;
    owf_xl_links links;
    /* styles: each of the book's has an xf of the same number */
    xfont *fonts;
    int nfonts;
    int *xf_font, *xf_fill, *xf_border;
    unsigned long *fills;        /* fills[0] none, [1] gray125, then solid colours */
    int nfills;
    xborder *borders;
    int nborders;
    int *numfmt_id;              /* the book's number formats' ids */
    /* shared strings: the cells' texts, once each */
    const char **strings;
    unsigned long nstrings, capstrings;
    unsigned long *slots;        /* index + 1, 0 empty */
    unsigned long nslots;
    unsigned long text_cells;
    char **names;                /* the sheets' names as written */
    int failed;
} xlsx_out;

static int same_font(const xfont *a, const xfont *b)
{
    return a->font == b->font && a->size == b->size && a->flags == b->flags && a->colour == b->colour;
}

static int same_border(const xborder *a, const xborder *b)
{
    int i;
    for (i = 0; i < 4; i++)
        if (a->line[i] != b->line[i] || (a->line[i] && a->colour[i] != b->colour[i]))
            return 0;
    return 1;
}

static void build_styles(xlsx_out *w)
{
    const owf_book *book = w->book;
    int n = book->nstyles, i, k, next = 164;
    int base_size = book->nstyles && book->styles[0].font.size > 0 ? book->styles[0].font.size : 200;
    int base_font = book->nstyles ? book->styles[0].font.font : -1;

    w->fonts = calloc((size_t)n + 1, sizeof *w->fonts);
    w->xf_font = calloc((size_t)n + 1, sizeof *w->xf_font);
    w->xf_fill = calloc((size_t)n + 1, sizeof *w->xf_fill);
    w->xf_border = calloc((size_t)n + 1, sizeof *w->xf_border);
    w->fills = calloc((size_t)n + 2, sizeof *w->fills);
    w->borders = calloc((size_t)n + 1, sizeof *w->borders);
    w->numfmt_id = calloc((size_t)book->nnumfmts + 1, sizeof *w->numfmt_id);
    if (!w->fonts || !w->xf_font || !w->xf_fill || !w->xf_border || !w->fills || !w->borders || !w->numfmt_id) {
        w->failed = 1;
        return;
    }
    w->nfills = 2;
    w->nborders = 1;
    for (i = 0; i < n; i++) {
        const owf_cellstyle *s = &book->styles[i];
        xfont f;
        xborder b;
        f.font = s->font.font >= 0 && s->font.font < book->nfonts ? s->font.font : base_font;
        f.size = s->font.size > 0 ? s->font.size : base_size;
        f.flags = s->font.flags;
        f.colour = s->font.colour;
        for (k = 0; k < w->nfonts && !same_font(&w->fonts[k], &f); k++)
            ;
        if (k == w->nfonts)
            w->fonts[w->nfonts++] = f;
        w->xf_font[i] = k;
        if (OWF_SHADE_SET(s->fill)) {
            for (k = 2; k < w->nfills && w->fills[k] != s->fill; k++)
                ;
            if (k == w->nfills)
                w->fills[w->nfills++] = s->fill;
            w->xf_fill[i] = k;
        }
        memset(&b, 0, sizeof b);
        for (k = 0; k < 4; k++) {
            b.line[k] = s->border[k] <= OWF_LINE_SLANTED_DASH_DOT ? s->border[k] : OWF_LINE_THIN;
            b.colour[k] = b.line[k] ? s->border_colour[k] : 0;
        }
        for (k = 0; k < w->nborders && !same_border(&w->borders[k], &b); k++)
            ;
        if (k == w->nborders)
            w->borders[w->nborders++] = b;
        w->xf_border[i] = k;
    }
    for (i = 0; i < book->nnumfmts; i++) {
        unsigned id;
        w->numfmt_id[i] = -1;
        for (id = 0; id < 50; id++)
            if (owf_xl_builtin_format(id) && !strcmp(owf_xl_builtin_format(id), book->numfmts[i])) {
                w->numfmt_id[i] = (int)id;
                break;
            }
        if (w->numfmt_id[i] < 0)
            w->numfmt_id[i] = next++;
    }
}

static const char *const lines[14] = { "none", "thin", "medium", "dashed", "dotted", "thick", "double", "hair",
                                       "mediumDashed", "dashDot", "mediumDashDot", "dashDotDot",
                                       "mediumDashDotDot", "slantDashDot" };

static void put_rgb(owf_buf *b, const char *element, unsigned long rgb)
{
    owf_buf_printf(b, "<%s rgb=\"FF%06lX\"/>", element, rgb & 0xFFFFFFUL);
}

/* ---- Conditional formatting's looks: the styles' dxfs, one for each rule with a look, sheet by sheet ---- */

static int cf_has_look(const owf_condfmt *cf)
{
    return cf->font_set || OWF_SHADE_SET(cf->font_colour) || OWF_SHADE_SET(cf->fill);
}

static int cf_writable(const owf_condfmt *cf)
{
    return cf->nranges > 0 && (cf->type != OWF_CF_KEPT || cf->xml);
}

/* The dxf of sheet's rule k (-1: none): the looks before it counted. */
static int cf_dxf(const owf_book *book, int sheet, int k)
{
    int i, j, n = 0;
    for (i = 0; i <= sheet; i++)
        for (j = 0; j < book->sheets[i].ncondfmts && (i < sheet || j < k); j++)
            n += cf_writable(&book->sheets[i].condfmts[j]) && cf_has_look(&book->sheets[i].condfmts[j]);
    return cf_writable(&book->sheets[sheet].condfmts[k]) && cf_has_look(&book->sheets[sheet].condfmts[k]) ? n : -1;
}

static void dxfs_xml(xlsx_out *w, owf_buf *b)
{
    static const struct { unsigned flag; const char *on, *off; } parts[4] = {
        { OWF_BOLD, "<b/>", "<b val=\"0\"/>" }, { OWF_ITALIC, "<i/>", "<i val=\"0\"/>" },
        { OWF_STRIKE, "<strike/>", "<strike val=\"0\"/>" }, { OWF_UNDERLINE, "<u/>", "<u val=\"none\"/>" }
    };
    const owf_book *book = w->book;
    int i, j, k, n = 0;
    for (i = 0; i < book->nsheets; i++)
        for (j = 0; j < book->sheets[i].ncondfmts; j++)
            n += cf_writable(&book->sheets[i].condfmts[j]) && cf_has_look(&book->sheets[i].condfmts[j]);
    owf_buf_printf(b, "<dxfs count=\"%d\">", n);
    for (i = 0; i < book->nsheets; i++)
        for (j = 0; j < book->sheets[i].ncondfmts; j++) {
            const owf_condfmt *cf = &book->sheets[i].condfmts[j];
            if (!cf_writable(cf) || !cf_has_look(cf))
                continue;
            owf_buf_puts(b, "<dxf>");
            if (cf->font_set || OWF_SHADE_SET(cf->font_colour)) {
                owf_buf_puts(b, "<font>");
                for (k = 0; k < 4; k++)
                    if (cf->font_set & parts[k].flag)
                        owf_buf_puts(b, cf->font_flags & parts[k].flag ? parts[k].on : parts[k].off);
                if (OWF_SHADE_SET(cf->font_colour))
                    put_rgb(b, "color", OWF_SHADE_RGB(cf->font_colour));
                owf_buf_puts(b, "</font>");
            }
            if (OWF_SHADE_SET(cf->fill)) {               /* a look's solid fill is its bgColor, as Excel writes it */
                owf_buf_puts(b, "<fill><patternFill>");
                put_rgb(b, "bgColor", OWF_SHADE_RGB(cf->fill));
                owf_buf_puts(b, "</patternFill></fill>");
            }
            owf_buf_puts(b, "</dxf>");
        }
    owf_buf_puts(b, "</dxfs>");
}

static void styles_xml(xlsx_out *w, owf_buf *b)
{
    const owf_book *book = w->book;
    int i, k, custom = 0;
    for (i = 0; i < book->nnumfmts; i++)
        custom += w->numfmt_id[i] >= 164;
    owf_buf_puts(b, XML_HEAD "<styleSheet xmlns=\"" NS_MAIN "\">");
    if (custom) {
        owf_buf_printf(b, "<numFmts count=\"%d\">", custom);
        for (i = 0; i < book->nnumfmts; i++)
            if (w->numfmt_id[i] >= 164) {
                owf_buf_printf(b, "<numFmt numFmtId=\"%d\" formatCode=\"", w->numfmt_id[i]);
                put_xml(b, book->numfmts[i], X_ATTR);
                owf_buf_puts(b, "\"/>");
            }
        owf_buf_puts(b, "</numFmts>");
    }
    owf_buf_printf(b, "<fonts count=\"%d\">", w->nfonts ? w->nfonts : 1);
    if (!w->nfonts)
        owf_buf_puts(b, "<font><sz val=\"10\"/><name val=\"Arial\"/><family val=\"2\"/></font>");
    for (i = 0; i < w->nfonts; i++) {
        const xfont *f = &w->fonts[i];
        const char *name = f->font >= 0 ? book->fonts[f->font].name : "Arial";
        owf_font_kind kind = f->font >= 0 ? book->fonts[f->font].kind : OWF_FONT_SANS;
        owf_buf_puts(b, "<font>");
        if (f->flags & OWF_BOLD)
            owf_buf_puts(b, "<b/>");
        if (f->flags & OWF_ITALIC)
            owf_buf_puts(b, "<i/>");
        if (f->flags & OWF_STRIKE)
            owf_buf_puts(b, "<strike/>");
        if (f->flags & OWF_UNDERLINE)
            owf_buf_puts(b, "<u/>");
        if (f->flags & (OWF_SUPER | OWF_SUB))
            owf_buf_printf(b, "<vertAlign val=\"%s\"/>", f->flags & OWF_SUPER ? "superscript" : "subscript");
        owf_buf_puts(b, "<sz val=\"");
        put_number(b, f->size / 20.0);
        owf_buf_puts(b, "\"/>");
        if (f->colour != OWF_COLOUR_AUTO)
            put_rgb(b, "color", f->colour);
        owf_buf_puts(b, "<name val=\"");
        put_xml(b, name, X_ATTR);
        owf_buf_puts(b, "\"/>");
        if (kind == OWF_FONT_SERIF || kind == OWF_FONT_SANS || kind == OWF_FONT_MONO)
            owf_buf_printf(b, "<family val=\"%d\"/>", kind == OWF_FONT_SERIF ? 1 : kind == OWF_FONT_SANS ? 2 : 3);
        owf_buf_puts(b, "</font>");
    }
    owf_buf_printf(b, "</fonts><fills count=\"%d\"><fill><patternFill patternType=\"none\"/></fill>"
                      "<fill><patternFill patternType=\"gray125\"/></fill>", w->nfills);
    for (i = 2; i < w->nfills; i++) {
        owf_buf_puts(b, "<fill><patternFill patternType=\"solid\">");
        put_rgb(b, "fgColor", OWF_SHADE_RGB(w->fills[i]));
        owf_buf_puts(b, "<bgColor indexed=\"64\"/></patternFill></fill>");
    }
    owf_buf_printf(b, "</fills><borders count=\"%d\">", w->nborders);
    for (i = 0; i < w->nborders; i++) {
        static const char *const sides[4] = { "left", "right", "top", "bottom" };
        owf_buf_puts(b, "<border>");
        for (k = 0; k < 4; k++) {
            const xborder *x = &w->borders[i];
            if (!x->line[k]) {
                owf_buf_printf(b, "<%s/>", sides[k]);
                continue;
            }
            owf_buf_printf(b, "<%s style=\"%s\">", sides[k], lines[x->line[k]]);
            if (OWF_SHADE_SET(x->colour[k]))
                put_rgb(b, "color", OWF_SHADE_RGB(x->colour[k]));
            else
                owf_buf_puts(b, "<color auto=\"1\"/>");
            owf_buf_printf(b, "</%s>", sides[k]);
        }
        owf_buf_puts(b, "<diagonal/></border>");
    }
    owf_buf_puts(b, "</borders><cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>"
                    "</cellStyleXfs>");
    owf_buf_printf(b, "<cellXfs count=\"%d\">", book->nstyles ? book->nstyles : 1);
    if (!book->nstyles)
        owf_buf_puts(b, "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>");
    for (i = 0; i < book->nstyles; i++) {
        static const char *const h[7] = { NULL, "left", "center", "right", "fill", "justify", "centerContinuous" };
        static const char *const v[3] = { "top", "center", NULL };
        const owf_cellstyle *s = &book->styles[i];
        int fmt = s->number_format >= 0 && s->number_format < book->nnumfmts ? w->numfmt_id[s->number_format] : 0;
        int align = (s->halign && s->halign < 7) || (s->valign < 2) || s->wrap || s->indent;
        owf_buf_printf(b, "<xf numFmtId=\"%d\" fontId=\"%d\" fillId=\"%d\" borderId=\"%d\" xfId=\"0\"", fmt,
                       w->xf_font[i], w->xf_fill[i], w->xf_border[i]);
        if (fmt)
            owf_buf_puts(b, " applyNumberFormat=\"1\"");
        if (w->xf_font[i])
            owf_buf_puts(b, " applyFont=\"1\"");
        if (w->xf_fill[i])
            owf_buf_puts(b, " applyFill=\"1\"");
        if (w->xf_border[i])
            owf_buf_puts(b, " applyBorder=\"1\"");
        if (!align) {
            owf_buf_puts(b, "/>");
            continue;
        }
        owf_buf_puts(b, " applyAlignment=\"1\"><alignment");
        if (s->halign && s->halign < 7)
            owf_buf_printf(b, " horizontal=\"%s\"", h[s->halign]);
        if (s->valign < 2)
            owf_buf_printf(b, " vertical=\"%s\"", v[s->valign]);
        if (s->wrap)
            owf_buf_puts(b, " wrapText=\"1\"");
        if (s->indent)
            owf_buf_printf(b, " indent=\"%u\"", (unsigned)s->indent);
        owf_buf_puts(b, "/></xf>");
    }
    owf_buf_puts(b, "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>");
    dxfs_xml(w, b);
    owf_buf_puts(b, "<tableStyles count=\"0\"/></styleSheet>\n");
}

/* ---- Shared strings ---- */

static unsigned long hash_text(const char *s, size_t n)
{
    unsigned long h = 2166136261UL;
    size_t i;
    for (i = 0; i < n; i++)
        h = ((h ^ (unsigned char)s[i]) * 16777619UL) & 0xFFFFFFFFUL;
    return h;
}

/* The text's number in the shared strings, added if new; -1 when memory
 * ran out. The table was sized for every text cell. */
static long shared_string(xlsx_out *w, const char *s)
{
    size_t n = strlen(s);
    unsigned long h = hash_text(s, n) & (w->nslots - 1);
    while (w->slots[h]) {
        const char *t = w->strings[w->slots[h] - 1];
        if (!strcmp(t, s))
            return (long)(w->slots[h] - 1);
        h = (h + 1) & (w->nslots - 1);
    }
    if (w->nstrings == w->capstrings)
        return -1;
    w->strings[w->nstrings] = s;
    w->slots[h] = ++w->nstrings;
    return (long)(w->nstrings - 1);
}

static void build_strings(xlsx_out *w)
{
    const owf_book *book = w->book;
    int i, k;
    for (i = 0; i < book->nsheets; i++)
        for (k = 0; k < book->sheets[i].ncells; k++) {
            const owf_cell *c = &book->sheets[i].cells[k];
            if (c->type == OWF_VALUE_TEXT && !c->formula)
                w->text_cells++;
        }
    w->capstrings = w->text_cells;
    for (w->nslots = 16; w->nslots < 2 * w->text_cells; w->nslots *= 2)
        ;
    w->strings = malloc((w->capstrings + 1) * sizeof *w->strings);
    w->slots = calloc(w->nslots, sizeof *w->slots);
    if (!w->strings || !w->slots) {
        w->failed = 1;
        return;
    }
    for (i = 0; i < book->nsheets; i++)
        for (k = 0; k < book->sheets[i].ncells; k++) {
            const owf_cell *c = &book->sheets[i].cells[k];
            if (c->type == OWF_VALUE_TEXT && !c->formula)
                shared_string(w, c->text);
        }
}

static void strings_xml(xlsx_out *w, owf_buf *b)
{
    unsigned long i;
    int cut, any_cut = 0;
    owf_buf_printf(b, XML_HEAD "<sst xmlns=\"" NS_MAIN "\" count=\"%lu\" uniqueCount=\"%lu\">", w->text_cells, w->nstrings);
    for (i = 0; i < w->nstrings; i++) {
        size_t n = text_length(w->strings[i], &cut);
        any_cut |= cut;
        owf_buf_puts(b, "<si><t xml:space=\"preserve\">");
        put_text(b, w->strings[i], n, X_CELL);
        owf_buf_puts(b, "</t></si>");
    }
    owf_buf_puts(b, "</sst>\n");
    if (any_cut)
        owf_report_add(w->report, OWF_NOTE_LOST, "Text longer than Excel's 32767 characters in a cell was cut short");
}

/* ---- Sheet names, as Excel allows them ---- */

/* At most 31 characters, none of : \ / ? * [ ], not starting or ending
 * with ', and no two the same but for case. */
static void build_names(xlsx_out *w)
{
    const owf_book *book = w->book;
    int i, k, changed = 0;
    w->names = calloc((size_t)book->nsheets + 1, sizeof *w->names);
    if (!w->names) {
        w->failed = 1;
        return;
    }
    for (i = 0; i < book->nsheets; i++) {
        owf_buf clean, b;
        size_t at;
        int chars = 0, tries;
        owf_buf_init(&clean);
        owf_buf_init(&b);
        owf_buf_put_utf8_clean(&clean, book->sheets[i].name, strlen(book->sheets[i].name));
        for (at = 0; at < clean.length; at++) {
            unsigned char c = clean.data[at];
            if (c < 0x80 || c >= 0xC0) {      /* a character starts */
                if (chars == 31)
                    break;
                chars++;
            }
            if (c == ':' || c == '\\' || c == '/' || c == '?' || c == '*' || c == '[' || c == ']' || c < 0x20)
                c = '_';
            owf_buf_putc(&b, c);
        }
        owf_buf_free(&clean);
        while (b.length && b.data[b.length - 1] == '\'')
            b.length--;
        while (b.length && b.data[0] == '\'')
            memmove(b.data, b.data + 1, --b.length);
        if (!b.length)
            owf_buf_printf(&b, "Sheet%d", i + 1);
        owf_buf_putc(&b, 0);
        b.length--;
        for (tries = 2; !b.failed && tries < 1000; tries++) {
            char tail[16];
            size_t cut = b.length;
            int len, n = 0;
            for (k = 0; k < i && !owf_same_word(w->names[k], (const char *)b.data); k++)
                ;
            if (k == i)
                break;
            /* the same as one before: a number after it, as Excel's copies have */
            len = snprintf(tail, sizeof tail, " (%d)", tries);
            for (at = 0; at < b.length; at++)
                if (b.data[at] < 0x80 || b.data[at] >= 0xC0)
                    if (++n > 31 - len) {
                        cut = at;
                        break;
                    }
            b.length = cut;
            owf_buf_puts(&b, tail);
        }
        if (b.failed) {
            owf_buf_free(&b);
            w->failed = 1;
            return;
        }
        w->names[i] = (char *)b.data;
        if (strcmp(w->names[i], book->sheets[i].name))
            changed = 1;
    }
    if (changed)
        owf_report_add(w->report, OWF_NOTE_APPROX, "Some sheets were renamed as Excel allows sheet names (31 characters, none of : \\ / ? * [ ]); formulas naming them were not changed");
}

/* ---- Formulas ---- */

static char *formula_for_file(xlsx_out *w, const char *text)
{
    char *f = owf_xl_formula_for_file(text, w->book, &w->links);
    if (!f || w->links.failed)
        w->failed = 1;
    return f;
}

/* Every formula once before the parts, so that the other books are known
 * for [Content_Types].xml and the workbook. */
static void collect_links(xlsx_out *w)
{
    const owf_book *book = w->book;
    int i, k;
    for (i = 0; i < book->nsheets && !w->failed; i++)
        for (k = 0; k < book->sheets[i].ncells && !w->failed; k++)
            if (book->sheets[i].cells[k].formula)
                free(formula_for_file(w, book->sheets[i].cells[k].formula));
    for (i = 0; i < book->nnames && !w->failed; i++)
        free(formula_for_file(w, book->names[i].formula));
    for (i = 0; i < book->nsheets && !w->failed; i++)
        for (k = 0; k < book->sheets[i].ncondfmts && !w->failed; k++) {
            const owf_condfmt *cf = &book->sheets[i].condfmts[k];
            int j;
            for (j = 0; j < 2 && cf->type != OWF_CF_KEPT; j++)
                if (cf->formula[j])
                    free(formula_for_file(w, cf->formula[j]));
        }
}

/* ---- A sheet ---- */

static int same_array(const owf_sheet *sh, const owf_cell *c, unsigned long row, unsigned long col)
{
    const owf_cell *o = owf_sheet_find(sh, (unsigned)row, (unsigned)col);
    return o && (o->flags & OWF_CELL_ARRAY) && o->formula && !strcmp(o->formula, c->formula);
}

/* An array formula's range is the block of cells holding it: the first
 * cell has nothing of it to its left or above. */
static int array_range(const owf_sheet *sh, const owf_cell *c, owf_range *r)
{
    unsigned long w = 1, h = 1, k;
    if ((c->col && same_array(sh, c, c->row, c->col - 1)) || (c->row && same_array(sh, c, c->row - 1, c->col)))
        return 0;
    while (c->col + w < MAX_COLS && same_array(sh, c, c->row, c->col + w))
        w++;
    for (;;) {
        if (c->row + h >= MAX_ROWS)
            break;
        for (k = 0; k < w && same_array(sh, c, c->row + h, c->col + k); k++)
            ;
        if (k < w)
            break;
        h++;
    }
    r->first_row = c->row;
    r->first_col = c->col;
    r->last_row = (unsigned)(c->row + h - 1);
    r->last_col = (unsigned)(c->col + w - 1);
    return 1;
}

static void put_cell(xlsx_out *w, owf_buf *b, const owf_sheet *sh, const owf_cell *c)
{
    static const char *const types[5] = { NULL, NULL, "s", "b", "e" };
    const char *type = types[c->type];
    owf_range r;
    int array = (c->flags & OWF_CELL_ARRAY) && c->formula, anchor = 0;
    if (array)
        anchor = array_range(sh, c, &r);
    if (c->formula && c->type == OWF_VALUE_TEXT)
        type = "str";                    /* a formula's text result is the cell's own */
    owf_buf_puts(b, "<c r=\"");
    put_ref(b, c->row, c->col);
    owf_buf_putc(b, '"');
    if (c->style > 0 && c->style < w->book->nstyles)
        owf_buf_printf(b, " s=\"%d\"", c->style);
    if (type)
        owf_buf_printf(b, " t=\"%s\"", type);
    if (!c->formula && c->type == OWF_VALUE_EMPTY) {
        owf_buf_puts(b, "/>");
        return;
    }
    owf_buf_putc(b, '>');
    if (c->formula && (!array || anchor)) {
        char *f = formula_for_file(w, c->formula);
        if (!f)
            return;
        if (array) {
            owf_buf_puts(b, "<f t=\"array\" ref=\"");
            put_ref(b, r.first_row, r.first_col);
            if (r.last_row != r.first_row || r.last_col != r.first_col) {
                owf_buf_putc(b, ':');
                put_ref(b, r.last_row, r.last_col);
            }
            owf_buf_puts(b, "\">");
        } else {
            owf_buf_puts(b, "<f>");
        }
        put_xml(b, f, X_TEXT);
        owf_buf_puts(b, "</f>");
        free(f);
    }
    switch (c->type) {
    case OWF_VALUE_NUMBER:
        owf_buf_puts(b, "<v>");
        put_number(b, c->number);
        owf_buf_puts(b, "</v>");
        break;
    case OWF_VALUE_BOOL:
        owf_buf_puts(b, c->number ? "<v>1</v>" : "<v>0</v>");
        break;
    case OWF_VALUE_ERROR:
        owf_buf_printf(b, "<v>%s</v>", owf_xl_error_text(c->error));
        break;
    case OWF_VALUE_TEXT:
        if (c->formula) {
            int cut;
            owf_buf_puts(b, "<v xml:space=\"preserve\">");
            put_text(b, c->text, text_length(c->text, &cut), X_CELL);
            owf_buf_puts(b, "</v>");
        } else {
            long i = shared_string(w, c->text);
            if (i < 0)
                w->failed = 1;
            owf_buf_printf(b, "<v>%ld</v>", i < 0 ? 0L : i);
        }
        break;
    case OWF_VALUE_EMPTY:
        break;
    }
    owf_buf_puts(b, "</c>");
}

/* The book's columns in order, without overlaps, as Excel wants them. */
static void put_cols(xlsx_out *w, owf_buf *b, const owf_sheet *sh)
{
    owf_colinfo *cols;
    int i, k, any = 0;
    unsigned long next = 0;
    if (!sh->ncols)
        return;
    cols = malloc((size_t)sh->ncols * sizeof *cols);
    if (!cols) {
        w->failed = 1;
        return;
    }
    memcpy(cols, sh->cols, (size_t)sh->ncols * sizeof *cols);
    for (i = 1; i < sh->ncols; i++) {    /* few, and nearly always in order already */
        owf_colinfo c = cols[i];
        for (k = i; k > 0 && cols[k - 1].first > c.first; k--)
            cols[k] = cols[k - 1];
        cols[k] = c;
    }
    for (i = 0; i < sh->ncols; i++) {
        unsigned long first = cols[i].first, last = cols[i].last;
        if (first < next)
            first = next;
        if (last >= MAX_COLS)
            last = MAX_COLS - 1;
        if (first > last)
            continue;
        if (!any)
            owf_buf_puts(b, "<cols>");
        any = 1;
        owf_buf_printf(b, "<col min=\"%lu\" max=\"%lu\" width=\"", first + 1, last + 1);
        put_number(b, (cols[i].width > 0 ? cols[i].width : 0) / 105.0);
        owf_buf_puts(b, "\"");
        if (cols[i].style > 0 && cols[i].style < w->book->nstyles)
            owf_buf_printf(b, " style=\"%d\"", cols[i].style);
        if (cols[i].hidden)
            owf_buf_puts(b, " hidden=\"1\"");
        if (cols[i].width != sh->default_width)
            owf_buf_puts(b, " customWidth=\"1\"");
        owf_buf_puts(b, "/>");
        next = last + 1;
    }
    if (any)
        owf_buf_puts(b, "</cols>");
    free(cols);
}

static void put_row_start(owf_buf *b, const owf_sheet *sh, unsigned long row, int *ri)
{
    owf_buf_printf(b, "<row r=\"%lu\"", row + 1);
    while (*ri < sh->nrows && sh->rows[*ri].row < row)
        (*ri)++;
    if (*ri < sh->nrows && sh->rows[*ri].row == row) {
        const owf_rowinfo *r = &sh->rows[*ri];
        owf_buf_puts(b, " ht=\"");
        put_number(b, (r->height > 0 ? r->height : 0) / 20.0);
        owf_buf_puts(b, "\" customHeight=\"1\"");
        if (r->hidden)
            owf_buf_puts(b, " hidden=\"1\"");
    }
    owf_buf_putc(b, '>');
}

/* A formula of the model in a file's element, as Excel writes it. */
static void put_formula_element(xlsx_out *w, owf_buf *b, const char *element, const char *text)
{
    char *f = formula_for_file(w, text);
    if (!f)
        return;
    owf_buf_printf(b, "<%s>", element);
    put_xml(b, f, X_TEXT);
    owf_buf_printf(b, "</%s>", element);
    free(f);
}

static void cf_xml(xlsx_out *w, owf_buf *b, int index)
{
    static const char *const ops[8] = { "between", "notBetween", "equal", "notEqual", "greaterThan", "lessThan",
                                        "greaterThanOrEqual", "lessThanOrEqual" };
    static const char *const kinds[6] = { "min", "max", "num", "percent", "percentile", "formula" };
    const owf_sheet *sh = &w->book->sheets[index];
    int i, r, k;
    for (i = 0; i < sh->ncondfmts && !w->failed; i++) {
        const owf_condfmt *cf = &sh->condfmts[i];
        int dxf = cf_dxf(w->book, index, i), any = 0;
        if (!cf_writable(cf))
            continue;
        owf_buf_puts(b, "<conditionalFormatting sqref=\"");
        for (r = 0; r < cf->nranges; r++) {
            const owf_range *m = &cf->ranges[r];
            if (m->last_row >= MAX_ROWS || m->last_col >= MAX_COLS)
                continue;
            if (any++)
                owf_buf_putc(b, ' ');
            put_ref(b, m->first_row, m->first_col);
            if (m->last_row != m->first_row || m->last_col != m->first_col) {
                owf_buf_putc(b, ':');
                put_ref(b, m->last_row, m->last_col);
            }
        }
        owf_buf_puts(b, "\">");
        if (cf->type == OWF_CF_KEPT) {                   /* as read, its priority and look put back in */
            const char *rest = cf->xml + (strncmp(cf->xml, "<cfRule", 7) ? 0 : 7);
            owf_buf_printf(b, "<cfRule priority=\"%d\"", cf->priority > 0 ? cf->priority : i + 1);
            if (dxf >= 0)
                owf_buf_printf(b, " dxfId=\"%d\"", dxf);
            owf_buf_puts(b, rest);
            owf_buf_puts(b, "</conditionalFormatting>");
            continue;
        }
        owf_buf_printf(b, "<cfRule type=\"%s\"", cf->type == OWF_CF_CELL_IS ? "cellIs" : cf->type == OWF_CF_EXPRESSION ? "expression" : "colorScale");
        if (dxf >= 0 && cf->type != OWF_CF_COLOUR_SCALE)
            owf_buf_printf(b, " dxfId=\"%d\"", dxf);
        owf_buf_printf(b, " priority=\"%d\"", cf->priority > 0 ? cf->priority : i + 1);
        if (cf->stop)
            owf_buf_puts(b, " stopIfTrue=\"1\"");
        if (cf->type == OWF_CF_CELL_IS)
            owf_buf_printf(b, " operator=\"%s\"", ops[cf->op >= 0 && cf->op < 8 ? cf->op : 2]);
        owf_buf_putc(b, '>');
        if (cf->type == OWF_CF_COLOUR_SCALE) {
            owf_buf_puts(b, "<colorScale>");
            for (k = 0; k < cf->npoints && k < 3; k++) {
                const owf_cfpoint *p = &cf->points[k];
                owf_buf_printf(b, "<cfvo type=\"%s\"", kinds[p->kind >= 0 && p->kind < 6 ? p->kind : 0]);
                if (p->value && p->kind == OWF_CFV_FORMULA) {
                    char *f = formula_for_file(w, p->value);
                    if (f) {
                        owf_buf_puts(b, " val=\"");
                        put_xml(b, f, X_ATTR);
                        owf_buf_putc(b, '"');
                        free(f);
                    }
                } else if (p->value) {
                    owf_buf_puts(b, " val=\"");
                    put_xml(b, p->value, X_ATTR);
                    owf_buf_putc(b, '"');
                }
                owf_buf_puts(b, "/>");
            }
            for (k = 0; k < cf->npoints && k < 3; k++)
                put_rgb(b, "color", OWF_SHADE_RGB(cf->points[k].colour));
            owf_buf_puts(b, "</colorScale>");
        } else {
            for (k = 0; k < 2; k++)
                if (cf->formula[k])
                    put_formula_element(w, b, "formula", cf->formula[k]);
        }
        owf_buf_puts(b, "</cfRule></conditionalFormatting>");
    }
}

static void sheet_xml(xlsx_out *w, owf_buf *b, int index)
{
    const owf_sheet *sh = &w->book->sheets[index];
    unsigned long minr = 0, maxr = 0, minc = 0, maxc = 0, row = 0;
    int i, ri = 0, open = 0, any = 0, skipped = 0;

    for (i = 0; i < sh->ncells; i++) {
        const owf_cell *c = &sh->cells[i];
        if (c->row >= MAX_ROWS || c->col >= MAX_COLS)
            continue;
        if (!any || c->row < minr) minr = c->row;
        if (!any || c->row > maxr) maxr = c->row;
        if (!any || c->col < minc) minc = c->col;
        if (!any || c->col > maxc) maxc = c->col;
        any = 1;
    }
    owf_buf_puts(b, XML_HEAD "<worksheet xmlns=\"" NS_MAIN "\" xmlns:r=\"" NS_R "\"><dimension ref=\"");
    put_ref(b, minr, minc);
    if (maxr != minr || maxc != minc) {
        owf_buf_putc(b, ':');
        put_ref(b, maxr, maxc);
    }
    owf_buf_printf(b, "\"/><sheetViews><sheetView workbookViewId=\"0\"%s>", index == w->book->active ? " tabSelected=\"1\"" : "");
    if ((sh->freeze_rows || sh->freeze_cols) && sh->freeze_rows < MAX_ROWS && sh->freeze_cols < MAX_COLS) {
        const char *pane = sh->freeze_rows && sh->freeze_cols ? "bottomRight" : sh->freeze_rows ? "bottomLeft" : "topRight";
        owf_buf_puts(b, "<pane");
        if (sh->freeze_cols)
            owf_buf_printf(b, " xSplit=\"%u\"", sh->freeze_cols);
        if (sh->freeze_rows)
            owf_buf_printf(b, " ySplit=\"%u\"", sh->freeze_rows);
        owf_buf_puts(b, " topLeftCell=\"");
        put_ref(b, sh->freeze_rows, sh->freeze_cols);
        owf_buf_printf(b, "\" activePane=\"%s\" state=\"frozen\"/><selection pane=\"%s\"/>", pane, pane);
    }
    owf_buf_puts(b, "</sheetView></sheetViews><sheetFormatPr defaultRowHeight=\"");
    put_number(b, (sh->default_height > 0 ? sh->default_height : 255) / 20.0);
    owf_buf_puts(b, "\" defaultColWidth=\"");
    put_number(b, (sh->default_width > 0 ? sh->default_width : 960) / 105.0);
    owf_buf_puts(b, "\"/>");
    put_cols(w, b, sh);
    owf_buf_puts(b, "<sheetData>");
    /* rows: those with cells, and those with a height or hidden */
    for (i = 0; i < sh->ncells && !w->failed; i++) {
        const owf_cell *c = &sh->cells[i];
        if (c->row >= MAX_ROWS || c->col >= MAX_COLS) {
            skipped = 1;
            continue;
        }
        if (!open || c->row != row) {
            if (open)
                owf_buf_puts(b, "</row>");
            while (ri < sh->nrows && sh->rows[ri].row < c->row) {
                if (sh->rows[ri].row < MAX_ROWS) {
                    int at = ri;
                    put_row_start(b, sh, sh->rows[ri].row, &at);
                    owf_buf_puts(b, "</row>");
                }
                ri++;
            }
            row = c->row;
            put_row_start(b, sh, row, &ri);
            open = 1;
        }
        put_cell(w, b, sh, c);
    }
    if (open)
        owf_buf_puts(b, "</row>");
    for (; ri < sh->nrows; ri++)
        if (sh->rows[ri].row < MAX_ROWS && (!open || sh->rows[ri].row > row)) {
            int at = ri;
            put_row_start(b, sh, sh->rows[ri].row, &at);
            owf_buf_puts(b, "</row>");
        }
    owf_buf_puts(b, "</sheetData>");
    if (sh->nmerges) {
        int n = 0;
        for (i = 0; i < sh->nmerges; i++)
            n += sh->merges[i].last_row < MAX_ROWS && sh->merges[i].last_col < MAX_COLS;
        if (n) {
            owf_buf_printf(b, "<mergeCells count=\"%d\">", n);
            for (i = 0; i < sh->nmerges; i++) {
                const owf_range *m = &sh->merges[i];
                if (m->last_row >= MAX_ROWS || m->last_col >= MAX_COLS)
                    continue;
                owf_buf_puts(b, "<mergeCell ref=\"");
                put_ref(b, m->first_row, m->first_col);
                owf_buf_putc(b, ':');
                put_ref(b, m->last_row, m->last_col);
                owf_buf_puts(b, "\"/>");
            }
            owf_buf_puts(b, "</mergeCells>");
        }
    }
    cf_xml(w, b, index);
    owf_buf_puts(b, "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>"
                    "</worksheet>\n");
    if (skipped)
        owf_report_add(w->report, OWF_NOTE_LOST, "Cells beyond Excel's 1048576 rows or 16384 columns were left out");
}

/* ---- The workbook ---- */

static int builtin_name(const char *name)
{
    static const char *const names[] = { "Print_Area", "Print_Titles", "Criteria", "Database", "Extract",
                                         "Consolidate_Area", "Sheet_Title", "_FilterDatabase", NULL };
    int i;
    for (i = 0; names[i]; i++)
        if (owf_same_word(name, names[i]))
            return 1;
    return 0;
}

static void workbook_xml(xlsx_out *w, owf_buf *b)
{
    const owf_book *book = w->book;
    int i, active = book->active >= 0 && book->active < book->nsheets ? book->active : 0;
    owf_buf_puts(b, XML_HEAD "<workbook xmlns=\"" NS_MAIN "\" xmlns:r=\"" NS_R "\">");
    owf_buf_puts(b, book->date1904 ? "<workbookPr date1904=\"1\"/>" : "<workbookPr/>");
    owf_buf_printf(b, "<bookViews><workbookView activeTab=\"%d\"/></bookViews><sheets>", active);
    for (i = 0; i < book->nsheets; i++) {
        owf_buf_puts(b, "<sheet name=\"");
        put_xml(b, w->names[i], X_ATTR);
        owf_buf_printf(b, "\" sheetId=\"%d\"", i + 1);
        if (book->sheets[i].hidden)
            owf_buf_puts(b, book->sheets[i].hidden == 2 ? " state=\"veryHidden\"" : " state=\"hidden\"");
        owf_buf_printf(b, " r:id=\"rId%d\"/>", i + 1);
    }
    owf_buf_puts(b, "</sheets>");
    if (w->links.count) {
        owf_buf_puts(b, "<externalReferences>");
        for (i = 0; i < w->links.count; i++)
            owf_buf_printf(b, "<externalReference r:id=\"rId%d\"/>", book->nsheets + 3 + i);
        owf_buf_puts(b, "</externalReferences>");
    }
    if (book->nnames) {
        owf_buf_puts(b, "<definedNames>");
        for (i = 0; i < book->nnames && !w->failed; i++) {
            const owf_defname *n = &book->names[i];
            char *f;
            if (n->sheet >= book->nsheets)
                continue;
            f = formula_for_file(w, n->formula);
            if (!f)
                break;
            owf_buf_puts(b, "<definedName name=\"");
            if (builtin_name(n->name))
                owf_buf_puts(b, "_xlnm.");
            put_xml(b, n->name, X_ATTR);
            owf_buf_putc(b, '"');
            if (n->sheet >= 0)
                owf_buf_printf(b, " localSheetId=\"%d\"", n->sheet);
            if (n->hidden)
                owf_buf_puts(b, " hidden=\"1\"");
            owf_buf_putc(b, '>');
            put_xml(b, f, X_TEXT);
            owf_buf_puts(b, "</definedName>");
            free(f);
        }
        owf_buf_puts(b, "</definedNames>");
    }
    owf_buf_puts(b, "</workbook>\n");
}

static void external_xml(const owf_xl_link *l, owf_buf *b)
{
    int i;
    owf_buf_puts(b, XML_HEAD "<externalLink xmlns=\"" NS_MAIN "\" xmlns:r=\"" NS_R "\"><externalBook r:id=\"rId1\">");
    if (l->nsheets) {
        owf_buf_puts(b, "<sheetNames>");
        for (i = 0; i < l->nsheets; i++) {
            owf_buf_puts(b, "<sheetName val=\"");
            put_xml(b, l->sheets[i], X_ATTR);
            owf_buf_puts(b, "\"/>");
        }
        owf_buf_puts(b, "</sheetNames>");
    }
    if (l->nnames) {
        owf_buf_puts(b, "<definedNames>");
        for (i = 0; i < l->nnames; i++) {
            owf_buf_puts(b, "<definedName name=\"");
            put_xml(b, l->names[i], X_ATTR);
            owf_buf_puts(b, "\"/>");
        }
        owf_buf_puts(b, "</definedNames>");
    }
    owf_buf_puts(b, "</externalBook></externalLink>\n");
}

static int add_part(owf_zip *zip, owf_buf *part, const char *name, int result)
{
    if (result == OWF_OK)
        result = part->failed ? OWF_ERR_MEMORY : owf_zip_add(zip, name, part->data ? part->data : (const unsigned char *)"",
                                                             part->length, 1);
    part->length = 0;
    part->failed = 0;
    return result;
}

static void free_writer(xlsx_out *w)
{
    int i;
    owf_xl_links_free(&w->links);
    free(w->fonts);
    free(w->xf_font);
    free(w->xf_fill);
    free(w->xf_border);
    free(w->fills);
    free(w->borders);
    free(w->numfmt_id);
    free(w->strings);
    free(w->slots);
    for (i = 0; w->names && i < w->book->nsheets; i++)
        free(w->names[i]);
    free(w->names);
}

int owf_export_xlsx(const owf_book *book, unsigned char **data, size_t *length, owf_report *report)
{
    xlsx_out w;
    owf_buf out, part;
    owf_zip *zip;
    int result = OWF_OK, i, nsh = book->nsheets ? book->nsheets : 1;
    char name[64];

    *data = NULL;
    *length = 0;
    memset(&w, 0, sizeof w);
    w.book = book;
    w.report = report;
    build_styles(&w);
    if (!w.failed)
        build_strings(&w);
    if (!w.failed)
        build_names(&w);
    if (!w.failed)
        collect_links(&w);
    if (w.failed) {
        free_writer(&w);
        return OWF_ERR_MEMORY;
    }
    if (!book->nsheets)
        owf_report_add(report, OWF_NOTE_INFO, "Excel needs a sheet: an empty one was written");

    owf_buf_init(&out);
    owf_buf_init(&part);
    zip = owf_zip_new(&out);
    if (!zip) {
        free_writer(&w);
        return OWF_ERR_MEMORY;
    }

    owf_buf_puts(&part, XML_HEAD "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"" CT "sheet.main+xml\"/>"
                        "<Override PartName=\"/xl/styles.xml\" ContentType=\"" CT "styles+xml\"/>"
                        "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"" CT "sharedStrings+xml\"/>");
    for (i = 0; i < nsh; i++)
        owf_buf_printf(&part, "<Override PartName=\"/xl/worksheets/sheet%d.xml\" ContentType=\"" CT "worksheet+xml\"/>", i + 1);
    for (i = 0; i < w.links.count; i++)
        owf_buf_printf(&part, "<Override PartName=\"/xl/externalLinks/externalLink%d.xml\" ContentType=\"" CT
                              "externalLink+xml\"/>", i + 1);
    owf_buf_puts(&part, "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
                        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
                        "</Types>\n");
    result = add_part(zip, &part, "[Content_Types].xml", result);

    owf_buf_puts(&part, XML_HEAD "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                        "<Relationship Id=\"rId1\" Type=\"" REL "officeDocument\" Target=\"xl/workbook.xml\"/>"
                        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
                        "<Relationship Id=\"rId3\" Type=\"" REL "extended-properties\" Target=\"docProps/app.xml\"/>"
                        "</Relationships>\n");
    result = add_part(zip, &part, "_rels/.rels", result);

    owf_buf_puts(&part, XML_HEAD "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                        "<Application>OpenCalc libowf " OWF_VERSION "</Application></Properties>\n");
    result = add_part(zip, &part, "docProps/app.xml", result);
    owf_buf_puts(&part, XML_HEAD "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"/>\n");
    result = add_part(zip, &part, "docProps/core.xml", result);

    if (book->nsheets) {
        workbook_xml(&w, &part);
    } else {
        owf_buf_puts(&part, XML_HEAD "<workbook xmlns=\"" NS_MAIN "\" xmlns:r=\"" NS_R "\"><sheets>"
                            "<sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>\n");
    }
    if (w.failed)
        result = OWF_ERR_MEMORY;
    result = add_part(zip, &part, "xl/workbook.xml", result);

    owf_buf_puts(&part, XML_HEAD "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (i = 0; i < nsh; i++)
        owf_buf_printf(&part, "<Relationship Id=\"rId%d\" Type=\"" REL "worksheet\" Target=\"worksheets/sheet%d.xml\"/>", i + 1,
                       i + 1);
    owf_buf_printf(&part, "<Relationship Id=\"rId%d\" Type=\"" REL "styles\" Target=\"styles.xml\"/>"
                          "<Relationship Id=\"rId%d\" Type=\"" REL "sharedStrings\" Target=\"sharedStrings.xml\"/>",
                   nsh + 1, nsh + 2);
    for (i = 0; i < w.links.count; i++)
        owf_buf_printf(&part, "<Relationship Id=\"rId%d\" Type=\"" REL "externalLink\" Target=\"externalLinks/externalLink%d.xml\"/>",
                       nsh + 3 + i, i + 1);
    owf_buf_puts(&part, "</Relationships>\n");
    result = add_part(zip, &part, "xl/_rels/workbook.xml.rels", result);

    styles_xml(&w, &part);
    result = add_part(zip, &part, "xl/styles.xml", result);

    for (i = 0; i < book->nsheets && result == OWF_OK; i++) {
        sheet_xml(&w, &part, i);
        if (w.failed)
            result = OWF_ERR_MEMORY;
        snprintf(name, sizeof name, "xl/worksheets/sheet%d.xml", i + 1);
        result = add_part(zip, &part, name, result);
    }
    if (!book->nsheets) {
        owf_buf_puts(&part, XML_HEAD "<worksheet xmlns=\"" NS_MAIN "\"><sheetData/></worksheet>\n");
        result = add_part(zip, &part, "xl/worksheets/sheet1.xml", result);
    }

    strings_xml(&w, &part);
    result = add_part(zip, &part, "xl/sharedStrings.xml", result);

    for (i = 0; i < w.links.count && result == OWF_OK; i++) {
        external_xml(&w.links.items[i], &part);
        snprintf(name, sizeof name, "xl/externalLinks/externalLink%d.xml", i + 1);
        result = add_part(zip, &part, name, result);
        owf_buf_puts(&part, XML_HEAD "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                            "<Relationship Id=\"rId1\" Type=\"" REL "externalLinkPath\" Target=\"");
        put_xml(&part, w.links.items[i].file, X_ATTR);
        owf_buf_puts(&part, "\" TargetMode=\"External\"/></Relationships>\n");
        snprintf(name, sizeof name, "xl/externalLinks/_rels/externalLink%d.xml.rels", i + 1);
        result = add_part(zip, &part, name, result);
    }
    if (w.links.count)
        owf_report_add(report, OWF_NOTE_INFO, "Formulas that use other workbooks keep their links (and their last values)");

    owf_buf_free(&part);
    if (owf_zip_finish(zip) != OWF_OK && result == OWF_OK)
        result = OWF_ERR_MEMORY;
    free_writer(&w);
    if (result != OWF_OK) {
        owf_buf_free(&out);
        return result;
    }
    return owf_buf_take(&out, data, length);
}

/*
 * libowf: ODP out (OpenDocument Presentation, OASIS ODF 1.2) from a deck
 * (owf_deck.h), as a package (.odp, .otp) or one flat XML file (.fodp).
 *
 * styles.xml holds the fonts, the masters' presentation styles (each text
 * level's look, the outline's bullets), the layouts, the slide and notes
 * page sizes and the master pages with their placeholders and art;
 * content.xml the slides, each a draw:page with its objects, notes,
 * footer fields and transition. Automatic style names start with "op"
 * ("opMgr1" in styles.xml), so they never meet the names of styles kept
 * from the file the deck came from, which are written back as they were.
 * Lengths are written in points with two decimals: exactly the twips the
 * deck holds.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_deck_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIME_ODP "application/vnd.oasis.opendocument.presentation"
#define MIME_OTP "application/vnd.oasis.opendocument.presentation-template"

#define NS "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" " \
           "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" " \
           "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" " \
           "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" " \
           "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" " \
           "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" " \
           "xmlns:xlink=\"http://www.w3.org/1999/xlink\" " \
           "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" " \
           "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" " \
           "xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" " \
           "xmlns:presentation=\"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0\" " \
           "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" " \
           "xmlns:smil=\"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0\" " \
           "xmlns:anim=\"urn:oasis:names:tc:opendocument:xmlns:animation:1.0\" " \
           "office:version=\"1.2\""

/* ---- Automatic styles, each written once ---- */

enum { EL_STYLE, EL_LIST };

typedef struct {
    int element;
    const char *family, *code;       /* "graphic", "gr" */
    char *parent;
    char *body;
    int number;                      /* within its code */
} entry;

typedef struct {
    const char *prefix;              /* "op", "opM" */
    entry *items;
    int count, capacity;
    int counts[12];
    int failed;
} registry;

static const char *const codes[] = { "gr", "pr", "P", "T", "dp", "L", "co", "ce", "ta", "ro", NULL };

static int code_index(const char *code)
{
    int i;
    for (i = 0; codes[i]; i++)
        if (!strcmp(codes[i], code))
            return i;
    return 11;
}

static void reg_free(registry *r)
{
    int i;
    for (i = 0; i < r->count; i++) {
        free(r->items[i].parent);
        free(r->items[i].body);
    }
    free(r->items);
}

/* The style's name (out holds 32 bytes), adding it when it is new. */
static void reg_style(registry *r, int element, const char *family, const char *code, const char *parent,
                      const owf_buf *body, char *out)
{
    int i;
    entry *e;
    out[0] = 0;
    if (body->failed) {
        r->failed = 1;
        return;
    }
    for (i = 0; i < r->count; i++) {
        e = &r->items[i];
        if (e->element == element && !strcmp(e->code, code) &&
            !strcmp(e->parent ? e->parent : "", parent ? parent : "") &&
            strlen(e->body) == body->length && !memcmp(e->body, body->data, body->length)) {
            snprintf(out, 32, "%s%s%d", r->prefix, code, e->number);
            return;
        }
    }
    if (r->count == r->capacity) {
        int want = r->capacity ? r->capacity * 2 : 32;
        entry *p = realloc(r->items, (size_t)want * sizeof *p);
        if (!p) {
            r->failed = 1;
            return;
        }
        r->items = p;
        r->capacity = want;
    }
    e = &r->items[r->count];
    e->element = element;
    e->family = family;
    e->code = code;
    e->parent = parent ? owf_deck_strdup(parent, strlen(parent)) : NULL;
    e->body = owf_deck_strdup(body->length ? (const char *)body->data : "", body->length);
    if (!e->body || (parent && !e->parent)) {
        free(e->parent);
        free(e->body);
        r->failed = 1;
        return;
    }
    e->number = ++r->counts[code_index(code)];
    r->count++;
    snprintf(out, 32, "%s%s%d", r->prefix, code, e->number);
}

static void reg_write(owf_buf *out, const registry *r)
{
    int i;
    for (i = 0; i < r->count; i++) {
        const entry *e = &r->items[i];
        if (e->element == EL_LIST) {
            owf_buf_printf(out, "<text:list-style style:name=\"%s%s%d\">", r->prefix, e->code, e->number);
            owf_buf_puts(out, e->body);
            owf_buf_puts(out, "</text:list-style>");
            continue;
        }
        owf_buf_printf(out, "<style:style style:name=\"%s%s%d\" style:family=\"%s\"", r->prefix, e->code,
                       e->number, e->family);
        if (e->parent) {
            owf_buf_puts(out, " style:parent-style-name=\"");
            owf_buf_puts_xml(out, e->parent);
            owf_buf_putc(out, '"');
        }
        if (*e->body) {
            owf_buf_putc(out, '>');
            owf_buf_puts(out, e->body);
            owf_buf_puts(out, "</style:style>");
        } else {
            owf_buf_puts(out, "/>");
        }
    }
}

/* ---- The writer ---- */

typedef struct {
    const owf_deck *deck;
    owf_report *report;
    int flat;                        /* one XML file: pictures inline */
    registry autos_styles, autos_content;
    registry *reg;                   /* the one objects being written use */
    int *pic_used;                   /* per res image: written as Pictures/ (1) */
    const owf_master *master;        /* the master of what is being written */
    int on_master;
    int failed;
} odpw;

/* Twips as points, exactly: "12pt", "12.35pt", "-0.05pt". */
static void put_len(owf_buf *out, int twips)
{
    long t = twips < 0 ? -(long)twips : twips;
    if (twips < 0)
        owf_buf_putc(out, '-');
    if (t % 20)
        owf_buf_printf(out, "%ld.%02ldpt", t / 20, (t % 20) * 5);
    else
        owf_buf_printf(out, "%ldpt", t / 20);
}

static void len_attr(owf_buf *out, const char *name, int twips)
{
    owf_buf_printf(out, " %s=\"", name);
    put_len(out, twips);
    owf_buf_putc(out, '"');
}

/* A name as an NCName: letters, digits, '-', '_' and '.' stay; anything
 * else is _XX_ for each byte ("Title Slide" is "Title_20_Slide"). */
static void put_ncname(owf_buf *out, const char *s)
{
    const unsigned char *p = (const unsigned char *)(s && *s ? s : "Default");
    int first = 1;
    for (; *p; p++, first = 0) {
        int ok = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_' ||
                 (!first && ((*p >= '0' && *p <= '9') || *p == '-' || *p == '.'));
        if (ok)
            owf_buf_putc(out, *p);
        else
            owf_buf_printf(out, "_%02X_", *p);
    }
}

static void ncname_attr(owf_buf *out, const char *name, const char *value)
{
    owf_buf_printf(out, " %s=\"", name);
    put_ncname(out, value);
    owf_buf_putc(out, '"');
}

/* "Default", or the master's name as an NCName, into a small buffer. */
static void master_style_name(const owf_master *m, char *out, size_t size)
{
    owf_buf b;
    owf_buf_init(&b);
    put_ncname(&b, m->name);
    if (b.failed || b.length >= size)
        snprintf(out, size, "Default");
    else {
        memcpy(out, b.data, b.length);
        out[b.length] = 0;
    }
    owf_buf_free(&b);
}

static int needs_display_name(const char *s)
{
    owf_buf b;
    int differs;
    if (!s || !*s)
        return 0;
    owf_buf_init(&b);
    put_ncname(&b, s);
    differs = b.failed || b.length != strlen(s) || memcmp(b.data, s, b.length);
    owf_buf_free(&b);
    return differs;
}

static void colour_attr(owf_buf *out, const char *name, unsigned long rgb)
{
    owf_buf_printf(out, " %s=\"#%06lx\"", name, rgb & 0xFFFFFFUL);
}

/* The name a font has in the file: today's equivalent of an Amiga font. */
static const char *face_name(const owf_doc *doc, int font, owf_report *report)
{
    const char *modern;
    if (font < 0 || font >= doc->nfonts)
        return "Liberation Sans";
    modern = owf_font_modern(doc->fonts[font].name, NULL);
    if (modern) {
        owf_report_add(report, OWF_NOTE_APPROX, "The font %s is saved as %s", doc->fonts[font].name, modern);
        return modern;
    }
    return doc->fonts[font].name;
}

static void put_font_faces(owf_buf *out, const owf_doc *doc, owf_report *report)
{
    static const char *generic[] = { "swiss", "roman", "swiss", "modern" };
    int i, j;
    owf_buf_puts(out, "<office:font-face-decls>");
    for (i = 0; i < doc->nfonts; i++) {
        const char *name = face_name(doc, i, report);
        int seen = 0;
        for (j = 0; j < i && !seen; j++)
            seen = !strcmp(face_name(doc, j, NULL), name);
        if (seen)
            continue;
        owf_buf_puts(out, "<style:font-face style:name=\"");
        owf_buf_puts_xml(out, name);
        owf_buf_puts(out, "\" svg:font-family=\"'");
        owf_buf_puts_xml(out, name);
        owf_buf_printf(out, "'\" style:font-family-generic=\"%s\" style:font-pitch=\"%s\"/>",
                       generic[doc->fonts[i].kind], doc->fonts[i].kind == OWF_FONT_MONO ? "fixed" : "variable");
    }
    if (!doc->nfonts)
        owf_buf_puts(out, "<style:font-face style:name=\"Liberation Sans\" svg:font-family=\"'Liberation Sans'\" "
                          "style:font-family-generic=\"swiss\" style:font-pitch=\"variable\"/>");
    owf_buf_puts(out, "</office:font-face-decls>");
}

/* Text properties: all of f (full), or where f differs from the level's. */
static void put_text_props(owf_buf *out, const owf_doc *doc, const owf_charfmt *f, const owf_charfmt *level, int full)
{
    unsigned flags = f->flags, diff = level ? (f->flags ^ level->flags) : f->flags;
    size_t start = out->length;
    owf_buf_puts(out, "<style:text-properties");
    if (f->font >= 0 && f->font < doc->nfonts && (full || !level || f->font != level->font)) {
        owf_buf_puts(out, " style:font-name=\"");
        owf_buf_puts_xml(out, face_name(doc, f->font, NULL));
        owf_buf_putc(out, '"');
    }
    if (f->size > 0 && (full || !level || f->size != level->size))
        len_attr(out, "fo:font-size", f->size);
    if (full)
        diff = OWF_BOLD | OWF_ITALIC | OWF_UNDERLINE | OWF_STRIKE | OWF_SUPER | OWF_SUB;
    if (diff & OWF_BOLD)
        owf_buf_puts(out, flags & OWF_BOLD ? " fo:font-weight=\"bold\"" : " fo:font-weight=\"normal\"");
    if (diff & OWF_ITALIC)
        owf_buf_puts(out, flags & OWF_ITALIC ? " fo:font-style=\"italic\"" : " fo:font-style=\"normal\"");
    if (diff & OWF_UNDERLINE)
        owf_buf_puts(out, flags & OWF_UNDERLINE ? " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\""
                                                  " style:text-underline-color=\"font-color\""
                                                : " style:text-underline-style=\"none\"");
    if (diff & OWF_STRIKE)
        owf_buf_puts(out, flags & OWF_STRIKE ? " style:text-line-through-style=\"solid\""
                                             : " style:text-line-through-style=\"none\"");
    if (diff & (OWF_SUPER | OWF_SUB))
        owf_buf_puts(out, flags & OWF_SUPER ? " style:text-position=\"super 58%\""
                        : flags & OWF_SUB ? " style:text-position=\"sub 58%\"" : " style:text-position=\"0% 100%\"");
    if (f->colour != OWF_COLOUR_AUTO && (full || !level || f->colour != level->colour))
        colour_attr(out, "fo:color", f->colour);
    else if (full && f->colour == OWF_COLOUR_AUTO)
        owf_buf_puts(out, " style:use-window-font-color=\"true\"");
    if (out->length == start + 22) {             /* nothing to say */
        out->length = start;
        return;
    }
    owf_buf_puts(out, "/>");
}

/* Paragraph properties: all of f, or where it differs from the level's.
 * A list paragraph's indents are its list level's. */
static void put_para_props(owf_buf *out, const owf_parafmt *f, const owf_parafmt *level, int full, int in_list)
{
    static const char *align[] = { "start", "center", "end", "justify" };
    static const char *tab_type[] = { "left", "center", "right", "char" };
    size_t start = out->length;
    int i, tabs_differ = full ? f->ntabs > 0 : (level && f->ntabs != level->ntabs);
    owf_buf_puts(out, "<style:paragraph-properties");
#define DIFF(field) (full || !level || f->field != level->field)
    if (DIFF(align))
        owf_buf_printf(out, " fo:text-align=\"%s\"", align[(unsigned)f->align < 4 ? f->align : 0]);
    if (!in_list && DIFF(indent_left))
        len_attr(out, "fo:margin-left", f->indent_left);
    if (DIFF(indent_right))
        len_attr(out, "fo:margin-right", f->indent_right);
    if (!in_list && DIFF(indent_first))
        len_attr(out, "fo:text-indent", f->indent_first);
    if (DIFF(space_before))
        len_attr(out, "fo:margin-top", f->space_before);
    if (DIFF(space_after))
        len_attr(out, "fo:margin-bottom", f->space_after);
    if (DIFF(line_spacing))
        owf_buf_printf(out, " fo:line-height=\"%d%%\"", f->line_spacing > 0 ? f->line_spacing : 100);
    if (DIFF(shading) && (OWF_SHADE_SET(f->shading) || !full))
        owf_buf_printf(out, OWF_SHADE_SET(f->shading) ? " fo:background-color=\"#%06lx\"" : " fo:background-color=\"transparent\"",
                       OWF_SHADE_RGB(f->shading));
    if (DIFF(borders) || DIFF(border_colour)) {
        static const char *side[] = { "top", "left", "bottom", "right" };
        unsigned long col = OWF_SHADE_SET(f->border_colour) ? OWF_SHADE_RGB(f->border_colour) : 0;
        int k;
        for (k = 0; k < 4; k++)
            if (f->borders & (1 << k))
                owf_buf_printf(out, " fo:border-%s=\"0.5pt solid #%06lx\"", side[k], col);
            else if (!full)
                owf_buf_printf(out, " fo:border-%s=\"none\"", side[k]);
    }
#undef DIFF
    if (!tabs_differ && level && !full)
        for (i = 0; i < f->ntabs && !tabs_differ; i++)
            tabs_differ = f->tabs[i].position != level->tabs[i].position || f->tabs[i].kind != level->tabs[i].kind;
    if (!tabs_differ) {
        if (out->length == start + 27) {
            out->length = start;
            return;
        }
        owf_buf_puts(out, "/>");
        return;
    }
    owf_buf_puts(out, "><style:tab-stops>");
    for (i = 0; i < f->ntabs && i < OWF_MAX_TABS; i++) {
        owf_buf_puts(out, "<style:tab-stop");
        len_attr(out, "style:position", f->tabs[i].position);
        owf_buf_printf(out, " style:type=\"%s\"", tab_type[(unsigned)f->tabs[i].kind < 4 ? f->tabs[i].kind : 0]);
        if (f->tabs[i].kind == OWF_TAB_DECIMAL)
            owf_buf_puts(out, " style:char=\".\"");
        owf_buf_puts(out, "/>");
    }
    owf_buf_puts(out, "</style:tab-stops></style:paragraph-properties>");
}

/* A list style's levels (text:list-level-style-...). */
static void put_list_levels(owf_buf *out, const owf_doc *doc, const owf_textlevel *lv)
{
    int i;
    for (i = 0; i < OWF_TEXT_LEVELS; i++) {
        const owf_textlevel *l = &lv[i];
        int width = l->para.indent_first < 0 ? -l->para.indent_first : 0;
        int before = l->para.indent_left - width;
        if (l->bullet & OWF_BULLET_NUMBER) {
            char f[2];
            f[0] = (char)(l->bullet & 0xFF);
            f[1] = 0;
            if (!strchr("1aAiI", f[0]))
                f[0] = '1';
            owf_buf_printf(out, "<text:list-level-style-number text:level=\"%d\" style:num-format=\"%s\"", i + 1, f);
            if (l->prefix[0]) {
                owf_buf_puts(out, " style:num-prefix=\"");
                owf_buf_puts_xml(out, l->prefix);
                owf_buf_putc(out, '"');
            }
            if (l->suffix[0]) {
                owf_buf_puts(out, " style:num-suffix=\"");
                owf_buf_puts_xml(out, l->suffix);
                owf_buf_putc(out, '"');
            }
            if (l->start > 1)
                owf_buf_printf(out, " text:start-value=\"%d\"", l->start);
        } else if (!l->bullet) {
            /* a level without a bullet: a number in no format */
            owf_buf_printf(out, "<text:list-level-style-number text:level=\"%d\" style:num-format=\"\"", i + 1);
        } else {
            owf_buf_printf(out, "<text:list-level-style-bullet text:level=\"%d\" text:bullet-char=\"", i + 1);
            owf_buf_put_utf8(out, l->bullet);
            owf_buf_putc(out, '"');
        }
        /* a bullet's size is its own attribute; a number's, its text's */
        if (l->bullet_size > 0 && l->bullet_size != 100 && l->bullet && !(l->bullet & OWF_BULLET_NUMBER))
            owf_buf_printf(out, " text:bullet-relative-size=\"%d%%\"", l->bullet_size);
        owf_buf_puts(out, "><style:list-level-properties");
        len_attr(out, "text:space-before", before);
        len_attr(out, "text:min-label-width", width);
        owf_buf_puts(out, "/>");
        if (l->bullet_font >= 0 || OWF_SHADE_SET(l->bullet_colour) ||
            ((l->bullet & OWF_BULLET_NUMBER) && l->bullet_size > 0 && l->bullet_size != 100)) {
            owf_buf_puts(out, "<style:text-properties");
            if (l->bullet_font >= 0 && l->bullet_font < doc->nfonts) {
                owf_buf_puts(out, " style:font-name=\"");
                owf_buf_puts_xml(out, face_name(doc, l->bullet_font, NULL));
                owf_buf_putc(out, '"');
            }
            if (OWF_SHADE_SET(l->bullet_colour))
                colour_attr(out, "fo:color", OWF_SHADE_RGB(l->bullet_colour));
            else
                owf_buf_puts(out, " style:use-window-font-color=\"true\"");
            if ((l->bullet & OWF_BULLET_NUMBER) && l->bullet_size > 0 && l->bullet_size != 100)
                owf_buf_printf(out, " fo:font-size=\"%d%%\"", l->bullet_size);
            owf_buf_puts(out, "/>");
        }
        owf_buf_puts(out, (l->bullet & OWF_BULLET_NUMBER) || !l->bullet ? "</text:list-level-style-number>"
                                                                        : "</text:list-level-style-bullet>");
    }
}

/* ---- Text ---- */

/* Text: ODF collapses spaces, so the second and later of a row, and one at
 * the start, are written as <text:s/>; tabs and line breaks as elements. */
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
        } else if (*p == '\t' || *p == '\n') {
            owf_buf_put_xml(out, start, (size_t)(p - start));
            owf_buf_puts(out, *p == '\t' ? "<text:tab/>" : "<text:line-break/>");
            p++;
            start = p;
            *after_space = 1;
        } else {
            p++;
            *after_space = 0;
        }
    }
    owf_buf_put_xml(out, start, (size_t)(p - start));
}

static void put_style_name(owf_buf *out, const char *attr, const char *name)
{
    if (name && *name)
        owf_buf_printf(out, " %s=\"%s\"", attr, name);
}

/* One paragraph: text:p with its style where it differs from its level. */
static void put_para(odpw *w, owf_buf *out, const owf_para *p, const owf_textlevel *lv, int in_list,
                     owf_placeholder field_ph)
{
    static const char *fields[] = {
        "<text:page-number>1</text:page-number>", "<text:page-count>1</text:page-count>", "<text:date/>", "<text:time/>"
    };
    owf_buf body;
    char pname[32];
    int j, after_space = 1;

    owf_buf_init(&body);
    put_para_props(&body, &p->fmt, &lv->para, 0, in_list);
    pname[0] = 0;
    if (body.length)
        reg_style(w->reg, EL_STYLE, "paragraph", "P", NULL, &body, pname);
    owf_buf_puts(out, "<text:p");
    put_style_name(out, "text:style-name", pname);
    owf_buf_putc(out, '>');
    for (j = 0; j < p->nruns; j++) {
        const owf_run *run = &p->runs[j];
        char tname[32];
        body.length = 0;
        put_text_props(&body, w->deck->res, &run->fmt, &lv->chars, 0);
        tname[0] = 0;
        if (body.length)
            reg_style(w->reg, EL_STYLE, "text", "T", NULL, &body, tname);
        if (run->kind == OWF_RUN_TEXT && run->href) {
            owf_buf_puts(out, "<text:a xlink:type=\"simple\" xlink:href=\"");
            owf_buf_puts_xml(out, run->href);
            owf_buf_puts(out, "\">");
        }
        if (tname[0])
            owf_buf_printf(out, "<text:span text:style-name=\"%s\">", tname);
        switch (run->kind) {
        case OWF_RUN_TEXT: put_text(out, run->text ? run->text : "", &after_space); break;
        case OWF_RUN_TAB: owf_buf_puts(out, "<text:tab/>"); after_space = 1; break;
        case OWF_RUN_LINEBREAK: owf_buf_puts(out, "<text:line-break/>"); after_space = 1; break;
        case OWF_RUN_FIELD: owf_buf_puts(out, fields[(unsigned)run->field < 4 ? run->field : 0]); after_space = 0; break;
        case OWF_RUN_IMAGE:
            owf_report_add(w->report, OWF_NOTE_LOST, "Pictures inside slide text were left out");
            break;
        }
        if (tname[0])
            owf_buf_puts(out, "</text:span>");
        if (run->kind == OWF_RUN_TEXT && run->href)
            owf_buf_puts(out, "</text:a>");
    }
    /* a master's footer, date and number boxes show the slide's fields */
    if (field_ph == OWF_PH_FOOTER)
        owf_buf_puts(out, "<presentation:footer/>");
    else if (field_ph == OWF_PH_DATE)
        owf_buf_puts(out, "<presentation:date-time/>");
    else if (field_ph == OWF_PH_NUMBER && !p->nruns)
        owf_buf_puts(out, "<text:page-number>&lt;number&gt;</text:page-number>");
    owf_buf_puts(out, "</text:p>");
    if (body.failed)
        w->failed = 1;
    owf_buf_free(&body);
}

static const owf_textlevel *para_level(odpw *w, const owf_obj *obj, int level)
{
    const owf_master *m = w->master;
    if (obj->list && level > 0)
        return &obj->list[level - 1];
    switch (obj->placeholder) {
    case OWF_PH_TITLE: return &m->title;
    case OWF_PH_SUBTITLE: return &m->subtitle;
    case OWF_PH_NOTES: return &m->notes;
    case OWF_PH_BODY:
    case OWF_PH_TEXT:
    case OWF_PH_OBJECT:
        return &m->body[level > 0 ? level - 1 : 0];
    default: return &m->other;
    }
}

/* A story's paragraphs, lists nested by their levels. */
static void put_story(odpw *w, owf_buf *out, const owf_obj *obj, const owf_story *story, int with_levels)
{
    int i, depth = 0;
    char lname[32];

    lname[0] = 0;
    if (with_levels && obj->list) {
        owf_buf body;
        owf_buf_init(&body);
        put_list_levels(&body, w->deck->res, obj->list);
        reg_style(w->reg, EL_LIST, "list", "L", NULL, &body, lname);
        owf_buf_free(&body);
    }
    for (i = 0; i < story->nparas; i++) {
        int level = with_levels ? owf_obj_level(obj, i) : 0;
        while (depth > level) {
            owf_buf_puts(out, depth > 1 ? "</text:list></text:list-item>" : "</text:list>");
            depth--;
        }
        while (depth < level) {
            if (depth == 0) {
                owf_buf_puts(out, "<text:list");
                put_style_name(out, "text:style-name", lname);
                owf_buf_putc(out, '>');
            } else {
                owf_buf_puts(out, "<text:list-item><text:list>");
            }
            depth++;
        }
        if (level)
            owf_buf_puts(out, "<text:list-item>");
        put_para(w, out, &story->paras[i], para_level(w, obj, level), level > 0,
                 w->on_master ? obj->placeholder : OWF_PH_NONE);
        if (level)
            owf_buf_puts(out, "</text:list-item>");
    }
    while (depth > 0) {
        owf_buf_puts(out, depth > 1 ? "</text:list></text:list-item>" : "</text:list>");
        depth--;
    }
}

/* ---- Objects ---- */

static const char *class_name(owf_placeholder ph)
{
    switch (ph) {
    case OWF_PH_TITLE: return "title";
    case OWF_PH_SUBTITLE: return "subtitle";
    case OWF_PH_BODY: return "outline";
    case OWF_PH_TEXT: return "text";
    case OWF_PH_PICTURE: return "graphic";
    case OWF_PH_OBJECT: return "object";
    case OWF_PH_CHART: return "chart";
    case OWF_PH_TABLE: return "table";
    case OWF_PH_NOTES: return "notes";
    case OWF_PH_SLIDE_IMAGE: return "page";
    case OWF_PH_DATE: return "date-time";
    case OWF_PH_FOOTER: return "footer";
    case OWF_PH_NUMBER: return "page-number";
    case OWF_PH_HEADER: return "header";
    default: return NULL;
    }
}

/* The presentation style a placeholder's own style is under. */
static void master_kind_style(const owf_master *m, owf_placeholder ph, char *out, size_t size)
{
    char name[96];
    const char *kind;
    master_style_name(m, name, sizeof name);
    switch (ph) {
    case OWF_PH_TITLE: kind = "title"; break;
    case OWF_PH_SUBTITLE: kind = "subtitle"; break;
    case OWF_PH_NOTES: kind = "notes"; break;
    case OWF_PH_BODY: case OWF_PH_TEXT: case OWF_PH_OBJECT: kind = "outline1"; break;
    default: kind = "backgroundobjects"; break;
    }
    snprintf(out, size, "%s-%s", name, kind);
}

static const char *pic_ext(const owf_image *im)
{
    const char *m = im->mime ? im->mime : "";
    if (!strcmp(m, "image/png")) return "png";
    if (!strcmp(m, "image/jpeg")) return "jpg";
    if (!strcmp(m, "image/gif")) return "gif";
    if (!strcmp(m, "image/svg+xml")) return "svg";
    if (!strcmp(m, "image/bmp")) return "bmp";
    if (!strcmp(m, "image/x-wmf")) return "wmf";
    if (!strcmp(m, "image/x-emf")) return "emf";
    if (!strcmp(m, "image/x-ilbm") || !strcmp(m, "image/iff")) return "iff";
    return "bin";
}

static void put_href(owf_buf *out, int image, const owf_image *im)
{
    owf_buf_printf(out, "Pictures/op%d.%s", image + 1, pic_ext(im));
}

/* draw:image (or draw:fill-image's) picture: a package member, or inline. */
static void put_image(odpw *w, owf_buf *out, const char *element, const char *extra, int image)
{
    const owf_image *im;
    if (image < 0 || image >= w->deck->res->nimages)
        return;
    im = &w->deck->res->images[image];
    owf_buf_printf(out, "<%s%s", element, extra ? extra : "");
    if (w->flat) {
        owf_buf_puts(out, "><office:binary-data>");
        owf_deck_base64(out, im->data, im->length);
        owf_buf_printf(out, "</office:binary-data></%s>", element);
        return;
    }
    w->pic_used[image] = 1;
    owf_buf_puts(out, " xlink:href=\"");
    put_href(out, image, im);
    owf_buf_puts(out, "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>");
}

/* The object's graphic style: fill, line, insets, alignment, fitting. */
static void graphic_style(odpw *w, const owf_obj *o, char *name, int text_frame)
{
    owf_buf body;
    char parent[128];
    int presentation = o->placeholder != OWF_PH_NONE;
    static const char *valign[] = { "top", "middle", "bottom" };

    owf_buf_init(&body);
    owf_buf_puts(&body, "<style:graphic-properties");
    if (OWF_SHADE_SET(o->look.fill))
        owf_buf_printf(&body, " draw:fill=\"solid\" draw:fill-color=\"#%06lx\"", OWF_SHADE_RGB(o->look.fill));
    else
        owf_buf_puts(&body, " draw:fill=\"none\"");
    if (OWF_SHADE_SET(o->look.line)) {
        int dashed = o->look.line_style == OWF_LINE_DASHED || o->look.line_style == OWF_LINE_DOTTED ||
                     o->look.line_style == OWF_LINE_MEDIUM_DASHED || o->look.line_style == OWF_LINE_DASH_DOT;
        owf_buf_puts(&body, dashed ? " draw:stroke=\"dash\" draw:stroke-dash=\"op-dash\"" : " draw:stroke=\"solid\"");
        owf_buf_printf(&body, " svg:stroke-color=\"#%06lx\"", OWF_SHADE_RGB(o->look.line));
        len_attr(&body, "svg:stroke-width", o->look.line_width);
    } else {
        owf_buf_puts(&body, " draw:stroke=\"none\"");
    }
    if (o->look.arrows & OWF_ARROW_START) {
        owf_buf_puts(&body, " draw:marker-start=\"op-arrow\"");
        len_attr(&body, "draw:marker-start-width", o->look.line_width * 3 > 200 ? o->look.line_width * 3 : 200);
    }
    if (o->look.arrows & OWF_ARROW_END) {
        owf_buf_puts(&body, " draw:marker-end=\"op-arrow\"");
        len_attr(&body, "draw:marker-end-width", o->look.line_width * 3 > 200 ? o->look.line_width * 3 : 200);
    }
    len_attr(&body, "fo:padding-left", o->inset[0]);
    len_attr(&body, "fo:padding-top", o->inset[1]);
    len_attr(&body, "fo:padding-right", o->inset[2]);
    len_attr(&body, "fo:padding-bottom", o->inset[3]);
    if (text_frame) {
        owf_buf_printf(&body, " draw:textarea-vertical-align=\"%s\"", valign[o->valign <= 2 ? o->valign : 0]);
        owf_buf_printf(&body, " draw:auto-grow-height=\"%s\"", o->autofit == OWF_AUTOFIT_GROW ? "true" : "false");
        if (o->autofit == OWF_AUTOFIT_SHRINK)
            owf_buf_puts(&body, " style:shrink-to-fit=\"true\"");
    }
    if (o->kind == OWF_OBJ_PICTURE && o->image >= 0 && (o->crop[0] || o->crop[1] || o->crop[2] || o->crop[3])) {
        int nw, nh;
        if (owf_deck_image_size(&w->deck->res->images[o->image], &nw, &nh)) {
            owf_buf_puts(&body, " fo:clip=\"rect(");
            put_len(&body, (int)(((long)o->crop[1] * nh + 500) / 1000));
            owf_buf_puts(&body, ", ");
            put_len(&body, (int)(((long)o->crop[2] * nw + 500) / 1000));
            owf_buf_puts(&body, ", ");
            put_len(&body, (int)(((long)o->crop[3] * nh + 500) / 1000));
            owf_buf_puts(&body, ", ");
            put_len(&body, (int)(((long)o->crop[0] * nw + 500) / 1000));
            owf_buf_puts(&body, ")\"");
        }
    }
    owf_buf_puts(&body, "/>");
    if (presentation) {
        master_kind_style(w->master, o->placeholder, parent, sizeof parent);
        reg_style(w->reg, EL_STYLE, "presentation", "pr", parent, &body, name);
    } else {
        reg_style(w->reg, EL_STYLE, "graphic", "gr", "standard", &body, name);
    }
    if (body.failed)
        w->failed = 1;
    owf_buf_free(&body);
}

/* The radians, "0.523599", of tenths of a degree. */
static void put_radians(owf_buf *out, int tenths)
{
    long long micro = (long long)tenths * 3141593;
    long v;
    micro = micro >= 0 ? (micro + 900) / 1800 : -((-micro + 900) / 1800);
    v = (long)(micro < 0 ? -micro : micro);
    owf_buf_printf(out, "%s%ld.%06ld", micro < 0 ? "-" : "", v / 1000000, v % 1000000);
}

static void put_geometry(owf_buf *out, const owf_obj *o)
{
    len_attr(out, "svg:width", o->width);
    len_attr(out, "svg:height", o->height);
    if (o->rotation) {
        long c = owf_deck_cos(o->rotation), s = owf_deck_sin(o->rotation);
        long cx = ((long)o->width * c + (long)o->height * s) / 2;
        long cy = (-(long)o->width * s + (long)o->height * c) / 2;
        int tx = (int)(o->x + o->width / 2 - (cx + 8192) / 16384);
        int ty = (int)(o->y + o->height / 2 - (cy + 8192) / 16384);
        /* read back: x = tx + cx - w/2 */
        owf_buf_puts(out, " draw:transform=\"rotate (");
        put_radians(out, o->rotation);
        owf_buf_puts(out, ") translate (");
        put_len(out, tx);
        owf_buf_putc(out, ' ');
        put_len(out, ty);
        owf_buf_puts(out, ")\"");
        return;
    }
    len_attr(out, "svg:x", o->x);
    len_attr(out, "svg:y", o->y);
}

static void put_common(odpw *w, owf_buf *out, const owf_obj *o)
{
    if (o->name) {
        owf_buf_puts(out, " draw:name=\"");
        owf_buf_puts_xml(out, o->name);
        owf_buf_putc(out, '"');
    }
    if (o->xml_id || o->id > 0) {
        char id[48];
        if (o->xml_id)
            snprintf(id, sizeof id, "%.40s", o->xml_id);
        else
            snprintf(id, sizeof id, "id%ld", o->id);
        owf_buf_puts(out, " draw:id=\"");
        owf_buf_puts_xml(out, id);
        owf_buf_puts(out, "\" xml:id=\"");
        owf_buf_puts_xml(out, id);
        owf_buf_putc(out, '"');
    }
    owf_buf_printf(out, " draw:layer=\"%s\"", w->on_master ? "backgroundobjects" : "layout");
    if (o->flags & OWF_OBJ_HIDDEN)
        owf_buf_puts(out, " draw:display=\"none\"");
}

static void put_alt(owf_buf *out, const owf_obj *o)
{
    if (o->alt) {
        owf_buf_puts(out, "<svg:desc>");
        owf_buf_puts_xml(out, o->alt);
        owf_buf_puts(out, "</svg:desc>");
    }
}

static void put_table(odpw *w, owf_buf *out, const owf_obj *o)
{
    int ncols = o->ncols > 0 ? o->ncols : 1, i = 0, c, row;
    char name[32];
    owf_buf body;

    owf_buf_init(&body);
    for (c = 0; c < o->text.nparas; c++)
        if (o->text.paras[c].table_cols > ncols)
            ncols = o->text.paras[c].table_cols;
    if (ncols > 64)
        ncols = 64;
    owf_buf_puts(out, "<table:table>");
    for (c = 0; c < ncols; c++) {
        int width = o->col_widths && c < o->ncols ? o->col_widths[c] : o->width / ncols;
        body.length = 0;
        owf_buf_puts(&body, "<style:table-column-properties");
        len_attr(&body, "style:column-width", width);
        owf_buf_puts(&body, "/>");
        reg_style(w->reg, EL_STYLE, "table-column", "co", NULL, &body, name);
        owf_buf_printf(out, "<table:table-column table:style-name=\"%s\"/>", name);
    }
    while (i < o->text.nparas) {
        int col = 0;
        row = o->text.paras[i].table_row;
        owf_buf_puts(out, "<table:table-row>");
        while (i < o->text.nparas && o->text.paras[i].table_row == row) {
            const owf_para *p = &o->text.paras[i];
            int target = p->table_col < ncols ? p->table_col : ncols - 1;
            while (col < target) {
                owf_buf_puts(out, "<table:table-cell/>");
                col++;
            }
            owf_buf_puts(out, "<table:table-cell office:value-type=\"string\"");
            if (OWF_SHADE_SET(p->cell_shading)) {
                body.length = 0;
                owf_buf_printf(&body, "<style:graphic-properties draw:fill=\"solid\" draw:fill-color=\"#%06lx\"/>"
                                      "<style:table-cell-properties fo:background-color=\"#%06lx\"/>",
                               OWF_SHADE_RGB(p->cell_shading), OWF_SHADE_RGB(p->cell_shading));
                reg_style(w->reg, EL_STYLE, "table-cell", "ce", NULL, &body, name);
                owf_buf_printf(out, " table:style-name=\"%s\"", name);
            }
            owf_buf_putc(out, '>');
            /* every paragraph of this cell */
            while (i < o->text.nparas && o->text.paras[i].table_row == row && o->text.paras[i].table_col == p->table_col) {
                put_para(w, out, &o->text.paras[i], para_level(w, o, 0), 0, OWF_PH_NONE);
                i++;
            }
            owf_buf_puts(out, "</table:table-cell>");
            col++;
            if (col >= ncols)
                while (i < o->text.nparas && o->text.paras[i].table_row == row)
                    i++;
        }
        while (col < ncols) {
            owf_buf_puts(out, "<table:table-cell/>");
            col++;
        }
        owf_buf_puts(out, "</table:table-row>");
    }
    if (!o->text.nparas) {
        owf_buf_puts(out, "<table:table-row>");
        for (c = 0; c < ncols; c++)
            owf_buf_puts(out, "<table:table-cell/>");
        owf_buf_puts(out, "</table:table-row>");
    }
    owf_buf_puts(out, "</table:table>");
    if (body.failed)
        w->failed = 1;
    owf_buf_free(&body);
}

static void put_objects(odpw *w, owf_buf *out, const owf_obj *objs, int n, int parent, int depth);

/* A shape's custom geometry (ODF's enhanced geometry, 21600 units a side). */
static void put_custom_geometry(owf_buf *out, const owf_obj *o)
{
    const char *type, *path;
    switch (o->shape) {
    case OWF_SHAPE_TRIANGLE:
        type = "isosceles-triangle";
        path = "M 10800 0 L 21600 21600 0 21600 Z N";
        break;
    case OWF_SHAPE_DIAMOND:
        type = "diamond";
        path = "M 10800 0 L 21600 10800 10800 21600 0 10800 Z N";
        break;
    default:
        type = "right-arrow";
        path = "M 0 5400 L 16200 5400 16200 0 21600 10800 16200 21600 16200 16200 0 16200 Z N";
        break;
    }
    owf_buf_printf(out, "<draw:enhanced-geometry svg:viewBox=\"0 0 21600 21600\" draw:type=\"%s\"", type);
    if (o->flags & OWF_OBJ_FLIP_H)
        owf_buf_puts(out, " draw:mirror-horizontal=\"true\"");
    if ((o->flags & OWF_OBJ_FLIP_V) && o->shape != OWF_SHAPE_LINE)
        owf_buf_puts(out, " draw:mirror-vertical=\"true\"");
    owf_buf_printf(out, " draw:enhanced-path=\"%s\"/>", path);
}

static void put_object(odpw *w, owf_buf *out, const owf_obj *objs, int n, int index, int depth)
{
    const owf_obj *o = &objs[index];
    const char *cls = class_name(o->placeholder);
    char style[32];
    int presentation = o->placeholder != OWF_PH_NONE;

    if (depth > OWF_DECK_MAX_DEPTH)
        return;
    if (o->kind == OWF_OBJ_OTHER || o->kind == OWF_OBJ_CHART) {
        if (o->keep >= 0 && o->keep < w->deck->nkept && w->deck->kept[o->keep].kind == OWF_KEPT_OBJECT)
            owf_buf_puts(out, w->deck->kept[o->keep].xml);
        else
            owf_report_add(w->report, OWF_NOTE_LOST, "Objects OpenPresentation can't save were left out");
        return;
    }
    if (o->kind == OWF_OBJ_GROUP) {
        owf_buf_puts(out, "<draw:g");
        if (o->name) {
            owf_buf_puts(out, " draw:name=\"");
            owf_buf_puts_xml(out, o->name);
            owf_buf_putc(out, '"');
        }
        owf_buf_putc(out, '>');
        put_objects(w, out, objs, n, index, depth + 1);
        owf_buf_puts(out, "</draw:g>");
        return;
    }
    graphic_style(w, o, style, o->kind == OWF_OBJ_TEXT || o->kind == OWF_OBJ_SHAPE || o->kind == OWF_OBJ_TABLE);
    if (o->kind == OWF_OBJ_SHAPE) {
        const char *element;
        if (o->shape == OWF_SHAPE_LINE) {
            int x1 = o->x, y1 = o->y, x2 = o->x + o->width, y2 = o->y + o->height, t;
            if (o->flags & OWF_OBJ_FLIP_H) { t = x1; x1 = x2; x2 = t; }
            if (o->flags & OWF_OBJ_FLIP_V) { t = y1; y1 = y2; y2 = t; }
            owf_buf_printf(out, "<draw:line draw:style-name=\"%s\"", style);
            put_common(w, out, o);
            len_attr(out, "svg:x1", x1);
            len_attr(out, "svg:y1", y1);
            len_attr(out, "svg:x2", x2);
            len_attr(out, "svg:y2", y2);
            owf_buf_putc(out, '>');
            put_alt(out, o);
            put_story(w, out, o, &o->text, 1);
            owf_buf_puts(out, "</draw:line>");
            return;
        }
        element = o->shape == OWF_SHAPE_RECT || o->shape == OWF_SHAPE_ROUNDRECT ? "draw:rect"
                : o->shape == OWF_SHAPE_ELLIPSE ? "draw:ellipse"
                : o->shape == OWF_SHAPE_POLYGON ? "draw:polygon" : "draw:custom-shape";
        owf_buf_printf(out, "<%s %s=\"%s\"", element, presentation ? "presentation:style-name" : "draw:style-name", style);
        put_common(w, out, o);
        put_geometry(out, o);
        if (o->shape == OWF_SHAPE_ROUNDRECT)
            len_attr(out, "draw:corner-radius", o->radius);
        if (o->shape == OWF_SHAPE_POLYGON) {
            int k;
            owf_buf_printf(out, " svg:viewBox=\"0 0 %d %d\" draw:points=\"", o->width > 0 ? o->width : 1,
                           o->height > 0 ? o->height : 1);
            for (k = 0; k < o->npoints; k++)
                owf_buf_printf(out, "%s%d,%d", k ? " " : "", o->points[k * 2], o->points[k * 2 + 1]);
            if (!o->npoints)
                owf_buf_puts(out, "0,0");
            owf_buf_putc(out, '"');
        }
        if (cls)
            owf_buf_printf(out, " presentation:class=\"%s\"", cls);
        owf_buf_putc(out, '>');
        put_alt(out, o);
        put_story(w, out, o, &o->text, 1);
        if (!strcmp(element, "draw:custom-shape"))
            put_custom_geometry(out, o);
        owf_buf_printf(out, "</%s>", element);
        return;
    }
    /* frames: text, pictures, tables */
    owf_buf_printf(out, "<draw:frame %s=\"%s\"", presentation ? "presentation:style-name" : "draw:style-name", style);
    put_common(w, out, o);
    put_geometry(out, o);
    if (cls) {
        owf_buf_printf(out, " presentation:class=\"%s\"", cls);
        if (o->flags & OWF_OBJ_EMPTY)
            owf_buf_puts(out, " presentation:placeholder=\"true\"");
        if (!w->on_master)
            owf_buf_puts(out, " presentation:user-transformed=\"true\"");
    }
    owf_buf_putc(out, '>');
    if (o->kind == OWF_OBJ_PICTURE) {
        put_image(w, out, "draw:image", NULL, o->image);
        if (o->image < 0)
            owf_buf_puts(out, "<draw:text-box/>");
    } else if (o->kind == OWF_OBJ_TABLE) {
        put_table(w, out, o);
    } else {
        owf_buf_puts(out, "<draw:text-box>");
        if (!(o->flags & OWF_OBJ_EMPTY) || o->text.nparas)
            put_story(w, out, o, &o->text, 1);
        owf_buf_puts(out, "</draw:text-box>");
    }
    put_alt(out, o);
    owf_buf_puts(out, "</draw:frame>");
}

static void put_objects(odpw *w, owf_buf *out, const owf_obj *objs, int n, int parent, int depth)
{
    int i;
    for (i = 0; i < n; i++)
        if (objs[i].parent == parent)
            put_object(w, out, objs, n, i, depth);
}

/* ---- Pages ---- */

static void put_background(owf_buf *out, const owf_deck *d, unsigned long colour, int image, int master)
{
    if (image >= 0 && image < d->res->nimages)
        owf_buf_printf(out, " draw:fill=\"bitmap\" draw:fill-image-name=\"op-bg%d\" style:repeat=\"stretch\"", image + 1);
    else if (OWF_SHADE_SET(colour))
        owf_buf_printf(out, " draw:fill=\"solid\" draw:fill-color=\"#%06lx\"", OWF_SHADE_RGB(colour));
    else if (master)
        owf_buf_puts(out, " draw:fill=\"none\"");
}

static void put_transition(odpw *w, owf_buf *out, const owf_slide *s)
{
    const owf_transition *t = &s->transition;
    static const char *from[] = { "fromLeft", "fromRight", "fromTop", "fromBottom" };
    int dir = t->direction <= OWF_DIR_DOWN ? t->direction : 0;
    switch (t->kind) {
    case OWF_TRANS_FADE: owf_buf_puts(out, " smil:type=\"fade\" smil:subtype=\"crossfade\""); break;
    case OWF_TRANS_FADE_BLACK:
        owf_buf_puts(out, " smil:type=\"fade\" smil:subtype=\"fadeOverColor\" smil:fadeColor=\"#000000\"");
        break;
    case OWF_TRANS_WIPE:
        owf_buf_printf(out, " smil:type=\"barWipe\" smil:subtype=\"%s\"",
                       dir == OWF_DIR_UP || dir == OWF_DIR_DOWN ? "topToBottom" : "leftToRight");
        if (dir == OWF_DIR_RIGHT || dir == OWF_DIR_DOWN)
            owf_buf_puts(out, " smil:direction=\"reverse\"");
        break;
    case OWF_TRANS_PUSH: owf_buf_printf(out, " smil:type=\"pushWipe\" smil:subtype=\"%s\"", from[dir]); break;
    case OWF_TRANS_COVER: owf_buf_printf(out, " smil:type=\"slideWipe\" smil:subtype=\"%s\"", from[dir]); break;
    case OWF_TRANS_UNCOVER:
        owf_buf_printf(out, " smil:type=\"slideWipe\" smil:subtype=\"%s\" smil:direction=\"reverse\"", from[dir]);
        break;
    case OWF_TRANS_SPLIT:
        owf_buf_printf(out, " smil:type=\"barnDoorWipe\" smil:subtype=\"%s\"",
                       dir == OWF_DIR_UP || dir == OWF_DIR_DOWN ? "horizontal" : "vertical");
        break;
    case OWF_TRANS_BLINDS:
        owf_buf_printf(out, " smil:type=\"blindsWipe\" smil:subtype=\"%s\"",
                       dir == OWF_DIR_UP || dir == OWF_DIR_DOWN ? "horizontal" : "vertical");
        break;
    case OWF_TRANS_DISSOLVE: owf_buf_puts(out, " smil:type=\"dissolve\" smil:subtype=\"mode\""); break;
    case OWF_TRANS_OTHER:
        if (t->keep >= 0 && t->keep < w->deck->nkept && w->deck->kept[t->keep].kind == OWF_KEPT_ATTRIBUTES)
            owf_buf_puts(out, w->deck->kept[t->keep].xml);
        break;
    default: break;
    }
    if (t->kind != OWF_TRANS_NONE && t->duration > 0)
        owf_buf_printf(out, " presentation:transition-speed=\"%s\"",
                       t->duration <= 700 ? "fast" : t->duration >= 1500 ? "slow" : "medium");
    if (t->advance_after >= 0) {
        long ms = t->advance_after;
        owf_buf_puts(out, " presentation:transition-type=\"automatic\" presentation:duration=\"PT");
        owf_buf_printf(out, "%ldH%02ldM%02ld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60);
        if (ms % 1000)
            owf_buf_printf(out, ".%03ld", ms % 1000);
        owf_buf_puts(out, "S\"");
    }
}

static void put_notes(odpw *w, owf_buf *out, const owf_slide *s, int number, const char *page_layout)
{
    const owf_page *np = &w->deck->notes_page;
    owf_obj notes;
    int width = np->width - np->margin_left - np->margin_right;
    int thumb_w = width * 8 / 10, thumb_h;
    char style[32];

    if (thumb_w <= 0)
        thumb_w = 9000;
    thumb_h = (int)((long)thumb_w * w->deck->height / (w->deck->width > 0 ? w->deck->width : 1));
    owf_buf_puts(out, "<presentation:notes");
    if (page_layout)
        owf_buf_printf(out, " style:page-layout-name=\"%s\"", page_layout);
    owf_buf_putc(out, '>');
    owf_buf_printf(out, "<draw:page-thumbnail draw:layer=\"layout\" draw:page-number=\"%d\" presentation:class=\"page\"", number);
    len_attr(out, "svg:width", thumb_w);
    len_attr(out, "svg:height", thumb_h);
    len_attr(out, "svg:x", np->margin_left + (width - thumb_w) / 2);
    len_attr(out, "svg:y", np->margin_top);
    owf_buf_puts(out, "/>");
    owf_obj_init(&notes, OWF_OBJ_TEXT);
    notes.placeholder = OWF_PH_NOTES;
    notes.x = np->margin_left;
    notes.y = np->margin_top + thumb_h + 567;
    notes.width = width;
    notes.height = np->height - np->margin_bottom - notes.y;
    if (notes.height < 1000)
        notes.height = 1000;
    graphic_style(w, &notes, style, 1);
    owf_buf_printf(out, "<draw:frame presentation:style-name=\"%s\" draw:layer=\"layout\"", style);
    put_geometry(out, &notes);
    owf_buf_puts(out, " presentation:class=\"notes\"");
    if (s && !s->notes.nparas)
        owf_buf_puts(out, " presentation:placeholder=\"true\"");
    owf_buf_puts(out, "><draw:text-box>");
    if (s)
        put_story(w, out, &notes, &s->notes, 0);
    owf_buf_puts(out, "</draw:text-box></draw:frame></presentation:notes>");
}

static int build_content(odpw *w, owf_buf *out)
{
    const owf_deck *d = w->deck;
    owf_buf pages, body;
    int i;

    owf_buf_init(&pages);
    owf_buf_init(&body);
    w->reg = &w->autos_content;
    w->on_master = 0;
    /* footer and date declarations, then the pages */
    for (i = 0; i < d->nslides; i++) {
        const owf_slide *s = &d->slides[i];
        if (s->footer) {
            owf_buf_printf(&pages, "<presentation:footer-decl presentation:name=\"ftr%d\">", i + 1);
            owf_buf_puts_xml(&pages, s->footer);
            owf_buf_puts(&pages, "</presentation:footer-decl>");
        }
        if ((s->show & OWF_SHOW_DATE) || s->date_text) {
            owf_buf_printf(&pages, "<presentation:date-time-decl presentation:name=\"dtd%d\" presentation:source=\"%s\">",
                           i + 1, s->date_text ? "fixed" : "current");
            if (s->date_text)
                owf_buf_puts_xml(&pages, s->date_text);
            owf_buf_puts(&pages, "</presentation:date-time-decl>");
        }
    }
    for (i = 0; i < d->nslides; i++) {
        const owf_slide *s = &d->slides[i];
        char style[32], mname[96];
        w->master = owf_deck_slide_master(d, i);
        /* the page's style: background, fields, transition */
        body.length = 0;
        owf_buf_puts(&body, "<style:drawing-page-properties");
        put_background(&body, d, s->background, s->background_image, 0);
        owf_buf_printf(&body, " presentation:display-footer=\"%s\" presentation:display-page-number=\"%s\""
                              " presentation:display-date-time=\"%s\"",
                       s->show & OWF_SHOW_FOOTER ? "true" : "false", s->show & OWF_SHOW_NUMBER ? "true" : "false",
                       s->show & OWF_SHOW_DATE ? "true" : "false");
        if (s->hidden)
            owf_buf_puts(&body, " presentation:visibility=\"hidden\"");
        put_transition(w, &body, s);
        owf_buf_puts(&body, "/>");
        reg_style(w->reg, EL_STYLE, "drawing-page", "dp", NULL, &body, style);
        master_style_name(w->master, mname, sizeof mname);
        owf_buf_printf(&pages, "<draw:page draw:style-name=\"%s\" draw:master-page-name=\"%s\"", style, mname);
        if (s->name) {
            owf_buf_puts(&pages, " draw:name=\"");
            owf_buf_puts_xml(&pages, s->name);
            owf_buf_putc(&pages, '"');
        } else {
            owf_buf_printf(&pages, " draw:name=\"page%d\"", i + 1);
        }
        if (s->layout >= 0 && s->layout < d->nlayouts)
            ncname_attr(&pages, "presentation:presentation-page-layout-name", d->layouts[s->layout].name);
        if (s->footer)
            owf_buf_printf(&pages, " presentation:use-footer-name=\"ftr%d\"", i + 1);
        if ((s->show & OWF_SHOW_DATE) || s->date_text)
            owf_buf_printf(&pages, " presentation:use-date-time-name=\"dtd%d\"", i + 1);
        owf_buf_putc(&pages, '>');
        put_objects(w, &pages, s->objs, s->nobjs, -1, 0);
        if (s->keep_timing >= 0 && s->keep_timing < d->nkept && d->kept[s->keep_timing].kind == OWF_KEPT_TIMING)
            owf_buf_puts(&pages, d->kept[s->keep_timing].xml);
        put_notes(w, &pages, s, i + 1, NULL);
        owf_buf_puts(&pages, "</draw:page>");
    }

    owf_buf_puts(out, "<office:automatic-styles>");
    reg_write(out, &w->autos_content);
    for (i = 0; i < d->nkept; i++)
        if (d->kept[i].kind == OWF_KEPT_STYLE)
            owf_buf_puts(out, d->kept[i].xml);
    owf_buf_puts(out, "</office:automatic-styles><office:body><office:presentation>");
    owf_buf_put(out, pages.data, pages.length);
    for (i = 0; i < d->nkept; i++)
        if (d->kept[i].kind == OWF_KEPT_SETTINGS)
            owf_buf_puts(out, d->kept[i].xml);
    owf_buf_puts(out, "</office:presentation></office:body>");
    if (pages.failed || body.failed)
        w->failed = 1;
    owf_buf_free(&pages);
    owf_buf_free(&body);
    return w->failed || w->autos_content.failed ? OWF_ERR_MEMORY : OWF_OK;
}

/* ---- styles.xml ---- */

static void put_level_style(odpw *w, owf_buf *out, const char *name, const char *parent, const owf_textlevel *lv,
                            const owf_textlevel *list_levels, int outline)
{
    owf_buf_printf(out, "<style:style style:name=\"%s\" style:family=\"presentation\"", name);
    if (parent)
        owf_buf_printf(out, " style:parent-style-name=\"%s\"", parent);
    owf_buf_putc(out, '>');
    if (list_levels) {
        owf_buf_puts(out, "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"><text:list-style style:name=\"");
        owf_buf_puts(out, name);
        owf_buf_puts(out, "\">");
        put_list_levels(out, w->deck->res, list_levels);
        owf_buf_puts(out, "</text:list-style></style:graphic-properties>");
    } else {
        owf_buf_puts(out, "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>");
    }
    /* an outline level's indents are its list level's */
    put_para_props(out, &lv->para, NULL, 1, outline);
    put_text_props(out, w->deck->res, &lv->chars, NULL, 1);
    owf_buf_puts(out, "</style:style>");
}

static void put_master_styles(odpw *w, owf_buf *out, const owf_master *m)
{
    char base[96], name[128], parent[128];
    int i;
    master_style_name(m, base, sizeof base);
    snprintf(name, sizeof name, "%s-title", base);
    put_level_style(w, out, name, NULL, &m->title, NULL, 0);
    snprintf(name, sizeof name, "%s-subtitle", base);
    put_level_style(w, out, name, NULL, &m->subtitle, NULL, 0);
    for (i = 0; i < OWF_TEXT_LEVELS; i++) {
        snprintf(name, sizeof name, "%s-outline%d", base, i + 1);
        snprintf(parent, sizeof parent, "%s-outline%d", base, i);
        put_level_style(w, out, name, i ? parent : NULL, &m->body[i], i ? NULL : m->body, 1);
    }
    snprintf(name, sizeof name, "%s-notes", base);
    put_level_style(w, out, name, NULL, &m->notes, NULL, 0);
    snprintf(name, sizeof name, "%s-background", base);
    owf_buf_printf(out, "<style:style style:name=\"%s\" style:family=\"presentation\">"
                        "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/></style:style>", name);
    snprintf(name, sizeof name, "%s-backgroundobjects", base);
    put_level_style(w, out, name, NULL, &m->other, NULL, 0);
}

static void put_layouts(odpw *w, owf_buf *out)
{
    int i, k;
    for (i = 0; i < w->deck->nlayouts; i++) {
        const owf_slide_layout *l = &w->deck->layouts[i];
        owf_buf_puts(out, "<style:presentation-page-layout");
        ncname_attr(out, "style:name", l->name);
        if (needs_display_name(l->name)) {
            owf_buf_puts(out, " style:display-name=\"");
            owf_buf_puts_xml(out, l->name);
            owf_buf_putc(out, '"');
        }
        owf_buf_putc(out, '>');
        for (k = 0; k < l->nobjs; k++) {
            const char *cls = class_name(l->objs[k].placeholder);
            if (!cls)
                continue;
            owf_buf_printf(out, "<presentation:placeholder presentation:object=\"%s\"", cls);
            len_attr(out, "svg:x", l->objs[k].x);
            len_attr(out, "svg:y", l->objs[k].y);
            len_attr(out, "svg:width", l->objs[k].width);
            len_attr(out, "svg:height", l->objs[k].height);
            owf_buf_puts(out, "/>");
        }
        owf_buf_puts(out, "</style:presentation-page-layout>");
    }
}

static void put_page_layout(owf_buf *out, const char *name, int width, int height)
{
    owf_buf_printf(out, "<style:page-layout style:name=\"%s\"><style:page-layout-properties fo:margin-top=\"0pt\""
                        " fo:margin-bottom=\"0pt\" fo:margin-left=\"0pt\" fo:margin-right=\"0pt\"", name);
    len_attr(out, "fo:page-width", width);
    len_attr(out, "fo:page-height", height);
    owf_buf_printf(out, " style:print-orientation=\"%s\"/></style:page-layout>", width > height ? "landscape" : "portrait");
}

static int build_styles(odpw *w, owf_buf *out)
{
    const owf_deck *d = w->deck;
    owf_buf masters, body;
    int i;

    owf_buf_init(&masters);
    owf_buf_init(&body);
    w->reg = &w->autos_styles;
    w->on_master = 1;
    owf_buf_puts(&masters, "<office:master-styles><draw:layer-set><draw:layer draw:name=\"layout\"/>"
                           "<draw:layer draw:name=\"background\"/><draw:layer draw:name=\"backgroundobjects\"/>"
                           "<draw:layer draw:name=\"controls\"/><draw:layer draw:name=\"measurelines\"/></draw:layer-set>");
    for (i = 0; i < d->nmasters; i++) {
        const owf_master *m = &d->masters[i];
        char style[32];
        w->master = m;
        body.length = 0;
        owf_buf_puts(&body, "<style:drawing-page-properties draw:background-size=\"full\"");
        put_background(&body, d, m->background, m->background_image, 1);
        owf_buf_puts(&body, "/>");
        reg_style(w->reg, EL_STYLE, "drawing-page", "dp", NULL, &body, style);
        owf_buf_puts(&masters, "<style:master-page");
        ncname_attr(&masters, "style:name", m->name);
        if (needs_display_name(m->name)) {
            owf_buf_puts(&masters, " style:display-name=\"");
            owf_buf_puts_xml(&masters, m->name);
            owf_buf_putc(&masters, '"');
        }
        owf_buf_printf(&masters, " style:page-layout-name=\"opPM1\" draw:style-name=\"%s\">", style);
        put_objects(w, &masters, m->objs, m->nobjs, -1, 0);
        put_notes(w, &masters, NULL, 1, "opPM2");
        owf_buf_puts(&masters, "</style:master-page>");
    }
    owf_buf_puts(&masters, "</office:master-styles>");

    owf_buf_puts(out, "<office:styles>");
    owf_buf_puts(out, "<style:default-style style:family=\"graphic\">");
    {
        owf_charfmt base = d->res->base;
        if (base.size <= 0)
            base.size = 18 * OWF_TWIPS_PER_POINT;
        owf_buf_puts(out, "<style:graphic-properties draw:shadow=\"hidden\"/>");
        put_text_props(out, d->res, &base, NULL, 1);
    }
    owf_buf_puts(out, "</style:default-style>");
    owf_buf_puts(out, "<style:style style:name=\"standard\" style:family=\"graphic\">");
    /* the file's own graphic defaults, which kept objects inherit; else none */
    for (i = 0; i < d->nkept; i++)
        if (d->kept[i].kind == OWF_KEPT_GRAPHIC)
            break;
    owf_buf_puts(out, i < d->nkept ? d->kept[i].xml : "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\"/>");
    if (d->nmasters) {
        put_para_props(out, &d->masters[0].other.para, NULL, 1, 0);
        put_text_props(out, d->res, &d->masters[0].other.chars, NULL, 1);
    }
    owf_buf_puts(out, "</style:style>");
    owf_buf_puts(out, "<style:style style:name=\"objectwithoutfill\" style:family=\"graphic\" style:parent-style-name=\"standard\">"
                      "<style:graphic-properties draw:fill=\"none\"/></style:style>");
    owf_buf_puts(out, "<draw:marker draw:name=\"op-arrow\" svg:viewBox=\"0 0 20 30\" svg:d=\"M10 0l-10 30h20z\"/>");
    owf_buf_puts(out, "<draw:stroke-dash draw:name=\"op-dash\" draw:style=\"rect\" draw:dots1=\"1\" draw:dots1-length=\"200%\""
                      " draw:distance=\"100%\"/>");
    /* background pictures */
    for (i = 0; i < d->res->nimages; i++) {
        int used = 0, k;
        for (k = 0; k < d->nslides && !used; k++)
            used = d->slides[k].background_image == i;
        for (k = 0; k < d->nmasters && !used; k++)
            used = d->masters[k].background_image == i;
        if (!used)
            continue;
        {
            char extra[64];
            snprintf(extra, sizeof extra, " draw:name=\"op-bg%d\"", i + 1);
            put_image(w, out, "draw:fill-image", extra, i);
        }
    }
    for (i = 0; i < d->nkept; i++)
        if (d->kept[i].kind == OWF_KEPT_DEFINITION)
            owf_buf_puts(out, d->kept[i].xml);
    for (i = 0; i < d->nmasters; i++)
        put_master_styles(w, out, &d->masters[i]);
    put_layouts(w, out);
    owf_buf_puts(out, "</office:styles><office:automatic-styles>");
    put_page_layout(out, "opPM1", d->width, d->height);
    put_page_layout(out, "opPM2", d->notes_page.width > 0 ? d->notes_page.width : 11906,
                    d->notes_page.height > 0 ? d->notes_page.height : 16838);
    reg_write(out, &w->autos_styles);
    owf_buf_puts(out, "</office:automatic-styles>");
    owf_buf_put(out, masters.data, masters.length);
    if (masters.failed || body.failed)
        w->failed = 1;
    owf_buf_free(&masters);
    owf_buf_free(&body);
    return w->failed || w->autos_styles.failed ? OWF_ERR_MEMORY : OWF_OK;
}

static void put_meta(odpw *w, owf_buf *out)
{
    owf_buf_puts(out, "<office:meta><meta:generator>OpenPresentation/" OWF_VERSION " libowf</meta:generator>");
    if (w->deck->res->title) {
        owf_buf_puts(out, "<dc:title>");
        owf_buf_puts_xml(out, w->deck->res->title);
        owf_buf_puts(out, "</dc:title>");
    }
    owf_buf_puts(out, "</office:meta>");
}

static odpw *new_writer(const owf_deck *deck, owf_report *report, int flat)
{
    odpw *w = calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->deck = deck;
    w->report = report;
    w->flat = flat;
    w->autos_styles.prefix = "opM";
    w->autos_content.prefix = "op";
    w->pic_used = calloc((size_t)(deck->res->nimages > 0 ? deck->res->nimages : 1), sizeof *w->pic_used);
    if (!w->pic_used) {
        free(w);
        return NULL;
    }
    return w;
}

static void free_writer(odpw *w)
{
    reg_free(&w->autos_styles);
    reg_free(&w->autos_content);
    free(w->pic_used);
    free(w);
}

static int check_deck(const owf_deck *deck)
{
    return deck && deck->res && deck->nmasters > 0 && deck->width > 0 && deck->height > 0;
}

static int export_package(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report,
                          const char *mimetype)
{
    owf_buf out, part, styles;
    owf_zip *zip;
    odpw *w;
    int result, i;

    if (!check_deck(deck))
        return OWF_ERR_FORMAT;
    w = new_writer(deck, report, 0);
    if (!w)
        return OWF_ERR_MEMORY;
    owf_buf_init(&out);
    owf_buf_init(&part);
    owf_buf_init(&styles);
    zip = owf_zip_new(&out);
    if (!zip) {
        free_writer(w);
        return OWF_ERR_MEMORY;
    }
    result = owf_zip_add(zip, "mimetype", mimetype, strlen(mimetype), 0);

    /* content first: the pictures it uses, and its styles' registry */
    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content " NS ">");
    put_font_faces(&part, deck->res, report);
    if (result == OWF_OK)
        result = build_content(w, &part);
    owf_buf_puts(&part, "</office:document-content>\n");
    owf_buf_puts(&styles, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles " NS ">");
    put_font_faces(&styles, deck->res, NULL);
    if (result == OWF_OK)
        result = build_styles(w, &styles);
    owf_buf_puts(&styles, "</office:document-styles>\n");
    if (result == OWF_OK)
        result = part.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, "content.xml", part.data, part.length, 1);
    if (result == OWF_OK)
        result = styles.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, "styles.xml", styles.data, styles.length, 1);

    part.length = 0;
    owf_buf_puts(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta " NS ">");
    put_meta(w, &part);
    owf_buf_puts(&part, "</office:document-meta>\n");
    if (result == OWF_OK)
        result = part.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, "meta.xml", part.data, part.length, 1);

    /* pictures, kept members, then the manifest of them all */
    for (i = 0; i < deck->res->nimages && result == OWF_OK; i++) {
        const owf_image *im = &deck->res->images[i];
        owf_buf name;
        if (!w->pic_used[i])
            continue;
        owf_buf_init(&name);
        put_href(&name, i, im);
        owf_buf_putc(&name, 0);
        /* pictures are compressed already: stored */
        result = name.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, (const char *)name.data, im->data, im->length, 0);
        owf_buf_free(&name);
    }
    for (i = 0; i < deck->nparts && result == OWF_OK; i++)
        if (deck->parts[i].data && deck->parts[i].length)
            result = owf_zip_add(zip, deck->parts[i].name, deck->parts[i].data, deck->parts[i].length, 1);
        else if (deck->parts[i].name[0] && deck->parts[i].name[strlen(deck->parts[i].name) - 1] != '/')
            result = owf_zip_add(zip, deck->parts[i].name, "", 0, 0);

    part.length = 0;
    owf_buf_printf(&part, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
                          "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"%s\"/>"
                          "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
                          "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
                          "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>",
                   mimetype);
    for (i = 0; i < deck->res->nimages; i++) {
        const owf_image *im = &deck->res->images[i];
        if (!w->pic_used[i])
            continue;
        owf_buf_puts(&part, "<manifest:file-entry manifest:full-path=\"");
        put_href(&part, i, im);
        owf_buf_puts(&part, "\" manifest:media-type=\"");
        owf_buf_puts_xml(&part, im->mime ? im->mime : "application/octet-stream");
        owf_buf_puts(&part, "\"/>");
    }
    for (i = 0; i < deck->nparts; i++) {
        owf_buf_puts(&part, "<manifest:file-entry manifest:full-path=\"");
        owf_buf_puts_xml(&part, deck->parts[i].name);
        owf_buf_puts(&part, "\" manifest:media-type=\"");
        owf_buf_puts_xml(&part, deck->parts[i].mime ? deck->parts[i].mime : "");
        owf_buf_puts(&part, "\"/>");
    }
    owf_buf_puts(&part, "</manifest:manifest>\n");
    if (result == OWF_OK)
        result = part.failed ? OWF_ERR_MEMORY : owf_zip_add(zip, "META-INF/manifest.xml", part.data, part.length, 1);

    owf_buf_free(&part);
    owf_buf_free(&styles);
    free_writer(w);
    if (owf_zip_finish(zip) != OWF_OK && result == OWF_OK)
        result = OWF_ERR_MEMORY;
    if (result != OWF_OK) {
        owf_buf_free(&out);
        return result;
    }
    return owf_buf_take(&out, data, length);
}

int owf_export_odp(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report)
{
    return export_package(deck, data, length, report, MIME_ODP);
}

int owf_export_fodp(const owf_deck *deck, unsigned char **data, size_t *length, owf_report *report)
{
    owf_buf out, content, styles;
    odpw *w;
    int result;
    size_t a, b;
    static const char auto_open[] = "<office:automatic-styles>", auto_close[] = "</office:automatic-styles>";

    if (!check_deck(deck))
        return OWF_ERR_FORMAT;
    w = new_writer(deck, report, 1);
    if (!w)
        return OWF_ERR_MEMORY;
    owf_buf_init(&out);
    owf_buf_init(&content);
    owf_buf_init(&styles);
    result = build_content(w, &content);
    if (result == OWF_OK)
        result = build_styles(w, &styles);
    if (deck->nparts)
        owf_report_add(report, OWF_NOTE_LOST, "Embedded objects' own parts can't be kept in flat XML: save as .odp");
    if (result == OWF_OK && (content.failed || styles.failed))
        result = OWF_ERR_MEMORY;
    if (result == OWF_OK) {
        /* one office:automatic-styles: styles.xml's, then content.xml's */
        const char *s = (const char *)styles.data, *c = (const char *)content.data;
        const char *s_open, *s_close, *c_open, *c_close;
        owf_buf_putc(&styles, 0);
        owf_buf_putc(&content, 0);
        s = (const char *)styles.data;
        c = (const char *)content.data;
        s_open = strstr(s, auto_open);
        s_close = s_open ? strstr(s_open, auto_close) : NULL;
        c_open = strstr(c, auto_open);
        c_close = c_open ? strstr(c_open, auto_close) : NULL;
        if (!s_open || !s_close || !c_open || !c_close) {
            result = OWF_ERR_MEMORY;
        } else {
            owf_buf_puts(&out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document " NS
                               " office:mimetype=\"" MIME_ODP "\">");
            put_meta(w, &out);
            put_font_faces(&out, deck->res, report);
            /* office:styles */
            owf_buf_put(&out, s, (size_t)(s_open - s));
            a = (size_t)(s_open - s) + strlen(auto_open);
            b = (size_t)(s_close - s);
            owf_buf_puts(&out, auto_open);
            owf_buf_put(&out, s + a, b - a);
            a = (size_t)(c_open - c) + strlen(auto_open);
            b = (size_t)(c_close - c);
            owf_buf_put(&out, c + a, b - a);
            owf_buf_puts(&out, auto_close);
            owf_buf_puts(&out, s_close + strlen(auto_close));    /* the master styles */
            owf_buf_puts(&out, c_close + strlen(auto_close));    /* the body */
            owf_buf_puts(&out, "</office:document>\n");
        }
    }
    owf_buf_free(&content);
    owf_buf_free(&styles);
    free_writer(w);
    if (result != OWF_OK || out.failed) {
        owf_buf_free(&out);
        return result != OWF_OK ? result : OWF_ERR_MEMORY;
    }
    return owf_buf_take(&out, data, length);
}

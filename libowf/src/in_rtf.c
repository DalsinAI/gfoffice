/*
 * libowf: Rich Text Format import. The parser is deliberately independent of
 * GFOpenWrite's window: OWConvert and owfwrite.library use the same reader.
 * It keeps RTF's ordinary text and page, paragraph and character formatting;
 * unsupported embedded objects are named in the import report.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RTF_DEPTH 128
#define RTF_FONTS 64
#define RTF_COLOURS 64

enum { RTF_BODY, RTF_SKIP, RTF_FONTTBL, RTF_COLORTBL, RTF_PICTURE };

typedef struct {
    owf_charfmt chr;
    owf_parafmt para;
    owf_story *story;
    int destination;
    int uc;
    int fresh;
    int optional;
    int tab_kind;
    int tab_leader;
} rtf_state;

typedef struct {
    owf_builder b;
    owf_report *report;
    rtf_state state, stack[RTF_DEPTH];
    int depth, fallback;
    int font_ids[RTF_FONTS], font_indices[RTF_FONTS], nfonts;
    int default_font, font_id, font_kind;
    char font_name[128];
    size_t font_length;
    unsigned long colours[RTF_COLOURS];
    int ncolours, red, green, blue, colour_set;
    int pictures, objects, first_headers;
    int table_active, row_open, table_id, next_table, table_row, table_col, table_cols;
    owf_buf picture;
    int picture_kind, picture_nibble, picture_depth, picw, pich, picwgoal, pichgoal, picture_number;
} rtf;

static int hex_digit(int c);

static int word(const char *s, size_t n, const char *want)
{
    return strlen(want) == n && !memcmp(s, want, n);
}

static int rtf_detect(const unsigned char *data, size_t length)
{
    return length >= 6 && !memcmp(data, "{\\rtf", 5) && data[5] >= '0' && data[5] <= '9' ? 100 : 0;
}

static void apply_char(rtf *r)
{
    owf_builder_flush(&r->b);
    r->b.charfmt = r->state.chr;
}

static void apply_para(rtf *r)
{
    r->b.parafmt = r->state.para;
    if (r->b.para && !r->b.text.length)
        r->b.para->fmt = r->state.para;
}

static void put_codepoint(rtf *r, unsigned long c)
{
    if (r->state.destination == RTF_BODY && !r->state.optional)
        owf_buf_put_utf8(&r->b.text, c);
}

static void put_byte(rtf *r, unsigned char c)
{
    if (r->state.destination == RTF_BODY && !r->state.optional)
        owf_buf_put_cp1252(&r->b.text, &c, 1);
}

static int font_index(const rtf *r, int id)
{
    int i;
    for (i = 0; i < r->nfonts; ++i)
        if (r->font_ids[i] == id)
            return r->font_indices[i];
    return -1;
}

static void finish_font(rtf *r)
{
    owf_font_kind kind = (owf_font_kind)r->font_kind;
    const char *modern;
    size_t start = 0, end = r->font_length;
    int index;
    while (start < end && (r->font_name[start] == ' ' || r->font_name[start] == '\t')) ++start;
    while (end > start && (r->font_name[end - 1] == ' ' || r->font_name[end - 1] == '\t')) --end;
    r->font_name[end] = 0;
    if (end == start || r->nfonts >= RTF_FONTS) {
        r->font_length = 0;
        return;
    }
    modern = owf_font_modern(r->font_name + start, &kind);
    index = owf_doc_font(r->b.doc, modern ? modern : r->font_name + start, kind);
    if (index >= 0) {
        r->font_ids[r->nfonts] = r->font_id;
        r->font_indices[r->nfonts++] = index;
    } else {
        r->b.failed = 1;
    }
    r->font_length = 0;
}

static void finish_colour(rtf *r)
{
    if (r->ncolours < RTF_COLOURS)
        r->colours[r->ncolours++] = r->colour_set
            ? ((unsigned long)r->red << 16) | ((unsigned long)r->green << 8) | (unsigned long)r->blue
            : OWF_COLOUR_AUTO;
    r->red = r->green = r->blue = r->colour_set = 0;
}

static void start_picture(rtf *r)
{
    r->picture.length = 0;
    r->picture_kind = 0;
    r->picture_nibble = -1;
    r->picture_depth = r->depth;
    r->picw = r->pich = r->picwgoal = r->pichgoal = 0;
}

static void finish_picture(rtf *r)
{
    const char *mime = NULL, *ext = NULL;
    char name[48];
    int width, height, image;
    if (r->picture_kind == 1 || (r->picture.length >= 8 && !memcmp(r->picture.data, "\211PNG\r\n\032\n", 8))) {
        mime = "image/png"; ext = "png";
    } else if (r->picture_kind == 2 || (r->picture.length >= 2 && r->picture.data[0] == 0xFF && r->picture.data[1] == 0xD8)) {
        mime = "image/jpeg"; ext = "jpg";
    }
    if (r->picture.failed) {
        r->b.failed = 1;
        return;
    }
    if (!mime || !r->picture.length || r->picture_nibble >= 0) {
        ++r->pictures;
        return;
    }
    /* \picw and \pich are pixels (at most 100000, kept so when read), a twip is 1/15 of one */
    width = r->picwgoal > 0 ? r->picwgoal : r->picw > 0 ? r->picw * 15 : 1440;
    height = r->pichgoal > 0 ? r->pichgoal : r->pich > 0 ? r->pich * 15 : 1440;
    snprintf(name, sizeof name, "rtf-image-%d.%s", ++r->picture_number, ext);
    image = owf_doc_add_image(r->b.doc, name, mime, r->picture.data, r->picture.length, width, height, NULL);
    if (image < 0) r->b.failed = 1;
    else owf_builder_image(&r->b, image);
}

static void literal(rtf *r, unsigned char c)
{
    if (r->fallback > 0) {
        --r->fallback;
        return;
    }
    if (r->state.destination == RTF_FONTTBL) {
        if (c == ';') {
            finish_font(r);
        } else if (c >= 0x20 && r->font_length + 1 < sizeof r->font_name) {
            r->font_name[r->font_length++] = (char)c;
        }
    } else if (r->state.destination == RTF_COLORTBL) {
        if (c == ';') finish_colour(r);
    } else if (r->state.destination == RTF_PICTURE) {
        int h = hex_digit(c);
        if (h >= 0) {
            if (r->picture_nibble < 0) r->picture_nibble = h;
            else {
                owf_buf_putc(&r->picture, (r->picture_nibble << 4) | h);
                r->picture_nibble = -1;
            }
        }
    } else if (r->state.destination == RTF_BODY && !r->state.optional) {
        put_byte(r, c);
    }
}

static int skipped_destination(const char *s, size_t n, rtf *r)
{
    static const char *const names[] = {
        "stylesheet", "info", "object", "objdata", "headerf", "footerf", "footnote", "annotation",
        "listtable", "listoverridetable",
        "revtbl", "generator", "xmlnstbl", "datastore", "themedata", "colorschememapping", "fldinst"
    };
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (word(s, n, names[i])) {
            if (word(s, n, "object") || word(s, n, "objdata")) ++r->objects;
            else if (word(s, n, "headerf") || word(s, n, "footerf")) ++r->first_headers;
            return 1;
        }
    return 0;
}

static owf_story *story_destination(rtf *r, const char *s, size_t n)
{
    if (word(s, n, "header") || word(s, n, "headerr")) return &r->b.doc->header;
    if (word(s, n, "headerl")) return &r->b.doc->header_left;
    if (word(s, n, "footer") || word(s, n, "footerr")) return &r->b.doc->footer;
    if (word(s, n, "footerl")) return &r->b.doc->footer_left;
    return NULL;
}

static void switch_story(rtf *r, owf_story *story)
{
    owf_builder_flush(&r->b);
    r->b.para = NULL;
    r->state.story = story;
    r->b.story = story;
}

static void end_table_cell(rtf *r)
{
    owf_para *p;
    if (!r->row_open) return;
    owf_builder_end_para(&r->b);
    if (!r->b.story->nparas) return;
    p = &r->b.story->paras[r->b.story->nparas - 1];
    p->table_id = r->table_id;
    p->table_row = r->table_row;
    p->table_col = r->table_col++;
    p->table_cols = r->table_cols > 0 ? r->table_cols : r->table_col;
}

static void set_flag(rtf *r, unsigned flag, int on)
{
    if (on) r->state.chr.flags |= flag;
    else r->state.chr.flags &= ~flag;
    apply_char(r);
}

static void control(rtf *r, const char *s, size_t n, int have, long value)
{
    int on = !have || value != 0, k;
    owf_story *story;

    if (r->state.fresh && r->state.optional) {
        r->state.destination = RTF_SKIP;
        r->state.fresh = r->state.optional = 0;
        return;
    }
    if (r->state.fresh && r->state.destination == RTF_BODY) {
        if ((story = story_destination(r, s, n)) != NULL) switch_story(r, story);
        else if (word(s, n, "pict")) { start_picture(r); r->state.destination = RTF_PICTURE; }
        else if (word(s, n, "fonttbl")) r->state.destination = RTF_FONTTBL;
        else if (word(s, n, "colortbl")) r->state.destination = RTF_COLORTBL;
        else if (r->state.optional || skipped_destination(s, n, r)) r->state.destination = RTF_SKIP;
    }
    r->state.fresh = 0;
    r->state.optional = 0;

    if (r->state.destination == RTF_SKIP)
        return;
    if (r->state.destination == RTF_FONTTBL) {
        if (word(s, n, "f") && have) { r->font_id = (int)value; r->font_length = 0; }
        else if (word(s, n, "froman")) r->font_kind = OWF_FONT_SERIF;
        else if (word(s, n, "fswiss")) r->font_kind = OWF_FONT_SANS;
        else if (word(s, n, "fmodern")) r->font_kind = OWF_FONT_MONO;
        return;
    }
    if (r->state.destination == RTF_COLORTBL) {
        if (word(s, n, "red") && have) { r->red = (int)value & 255; r->colour_set = 1; }
        else if (word(s, n, "green") && have) { r->green = (int)value & 255; r->colour_set = 1; }
        else if (word(s, n, "blue") && have) { r->blue = (int)value & 255; r->colour_set = 1; }
        return;
    }
    if (r->state.destination == RTF_PICTURE) {
        if (word(s, n, "pngblip")) r->picture_kind = 1;
        else if (word(s, n, "jpegblip")) r->picture_kind = 2;
        else if (word(s, n, "picw") && have && value > 0 && value <= 100000L) r->picw = (int)value;
        else if (word(s, n, "pich") && have && value > 0 && value <= 100000L) r->pich = (int)value;
        else if (word(s, n, "picwgoal") && have && value > 0) r->picwgoal = (int)value;
        else if (word(s, n, "pichgoal") && have && value > 0) r->pichgoal = (int)value;
        return;
    }

    if (word(s, n, "b")) set_flag(r, OWF_BOLD, on);
    else if (word(s, n, "i")) set_flag(r, OWF_ITALIC, on);
    else if (word(s, n, "ul")) set_flag(r, OWF_UNDERLINE, on);
    else if (word(s, n, "ulnone")) set_flag(r, OWF_UNDERLINE, 0);
    else if (word(s, n, "strike")) set_flag(r, OWF_STRIKE, on);
    else if (word(s, n, "super")) { r->state.chr.flags &= ~OWF_SUB; set_flag(r, OWF_SUPER, 1); }
    else if (word(s, n, "sub")) { r->state.chr.flags &= ~OWF_SUPER; set_flag(r, OWF_SUB, 1); }
    else if (word(s, n, "nosupersub")) { r->state.chr.flags &= ~(OWF_SUPER | OWF_SUB); apply_char(r); }
    else if (word(s, n, "plain")) { owf_charfmt_init(&r->state.chr); apply_char(r); }
    else if (word(s, n, "fs") && have && value > 0 && value < 1000) { r->state.chr.size = (int)value * 10; apply_char(r); }
    else if (word(s, n, "f") && have && (k = font_index(r, (int)value)) >= 0) { r->state.chr.font = k; apply_char(r); }
    else if (word(s, n, "cf") && have && value >= 0 && value < r->ncolours) { r->state.chr.colour = r->colours[value]; apply_char(r); }
    else if (word(s, n, "pard")) {
        if (r->table_active && !r->row_open) r->table_active = 0;
        owf_parafmt_init(&r->state.para); apply_para(r);
    }
    else if (word(s, n, "ql")) { r->state.para.align = OWF_ALIGN_LEFT; apply_para(r); }
    else if (word(s, n, "qc")) { r->state.para.align = OWF_ALIGN_CENTRE; apply_para(r); }
    else if (word(s, n, "qr")) { r->state.para.align = OWF_ALIGN_RIGHT; apply_para(r); }
    else if (word(s, n, "qj")) { r->state.para.align = OWF_ALIGN_JUSTIFY; apply_para(r); }
    else if (word(s, n, "li") && have) { r->state.para.indent_left = (int)value; apply_para(r); }
    else if (word(s, n, "ri") && have) { r->state.para.indent_right = (int)value; apply_para(r); }
    else if (word(s, n, "fi") && have) { r->state.para.indent_first = (int)value; apply_para(r); }
    else if (word(s, n, "sb") && have) { r->state.para.space_before = (int)value; apply_para(r); }
    else if (word(s, n, "sa") && have) { r->state.para.space_after = (int)value; apply_para(r); }
    else if (word(s, n, "outlinelevel") && have && value >= 0 && value < 6) { r->state.para.heading = (int)value + 1; apply_para(r); }
    else if (word(s, n, "tqc")) r->state.tab_kind = OWF_TAB_CENTRE;
    else if (word(s, n, "tqr")) r->state.tab_kind = OWF_TAB_RIGHT;
    else if (word(s, n, "tqdec")) r->state.tab_kind = OWF_TAB_DECIMAL;
    else if (word(s, n, "tldot")) r->state.tab_leader = OWF_LEADER_DOT;
    else if (word(s, n, "tlhyph")) r->state.tab_leader = OWF_LEADER_HYPHEN;
    else if (word(s, n, "tlul")) r->state.tab_leader = OWF_LEADER_UNDERSCORE;
    else if (word(s, n, "tx") && have && r->state.para.ntabs < OWF_MAX_TABS) {
        owf_tab *t = &r->state.para.tabs[r->state.para.ntabs++];
        t->position = (int)value; t->kind = (owf_tab_kind)r->state.tab_kind; t->leader = (owf_tab_leader)r->state.tab_leader;
        r->state.tab_kind = OWF_TAB_LEFT; r->state.tab_leader = OWF_LEADER_NONE; apply_para(r);
    } else if (word(s, n, "trowd")) {
        if (!r->table_active) {
            r->table_active = 1;
            r->table_id = r->next_table++;
            r->table_row = 0;
        }
        r->row_open = 1;
        r->table_col = 0;
        r->table_cols = 0;
    } else if (word(s, n, "cellx") && have && r->row_open) {
        ++r->table_cols;
    } else if (word(s, n, "cell") && r->row_open) {
        end_table_cell(r);
    } else if (word(s, n, "row") && r->row_open) {
        while (r->table_col < r->table_cols) end_table_cell(r);
        ++r->table_row;
        r->row_open = 0;
    } else if (word(s, n, "par")) {
        if (r->row_open) owf_builder_special(&r->b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
        else owf_builder_end_para(&r->b);
    }
    else if (word(s, n, "line")) owf_builder_special(&r->b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
    else if (word(s, n, "tab")) owf_builder_special(&r->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
    else if (word(s, n, "page") || word(s, n, "sect")) {
        if (r->b.para || r->b.text.length) owf_builder_end_para(&r->b);
        r->state.para.page_break_before = 1; apply_para(r);
    } else if (word(s, n, "chpgn")) owf_builder_special(&r->b, OWF_RUN_FIELD, OWF_FIELD_PAGE);
    else if (word(s, n, "paperw") && have && value > 0) r->b.doc->page.width = (int)value;
    else if (word(s, n, "paperh") && have && value > 0) r->b.doc->page.height = (int)value;
    else if (word(s, n, "margl") && have && value >= 0) r->b.doc->page.margin_left = (int)value;
    else if (word(s, n, "margr") && have && value >= 0) r->b.doc->page.margin_right = (int)value;
    else if (word(s, n, "margt") && have && value >= 0) r->b.doc->page.margin_top = (int)value;
    else if (word(s, n, "margb") && have && value >= 0) r->b.doc->page.margin_bottom = (int)value;
    else if (word(s, n, "deff") && have) r->default_font = (int)value;
    else if (word(s, n, "uc") && have && value >= 0 && value < 16) r->state.uc = (int)value;
    else if (word(s, n, "u") && have) {
        unsigned long c = value < 0 ? (unsigned long)(value + 65536L) : (unsigned long)value;
        put_codepoint(r, c); r->fallback = r->state.uc;
    } else if (word(s, n, "emdash")) put_codepoint(r, 0x2014);
    else if (word(s, n, "endash")) put_codepoint(r, 0x2013);
    else if (word(s, n, "bullet")) put_codepoint(r, 0x2022);
    else if (word(s, n, "lquote")) put_codepoint(r, 0x2018);
    else if (word(s, n, "rquote")) put_codepoint(r, 0x2019);
    else if (word(s, n, "ldblquote")) put_codepoint(r, 0x201C);
    else if (word(s, n, "rdblquote")) put_codepoint(r, 0x201D);
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int rtf_fail(rtf *r, int error)
{
    owf_buf_free(&r->picture);
    owf_builder_done(&r->b);
    return error;
}

static int import_rtf(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    rtf r;
    size_t i = 0;
    memset(&r, 0, sizeof r);
    owf_builder_init(&r.b, doc, &doc->body);
    owf_buf_init(&r.picture);
    owf_charfmt_init(&r.state.chr);
    owf_parafmt_init(&r.state.para);
    r.state.uc = 1;
    r.state.fresh = 1;
    r.state.story = &doc->body;
    r.state.tab_kind = OWF_TAB_LEFT;
    r.default_font = r.font_id = -1;
    while (i < length) {
        int c = data[i++];
        if (c == '{') {
            if (r.depth >= RTF_DEPTH) return rtf_fail(&r, OWF_ERR_CORRUPT);
            r.stack[r.depth++] = r.state;
            r.state.fresh = 1;
            r.state.optional = 0;
        } else if (c == '}') {
            owf_story *old_story;
            if (!r.depth) return rtf_fail(&r, OWF_ERR_CORRUPT);
            if (r.state.destination == RTF_BODY && !r.state.optional) owf_builder_flush(&r.b);
            if (r.state.destination == RTF_PICTURE && r.depth == r.picture_depth) finish_picture(&r);
            old_story = r.state.story;
            r.state = r.stack[--r.depth];
            if (old_story != r.state.story) r.b.para = NULL;
            r.b.story = r.state.story;
            r.b.charfmt = r.state.chr;
            r.b.parafmt = r.state.para;
        } else if (c == '\\') {
            size_t start, end;
            long value = 0;
            int have = 0, neg = 0;
            if (i >= length) break;
            c = data[i++];
            if (c == '\\' || c == '{' || c == '}') literal(&r, (unsigned char)c);
            else if (c == '~') put_codepoint(&r, 0x00A0);
            else if (c == '-') put_codepoint(&r, 0x00AD);
            else if (c == '_') put_codepoint(&r, 0x2011);
            else if (c == '*') r.state.optional = 1;
            else if (c == '\r' || c == '\n') { /* a source line break */ }
            else if (c == '\'') {
                int a, b;
                if (i + 1 >= length || (a = hex_digit(data[i])) < 0 || (b = hex_digit(data[i + 1])) < 0) {
                    return rtf_fail(&r, OWF_ERR_CORRUPT);
                }
                i += 2;
                literal(&r, (unsigned char)((a << 4) | b));
            } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                start = i - 1;
                while (i < length && ((data[i] >= 'A' && data[i] <= 'Z') || (data[i] >= 'a' && data[i] <= 'z'))) ++i;
                end = i;
                if (i < length && data[i] == '-') { neg = 1; ++i; }
                while (i < length && data[i] >= '0' && data[i] <= '9') {
                    have = 1;
                    if (value < 100000000L) value = value * 10 + (data[i] - '0');
                    ++i;
                }
                if (neg) value = -value;
                if (i < length && data[i] == ' ') ++i;
                if (word((const char *)data + start, end - start, "bin") && have) {
                    size_t skip = value > 0 ? (size_t)value : 0;
                    if (skip > length - i) return rtf_fail(&r, OWF_ERR_CORRUPT);
                    if (r.state.destination == RTF_PICTURE) {
                        if (r.picture_nibble >= 0) return rtf_fail(&r, OWF_ERR_CORRUPT);
                        owf_buf_put(&r.picture, data + i, skip);
                    }
                    i += skip;
                } else {
                    control(&r, (const char *)data + start, end - start, have, value);
                }
            }
        } else if (c != '\r' && c != '\n' && c != 0) {
            literal(&r, (unsigned char)c);
        }
    }
    if (r.depth) return rtf_fail(&r, OWF_ERR_CORRUPT);
    if (r.default_font >= 0) doc->base.font = font_index(&r, r.default_font);
    if (r.pictures) owf_report_add(report, OWF_NOTE_LOST, "RTF pictures are not imported yet");
    if (r.objects) owf_report_add(report, OWF_NOTE_LOST, "RTF embedded objects are not imported yet");
    if (r.first_headers) owf_report_add(report, OWF_NOTE_LOST, "RTF first-page-only headers and footers are not imported yet");
    owf_report_add(report, OWF_NOTE_INFO, "Read Rich Text Format text and basic formatting");
    owf_buf_free(&r.picture);
    return owf_builder_done(&r.b);
}

const owf_format owf_format_rtf = {
    "rtf", "Rich Text Format", "rtf", rtf_detect, import_rtf, owf_export_rtf
};

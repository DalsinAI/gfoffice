/*
 * OpenWrite - the native OpenAmigaWriter shell.
 *
 * First build: OpenGadTools chrome, libowf documents, a lightweight native
 * page canvas, DOCX/ODT/classic-Amiga Open/Save, navigator, inspector and
 * status line.  OpenRTG is used by OpenGadTools automatically on true-colour
 * screens; the page renderer remains portable to AGA.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <graphics/text.h>
#include <graphics/gfx.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openwrite_core.h"
#include "ow_stack.h"
#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_font.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL;

#define VERSION_TEXT "OpenWrite 0.1-dev (6.10.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " MIT, Copyright (c) 2026 Dalsin Limited";

enum {
    C_NEW = 1, C_OPEN, C_SAVE, C_PRINT, C_UNDO, C_REDO,
    C_IMAGE, C_TABLE, C_PDF
};
enum { GID_TOOLBAR = 100 };

enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVE_AS, M_PRINT, M_PDF, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE,
    M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_100,
    M_NAV, M_INSPECTOR,
    M_IMAGE, M_TABLE, M_LINK,
    M_PARAGRAPH, M_PAGE_SETUP,
    M_DATATYPES, M_OPENPRINT,
    M_THEME_OPEN, M_THEME_GRAPHITE, M_THEME_EMBER, M_THEME_CLEAR, M_THEME_CLASSIC,
    M_TB_BOTH, M_TB_ICONS, M_TB_TEXT
};

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", NULL, 0, 0, (APTR)M_SAVE_AS },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Print...", "P", 0, 0, (APTR)M_PRINT },
    { NM_ITEM, "Export PDF...", NULL, 0, 0, (APTR)M_PDF },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "About...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },

    { NM_TITLE, "Edit", NULL, 0, 0, NULL },
    { NM_ITEM, "Undo", "Z", 0, 0, (APTR)M_UNDO },
    { NM_ITEM, "Redo", NULL, 0, 0, (APTR)M_REDO },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Cut", "X", 0, 0, (APTR)M_CUT },
    { NM_ITEM, "Copy", "C", 0, 0, (APTR)M_COPY },
    { NM_ITEM, "Paste", "V", 0, 0, (APTR)M_PASTE },

    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "Navigator", NULL, CHECKIT | CHECKED, 0, (APTR)M_NAV },
    { NM_ITEM, "Inspector", NULL, CHECKIT | CHECKED, 0, (APTR)M_INSPECTOR },
    { NM_ITEM, "Zoom in", "+", 0, 0, (APTR)M_ZOOM_IN },
    { NM_ITEM, "Zoom out", "-", 0, 0, (APTR)M_ZOOM_OUT },
    { NM_ITEM, "Actual size", "0", 0, 0, (APTR)M_ZOOM_100 },
    { NM_ITEM, "Toolbar", NULL, 0, 0, NULL },
    { NM_SUB, "Icons and text", NULL, CHECKIT | CHECKED, ~1 & 7, (APTR)M_TB_BOTH },
    { NM_SUB, "Icons only", NULL, CHECKIT, ~2 & 7, (APTR)M_TB_ICONS },
    { NM_SUB, "Text only", NULL, CHECKIT, ~4 & 7, (APTR)M_TB_TEXT },
    { NM_ITEM, "Theme", NULL, 0, 0, NULL },
    { NM_SUB, "Open", NULL, 0, 0, (APTR)M_THEME_OPEN },
    { NM_SUB, "Graphite", NULL, 0, 0, (APTR)M_THEME_GRAPHITE },
    { NM_SUB, "Ember", NULL, 0, 0, (APTR)M_THEME_EMBER },
    { NM_SUB, "Clear", NULL, 0, 0, (APTR)M_THEME_CLEAR },
    { NM_SUB, "Classic", NULL, 0, 0, (APTR)M_THEME_CLASSIC },

    { NM_TITLE, "Insert", NULL, 0, 0, NULL },
    { NM_ITEM, "Image...", NULL, 0, 0, (APTR)M_IMAGE },
    { NM_ITEM, "Table...", NULL, 0, 0, (APTR)M_TABLE },
    { NM_ITEM, "Link...", NULL, 0, 0, (APTR)M_LINK },

    { NM_TITLE, "Format", NULL, 0, 0, NULL },
    { NM_ITEM, "Paragraph...", NULL, 0, 0, (APTR)M_PARAGRAPH },

    { NM_TITLE, "Layout", NULL, 0, 0, NULL },
    { NM_ITEM, "Page Setup...", NULL, 0, 0, (APTR)M_PAGE_SETUP },

    { NM_TITLE, "Datatypes", NULL, 0, 0, NULL },
    { NM_ITEM, "OpenDatatypes...", NULL, 0, 0, (APTR)M_DATATYPES },

    { NM_TITLE, "Tools", NULL, 0, 0, NULL },
    { NM_ITEM, "OpenPrint...", NULL, 0, 0, (APTR)M_OPENPRINT },

    { NM_TITLE, "Help", NULL, 0, 0, NULL },
    { NM_ITEM, "About OpenWrite...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static const ogt_tool tools[] = {
    { C_NEW,   "New",    OGT_ICON_FILE,    0, 0, 0 },
    { C_OPEN,  "Open...",OGT_ICON_OPEN,    0, 0, 0 },
    { C_SAVE,  "Save",   OGT_ICON_DISK,    0, 0, 0 },
    { C_PRINT, "Print...",OGT_ICON_TEST,    1, 0, 0 },
    { C_UNDO,  "Undo",   OGT_ICON_PARENT,  1, 1, 0 },
    { C_REDO,  "Redo",   OGT_ICON_FORWARD, 0, 1, 0 },
    { C_IMAGE, "Image...",OGT_ICON_FILE,    1, 0, 0 },
    { C_TABLE, "Table...",OGT_ICON_FOLDER,  0, 0, 0 },
    { C_PDF,   "PDF...",  OGT_ICON_SENT,    1, 0, 0 }
};

typedef struct box { int x, y, w, h; } box;

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *chain;
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;
static int quit_now;

static ogt_theme theme;
static ogt_ctx ctx;
static ogt_toolbar tb;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS_TEXT;

static owf_doc *doc;
static ow_editor *editor;
static char current_path[512];
static char current_format[32] = "ODT";
static char status[256] = "Ready.";
static int zoom = 100;
static int show_nav = 1, show_inspector = 1;
static int page_index;

static box area, toolbar_box, format_box, navigator_box, canvas_box;
static box ruler_box, page_box, inspector_box, status_box;

static int render_x0, render_y0, render_num = 1, render_den = 1;
static int render_bottom, render_last_y = -1;

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

static char *read_file(const char *path)
{
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    LONG n, cap = 32768;
    char *buf;
    if (!f) return NULL;
    buf = malloc(cap);
    if (!buf) { Close(f); return NULL; }
    n = Read(f, buf, cap - 1);
    Close(f);
    if (n < 0) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

static void load_theme(void)
{
    char path[160], err[120];
    char *text;
    snprintf(path, sizeof path, "PROGDIR:Themes/%s.theme", theme_name);
    text = read_file(path);
    if (!text) {
        snprintf(path, sizeof path, "SYS:Prefs/Presets/Themes/%s.theme", theme_name);
        text = read_file(path);
    }
    if (theme.text) ogt_theme_free(&theme);
    if (!text || !ogt_theme_parse(&theme, text, err, sizeof err)) {
        ogt_theme_parse(&theme, classic_theme, err, sizeof err);
        strcpy(theme_name, "Classic");
    }
    free(text);
}

static void read_theme_choice(void)
{
    char *s = read_file("ENV:OpenGadTools/Theme");
    if (!s) return;
    {
        char *nl = strchr(s, '\n');
        if (nl) *nl = 0;
        if (*s && strlen(s) < sizeof theme_name) strcpy(theme_name, s);
        theme_mode = nl && !strncmp(nl + 1, "dark", 4) ? OGT_DARK : OGT_LIGHT;
    }
    free(s);
}

static void tell(const char *title, const char *text)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)"OK"
    };
    EasyRequest(win, &es, NULL, (ULONG)text);
}

static void set_status(const char *s)
{
    snprintf(status, sizeof status, "%s", s ? s : "");
}

static const char *leaf(const char *path)
{
    const char *s = FilePart((STRPTR)path);
    return s && *s ? s : path;
}

static int screen_depth(void)
{
    if (((struct Library *)GfxBase)->lib_Version >= 39)
        return (int)GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    return scr->RastPort.BitMap->Depth;
}

static void field(struct RastPort *rp, const char *fill, int x, int y, int w, int h)
{
    if (w < 3 || h < 3) return;
    ogt_fill(&ctx, rp, fill, x + 1, y + 1, w - 2, h - 2);
    ogt_bevel(rp, ogt_pen(&ctx, "string.shadow"), ogt_pen(&ctx, "string.shine"),
              x, y, w, h);
}

static void raised(struct RastPort *rp, int x, int y, int w, int h)
{
    ogt_fill(&ctx, rp, "button", x + 1, y + 1, w - 2, h - 2);
    ogt_bevel(rp, ogt_pen(&ctx, "button.shine"), ogt_pen(&ctx, "button.shadow"),
              x, y, w, h);
}

static owf_para *add_para(owf_doc *d, const char *text, int heading, int bold, int before, int after)
{
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    pf.heading = heading;
    pf.space_before = before;
    pf.space_after = after;
    if (bold) cf.flags |= OWF_BOLD;
    if (heading == 1) cf.size = 24 * OWF_TWIPS_PER_POINT;
    else if (heading == 2) cf.size = 16 * OWF_TWIPS_PER_POINT;
    p = owf_story_add(&d->body, &pf);
    if (!p) return NULL;
    if (owf_para_add_text(p, &cf, text, strlen(text)) != OWF_OK) return NULL;
    return p;
}

static owf_doc *sample_document(void)
{
    owf_doc *d = owf_doc_new();
    if (!d) return NULL;
    owf_doc_set_title(d, "OpenWrite Design Overview");
    if (!add_para(d, "OPENWRITE DESIGN OVERVIEW", 1, 1, 0, 180)) goto fail;
    if (!add_para(d, "A native word processor for the modern Amiga", 2, 1, 0, 260)) goto fail;
    if (!add_para(d, "OpenWrite combines the lightness of classic Amiga software with modern document compatibility. The application chrome is OpenGadTools; the page is rendered as a document, not as a web page.", 0, 0, 0, 160)) goto fail;
    if (!add_para(d, "Designed for the Open stack", 2, 1, 100, 120)) goto fail;
    if (!add_para(d, "OpenRTG provides the fast true-colour canvas and smooth page caches. OpenDatatypes supplies document objects and media. OpenPrint owns PDF, printer profiles and network output.", 0, 0, 0, 160)) goto fail;
    if (!add_para(d, "Compatibility without the weight", 2, 1, 100, 120)) goto fail;
    if (!add_para(d, "DOCX and ODT pass through libowf alongside Final Writer, Wordworth, ProWrite and other Amiga document formats. The native editor remains small C code suitable for an A1200.", 0, 0, 0, 120)) goto fail;
    return d;
fail:
    owf_doc_free(d);
    return NULL;
}

static void set_document(owf_doc *newdoc, const char *path, const char *fmt)
{
    if (editor) ow_editor_free(editor);
    if (doc) owf_doc_free(doc);
    doc = newdoc;
    editor = doc ? ow_editor_new(doc) : NULL;
    if (editor) {
        ow_editor_set_zoom(editor, zoom);
        ow_editor_layout(editor);
    }
    page_index = 0;
    snprintf(current_path, sizeof current_path, "%s", path ? path : "");
    snprintf(current_format, sizeof current_format, "%s", fmt && *fmt ? fmt : "ODT");
}

static int ask_file(int save, char *out, size_t out_size)
{
    struct FileRequester *fr;
    int ok = 0;
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(save ? "Save OpenWrite document" : "Open document"),
        ASLFR_Window, (ULONG)win,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, save ? TRUE : FALSE,
        ASLFR_InitialFile, (ULONG)(save ? (current_path[0] ? leaf(current_path) : "Untitled.odt") : ""),
        ASLFR_PositiveText, (ULONG)(save ? "Save" : "Open"),
        TAG_DONE);
    if (!fr) return 0;
    if (AslRequestTags(fr, TAG_DONE) && fr->fr_File[0]) {
        snprintf(out, out_size, "%s", fr->fr_Drawer);
        AddPart((STRPTR)out, fr->fr_File, out_size);
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

static void open_document(const char *path)
{
    owf_doc *newdoc = NULL;
    owf_report *report = owf_report_new();
    const owf_format *used = NULL;
    char msg[256];
    int rc = owf_import_file(path, NULL, &newdoc, report, &used);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "OpenWrite could not open the document.\n\n%s", owf_error_text(rc));
        tell("Open document", msg);
        owf_report_free(report);
        return;
    }
    set_document(newdoc, path, used ? used->name : "document");
    snprintf(msg, sizeof msg, "%s opened%s%s.",
             leaf(path),
             report && owf_report_count(report) ? "; compatibility notes: " : "",
             report && owf_report_count(report) ? "see import report" : "");
    set_status(msg);
    owf_report_free(report);
}

static void save_document_as(const char *path)
{
    owf_report *report = owf_report_new();
    char msg[256];
    int rc;
    if (!doc) return;
    rc = owf_export_file(doc, path, NULL, report);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "OpenWrite could not save this document.\n\n%s", owf_error_text(rc));
        tell("Save document", msg);
        owf_report_free(report);
        return;
    }
    snprintf(current_path, sizeof current_path, "%s", path);
    {
        const owf_format *f = owf_format_for_filename(path);
        if (f) snprintf(current_format, sizeof current_format, "%s", f->name);
    }
    snprintf(msg, sizeof msg, "%s saved.", leaf(path));
    set_status(msg);
    owf_report_free(report);
}

static int word_count(void)
{
    int i, j, words = 0, inword = 0;
    if (!doc) return 0;
    for (i = 0; i < doc->body.nparas; ++i) {
        for (j = 0; j < doc->body.paras[i].nruns; ++j) {
            const char *s = doc->body.paras[i].runs[j].text;
            if (!s) continue;
            while (*s) {
                if (isspace((unsigned char)*s)) inword = 0;
                else if (!inword) { ++words; inword = 1; }
                ++s;
            }
            inword = 0;
        }
    }
    return words;
}

static void render_begin(void *ud, const ow_page_info *p)
{
    (void)ud; (void)p;
}

static void render_end(void *ud, const ow_page_info *p)
{
    (void)ud; (void)p;
}

static void render_text(void *ud, int x, int y, const char *utf8, const owf_charfmt *fmt)
{
    struct RastPort *rp = (struct RastPort *)ud;
    int px, py, style = FS_NORMAL, maxw, lineh, first = 1;
    const char *s = utf8;
    if (!utf8 || !*utf8) return;
    px = render_x0 + x * render_num / render_den;
    py = render_y0 + y * render_num / render_den;
    lineh = fh + 2;
    if (y != render_last_y && render_bottom > py) py = render_bottom + 2;
    render_last_y = y;
    maxw = page_box.x + page_box.w - 9 - px;
    if (maxw < 20) return;
    if (fmt && (fmt->flags & OWF_BOLD)) style |= FSF_BOLD;
    if (fmt && (fmt->flags & OWF_ITALIC)) style |= FSF_ITALIC;
    if (fmt && (fmt->flags & OWF_UNDERLINE)) style |= FSF_UNDERLINED;
    SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);

    while (*s && py + fh < page_box.y + page_box.h - 7) {
        char line[192];
        int n = 0, last_space = -1, take;
        const char *q = s;
        while (*q && *q != '\n' && n < (int)sizeof(line) - 1) {
            line[n] = *q;
            line[n + 1] = 0;
            if (*q == ' ' || *q == '\t') last_space = n;
            if (ogt_text_width(rp, line) > maxw) break;
            ++n; ++q;
        }
        if (!n) { line[0] = *s; n = 1; }
        take = n;
        if (*q && *q != '\n' && ogt_text_width(rp, line) > maxw) {
            if (last_space > 0) take = last_space;
            else if (take > 1) --take;
        }
        while (take > 0 && (s[take - 1] == ' ' || s[take - 1] == '\t')) --take;
        if (take <= 0) take = 1;
        if (take >= (int)sizeof(line)) take = sizeof(line) - 1;
        memcpy(line, s, (size_t)take);
        line[take] = 0;
        ogt_text(rp, ogt_pen(&ctx, "fill.text"), px, py, line, maxw);
        py += lineh;
        s += take;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '\n') { ++s; py += 2; }
        if (first) first = 0;
    }
    render_bottom = py;
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
}

static void draw_format_bar(void)
{
    struct RastPort *rp = win->RPort;
    int x = format_box.x + 6, y = format_box.y + 4;
    int h = format_box.h - 8, gap = 5;
    int stylew = 104, fontw = 140, sizew = 46;
    LONG textpen = ogt_pen(&ctx, "label");
    field(rp, "string", x, y, stylew, h);
    ogt_text(rp, textpen, x + 7, y + (h - fh) / 2, "Body Text  v", stylew - 12);
    x += stylew + gap;
    field(rp, "string", x, y, fontw, h);
    ogt_text(rp, textpen, x + 7, y + (h - fh) / 2, "CGTriumvirate  v", fontw - 12);
    x += fontw + gap;
    field(rp, "string", x, y, sizew, h);
    ogt_text(rp, textpen, x + 8, y + (h - fh) / 2, "12", sizew - 12);
    x += sizew + gap;
    {
        const char *labels[] = { "B", "I", "U", "L", "C", "R", "J", "•", "1." };
        int i, bw = fh + 11;
        for (i = 0; i < 9 && x + bw < format_box.x + format_box.w - 4; ++i) {
            raised(rp, x, y, bw, h);
            if (i == 0) ogt_bold(rp, 1);
            ogt_text(rp, textpen, x + (bw - ogt_text_width(rp, labels[i])) / 2,
                     y + (h - fh) / 2, labels[i], bw - 4);
            if (i == 0) ogt_bold(rp, 0);
            x += bw + 3;
        }
    }
}

static void draw_tabs(struct RastPort *rp, int x, int y, int w, const char **labels, int n, int active)
{
    int i, tw = w / n;
    for (i = 0; i < n; ++i) {
        if (i == active) ogt_fill(&ctx, rp, "button", x + i * tw, y, tw, fh + 8);
        ogt_hline(rp, ogt_pen(&ctx, "group.line"), x + i * tw, y + fh + 7, tw);
        if (i) ogt_vline(rp, ogt_pen(&ctx, "group.line"), x + i * tw, y + 2, fh + 4);
        ogt_text(rp, ogt_pen(&ctx, "label"), x + i * tw + 5, y + 4, labels[i], tw - 10);
    }
}

static void draw_navigator(void)
{
    struct RastPort *rp = win->RPort;
    const char *tabs[] = { "Pages", "Outline", "Styles", "Assets" };
    int top, i, pages = editor ? ow_editor_page_count(editor) : 1;
    if (!navigator_box.w) return;
    ogt_fill(&ctx, rp, "window", navigator_box.x, navigator_box.y, navigator_box.w, navigator_box.h);
    if (navigator_box.w < 210) {
        field(rp, "string", navigator_box.x + 5, navigator_box.y + 3, navigator_box.w - 10, fh + 8);
        ogt_text(rp, ogt_pen(&ctx, "label"), navigator_box.x + 12, navigator_box.y + 7, "Pages  v", navigator_box.w - 24);
    } else draw_tabs(rp, navigator_box.x, navigator_box.y, navigator_box.w, tabs, 4, 0);
    top = navigator_box.y + fh + 13;
    field(rp, "list", navigator_box.x + 5, top, navigator_box.w - 10, navigator_box.h - (top - navigator_box.y) - 5);
    for (i = 0; i < pages && i < 4; ++i) {
        int pw = (navigator_box.w - 34) * 2 / 3;
        int ph = pw * 141 / 100;
        int px = navigator_box.x + (navigator_box.w - pw) / 2;
        int py = top + 9 + i * (ph + fh + 11);
        if (py + ph + fh > navigator_box.y + navigator_box.h - 8) break;
        ogt_box(rp, ogt_pen(&ctx, "track"), px + 2, py + 2, pw, ph);
        ogt_box(rp, ogt_pen_rgb(&ctx, (ogt_rgb){255,255,255}), px, py, pw, ph);
        ogt_frame(rp, ogt_pen(&ctx, i == page_index ? "accent" : "group.line"), px, py, pw, ph);
        {
            char s[16];
            snprintf(s, sizeof s, "%d", i + 1);
            ogt_text(rp, ogt_pen(&ctx, "muted"), navigator_box.x + (navigator_box.w - ogt_text_width(rp, s)) / 2,
                     py + ph + 3, s, 0);
        }
    }
}

static void draw_inspector(void)
{
    struct RastPort *rp = win->RPort;
    const char *tabs[] = { "Properties", "Styles", "Datatypes", "OpenPrint" };
    int x, y, fw;
    char buf[96];
    if (!inspector_box.w) return;
    ogt_fill(&ctx, rp, "window", inspector_box.x, inspector_box.y, inspector_box.w, inspector_box.h);
    draw_tabs(rp, inspector_box.x, inspector_box.y, inspector_box.w, tabs, 4, 0);
    x = inspector_box.x + 8;
    y = inspector_box.y + fh + 18;
    fw = inspector_box.w - 16;
    ogt_bold(rp, 1);
    ogt_text(rp, ogt_pen(&ctx, "label"), x, y, "Document", fw);
    ogt_bold(rp, 0);
    y += fh + 7;
    snprintf(buf, sizeof buf, "Format     %s", current_format);
    field(rp, "string", x, y, fw, fh + 8);
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 6, y + 4, buf, fw - 12);
    y += fh + 13;
    field(rp, "string", x, y, fw, fh + 8);
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 6, y + 4, "Page       A4 portrait", fw - 12);
    y += fh + 13;
    snprintf(buf, sizeof buf, "Zoom       %d%%", zoom);
    field(rp, "string", x, y, fw, fh + 8);
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 6, y + 4, buf, fw - 12);
    y += fh + 18;

    ogt_bold(rp, 1);
    ogt_text(rp, ogt_pen(&ctx, "label"), x, y, "Open stack", fw);
    ogt_bold(rp, 0);
    y += fh + 7;
    snprintf(buf, sizeof buf, "OpenRTG    %s", screen_depth() > 8 ? "true-colour" : "AGA");
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 3, y, buf, fw - 6);
    y += fh + 5;
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 3, y, "Datatypes  ready", fw - 6);
    y += fh + 5;
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 3, y, "OpenPrint  ready", fw - 6);
    y += fh + 13;
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), x, y, fw);
    y += 7;
    ogt_text(rp, ogt_pen(&ctx, "muted"), x, y, "DOCX / ODT / Amiga", fw);
    y += fh + 3;
    ogt_text(rp, ogt_pen(&ctx, "muted"), x, y, "via libowf", fw);
}

static void draw_ruler(void)
{
    struct RastPort *rp = win->RPort;
    int i, base = ruler_box.y + ruler_box.h - 3;
    ogt_fill(&ctx, rp, "window", ruler_box.x, ruler_box.y, ruler_box.w, ruler_box.h);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), ruler_box.x, base, ruler_box.w);
    for (i = 0; i <= 10; ++i) {
        int x = ruler_box.x + 8 + (ruler_box.w - 16) * i / 10;
        int h = (i % 5 == 0) ? 7 : 4;
        ogt_vline(rp, ogt_pen(&ctx, "muted"), x, base - h + 1, h);
    }
}

static void draw_page(void)
{
    struct RastPort *rp = win->RPort;
    int availw, availh, pw, ph, scale_num, scale_den;
    ow_renderer r;
    LONG paper = ogt_pen_rgb(&ctx, (ogt_rgb){255,255,255});
    LONG shadow = ogt_pen(&ctx, "track");
    LONG edge = ogt_pen(&ctx, "group.line");
    if (!doc || !editor) return;
    availw = canvas_box.w - 28;
    availh = canvas_box.h - ruler_box.h - 24;
    if (availw < 80 || availh < 80) return;
    scale_num = availw;
    scale_den = doc->page.width;
    if (doc->page.height * scale_num / scale_den > availh) {
        scale_num = availh;
        scale_den = doc->page.height;
    }
    scale_num = scale_num * zoom / 100;
    pw = doc->page.width * scale_num / scale_den;
    ph = doc->page.height * scale_num / scale_den;
    if (pw > availw) pw = availw;
    if (ph > availh) ph = availh;
    page_box.w = pw;
    page_box.h = ph;
    page_box.x = canvas_box.x + (canvas_box.w - pw) / 2;
    page_box.y = ruler_box.y + ruler_box.h + 10;
    ogt_box(rp, shadow, page_box.x + 4, page_box.y + 4, pw, ph);
    ogt_box(rp, paper, page_box.x, page_box.y, pw, ph);
    ogt_frame(rp, edge, page_box.x, page_box.y, pw, ph);

    render_x0 = page_box.x;
    render_y0 = page_box.y;
    render_bottom = page_box.y;
    render_last_y = -1;
    render_num = scale_num;
    render_den = scale_den;
    memset(&r, 0, sizeof r);
    r.userdata = rp;
    r.begin_page = render_begin;
    r.end_page = render_end;
    r.text = render_text;
    ow_editor_render_page(editor, page_index, &r);
}

static void draw_status(void)
{
    struct RastPort *rp = win->RPort;
    char left[260], right[64];
    int pages = editor ? ow_editor_page_count(editor) : 1;
    snprintf(left, sizeof left, " Page %d of %d  |  %d words  |  %s  |  %s",
             page_index + 1, pages, word_count(), current_format, status);
    snprintf(right, sizeof right, "%s  |  %d%% ",
             screen_depth() > 8 ? "OpenRTG" : "AGA", zoom);
    field(rp, "string", status_box.x, status_box.y, status_box.w, status_box.h);
    ogt_text(rp, ogt_pen(&ctx, "label"), status_box.x + 4,
             status_box.y + (status_box.h - fh) / 2, left,
             status_box.w - ogt_text_width(rp, right) - 14);
    ogt_text(rp, ogt_pen(&ctx, "label"),
             status_box.x + status_box.w - ogt_text_width(rp, right) - 5,
             status_box.y + (status_box.h - fh) / 2, right, 0);
}

static void draw_all(void)
{
    struct RastPort *rp;
    if (!win) return;
    rp = win->RPort;
    SetFont(rp, font);
    ogt_fill(&ctx, rp, "window", area.x, area.y, area.w, area.h);
    ogt_toolbar_draw(&tb, &ctx, rp, "window");
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), toolbar_box.x, toolbar_box.y + toolbar_box.h - 1, toolbar_box.w);
    draw_format_bar();
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), format_box.x, format_box.y + format_box.h - 1, format_box.w);

    if (navigator_box.w) {
        draw_navigator();
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), navigator_box.x + navigator_box.w, navigator_box.y, navigator_box.h);
    }
    ogt_fill(&ctx, rp, "list", canvas_box.x, canvas_box.y, canvas_box.w, canvas_box.h);
    draw_ruler();
    draw_page();
    if (inspector_box.w) {
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), inspector_box.x - 1, inspector_box.y, inspector_box.h);
        draw_inspector();
    }
    draw_status();
}

static void free_gadgets(void)
{
    if (chain) {
        RemoveGList(win, chain, -1);
        chain = NULL;
    }
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    int tbh, use_style, navw, inspw, body_y, body_h;
    free_gadgets();
    area.x = win->BorderLeft;
    area.y = win->BorderTop;
    area.w = win->Width - win->BorderLeft - win->BorderRight;
    area.h = win->Height - win->BorderTop - win->BorderBottom;
    SetFont(rp, font);

    use_style = area.w < 780 ? OGT_TB_ICONS : tb_style;
    ogt_toolbar_set(&tb, tools, sizeof tools / sizeof tools[0], use_style);
    tbh = ogt_toolbar_layout(&tb, rp, area.x + 5, area.y + 3, area.w - 10);
    toolbar_box = (box){ area.x, area.y, area.w, tbh + 7 };
    format_box = (box){ area.x, toolbar_box.y + toolbar_box.h, area.w, fh + 16 };
    status_box = (box){ area.x + 5, area.y + area.h - fh - 11, area.w - 10, fh + 7 };

    body_y = format_box.y + format_box.h;
    body_h = status_box.y - body_y - 5;
    navw = show_nav && area.w >= 560 ? (area.w < 850 ? 138 : area.w / 7) : 0;
    inspw = show_inspector && area.w >= 900 ? (area.w < 1200 ? 205 : area.w / 5) : 0;
    navigator_box = (box){ area.x, body_y, navw, body_h };
    inspector_box = (box){ area.x + area.w - inspw, body_y, inspw, body_h };
    canvas_box = (box){ area.x + navw + (navw ? 1 : 0), body_y,
                        area.w - navw - inspw - (navw ? 1 : 0) - (inspw ? 1 : 0), body_h };
    ruler_box = (box){ canvas_box.x, canvas_box.y, canvas_box.w, fh + 10 };

    if ((chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR)))
        AddGList(win, chain, ~0, -1, NULL);
}

static void relayout(void)
{
    layout();
    draw_all();
}

static void apply_theme(const char *name)
{
    if (name && *name) snprintf(theme_name, sizeof theme_name, "%s", name);
    ogt_ctx_free(&ctx);
    load_theme();
    {
        struct TextFont *f = ogt_open_font(&theme, scr, &font_attr);
        if (f) {
            if (font) CloseFont(font);
            font = f;
            fh = font->tf_YSize;
        }
    }
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    if (win) relayout();
}

static int open_window(void)
{
    int w, h;
    if (!(scr = LockPubScreen(NULL))) return 0;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return 0;
    load_theme();
    if (!(font = ogt_open_font(&theme, scr, &font_attr))) return 0;
    fh = font->tf_YSize;
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);

    w = scr->Width >= 1200 ? scr->Width * 9 / 10 : scr->Width - 12;
    h = scr->Height >= 700 ? (scr->Height - scr->BarHeight) * 9 / 10 : scr->Height - scr->BarHeight - 8;
    if (w < 520) w = scr->Width;
    if (h < 330) h = scr->Height - scr->BarHeight;

    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"OpenWrite — OpenAmigaWriter",
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Width, w, WA_Height, h,
        WA_Left, (scr->Width - w) / 2,
        WA_Top, scr->BarHeight + (scr->Height - scr->BarHeight - h) / 2,
        WA_MinWidth, 520, WA_MinHeight, 330,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN |
                  IDCMP_MENUPICK | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW |
                  IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    relayout();
    return 1;
}

static void close_window(void)
{
    if (win) {
        ClearMenuStrip(win);
        free_gadgets();
        CloseWindow(win);
        win = NULL;
    }
    ogt_ctx_free(&ctx);
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static void new_document(void)
{
    owf_doc *d = sample_document();
    if (!d) { tell("New document", "There is not enough memory for a new document."); return; }
    set_document(d, "", "ODT");
    set_status("New document.");
    relayout();
}

static void do_open(void)
{
    char path[512];
    if (ask_file(0, path, sizeof path)) {
        open_document(path);
        relayout();
    }
}

static void do_save_as(void)
{
    char path[512];
    if (ask_file(1, path, sizeof path)) {
        save_document_as(path);
        draw_status();
    }
}

static void do_save(void)
{
    if (current_path[0]) {
        save_document_as(current_path);
        draw_status();
    } else do_save_as();
}

static void about(void)
{
    tell("OpenWrite",
         VERSION_TEXT "\n\n"
         "A lightweight native word processor for AmigaOS.\n\n"
         "OpenGadTools interface\n"
         "libowf DOCX/ODT/Amiga compatibility\n"
         "OpenRTG-ready page canvas\n"
         "OpenDatatypes and OpenPrint integration points\n\n"
         "MIT licence, Copyright (c) 2026 Dalsin Limited.");
}

static void coming(const char *name, const char *detail)
{
    char msg[300];
    snprintf(msg, sizeof msg, "%s\n\n%s", name, detail);
    tell(name, msg);
}

static void set_zoom(int z)
{
    if (z < 25) z = 25;
    if (z > 200) z = 200;
    zoom = z;
    if (editor) ow_editor_set_zoom(editor, z);
    relayout();
}

static void do_command(int id)
{
    switch (id) {
    case C_NEW: new_document(); break;
    case C_OPEN: do_open(); break;
    case C_SAVE: do_save(); break;
    case C_PRINT:
        coming("OpenPrint", "Print preview and physical/PDF output are the next integration milestone.");
        break;
    case C_UNDO: set_status("Undo becomes live with the editing milestone."); draw_status(); break;
    case C_REDO: set_status("Redo becomes live with the editing milestone."); draw_status(); break;
    case C_IMAGE:
        coming("OpenDatatypes", "Image insertion will use OpenDatatypes, preserving unknown objects where possible.");
        break;
    case C_TABLE:
        coming("Tables", "Table editing is in the first native layout milestone.");
        break;
    case C_PDF:
        coming("OpenPrint PDF", "PDF export will use the same pagination model as print preview.");
        break;
    }
}

static void toolbar_event(ULONG class, struct Gadget *g)
{
    int i = ogt_toolbar_index(&tb, g->GadgetID);
    if (i < 0) return;
    if (class == IDCMP_GADGETDOWN) {
        tb.pressed = i;
        ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 1, "window");
    } else {
        tb.pressed = -1;
        ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 0, "window");
        if (!tb.tool[i].disabled) do_command(tb.tool[i].id);
    }
}

static void menu_action(ULONG id)
{
    switch (id) {
    case M_NEW: new_document(); break;
    case M_OPEN: do_open(); break;
    case M_SAVE: do_save(); break;
    case M_SAVE_AS: do_save_as(); break;
    case M_PRINT: do_command(C_PRINT); break;
    case M_PDF: do_command(C_PDF); break;
    case M_ABOUT: about(); break;
    case M_QUIT: quit_now = 1; break;
    case M_UNDO: do_command(C_UNDO); break;
    case M_REDO: do_command(C_REDO); break;
    case M_CUT: case M_COPY: case M_PASTE:
        set_status("Clipboard editing becomes live with the caret/selection milestone."); draw_status(); break;
    case M_ZOOM_IN: set_zoom(zoom + 10); break;
    case M_ZOOM_OUT: set_zoom(zoom - 10); break;
    case M_ZOOM_100: set_zoom(100); break;
    case M_NAV: show_nav = !show_nav; relayout(); break;
    case M_INSPECTOR: show_inspector = !show_inspector; relayout(); break;
    case M_IMAGE: do_command(C_IMAGE); break;
    case M_TABLE: do_command(C_TABLE); break;
    case M_LINK: coming("Link", "Hyperlink editing is part of DOCX Tier A."); break;
    case M_PARAGRAPH: coming("Paragraph", "Paragraph properties will bind directly to libowf paragraph formatting."); break;
    case M_PAGE_SETUP: coming("Page Setup", "Page size, margins and orientation are already represented in libowf."); break;
    case M_DATATYPES: do_command(C_IMAGE); break;
    case M_OPENPRINT: do_command(C_PRINT); break;
    case M_THEME_OPEN: apply_theme("Open"); break;
    case M_THEME_GRAPHITE: apply_theme("Graphite"); break;
    case M_THEME_EMBER: apply_theme("Ember"); break;
    case M_THEME_CLEAR: apply_theme("Clear"); break;
    case M_THEME_CLASSIC: apply_theme("Classic"); break;
    case M_TB_BOTH: tb_style = OGT_TB_ICONS_TEXT; relayout(); break;
    case M_TB_ICONS: tb_style = OGT_TB_ICONS; relayout(); break;
    case M_TB_TEXT: tb_style = OGT_TB_TEXT; relayout(); break;
    }
}

static void events(void)
{
    struct IntuiMessage *im;
    while (win && (im = GT_GetIMsg(win->UserPort))) {
        ULONG class = im->Class;
        UWORD code = im->Code;
        APTR ia = im->IAddress;
        struct Gadget *g = (struct Gadget *)ia;
        GT_ReplyIMsg(im);
        switch (class) {
        case IDCMP_CLOSEWINDOW: quit_now = 1; break;
        case IDCMP_NEWSIZE: relayout(); break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win); draw_all(); GT_EndRefresh(win, TRUE); break;
        case IDCMP_GADGETDOWN:
        case IDCMP_GADGETUP:
            if (g && g->GadgetID >= GID_TOOLBAR) toolbar_event(class, g);
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTUP && tb.pressed >= 0) {
                ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window");
                tb.pressed = -1;
            }
            break;
        case IDCMP_VANILLAKEY:
            switch (code) {
            case '+': case '=': set_zoom(zoom + 10); break;
            case '-': set_zoom(zoom - 10); break;
            case '0': set_zoom(100); break;
            }
            break;
        case IDCMP_MENUPICK:
            while (code != MENUNULL && win) {
                struct MenuItem *item = ItemAddress(menu, code);
                if (!item) break;
                menu_action((ULONG)GTMENUITEM_USERDATA(item));
                code = item->NextSelect;
            }
            break;
        }
    }
}

static int window_main(int argc, char **argv)
{
    int rc = 0;
    char first[512] = "";
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)"OpenWrite needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }

    read_theme_choice();
    set_document(sample_document(), "", "ODT");
    if (!doc || !editor) { rc = 20; goto out; }

    if (argc > 1) snprintf(first, sizeof first, "%s", argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 &&
            NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }

    if (!open_window()) {
        if (argc) PutStr((STRPTR)"OpenWrite: the window could not open\n");
        rc = 20;
        goto out;
    }
    if (first[0]) { open_document(first); relayout(); }

    while (!quit_now) {
        ULONG sig = 1UL << win->UserPort->mp_SigBit;
        ULONG got = Wait(sig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) quit_now = 1;
        if (got & sig) events();
    }

out:
    close_window();
    if (editor) { ow_editor_free(editor); editor = NULL; }
    if (doc) { owf_doc_free(doc); doc = NULL; }
    if (theme.text) ogt_theme_free(&theme);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}

int main(int argc, char **argv)
{
    return ow_run_with_stack(65536, window_main, argc, argv);
}

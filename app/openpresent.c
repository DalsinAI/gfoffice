/*
 * OpenPresentation - the GFOffice Suite's slides, on AmigaOS 3.2.
 *
 * P2's program, with P3's first editing (Design-OpenPresentation.md, 12):
 * opens ODP and PPTX (libowf's deck formats), shows the slide at the
 * window's size with a strip of the slides beside it, lets a text box's
 * text be typed in (src/deck/opr_edit.c: the caret, backspace and delete,
 * new paragraphs, undo), saves ODP, exports PDF and prints through
 * OpenPrint (the deck's PDF to T:, then C:OpenPrint on it, as OpenWrite
 * does).
 *
 * The slide is laid out by OpenLayout through the core (opr_slide_layout),
 * measured with the PDF's standard fonts' widths, and drawn here from its
 * display list at the screen's pixels: each glyph where the layout put it,
 * so the screen, the caret and the PDF agree. Shapes OpenLayout doesn't draw
 * (ellipses, triangles, arrows, lines...) are drawn here, at their frame's
 * place in the drawing order, as the PDF writer draws them. Pictures go
 * through datatypes, as OpenWrite's do.
 *
 * Chrome: OpenGadTools' toolbar with the Chrome icon set
 * (ogt_iconset_attach), status bar and list, in the theme
 * (opengadtools docs/style/README.md). 68040, no FPU needed.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <graphics/text.h>
#include <graphics/gfx.h>
#include <graphics/regions.h>
#include <devices/inputevent.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <cybergraphx/cybergraphics.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/datatypes.h>
#include <proto/cybergraphics.h>
#include <proto/diskfont.h>
#include <proto/layers.h>
#include "ogt_openfont.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <clib/alib_protos.h>

#include "openpresentation_core.h"
#include "ow_layout.h"
#include "owf_pdf.h"
#include "ow_stack.h"
#include "lastdir.h"
#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_iconset.h"
#include "ogt_font.h"
#include "ogt_list.h"
#include "ogt_status.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *OpenFontTextBase = NULL, *LayersBase = NULL;
struct Library *DataTypesBase = NULL, *CyberGfxBase = NULL, *DiskfontBase = NULL;

#define PROGRAM "OpenPresentation"
#define VERSION_TEXT "OpenPresentation 0.2.0 (25.10.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " MIT, Copyright (c) 2026 AmigaChrome Team";

enum { C_NEW = 1, C_OPEN, C_SAVE, C_PRINT, C_PDF, C_UNDO, C_REDO };
enum { GID_TOOLBAR = 100, GID_SLIDES = 400 };
enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVE_AS, M_PRINT, M_PDF, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_STOP,
    M_FIRST, M_PREV, M_NEXT, M_LAST, M_TB_BOTH, M_TB_ICONS, M_TB_TEXT
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
    { NM_ITEM, "Stop Typing", NULL, 0, 0, (APTR)M_STOP },

    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "First Slide", NULL, 0, 0, (APTR)M_FIRST },
    { NM_ITEM, "Previous Slide", "[", 0, 0, (APTR)M_PREV },
    { NM_ITEM, "Next Slide", "]", 0, 0, (APTR)M_NEXT },
    { NM_ITEM, "Last Slide", NULL, 0, 0, (APTR)M_LAST },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Buttons", NULL, 0, 0, NULL },
    { NM_SUB, "Icons and Text", NULL, CHECKIT, ~1 & 7, (APTR)M_TB_BOTH },
    { NM_SUB, "Icons", NULL, CHECKIT | CHECKED, ~2 & 7, (APTR)M_TB_ICONS },
    { NM_SUB, "Text", NULL, CHECKIT, ~4 & 7, (APTR)M_TB_TEXT },

    { NM_TITLE, "Help", NULL, 0, 0, NULL },
    { NM_ITEM, "About OpenPresentation...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* The Chrome icon set's pictures by name, the vector icons as their fallback. The set has no Show or New
 * Slide picture yet (they are asked of opengadtools): those come with the player. */
static const ogt_tool tools[] = {
    { C_NEW,   "New",       OGT_ICON_FILE,    0, 0, 0, "new" },
    { C_OPEN,  "Open...",   OGT_ICON_OPEN,    0, 0, 0, "open" },
    { C_SAVE,  "Save",      OGT_ICON_DISK,    0, 0, 0, "save" },
    { C_PRINT, "Print...",  OGT_ICON_TEST,    1, 0, 0, "print" },
    { C_PDF,   "PDF...",    OGT_ICON_SENT,    0, 0, 0, "pdf" },
    { C_UNDO,  "Undo",      OGT_ICON_PARENT,  1, 1, 0, "undo" },
    { C_REDO,  "Redo",      OGT_ICON_FORWARD, 0, 1, 0, "redo" }
};

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

typedef struct box { int x, y, w, h; } box;

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *chain;
static ogt_list *strip;                              /* the slides, a thumbnail each */
static ogt_status sbar;
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;
static ogt_theme theme;
static ogt_ctx ctx;
static ogt_toolbar tb;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS;
static int quit_now;

static box area, toolbar_box, strip_box, view_box, slide_box, status_box;

static owf_deck *deck;
static int current;                                  /* the slide shown */
static char current_path[512];
static char current_format[16] = "ODP";
static int dirty;
static char status[320] = "Ready.";
static opr_undo *undo;

/* Typing: the object typed in (-1: none), the caret, and whether its text changed since the caret was put
 * there (an undo step then). */
static int edit_obj = -1;
static opr_pos caret;
static int edit_grouped;

static void draw_all(void);
static void draw_slide(void);
static void draw_status(void);
static void layout(void);

/* ---- Small helpers ---- */

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
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)"OK" };
    EasyRequest(win, &es, NULL, (ULONG)text);
}

static int ask(const char *title, const char *text, const char *buttons)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)buttons };
    return (int)EasyRequest(win, &es, NULL, (ULONG)text);
}

static void set_status(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n >= sizeof status) n = sizeof status - 1;
    if (n) memcpy(status, s, n);
    status[n] = 0;
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

static LONG argb_pen(uint32_t argb)
{
    return ogt_pen_rgb(&ctx, (ogt_rgb){ (UBYTE)(argb >> 16), (UBYTE)(argb >> 8), (UBYTE)argb });
}

static LONG shade_pen(unsigned long shade)
{
    return argb_pen(0xff000000u | (uint32_t)OWF_SHADE_RGB(shade));
}

/* Text and lines need a real pen, and on an RTG screen the shared palette
   runs out (a slide's red came out olive). So one pen of our own, set to
   each colour just before it draws: on a true-colour screen that changes
   nothing already drawn. Fills are drawn in exact colours already. */
static LONG own_pen = -1;
static int own_pen_tried;

static LONG exact_pen(LONG pen)
{
    ULONG rgb;
    if (!(pen & OGT_DIRECT) || !scr) return pen;
    if (own_pen < 0 && !own_pen_tried) {
        own_pen_tried = 1;
        if (((struct Library *)GfxBase)->lib_Version >= 39)
            own_pen = ObtainPen(scr->ViewPort.ColorMap, (ULONG)-1, 0, 0, 0, PEN_EXCLUSIVE | PEN_NO_SETCOLOR);
    }
    if (own_pen < 0) return pen;
    rgb = (ULONG)pen & 0xffffffUL;
    SetRGB32(&scr->ViewPort, (ULONG)own_pen, ((rgb >> 16) & 255) * 0x01010101UL,
             ((rgb >> 8) & 255) * 0x01010101UL, (rgb & 255) * 0x01010101UL);
    return own_pen;
}

static void own_pen_free(void)
{
    if (own_pen >= 0 && scr) ReleasePen(scr->ViewPort.ColorMap, (ULONG)own_pen);
    own_pen = -1;
    own_pen_tried = 0;
}

/* UTF-8 to the Amiga fonts' ISO-8859-1, one character: its byte, the nearest one for common typography
 * outside it, '?' for the rest; 0 for one that draws nothing. */
static unsigned char latin1_of(unsigned long cp)
{
    if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) return (unsigned char)cp;
    switch (cp) {
    case 0x2022: case 0x25CF: case 0x25AA: case 0x2219: case 0x25A0: case 0x25E6: case 0x27A2: return 0xB7;
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
    case 0x2013: case 0x2014: case 0x2212: case 0x2010: case 0x2011: return '-';
    case 0x00A0: case 0x2002: case 0x2003: case 0x2009: case 0x202F: return ' ';
    case 0x2026: return 0x2E;            /* an ellipsis: a full stop */
    case 0x20AC: return 'E';
    case 0xFEFF: case 0x200B: case 0x200D: case 0x00AD: return 0;
    }
    return '?';
}

/* The code point at *at (moved on past it); a broken byte is U+FFFD. */
static unsigned long utf8_next(const char *s, size_t n, size_t *at)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned long cp = p[*at];
    int len = cp < 0x80 ? 1 : (cp & 0xE0) == 0xC0 ? 2 : (cp & 0xF0) == 0xE0 ? 3 : (cp & 0xF8) == 0xF0 ? 4 : 0, k;
    if (!len || *at + (size_t)len > n) {
        (*at)++;
        return 0xFFFD;
    }
    if (len > 1) {
        cp &= 0x3F >> (len - 1);
        for (k = 1; k < len; k++) {
            if ((p[*at + k] & 0xC0) != 0x80) {
                (*at)++;
                return 0xFFFD;
            }
            cp = cp << 6 | (p[*at + k] & 0x3F);
        }
    }
    *at += (size_t)len;
    return cp;
}

/* ---- Fonts ----
 * A deck's faces drawn with the Amiga's: sans as CGTriumvirate, serif as CGTimes, mono as courier (OS 3.2's
 * Intellifont outlines and bitmaps, as OpenWrite maps them), at the pixel size the scale gives, through
 * openfont.library, else diskfont (which scales and draws outlines), else the fonts in memory. Kept open in a
 * small cache. */

#define FONT_CACHE 20
static struct { char file[40]; int pixels; struct TextFont *font; } fonts[FONT_CACHE];
static int font_next;
static struct { char file[40]; int pixels; } font_misses[16];
static int font_miss_next;
static int diskfont_tried;

static void font_cache_clear(void)
{
    int i;
    for (i = 0; i < FONT_CACHE; ++i) {
        if (fonts[i].font) CloseFont(fonts[i].font);
        memset(&fonts[i], 0, sizeof fonts[i]);
    }
    memset(font_misses, 0, sizeof font_misses);
    font_next = font_miss_next = 0;
}

static struct TextFont *open_route(int route, struct TextAttr *ta, UBYTE flags)
{
    ta->ta_Flags = flags;
    switch (route) {
    case 0: return OpenFontTextBase ? ogt_openfont_font(OpenFontTextBase, ta) : NULL;
    case 1:
        if (!DiskfontBase && !diskfont_tried) {
            diskfont_tried = 1;
            DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
        }
        return DiskfontBase ? OpenDiskFont(ta) : NULL;
    default: return OpenFont(ta);
    }
}

static struct TextFont *font_px(const char *file, int pixels)
{
    struct TextAttr ta;
    struct TextFont *f = NULL;
    int i, slot = -1, route;
    if (pixels < 4) pixels = 4;
    if (pixels > 160) pixels = 160;
    for (i = 0; i < FONT_CACHE; ++i) {
        if (fonts[i].font && fonts[i].pixels == pixels && !strcmp(fonts[i].file, file)) return fonts[i].font;
        if (!fonts[i].font && slot < 0) slot = i;
    }
    for (i = 0; i < 16; ++i)
        if (font_misses[i].pixels == pixels && !strcmp(font_misses[i].file, file)) return NULL;
    memset(&ta, 0, sizeof ta);
    ta.ta_Name = (STRPTR)file;
    ta.ta_YSize = (UWORD)pixels;
    for (route = 0; !f && route < 2; ++route)
        if ((f = open_route(route, &ta, FPF_DISKFONT)) && abs(f->tf_YSize - pixels) > pixels / 8 + 1) {
            CloseFont(f);
            f = NULL;
        }
    for (route = 0; !f && route < 3; ++route)
        f = open_route(route, &ta, route == 2 ? 0 : FPF_DISKFONT | FPF_DESIGNED);
    if (!f) {
        snprintf(font_misses[font_miss_next].file, sizeof font_misses[0].file, "%s", file);
        font_misses[font_miss_next].pixels = pixels;
        font_miss_next = (font_miss_next + 1) % 16;
        return NULL;
    }
    if (slot < 0) {
        slot = font_next;
        font_next = (font_next + 1) % FONT_CACHE;
        CloseFont(fonts[slot].font);
    }
    snprintf(fonts[slot].file, sizeof fonts[slot].file, "%s", file);
    fonts[slot].pixels = pixels;
    fonts[slot].font = f;
    return f;
}

static struct TextFont *slide_font(const ol_style *st, int pixels)
{
    int index = OW_LAYOUT_FONT_INDEX(OL_TEXT_FONT_OF(st->text_flags));
    owf_font_kind kind = (st->text_flags & OL_TEXT_MONO) ? OWF_FONT_MONO : opr_font_kind(deck, index);
    const char *file = kind == OWF_FONT_MONO ? "courier.font" : kind == OWF_FONT_SERIF ? "CGTimes.font"
                     : "CGTriumvirate.font";
    struct TextFont *f = font_px(file, pixels);
    if (!f && kind != OWF_FONT_SANS) f = font_px("CGTriumvirate.font", pixels);
    if (!f) f = font_px("helvetica.font", pixels);
    return f ? f : font;
}

/* ---- Pictures, through datatypes (as OpenWrite's image cache) ---- */

#define IMAGE_SLOTS 12
#define IMAGE_MAX_PIXELS (1024UL * 1024UL)
#define IMAGE_BYTES (8UL * 1024UL * 1024UL)
typedef struct { int index; UBYTE *argb; UWORD w, h; ULONG used; } picture;
static picture pictures[IMAGE_SLOTS];
static int npictures;
static ULONG picture_tick;

static void pictures_clear(void)
{
    int i;
    for (i = 0; i < npictures; ++i) free(pictures[i].argb);
    memset(pictures, 0, sizeof pictures);
    npictures = 0;
}

static void pictures_room(ULONG need)
{
    for (;;) {
        ULONG total = 0;
        int i, old = -1;
        for (i = 0; i < npictures; ++i) {
            total += (ULONG)pictures[i].w * pictures[i].h * 4;
            if (old < 0 || pictures[i].used < pictures[old].used) old = i;
        }
        if (old < 0 || (npictures < IMAGE_SLOTS && total + need <= IMAGE_BYTES)) return;
        free(pictures[old].argb);
        pictures[old] = pictures[--npictures];
        memset(&pictures[npictures], 0, sizeof pictures[npictures]);
    }
}

static ULONG isqrt_ul(ULONG v)
{
    ULONG r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* The picture's pixels at tw x th from the datatype's sw x sh, each the nearest source pixel, a band of rows
 * at a time. */
static int read_scaled(Object *o, UBYTE *out, int sw, int sh, int tw, int th)
{
    int rows = 65536 / (sw * 4) > 0 ? 65536 / (sw * 4) : 1, y0, ty, ok = 1;
    UBYTE *band = (UBYTE *)malloc((size_t)sw * 4 * (rows < sh ? rows : sh));
    if (!band) return 0;
    for (y0 = 0, ty = 0; ok && y0 < sh && ty < th; y0 += rows) {
        int n = sh - y0 < rows ? sh - y0 : rows;
        if (!DoMethod(o, PDTM_READPIXELARRAY, (ULONG)band, PBPAFMT_ARGB, (ULONG)sw * 4, 0, (ULONG)y0, (ULONG)sw,
                      (ULONG)n)) {
            ok = 0;
            break;
        }
        for (; ty < th; ++ty) {
            int sy = (int)((ULONG)ty * sh / th), k;
            const ULONG *src;
            ULONG *dst = (ULONG *)(out + (size_t)ty * tw * 4);
            if (sy >= y0 + n) break;
            src = (const ULONG *)(band + (size_t)(sy - y0) * sw * 4);
            for (k = 0; k < tw; ++k) dst[k] = src[(ULONG)k * sw / tw];
        }
    }
    free(band);
    return ok;
}

static picture *picture_get(int index)
{
    int i, sw = 0, sh = 0, tw = 0, th = 0, ok;
    char tmp[96];
    BPTR f;
    Object *o = NULL;
    struct BitMapHeader *bmh = NULL;
    picture *c;
    const owf_image *im;
    if (!deck || index < 0 || index >= deck->res->nimages || !DataTypesBase) return NULL;
    for (i = 0; i < npictures; ++i)
        if (pictures[i].index == index) {
            pictures[i].used = ++picture_tick;
            return pictures[i].argb ? &pictures[i] : NULL;
        }
    im = &deck->res->images[index];
    snprintf(tmp, sizeof tmp, "T:OpenPresentation-Picture-%ld-%d.tmp", (long)FindTask(NULL), index);
    f = im->data && im->length ? Open((STRPTR)tmp, MODE_NEWFILE) : 0;
    if (f) {
        ok = Write(f, (APTR)im->data, (LONG)im->length) == (LONG)im->length;
        Close(f);
        if (ok) o = NewDTObject((APTR)tmp, DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43, PDTA_Remap, FALSE,
                                TAG_DONE);
    }
    if (o) {
        GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
        DoMethod(o, DTM_PROCLAYOUT, NULL, 1);
        sw = bmh ? bmh->bmh_Width : 0;
        sh = bmh ? bmh->bmh_Height : 0;
        tw = sw;
        th = sh;
        if (sw > 0 && sh > 0 && (ULONG)sw * sh > IMAGE_MAX_PIXELS) {   /* the same shape, a megapixel */
            ULONG root = isqrt_ul((ULONG)sw * sh);
            tw = (int)((ULONG)sw * 1024UL / root);
            th = (int)((ULONG)sh * 1024UL / root);
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
        }
    }
    /* a slot, and room for the pixels, before anything is read: one that can't be read is remembered too */
    pictures_room(sw > 0 && sh > 0 ? (ULONG)tw * th * 4 : 0);
    c = &pictures[npictures++];
    memset(c, 0, sizeof *c);
    c->index = index;
    c->used = ++picture_tick;
    if (o && sw > 0 && sh > 0 && (c->argb = (UBYTE *)malloc((ULONG)tw * th * 4)) != NULL) {
        ok = tw == sw && th == sh
             ? DoMethod(o, PDTM_READPIXELARRAY, (ULONG)c->argb, PBPAFMT_ARGB, (ULONG)sw * 4, 0, 0, (ULONG)sw,
                        (ULONG)sh) != 0
             : read_scaled(o, c->argb, sw, sh, tw, th);
        if (ok) {
            c->w = (UWORD)tw;
            c->h = (UWORD)th;
        } else {
            free(c->argb);
            c->argb = NULL;
        }
    }
    if (o) DisposeDTObject(o);
    if (f) DeleteFile((STRPTR)tmp);
    if (!c->argb) return NULL;
    {   /* see-through pixels onto white: WritePixelArray doesn't blend */
        ULONG k, n = (ULONG)c->w * c->h;
        int any = 0, partial = 0;
        for (k = 0; k < n; ++k) { UBYTE a = c->argb[k * 4]; if (a) any = 1; if (a != 255) partial = 1; }
        if (any && partial)
            for (k = 0; k < n; ++k) {
                UBYTE *px = c->argb + k * 4;
                unsigned a = px[0];
                px[1] = (UBYTE)((px[1] * a + 255 * (255 - a)) / 255);
                px[2] = (UBYTE)((px[2] * a + 255 * (255 - a)) / 255);
                px[3] = (UBYTE)((px[3] * a + 255 * (255 - a)) / 255);
                px[0] = 255;
            }
    }
    return c;
}

/* A picture into the box, only the part inside lim, a band of rows at a time. */
static void draw_picture(struct RastPort *rp, const picture *c, int bx, int by, int bw, int bh, const ol_pixel_rect *lim)
{
    int x0 = bx > lim->x ? bx : lim->x, y0 = by > lim->y ? by : lim->y;
    int x1 = bx + bw < lim->x + lim->width ? bx + bw : lim->x + lim->width;
    int y1 = by + bh < lim->y + lim->height ? by + bh : lim->y + lim->height;
    int w = x1 - x0, rows, y, k;
    ULONG *band;
    UWORD *xs;
    if (w <= 0 || y1 <= y0 || bw <= 0 || bh <= 0 || x0 < 0 || y0 < 0) return;
    rows = 16384 / w > 0 ? 16384 / w : 1;
    if (rows > y1 - y0) rows = y1 - y0;
    band = (ULONG *)malloc((size_t)w * rows * 4);
    xs = (UWORD *)malloc((size_t)w * sizeof(UWORD));
    if (band && xs) {
        for (k = 0; k < w; ++k) xs[k] = (UWORD)((ULONG)(x0 + k - bx) * c->w / (ULONG)bw);
        for (y = y0; y < y1; y += rows) {
            int n = y1 - y < rows ? y1 - y : rows, r;
            for (r = 0; r < n; ++r) {
                const ULONG *src = (const ULONG *)c->argb + (ULONG)(y + r - by) * c->h / (ULONG)bh * c->w;
                ULONG *dst = band + (size_t)r * w;
                for (k = 0; k < w; ++k) dst[k] = src[xs[k]];
            }
            WritePixelArray(band, 0, 0, (UWORD)(w * 4), rp, (UWORD)x0, (UWORD)y, (UWORD)w, (UWORD)n, RECTFMT_ARGB);
        }
    }
    free(xs);
    free(band);
}

/* ---- Slides laid out ----
 * A few slides kept laid out (the one shown and the thumbnails in view), each by its slide's change count,
 * so a change lays out that slide again and no other. */

#define TW(v) ((ol_unit)(v) * OL_UNITS_PER_TWIP)
#define LAID_MAX 8
static struct { int slide; unsigned long version; opr_slide *s; ULONG used; } laid[LAID_MAX];
static unsigned long *versions;                      /* a count of each slide's changes */
static int nversions;
static ULONG laid_tick;

static void laid_clear(void)
{
    int i;
    for (i = 0; i < LAID_MAX; ++i) {
        opr_slide_free(laid[i].s);
        memset(&laid[i], 0, sizeof laid[i]);
    }
}

static unsigned long slide_version(int slide)
{
    if (deck && deck->nslides > nversions) {
        unsigned long *v = realloc(versions, (size_t)deck->nslides * sizeof *v);
        if (v) {
            memset(v + nversions, 0, (size_t)(deck->nslides - nversions) * sizeof *v);
            versions = v;
            nversions = deck->nslides;
        }
    }
    return slide >= 0 && slide < nversions ? versions[slide] : 0;
}

static void slide_changed(int slide)
{
    slide_version(slide);
    if (slide >= 0 && slide < nversions) versions[slide]++;
}

static opr_slide *laid_get(int slide)
{
    int i, slot = 0;
    unsigned long v = slide_version(slide);
    if (!deck || slide < 0 || slide >= deck->nslides) return NULL;
    for (i = 0; i < LAID_MAX; ++i)
        if (laid[i].s && laid[i].slide == slide && laid[i].version == v) {
            laid[i].used = ++laid_tick;
            return laid[i].s;
        }
    for (i = 0; i < LAID_MAX; ++i) {
        if (!laid[i].s) { slot = i; break; }
        if (laid[i].used < laid[slot].used) slot = i;
    }
    opr_slide_free(laid[slot].s);
    laid[slot].s = opr_slide_layout(deck, slide, NULL, NULL, NULL);
    laid[slot].slide = slide;
    laid[slot].version = v;
    laid[slot].used = ++laid_tick;
    return laid[slot].s;
}

/* ---- Drawing a slide from its display list ---- */

typedef struct {
    struct RastPort *rp;
    const opr_slide *s;
    ol_scale scale;
    ol_pixel_rect lim;           /* the slide, and of it what is being drawn */
    int thumb;                   /* a thumbnail: text as lines */
    int next_frame;              /* the first frame whose shape is still to draw */
} painter;

/* cos and sin of 0, 15, ... 90 degrees, in thousandths */
static const int cos15[7] = { 1000, 966, 866, 707, 500, 259, 0 };

static int px_x(const painter *p, int twips) { return ol_scale_x(&p->scale, TW(twips)); }
static int px_y(const painter *p, int twips) { return ol_scale_y(&p->scale, TW(twips)); }

static void outline(struct RastPort *rp, LONG pen, int *xy, int n, int width)
{
    int k;
    if (n < 2) return;
    pen = exact_pen(pen);
    xy[n * 2] = xy[0];
    xy[n * 2 + 1] = xy[1];
    ogt_poly_outline(rp, pen, xy, n + 1);
    for (k = 1; k < width && k < 4; ++k) {   /* a thicker line: drawn again, a pixel along */
        int i, moved[2 * 66];
        for (i = 0; i <= n && i < 65; ++i) {
            moved[i * 2] = xy[i * 2] + (k & 1);
            moved[i * 2 + 1] = xy[i * 2 + 1] + (k >> 1 ? 1 : 0) + (k == 2);
        }
        ogt_poly_outline(rp, pen, moved, n + 1 < 65 ? n + 1 : 65);
    }
}

static void shape_poly(const painter *p, const owf_obj *o, int *xy, int n)
{
    LONG fill = OWF_SHADE_SET(o->look.fill) ? shade_pen(o->look.fill) : -1;
    LONG line = OWF_SHADE_SET(o->look.line) ? shade_pen(o->look.line) : -1;
    int width = o->look.line_width > 0 ? ol_scale_length(&p->scale, TW(o->look.line_width)) : 1;
    if (fill >= 0 && n >= 3) ogt_fill_poly(p->rp, fill, xy, n);
    if (line >= 0) outline(p->rp, line, xy, n, width);
}

static void arrowhead(struct RastPort *rp, LONG pen, int x1, int y1, int x2, int y2, int size)
{
    long dx = x2 - x1, dy = y2 - y1, len;
    int pts[6];
    len = (long)isqrt_ul((ULONG)(dx * dx + dy * dy));
    if (len <= 0) return;
    pts[0] = x2;
    pts[1] = y2;
    pts[2] = (int)(x2 - dx * size / len - dy * size / (2 * len));
    pts[3] = (int)(y2 - dy * size / len + dx * size / (2 * len));
    pts[4] = (int)(x2 - dx * size / len + dy * size / (2 * len));
    pts[5] = (int)(y2 - dy * size / len - dx * size / (2 * len));
    ogt_fill_poly(rp, pen, pts, 3);
}

/* A shape OpenLayout doesn't draw, at its box, as the PDF writer draws it (src/deck/opr_pdf.c). */
static void draw_shape(const painter *p, const owf_obj *o)
{
    int x = px_x(p, o->x), y = px_y(p, o->y), x2 = px_x(p, o->x + o->width), y2 = px_y(p, o->y + o->height);
    int w = x2 - x, h = y2 - y, xy[2 * 66], k, n = 0;
    if (w < 1 && h < 1) return;
    switch (o->shape) {
    case OWF_SHAPE_ELLIPSE: {
        static const int s24[24][2] = {
            { 1000, 0 }, { 966, 259 }, { 866, 500 }, { 707, 707 }, { 500, 866 }, { 259, 966 }, { 0, 1000 },
            { -259, 966 }, { -500, 866 }, { -707, 707 }, { -866, 500 }, { -966, 259 }, { -1000, 0 },
            { -966, -259 }, { -866, -500 }, { -707, -707 }, { -500, -866 }, { -259, -966 }, { 0, -1000 },
            { 259, -966 }, { 500, -866 }, { 707, -707 }, { 866, -500 }, { 966, -259 } };
        for (k = 0; k < 24; ++k) {
            xy[n * 2] = x + w / 2 + (int)((long)w * s24[k][0] / 2000);
            xy[n * 2 + 1] = y + h / 2 + (int)((long)h * s24[k][1] / 2000);
            n++;
        }
        break;
    }
    case OWF_SHAPE_ROUNDRECT: {
        /* the corners clockwise from the top right, each a quarter circle in 15-degree steps */
        int r = ol_scale_length(&p->scale, TW(o->radius)), right = x + w - 1 - r, bottom = y + h - 1 - r;
        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;
        right = x + w - 1 - r;
        bottom = y + h - 1 - r;
        for (k = 0; k < 7; ++k, ++n) {
            xy[n * 2] = right + r * cos15[6 - k] / 1000;
            xy[n * 2 + 1] = y + r - r * cos15[k] / 1000;
        }
        for (k = 0; k < 7; ++k, ++n) {
            xy[n * 2] = right + r * cos15[k] / 1000;
            xy[n * 2 + 1] = bottom + r * cos15[6 - k] / 1000;
        }
        for (k = 0; k < 7; ++k, ++n) {
            xy[n * 2] = x + r - r * cos15[6 - k] / 1000;
            xy[n * 2 + 1] = bottom + r * cos15[k] / 1000;
        }
        for (k = 0; k < 7; ++k, ++n) {
            xy[n * 2] = x + r - r * cos15[k] / 1000;
            xy[n * 2 + 1] = y + r - r * cos15[6 - k] / 1000;
        }
        break;
    }
    case OWF_SHAPE_TRIANGLE:
        xy[0] = x + w / 2; xy[1] = (o->flags & OWF_OBJ_FLIP_V) ? y + h : y;
        xy[2] = x + w; xy[3] = (o->flags & OWF_OBJ_FLIP_V) ? y : y + h;
        xy[4] = x; xy[5] = xy[3];
        n = 3;
        break;
    case OWF_SHAPE_DIAMOND:
        xy[0] = x + w / 2; xy[1] = y;
        xy[2] = x + w; xy[3] = y + h / 2;
        xy[4] = x + w / 2; xy[5] = y + h;
        xy[6] = x; xy[7] = y + h / 2;
        n = 4;
        break;
    case OWF_SHAPE_ARROW: {
        static const int ax[7] = { 0, 750, 750, 1000, 750, 750, 0 };
        static const int ay[7] = { 250, 250, 0, 500, 1000, 750, 750 };
        for (k = 0; k < 7; ++k) {
            int dx = (int)((long)w * ax[k] / 1000);
            xy[k * 2] = (o->flags & OWF_OBJ_FLIP_H) ? x + w - dx : x + dx;
            xy[k * 2 + 1] = y + (int)((long)h * ay[k] / 1000);
        }
        n = 7;
        break;
    }
    case OWF_SHAPE_POLYGON:
        for (k = 0; k < o->npoints && k < 64; ++k) {
            xy[k * 2] = px_x(p, o->x + o->points[k * 2]);
            xy[k * 2 + 1] = px_y(p, o->y + o->points[k * 2 + 1]);
        }
        n = k;
        break;
    case OWF_SHAPE_LINE: {
        int ax1 = (o->flags & OWF_OBJ_FLIP_H) ? x + w : x, ax2 = (o->flags & OWF_OBJ_FLIP_H) ? x : x + w;
        int ay1 = (o->flags & OWF_OBJ_FLIP_V) ? y + h : y, ay2 = (o->flags & OWF_OBJ_FLIP_V) ? y : y + h;
        int width = o->look.line_width > 0 ? ol_scale_length(&p->scale, TW(o->look.line_width)) : 1;
        LONG pen;
        if (!OWF_SHADE_SET(o->look.line)) return;
        pen = shade_pen(o->look.line);
        ogt_set_apen(p->rp, exact_pen(pen));
        for (k = 0; k < width && k < 6; ++k) {
            int off = k - width / 2;
            if (abs(ax2 - ax1) >= abs(ay2 - ay1)) { Move(p->rp, ax1, ay1 + off); Draw(p->rp, ax2, ay2 + off); }
            else { Move(p->rp, ax1 + off, ay1); Draw(p->rp, ax2 + off, ay2); }
        }
        if (o->look.arrows & OWF_ARROW_END) arrowhead(p->rp, pen, ax1, ay1, ax2, ay2, width * 3 > 6 ? width * 3 : 6);
        if (o->look.arrows & OWF_ARROW_START) arrowhead(p->rp, pen, ax2, ay2, ax1, ay1, width * 3 > 6 ? width * 3 : 6);
        return;
    }
    default:
        return;
    }
    shape_poly(p, o, xy, n);
}

static void shapes_up_to(painter *p, int frame)
{
    for (; p->next_frame <= frame && p->next_frame < p->s->nframes; p->next_frame++) {
        const opr_frame *f = &p->s->frames[p->next_frame];
        if (f->obj && f->obj->kind == OWF_OBJ_SHAPE && f->obj->shape != OWF_SHAPE_RECT)
            draw_shape(p, &f->box);
    }
}

/* The list bullets drawn as shapes: 1 a dot, 2 a square, 3 a ring, 4 a big
   disc (U+25CF, LibreOffice's, drawn small by its relative size); 0 not one. */
static int bullet_shape(unsigned long cp)
{
    switch (cp) {
    case 0x2022: case 0x2219: case 0xF0B7: return 1;
    case 0x25AA: case 0x25A0: case 0x25FC: case 0x25FE: case 0xF0A7: case 0xF06E: return 2;
    case 0x25CB: case 0x25E6: case 0xF06F: return 3;
    case 0x25CF: case 0xF06C: return 4;
    default: return 0;
    }
}

static void draw_bullet(struct RastPort *rp, LONG pen, int shape, int x, int baseline, int font_pixels)
{
    int r = font_pixels * (shape == 4 ? 30 : 22) / 100;
    int cy = baseline - font_pixels * 33 / 100, dy;
    if (r < 2) r = font_pixels >= 6 ? 2 : 1;
    if (shape == 2) {
        ogt_box(rp, pen, x, cy - r, 2 * r + 1, 2 * r + 1);
        return;
    }
    for (dy = -r; dy <= r; ++dy) {
        int hw = r;
        while (hw > 0 && hw * hw + dy * dy > r * r + r / 2) --hw;
        if (shape == 3 && dy > -r && dy < r) {
            int in = r - 1, iw = in;
            while (iw > 0 && iw * iw + dy * dy > in * in) --iw;
            if (abs(dy) < in) {
                ogt_box(rp, pen, x + r - hw, cy + dy, hw - iw > 0 ? hw - iw : 1, 1);
                ogt_box(rp, pen, x + r + iw + 1, cy + dy, hw - iw > 0 ? hw - iw : 1, 1);
                continue;
            }
        }
        ogt_box(rp, pen, x + r - hw, cy + dy, 2 * hw + 1, 1);
    }
}

/* A text run's words, measured before they are drawn. */
#define RUN_WORDS 48
typedef struct { int x0, x1, shape; unsigned short n; char text[64]; } run_word;
static run_word run_words[RUN_WORDS];

static void paint_text(painter *p, const ol_device_op *d)
{
    struct RastPort *rp = p->rp;
    const ol_text_op *t = &d->op->u.text;
    const ol_node *n = ol_node_by_id(p->s->doc, t->node_id);
    const char *s = n ? ol_node_text(n) : NULL;
    size_t len = t->text_length, at = 0;
    LONG pen;
    int style = FS_NORMAL, baseline = d->baseline;
    if (!s || !len) return;
    s += t->text_offset;
    pen = (t->style.foreground >> 24) ? argb_pen(t->style.foreground) : argb_pen(0xff000000u);
    if (p->thumb || d->font_pixels < 6) {
        /* too small to read: a line where the words are; but a bullet at a
           small relative size (LibreOffice's 45%) is still drawn */
        size_t k = len;
        if (!p->thumb) {
            size_t b = 0;
            int shape = bullet_shape(utf8_next(s, len, &b));
            if (shape && (b >= len || s[b] == ' ' || s[b] == '\t')) {
                draw_bullet(rp, pen, shape, d->box.x, baseline, d->font_pixels);
                return;
            }
        }
        int hh = d->font_pixels / 2 > 0 ? d->font_pixels / 2 : 1;
        while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) --k;
        if (k) ogt_box(rp, pen, d->box.x, baseline - hh, (int)(d->box.width * (long)k / (long)len) + 1, hh);
        return;
    }
    if (t->style.text_flags & OL_TEXT_BOLD) style |= FSF_BOLD;
    if (t->style.text_flags & OL_TEXT_ITALIC) style |= FSF_ITALIC;
    if (t->style.text_flags & OL_TEXT_SUPER) baseline -= d->font_pixels / 2;
    else if (t->style.text_flags & OL_TEXT_SUB) baseline += d->font_pixels / 4;
    SetFont(rp, slide_font(&t->style, d->font_pixels));
    SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    ogt_set_apen(rp, exact_pen(pen));
    SetDrMd(rp, JAM1);
    /* Each word starts where the layout put it and is drawn with the font's
       own spacing (placing every glyph by the layout's widths left gaps
       inside words). The layout measures with the print (PDF) widths, and an
       Amiga font's size counts more than its letters' em, so its words come
       out narrower: the font is taken a size up until the words' widths match
       the layout's, and the words don't sit apart. */
    while (at < len) {
        int nw = 0, k;
        long layout_w = 0, screen_w = 0;
        while (at < len && nw < RUN_WORDS) {
            run_word *w = &run_words[nw];
            size_t start = at;
            unsigned long cp = utf8_next(s, len, &at);
            unsigned char c = latin1_of(cp);
            ol_text_metrics m;
            if (c <= ' ' || c == 0x7F) continue;
            w->n = 0;
            w->shape = bullet_shape(cp);
            if (!(w->shape && (at >= len || s[at] == ' ' || s[at] == '\t'))) {
                w->shape = 0;
                w->text[w->n++] = (char)c;
                while (at < len && w->n < sizeof w->text) {
                    size_t here = at;
                    c = latin1_of(utf8_next(s, len, &at));
                    if (c <= ' ' || c == 0x7F) { at = here; break; }
                    w->text[w->n++] = (char)c;
                }
            }
            m.width = 0;
            if (start) opr_measure((void *)deck, s, start, &t->style, &m);
            w->x0 = m.width;
            opr_measure((void *)deck, s, at, &t->style, &m);
            w->x1 = m.width;
            if (!w->shape) {
                layout_w += w->x1 - w->x0;
                screen_w += TextLength(rp, (STRPTR)w->text, (UWORD)w->n);
            }
            nw++;
        }
        if (screen_w > 0) {
            long want = ol_scale_length(&p->scale, (int)layout_w);
            if (want * 100 > screen_w * 104) {
                long px = (long)d->font_pixels * want / screen_w;
                if (px > (long)d->font_pixels * 14 / 10) px = (long)d->font_pixels * 14 / 10;
                SetFont(rp, slide_font(&t->style, (int)px));
                SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
            }
        }
        for (k = 0; k < nw; ++k) {
            const run_word *w = &run_words[k];
            int x = ol_scale_x(&p->scale, t->bounds.x + w->x0);
            if (w->shape) {
                /* a bullet on its own: drawn, as the fonts' middle dot is too small to see */
                draw_bullet(rp, pen, w->shape, x, baseline, d->font_pixels);
                ogt_set_apen(rp, exact_pen(pen));
                continue;
            }
            Move(rp, x, baseline);
            Text(rp, (STRPTR)w->text, (WORD)w->n);
        }
    }
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (t->style.text_flags & OL_TEXT_UNDERLINE) ogt_hline(rp, pen, d->box.x, baseline + 1, d->box.width);
    if (t->style.text_flags & OL_TEXT_STRIKE) ogt_hline(rp, pen, d->box.x, baseline - d->font_pixels / 3, d->box.width);
}

static void paint_op(void *userdata, const ol_device_op *d)
{
    painter *p = (painter *)userdata;
    struct RastPort *rp = p->rp;
    int frame = opr_slide_frame_of(p->s, d->op);
    shapes_up_to(p, frame);
    switch (d->op->type) {
    case OL_OP_TEXT:
        paint_text(p, d);
        break;
    case OL_OP_FILL_RECT:
        if (d->op->u.fill_rect.argb >> 24)
            ogt_box(rp, argb_pen(d->op->u.fill_rect.argb), d->box.x, d->box.y, d->box.width, d->box.height);
        break;
    case OL_OP_RULE: {
        LONG pen = argb_pen(d->op->u.rule.argb);
        int x1 = d->x1 < d->x2 ? d->x1 : d->x2, x2 = d->x1 < d->x2 ? d->x2 : d->x1;
        int y1 = d->y1 < d->y2 ? d->y1 : d->y2, y2 = d->y1 < d->y2 ? d->y2 : d->y1;
        if (y1 == y2) ogt_box(rp, pen, x1, y1 - (d->thickness - 1) / 2, x2 - x1 + 1, d->thickness);
        else if (x1 == x2) ogt_box(rp, pen, x1 - (d->thickness - 1) / 2, y1, d->thickness, y2 - y1 + 1);
        else { ogt_set_apen(rp, exact_pen(pen)); Move(rp, d->x1, d->y1); Draw(rp, d->x2, d->y2); }
        break;
    }
    case OL_OP_IMAGE: {
        int index = d->op->u.image.image_id;
        const owf_obj *o = frame >= 0 ? p->s->frames[frame].obj : NULL;
        picture *c = p->thumb ? NULL : picture_get(index);
        if (d->box.width < 1 || d->box.height < 1) break;
        if (c && CyberGfxBase && screen_depth() > 8) {
            ol_pixel_rect lim = p->lim;
            int bx = d->box.x, by = d->box.y, bw = d->box.width, bh = d->box.height;
            /* the box's part of the slide only, and a cropped picture larger behind it */
            if (lim.x < d->box.x) { lim.width -= d->box.x - lim.x; lim.x = d->box.x; }
            if (lim.y < d->box.y) { lim.height -= d->box.y - lim.y; lim.y = d->box.y; }
            if (lim.x + lim.width > d->box.x + d->box.width) lim.width = d->box.x + d->box.width - lim.x;
            if (lim.y + lim.height > d->box.y + d->box.height) lim.height = d->box.y + d->box.height - lim.y;
            if (o && o->kind == OWF_OBJ_PICTURE && (o->crop[0] || o->crop[1] || o->crop[2] || o->crop[3]) &&
                o->crop[0] + o->crop[2] < 1000 && o->crop[1] + o->crop[3] < 1000) {
                bw = (int)((long)d->box.width * 1000 / (1000 - o->crop[0] - o->crop[2]));
                bh = (int)((long)d->box.height * 1000 / (1000 - o->crop[1] - o->crop[3]));
                bx = d->box.x - (int)((long)bw * o->crop[0] / 1000);
                by = d->box.y - (int)((long)bh * o->crop[1] / 1000);
            }
            draw_picture(rp, c, bx, by, bw, bh, &lim);
        } else {
            ogt_frame(rp, ogt_pen(&ctx, "group.line"), d->box.x, d->box.y, d->box.width, d->box.height);
            if (!p->thumb && d->box.width > 40 && d->box.height > fh + 8) {
                SetFont(rp, font);
                ogt_text(rp, ogt_pen(&ctx, "muted"), d->box.x + 6, d->box.y + 6, "Picture", d->box.width - 12);
            }
        }
        break;
    }
    }
}

/* The slide at x, y, w wide (its height from the deck's shape), what crosses clip (NULL: all of it). */
static void paint_slide(struct RastPort *rp, const opr_slide *s, int x, int y, int w, const ol_pixel_rect *clip,
                        int thumb, ol_scale *out)
{
    painter p;
    int h = (int)((long)w * deck->height / deck->width);
    memset(&p, 0, sizeof p);
    p.rp = rp;
    p.s = s;
    p.thumb = thumb;
    ol_scale_fit(&p.scale, TW(deck->width), w);
    p.scale.origin_x = x;
    p.scale.origin_y = y;
    p.lim.x = x;
    p.lim.y = y;
    p.lim.width = w;
    p.lim.height = h;
    if (clip) {
        int x1 = x + w < clip->x + clip->width ? x + w : clip->x + clip->width;
        int y1 = y + h < clip->y + clip->height ? y + h : clip->y + clip->height;
        if (p.lim.x < clip->x) p.lim.x = clip->x;
        if (p.lim.y < clip->y) p.lim.y = clip->y;
        p.lim.width = x1 - p.lim.x;
        p.lim.height = y1 - p.lim.y;
    }
    if (out) *out = p.scale;
    if (!s) {
        ogt_box(rp, argb_pen(0xffffffffu), x, y, w, h);
        return;
    }
    ol_draw_page(s->doc, 0, &p.scale, &p.lim, paint_op, &p);
    shapes_up_to(&p, s->nframes);
    SetFont(rp, font);
}

/* ---- The window ---- */

static void update_window_title(void)
{
    static char title[sizeof current_path + 32];     /* static: Intuition keeps the pointer */
    const char *name = current_path[0] ? leaf(current_path) : "Untitled";
    snprintf(title, sizeof title, "%s%s - " PROGRAM, dirty ? "*" : "", name);
    if (win) SetWindowTitles(win, (STRPTR)title, (STRPTR)-1);
}

static void update_edit_tools(void)
{
    ogt_toolbar_enable(&tb, C_UNDO, opr_undo_can(undo));
    ogt_toolbar_enable(&tb, C_REDO, opr_redo_can(undo));
}

/* Where the slide goes in the view: as large as fits, its shape kept, centred. */
static void place_slide(void)
{
    int mw = view_box.w - 24, mh = view_box.h - 24, w, h;
    if (!deck || mw < 16 || mh < 16) {
        slide_box = (box){ view_box.x, view_box.y, 0, 0 };
        return;
    }
    w = mw;
    h = (int)((long)w * deck->height / deck->width);
    if (h > mh) {
        h = mh;
        w = (int)((long)h * deck->width / deck->height);
    }
    slide_box = (box){ view_box.x + (view_box.w - w) / 2, view_box.y + (view_box.h - h) / 2, w, h };
}

static int thumb_height(void *user, int index)
{
    int w = strip_box.w - 32;
    (void)user; (void)index;
    if (!deck || w < 8) return fh + 8;
    return (int)((long)w * deck->height / deck->width) + fh + 14;
}

static void thumb_draw(void *user, int index, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    int tw = w - 20, th, tx = x + 10, ty = y + 4;
    char num[32];
    opr_slide *s;
    ol_pixel_rect clip;
    (void)user; (void)h;
    if (!deck || index >= deck->nslides || tw < 8) return;
    th = (int)((long)tw * deck->height / deck->width);
    ogt_box(rp, ogt_pen(&ctx, "track"), tx + 2, ty + 2, tw, th);
    s = laid_get(index);
    clip.x = tx;
    clip.y = ty;
    clip.width = tw;
    clip.height = th;
    paint_slide(rp, s, tx, ty, tw, &clip, 1, NULL);
    ogt_frame(rp, ogt_pen(&ctx, selected || index == current ? "accent" : "group.line"), tx, ty, tw, th);
    if (selected || index == current) ogt_frame(rp, ogt_pen(&ctx, "accent"), tx - 1, ty - 1, tw + 2, th + 2);
    snprintf(num, sizeof num, "%d%s", index + 1, deck->slides[index].hidden ? " (hidden)" : "");
    SetFont(rp, font);
    ogt_text(rp, ogt_pen(&ctx, "muted"), x + (w - ogt_text_width(rp, num)) / 2, ty + th + 3, num, 0);
}

static void free_gadgets(void)
{
    if (!win) return;
    if (chain) { RemoveGList(win, chain, -1); chain = NULL; }
    if (strip) { ogt_list_free(strip); strip = NULL; }
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    int tbh, stripw, body_y, body_h;
    free_gadgets();
    area.x = win->BorderLeft;
    area.y = win->BorderTop;
    area.w = win->Width - win->BorderLeft - win->BorderRight;
    area.h = win->Height - win->BorderTop - win->BorderBottom;
    SetFont(rp, font);
    ogt_toolbar_set(&tb, tools, sizeof tools / sizeof tools[0], area.w < 600 ? OGT_TB_ICONS : tb_style);
    update_edit_tools();
    tbh = ogt_toolbar_layout(&tb, rp, area.x + 5, area.y + 3, area.w - 10);
    toolbar_box = (box){ area.x, area.y, area.w, tbh + 7 };
    status_box = (box){ area.x, area.y + area.h - fh - 10, area.w, fh + 10 };
    body_y = toolbar_box.y + toolbar_box.h;
    body_h = status_box.y - body_y;
    /* the slides beside the slide, hidden in a narrow window (as OpenWrite's navigator) */
    stripw = area.w >= 480 ? (area.w < 1000 ? 140 : area.w / 7) : 0;
    strip_box = (box){ area.x, body_y, stripw, body_h };
    view_box = (box){ area.x + stripw + (stripw ? 1 : 0), body_y, area.w - stripw - (stripw ? 1 : 0), body_h };
    place_slide();
    if ((chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR)))
        AddGList(win, chain, ~0, -1, NULL);
    if (stripw && (strip = ogt_list_new(win, &ctx, GID_SLIDES, thumb_height, thumb_draw, NULL, NULL, "list"))) {
        ogt_list_layout(strip, strip_box.x + 4, strip_box.y + 4, strip_box.w - 8, strip_box.h - 8);
        ogt_list_set_count(strip, deck ? deck->nslides : 0);
        ogt_list_select(strip, current);
    }
    ogt_status_init(&sbar, win, &ctx, 4);
    ogt_status_layout(&sbar, status_box.x, status_box.y, status_box.w, status_box.h);
}

/* A layout's name in words. LibreOffice names its ODP layouts "AL<n>T<k>",
   k being its layout number: those few that have plain names get them,
   the rest show nothing rather than a code. */
static const char *layout_words(const char *name)
{
    const char *p = name;
    long k;
    if (!p || p[0] != 'A' || p[1] != 'L' || !isdigit((unsigned char)p[2])) return name;
    for (p += 2; isdigit((unsigned char)*p); ++p) ;
    if (*p != 'T' || !isdigit((unsigned char)p[1])) return name;
    k = strtol(p + 1, (char **)&p, 10);
    if (*p) return name;
    switch (k) {
    case 0: return "Title Slide";
    case 1: return "Title, Content";
    case 3: return "Title and 2 Content";
    case 18: return "Title, 4 Content";
    case 19: return "Title Only";
    case 20: return "Blank";
    default: return NULL;
    }
}

static void draw_status(void)
{
    char s[96];
    const char *lname = NULL;
    if (!win) return;
    if (deck && deck->nslides) {
        int l = deck->slides[current].layout;
        snprintf(s, sizeof s, "Slide %d of %d", current + 1, deck->nslides);
        if (l >= 0 && l < deck->nlayouts) lname = layout_words(deck->layouts[l].name);
    } else {
        snprintf(s, sizeof s, "No slides");
    }
    ogt_status_set(&sbar, 0, s);
    ogt_status_set(&sbar, 1, lname ? lname : "");
    ogt_status_set(&sbar, 2, status);
    ogt_status_set(&sbar, 3, current_format);
}

/* The caret and the frame being typed in, over the slide. */
static void draw_caret(const opr_slide *s, const ol_scale *sc)
{
    int frame;
    ol_rect b;
    if (edit_obj < 0 || !s || (frame = opr_slide_frame_for(s, edit_obj)) < 0) return;
    {
        const owf_obj *o = &deck->slides[current].objs[edit_obj];
        int x = ol_scale_x(sc, TW(o->x)), y = ol_scale_y(sc, TW(o->y));
        int x2 = ol_scale_x(sc, TW(o->x + o->width)), y2 = ol_scale_y(sc, TW(o->y + o->height));
        ogt_frame(win->RPort, ogt_pen(&ctx, "accent"), x - 2, y - 2, x2 - x + 4, y2 - y + 4);
    }
    if (opr_caret_box(s, frame, caret, NULL, NULL, &b)) {
        int x = ol_scale_x(sc, b.x), y = ol_scale_y(sc, b.y), h = ol_scale_length(sc, b.height);
        ogt_box(win->RPort, ogt_pen(&ctx, "accent"), x, y, 2, h > 2 ? h : 2);
    }
}

/* The slide, or only what of it is in the rectangle (the frame being typed in: no flicker elsewhere), drawn
 * through a clip region, so nothing a slide holds is drawn outside it. */
static void draw_slide_part(const box *part)
{
    struct RastPort *rp;
    struct Region *region = NULL, *old = NULL;
    struct Rectangle r;
    opr_slide *s;
    ol_scale sc;
    ol_pixel_rect clip;
    if (!win) return;
    rp = win->RPort;
    if (!part) {
        /* the window's own colour around the slide, so the slide stands out */
        ogt_fill(&ctx, rp, "window", view_box.x, view_box.y, view_box.w, view_box.h);
        if (!deck || !deck->nslides || slide_box.w <= 0) {
            ogt_text(rp, ogt_pen(&ctx, "muted"), view_box.x + 16, view_box.y + 16, "No slides.", view_box.w - 32);
            return;
        }
        ogt_box(rp, ogt_pen(&ctx, "track"), slide_box.x + 4, slide_box.y + 4, slide_box.w, slide_box.h);
        ogt_frame(rp, ogt_pen(&ctx, "group.line"), slide_box.x - 1, slide_box.y - 1, slide_box.w + 2, slide_box.h + 2);
    }
    if (!deck || !deck->nslides || slide_box.w <= 0) return;
    r.MinX = (WORD)slide_box.x;
    r.MinY = (WORD)slide_box.y;
    r.MaxX = (WORD)(slide_box.x + slide_box.w - 1);
    r.MaxY = (WORD)(slide_box.y + slide_box.h - 1);
    if (part) {
        if (part->x > r.MinX) r.MinX = (WORD)part->x;
        if (part->y > r.MinY) r.MinY = (WORD)part->y;
        if (part->x + part->w - 1 < r.MaxX) r.MaxX = (WORD)(part->x + part->w - 1);
        if (part->y + part->h - 1 < r.MaxY) r.MaxY = (WORD)(part->y + part->h - 1);
        if (r.MaxX < r.MinX || r.MaxY < r.MinY) return;
    }
    if (LayersBase && (region = NewRegion()) != NULL) {
        if (OrRectRegion(region, &r)) {
            old = InstallClipRegion(win->WLayer, region);
        } else {
            DisposeRegion(region);
            region = NULL;
        }
    }
    clip.x = r.MinX;
    clip.y = r.MinY;
    clip.width = r.MaxX - r.MinX + 1;
    clip.height = r.MaxY - r.MinY + 1;
    s = laid_get(current);
    paint_slide(rp, s, slide_box.x, slide_box.y, slide_box.w, &clip, 0, &sc);
    if (!s)
        ogt_text(rp, ogt_pen(&ctx, "muted"), slide_box.x + 8, slide_box.y + 8, "Not enough memory to show this slide.",
                 slide_box.w - 16);
    draw_caret(s, &sc);
    if (region) {
        InstallClipRegion(win->WLayer, old);
        DisposeRegion(region);
    }
}

static void draw_slide(void)
{
    draw_slide_part(NULL);
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
    if (strip) {
        ogt_list_draw(strip);
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), strip_box.x + strip_box.w, strip_box.y, strip_box.h);
    }
    draw_slide();
    draw_status();
}

static void relayout(void)
{
    layout();
    draw_all();
}

/* ---- Slides and typing ---- */

static void stop_typing(void)
{
    edit_obj = -1;
    edit_grouped = 0;
}

static void show_slide(int index)
{
    if (!deck || !deck->nslides) return;
    if (index < 0) index = 0;
    if (index >= deck->nslides) index = deck->nslides - 1;
    if (index != current) stop_typing();
    current = index;
    if (strip) {
        ogt_list_select(strip, current);
        ogt_list_show(strip, current);
        ogt_list_draw(strip);
    }
    {
        char msg[64];
        snprintf(msg, sizeof msg, "Slide %d.", current + 1);
        set_status(msg);
    }
    draw_slide();
    draw_status();
}

/* The pixel box of the object typed in, and around it, as laid out now: what a keystroke redraws. */
static int edit_area(box *b)
{
    opr_slide *s;
    int frame;
    ol_scale sc;
    ol_rect r;
    if (edit_obj < 0 || !deck || !(s = laid_get(current)) || (frame = opr_slide_frame_for(s, edit_obj)) < 0) return 0;
    ol_scale_fit(&sc, TW(deck->width), slide_box.w);
    sc.origin_x = slide_box.x;
    sc.origin_y = slide_box.y;
    r = ol_node_bounds(ol_node_by_id(s->doc, s->frames[frame].node));
    {
        const owf_obj *o = &deck->slides[current].objs[edit_obj];
        int x = ol_scale_x(&sc, TW(o->x)), y = ol_scale_y(&sc, TW(o->y));
        int x2 = ol_scale_x(&sc, TW(o->x + o->width)), y2 = ol_scale_y(&sc, TW(o->y + o->height));
        int ry2 = ol_scale_y(&sc, r.y + r.height);
        if (ry2 > y2) y2 = ry2;
        /* the text may have run below its box, and the caret and frame are drawn around it */
        b->x = x - 4;
        b->y = y - 4;
        b->w = x2 - x + 8;
        b->h = y2 - y + 8 + slide_box.h / 4;
    }
    return 1;
}

static void union_box(box *a, const box *b)
{
    int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
    if (b->x < a->x) a->x = b->x;
    if (b->y < a->y) a->y = b->y;
    a->w = x1 - a->x;
    a->h = y1 - a->y;
}

/* After a change to the object typed in: laid out again, and its part of the slide drawn. */
static void text_changed(const box *before)
{
    box after;
    slide_changed(current);
    if (!dirty) {
        dirty = 1;
        update_window_title();
    }
    if (edit_area(&after)) {
        if (before) union_box(&after, before);
        draw_slide_part(&after);
    } else {
        draw_slide();
    }
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
}

/* Before a change: an undo step, once for each run of typing. */
static void before_change(box *b)
{
    if (!edit_area(b)) b->w = 0;
    if (!edit_grouped) {
        opr_undo_push(undo, deck, current, edit_obj);
        edit_grouped = 1;
    }
}

static void caret_moved(void)
{
    box b;
    edit_grouped = 0;
    if (edit_area(&b)) draw_slide_part(&b);
}

static void type_text(const char *utf8, size_t n)
{
    box b;
    if (edit_obj < 0) return;
    before_change(&b);
    if (opr_text_insert(deck, current, edit_obj, &caret, utf8, n) < 0) {
        tell(PROGRAM, "There is not enough memory for that.");
        return;
    }
    text_changed(b.w ? &b : NULL);
}

static void type_key(int key)
{
    box b;
    int rc = 0;
    if (edit_obj < 0) return;
    before_change(&b);
    if (key == 8) rc = opr_text_backspace(deck, current, edit_obj, &caret);
    else if (key == 127) rc = opr_text_delete(deck, current, edit_obj, &caret);
    else if (key == 13) rc = opr_text_newline(deck, current, edit_obj, &caret);
    if (rc < 0) tell(PROGRAM, "There is not enough memory for that.");
    text_changed(b.w ? &b : NULL);
}

/* Up or down a line: the place nearest the caret on the line above or below, in the same text. */
static void caret_line(int dir)
{
    opr_slide *s = laid_get(current);
    int frame;
    ol_rect b;
    opr_pos to;
    if (!s || (frame = opr_slide_frame_for(s, edit_obj)) < 0 || !opr_caret_box(s, frame, caret, NULL, NULL, &b))
        return;
    if (opr_hit_frame(s, frame, b.x, dir < 0 ? b.y - b.height / 2 : b.y + b.height + b.height / 2, NULL, NULL, &to))
        caret = to;
    caret_moved();
}

/* A click on the slide: typing starts in the text box under it, at the place clicked. */
static void slide_click(int mx, int my)
{
    opr_slide *s = laid_get(current);
    ol_scale sc;
    opr_pos at;
    int frame, obj, was = edit_obj;
    box old;
    if (!s || mx < slide_box.x || my < slide_box.y || mx >= slide_box.x + slide_box.w || my >= slide_box.y + slide_box.h)
        return;
    ol_scale_fit(&sc, TW(deck->width), slide_box.w);
    sc.origin_x = slide_box.x;
    sc.origin_y = slide_box.y;
    if (!edit_area(&old)) old.w = 0;
    frame = opr_hit(s, ol_unscale_x(&sc, mx), ol_unscale_y(&sc, my), NULL, NULL, &at);
    obj = frame >= 0 ? opr_frame_obj(s, frame) : -1;
    if (obj >= 0 && opr_obj_editable(deck, current, obj)) {
        edit_obj = obj;
        caret = at;
        edit_grouped = 0;
        set_status("Typing: Esc stops.");
    } else {
        stop_typing();
        set_status(obj >= 0 ? "Only text boxes and shapes take typing yet." : "Ready.");
    }
    if (was >= 0 && old.w) draw_slide_part(&old);
    caret_moved();
    draw_status();
}

static void do_undo(int redo)
{
    int slide = -1, obj = -1;
    if ((redo ? opr_undo_redo(undo, deck, &slide, &obj) : opr_undo_undo(undo, deck, &slide, &obj)) < 0) return;
    slide_changed(slide);
    dirty = 1;
    update_window_title();
    if (slide != current) show_slide(slide);
    edit_obj = obj;
    edit_grouped = 0;
    opr_text_move(deck, current, edit_obj, &caret, OPR_MOVE_TEXT_END);
    set_status(redo ? "Redone." : "Undone.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

/* ---- Files ---- */

static void set_deck(owf_deck *d, const char *path, const char *format)
{
    laid_clear();
    pictures_clear();
    if (undo) opr_undo_clear(undo);
    if (deck) owf_deck_free(deck);
    deck = d;
    free(versions);
    versions = NULL;
    nversions = 0;
    current = 0;
    dirty = 0;
    stop_typing();
    snprintf(current_path, sizeof current_path, "%s", path ? path : "");
    snprintf(current_format, sizeof current_format, "%s", format ? format : "ODP");
    update_window_title();
}

static int confirm_discard(void)
{
    if (!dirty) return 1;
    return ask(PROGRAM, "The presentation has changes that are not saved.\n\nLose them?", "Lose Them|Cancel") == 1;
}

static int ask_file(int save, const char *title, const char *initial, char *out, size_t out_size)
{
    struct FileRequester *fr;
    char last[512];
    int ok = 0;
    lastdir_get(PROGRAM, last, sizeof last);
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)title,
        ASLFR_InitialDrawer, (ULONG)last,
        ASLFR_Window, (ULONG)win,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, save ? TRUE : FALSE,
        ASLFR_InitialFile, (ULONG)(initial ? initial : ""),
        ASLFR_PositiveText, (ULONG)(save ? "Save" : "Open"),
        TAG_DONE);
    if (!fr) return 0;
    if (AslRequestTags(fr, TAG_DONE) && fr->fr_File[0]) {
        snprintf(out, out_size, "%s", fr->fr_Drawer);
        lastdir_put(PROGRAM, (const char *)fr->fr_Drawer);
        AddPart((STRPTR)out, fr->fr_File, out_size);
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

/* The file's name with another extension: "Talk.pptx" as "Talk.odp". */
static void with_extension(const char *path, const char *ext, char *out, size_t size)
{
    const char *name = path && *path ? leaf(path) : "Untitled";
    const char *dot = strrchr(name, '.');
    int n = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    snprintf(out, size, "%.*s.%s", n > 100 ? 100 : n, name, ext);
}

static void open_deck(const char *path)
{
    owf_deck *d = NULL;
    owf_report *report = owf_report_new();
    const owf_deck_format *used = NULL;
    char msg[600];
    int rc = owf_import_deck_file(path, NULL, &d, report, &used);
    if (rc != OWF_OK || !d) {
        snprintf(msg, sizeof msg, PROGRAM " could not open %.200s.\n\n%.200s", leaf(path),
                 rc == OWF_ERR_FORMAT ? "It is not a presentation OpenPresentation reads (ODP or PPTX)." : owf_error_text(rc));
        tell("Open", msg);
        owf_report_free(report);
        return;
    }
    set_deck(d, path, used && !strcmp(used->name, "pptx") ? "PPTX" : "ODP");
    snprintf(msg, sizeof msg, "%d slide%s%s.", d->nslides, d->nslides == 1 ? "" : "s",
             owf_report_count(report) ? "; some parts shown simpler" : "");
    set_status(msg);
    owf_report_free(report);
}

static void new_deck(void)
{
    owf_deck *d;
    if (!confirm_discard()) return;
    d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    if (!d || !owf_deck_add_slide(d, -1, 1)) {      /* a title slide */
        owf_deck_free(d);
        tell(PROGRAM, "There is not enough memory for a new presentation.");
        return;
    }
    set_deck(d, "", "ODP");
    set_status("A new presentation: click a box to type in it.");
    relayout();
}

static void do_open(void)
{
    char path[512];
    if (!confirm_discard()) return;
    if (!ask_file(0, "Open a presentation", NULL, path, sizeof path)) return;
    open_deck(path);
    relayout();
}

static int save_to(const char *path)
{
    owf_report *report = owf_report_new();
    char msg[600];
    int rc = owf_export_deck_file(deck, path, "odp", report);
    owf_report_free(report);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, PROGRAM " could not save %.200s.\n\n%.200s", leaf(path), owf_error_text(rc));
        tell("Save", msg);
        return 0;
    }
    snprintf(current_path, sizeof current_path, "%s", path);
    snprintf(current_format, sizeof current_format, "ODP");
    dirty = 0;
    snprintf(msg, sizeof msg, "%.200s saved.", leaf(path));
    set_status(msg);
    update_window_title();
    draw_status();
    return 1;
}

static void do_save_as(void)
{
    char path[512], name[128];
    if (!deck) return;
    with_extension(current_path, "odp", name, sizeof name);
    if (ask_file(1, "Save the presentation as ODP", name, path, sizeof path)) {
        size_t n = strlen(path);
        if (n < 4 || (strcmp(path + n - 4, ".odp") && strcmp(path + n - 4, ".ODP")))
            snprintf(path + n, sizeof path - n, ".odp");
        save_to(path);
    }
}

static void do_save(void)
{
    size_t n = strlen(current_path);
    /* PPTX is read only for now: a PowerPoint file is saved as ODP beside it, under a name asked for */
    if (!n || strcmp(current_format, "ODP") || n < 4 || (strcmp(current_path + n - 4, ".odp") &&
                                                         strcmp(current_path + n - 4, ".ODP"))) {
        do_save_as();
        return;
    }
    save_to(current_path);
}

static int write_pdf(const char *path)
{
    unsigned char *data = NULL;
    size_t length = 0;
    owf_report *report = owf_report_new();
    BPTR f;
    int rc = opr_export_pdf(deck, NULL, &data, &length, report), ok = 0;
    owf_report_free(report);
    if (rc == OWF_OK && (f = Open((STRPTR)path, MODE_NEWFILE))) {
        ok = Write(f, data, (LONG)length) == (LONG)length;
        Close(f);
    }
    free(data);
    return ok;
}

static void do_export_pdf(void)
{
    char path[512], name[128], msg[600];
    if (!deck) return;
    with_extension(current_path, "pdf", name, sizeof name);
    if (!ask_file(1, "Export the slides as PDF", name, path, sizeof path)) return;
    if (write_pdf(path)) snprintf(msg, sizeof msg, "%.200s written: a page a slide.", leaf(path));
    else snprintf(msg, sizeof msg, "%.200s could not be written.", leaf(path));
    set_status(msg);
    draw_status();
}

/* Print: the slides as a PDF in T:, and OpenPrint's Print window on it, started on its own (OpenWrite's way,
 * app/ow_print.c). OpenPrint chooses the printer, or saves the PDF. */
#define OPENPRINT "C:OpenPrint"
static void do_print(void)
{
    char path[160], name[128], cmd[300];
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR lock, in, out;
    if (!deck) return;
    me->pr_WindowPtr = (APTR)-1;
    lock = Lock((STRPTR)OPENPRINT, ACCESS_READ);
    me->pr_WindowPtr = was;
    if (!lock) {
        tell("Print", "Printing needs OpenPrint (C:OpenPrint).\n\nExport PDF still works.");
        return;
    }
    UnLock(lock);
    with_extension(current_path, "pdf", name, sizeof name);
    snprintf(path, sizeof path, "T:%s", name);
    if (!write_pdf(path)) {
        set_status("The slides couldn't be made ready to print.");
        draw_status();
        return;
    }
    snprintf(cmd, sizeof cmd, "%s \"%s\"", OPENPRINT, path);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, in, SYS_Output, out, NP_StackSize, 65536,
                                  TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
        set_status("OpenPrint couldn't be started.");
    } else {
        set_status("OpenPrint's Print window is open for these slides.");
    }
    draw_status();
}

static void about(void)
{
    char text[400];
    snprintf(text, sizeof text,
             VERSION_TEXT "\nThe GFOffice Suite's slides.\n\nOpens ODP and PPTX, saves ODP, exports PDF,\n"
             "prints through OpenPrint.\n\nCore %s, libowf %s, OpenLayout %s.\nMIT, Copyright (c) 2026 AmigaChrome Team.",
             OPR_CORE_VERSION, OWF_VERSION, OL_VERSION);
    tell("About " PROGRAM, text);
}

/* ---- Commands ---- */

static void set_buttons(int style)
{
    tb_style = style;
    relayout();
}

static void menu_action(ULONG id)
{
    switch (id) {
    case M_NEW: new_deck(); break;
    case M_OPEN: do_open(); break;
    case M_SAVE: do_save(); break;
    case M_SAVE_AS: do_save_as(); break;
    case M_PRINT: do_print(); break;
    case M_PDF: do_export_pdf(); break;
    case M_ABOUT: about(); break;
    case M_QUIT: if (confirm_discard()) quit_now = 1; break;
    case M_UNDO: do_undo(0); break;
    case M_REDO: do_undo(1); break;
    case M_STOP:
        stop_typing();
        draw_slide();
        break;
    case M_FIRST: show_slide(0); break;
    case M_PREV: show_slide(current - 1); break;
    case M_NEXT: show_slide(current + 1); break;
    case M_LAST: show_slide(deck ? deck->nslides - 1 : 0); break;
    case M_TB_BOTH: set_buttons(OGT_TB_ICONS_TEXT); break;
    case M_TB_ICONS: set_buttons(OGT_TB_ICONS); break;
    case M_TB_TEXT: set_buttons(OGT_TB_TEXT); break;
    }
}

static void tool_command(int id)
{
    switch (id) {
    case C_NEW: menu_action(M_NEW); break;
    case C_OPEN: menu_action(M_OPEN); break;
    case C_SAVE: menu_action(M_SAVE); break;
    case C_PRINT: menu_action(M_PRINT); break;
    case C_PDF: menu_action(M_PDF); break;
    case C_UNDO: menu_action(M_UNDO); break;
    case C_REDO: menu_action(M_REDO); break;
    }
}

static void toolbar_event(ULONG class, struct Gadget *g)
{
    int i = ogt_toolbar_index(&tb, g->GadgetID);
    if (i < 0) return;
    if (class == IDCMP_GADGETDOWN) {
        tb.pressed = i;
        ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 1, "window");
        return;
    }
    ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 0, "window");
    tb.pressed = -1;
    if (!tb.tool[i].disabled) tool_command(tb.tool[i].id);
}

/* A typed character as UTF-8 (the keymap gives ISO-8859-1). */
static int vanilla_utf8(UWORD code, char out[3])
{
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    out[0] = (char)(0xC0 | (code >> 6));
    out[1] = (char)(0x80 | (code & 0x3F));
    return 2;
}

static void events(void)
{
    struct IntuiMessage *im;
    while (win && (im = GT_GetIMsg(win->UserPort))) {
        ULONG class = im->Class, secs = im->Seconds, micros = im->Micros;
        UWORD code = im->Code, qual = im->Qualifier;
        APTR ia = im->IAddress;
        int mx = im->MouseX, my = im->MouseY, row = -1, r, handled = 0;
        struct Gadget *g = (struct Gadget *)ia;
        if (strip) {
            r = ogt_list_event(strip, class, code, ia, mx, my, secs, micros, &row);
            if (r == OGT_LIST_PICKED || r == OGT_LIST_OPENED) { show_slide(row); handled = 1; }
            else if (r == OGT_LIST_SCROLLED) handled = 1;
        }
        GT_ReplyIMsg(im);
        if (handled) continue;
        switch (class) {
        case IDCMP_CLOSEWINDOW:
            if (confirm_discard()) quit_now = 1;
            break;
        case IDCMP_NEWSIZE:
            relayout();
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win);
            draw_all();
            GT_EndRefresh(win, TRUE);
            break;
        case IDCMP_GADGETDOWN:
        case IDCMP_GADGETUP:
            if (g && g->GadgetID >= GID_TOOLBAR && g->GadgetID < GID_SLIDES) toolbar_event(class, g);
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN) slide_click(mx, my);
            else if (code == SELECTUP && tb.pressed >= 0) {
                ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window");
                tb.pressed = -1;
            }
            break;
        case IDCMP_RAWKEY:
            if (code & 0x80) break;
            if (edit_obj >= 0 && !(qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))) {
                switch (code) {
                case 0x4f: opr_text_move(deck, current, edit_obj, &caret, OPR_MOVE_LEFT); caret_moved(); break;
                case 0x4e: opr_text_move(deck, current, edit_obj, &caret, OPR_MOVE_RIGHT); caret_moved(); break;
                case 0x4c: caret_line(-1); break;
                case 0x4d: caret_line(1); break;
                }                               /* Del comes as a vanilla key (127) */
            } else if (code == 0x4c || code == 0x4f) {
                show_slide(current - 1);     /* up, left, or with Shift while typing: the slide before */
            } else if (code == 0x4d || code == 0x4e) {
                show_slide(current + 1);
            }
            break;
        case IDCMP_VANILLAKEY:
            if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) break;
            if (code == 27) {
                if (edit_obj >= 0) {
                    box b;
                    int had = edit_area(&b);
                    stop_typing();
                    set_status("Ready.");
                    if (had) draw_slide_part(&b);
                    draw_status();
                }
                break;
            }
            if (edit_obj < 0) {
                if (code == ' ') show_slide(current + 1);
                else if (code == 8) show_slide(current - 1);
                break;
            }
            if (code == 8 || code == 127 || code == 13) type_key(code);
            else if (code == 9 || (code >= 32 && code < 256 && code != 127)) {
                char u[3];
                int n = vanilla_utf8(code, u);
                type_text(u, (size_t)n);
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

/* ---- Opening and closing ---- */

/* The part of the screen a window may have: below the title bar, less OpenDock's strip when it publishes
 * one (ENV:OpenDock/Free), as OpenWrite's free_area. */
static void free_area(int *l, int *t, int *w, int *h)
{
    char buf[48];
    int a, b, c, d;
    LONG got = GetVar((STRPTR)"OpenDock/Free", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (got > 0 && sscanf(buf, "%d %d %d %d", &a, &b, &c, &d) == 4 && c >= 400 && d >= 200 && a >= 0 && b >= 0 &&
        a + c <= scr->Width && b + d <= scr->Height) {
        *l = a;
        *t = b < scr->BarHeight + 1 ? scr->BarHeight + 1 : b;
        *w = c;
        *h = b + d - *t;
        return;
    }
    *l = 0;
    *t = scr->BarHeight + 1;
    *w = scr->Width;
    *h = scr->Height - *t;
}

static int open_window(void)
{
    int l, t, w, h, aw, ah;
    if (!(scr = LockPubScreen(NULL))) return 0;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return 0;
    load_theme();
    if (!(font = ogt_open_font(&theme, scr, &font_attr))) return 0;
    fh = font->tf_YSize;
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    free_area(&l, &t, &aw, &ah);
    /* 800 x 600 to start (the Open apps' rule), centred, never bigger than the screen allows */
    w = aw < 800 ? aw : 800;
    h = ah < 600 ? ah : 600;
    l += (aw - w) / 2;
    t += (ah - h) / 2;
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)PROGRAM,
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Left, l, WA_Top, t, WA_Width, w, WA_Height, h,
        WA_MinWidth, w < 400 ? w : 400, WA_MinHeight, h < 280 ? h : 280,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
        WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_NEWSIZE |
                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                  IDCMP_IDCMPUPDATE,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    update_window_title();
    relayout();
    return 1;
}

static void close_window(void)
{
    font_cache_clear();
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
    own_pen_free();
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static int window_main(int argc, char **argv)
{
    int rc = 0;
    char first[512] = "";
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)PROGRAM " needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    OpenFontTextBase = ogt_openfont_open();
    LayersBase = OpenLibrary((STRPTR)"layers.library", 36);
    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 0);
    ogt_iconset_attach();   /* the toolbar's pictures from the Chrome set (Images/Toolbar/) */
    read_theme_choice();
    undo = opr_undo_new();
    if (argc > 1) snprintf(first, sizeof first, "%s", argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }
    {
        owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
        if (!d || !owf_deck_add_slide(d, -1, 1) || !undo) {
            owf_deck_free(d);
            rc = 20;
            goto out;
        }
        set_deck(d, "", "ODP");
    }
    if (!open_window()) {
        if (argc) PutStr((STRPTR)PROGRAM ": the window could not open\n");
        rc = 20;
        goto out;
    }
    if (first[0]) {
        open_deck(first);
        relayout();
    }
    while (!quit_now) {
        ULONG sig = 1UL << win->UserPort->mp_SigBit;
        ULONG got = Wait(sig | SIGBREAKF_CTRL_C);
        if ((got & SIGBREAKF_CTRL_C) && confirm_discard()) quit_now = 1;
        if (got & sig) events();
    }
out:
    close_window();
    laid_clear();
    pictures_clear();
    free(versions);
    if (deck) owf_deck_free(deck);
    opr_undo_free(undo);
    if (theme.text) ogt_theme_free(&theme);
    if (LayersBase) CloseLibrary(LayersBase);
    if (OpenFontTextBase) CloseLibrary(OpenFontTextBase);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}

int main(int argc, char **argv)
{
    return ow_run_with_stack(65536, window_main, argc, argv);
}

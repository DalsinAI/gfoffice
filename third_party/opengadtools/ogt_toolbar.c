/* ogt_toolbar: a row of icon buttons drawn in the theme.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include <string.h>

#include "ogt_toolbar.h"
#include "ogt_icons.h"

/* The icon set's calls, or NULL. This file never names the icon set itself,
 * so a program that compiles it without lib/ogt_iconset.c still links
 * (OpenBench, 25 October 2026). */
static const ogt_toolbar_pictures *pictures;

void ogt_toolbar_pictures_use(const ogt_toolbar_pictures *p)
{
    pictures = p;
}

void ogt_toolbar_set(ogt_toolbar *tb, const ogt_tool *tools, int n, int style)
{
    memset(tb, 0, sizeof *tb);
    if (n > OGT_TB_MAX) n = OGT_TB_MAX;
    memcpy(tb->tool, tools, n * sizeof *tools);
    tb->n = n;
    tb->style = style;
    tb->pressed = -1;
}

static int layout_once(ogt_toolbar *tb, struct RastPort *rp, int x, int y, int maxw);

/* Icons with names don't fit: icons only (the names stay in help and the
 * menus) rather than hiding buttons. Text only falls back the same way. */
int ogt_toolbar_layout(ogt_toolbar *tb, struct RastPort *rp, int x, int y, int maxw)
{
    int h = layout_once(tb, rp, x, y, maxw), i, hidden = 0;
    for (i = 0; i < tb->n; i++) hidden += !tb->box[i].w;
    if (hidden && (tb->style == OGT_TB_ICONS_TEXT || tb->style == OGT_TB_TEXT)) {
        tb->style = OGT_TB_ICONS;
        h = layout_once(tb, rp, x, y, maxw);
    }
    return h;
}

static int layout_once(ogt_toolbar *tb, struct RastPort *rp, int x, int y, int maxw)
{
    int fh = rp->TxHeight, i, cx = x, h, images = 0, more;
    for (i = 0; i < tb->n; i++) images |= tb->tool[i].image != NULL;
    tb->sets = pictures && images;
    if (tb->sets) {
        /* the style guide's (sections 3.4 and 4.1): 1.25 times the font's height, rounded to the set's
         * sizes, on a 4-pixel grid: a 28-pixel icon in a 36 x 36 button, buttons 40 apart */
        tb->icon_size = pictures->size(tb->style == OGT_TB_INLINE ? fh + 2 : pictures->want(fh));
        tb->padx = 4; tb->pady = 4; tb->gap = 4; tb->sep = 8; more = 0;
    } else {
        /* as before the icon sets: twice the font's height, 24 at least */
        tb->icon_size = tb->style == OGT_TB_INLINE ? fh + 2 : (fh * 2 > 24 ? fh * 2 : 24);
        tb->padx = 6; tb->pady = 3; tb->gap = 2; tb->sep = 9; more = 2;
    }
    switch (tb->style) {
    case OGT_TB_ICONS: h = tb->icon_size + 2 * tb->pady + more; break;
    case OGT_TB_TEXT: h = fh + 2 * tb->pady + 2 + more; break;
    case OGT_TB_INLINE: h = (tb->icon_size > fh ? tb->icon_size : fh) + 2 * tb->pady + more; break;
    default: h = tb->icon_size + fh + 3 * tb->pady + more; break;
    }
    for (i = 0; i < tb->n; i++) {
        int tw = ogt_text_width(rp, tb->tool[i].label), w;
        if (tb->tool[i].sep_before && i) cx += tb->sep;
        switch (tb->style) {
        case OGT_TB_ICONS: w = tb->icon_size + 2 * tb->padx; break;
        case OGT_TB_TEXT: w = tw + 2 * tb->padx + 4; break;
        case OGT_TB_INLINE: w = tb->icon_size + 4 + tw + 2 * tb->padx; break;
        default: w = (tw > tb->icon_size ? tw : tb->icon_size) + 2 * tb->padx; break;
        }
        if (maxw > 0 && cx + w > x + maxw) w = 0;              /* no room: hidden (the menus still have it) */
        tb->box[i].x = cx;
        tb->box[i].y = y;
        tb->box[i].w = w;
        tb->box[i].h = h;
        cx += w ? w + tb->gap : 0;
    }
    tb->x = x;
    tb->y = y;
    tb->w = cx - x;
    tb->h = h;
    return h;
}

struct Gadget *ogt_toolbar_gadgets(ogt_toolbar *tb, int first_gid)
{
    int i;
    struct Gadget *first = NULL, *prev = NULL;
    tb->first_gid = first_gid;
    for (i = 0; i < tb->n; i++) {
        struct Gadget *g = &tb->gad[i];
        memset(g, 0, sizeof *g);
        if (!tb->box[i].w) continue;
        g->LeftEdge = tb->box[i].x;
        g->TopEdge = tb->box[i].y;
        g->Width = tb->box[i].w;
        g->Height = tb->box[i].h;
        g->Flags = GFLG_GADGHNONE;          /* disabled buttons are drawn faded here, not ghosted by Intuition */
        g->Activation = GACT_RELVERIFY | GACT_IMMEDIATE;
        g->GadgetType = GTYP_BOOLGADGET;
        g->GadgetID = first_gid + i;
        if (prev) prev->NextGadget = g;
        else first = g;
        prev = g;
    }
    return first;
}

int ogt_toolbar_index(const ogt_toolbar *tb, int gadget_id)
{
    int i = gadget_id - tb->first_gid;
    return i >= 0 && i < tb->n ? i : -1;
}

void ogt_toolbar_enable(ogt_toolbar *tb, int id, int enabled)
{
    int i;
    for (i = 0; i < tb->n; i++)
        if (tb->tool[i].id == id) {
            tb->tool[i].disabled = !enabled;
        }
}

void ogt_toolbar_select(ogt_toolbar *tb, int id, int on)
{
    int i;
    for (i = 0; i < tb->n; i++)
        if (tb->tool[i].id == id) tb->tool[i].selected = on != 0;
}

/* The button's picture: from the icon set when it has one, else the vector icon. */
static void picture(const ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, const ogt_tool *t, int x, int y, int s)
{
    if (tb->sets && t->image && pictures->draw(c, rp, t->image, s, x, y, t->disabled)) return;
    ogt_icon_draw(c, rp, t->icon, x, y, s, t->disabled);
}

/* The states (docs/style/README.md, section 2.1): at rest, the bar's own
 * background; on (a toggle), the inactive selection with an accent frame;
 * pressed, the inactive selection sunk, and the picture a pixel down and
 * right; disabled, the set's disabled colours and a muted label. */
void ogt_toolbar_draw_one(ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, int i, int down, const char *bg_key)
{
    const ogt_tool *t = &tb->tool[i];
    int x = tb->box[i].x, y = tb->box[i].y, w = tb->box[i].w, h = tb->box[i].h, fh = rp->TxHeight;
    int tw = ogt_text_width(rp, t->label), s = tb->icon_size, o = down && tb->sets ? 1 : 0;
    int padx = tb->padx, pady = tb->pady, more = tb->sets ? 0 : 1;   /* as each layout always drew it */
    LONG text = t->disabled ? ogt_pen(c, "muted") : ogt_pen(c, "label");
    if (!w) return;
    if (t->primary) {
        ogt_box(rp, ogt_pen(c, "accent"), x, y, w, h);
        ogt_frame(rp, ogt_pen(c, "frame.active"), x, y, w, h);
        text = ogt_pen(c, "accent.text");
    } else if (down) {
        ogt_box(rp, ogt_pen(c, "selection.inactive"), x, y, w, h);
        ogt_bevel(rp, ogt_pen(c, "string.shadow"), ogt_pen(c, "string.shine"), x, y, w, h);
    } else if (t->selected) {
        ogt_box(rp, ogt_pen(c, "selection.inactive"), x, y, w, h);
        ogt_frame(rp, ogt_pen(c, "accent"), x, y, w, h);
    } else {
        ogt_fill(c, rp, bg_key, x, y, w, h);
    }
    if (down && t->primary) ogt_bevel(rp, ogt_pen(c, "string.shadow"), ogt_pen(c, "string.shine"), x, y, w, h);
    ogt_bold(rp, t->primary);
    switch (tb->style) {
    case OGT_TB_ICONS:
        picture(tb, c, rp, t, x + (w - s) / 2 + o, y + pady + more + o, s);
        break;
    case OGT_TB_TEXT:
        ogt_text(rp, text, x + (w - tw) / 2 + o, y + (h - fh) / 2 + o, t->label, w - 4);
        break;
    case OGT_TB_INLINE:
        picture(tb, c, rp, t, x + padx + o, y + (h - s) / 2 + o, s);
        ogt_text(rp, text, x + padx + s + 4 + o, y + (h - fh) / 2 + o, t->label, 0);
        break;
    default:
        picture(tb, c, rp, t, x + (w - s) / 2 + o, y + pady + more + o, s);
        ogt_text(rp, text, x + (w - tw) / 2 + o, y + pady + s + pady + o, t->label, w - 2);
        break;
    }
    ogt_bold(rp, 0);
}

void ogt_toolbar_draw(ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, const char *bg_key)
{
    int i;
    for (i = 0; i < tb->n; i++) {
        if (tb->tool[i].sep_before && i && tb->box[i].w)
            ogt_vline(rp, ogt_pen(c, "group.line"), tb->box[i].x - (tb->sep + tb->gap) / 2, tb->box[i].y + 4, tb->box[i].h - 8);
        ogt_toolbar_draw_one(tb, c, rp, i, i == tb->pressed, bg_key);
    }
}

int ogt_buttons_parse(const char *s)
{
    char w[16];
    int n = 0;
    while (s && (*s == ' ' || *s == '\t' || *s == '=')) s++;
    while (s && *s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && n < (int)sizeof w - 1) {
        char ch = *s++;
        w[n++] = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    }
    w[n] = 0;
    if (!strcmp(w, "icons+text") || !strcmp(w, "both") || !strcmp(w, "icons-text")) return OGT_TB_ICONS_TEXT;
    if (!strcmp(w, "icons")) return OGT_TB_ICONS;
    if (!strcmp(w, "text")) return OGT_TB_TEXT;
    return -1;
}

int ogt_buttons_style(void)
{
    char buf[1024], *l;
    BPTR fh = Open((STRPTR)"ENV:OpenGadTools/Look", MODE_OLDFILE);
    LONG n;
    int style = OGT_TB_ICONS;      /* every Open app starts with icons only (the Team's rule, 10 October 2026) */
    if (!fh) return style;
    n = Read(fh, buf, sizeof buf - 1);
    Close(fh);
    buf[n > 0 ? n : 0] = 0;
    for (l = buf; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        if (!strncmp(l, "buttons", 7) && (l[7] == ' ' || l[7] == '\t')) {
            int s = ogt_buttons_parse(l + 7);
            if (s >= 0) style = s;
        }
    }
    return style;
}

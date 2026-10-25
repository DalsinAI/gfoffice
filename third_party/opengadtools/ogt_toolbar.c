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
#include "ogt_iconset.h"

#define PADX 6
#define PADY 3
#define GAP 2
#define SEP 9

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
    int fh = rp->TxHeight, i, cx = x, h, images = 0;
    tb->icon_size = tb->style == OGT_TB_INLINE ? fh + 2 : (fh * 2 > 24 ? fh * 2 : 24);
    for (i = 0; i < tb->n; i++) images |= tb->tool[i].image != NULL;
    if (images) tb->icon_size = ogt_iconset_size(tb->icon_size);   /* the sizes a set has */
    switch (tb->style) {
    case OGT_TB_ICONS: h = tb->icon_size + 2 * PADY + 2; break;
    case OGT_TB_TEXT: h = fh + 2 * PADY + 4; break;
    case OGT_TB_INLINE: h = (tb->icon_size > fh ? tb->icon_size : fh) + 2 * PADY + 2; break;
    default: h = tb->icon_size + fh + 3 * PADY + 2; break;
    }
    for (i = 0; i < tb->n; i++) {
        int tw = ogt_text_width(rp, tb->tool[i].label), w;
        if (tb->tool[i].sep_before && i) cx += SEP;
        switch (tb->style) {
        case OGT_TB_ICONS: w = tb->icon_size + 2 * PADX; break;
        case OGT_TB_TEXT: w = tw + 2 * PADX + 4; break;
        case OGT_TB_INLINE: w = tb->icon_size + 4 + tw + 2 * PADX; break;
        default: w = (tw > tb->icon_size ? tw : tb->icon_size) + 2 * PADX; break;
        }
        if (maxw > 0 && cx + w > x + maxw) w = 0;              /* no room: hidden (the menus still have it) */
        tb->box[i].x = cx;
        tb->box[i].y = y;
        tb->box[i].w = w;
        tb->box[i].h = h;
        cx += w ? w + GAP : 0;
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
static void picture(ogt_ctx *c, struct RastPort *rp, const ogt_tool *t, int x, int y, int s)
{
    if (t->image && ogt_iconset_draw(c, rp, t->image, s, x, y, t->disabled)) return;
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
    int tw = ogt_text_width(rp, t->label), s = tb->icon_size, o = down ? 1 : 0;
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
        picture(c, rp, t, x + (w - s) / 2 + o, y + PADY + 1 + o, s);
        break;
    case OGT_TB_TEXT:
        ogt_text(rp, text, x + (w - tw) / 2 + o, y + (h - fh) / 2 + o, t->label, w - 4);
        break;
    case OGT_TB_INLINE:
        picture(c, rp, t, x + PADX + o, y + (h - s) / 2 + o, s);
        ogt_text(rp, text, x + PADX + s + 4 + o, y + (h - fh) / 2 + o, t->label, 0);
        break;
    default:
        picture(c, rp, t, x + (w - s) / 2 + o, y + PADY + 1 + o, s);
        ogt_text(rp, text, x + (w - tw) / 2 + o, y + PADY + s + PADY + o, t->label, w - 2);
        break;
    }
    ogt_bold(rp, 0);
}

void ogt_toolbar_draw(ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, const char *bg_key)
{
    int i;
    for (i = 0; i < tb->n; i++) {
        if (tb->tool[i].sep_before && i && tb->box[i].w)
            ogt_vline(rp, ogt_pen(c, "group.line"), tb->box[i].x - SEP / 2 - 1, tb->box[i].y + 4, tb->box[i].h - 8);
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

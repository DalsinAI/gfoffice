/* ogt_toolbar: a row of icon buttons (DESIGN.md section 0: icons only by
 * default; icons with their names under them; text only; or, for small rows,
 * the icon beside its name). Each button is a plain Intuition boolean
 * gadget that draws nothing itself; the toolbar draws them in the theme.
 *
 * Pictures from an icon set: a program that links lib/ogt_iconset.c and
 * calls ogt_iconset_attach() once gives its buttons pictures by name (the
 * tool's image), sized and spaced as the style guide has them
 * (docs/style/README.md, sections 3.4 and 4.1: 1.25 times the font's
 * height, rounded to the set's sizes, on a 4-pixel grid), in its states
 * (section 2.1). Without that, this file needs nothing of the icon set, and
 * buttons show their vector icons, laid out and drawn as before the sets.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OGT_TOOLBAR_H
#define OGT_TOOLBAR_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "ogt_draw.h"

enum { OGT_TB_ICONS_TEXT = 0, OGT_TB_ICONS = 1, OGT_TB_TEXT = 2, OGT_TB_INLINE = 3 };

#define OGT_TB_MAX 24

typedef struct ogt_tool {
    int id;             /* the program's command number */
    const char *label;
    int icon;           /* OGT_ICON_*: the vector icon, drawn when there is no picture */
    int sep_before;     /* a separator line before it */
    int disabled;
    int primary;        /* drawn in the accent colour (Send) */
    const char *image;  /* the picture's name in the icon set ("save"; ogt_iconset), or NULL */
    int selected;       /* on, as a toggle is (Bold): ogt_toolbar_select() */
} ogt_tool;

typedef struct ogt_toolbar {
    int n, style, icon_size;
    ogt_tool tool[OGT_TB_MAX];
    struct { int x, y, w, h; } box[OGT_TB_MAX];
    struct Gadget gad[OGT_TB_MAX];
    int x, y, w, h;     /* the row */
    int pressed;        /* the button held down, or -1 */
    int first_gid;
    int sets;           /* laid out for an icon set's pictures (the style guide's measures) */
    int padx, pady, gap, sep;
} ogt_toolbar;

/* An icon set's calls, handed to the toolbar by ogt_iconset_attach(). */
typedef struct ogt_toolbar_pictures {
    int (*want)(int font_height);       /* the icon size a font asks for */
    int (*size)(int want);              /* the nearest size the set has */
    int (*draw)(ogt_ctx *c, struct RastPort *rp, const char *name, int size, int x, int y, int disabled);
} ogt_toolbar_pictures;

/* Gives every toolbar an icon set's pictures (NULL: none, the vector icons). */
void ogt_toolbar_pictures_use(const ogt_toolbar_pictures *p);

/* Sets the buttons (copied). */
void ogt_toolbar_set(ogt_toolbar *tb, const ogt_tool *tools, int n, int style);
/* Places the buttons in a row starting at x,y (font from rp); returns the row's height. */
int ogt_toolbar_layout(ogt_toolbar *tb, struct RastPort *rp, int x, int y, int maxw);
/* The buttons' gadgets, linked in a chain, with ids from first_gid. Add them to the window. */
struct Gadget *ogt_toolbar_gadgets(ogt_toolbar *tb, int first_gid);
/* The tool for a gadget id, or -1. */
int ogt_toolbar_index(const ogt_toolbar *tb, int gadget_id);
void ogt_toolbar_draw(ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, const char *bg_key);
void ogt_toolbar_draw_one(ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, int i, int down, const char *bg_key);
void ogt_toolbar_enable(ogt_toolbar *tb, int id, int enabled);
/* Shows a toggle button on or off (redraw it after). */
void ogt_toolbar_select(ogt_toolbar *tb, int id, int on);

/* The Buttons style (Open Apps Look and Feel): "icons+text", "icons" or
 * "text" (also BOTH, ICONS, TEXT as a ToolType gives them) as OGT_TB_*, or
 * -1 when it says none of those ("look", empty). */
int ogt_buttons_parse(const char *s);
/* The style for every Open app: the "buttons" line in ENV:OpenGadTools/Look
 * (OpenPrefs Look), or icons only when there is none. */
int ogt_buttons_style(void);

#endif

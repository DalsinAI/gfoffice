/* ogt_iconset: toolbar pictures from an icon set (docs/style/README.md):
 * one ILBM strip per size (24, 28, 32, 48) and mode (light, dark), in 32
 * colours, and in 8 for screens of fewer than 64. The strips are made by
 * tools/iconset/make_iconset.py from SVG sources and found in
 * PROGDIR:Images/Toolbar/, then in SYS:Prefs/Presets/Toolbar/.
 *
 * A set is opened when first drawn and kept with the drawing context: its
 * colours are the context's pens (or exact colours on RTG), and it goes
 * when the context is freed. When there is no strip, no such icon, or the
 * screen can't show it (OS 3.0 or older, four colours, the Classic theme),
 * ogt_iconset_draw() draws nothing and returns 0, and the caller draws its
 * vector icon (ogt_icons) instead.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OGT_ICONSET_H
#define OGT_ICONSET_H

#include "ogt_draw.h"

#define OGT_ICONSET_DEFAULT "Chrome"
#define OGT_ICONSET_SIZES 4               /* 24, 28, 32 and 48 pixels */

/* The set toolbars use, by name; NULL or "" for the vector icons only. */
void ogt_iconset_use(const char *name);
/* The size a set is drawn at for a wanted size: 24, 28, 32 or 48, the
 * nearest (the smaller when halfway). */
int ogt_iconset_size(int want);
/* The toolbar icon size the style guide asks for with a font (section 3.4):
 * 1.25 times its height, and 24 at least. A 13-point font wants 24, and a
 * 23-pixel one 29, which a set draws at 28. */
int ogt_iconset_want(int font_height);
/* Draws the icon called name, size x size, at x,y: the normal picture, or
 * the disabled one. 1 when drawn, 0 when not (draw a vector icon). */
int ogt_iconset_draw(ogt_ctx *c, struct RastPort *rp, const char *name, int size, int x, int y, int disabled);

#endif

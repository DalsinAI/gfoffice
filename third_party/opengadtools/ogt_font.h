/* ogt_font: the font an Open app draws in. The theme names one ("DejaVu
 * Sans" 12); until smooth text (DESIGN.md 2b) draws TrueType, the nearest
 * proportional Amiga font stands in for it, never the fixed Topaz.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_FONT_H
#define OGT_FONT_H

#include <graphics/text.h>
#include <intuition/screens.h>
#include "ogt_theme.h"

/* Opens the theme's font, else the first of these the system has: CGTriumvirate
 * (OS 3.x's outline sans, scaled), helvetica, the screen's own font. ta gets
 * what was opened (its name stays valid while the font is open). Close it
 * with CloseFont. NULL only when not even the screen's font opens. */
struct TextFont *ogt_open_font(const ogt_theme *t, struct Screen *scr, struct TextAttr *ta);

#endif

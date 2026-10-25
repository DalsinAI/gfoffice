# OpenGadTools (source copy)

OpenWrite builds against a source copy of the MIT OpenGadTools link library
from `DalsinAI/opengadtools`, main commit `d5f4dff` (6 October 2026).

The copy keeps OpenWrite's Amiga build self-contained. Changes belong in the
OpenGadTools repository first and are then refreshed here.

`ogt_font.c` and `ogt_openfont.h` are refreshed from DalsinAI/opengadtools
PR #71 (22 October 2026): fonts through openfont.library 1.2's
`OF_OpenTextFont`, never diskfont.library.

`ogt_status.c`, `ogt_ruler.c` and `ogt_draw.c` are refreshed from
DalsinAI/opengadtools PR #84 (25 October 2026): the status bar and the
ruler fill with `window`, and a key a theme doesn't name takes its role's
colour, not pen 1 (they were black: no theme has `panel`).

`ogt_toolbar.c`, `ogt_toolbar.h`, `ogt_draw.c` and `ogt_draw.h` are refreshed,
and `ogt_iconfile.c`, `ogt_iconfile.h`, `ogt_iconset.c` and `ogt_iconset.h`
added, from DalsinAI/opengadtools PR #86 (branch
`team5/chrome-iconset-20261025`, 25 October 2026). The toolbar shows pictures
from an icon set and draws the style guide's states. The toolbar's copy also
brings main's Buttons style (`ogt_buttons_parse`, `ogt_buttons_style`), which
these programs don't call yet.

`ogt_toolbar.c`, `ogt_toolbar.h`, `ogt_iconset.c` and `ogt_iconset.h` are
refreshed from DalsinAI/opengadtools PR #87 (branch
`team5/toolbar-size-20261025`, 25 October 2026). Toolbar icons are 1.25 times
the font's height, rounded to the set's 24, 28, 32 or 48: 28 with the lab's
23-pixel font, where twice the height drew 48. The buttons' padding is on the
style's 4-pixel grid.

`Images/Toolbar/` at the top of this repository holds the Chrome set's strips
(24, 28, 32 and 48 pixels, light and dark, 32 and 8 colours), made by #87's
`python3 tools/iconset/make_iconset.py`. `build-os3.sh` copies them next to the
programs, and OpenWrite and OpenCalc find them in `PROGDIR:Images/Toolbar/`.

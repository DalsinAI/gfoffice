# OpenGadTools (source copy)

OpenWrite builds against a source copy of the MIT OpenGadTools link library
from `DalsinAI/opengadtools`, main commit `d5f4dff` (6 October 2026).

The copy keeps OpenWrite's Amiga build self-contained. Changes belong in the
OpenGadTools repository first and are then refreshed here.

`ogt_font.c` and `ogt_openfont.h` are refreshed from DalsinAI/opengadtools
PR #71 (22 October 2026): fonts through openfont.library 1.2's
`OF_OpenTextFont`, never diskfont.library.

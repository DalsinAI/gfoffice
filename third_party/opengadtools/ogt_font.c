/* ogt_font: the theme's font, or the nearest proportional one.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "ogt_font.h"
#include "ogt_openfont.h"

static char name_buf[72];

/* One family at one size; a bitmap family is asked at its nearest size by openfont. */
static struct TextFont *try_font(struct Library *of, const char *file, int size, struct TextAttr *ta)
{
    struct TextFont *f;
    snprintf(name_buf, sizeof name_buf, "%s", file);
    ta->ta_Name = (STRPTR)name_buf;
    ta->ta_YSize = size;
    ta->ta_Style = FS_NORMAL;
    ta->ta_Flags = FPF_DISKFONT | FPF_DESIGNED;
    if ((f = ogt_openfont_font(of, ta))) return f;
    ta->ta_Flags = FPF_DISKFONT;                 /* outline fonts are scaled, not designed */
    if ((f = ogt_openfont_font(of, ta)) && !(f->tf_Flags & FPF_PROPORTIONAL)) { CloseFont(f); f = NULL; }
    return f;
}

struct TextFont *ogt_open_font(const ogt_theme *t, struct Screen *scr, struct TextAttr *ta)
{
    struct Library *of = ogt_openfont_open();
    struct TextFont *f = NULL;
    int size = t && t->font_size > 0 ? t->font_size : 12;
    if (scr && scr->Height < 400 && size > 11) size = 11;   /* a 640x256 screen: smaller */
    if (of) {
        if (t && t->font[0]) {
            char file[72], *d;
            const char *s;
            /* "DejaVu Sans" is installed as DejaVuSans.font or "DejaVu Sans.font" */
            for (s = t->font, d = file; *s && d < file + sizeof file - 6; s++) if (*s != ' ') *d++ = *s;
            strcpy(d, ".font");
            f = try_font(of, file, size, ta);
            if (!f) { snprintf(file, sizeof file, "%s.font", t->font); f = try_font(of, file, size, ta); }
        }
        if (!f) f = try_font(of, "CGTriumvirate.font", size, ta);
        if (!f) f = try_font(of, "helvetica.font", size <= 12 ? 11 : 13, ta);
        CloseLibrary(of);
    }
    if (!f && scr) {
        *ta = *scr->Font;
        f = OpenFont(ta);
    }
    return f;
}

/* ogt_iconset: toolbar pictures from an icon set (see ogt_iconset.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include <inline/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ogt_iconset.h"
#include "ogt_iconfile.h"
#include "ogt_toolbar.h"

/* cybergraphics.library: reading and writing true-colour pixels. */
static struct Library *CyberGfxBase;
#define RECTFMT_ARGB 2
#define CGX_READ(buf, mod, rp, x, y, w, h) LP10(0x78, ULONG, ReadPixelArray, APTR, (buf), a0, UWORD, 0, d0, UWORD, 0, d1, UWORD, (mod), d2, \
    struct RastPort *, (rp), a1, UWORD, (x), d3, UWORD, (y), d4, UWORD, (w), d5, UWORD, (h), d6, UBYTE, RECTFMT_ARGB, d7, , CyberGfxBase)
#define CGX_WRITE(buf, mod, rp, x, y, w, h) LP10(0x7e, ULONG, WritePixelArray, APTR, (buf), a0, UWORD, 0, d0, UWORD, 0, d1, UWORD, (mod), d2, \
    struct RastPort *, (rp), a1, UWORD, (x), d3, UWORD, (y), d4, UWORD, (w), d5, UWORD, (h), d6, UBYTE, RECTFMT_ARGB, d7, , CyberGfxBase)

static char set_name[32] = OGT_ICONSET_DEFAULT;

/* One strip, as drawn on one context. */
typedef struct strip {
    int tried, ok;
    int size, eight;
    ogt_iconfile f;
    LONG pen[2][32];            /* normal, disabled: pens, or OGT_DIRECT colours */
    int have_pens[2];
} strip;

/* What a context keeps (ogt_ctx.icons): a strip for each size, and the
 * buffers drawing needs. */
typedef struct sets {
    char name[32];
    strip s[OGT_ICONSET_SIZES];
    ULONG *argb;                /* 48 x 48 */
    UBYTE *chunky;              /* 48 x 48, rows padded to 16 */
    struct BitMap *tmpbm;
    int tmpdepth;
} sets;

static const int sizes[OGT_ICONSET_SIZES] = { 24, 28, 32, 48 };

void ogt_iconset_attach(void)
{
    static const ogt_toolbar_pictures calls = { ogt_iconset_want, ogt_iconset_size, ogt_iconset_draw };
    ogt_toolbar_pictures_use(&calls);
}

void ogt_iconset_use(const char *name)
{
    snprintf(set_name, sizeof set_name, "%s", name ? name : "");
}

int ogt_iconset_size(int want)
{
    int i, best = sizes[0];
    for (i = 1; i < OGT_ICONSET_SIZES; i++)
        if (abs(sizes[i] - want) < abs(best - want)) best = sizes[i];
    return best;
}

int ogt_iconset_want(int font_height)
{
    int want = (font_height * 5 + 2) / 4;              /* 1.25 times, rounded */
    return want > 24 ? want : 24;
}

static void sets_free(void *p)
{
    sets *ss = p;
    int i;
    if (!ss) return;
    for (i = 0; i < OGT_ICONSET_SIZES; i++) ogt_iconfile_free(&ss->s[i].f);
    if (ss->tmpbm) FreeBitMap(ss->tmpbm);
    free(ss->argb);
    free(ss->chunky);
    free(ss);
}

static int screen_depth(ogt_ctx *c)
{
    struct BitMap *bm = c->scr->RastPort.BitMap;
    if (((struct Library *)GfxBase)->lib_Version >= 39) return (int)GetBitMapAttr(bm, BMA_DEPTH);
    return bm->Depth;
}

static int load(strip *st, const char *set, int size, int mode, int eight)
{
    static const char *dirs[] = { "PROGDIR:Images/Toolbar/", "SYS:Prefs/Presets/Toolbar/" };
    char path[128], err[64];
    int d;
    for (d = 0; d < 2; d++) {
        BPTR fh;
        LONG len;
        UBYTE *buf;
        int ok;
        snprintf(path, sizeof path, "%s%s-%d-%s%s.ilbm", dirs[d], set, size, mode == OGT_DARK ? "dark" : "light", eight ? "-8" : "");
        if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) continue;
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len <= 0 || len > 512 * 1024 || !(buf = malloc((size_t)len))) { Close(fh); continue; }
        ok = Read(fh, buf, len) == len && ogt_iconfile_parse(&st->f, buf, (size_t)len, err, sizeof err);
        Close(fh);
        free(buf);
        if (ok && st->f.size == size) return 1;
        if (ok) ogt_iconfile_free(&st->f);
    }
    return 0;
}

static strip *get_strip(ogt_ctx *c, int size)
{
    sets *ss = c->icons;
    strip *st;
    int i, depth;
    if (!set_name[0] || c->theme->passthrough || ((struct Library *)GfxBase)->lib_Version < 39) return NULL;
    depth = screen_depth(c);
    if (depth < 3) return NULL;                                 /* four colours: the vector icons */
    for (i = 0; i < OGT_ICONSET_SIZES && sizes[i] != size; i++) ;
    if (i == OGT_ICONSET_SIZES) return NULL;
    if (ss && strcmp(ss->name, set_name)) {                     /* another set chosen since */
        sets_free(ss);
        c->icons = ss = NULL;
    }
    if (!ss) {
        if (!(ss = calloc(1, sizeof *ss))) return NULL;
        snprintf(ss->name, sizeof ss->name, "%s", set_name);
        c->icons = ss;
        c->icons_free = sets_free;
    }
    st = &ss->s[i];
    if (!st->tried) {
        st->tried = 1;
        st->eight = depth < 6 && !c->direct;
        st->size = size;
        st->ok = load(st, ss->name, size, c->mode, st->eight);
    }
    return st->ok ? st : NULL;
}

static void get_pens(ogt_ctx *c, strip *st, int disabled)
{
    int i;
    if (st->have_pens[disabled]) return;
    for (i = 1; i < st->f.colours; i++) {
        ogt_rgb rgb = disabled ? st->f.disabled[i] : st->f.normal[i];
        st->pen[disabled][i] = c->direct ? (LONG)(((ULONG)rgb.r << 16) | ((ULONG)rgb.g << 8) | rgb.b) : ogt_pen_rgb(c, rgb);
    }
    st->have_pens[disabled] = 1;
}

int ogt_iconset_draw(ogt_ctx *c, struct RastPort *rp, const char *name, int size, int x, int y, int disabled)
{
    strip *st;
    sets *ss;
    const UBYTE *src;
    int k, row, col;
    if (!name || !(st = get_strip(c, size))) return 0;
    if ((k = ogt_iconfile_find(&st->f, name)) < 0) return 0;
    ss = c->icons;
    disabled = disabled ? 1 : 0;
    get_pens(c, st, disabled);
    src = ogt_iconfile_pixels(&st->f, k);
    if (c->direct) {
        if (!CyberGfxBase && !(CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40))) return 0;
        if (!ss->argb && !(ss->argb = malloc(48 * 48 * sizeof(ULONG)))) return 0;
        CGX_READ(ss->argb, size * 4, rp, x, y, size, size);
        for (row = 0; row < size; row++) {
            const UBYTE *s = src + row * st->f.stride;
            ULONG *d = ss->argb + row * size;
            for (col = 0; col < size; col++)
                if (s[col]) d[col] = 0xff000000UL | (ULONG)st->pen[disabled][s[col]];
        }
        CGX_WRITE(ss->argb, size * 4, rp, x, y, size, size);
        return 1;
    } else {
        struct RastPort tmp;
        int depth = screen_depth(c), w16 = ((size + 15) >> 4) << 4;
        if (depth > 8) return 0;
        if (!ss->chunky && !(ss->chunky = malloc(64 * 48))) return 0;
        if (!ss->tmpbm || ss->tmpdepth != depth) {
            if (ss->tmpbm) FreeBitMap(ss->tmpbm);
            if (!(ss->tmpbm = AllocBitMap(64, 1, depth, BMF_CLEAR, NULL))) return 0;
            ss->tmpdepth = depth;
        }
        tmp = *rp;
        tmp.Layer = NULL;
        tmp.BitMap = ss->tmpbm;
        ReadPixelArray8(rp, x, y, x + size - 1, y + size - 1, ss->chunky, &tmp);
        for (row = 0; row < size; row++) {
            const UBYTE *s = src + row * st->f.stride;
            UBYTE *d = ss->chunky + row * w16;
            for (col = 0; col < size; col++)
                if (s[col]) d[col] = (UBYTE)st->pen[disabled][s[col]];
        }
        WritePixelArray8(rp, x, y, x + size - 1, y + size - 1, ss->chunky, &tmp);
        return 1;
    }
}

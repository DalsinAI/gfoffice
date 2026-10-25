/*
 * OpenWrite's text widths for the layout (ow_fontwidths.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ow_fontwidths.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define UNITS_PER_PIXEL 60           /* OpenLayout's units a pixel at 100% (15 twips) */

static const char *const serif_faces[] = { "CGTimes.font", "DejaVuSerif.font", "LiberationSerif.font", "times.font", NULL };
static const char *const mono_faces[] = { "courier.font", "DejaVuSansMono.font", "LiberationMono.font", NULL };
static const char *const sans_faces[] = { "CGTriumvirate.font", "DejaVuSans.font", "LiberationSans.font",
                                          "helvetica.font", NULL };

static int has(const char *name, const char *part)
{
    size_t i, j, n = strlen(name), p = strlen(part);
    for (i = 0; i + p <= n; ++i) {
        for (j = 0; j < p && tolower((unsigned char)name[i + j]) == tolower((unsigned char)part[j]); ++j)
            ;
        if (j == p) return 1;
    }
    return 0;
}

void ow_fw_init(ow_fontwidths *fw, const ow_fw_backend *be, const char *theme_face)
{
    memset(fw, 0, sizeof *fw);
    fw->be = *be;
    if (theme_face) snprintf(fw->theme_face, sizeof fw->theme_face, "%s", theme_face);
}

void ow_fw_reset(ow_fontwidths *fw)
{
    fw->nfaces = fw->next_face = 0;
    fw->ntables = fw->next_table = 0;
}

int ow_fw_pixels(long size_units)
{
    long px = (size_units + UNITS_PER_PIXEL / 2) / UNITS_PER_PIXEL;
    return px < 4 ? 4 : px > 128 ? 128 : (int)px;
}

static int opens(ow_fontwidths *fw, const char *file)
{
    int ys = 0, as = 0;
    return file && file[0] && fw->be.open(fw->be.user, file, OW_FW_PROBE, &ys, &as) != NULL;
}

const char *ow_fw_face(ow_fontwidths *fw, const char *file)
{
    const char *const *kind;
    const char *face = NULL;
    int i, slot;
    for (i = 0; i < fw->nfaces; ++i)
        if (!strcmp(fw->faces[i].file, file)) return fw->faces[i].face;
    kind = has(file, "courier") || has(file, "mono") || has(file, "lettergothic") ? mono_faces
         : has(file, "times") || has(file, "serif") ? serif_faces : sans_faces;
    if (opens(fw, file)) face = file;
    for (i = 0; !face && kind[i]; ++i)
        if (opens(fw, kind[i])) face = kind[i];
    if (!face && opens(fw, fw->theme_face)) face = fw->theme_face;
    slot = fw->nfaces < OW_FW_FACES ? fw->nfaces++ : fw->next_face;
    fw->next_face = (slot + 1) % OW_FW_FACES;
    snprintf(fw->faces[slot].file, sizeof fw->faces[slot].file, "%s", file);
    snprintf(fw->faces[slot].face, sizeof fw->faces[slot].face, "%s", face ? face : "");
    return fw->faces[slot].face;
}

static int scaled(int v, int px, int ysize)
{
    return (v * px + ysize / 2) / ysize;
}

const ow_fw_table *ow_fw_widths(ow_fontwidths *fw, const char *file, int bold, int px)
{
    const char *face = ow_fw_face(fw, file);
    ow_fw_table *t;
    void *f = NULL;
    int i, ys = 0, as = 0, slot;
    bold = bold != 0;
    for (i = 0; i < fw->ntables; ++i)
        if (fw->tables[i].px == px && fw->tables[i].bold == bold && !strcmp(fw->tables[i].face, face))
            return &fw->tables[i];
    slot = fw->ntables < OW_FW_TABLES ? fw->ntables++ : fw->next_table;
    fw->next_table = (slot + 1) % OW_FW_TABLES;
    t = &fw->tables[slot];
    memset(t, 0, sizeof *t);
    snprintf(t->face, sizeof t->face, "%s", face);
    t->bold = bold;
    t->px = px;
    t->from = OW_FW_FROM_FACE;
    if (face[0]) {
        f = fw->be.open(fw->be.user, face, px, &ys, &as);
        if (!f) f = fw->be.open(fw->be.user, face, OW_FW_PROBE, &ys, &as);   /* no such size: another, scaled */
    }
    if (!f && fw->be.fallback) {
        f = fw->be.fallback;
        ys = fw->be.fallback_ysize;
        as = fw->be.fallback_ascent;
        t->from = OW_FW_FROM_SCREEN;
    }
    if (ys <= 0) ys = px;
    if (t->from == OW_FW_FROM_FACE && ys != px) t->from = OW_FW_FROM_SCALED;
    if (as > ys) as = ys;
    for (i = 32; i < 256; ++i) {
        int a = f ? fw->be.advance(fw->be.user, f, (unsigned char)i, bold) : px / 2;
        if (a < 0) a = 0;
        t->w[i] = (unsigned short)(ys == px ? a : scaled(a, px, ys));
    }
    if (!f) {
        t->ascent = px * 4 / 5;
        t->descent = px - t->ascent;
    } else {
        int a = as > 0 ? as : ys * 4 / 5, d = ys - a;
        t->ascent = ys == px ? a : scaled(a, px, ys);
        t->descent = ys == px ? d : scaled(d, px, ys);
    }
    return t;
}

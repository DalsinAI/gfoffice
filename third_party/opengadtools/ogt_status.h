/* ogt_status: OpenGadTools' status bar: a strip along the bottom of a window
 * with up to eight fields, drawn in the theme ("Page 1 of 3 · Line 12 ·
 * 412 words · saved 20:14"). The program sets the fields' text; the bar
 * draws them, the last one at the right.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_STATUS_H
#define OGT_STATUS_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "ogt_draw.h"

#define OGT_STATUS_MAX 8
#define OGT_STATUS_LEN 64

typedef struct ogt_status {
    struct Window *win;
    ogt_ctx *ctx;
    int x, y, w, h;
    int n;
    char text[OGT_STATUS_MAX][OGT_STATUS_LEN];
} ogt_status;

void ogt_status_init(ogt_status *s, struct Window *win, ogt_ctx *ctx, int n);
void ogt_status_layout(ogt_status *s, int x, int y, int w, int h);
/* Sets a field's text (copied) and draws it when it changed. */
void ogt_status_set(ogt_status *s, int field, const char *text);
void ogt_status_draw(ogt_status *s);

#endif

/*
 * GFOpenBase's print preview window (app/ob_previewwin.h): a report's pages, laid out by
 * include/openbase_preview.h, drawn a whole page at a time on white paper. The layout's text is measured
 * with the window's font (scaled to each style's size), so its lines break near enough as the PDF's do for
 * a preview; text too small to read at the page's scale is shown as grey lines, as a page preview does.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "openbase_preview.h"
#include "ob_previewwin.h"

enum { P_PREV = 1, P_NEXT, P_PAGE, P_CLOSE, P_COUNT };

typedef struct {
    struct RastPort *rp;
    const ol_document *od;              /* the ops' document: their text is its nodes' */
    LONG text_pen, rule_pen, grey_pen;
    int min_text;                       /* pixels: smaller text is drawn as a grey line */
} painter;

static struct RastPort measure_rp;      /* the window's font, for the layout's widths */
static int measure_ysize = 8;

static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    static char buf[512];
    size_t at = 0, took, n;
    long px = 0;
    (void)userdata;
    /* a piece at a time, so a long run is measured whole */
    while (at < length && (took = ob_preview_latin1(utf8 + at, length - at, buf, sizeof buf, &n)) > 0) {
        if (n) px += TextLength(&measure_rp, (STRPTR)buf, (UWORD)n);
        at += took;
    }
    m->width = (ol_unit)(px * style->font_size / measure_ysize);
    m->ascent = style->font_size * 4 / 5;
    m->descent = style->font_size / 5;
    return 1;
}

/* A colour as one of the three pens: dark is the text's, mid the rules', light none (the paper). */
static LONG argb_pen(const painter *p, ULONG argb)
{
    int r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, b = argb & 0xff, lum = (r * 3 + g * 6 + b) / 10;
    if (lum < 96) return p->text_pen;
    if (lum < 224) return p->rule_pen;
    return -1;
}

static void paint_op(void *userdata, const ol_device_op *d)
{
    painter *p = (painter *)userdata;
    struct RastPort *rp = p->rp;
    switch (d->op->type) {
    case OL_OP_TEXT: {
        static char buf[512];
        const ol_text_op *t = &d->op->u.text;
        const char *s = ol_node_text(ol_node_by_id(p->od, t->node_id));
        if (!s || d->box.width < 1) break;
        if (d->font_pixels < p->min_text) {
            int hh = d->font_pixels / 2 > 0 ? d->font_pixels / 2 : 1;
            SetAPen(rp, p->grey_pen);
            RectFill(rp, d->box.x, d->baseline - hh, d->box.x + d->box.width - 1, d->baseline - 1);
        } else {
            struct TextExtent te;
            size_t n;
            ULONG fit;
            ob_preview_latin1(s + t->text_offset, t->text_length, buf, sizeof buf, &n);
            fit = n ? TextFit(rp, (STRPTR)buf, (UWORD)n, &te, NULL, 1, d->box.width + 2, 32767) : 0;
            if (!fit) break;
            SetAPen(rp, p->text_pen);
            Move(rp, d->box.x, d->baseline);
            Text(rp, (STRPTR)buf, fit);
        }
        break;
    }
    case OL_OP_FILL_RECT: {
        LONG pen = d->op->u.fill_rect.argb >> 24 ? argb_pen(p, d->op->u.fill_rect.argb) : -1;
        if (pen < 0 || d->box.width < 1 || d->box.height < 1) break;
        SetAPen(rp, pen);
        RectFill(rp, d->box.x, d->box.y, d->box.x + d->box.width - 1, d->box.y + d->box.height - 1);
        break;
    }
    case OL_OP_RULE: {
        LONG pen = argb_pen(p, d->op->u.rule.argb);
        SetAPen(rp, pen < 0 ? p->rule_pen : pen);
        Move(rp, d->x1, d->y1);
        Draw(rp, d->x2, d->y2);
        break;
    }
    case OL_OP_IMAGE:
        if (d->box.width < 2 || d->box.height < 2) break;
        SetAPen(rp, p->rule_pen);
        Move(rp, d->box.x, d->box.y);
        Draw(rp, d->box.x + d->box.width - 1, d->box.y);
        Draw(rp, d->box.x + d->box.width - 1, d->box.y + d->box.height - 1);
        Draw(rp, d->box.x, d->box.y + d->box.height - 1);
        Draw(rp, d->box.x, d->box.y);
        break;
    default: break;
    }
}

/* Page page of v on paper at x, y, w by h pixels: its header and footer, then its body. */
static void draw_page(struct RastPort *rp, const ob_preview *v, int page, int x, int y, int w, int h,
                      painter *p, LONG paper_pen, LONG edge_pen)
{
    const ow_layout *l = ob_preview_layout(v);
    const ol_page *pg = ow_layout_page(l);
    ol_pixel_rect clip;
    ol_scale s;
    char date[32] = "";
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    int k;
    SetAPen(rp, paper_pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
    SetAPen(rp, edge_pen);
    Move(rp, x - 1, y - 1);
    Draw(rp, x + w, y - 1);
    Draw(rp, x + w, y + h);
    Draw(rp, x - 1, y + h);
    Draw(rp, x - 1, y - 1);
    if (!pg || pg->width <= 0) return;
    ol_scale_fit(&s, pg->width, w);
    s.origin_x = x;
    s.origin_y = y;
    clip.x = x;
    clip.y = y;
    clip.width = w;
    clip.height = h;
    if (tmv) strftime(date, sizeof date, "%d/%m/%Y", tmv);
    for (k = 0; k < 2; ++k) {
        ol_unit sx, sy;
        ol_scale ss = s;
        ol_document *story = ow_layout_story(l, k ? OW_STORY_FOOTER : OW_STORY_HEADER, page, date, NULL, &sx, &sy);
        if (!story) continue;
        ss.origin_x = ol_scale_x(&s, sx);
        ss.origin_y = ol_scale_y(&s, sy);
        p->od = story;
        ol_draw_page(story, 0, &ss, &clip, paint_op, p);
        ol_document_free(story);
    }
    p->od = ow_layout_body(l);
    ol_draw_page(p->od, page, &s, &clip, paint_op, p);
}

int ob_preview_window(const ob_rwin_env *env, struct Window *over, const ob_report *r,
                      const char *const *params, char *err, size_t errsize)
{
    static char label[40];
    struct Gadget *eg = NULL, *g, *gad[P_COUNT];
    struct NewGadget ng;
    struct DrawInfo *dri;
    struct Window *pw;
    ob_preview *v;
    const ol_page *pg;
    painter paint;
    int done = 0, failed = 0, rebuild = 1, redraw = 1, page = 0, fh = env->fh, fw = env->fw, rowh = fh + 8;
    int w, h, paper_w, paper_h, bw;
    InitRastPort(&measure_rp);
    SetFont(&measure_rp, env->font);
    measure_ysize = env->font && env->font->tf_YSize ? env->font->tf_YSize : 8;
    if (!(v = ob_preview_new(env->proj, r, params, measure, NULL, err, errsize))) return 0;
    pg = ow_layout_page(ob_preview_layout(v));
    /* a whole page as tall as the screen gives, at its own shape */
    paper_h = env->scr->Height - env->scr->BarHeight - rowh - 60;
    if (paper_h < 120) paper_h = 120;
    paper_w = pg && pg->height > 0 ? (int)((long)paper_h * pg->width / pg->height) : paper_h * 7 / 10;
    w = paper_w + 24;
    if (w < fw * 44) w = fw * 44;
    if (w > env->scr->Width - 20) {
        w = env->scr->Width - 20;
        paper_w = w - 24;
        if (pg && pg->width > 0) paper_h = (int)((long)paper_w * pg->height / pg->width);
    }
    h = paper_h + rowh + 24;
    pw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Report Preview",
        WA_PubScreen, (ULONG)env->scr,
        WA_Left, over ? over->LeftEdge + 16 : 20, WA_Top, env->scr->BarHeight + 2,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY |
                  BUTTONIDCMP | TEXTIDCMP,
        TAG_DONE);
    if (!pw) {
        if (err && errsize) snprintf(err, errsize, "Not enough memory for the preview window");
        ob_preview_free(v);
        return 0;
    }
    SetFont(pw->RPort, env->font);
    dri = GetScreenDrawInfo(env->scr);
    paint.rp = pw->RPort;
    paint.text_pen = dri ? dri->dri_Pens[TEXTPEN] : 1;
    paint.rule_pen = dri ? dri->dri_Pens[SHADOWPEN] : 1;
    paint.grey_pen = dri ? dri->dri_Pens[SHADOWPEN] : 1;
    paint.min_text = env->font && env->font->tf_YSize > 4 ? env->font->tf_YSize * 2 / 3 : 6;
    bw = fw * 10 + 16;
    while (!done) {
        int iw = pw->Width - pw->BorderLeft - pw->BorderRight;
        int ih = pw->Height - pw->BorderTop - pw->BorderBottom;
        int px = pw->BorderLeft + (iw - paper_w) / 2, py = pw->BorderTop + 8;
        if (rebuild) {
            int l = pw->BorderLeft + 8, by = pw->BorderTop + ih - 6 - (fh + 6);
            if (eg) { RemoveGList(pw, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            if (!(g = CreateContext(&eg))) { failed = 1; break; }
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = env->vi;
            ng.ng_TextAttr = env->ta;
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = by;
            ng.ng_Height = fh + 6;
            ng.ng_Width = bw;
            ng.ng_LeftEdge = l;
            ng.ng_GadgetText = (UBYTE *)"< Previous";
            ng.ng_GadgetID = P_PREV;
            g = gad[P_PREV] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, page == 0, TAG_DONE);
            ng.ng_LeftEdge = l + bw + 8;
            ng.ng_GadgetText = (UBYTE *)"Next >";
            ng.ng_GadgetID = P_NEXT;
            g = gad[P_NEXT] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, page + 1 >= ob_preview_pages(v),
                                           TAG_DONE);
            ob_preview_label(v, page, label, sizeof label);
            ng.ng_LeftEdge = l + 2 * (bw + 8);
            ng.ng_Width = iw - 16 - 3 * (bw + 8) > fw * 8 ? iw - 16 - 3 * (bw + 8) : fw * 8;
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = P_PAGE;
            g = gad[P_PAGE] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)label, GTTX_Justification,
                                           GTJ_CENTER, TAG_DONE);
            ng.ng_LeftEdge = pw->BorderLeft + iw - 8 - bw;
            ng.ng_Width = bw;
            ng.ng_GadgetText = (UBYTE *)"Close";
            ng.ng_GadgetID = P_CLOSE;
            g = gad[P_CLOSE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) { failed = 1; break; }
            SetAPen(pw->RPort, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
            RectFill(pw->RPort, pw->BorderLeft, pw->BorderTop, pw->Width - pw->BorderRight - 1,
                     pw->Height - pw->BorderBottom - 1);
            AddGList(pw, eg, ~0, -1, NULL);
            RefreshGList(eg, pw, NULL, -1);
            GT_RefreshWindow(pw, NULL);
            rebuild = 0;
            redraw = 1;
        }
        if (redraw) {
            draw_page(pw->RPort, v, page, px, py, paper_w, paper_h, &paint, dri ? dri->dri_Pens[SHINEPEN] : 2,
                      dri ? dri->dri_Pens[SHADOWPEN] : 1);
            redraw = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(pw->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(pw->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                int to = page;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) {
                    GT_BeginRefresh(pw);
                    GT_EndRefresh(pw, TRUE);
                    redraw = 1;
                    break;
                }
                if (class == IDCMP_VANILLAKEY) {
                    if (code == 27) { done = 1; break; }
                    if (code == ' ' || code == 'n' || code == 'N') to = ob_preview_turn(v, page, 1);
                    else if (code == 8 || code == 'p' || code == 'P') to = ob_preview_turn(v, page, -1);
                } else if (class == IDCMP_RAWKEY) {
                    if (code == 0x4e || code == 0x4d) to = ob_preview_turn(v, page, 1);         /* right, down */
                    else if (code == 0x4f || code == 0x4c) to = ob_preview_turn(v, page, -1);   /* left, up */
                } else if (class == IDCMP_GADGETUP) {
                    switch (gg->GadgetID) {
                    case P_PREV: to = ob_preview_turn(v, page, -1); break;
                    case P_NEXT: to = ob_preview_turn(v, page, 1); break;
                    case P_CLOSE: done = 1; break;
                    default: break;
                    }
                }
                if (to != page) { page = to; rebuild = 1; }
            }
        }
    }
    if (eg) { RemoveGList(pw, eg, -1); FreeGadgets(eg); }
    if (dri) FreeScreenDrawInfo(env->scr, dri);
    CloseWindow(pw);
    ob_preview_free(v);
    if (over) ActivateWindow(over);
    if (failed) {
        if (err && errsize) snprintf(err, errsize, "Not enough memory for the preview window's gadgets");
        return 0;
    }
    return 1;
}

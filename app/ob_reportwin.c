/*
 * GFOpenBase's Report window (app/ob_reportwin.h; Design-OpenBase.md section 7).
 *
 * One report at a time is edited in it: its name, title, source (a table or a saved query), the field it
 * groups by (then up to two more levels), and its columns with a total under each. Bands... opens the
 * report's bands: report and page headers and footers, and each group level's header, footer and totals.
 * Preview... shows its pages as the PDF gets them (app/ob_previewwin.c). Keep puts it in the project
 * (oadb_reports, through ob_report_store), the list at the top opens a kept one, and Make Document... saves
 * it through libowf as the requester's file name says (.odt, .docx, .pdf, .txt; no extension: .odt).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openbase_report.h"
#include "ob_reportwin.h"
#include "ob_previewwin.h"

enum {
    R_KEPT = 1, R_NAME, R_TITLE, R_KIND, R_SOURCE, R_GROUP, R_SUB1, R_SUB2, R_COLS, R_ADD, R_REMOVE, R_FIELD, R_TOTAL, R_HEAD,
    R_PREVIEW, R_BANDS, R_KEEP, R_DELETE, R_MAKE, R_CLOSE, R_COUNT
};

/* The totals a column can have, in ob_total's order with OB_TOTAL_GROUP left out. */
static STRPTR r_total_labels[] = { (STRPTR)"No total", (STRPTR)"Sum", (STRPTR)"Count", (STRPTR)"Average",
                                   (STRPTR)"Min", (STRPTR)"Max", NULL };
static const ob_total r_totals[] = { OB_TOTAL_NONE, OB_TOTAL_SUM, OB_TOTAL_COUNT, OB_TOTAL_AVERAGE,
                                     OB_TOTAL_MIN, OB_TOTAL_MAX };
#define R_NTOTALS 6
static STRPTR r_kind_labels[] = { (STRPTR)"Table", (STRPTR)"Query", NULL };

static const ob_rwin_env *env;
static struct Window *rw;
static ob_report rr;                      /* the report being edited */
static int is_query;                      /* its source is rr.query, else rr.table */
static char source[OB_NAME_MAX];
static int csel = -1;                     /* the column chosen in the list, -1 none */
static char new_field[OB_NAME_MAX], new_head[OB_NAME_MAX];
static int new_total;                     /* index in r_totals */
static int dirty;                         /* changed since it was opened, kept or started */

static struct List kept_list, col_list;
static struct Node *kept_nodes;
static char (*kept_names)[OB_NAME_MAX];
static int nkept;
static struct Node col_nodes[OB_REPORT_COLUMNS];
static char col_text[OB_REPORT_COLUMNS][2 * OB_NAME_MAX + 16];

static void put_text(char *dst, size_t n, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (!n) return;
    if (len >= n) len = n - 1;
    if (len) memcpy(dst, src, len);
    dst[len] = 0;
}

static void say(const char *text)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"Report", (UBYTE *)"%s", (UBYTE *)"OK" };
    EasyRequest(rw, &es, NULL, (ULONG)text);
}

static int ask_yes(const char *text)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"Report", (UBYTE *)"%s",
                             (UBYTE *)"Yes|Cancel" };
    return EasyRequest(rw, &es, NULL, (ULONG)text) == 1;
}

static int total_index(ob_total t)
{
    int i;
    for (i = 0; i < R_NTOTALS; i++)
        if (r_totals[i] == t) return i;
    return 0;
}

static void free_kept(void)
{
    free(kept_nodes);
    free(kept_names);
    kept_nodes = NULL;
    kept_names = NULL;
    nkept = 0;
    NewList(&kept_list);
}

static void load_kept(void)
{
    int i, n;
    free_kept();
    n = ob_report_count(env->proj);
    if (n <= 0) return;
    kept_nodes = calloc((size_t)n, sizeof *kept_nodes);
    kept_names = calloc((size_t)n, sizeof *kept_names);
    if (!kept_nodes || !kept_names) { free_kept(); return; }
    for (i = 0; i < n; i++) {
        const char *s = ob_report_name(env->proj, i);
        if (!s) break;
        put_text(kept_names[i], sizeof kept_names[i], s);
        kept_nodes[i].ln_Name = (char *)kept_names[i];
        AddTail(&kept_list, &kept_nodes[i]);
    }
    nkept = i;
}

static void load_cols(void)
{
    int i;
    NewList(&col_list);
    for (i = 0; i < rr.ncolumns; i++) {
        const ob_rcolumn *c = &rr.columns[i];
        const char *t = ob_total_name(c->total);
        snprintf(col_text[i], sizeof col_text[i], "%s%s%s%s%s", c->field, c->heading[0] ? " as " : "",
                 c->heading, t[0] ? ": " : "", t);
        col_nodes[i].ln_Name = col_text[i];
        AddTail(&col_list, &col_nodes[i]);
    }
}

/* A new report: from the table or the query the main window shows. */
static void source_into_report(void);

static void start_new(void)
{
    ob_report_init(&rr, "");
    is_query = env->query && env->query[0] && !(env->table && env->table[0]);
    put_text(source, sizeof source, is_query ? env->query : env->table);
    csel = -1;
    new_field[0] = new_head[0] = 0;
    new_total = 0;
    source_into_report();   /* rr matches what the window shows, so nothing reads as changed */
    dirty = 0;
}

/* The report as the window shows it, its source set from the cycle and the string. */
static void source_into_report(void)
{
    rr.table[0] = rr.query[0] = 0;
    put_text(is_query ? rr.query : rr.table, sizeof rr.table, source);
}

static void read_strings(struct Gadget **gad)
{
    STRPTR str;
#define RSTR(id, dst) do { str = NULL; if (gad[id]) GT_GetGadgetAttrs(gad[id], rw, NULL, GTST_String, (ULONG)&str, TAG_DONE); \
    if (str) put_text((dst), sizeof(dst), (const char *)str); } while (0)
    RSTR(R_NAME, rr.name);
    RSTR(R_TITLE, rr.title);
    RSTR(R_SOURCE, source);
    RSTR(R_GROUP, rr.group);
    RSTR(R_SUB1, rr.subgroup[0]);
    RSTR(R_SUB2, rr.subgroup[1]);
    if (csel >= 0 && csel < rr.ncolumns) {
        RSTR(R_FIELD, rr.columns[csel].field);
        RSTR(R_HEAD, rr.columns[csel].heading);
    } else {
        RSTR(R_FIELD, new_field);
        RSTR(R_HEAD, new_head);
    }
#undef RSTR
    source_into_report();
}

static void open_kept(int i)
{
    char err[OB_ERR_MAX];
    static ob_report r;             /* 4.5 KB: not on the stack */
    if (i < 0 || i >= nkept) return;
    if (ob_report_load(env->proj, kept_names[i], &r, err, sizeof err)) { say(err); return; }
    rr = r;
    is_query = rr.query[0] != 0;
    put_text(source, sizeof source, is_query ? rr.query : rr.table);
    csel = -1;
    new_field[0] = new_head[0] = 0;
    new_total = 0;
    dirty = 0;
}

/* Whether the report's changes may go: yes when there are none or the user says so. */
static int may_drop(void)
{
    char msg[OB_NAME_MAX + 60];
    if (!dirty) return 1;
    snprintf(msg, sizeof msg, "Drop the changes to %s?", rr.name[0] ? rr.name : "this report");
    return ask_yes(msg);
}

/* A query's parameter values, asked one at a time: 1 with them in vals (and ptrs pointing at them),
 * 0 when the user cancels or the query can't be read; *n how many. */
static int ask_params(char (*vals)[OB_VALUE_MAX], const char **ptrs, int *n)
{
    static ob_query q;
    static char names[OB_QUERY_PARAMS][OB_VALUE_MAX];  /* off the stack */
    int i, k;
    *n = 0;
    if (!rr.query[0]) return 1;
    if (!ob_query_get(env->proj, rr.query, &q)) { say(ob_error(env->proj)); return 0; }
    k = ob_query_params(env->proj, &q, names, OB_QUERY_PARAMS);
    if (k < 0) { say(ob_error(env->proj)); return 0; }
    if (k > OB_QUERY_PARAMS) k = OB_QUERY_PARAMS;
    for (i = 0; i < k; i++) {
        vals[i][0] = 0;
        if (!env->ask_text || !env->ask_text(rr.query, names[i], vals[i], OB_VALUE_MAX)) return 0;
        ptrs[i] = vals[i];
    }
    *n = k;
    return 1;
}

/* A document's file name from the ASL requester, starting in the project's drawer: 0 when cancelled. */
static int ask_document(char *out, size_t size)
{
    struct FileRequester *fr;
    char dir[400] = "", file[108];
    int ok = 0;
    if (env->proj_path && env->proj_path[0]) {
        const char *f = (const char *)FilePart((STRPTR)env->proj_path);
        size_t n = (size_t)(f - env->proj_path);
        if (n < sizeof dir) { memcpy(dir, env->proj_path, n); dir[n] = 0; }
    }
    snprintf(file, sizeof file, "%s.odt", rr.name[0] ? rr.name : "Report");
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)rw, ASLFR_TitleText, (ULONG)"Save the report as (.odt .docx .pdf .txt)",
        ASLFR_DoSaveMode, TRUE, ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern, (ULONG)"#?.(odt|docx|pdf|txt)",
        ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file, TAG_DONE);
    if (!fr) return 0;
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0]) {
        put_text(out, size, (const char *)fr->fr_Drawer);
        ok = AddPart((STRPTR)out, fr->fr_File, size) != 0;
    }
    FreeAslRequest(fr);
    if (ok && !strchr((const char *)FilePart((STRPTR)out), '.') && strlen(out) + 5 < size) strcat(out, ".odt");
    return ok;
}

/* The report's pages in the preview window. */
static void preview(void)
{
    static char vals[OB_QUERY_PARAMS][OB_VALUE_MAX];
    const char *ptrs[OB_QUERY_PARAMS];
    char err[OB_ERR_MAX];
    int nparams;
    if (!rr.name[0]) put_text(rr.name, sizeof rr.name, "Report");
    if (ob_report_check(env->proj, &rr, err, sizeof err)) { say(err); return; }
    if (!ask_params(vals, ptrs, &nparams)) return;
    if (!ob_preview_window(env, rw, &rr, nparams ? ptrs : NULL, err, sizeof err)) say(err);
}

static void make_document(void)
{
    static char vals[OB_QUERY_PARAMS][OB_VALUE_MAX];
    const char *ptrs[OB_QUERY_PARAMS];
    char path[512], err[OB_ERR_MAX], msg[600];
    const char *ext;
    owf_report *notes;
    int nparams;
    if (!rr.name[0]) put_text(rr.name, sizeof rr.name, "Report");
    if (ob_report_check(env->proj, &rr, err, sizeof err)) { say(err); return; }
    if (!ask_params(vals, ptrs, &nparams)) return;
    if (!ask_document(path, sizeof path)) return;
    ext = strrchr((const char *)FilePart((STRPTR)path), '.');
    notes = owf_report_new();
    /* .txt is libowf's "text"; the others go by their extension */
    if (ob_report_save(env->proj, &rr, nparams ? ptrs : NULL, path, ext && !strcmp(ext, ".txt") ? "text" : NULL, notes, err,
                       sizeof err))
        say(err);
    else {
        snprintf(msg, sizeof msg, "Saved %s", (const char *)FilePart((STRPTR)path));
        say(msg);
    }
    owf_report_free(notes);
}

/* ---- The Bands window: the report's bands (ob_report_band) and, for a group level, its header, footer
 * and totals. It edits a copy; OK puts the copy back into the report. ---- */

enum { BW_HEAD = 1, BW_FOOT, BW_PHEAD, BW_PFOOT, BW_LEVEL, BW_TOTALS, BW_NEWPAGE, BW_GHEAD, BW_GFOOT, BW_HELP1,
       BW_HELP2, BW_OK, BW_CANCEL, BW_COUNT };

static STRPTR bw_level_labels[] = { (STRPTR)"Group 1", (STRPTR)"Group 2", (STRPTR)"Group 3", NULL };

/* The strings into d: the four report and page bands, and level L's group bands. */
static void bw_read(struct Window *bw, struct Gadget **gad, ob_report *d, int L)
{
    static const struct { int id; ob_band b; int group; } map[] = {
        { BW_HEAD, OB_BAND_HEAD, 0 }, { BW_FOOT, OB_BAND_FOOT, 0 }, { BW_PHEAD, OB_BAND_PAGE_HEAD, 0 },
        { BW_PFOOT, OB_BAND_PAGE_FOOT, 0 }, { BW_GHEAD, OB_BAND_GROUP_HEAD, 1 }, { BW_GFOOT, OB_BAND_GROUP_FOOT, 1 }
    };
    size_t size;
    unsigned i;
    for (i = 0; i < sizeof map / sizeof map[0]; i++) {
        STRPTR str = NULL;
        char *t = ob_report_band(d, map[i].b, map[i].group ? L : 0, &size);
        if (!t || !gad[map[i].id]) continue;
        GT_GetGadgetAttrs(gad[map[i].id], bw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
        if (str) put_text(t, size, (const char *)str);
    }
}

/* 1 when the report's bands changed. */
static int bands_window(void)
{
    static ob_report d;                 /* the copy (4.5 KB: off the stack) */
    static char help1[100], help2[100];
    struct Window *bw;
    struct Gadget *eg = NULL, *g, *gad[BW_COUNT];
    struct NewGadget ng;
    struct DrawInfo *dri;
    int done = 0, ok = 0, rebuild = 1, L = 0, fh = env->fh, fw = env->fw, rowh = fh + 8, lw = fw * 14, l, t, w, h;
    d = rr;
    snprintf(help1, sizeof help1, "[Title] [Date] anywhere; [Page] [Pages] on a page; [Count] in a footer");
    snprintf(help2, sizeof help2, "[Group] [Value] and any field in a group's bands; [[ for a [");
    w = fw * 72;
    h = rowh * 11 + 16;
    if (w > env->scr->Width - 20) w = env->scr->Width - 20;
    if (h > env->scr->Height - 40) h = env->scr->Height - 40;
    bw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Report Bands",
        WA_PubScreen, (ULONG)env->scr,
        WA_Left, rw->LeftEdge + 16, WA_Top, rw->TopEdge + 16,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | CYCLEIDCMP |
                  STRINGIDCMP | BUTTONIDCMP | CHECKBOXIDCMP | TEXTIDCMP,
        TAG_DONE);
    if (!bw) { say("Not enough memory for the Bands window"); return 0; }
    SetFont(bw->RPort, env->font);
    dri = GetScreenDrawInfo(env->scr);
    l = bw->BorderLeft + 8;
    t = bw->BorderTop + 6;
    while (!done) {
        if (rebuild) {
            int iw = bw->Width - bw->BorderLeft - bw->BorderRight - 16, y = t, cw = fw * 10 + 24;
            if (eg) { RemoveGList(bw, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = env->vi;
            ng.ng_TextAttr = env->ta;
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_Width = iw - lw;
            ng.ng_Height = fh + 6;
#define BW_STRING(id, label, text) do { ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)(label); ng.ng_GadgetID = (id); \
    g = gad[id] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(text), GTST_MaxChars, OB_BAND_MAX - 1, \
                               TAG_DONE); y += rowh; } while (0)
            BW_STRING(BW_HEAD, "Report header", d.head);
            BW_STRING(BW_FOOT, "Report footer", d.foot);
            BW_STRING(BW_PHEAD, "Page header", d.page_head);
            BW_STRING(BW_PFOOT, "Page footer", d.page_foot);
            y += 4;
            ng.ng_TopEdge = y;
            ng.ng_Width = cw;
            ng.ng_GadgetText = (UBYTE *)"Bands of";
            ng.ng_GadgetID = BW_LEVEL;
            g = gad[BW_LEVEL] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)bw_level_labels,
                                             GTCY_Active, (ULONG)L, TAG_DONE);
            ng.ng_LeftEdge = l + lw + cw + 8;
            ng.ng_Flags = PLACETEXT_RIGHT;
            ng.ng_Width = fh + 10;
            ng.ng_GadgetText = (UBYTE *)"Totals";
            ng.ng_GadgetID = BW_TOTALS;
            g = gad[BW_TOTALS] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)!(d.no_totals & (1 << L)),
                                              GTCB_Scaled, TRUE, TAG_DONE);
            ng.ng_LeftEdge = l + lw + cw + 8 + fh + 10 + fw * 8;
            ng.ng_GadgetText = (UBYTE *)"New page for each group 1";
            ng.ng_GadgetID = BW_NEWPAGE;
            g = gad[BW_NEWPAGE] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)(d.new_page != 0),
                                               GTCB_Scaled, TRUE, TAG_DONE);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_Width = iw - lw;
            y += rowh;
            BW_STRING(BW_GHEAD, "Group header", d.group_head[L]);
            BW_STRING(BW_GFOOT, "Group footer", d.group_foot[L]);
#undef BW_STRING
            /* what the bands may name */
            ng.ng_LeftEdge = l;
            ng.ng_Width = iw;
            ng.ng_TopEdge = y + 4;
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = BW_HELP1;
            g = gad[BW_HELP1] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)help1, TAG_DONE);
            ng.ng_TopEdge = y + 4 + rowh - 4;
            ng.ng_GadgetID = BW_HELP2;
            g = gad[BW_HELP2] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)help2, TAG_DONE);
            /* OK and Cancel */
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = bw->Height - bw->BorderBottom - 6 - (fh + 6);
            ng.ng_Width = fw * 10 + 16;
            ng.ng_LeftEdge = l;
            ng.ng_GadgetText = (UBYTE *)"OK";
            ng.ng_GadgetID = BW_OK;
            g = gad[BW_OK] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = l + iw - ng.ng_Width;
            ng.ng_GadgetText = (UBYTE *)"Cancel";
            ng.ng_GadgetID = BW_CANCEL;
            g = gad[BW_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(bw->RPort, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
            RectFill(bw->RPort, bw->BorderLeft, bw->BorderTop, bw->Width - bw->BorderRight - 1,
                     bw->Height - bw->BorderBottom - 1);
            AddGList(bw, eg, ~0, -1, NULL);
            RefreshGList(eg, bw, NULL, -1);
            GT_RefreshWindow(bw, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(bw->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(bw->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(bw); GT_EndRefresh(bw, TRUE); continue; }
                if (class != IDCMP_GADGETUP) continue;
                switch (gg->GadgetID) {
                case BW_LEVEL:
                    bw_read(bw, gad, &d, L);
                    if (code < OB_REPORT_LEVELS) L = code;
                    rebuild = 1;
                    break;
                case BW_TOTALS:
                    if (gg->Flags & GFLG_SELECTED) d.no_totals &= ~(1 << L);
                    else d.no_totals |= 1 << L;
                    break;
                case BW_NEWPAGE: d.new_page = (gg->Flags & GFLG_SELECTED) != 0; break;
                case BW_OK:
                    bw_read(bw, gad, &d, L);
                    ok = 1;
                    done = 1;
                    break;
                case BW_CANCEL: done = 1; break;
                default: break;
                }
            }
        }
    }
    if (eg) { RemoveGList(bw, eg, -1); FreeGadgets(eg); }
    if (dri) FreeScreenDrawInfo(env->scr, dri);
    CloseWindow(bw);
    ActivateWindow(rw);
    if (!ok || !memcmp(&d, &rr, sizeof rr)) return 0;
    rr = d;
    return 1;
}


void ob_report_window(const ob_rwin_env *e)
{
    struct Gadget *eg = NULL, *g, *gad[R_COUNT];
    struct NewGadget ng;
    struct DrawInfo *dri;
    int done = 0, rebuild = 1, fh, fw, rowh, l, t, w, h, lw, bw;
    env = e;
    if (!env || !env->proj) return;
    fh = env->fh;
    fw = env->fw;
    rowh = fh + 8;
    lw = fw * 9;
    bw = fw * 16 + 16;
    NewList(&kept_list);
    NewList(&col_list);
    start_new();
    w = fw * 64;
    h = rowh * 15 + 16;
    if (w > env->scr->Width - 20) w = env->scr->Width - 20;
    if (h > env->scr->Height - 40) h = env->scr->Height - 40;
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Report",
        WA_PubScreen, (ULONG)env->scr,
        WA_Left, env->parent ? env->parent->LeftEdge + 24 : 20, WA_Top, env->parent ? env->parent->TopEdge + 20 : 20,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  LISTVIEWIDCMP | CYCLEIDCMP | STRINGIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!rw) return;
    SetFont(rw->RPort, env->font);
    dri = GetScreenDrawInfo(env->scr);
    l = rw->BorderLeft + 8;
    t = rw->BorderTop + 6;
    while (!done) {
        if (rebuild) {
            int iw = rw->Width - rw->BorderLeft - rw->BorderRight - 16, y, half = (iw - lw) / 2 - 4;
            const ob_rcolumn *c = csel >= 0 && csel < rr.ncolumns ? &rr.columns[csel] : NULL;
            if (eg) { RemoveGList(rw, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            load_kept();
            load_cols();
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = env->vi;
            ng.ng_TextAttr = env->ta;
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_TopEdge = y = t;
            ng.ng_Width = iw - lw;
            ng.ng_Height = rowh * 3;
            ng.ng_GadgetText = (UBYTE *)"Kept";
            ng.ng_GadgetID = R_KEPT;
            g = gad[R_KEPT] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&kept_list, TAG_DONE);
            ng.ng_Height = fh + 6;
            ng.ng_TopEdge = y += rowh * 3 + 4;
            ng.ng_GadgetText = (UBYTE *)"Name";
            ng.ng_GadgetID = R_NAME;
            g = gad[R_NAME] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)rr.name,
                                           GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_GadgetText = (UBYTE *)"Title";
            ng.ng_GadgetID = R_TITLE;
            g = gad[R_TITLE] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)rr.title,
                                            GTST_MaxChars, OB_VALUE_MAX - 1, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_Width = fw * 8 + 24;
            ng.ng_GadgetText = (UBYTE *)"From";
            ng.ng_GadgetID = R_KIND;
            g = gad[R_KIND] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)r_kind_labels,
                                           GTCY_Active, (ULONG)is_query, TAG_DONE);
            ng.ng_LeftEdge = l + lw + fw * 8 + 32;
            ng.ng_Width = iw - lw - fw * 8 - 32;
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = R_SOURCE;
            g = gad[R_SOURCE] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)source,
                                             GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
            ng.ng_LeftEdge = l + lw;
            ng.ng_Width = iw - lw;
            ng.ng_TopEdge = y += rowh;
            {
                /* Group by [ ] then [ ] then [ ]: three levels */
                int tw = fw * 5 + 8, third = (iw - lw - 2 * tw) / 3;
                ng.ng_Width = third;
                ng.ng_GadgetText = (UBYTE *)"Group by";
                ng.ng_GadgetID = R_GROUP;
                g = gad[R_GROUP] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)rr.group,
                                                GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
                ng.ng_LeftEdge = l + lw + third + tw;
                ng.ng_GadgetText = (UBYTE *)"then";
                ng.ng_GadgetID = R_SUB1;
                g = gad[R_SUB1] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)rr.subgroup[0],
                                               GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
                ng.ng_LeftEdge = l + lw + 2 * (third + tw);
                ng.ng_GadgetID = R_SUB2;
                g = gad[R_SUB2] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)rr.subgroup[1],
                                               GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
                ng.ng_LeftEdge = l + lw;
                ng.ng_Width = iw - lw;
            }
            /* the columns, and adding or taking one out */
            ng.ng_TopEdge = y += rowh;
            ng.ng_Width = half;
            ng.ng_Height = rowh * 4;
            ng.ng_GadgetText = (UBYTE *)"Columns";
            ng.ng_GadgetID = R_COLS;
            g = gad[R_COLS] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&col_list, GTLV_ShowSelected, 0,
                                           GTLV_Selected, csel >= 0 ? (ULONG)csel : ~0UL, TAG_DONE);
            ng.ng_LeftEdge = l + lw + half + 8;
            ng.ng_Height = fh + 6;
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_GadgetText = (UBYTE *)"Add Column";
            ng.ng_GadgetID = R_ADD;
            g = gad[R_ADD] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, rr.ncolumns >= OB_REPORT_COLUMNS, TAG_DONE);
            ng.ng_TopEdge = y + rowh;
            ng.ng_GadgetText = (UBYTE *)"Take Column Out";
            ng.ng_GadgetID = R_REMOVE;
            g = gad[R_REMOVE] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, c == NULL, TAG_DONE);
            ng.ng_TopEdge = y + 2 * rowh;
            ng.ng_GadgetText = (UBYTE *)"Preview...";
            ng.ng_GadgetID = R_PREVIEW;
            g = gad[R_PREVIEW] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_TopEdge = y + 3 * rowh;
            ng.ng_GadgetText = (UBYTE *)"Bands...";
            ng.ng_GadgetID = R_BANDS;
            g = gad[R_BANDS] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            /* the chosen column, or the one Add Column adds */
            ng.ng_TopEdge = y += rowh * 4 + 4;
            ng.ng_Width = half;
            ng.ng_GadgetText = (UBYTE *)"Field";
            ng.ng_GadgetID = R_FIELD;
            g = gad[R_FIELD] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(c ? c->field : new_field),
                                            GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
            ng.ng_LeftEdge = l + lw + half + 8;
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = R_TOTAL;
            g = gad[R_TOTAL] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)r_total_labels,
                                            GTCY_Active, (ULONG)(c ? total_index(c->total) : new_total), TAG_DONE);
            ng.ng_LeftEdge = l + lw;
            ng.ng_Width = iw - lw;
            ng.ng_TopEdge = y += rowh;
            ng.ng_GadgetText = (UBYTE *)"Heading";
            ng.ng_GadgetID = R_HEAD;
            g = gad[R_HEAD] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(c ? c->heading : new_head),
                                           GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
            /* the buttons along the bottom */
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = rw->Height - rw->BorderBottom - 6 - (fh + 6);
            ng.ng_Width = (iw - 3 * 8) / 4 < bw ? (iw - 3 * 8) / 4 : bw;
            ng.ng_LeftEdge = l;
            ng.ng_GadgetText = (UBYTE *)"Keep";
            ng.ng_GadgetID = R_KEEP;
            g = gad[R_KEEP] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = l + (iw - ng.ng_Width) / 3;
            ng.ng_GadgetText = (UBYTE *)"Delete Kept";
            ng.ng_GadgetID = R_DELETE;
            g = gad[R_DELETE] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, nkept == 0, TAG_DONE);
            ng.ng_LeftEdge = l + 2 * (iw - ng.ng_Width) / 3;
            ng.ng_GadgetText = (UBYTE *)"Make Document...";
            ng.ng_GadgetID = R_MAKE;
            g = gad[R_MAKE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = l + iw - ng.ng_Width;
            ng.ng_GadgetText = (UBYTE *)"Close";
            ng.ng_GadgetID = R_CLOSE;
            g = gad[R_CLOSE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(rw->RPort, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
            RectFill(rw->RPort, rw->BorderLeft, rw->BorderTop, rw->Width - rw->BorderRight - 1,
                     rw->Height - rw->BorderBottom - 1);
            AddGList(rw, eg, ~0, -1, NULL);
            RefreshGList(eg, rw, NULL, -1);
            GT_RefreshWindow(rw, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(rw->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(rw->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                char err[OB_ERR_MAX], msg[OB_NAME_MAX + 40];
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) {
                    if (gad[R_NAME]) read_strings(gad);
                    done = may_drop();
                    break;
                }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); continue; }
                if (class != IDCMP_GADGETUP && class != IDCMP_GADGETDOWN) continue;
                {
                    /* edits that change the report (not the draft column's strings) */
                    static ob_report before;     /* 4.5 KB: not on the stack */
                    before = rr;
                    read_strings(gad);
                    if (memcmp(&before, &rr, sizeof rr)) dirty = 1;
                }
                switch (gg->GadgetID) {
                case R_NAME: case R_TITLE: case R_SOURCE: case R_GROUP: case R_SUB1: case R_SUB2: break;
                case R_BANDS: if (bands_window()) dirty = 1; rebuild = 1; break;
                case R_FIELD: case R_HEAD: if (csel >= 0) rebuild = 1; break;
                case R_KEPT:
                    if (may_drop()) open_kept((int)code);
                    rebuild = 1;
                    break;
                case R_KIND: is_query = code == 1; source_into_report(); dirty = 1; break;
                case R_COLS: csel = (int)code < rr.ncolumns ? (int)code : -1; rebuild = 1; break;
                case R_TOTAL:
                    if (code < R_NTOTALS) {
                        if (csel >= 0 && csel < rr.ncolumns) { rr.columns[csel].total = r_totals[code]; dirty = 1; }
                        else new_total = code;
                        rebuild = csel >= 0;
                    }
                    break;
                case R_ADD:
                    if (csel >= 0) {            /* a column was chosen: Add starts a fresh one */
                        csel = -1;
                        new_field[0] = new_head[0] = 0;
                        new_total = 0;
                    } else if (!new_field[0]) {
                        say("Type the field the column shows first");
                    } else {
                        ob_rcolumn *nc = ob_report_add_column(&rr, new_field, r_totals[new_total]);
                        if (!nc) say("A report has at most 16 columns");
                        else {
                            put_text(nc->heading, sizeof nc->heading, new_head);
                            dirty = 1;
                            new_field[0] = new_head[0] = 0;
                            new_total = 0;
                        }
                    }
                    rebuild = 1;
                    break;
                case R_REMOVE:
                    if (csel >= 0 && csel < rr.ncolumns) {
                        memmove(&rr.columns[csel], &rr.columns[csel + 1],
                                sizeof rr.columns[0] * (size_t)(rr.ncolumns - csel - 1));
                        rr.ncolumns--;
                        memset(&rr.columns[rr.ncolumns], 0, sizeof rr.columns[0]);
                        csel = -1;
                        dirty = 1;
                        rebuild = 1;
                    }
                    break;
                case R_KEEP:
                    if (!rr.name[0]) say("Give the report a name first");
                    else if (ob_report_store(env->proj, &rr, err, sizeof err)) say(err);
                    else {
                        SetWindowTitles(rw, (UBYTE *)"Report (kept)", (UBYTE *)~0);
                        dirty = 0;
                        rebuild = 1;
                    }
                    break;
                case R_DELETE:
                    if (rr.name[0]) {
                        snprintf(msg, sizeof msg, "Delete the kept report %s?", rr.name);
                        if (ask_yes(msg)) {
                            if (!ob_report_delete(env->proj, rr.name)) say(ob_error(env->proj));
                            else { start_new(); rebuild = 1; }
                        }
                    } else {
                        say("Open the kept report to delete first");
                    }
                    break;
                case R_MAKE: make_document(); break;
                case R_PREVIEW: preview(); break;
                case R_CLOSE: done = may_drop(); break;
                }
            }
        }
    }
    if (eg) { RemoveGList(rw, eg, -1); FreeGadgets(eg); }
    if (dri) FreeScreenDrawInfo(env->scr, dri);
    CloseWindow(rw);
    rw = NULL;
    free_kept();
    if (env->parent) ActivateWindow(env->parent);
}

/*
 * GFOpenBase's Report window (app/ob_reportwin.h; Design-OpenBase.md section 7).
 *
 * One report at a time is edited in it: its name, title, source (a table or a saved query), the field it
 * groups by, and its columns with a total under each. Keep puts it in the project (oadb_reports, through
 * ob_report_store), the list at the top opens a kept one, and Make Document... saves it through libowf as
 * the requester's file name says (.odt, .docx, .pdf, .txt; no extension: .odt).
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

enum {
    R_KEPT = 1, R_NAME, R_TITLE, R_KIND, R_SOURCE, R_GROUP, R_COLS, R_ADD, R_REMOVE, R_FIELD, R_TOTAL, R_HEAD,
    R_KEEP, R_DELETE, R_MAKE, R_CLOSE, R_COUNT
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
static void start_new(void)
{
    ob_report_init(&rr, "");
    is_query = env->query && env->query[0] && !(env->table && env->table[0]);
    put_text(source, sizeof source, is_query ? env->query : env->table);
    csel = -1;
    new_field[0] = new_head[0] = 0;
    new_total = 0;
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
    ob_report r;
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
            ng.ng_GadgetText = (UBYTE *)"Group by";
            ng.ng_GadgetID = R_GROUP;
            g = gad[R_GROUP] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)rr.group,
                                            GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
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
                    ob_report before = rr;
                    read_strings(gad);
                    if (memcmp(&before, &rr, sizeof rr)) dirty = 1;
                }
                switch (gg->GadgetID) {
                case R_NAME: case R_TITLE: case R_SOURCE: case R_GROUP: break;
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

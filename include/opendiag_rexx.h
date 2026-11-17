/*
 * opendiag_rexx.h: GFOpenDiag's ARexx commands (src/diag/odg_rexx.c) for its GFOPENDIAG port. The app answers
 * VERSION, ACTIVATE, DEACTIVATE, FAULT, PRINT, QUIT and the canvas's ZOOM, FIT, TOOL, SELECT, GETSELECTION and SHAPE
 * itself and hands every other command here. Arguments as a
 * ReadArgs template (quotes, KEYWORD=value or KEYWORD value, /S switches); places and sizes are page pixels and
 * may have decimals ("12.5"); cells are named by their ids. Return codes as every Open app's port: 0 done, 5 not
 * possible now, 10 an error, 20 an unknown command or out of memory; on any but 0, LASTERROR gives the reason. No
 * requester is ever shown. Plain C99, no Amiga headers: tested on the host (tests/diag/test_odg_rexx.c), and run in
 * the lab by diag.rexx.
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, step D4.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_REXX_H
#define OPENDIAG_REXX_H

#include "opendiag_core.h"
#include "opendiag_clip.h"

#define ODG_RX_OK 0
#define ODG_RX_NOW 5
#define ODG_RX_ERROR 10
#define ODG_RX_FAIL 20

/* The page drawn as a picture, for EXPORT (and SAVE/SAVEAS) to PNG and IFF ILBM: the app paints box (x, y, w, h,
 * hundredths of a page pixel: what the page holds with ODG_PICTURE_MARGIN round it) at 100% into width by height
 * pixels, with no grid and no selection, and returns the rows as R, G, B bytes, width * 3 apart, owf_malloc'd (the
 * caller frees them with owf_free, which on owfdiag.library is the library's); NULL when it can't (out of memory, or no screen to draw with). */
typedef unsigned char *(*odg_picture_fn)(void *user, const owf_dnum box[4], int width, int height);
#define ODG_PICTURE_MARGIN 10            /* page pixels round what the page holds, as the PDF writer's */
#define ODG_PICTURE_MAX 4096             /* a picture's width or height at most, in pixels */

typedef struct {
    owf_diagram d;                       /* the diagram, replaced by NEW and OPEN (the app redraws when it changes) */
    odg_doc doc;                         /* editing d's page doc.page */
    char file[256];                      /* the diagram's file, "" when it has none */
    char lasterror[256];
    int changes;                         /* goes up with every command that changed what is drawn */
    odg_clip clip;                       /* COPY and CUT's cells for PASTE (and the Edit menu's); kept across NEW
                                          * and OPEN, so cells go from one diagram to another */
    odg_picture_fn picture;              /* the app's page drawer for PNG and ILBM; NULL: they are refused */
    void *picture_user;
} odg_rexx;

/* A new empty diagram (one page) to command. OWF_OK or OWF_ERR_MEMORY. */
int odg_rexx_init(odg_rexx *rx);
void odg_rexx_free(odg_rexx *rx);
/* A command, its answer into result (cut to size, "" when it has none). */
int odg_rexx_command(odg_rexx *rx, const char *command, char *result, size_t size);
/* The file a name's extension asks for: ODG_FILE_SVG for .svg, ODG_FILE_PNG for .png (the page drawn, with the
 * diagram inside, so it opens again), ODG_FILE_PDF for .pdf (every page; EXPORT only, as a PDF doesn't open
 * again), ODG_FILE_ILBM for .iff or .ilbm (the page drawn; EXPORT only, as an ILBM holds no diagram), ODG_FILE_EPS
 * for .eps, .epsf or .ps (the page; EXPORT only, as a PDF), ODG_FILE_MERMAID for .mmd or .mermaid and ODG_FILE_DOT
 * for .dot or .gv, ODG_FILE_GRAPHML for .graphml (the page as a graph to lay out again; EXPORT only),
 * ODG_FILE_EXCALIDRAW for .excalidraw (the page as an Excalidraw scene; EXPORT only), else ODG_FILE_DRAWIO. */
enum { ODG_FILE_DRAWIO, ODG_FILE_SVG, ODG_FILE_PNG, ODG_FILE_PDF, ODG_FILE_ILBM, ODG_FILE_EPS, ODG_FILE_MERMAID,
       ODG_FILE_DOT, ODG_FILE_GRAPHML, ODG_FILE_EXCALIDRAW };
int odg_rexx_format(const char *file);

/* On owfdiag.library (OWF_USE_DIAG_LIBRARY): its calls, once every header that declares them is complete. */
#define OPENDIAG_REXX_H_DONE
#if defined(OWF_USE_DIAG_LIBRARY) && !defined(OWF_LIBRARY_BUILD)
#include "opendiag_proto.h"
#endif

#endif

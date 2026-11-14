/*
 * opendiag_rexx.h: GFOpenDiag's ARexx commands (src/diag/odg_rexx.c) for its GFOPENDIAG port. The app answers
 * VERSION, ACTIVATE, DEACTIVATE, FAULT, PRINT and QUIT itself and hands every other command here. Arguments as a
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

#define ODG_RX_OK 0
#define ODG_RX_NOW 5
#define ODG_RX_ERROR 10
#define ODG_RX_FAIL 20

typedef struct {
    owf_diagram d;                       /* the diagram, replaced by NEW and OPEN (the app redraws when it changes) */
    odg_doc doc;                         /* editing d's page doc.page */
    char file[256];                      /* the diagram's file, "" when it has none */
    char lasterror[256];
    int changes;                         /* goes up with every command that changed what is drawn */
} odg_rexx;

/* A new empty diagram (one page) to command. OWF_OK or OWF_ERR_MEMORY. */
int odg_rexx_init(odg_rexx *rx);
void odg_rexx_free(odg_rexx *rx);
/* A command, its answer into result (cut to size, "" when it has none). */
int odg_rexx_command(odg_rexx *rx, const char *command, char *result, size_t size);
/* The file a name's extension asks for: ODG_FILE_SVG for .svg, ODG_FILE_PNG for .png, else ODG_FILE_DRAWIO. */
enum { ODG_FILE_DRAWIO, ODG_FILE_SVG, ODG_FILE_PNG };
int odg_rexx_format(const char *file);

#endif

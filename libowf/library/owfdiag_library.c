/*
 * owfdiag.library 1.0, GFOpenDiag's part of libowf: the diagram model and
 * its formats (libowf/diagram: draw.io, SVG, PNG with the diagram in it,
 * PDF, EPS, outlines, Mermaid, DOT, GraphML, Excalidraw) and GFOpenDiag's
 * editing core (src/diag: undo, shapes and edges, layers, the clipboard,
 * order and groups, align, mind maps, the style panel, the stencils and
 * the ARexx commands). GFOpenDiag's window and drawing stay in the program.
 *
 * Its calls are owfdiag_lib.sfd's (owfdiag_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h. The first 17 calls
 * are the same in all of GFOffice's libraries (docs/suite/owf-libraries.md).
 * owf_struct_size answers the diagram structures' sizes too (OWF_DSTRUCT_,
 * opendiag_core.h), for owf_open()'s check.
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define OWF_LIB_NAME "owfdiag.library"
#define OWF_LIB_VERSION 1
#define OWF_LIB_REVISION 0
#define OWF_LIB_VERS "1.0"
#define OWF_LIB_DATE "17.11.2026"
#define OWF_LIB_CALLS_H "owfdiag_calls.h"
#include "owf_diagram.h"
#include "opendiag_core.h"
#include "opendiag_clip.h"
#include "opendiag_order.h"
#include "opendiag_align.h"
#include "opendiag_mind.h"
#include "opendiag_style.h"
#include "opendiag_stencil.h"
#include "opendiag_rexx.h"
#include "owf_libbase.h"

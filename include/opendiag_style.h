/*
 * opendiag_style.h: GFOpenDiag's Style window's sums (src/diag/odg_style.c): the style keys it shows (fill, line and
 * text colours, line width, text size, dashed, rounded), what the selection has for each (one value when they all
 * agree, nothing shown when they differ), and the text typed checked and set on every selected cell as one Undo
 * step. Plain C99, no Amiga headers: tested on the host (tests/diag/test_odg_style.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, step D4 (the inspector).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_STYLE_H
#define OPENDIAG_STYLE_H

#include "opendiag_core.h"

typedef enum {
    ODG_PROP_FILL, ODG_PROP_STROKE, ODG_PROP_FONT_COLOUR, ODG_PROP_STROKE_WIDTH, ODG_PROP_FONT_SIZE,
    ODG_PROP_DASHED, ODG_PROP_ROUNDED, ODG_NPROPS
} odg_prop;

typedef enum { ODG_KIND_COLOUR, ODG_KIND_NUMBER, ODG_KIND_SWITCH } odg_prop_kind;

/* A property's style key ("fillColor"), its title in the window ("Fill") and its kind. */
const char *odg_prop_key(odg_prop p);
const char *odg_prop_title(odg_prop p);
odg_prop_kind odg_prop_kind_of(odg_prop p);

/* What the cells' styles have for p, into out: 1 when every one of them that is on the page agrees (out "" when
 * none sets it), 0 when they differ or none is on the page (out ""). A switch reads "1" or "0". */
int odg_style_common(const odg_doc *doc, const char *const *ids, int n, odg_prop p, char *out, size_t size);

/* text checked for p: a colour is #rgb, #rrggbb or none; a number above 0 and up to 999 with at most two
 * decimals; a switch 0 or 1; "" takes the key off (the default). Written as draw.io writes it into out
 * (at least 24 bytes): "#RGB" as "#rrggbb" in lower case, numbers without a trailing ".0". 0 when it is
 * not one of those. */
int odg_prop_check(odg_prop p, const char *text, char *out, size_t size);

/* What the Style window's field for p starts with: the cells' common value when odg_prop_check accepts it, as it
 * writes it (at least 24 bytes), and 1; "" and 0 when they differ or the value is one the field can't hold (a
 * light-dark(...) fill, a style word of its own), so the field starts empty and, left empty, keeps it as it is. */
int odg_style_field(const odg_doc *doc, const char *const *ids, int n, odg_prop p, char *out, size_t size);

/* text set as p on each cell, as one Undo step. OWF_ERR_FORMAT when the text doesn't check or no id is a cell on
 * the page; OWF_ERR_MEMORY. */
int odg_style_apply(odg_doc *doc, const char *const *ids, int n, odg_prop p, const char *text);
/* What the Style window's OK does: texts[p] (ODG_NPROPS of them, NULL for a property left as it was) set on each
 * cell as one Undo step, after every one is checked. OWF_OK and no step when none is given; OWF_ERR_FORMAT with
 * the first that doesn't check in *bad (nothing set), or when no id is a cell on the page; OWF_ERR_MEMORY. */
int odg_style_apply_all(odg_doc *doc, const char *const *ids, int n, const char *const *texts, int *bad);

#endif

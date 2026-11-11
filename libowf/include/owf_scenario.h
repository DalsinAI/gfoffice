/*
 * libowf: scenarios (GFOpenCalc's Data > Scenarios), as the spreadsheet's
 * core, the ODS reader and writer and the XLSX reader and writer share them.
 *
 * A scenario is a named set of values for some cells of a sheet (its
 * changing cells), with a comment; showing it puts those values in the
 * cells. The model keeps each in hidden names on its sheet:
 * _GF_SCENARIO_k (k from 1), a text with its name, comment and values, and
 * _GF_SCENARIO_k_Rn, its ranges (so they follow inserted rows and columns
 * as any name does). ODS files carry them as scenario tables
 * (table:scenario) after their sheet, XLSX files as the sheet's scenarios
 * element; the readers turn those back into the names.
 *
 * The code is in libowf/calc/owf_scenariodef.h, included after
 * owf_pivotdef.h (whose cell and sheet readers it uses) by owf_book.c, and
 * by GFOpenCalc's core when it runs on owfcalc.library.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_SCENARIO_H
#define OWF_SCENARIO_H

#include <stddef.h>
#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OWF_SCENARIO_PREFIX "_GF_SCENARIO_"
#define OWF_SCENARIOS 32             /* a sheet's, at most */
#define OWF_SCENARIO_RANGES 8
#define OWF_SCENARIO_CELLS 2048      /* a scenario's changing cells, at most */
#define OWF_SCENARIO_NAME 64         /* bytes, UTF-8 */
#define OWF_SCENARIO_COMMENT 256

typedef struct {
    char name[OWF_SCENARIO_NAME];
    char comment[OWF_SCENARIO_COMMENT];
    unsigned nranges;
    owf_range range[OWF_SCENARIO_RANGES];
    unsigned ncells;                 /* the ranges' cells: row by row, range after range */
    char **value;                    /* each one's input as typed: "12", "=A1*2", "East", "'007", "" (empty) */
    int active;                      /* the one last shown on its sheet */
} owf_scenario;

/* 1 for _GF_SCENARIO_ names. */
int owf_scenario_is_name(const char *name);
/* How many scenarios the sheet has. */
int owf_scenario_count(const owf_book *book, int sheet);
/* The sheet's scenario index (from 0): 1 when it reads (free it with
 * owf_scenario_free), 0 when there is none or its names no longer agree
 * (a range that grew, say). */
int owf_scenario_get(const owf_book *book, int sheet, int index, owf_scenario *sc);
void owf_scenario_free(owf_scenario *sc);
/* How many cells the ranges hold (what ncells must be). */
unsigned owf_scenario_cells(const owf_scenario *sc);
/* Writes the scenario's names: index count adds one. OWF_OK, OWF_ERR_FORMAT
 * (index, ranges or cell count wrong), OWF_ERR_MEMORY. */
int owf_scenario_put(owf_book *book, int sheet, int index, const owf_scenario *sc);
/* Removes one; the later ones move down. OWF_OK, OWF_ERR_FORMAT. */
int owf_scenario_remove(owf_book *book, int sheet, int index);
/* A cell's value as one would type it, for a scenario (owf_malloc'd, NULL:
 * memory): its formula with "=", a number, TRUE/FALSE, text (with a "'"
 * before text that would read as something else), "" when empty. */
char *owf_scenario_input(const owf_cell *cell);

#ifdef __cplusplus
}
#endif

#endif

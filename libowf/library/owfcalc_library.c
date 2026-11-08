/*
 * owfcalc.library 3.6, GFOpenCalc's part of libowf: workbooks. The workbook
 * model, its formats (XLSX, ODS, XLS, CSV), charts, formula engine and shared
 * page PDF writer, with the library's own double arithmetic and maths.
 *
 * Its calls are owfcalc_lib.sfd's (owfcalc_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h. The first 17 calls
 * are the same in all four of GFOffice's libraries
 * (docs/suite/owf-libraries.md).
 *
 * 3.1: owf_rowinfo.style, a row's own style, and owf_sheet_set_row_style
 * (GFOpenCalc 0.1.22); owf_rowinfo is larger, so GFOpenCalc's size check asks
 * for this library.
 * 3.2: owf_calc_place_chart and owf_calc_nudge_chart, a chart moved
 * (GFOpenCalc 0.1.27).
 * 3.3: the shared page PDF writer, used by GFOpenCalc's printed sheets.
 * 3.4: owf_sheet_keep_charts, independent chart parts for sheet duplication.
 * 3.5: owf_calc_transform_chart, atomic chart movement and resizing.
 * 3.6: axis titles in the shared owf_chart model (GFOpenCalc 0.1.49).
 *
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#define OWF_LIB_NAME "owfcalc.library"
#define OWF_LIB_VERSION 3
#define OWF_LIB_REVISION 6
#define OWF_LIB_VERS "3.6"
#define OWF_LIB_DATE "8.11.2026"
#define OWF_LIB_CALLS_H "owfcalc_calls.h"
#include "owf_libbase.h"

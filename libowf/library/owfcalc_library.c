/*
 * owfcalc.library 3.1, OpenCalc's part of libowf: workbooks. The workbook
 * model, its formats (XLSX, ODS, XLS, CSV), charts and the formula engine,
 * with the library's own double arithmetic and maths.
 *
 * Its calls are owfcalc_lib.sfd's (owfcalc_calls.h, made from it by
 * scripts/gen-lib-calls.py); the rest is owf_libbase.h. The first 17 calls
 * are the same in all four of GFOffice's libraries
 * (docs/suite/owf-libraries.md).
 *
 * 3.1: owf_rowinfo.style, a row's own style, and owf_sheet_set_row_style
 * (OpenCalc 0.1.22); owf_rowinfo is larger, so OpenCalc's size check asks
 * for this library.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#define OWF_LIB_NAME "owfcalc.library"
#define OWF_LIB_VERSION 3
#define OWF_LIB_REVISION 1
#define OWF_LIB_VERS "3.1"
#define OWF_LIB_DATE "26.10.2026"
#define OWF_LIB_CALLS_H "owfcalc_calls.h"
#include "owf_libbase.h"

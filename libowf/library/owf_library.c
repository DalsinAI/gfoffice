/*
 * owf.library 2.0: libowf, the GFOffice Suite's file formats, shared by
 * every program that reads or writes documents, workbooks or Publisher
 * files (OpenWrite, OpenCalc, OWConvert, datatypes, OpenBench search). The
 * calls are owf.h's, owf_calc.h's, owf_zip.h's and owf_pub.h's
 * (owf_lib.sfd); each takes its arguments in the SFD's registers and runs
 * libowf's own function. Version 1's 29 calls keep their offsets; version
 * 2's 58 follow them. A double comes in two data registers, high word
 * first, and is put together here.
 *
 * Global state: SysBase, DOSBase, the runtime's memory pool and z.library's
 * base (opened the first time a document needs inflate or deflate). A
 * document, book or engine belongs to the task using it; libowf keeps no
 * state between calls, so two tasks with two documents need nothing more.
 * The double arithmetic and the maths are the library's own
 * (owf_softdouble.c, owf_libm.c): no mathieee library is opened, so no
 * caller's FPCR is touched.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>

#include "owf.h"
#include "owf_calc.h"
#include "owf_pub.h"
#include "owf_zip.h"
#include "owf_runtime.h"
#include "owf_zlib.h"

#define REG(r, decl) register decl __asm(#r)
#define LIB_VERSION 2
#define LIB_REVISION 2                 /* 2.1: libowf 1.2's structures; 2.2: 1.3's (frames: owf_doc, owf_run_kind), owf_open
                                          checks their sizes */

struct OWFLibBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

/* Run from the Shell by mistake, the library does nothing. */
int start(void) { return -1; }

static const char lib_name[] = "owf.library";
static const char lib_id[] =
    "owf.library 2.2 (25.10.2026) libowf " OWF_VERSION ", AmigaChrome Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "$VER: owf.library 2.2 (25.10.2026) MIT, Copyright (c) 2026 AmigaChrome Team";

static struct Library *lib_init(REG(d0, struct OWFLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OWFLibBase *base));
static BPTR lib_close(REG(a6, struct OWFLibBase *base));
static BPTR lib_expunge(REG(a6, struct OWFLibBase *base));
static ULONG lib_null(void);

/* ---- the calls ----------------------------------------------------------- */

static const char *L_owf_error_text(REG(d0, int error), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_error_text(error);
}

static void *L_owf_malloc(REG(d0, size_t n), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_malloc(n);
}

static void *L_owf_calloc(REG(d0, size_t n),
    REG(d1, size_t size),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calloc(n, size);
}

static void *L_owf_realloc(REG(a0, void *p), REG(d0, size_t n), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_realloc(p, n);
}

static void L_owf_free(REG(a0, void *p), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_free(p);
}

static owf_doc *L_owf_doc_new(REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_doc_new();
}

static void L_owf_doc_free(REG(a0, owf_doc *doc), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_doc_free(doc);
}

static void L_owf_parafmt_init(REG(a0, owf_parafmt *fmt), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_parafmt_init(fmt);
}

static void L_owf_charfmt_init(REG(a0, owf_charfmt *fmt), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_charfmt_init(fmt);
}

static owf_para *L_owf_story_add(REG(a0, owf_story *story),
    REG(a1, const owf_parafmt *fmt),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_story_add(story, fmt);
}

static int L_owf_para_add_text(REG(a0, owf_para *para),
    REG(a1, const owf_charfmt *fmt),
    REG(a2, const char *utf8),
    REG(d0, size_t len),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_para_add_text(para, fmt, utf8, len);
}

static int L_owf_para_add_link_text(REG(a0, owf_para *para),
    REG(a1, const owf_charfmt *fmt),
    REG(a2, const char *utf8),
    REG(d0, size_t len),
    REG(a3, const char *href),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_para_add_link_text(para, fmt, utf8, len, href);
}

static int L_owf_para_add_special(REG(a0, owf_para *para),
    REG(a1, const owf_charfmt *fmt),
    REG(d0, owf_run_kind kind),
    REG(d1, owf_field field),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_para_add_special(para, fmt, kind, field);
}

static int L_owf_para_add_image(REG(a0, owf_para *para),
    REG(a1, const owf_charfmt *fmt),
    REG(d0, int image_index),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_para_add_image(para, fmt, image_index);
}

static int L_owf_doc_add_image(REG(a0, owf_doc *doc),
    REG(a1, const char *name),
    REG(a2, const char *mime),
    REG(a3, const void *data),
    REG(d0, size_t length),
    REG(d1, int width),
    REG(d2, int height),
    REG(d3, const char *alt),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_doc_add_image(doc, name, mime, data, length, width, height, alt);
}

static int L_owf_doc_font(REG(a0, owf_doc *doc),
    REG(a1, const char *name),
    REG(d0, owf_font_kind kind),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_doc_font(doc, name, kind);
}

static int L_owf_doc_set_title(REG(a0, owf_doc *doc),
    REG(a1, const char *utf8),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_doc_set_title(doc, utf8);
}

static const char *L_owf_font_modern(REG(a0, const char *name),
    REG(a1, owf_font_kind *kind),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_font_modern(name, kind);
}

static owf_report *L_owf_report_new(REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_report_new();
}

static void L_owf_report_free(REG(a0, owf_report *report), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_report_free(report);
}

static int L_owf_report_count(REG(a0, const owf_report *report), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_report_count(report);
}

static const char *L_owf_report_note(REG(a0, const owf_report *report),
    REG(d0, int n),
    REG(a1, owf_note_level *level),
    REG(a2, int *times),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_report_note(report, n, level, times);
}

static int L_owf_format_count(REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_format_count();
}

static const owf_format *L_owf_format_at(REG(d0, int index), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_format_at(index);
}

static const owf_format *L_owf_format_named(REG(a0, const char *name), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_format_named(name);
}

static const owf_format *L_owf_format_for_filename(REG(a0, const char *filename), REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_format_for_filename(filename);
}

static int L_owf_import_memory(REG(a0, const unsigned char *data),
    REG(d0, size_t length),
    REG(a1, const char *format),
    REG(a2, owf_doc **doc),
    REG(a3, owf_report *report),
    REG(d1, const owf_format **used),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_import_memory(data, length, format, doc, report, used);
}

static int L_owf_import_file(REG(a0, const char *path),
    REG(a1, const char *format),
    REG(a2, owf_doc **doc),
    REG(a3, owf_report *report),
    REG(d0, const owf_format **used),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_import_file(path, format, doc, report, used);
}

static int L_owf_export_file(REG(a0, const owf_doc *doc),
    REG(a1, const char *path),
    REG(a2, const char *format),
    REG(a3, owf_report *report),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_export_file(doc, path, format, report);
}

/* ---- version 2 ------------------------------------------------------------ */

static const char *L_owf_error_name(REG(d0, int error),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_error_name(error);
}

static owf_book *L_owf_book_new(REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_new();
}

static void L_owf_book_free(REG(a0, owf_book *book),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_book_free(book);
}

static owf_sheet *L_owf_book_add_sheet(REG(a0, owf_book *book),
    REG(a1, const char *name),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_add_sheet(book, name);
}

static void L_owf_cellstyle_init(REG(a0, owf_cellstyle *style),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_cellstyle_init(style);
}

static int L_owf_book_style(REG(a0, owf_book *book),
    REG(a1, const owf_cellstyle *style),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_style(book, style);
}

static int L_owf_book_numfmt(REG(a0, owf_book *book),
    REG(a1, const char *format),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_numfmt(book, format);
}

static int L_owf_book_font(REG(a0, owf_book *book),
    REG(a1, const char *name),
    REG(d0, owf_font_kind kind),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_font(book, name, kind);
}

static owf_cell *L_owf_sheet_cell(REG(a0, owf_sheet *sheet),
    REG(d0, unsigned row),
    REG(d1, unsigned col),
    REG(d2, int create),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_sheet_cell(sheet, row, col, create);
}

static const owf_cell *L_owf_sheet_find(REG(a0, const owf_sheet *sheet),
    REG(d0, unsigned row),
    REG(d1, unsigned col),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_sheet_find(sheet, row, col);
}

static void L_owf_cell_set_number(REG(a0, owf_cell *cell),
    REG(d0, ULONG number_hi),
    REG(d1, ULONG number_lo),
    REG(a6, struct OWFLibBase *base))
{
    union { ULONG u[2]; double d; } number;
    (void)base;
    number.u[0] = number_hi;
    number.u[1] = number_lo;
    owf_cell_set_number(cell, number.d);
}

static int L_owf_cell_set_text(REG(a0, owf_cell *cell),
    REG(a1, const char *utf8),
    REG(d0, size_t length),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_cell_set_text(cell, utf8, length);
}

static void L_owf_cell_set_bool(REG(a0, owf_cell *cell),
    REG(d0, int value),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_cell_set_bool(cell, value);
}

static void L_owf_cell_set_error(REG(a0, owf_cell *cell),
    REG(d0, int error),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_cell_set_error(cell, error);
}

static int L_owf_cell_set_formula(REG(a0, owf_cell *cell),
    REG(a1, const char *text),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_cell_set_formula(cell, text);
}

static int L_owf_sheet_set_columns(REG(a0, owf_sheet *sheet),
    REG(d0, unsigned first),
    REG(d1, unsigned last),
    REG(d2, int width),
    REG(d3, int style),
    REG(d4, int hidden),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_sheet_set_columns(sheet, first, last, width, style, hidden);
}

static int L_owf_sheet_set_row(REG(a0, owf_sheet *sheet),
    REG(d0, unsigned row),
    REG(d1, int height),
    REG(d2, int hidden),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_sheet_set_row(sheet, row, height, hidden);
}

static int L_owf_sheet_merge(REG(a0, owf_sheet *sheet),
    REG(a1, const owf_range *range),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_sheet_merge(sheet, range);
}

static int L_owf_book_add_name(REG(a0, owf_book *book),
    REG(a1, const char *name),
    REG(d0, int sheet),
    REG(a2, const char *formula),
    REG(d1, int hidden),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_add_name(book, name, sheet, formula, hidden);
}

static void L_owf_number_text(REG(d0, ULONG number_hi),
    REG(d1, ULONG number_lo),
    REG(a0, char *out),
    REG(a6, struct OWFLibBase *base))
{
    union { ULONG u[2]; double d; } number;
    (void)base;
    number.u[0] = number_hi;
    number.u[1] = number_lo;
    owf_number_text(number.d, out);
}

static int L_owf_book_format_count(REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_format_count();
}

static const owf_book_format *L_owf_book_format_at(REG(d0, int index),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_format_at(index);
}

static const owf_book_format *L_owf_book_format_named(REG(a0, const char *name),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_format_named(name);
}

static const owf_book_format *L_owf_book_format_for_filename(REG(a0, const char *filename),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_book_format_for_filename(filename);
}

static int L_owf_import_book_memory(REG(a0, const unsigned char *data),
    REG(d0, size_t length),
    REG(a1, const char *format),
    REG(a2, owf_book **book),
    REG(a3, owf_report *report),
    REG(d1, const owf_book_format **used),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_import_book_memory(data, length, format, book, report, used);
}

static int L_owf_import_book_file(REG(a0, const char *path),
    REG(a1, const char *format),
    REG(a2, owf_book **book),
    REG(a3, owf_report *report),
    REG(d0, const owf_book_format **used),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_import_book_file(path, format, book, report, used);
}

static int L_owf_export_book_file(REG(a0, const owf_book *book),
    REG(a1, const char *path),
    REG(a2, const char *format),
    REG(a3, owf_report *report),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_export_book_file(book, path, format, report);
}

static owf_calc *L_owf_calc_new(REG(a0, owf_book *book),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_new(book);
}

static void L_owf_calc_free(REG(a0, owf_calc *calc),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_calc_free(calc);
}

static int L_owf_calc_rebuild(REG(a0, owf_calc *calc),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_rebuild(calc);
}

static void L_owf_calc_set_date_order(REG(a0, owf_calc *calc),
    REG(d0, int order),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_calc_set_date_order(calc, order);
}

static int L_owf_calc_enter(REG(a0, owf_calc *calc),
    REG(d0, int sheet),
    REG(d1, unsigned row),
    REG(d2, unsigned col),
    REG(a1, const char *input),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_enter(calc, sheet, row, col, input);
}

static int L_owf_calc_recalc(REG(a0, owf_calc *calc),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_recalc(calc);
}

static int L_owf_calc_recalc_all(REG(a0, owf_calc *calc),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_recalc_all(calc);
}

static void L_owf_calc_set_notify(REG(a0, owf_calc *calc),
    REG(a1, owf_calc_notify notify),
    REG(a2, void *user),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_calc_set_notify(calc, notify, user);
}

static unsigned L_owf_calc_state(REG(a0, const owf_calc *calc),
    REG(d0, int sheet),
    REG(d1, unsigned row),
    REG(d2, unsigned col),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_state(calc, sheet, row, col);
}

static int L_owf_calc_circular_count(REG(a0, const owf_calc *calc),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_circular_count(calc);
}

static int L_owf_calc_shift(REG(a0, owf_calc *calc),
    REG(d0, int sheet),
    REG(d1, int axis),
    REG(d2, unsigned at),
    REG(d3, int count),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_shift(calc, sheet, axis, at, count);
}

static int L_owf_calc_touch(REG(a0, owf_calc *calc),
    REG(d0, int sheet),
    REG(a1, const owf_range *range),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_touch(calc, sheet, range);
}

static int L_owf_calc_rename_sheet(REG(a0, owf_calc *calc),
    REG(d0, int sheet),
    REG(a1, const char *name),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_rename_sheet(calc, sheet, name);
}

static int L_owf_calc_move_range(REG(a0, owf_calc *calc),
    REG(d0, int sheet),
    REG(a1, const owf_range *range),
    REG(d1, int dsheet),
    REG(d2, int drow),
    REG(d3, int dcol),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_move_range(calc, sheet, range, dsheet, drow, dcol);
}

static char *L_owf_calc_move_formula(REG(a0, const char *text),
    REG(d0, int drow),
    REG(d1, int dcol),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_move_formula(text, drow, dcol);
}

static int L_owf_calc_evaluate(REG(a0, owf_calc *calc),
    REG(d0, int sheet),
    REG(d1, unsigned row),
    REG(d2, unsigned col),
    REG(a1, const char *formula),
    REG(a2, owf_cell *result),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_evaluate(calc, sheet, row, col, formula, result);
}

static void L_owf_calc_cell_name(REG(d0, unsigned row),
    REG(d1, unsigned col),
    REG(a0, char *out),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_calc_cell_name(row, col, out);
}

static int L_owf_calc_parse_cell(REG(a0, const char *text),
    REG(a1, unsigned *row),
    REG(a2, unsigned *col),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_parse_cell(text, row, col);
}

static int L_owf_calc_format(REG(a0, const owf_book *book),
    REG(a1, const owf_cell *cell),
    REG(a2, char *out),
    REG(d0, size_t size),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_format(book, cell, out, size);
}

static int L_owf_calc_format_value(REG(a0, const char *format),
    REG(a1, const owf_cell *value),
    REG(d0, int date1904),
    REG(d1, int width),
    REG(a2, char *out),
    REG(d2, size_t size),
    REG(a3, unsigned long *colour),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_calc_format_value(format, value, date1904, width, out, size, colour);
}

static owf_kind L_owf_file_kind(REG(a0, const unsigned char *data),
    REG(d0, size_t length),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_file_kind(data, length);
}

static unsigned long L_owf_struct_size(REG(d0, int which),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_struct_size(which);
}

static int L_owf_unzip_open(REG(a0, owf_unzip *zip),
    REG(a1, const unsigned char *data),
    REG(d0, size_t length),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_unzip_open(zip, data, length);
}

static int L_owf_unzip_has(REG(a0, const owf_unzip *zip),
    REG(a1, const char *name),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_unzip_has(zip, name);
}

static int L_owf_unzip_read(REG(a0, const owf_unzip *zip),
    REG(a1, const char *name),
    REG(a2, unsigned char **data),
    REG(a3, size_t *length),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_unzip_read(zip, name, data, length);
}

static int L_owf_inflate(REG(a0, unsigned char *out),
    REG(d0, size_t out_length),
    REG(a1, const unsigned char *in),
    REG(d1, size_t in_length),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_inflate(out, out_length, in, in_length);
}

static const char *L_owf_pub_version_name(REG(d0, int version),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_pub_version_name(version);
}

static int L_owf_pub_detect(REG(a0, const unsigned char *data),
    REG(d0, size_t length),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_pub_detect(data, length);
}

static int L_owf_pub_read(REG(a0, const unsigned char *data),
    REG(d0, size_t length),
    REG(a1, owf_pub **pub),
    REG(a2, owf_report *report),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_pub_read(data, length, pub, report);
}

static int L_owf_pub_read_file(REG(a0, const char *path),
    REG(a1, owf_pub **pub),
    REG(a2, owf_report *report),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    return owf_pub_read_file(path, pub, report);
}

static void L_owf_pub_free(REG(a0, owf_pub *pub),
    REG(a6, struct OWFLibBase *base))
{
    (void)base;
    owf_pub_free(pub);
}

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)L_owf_error_text, (APTR)L_owf_malloc, (APTR)L_owf_calloc,
    (APTR)L_owf_realloc, (APTR)L_owf_free, (APTR)L_owf_doc_new,
    (APTR)L_owf_doc_free, (APTR)L_owf_parafmt_init, (APTR)L_owf_charfmt_init,
    (APTR)L_owf_story_add, (APTR)L_owf_para_add_text,
    (APTR)L_owf_para_add_link_text, (APTR)L_owf_para_add_special,
    (APTR)L_owf_para_add_image, (APTR)L_owf_doc_add_image,
    (APTR)L_owf_doc_font, (APTR)L_owf_doc_set_title, (APTR)L_owf_font_modern,
    (APTR)L_owf_report_new, (APTR)L_owf_report_free,
    (APTR)L_owf_report_count, (APTR)L_owf_report_note,
    (APTR)L_owf_format_count, (APTR)L_owf_format_at,
    (APTR)L_owf_format_named, (APTR)L_owf_format_for_filename,
    (APTR)L_owf_import_memory, (APTR)L_owf_import_file,
    (APTR)L_owf_export_file,
    /* version 2 */
    (APTR)L_owf_error_name, (APTR)L_owf_book_new, (APTR)L_owf_book_free, (APTR)L_owf_book_add_sheet,
    (APTR)L_owf_cellstyle_init, (APTR)L_owf_book_style, (APTR)L_owf_book_numfmt,
    (APTR)L_owf_book_font, (APTR)L_owf_sheet_cell, (APTR)L_owf_sheet_find,
    (APTR)L_owf_cell_set_number, (APTR)L_owf_cell_set_text, (APTR)L_owf_cell_set_bool,
    (APTR)L_owf_cell_set_error, (APTR)L_owf_cell_set_formula, (APTR)L_owf_sheet_set_columns,
    (APTR)L_owf_sheet_set_row, (APTR)L_owf_sheet_merge, (APTR)L_owf_book_add_name,
    (APTR)L_owf_number_text, (APTR)L_owf_book_format_count, (APTR)L_owf_book_format_at,
    (APTR)L_owf_book_format_named, (APTR)L_owf_book_format_for_filename,
    (APTR)L_owf_import_book_memory, (APTR)L_owf_import_book_file, (APTR)L_owf_export_book_file,
    (APTR)L_owf_calc_new, (APTR)L_owf_calc_free, (APTR)L_owf_calc_rebuild,
    (APTR)L_owf_calc_set_date_order, (APTR)L_owf_calc_enter, (APTR)L_owf_calc_recalc,
    (APTR)L_owf_calc_recalc_all, (APTR)L_owf_calc_set_notify, (APTR)L_owf_calc_state,
    (APTR)L_owf_calc_circular_count, (APTR)L_owf_calc_shift, (APTR)L_owf_calc_touch,
    (APTR)L_owf_calc_rename_sheet, (APTR)L_owf_calc_move_range, (APTR)L_owf_calc_move_formula,
    (APTR)L_owf_calc_evaluate, (APTR)L_owf_calc_cell_name, (APTR)L_owf_calc_parse_cell,
    (APTR)L_owf_calc_format, (APTR)L_owf_calc_format_value, (APTR)L_owf_file_kind,
    (APTR)L_owf_struct_size, (APTR)L_owf_unzip_open, (APTR)L_owf_unzip_has, (APTR)L_owf_unzip_read,
    (APTR)L_owf_inflate, (APTR)L_owf_pub_version_name, (APTR)L_owf_pub_detect, (APTR)L_owf_pub_read,
    (APTR)L_owf_pub_read_file, (APTR)L_owf_pub_free,
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct OWFLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

/* ---- the library itself -------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct OWFLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    if (!DOSBase || !owf_rt_init()) {
        owf_rt_cleanup();
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        FreeMem((UBYTE *)base - base->lib.lib_NegSize,
                base->lib.lib_NegSize + base->lib.lib_PosSize);
        return NULL;
    }
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct OWFLibBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct OWFLibBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct OWFLibBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    owf_z_close();
    owf_rt_cleanup();
    CloseLibrary((struct Library *)DOSBase);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void) { return 0; }

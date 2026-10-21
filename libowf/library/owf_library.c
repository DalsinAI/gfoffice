/*
 * owf.library 1.1: libowf, OpenWrite's file formats, shared by every program
 * that reads or writes documents (OpenWrite, OWConvert, datatypes, OpenBench
 * search). The calls are owf.h's (owf_lib.sfd); each takes its arguments in
 * the SFD's registers and runs libowf's own function.
 *
 * Global state: SysBase, DOSBase, the runtime's memory pool and z.library's
 * base (opened the first time a document needs inflate or deflate). A
 * document belongs to the task using it; libowf keeps no state between
 * calls, so two tasks with two documents need nothing more.
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
#include "owf_runtime.h"
#include "owf_zlib.h"

#define REG(r, decl) register decl __asm(#r)
#define LIB_VERSION 1
#define LIB_REVISION 1

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
    "owf.library 1.1 (21.10.2026) libowf " OWF_VERSION ", AmigaChrome Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "$VER: owf.library 1.1 (21.10.2026) MIT, Copyright (c) 2026 AmigaChrome Team";

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

/*
 * OpenWrite's Print.
 *
 * With OpenPrint on the disk (C:OpenPrint), Print exports the document as a
 * PDF, laid out as on the page (the same export as Export PDF), to T: and
 * opens OpenPrint's Print window on it: the printer, copies and pages are
 * chosen there, and it goes to an IPP printer or is saved as a PDF
 * (24 October 2026: AmigaChrome retires the OS's printer drivers in favour of
 * OpenPrint). Without OpenPrint (OpenWrite on its own), the document's text
 * goes to printer.device, as before, and whatever printer driver the
 * Printer preferences name prints it.
 */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/printer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "ow_print.h"

#define OW_OPENPRINT "C:OpenPrint"

/* OpenPrint's Print window on file, started on its own (asynchronous): 1 started */
static int run_openprint(const char *file)
{
    char cmd[300];
    BPTR in, out;
    snprintf(cmd, sizeof cmd, "%s \"%s\"", OW_OPENPRINT, file);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, in, SYS_Output, out,
                                  NP_StackSize, 65536, TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
        return 0;
    }
    return 1;
}

static int have_openprint(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR requesters = me->pr_WindowPtr;
    BPTR lock;
    me->pr_WindowPtr = (APTR)-1;              /* a quiet check: no "insert volume" requester */
    lock = Lock((STRPTR)OW_OPENPRINT, ACCESS_READ);
    me->pr_WindowPtr = requesters;
    if (lock) UnLock(lock);
    return lock != 0;
}

/* T:<the document's name>.pdf (OpenPrint shows the file's name), or T:OpenWrite-Print.pdf when it has none */
static void print_path(const char *name, char *path, size_t cap)
{
    char stem[64];
    size_t n = 0;
    const char *p;
    if (name)
        for (p = name; *p && n + 1 < sizeof stem; p++)
            if (*p != ':' && *p != '/' && *p != '"') stem[n++] = *p;
    stem[n] = 0;
    if (n > 4 && (!strcmp(stem + n - 4, ".odt") || !strcmp(stem + n - 4, ".rtf") || !strcmp(stem + n - 4, ".txt")))
        stem[n - 4] = 0;
    else if (n > 5 && !strcmp(stem + n - 5, ".docx"))
        stem[n - 5] = 0;
    snprintf(path, cap, "T:%s.pdf", stem[0] ? stem : "OpenWrite-Print");
}

static int print_with_openprint(const owf_doc *doc, const char *name, char *status, size_t status_size)
{
    char path[96];
    owf_report *report;
    int rc;
    print_path(name, path, sizeof path);
    report = owf_report_new();
    rc = owf_export_file(doc, path, "pdf", report);
    owf_report_free(report);
    if (rc != OWF_OK) {
        if (status && status_size)
            snprintf(status, status_size, "The document couldn't be made ready to print (%s).", owf_error_text(rc));
        return 0;
    }
    if (!run_openprint(path)) {
        if (status && status_size) snprintf(status, status_size, "OpenPrint couldn't be started (%s).", OW_OPENPRINT);
        return 0;
    }
    if (status && status_size) snprintf(status, status_size, "OpenPrint's Print window is open for this document.");
    return 1;
}

static int write_bytes(struct IOStdReq *io, const void *data, size_t length)
{
    if (!length) return 1;
    io->io_Command = CMD_WRITE;
    io->io_Data = (APTR)data;
    io->io_Length = (ULONG)length;
    return DoIO((struct IORequest *)io) == 0 && io->io_Error == 0;
}

static int write_cstr(struct IOStdReq *io, const char *text)
{
    return text ? write_bytes(io, text, strlen(text)) : 1;
}

int ow_print_document(const owf_doc *doc, char *status, size_t status_size)
{
    return ow_print_document_named(doc, NULL, status, status_size);
}

int ow_print_document_named(const owf_doc *doc, const char *name, char *status, size_t status_size)
{
    struct MsgPort *port = NULL;
    struct IOStdReq *io = NULL;
    int opened = 0, ok = 0, p, r;

    if (status && status_size) status[0] = 0;
    if (!doc) {
        if (status && status_size) snprintf(status, status_size, "No document to print.");
        return 0;
    }
    if (have_openprint())
        return print_with_openprint(doc, name, status, status_size);

    port = CreateMsgPort();
    if (port) io = (struct IOStdReq *)CreateIORequest(port, sizeof(*io));
    if (!port || !io) {
        if (status && status_size) snprintf(status, status_size, "Not enough memory for printer.device.");
        goto done;
    }
    if (OpenDevice((STRPTR)"printer.device", 0, (struct IORequest *)io, 0)) {
        if (status && status_size)
            snprintf(status, status_size, "printer.device could not be opened. Check Printer Preferences.");
        goto done;
    }
    opened = 1;

    /* Reset / initialise the selected printer driver. printer.device consumes
     * this ANSI sequence before passing ordinary text to the driver. */
    if (!write_cstr(io, "\033c")) goto io_error;

    for (p = 0; p < doc->body.nparas; ++p) {
        const owf_para *para = &doc->body.paras[p];

        if (p && para->fmt.page_break_before)
            if (!write_cstr(io, "\f")) goto io_error;

        for (r = 0; r < para->nruns; ++r) {
            const owf_run *run = &para->runs[r];
            if (run->kind == OWF_RUN_TEXT && run->text) {
                if (!write_cstr(io, run->text)) goto io_error;
            } else if (run->kind == OWF_RUN_TAB) {
                if (!write_cstr(io, "\t")) goto io_error;
            } else if (run->kind == OWF_RUN_LINEBREAK) {
                if (!write_cstr(io, "\n")) goto io_error;
            } else if (run->kind == OWF_RUN_FIELD) {
                char field[24];
                if (run->field == OWF_FIELD_PAGE) snprintf(field, sizeof field, "%d", p + 1);
                else if (run->field == OWF_FIELD_PAGES) snprintf(field, sizeof field, "%d", doc->body.nparas ? doc->body.nparas : 1);
                else field[0] = 0;
                if (!write_cstr(io, field)) goto io_error;
            }
        }
        if (!write_cstr(io, "\n")) goto io_error;
    }

    /* Close the final page. OpenAmigaPrint detects the resulting PDF job
     * boundary and opens its native Save PDF / IPP window. */
    if (!write_cstr(io, "\f")) goto io_error;

    if (status && status_size)
        snprintf(status, status_size, "Document submitted to printer.device.");
    ok = 1;
    goto done;

io_error:
    if (status && status_size)
        snprintf(status, status_size, "printer.device reported error %ld.", (long)io->io_Error);

done:
    if (opened) CloseDevice((struct IORequest *)io);
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
    return ok;
}

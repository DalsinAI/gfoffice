/*
 * OpenWrite printer.device adapter.
 *
 * The editor stays independent of a particular spooler. OpenAmigaPrint is a
 * normal printer.device driver, so selecting it in Printer Preferences turns
 * this path into OpenPrint/PDF/IPP without special-case code in OpenWrite.
 */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/printer.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

#include "ow_print.h"

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
    struct MsgPort *port = NULL;
    struct IOStdReq *io = NULL;
    int opened = 0, ok = 0, p, r;

    if (status && status_size) status[0] = 0;
    if (!doc) {
        if (status && status_size) snprintf(status, status_size, "No document to print.");
        return 0;
    }

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

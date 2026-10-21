/*
 * libowf: CSV (RFC 4180), one sheet's values as text: commas between
 * them, quotes where a value needs them, CR LF after each row, UTF-8.
 * A formula's cell gives its last result; numbers are written as a formula
 * would write them, unformatted.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <string.h>

static void put_text(owf_buf *b, const char *s)
{
    size_t n = strlen(s);
    int quote = n && (s[0] == ' ' || s[n - 1] == ' ');
    const char *p;
    for (p = s; *p && !quote; p++)
        quote = *p == ',' || *p == '"' || *p == '\r' || *p == '\n';
    if (!quote) {
        owf_buf_put(b, s, n);
        return;
    }
    owf_buf_putc(b, '"');
    for (p = s; *p; p++) {
        if (*p == '"')
            owf_buf_putc(b, '"');
        owf_buf_putc(b, *p);
    }
    owf_buf_putc(b, '"');
}

static void put_value(owf_buf *b, const owf_cell *c)
{
    char num[32];
    const char *e;
    switch (c->type) {
    case OWF_VALUE_NUMBER:
        owf_number_text(c->number, num);
        owf_buf_puts(b, num);
        break;
    case OWF_VALUE_BOOL:
        owf_buf_puts(b, c->number ? "TRUE" : "FALSE");
        break;
    case OWF_VALUE_ERROR:
        e = owf_error_name(c->error);
        owf_buf_puts(b, e ? e : "#N/A");
        break;
    case OWF_VALUE_TEXT:
        put_text(b, c->text);
        break;
    case OWF_VALUE_EMPTY:
        break;
    }
}

static int export_csv(const owf_book *book, unsigned char **data, size_t *length, owf_report *report)
{
    const owf_sheet *sheet;
    owf_buf b;
    unsigned last_col = 0, row = 0, col;
    int i, any = 0;

    *data = NULL;
    *length = 0;
    owf_buf_init(&b);
    if (book->nsheets) {
        sheet = &book->sheets[book->active >= 0 && book->active < book->nsheets ? book->active : 0];
        if (book->nsheets > 1)
            owf_report_add(report, OWF_NOTE_LOST, "CSV holds one sheet: only \"%s\" was written", sheet->name);
        for (i = 0; i < sheet->ncells; i++)
            if (sheet->cells[i].type != OWF_VALUE_EMPTY && (!any || sheet->cells[i].col > last_col)) {
                last_col = sheet->cells[i].col;
                any = 1;
            }
        /* every row as wide as the widest, as RFC 4180 asks */
        for (i = 0; any && i < sheet->ncells;) {
            const owf_cell *c = &sheet->cells[i];
            if (c->type == OWF_VALUE_EMPTY) {
                i++;
                continue;
            }
            for (; row < c->row; row++) {
                for (col = 0; col < last_col; col++)
                    owf_buf_putc(&b, ',');
                owf_buf_puts(&b, "\r\n");
            }
            for (col = 0; col <= last_col; col++) {
                if (col)
                    owf_buf_putc(&b, ',');
                while (i < sheet->ncells && sheet->cells[i].row == row && sheet->cells[i].col < col)
                    i++;
                if (i < sheet->ncells && sheet->cells[i].row == row && sheet->cells[i].col == col)
                    put_value(&b, &sheet->cells[i]);
            }
            owf_buf_puts(&b, "\r\n");
            while (i < sheet->ncells && sheet->cells[i].row == row)
                i++;
            row++;
        }
    }
    return owf_buf_take(&b, data, length);
}

const owf_book_format owf_book_format_csv = {
    "csv", "Comma-separated values", "csv", NULL, NULL, export_csv
};

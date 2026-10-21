/*
 * book_dump FILE: reads a workbook with libowf and prints everything the
 * model holds, one item a line, for test_books.py to check. Exit 0 when
 * the file was read, 10 when it was refused (with the reason), never a
 * crash, cut short or not.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"

#include <stdio.h>
#include <string.h>

static void put_string(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            printf("\\%c", *s);
        else if (*s == '\n')
            printf("\\n");
        else if ((unsigned char)*s < 0x20)
            printf("\\x%02x", (unsigned char)*s);
        else
            putchar(*s);
    }
    putchar('"');
}

static void put_colour(unsigned long shade)
{
    if (OWF_SHADE_SET(shade))
        printf("%06lX", OWF_SHADE_RGB(shade));
    else
        printf("-");
}

int main(int argc, char **argv)
{
    owf_report *report = owf_report_new();
    owf_book *book = NULL;
    const owf_book_format *used = NULL;
    int result, i, k;

    if (argc != 2 || !report) {
        fprintf(stderr, "usage: book_dump FILE\n");
        return 20;
    }
    result = owf_import_book_file(argv[1], NULL, &book, report, &used);
    for (i = 0; i < owf_report_count(report); i++) {
        owf_note_level level;
        int times;
        const char *note = owf_report_note(report, i, &level, &times);
        printf("note %d ", (int)level);
        put_string(note);
        printf(" x%d\n", times);
    }
    if (result != OWF_OK) {
        printf("error %s\n", owf_error_text(result));
        owf_report_free(report);
        return 10;
    }
    printf("book format=%s sheets=%d active=%d date1904=%d\n", used->name, book->nsheets, book->active, book->date1904);
    for (i = 0; i < book->nfonts; i++) {
        printf("font %d ", i);
        put_string(book->fonts[i].name);
        printf(" kind=%d\n", (int)book->fonts[i].kind);
    }
    for (i = 0; i < book->nnumfmts; i++) {
        printf("numfmt %d ", i);
        put_string(book->numfmts[i]);
        putchar('\n');
    }
    for (i = 0; i < book->nstyles; i++) {
        const owf_cellstyle *s = &book->styles[i];
        printf("style %d font=%d size=%d flags=%u colour=", i, s->font.font, s->font.size, s->font.flags);
        if (s->font.colour == OWF_COLOUR_AUTO)
            printf("auto");
        else
            printf("%06lX", s->font.colour);
        printf(" numfmt=%d halign=%d valign=%d wrap=%d indent=%d fill=", s->number_format, s->halign, s->valign,
               s->wrap, s->indent);
        put_colour(s->fill);
        printf(" border=");
        for (k = 0; k < 4; k++) {
            printf("%s%d:", k ? "," : "", s->border[k]);
            put_colour(s->border_colour[k]);
        }
        putchar('\n');
    }
    for (i = 0; i < book->nnames; i++) {
        printf("name ");
        put_string(book->names[i].name);
        printf(" sheet=%d hidden=%d ", book->names[i].sheet, book->names[i].hidden);
        put_string(book->names[i].formula);
        putchar('\n');
    }
    for (i = 0; i < book->nsheets; i++) {
        const owf_sheet *sh = &book->sheets[i];
        printf("sheet %d ", i);
        put_string(sh->name);
        printf(" hidden=%d width=%d height=%d freeze=%u,%u\n", sh->hidden, sh->default_width, sh->default_height,
               sh->freeze_rows, sh->freeze_cols);
        for (k = 0; k < sh->ncols; k++)
            printf("col %u-%u width=%d style=%d hidden=%d\n", sh->cols[k].first, sh->cols[k].last, sh->cols[k].width,
                   sh->cols[k].style, sh->cols[k].hidden);
        for (k = 0; k < sh->nrows; k++)
            printf("row %u height=%d hidden=%d\n", sh->rows[k].row, sh->rows[k].height, sh->rows[k].hidden);
        for (k = 0; k < sh->nmerges; k++)
            printf("merge %u,%u-%u,%u\n", sh->merges[k].first_row, sh->merges[k].first_col, sh->merges[k].last_row,
                   sh->merges[k].last_col);
        for (k = 0; k < sh->ncells; k++) {
            const owf_cell *c = &sh->cells[k];
            char num[32];
            printf("cell %u,%u style=%d ", c->row, c->col, c->style);
            switch (c->type) {
            case OWF_VALUE_EMPTY: printf("empty"); break;
            case OWF_VALUE_NUMBER: owf_number_text(c->number, num); printf("number %s", num); break;
            case OWF_VALUE_BOOL: printf("bool %d", (int)c->number); break;
            case OWF_VALUE_ERROR: printf("error %s", owf_error_name(c->error) ? owf_error_name(c->error) : "?"); break;
            case OWF_VALUE_TEXT: printf("text "); put_string(c->text); break;
            }
            if (c->formula) {
                printf(" formula ");
                put_string(c->formula);
            }
            if (c->flags & OWF_CELL_ARRAY)
                printf(" array");
            putchar('\n');
        }
    }
    owf_book_free(book);
    owf_report_free(report);
    return 0;
}

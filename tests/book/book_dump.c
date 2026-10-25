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
    for (i = 0; i < book->nparts; i++) {
        printf("part ");
        put_string(book->parts[i].name);
        putchar(' ');
        put_string(book->parts[i].type);
        printf(" length=%lu\n", (unsigned long)book->parts[i].length);
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
        if (sh->drawing) {
            printf("drawing ");
            put_string(sh->drawing);
            putchar('\n');
        }
        {
            owf_chart *charts;
            int nc, c, r, p;
            if (owf_sheet_charts(book, i, &charts, &nc) != OWF_OK)
                printf("charts: out of memory\n");
            for (c = 0; c < nc; c++) {
                const owf_chart *ch = &charts[c];
                printf("chart kind=%d grouping=%d from=%u,%u+%ld,%ld ", ch->kind, ch->grouping, ch->from_row, ch->from_col,
                       ch->from_row_off, ch->from_col_off);
                if (ch->sized)
                    printf("size=%ldx%ld", ch->width, ch->height);
                else
                    printf("to=%u,%u+%ld,%ld", ch->to_row, ch->to_col, ch->to_row_off, ch->to_col_off);
                printf(" legend=%c gap=%d grid=%d", ch->legend ? ch->legend : '-', ch->gap, ch->gridlines);
                if (ch->has_min || ch->has_max) {
                    char a[32], b[32];
                    owf_number_text(ch->min, a);
                    owf_number_text(ch->max, b);
                    printf(" min=%s max=%s", ch->has_min ? a : "-", ch->has_max ? b : "-");
                }
                if (ch->hole)
                    printf(" hole=%d", ch->hole);
                if (ch->first_angle)
                    printf(" angle=%d", ch->first_angle);
                if (ch->vary)
                    printf(" vary=1");
                if (ch->kind == OWF_CHART_SCATTER) {
                    char a[32], b[32];
                    owf_number_text(ch->xmin, a);
                    owf_number_text(ch->xmax, b);
                    printf(" xgrid=%d xmin=%s xmax=%s", ch->x_gridlines, ch->has_xmin ? a : "-", ch->has_xmax ? b : "-");
                }
                if (ch->value_format) {
                    printf(" format ");
                    put_string(ch->value_format);
                }
                if (ch->x_format) {
                    printf(" xformat ");
                    put_string(ch->x_format);
                }
                printf(" title ");
                put_string(ch->title ? ch->title : "");
                putchar('\n');
                for (r = 0; r < ch->nseries; r++) {
                    const owf_chart_series *se = &ch->series[r];
                    printf("series %d colour=", se->index);
                    put_colour(se->colour);
                    printf(" name ");
                    put_string(se->name ? se->name : "");
                    if (se->name_cell) {
                        printf(" from ");
                        put_string(se->name_cell);
                    }
                    printf(" values ");
                    put_string(se->values ? se->values : "");
                    printf(" categories ");
                    put_string(se->categories ? se->categories : "");
                    if (se->x_values) {
                        printf(" x ");
                        put_string(se->x_values);
                    }
                    if (!se->line)
                        printf(" line=0");
                    if (se->marker)
                        printf(" marker=%d", se->marker);
                    if (se->marker_size)
                        printf(" size=%d", se->marker_size);
                    if (se->labels)
                        printf(" labels=%u@%d", se->labels, se->label_pos);
                    if (se->label_format) {
                        printf(" lformat ");
                        put_string(se->label_format);
                    }
                    if (se->format) {
                        printf(" format ");
                        put_string(se->format);
                    }
                    printf(" points");
                    for (p = 0; p < se->npoints; p++) {
                        char num[32];
                        if (se->points[p].has_value)
                            owf_number_text(se->points[p].value, num);
                        else
                            strcpy(num, "-");
                        if (se->points[p].has_x) {
                            char xs[32];
                            owf_number_text(se->points[p].x, xs);
                            printf(" %s:%s", xs, num);
                        } else {
                            printf(" %s", num);
                        }
                        if (OWF_SHADE_SET(se->points[p].colour))
                            printf("#%06lX", OWF_SHADE_RGB(se->points[p].colour));
                        if (se->points[p].label) {
                            putchar('=');
                            put_string(se->points[p].label);
                        }
                    }
                    putchar('\n');
                }
            }
            owf_charts_free(charts, nc);
        }
        if (sh->has_autofilter) {
            printf("autofilter %u,%u-%u,%u ", sh->autofilter.first_row, sh->autofilter.first_col, sh->autofilter.last_row,
                   sh->autofilter.last_col);
            put_string(sh->autofilter_xml ? sh->autofilter_xml : "");
            putchar('\n');
        }
        for (k = 0; k < sh->nvalidations; k++) {
            const owf_validation *dv = &sh->validations[k];
            int r;
            printf("validation type=%d op=%d blank=%d nodropdown=%d prompt=%d error=%d style=%d ranges=", dv->type, dv->op,
                   dv->allow_blank, dv->no_dropdown, dv->show_prompt, dv->show_error, dv->error_style);
            for (r = 0; r < dv->nranges; r++)
                printf("%s%u,%u-%u,%u", r ? ";" : "", dv->ranges[r].first_row, dv->ranges[r].first_col,
                       dv->ranges[r].last_row, dv->ranges[r].last_col);
            for (r = 0; r < 2; r++)
                if (dv->formula[r]) {
                    printf(" formula ");
                    put_string(dv->formula[r]);
                }
            if (dv->prompt_title || dv->prompt) {
                printf(" prompt ");
                put_string(dv->prompt_title ? dv->prompt_title : "");
                putchar(' ');
                put_string(dv->prompt ? dv->prompt : "");
            }
            if (dv->error_title || dv->error) {
                printf(" message ");
                put_string(dv->error_title ? dv->error_title : "");
                putchar(' ');
                put_string(dv->error ? dv->error : "");
            }
            putchar('\n');
        }
        for (k = 0; k < sh->ncomments; k++) {
            printf("comment %u,%u author ", sh->comments[k].row, sh->comments[k].col);
            put_string(sh->comments[k].author ? sh->comments[k].author : "");
            printf(" text ");
            put_string(sh->comments[k].text);
            putchar('\n');
        }
        for (k = 0; k < sh->ncondfmts; k++) {
            const owf_condfmt *cf = &sh->condfmts[k];
            int r;
            printf("cf type=%d op=%d priority=%d stop=%d ranges=", cf->type, cf->op, cf->priority, cf->stop);
            for (r = 0; r < cf->nranges; r++)
                printf("%s%u,%u-%u,%u", r ? ";" : "", cf->ranges[r].first_row, cf->ranges[r].first_col,
                       cf->ranges[r].last_row, cf->ranges[r].last_col);
            printf(" set=%u flags=%u colour=", cf->font_set, cf->font_flags);
            put_colour(cf->font_colour);
            printf(" fill=");
            put_colour(cf->fill);
            for (r = 0; r < 2; r++)
                if (cf->formula[r]) {
                    printf(" formula ");
                    put_string(cf->formula[r]);
                }
            for (r = 0; r < cf->npoints; r++) {
                printf(" point %d ", cf->points[r].kind);
                put_string(cf->points[r].value ? cf->points[r].value : "");
                putchar(' ');
                put_colour(cf->points[r].colour);
            }
            if (cf->xml) {
                printf(" xml ");
                put_string(cf->xml);
            }
            putchar('\n');
        }
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

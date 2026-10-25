/*
 * libowf: comma-separated values (RFC 4180) read into one sheet. The
 * separator is found from the first lines (comma, semicolon or tab; a
 * first line "sep=;" as Excel writes it says it outright); quoted fields
 * may hold separators, quotes doubled and line breaks; lines may end in CR
 * LF, LF or CR. The text is UTF-8 (a byte order mark is skipped), UTF-16
 * with its mark, or else Windows-1252.
 *
 * Each field becomes what it says, as Excel and the others read CSV:
 * numbers ("12", "-0.5", "1E+20"), percentages ("12.5%"), ISO dates and
 * times ("2026-10-21", "2026-10-21 13:45"), TRUE and FALSE, Excel's error
 * names, or text. A field starting with "=" stays text: CSV is data, and
 * a formula in it would be worked out from another program's text.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 1048576UL
#define MAX_COLS 16384UL

/* The bytes as UTF-8 in out: UTF-16 by its mark, UTF-8, else Windows-1252. */
static void to_utf8(const unsigned char *data, size_t length, owf_buf *out)
{
    if (length >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        int little = data[0] == 0xFF;
        size_t i;
        unsigned long high = 0;
        for (i = 2; i + 1 < length; i += 2) {
            unsigned long u = little ? (unsigned long)data[i] | ((unsigned long)data[i + 1] << 8)
                                     : ((unsigned long)data[i] << 8) | data[i + 1];
            if (u >= 0xD800 && u < 0xDC00) {
                high = u;
                continue;
            }
            if (u >= 0xDC00 && u < 0xE000) {
                owf_buf_put_utf8(out, high ? 0x10000UL + ((high - 0xD800) << 10) + (u - 0xDC00) : 0xFFFD);
                high = 0;
                continue;
            }
            if (high)
                owf_buf_put_utf8(out, 0xFFFD);
            high = 0;
            if (u)
                owf_buf_put_utf8(out, u);
        }
        return;
    }
    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        length -= 3;
    }
    if (owf_utf8_valid(data, length))
        owf_buf_put(out, data, length);
    else
        owf_buf_put_cp1252(out, data, length);
}

/* The separator the first lines agree on: the one with the same count, more
 * than none, on the most lines; a comma when none does. */
static int find_separator(const char *p, const char *end)
{
    static const char seps[3] = { ',', ';', '\t' };
    int counts[3][20], lines = 0, k, best = 0, best_score = -1;
    memset(counts, 0, sizeof counts);
    while (p < end && lines < 20) {
        int quoted = 0;
        for (; p < end; p++) {
            if (*p == '"')
                quoted = !quoted;
            else if (!quoted && (*p == '\n' || *p == '\r'))
                break;
            else if (!quoted)
                for (k = 0; k < 3; k++)
                    if (*p == seps[k])
                        counts[k][lines]++;
        }
        if (p < end && *p == '\r' && p + 1 < end && p[1] == '\n')
            p++;
        if (p < end)
            p++;
        lines++;
    }
    for (k = 0; k < 3; k++) {
        int score = 0, i, j;
        for (i = 0; i < lines; i++) {
            int same = 0;
            if (!counts[k][i])
                continue;
            for (j = 0; j < lines; j++)
                same += counts[k][j] == counts[k][i];
            if (same > score)
                score = same;
        }
        if (score > best_score) {
            best_score = score;
            best = k;
        }
    }
    return best_score > 0 ? seps[best] : ',';
}

typedef struct {
    owf_book *book;
    owf_sheet *sheet;
    owf_report *report;
    int failed;
    int styles[4];               /* percent, percent with decimals, date, date and time; 0 not made yet */
} csv;

static int style_with(csv *c, int which, const char *format)
{
    owf_cellstyle s;
    int f;
    if (c->styles[which])
        return c->styles[which];
    s = c->book->styles[0];
    f = owf_book_numfmt(c->book, format);
    if (f < 0) {
        c->failed = 1;
        return 0;
    }
    s.number_format = f;
    c->styles[which] = owf_book_style(c->book, &s);
    if (c->styles[which] < 0) {
        c->failed = 1;
        c->styles[which] = 0;
    }
    return c->styles[which];
}

/* "2026-10-21", "2026-10-21 13:45", "2026-10-21T13:45:30" */
static int iso_date(const char *s, size_t n, double *serial, int *with_time)
{
    char tmp[40];
    size_t i;
    if (n < 10 || n > 30 || s[4] != '-' || s[7] != '-')
        return 0;
    for (i = 0; i < 10; i++)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            return 0;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    if (n > 10 && tmp[10] == ' ')
        tmp[10] = 'T';
    *with_time = n > 10;
    return owf_parse_iso_date(tmp, serial);
}

static void put_field(csv *c, unsigned long row, unsigned long col, const char *s, size_t n)
{
    owf_cell *cell;
    double v;
    int code, with_time;
    if (!n)
        return;
    cell = owf_sheet_cell(c->sheet, (unsigned)row, (unsigned)col, 1);
    if (!cell) {
        c->failed = 1;
        return;
    }
    if (owf_number_parse(s, n, &v)) {
        owf_cell_set_number(cell, v);
    } else if (n > 1 && s[n - 1] == '%' && owf_number_parse(s, n - 1, &v)) {
        owf_cell_set_number(cell, v / 100);
        cell->style = memchr(s, '.', n) ? style_with(c, 1, "0.00%") : style_with(c, 0, "0%");
    } else if ((n == 4 && !strncmp(s, "TRUE", 4)) || (n == 5 && !strncmp(s, "FALSE", 5)) ||
               (n == 4 && !strncmp(s, "true", 4)) || (n == 5 && !strncmp(s, "false", 5))) {
        owf_cell_set_bool(cell, n == 4);
    } else if (s[0] == '#' && (code = owf_xl_error_code(s, n)) >= 0) {
        owf_cell_set_error(cell, code);
    } else if (iso_date(s, n, &v, &with_time)) {
        owf_cell_set_number(cell, v);
        cell->style = with_time ? style_with(c, 3, "yyyy-mm-dd hh:mm:ss") : style_with(c, 2, "yyyy-mm-dd");
    } else if (owf_cell_set_text(cell, s, n) < 0) {
        c->failed = 1;
    }
}

static int import_csv(const unsigned char *data, size_t length, owf_book *book, owf_report *report)
{
    owf_buf text, field;
    const char *p, *end;
    unsigned long row = 0, col = 0;
    int sep, cut = 0, result;
    csv c;

    memset(&c, 0, sizeof c);
    c.book = book;
    c.report = report;
    owf_buf_init(&text);
    owf_buf_init(&field);
    to_utf8(data, length, &text);
    if (text.failed) {
        owf_buf_free(&text);
        return OWF_ERR_MEMORY;
    }
    p = text.data ? (const char *)text.data : "";
    end = p + text.length;
    if (end - p >= 5 && !strncmp(p, "sep=", 4) && (p + 5 == end || p[5] == '\r' || p[5] == '\n')) {
        sep = (unsigned char)p[4];
        p += 5;
        if (p < end && *p == '\r')
            p++;
        if (p < end && *p == '\n')
            p++;
    } else {
        sep = find_separator(p, end);
    }
    c.sheet = owf_book_add_sheet(book, "Sheet1");
    if (!c.sheet) {
        owf_buf_free(&text);
        return OWF_ERR_MEMORY;
    }
    while (p < end && !c.failed) {
        /* one field */
        field.length = 0;
        if (*p == '"') {
            for (p++; p < end; p++) {
                if (*p == '"') {
                    if (p + 1 < end && p[1] == '"') {
                        owf_buf_putc(&field, '"');
                        p++;
                        continue;
                    }
                    p++;
                    break;
                }
                owf_buf_putc(&field, *p);
            }
            /* anything after the closing quote, up to the separator, is the field's too */
            while (p < end && *p != sep && *p != '\r' && *p != '\n')
                owf_buf_putc(&field, *p++);
        } else {
            const char *start = p;
            while (p < end && *p != sep && *p != '\r' && *p != '\n')
                p++;
            owf_buf_put(&field, start, (size_t)(p - start));
        }
        if (field.failed) {
            c.failed = 1;
            break;
        }
        if (row < MAX_ROWS && col < MAX_COLS)
            put_field(&c, row, col, field.data ? (const char *)field.data : "", field.length);
        else
            cut = 1;
        if (p < end && *p == sep) {
            p++;
            col++;
            if (p == end)                /* a separator last: one more empty field */
                break;
            continue;
        }
        if (p < end && *p == '\r')
            p++;
        if (p < end && *p == '\n')
            p++;
        row++;
        col = 0;
    }
    if (cut)
        owf_report_add(report, OWF_NOTE_LOST, "Lines or fields beyond a sheet's 1048576 rows or 16384 columns were left out");
    result = c.failed ? OWF_ERR_MEMORY : OWF_OK;
    owf_buf_free(&text);
    owf_buf_free(&field);
    return result;
}

/* Text that is not another format's: no NULs (but UTF-16's), no zip,
 * compound file or XML. A low score: any format that knows its file wins. */
static int detect_csv(const unsigned char *data, size_t length)
{
    size_t n = length < 4096 ? length : 4096, i;
    int seps = 0;
    if (length >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF)))
        return 10;
    if (!length || (length >= 2 && data[0] == 'P' && data[1] == 'K') || (length >= 4 && !memcmp(data, "\xD0\xCF\x11\xE0", 4)))
        return 0;
    for (i = 0; i < n; i++) {
        if (!data[i])
            return 0;
        seps += data[i] == ',' || data[i] == ';' || data[i] == '\t';
    }
    for (i = 0; i < n && (data[i] == ' ' || data[i] == '\r' || data[i] == '\n' || data[i] == '\t'); i++)
        ;
    if (i < n && data[i] == '<')
        return 1;                        /* XML or HTML, most likely */
    return seps ? 15 : 10;
}

int owf_import_csv(const unsigned char *data, size_t length, owf_book *book, owf_report *report)
{
    return import_csv(data, length, book, report);
}

int owf_detect_csv(const unsigned char *data, size_t length)
{
    return detect_csv(data, length);
}

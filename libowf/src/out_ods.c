/*
 * libowf: OpenDocument spreadsheets, written to OASIS ODF 1.2 as a package
 * (.ods: mimetype first and stored, the manifest, meta.xml, settings.xml,
 * styles.xml, content.xml) or as one flat XML file (.fods). Everything the
 * model holds goes in: sheets, cells with their values (dates and times as
 * ODF's own, booleans, errors as LibreOffice marks them), formulas in
 * OpenFormula with their last results, cell styles (fonts, sizes, flags,
 * colours, number formats as ODF data styles, alignment, wrap, indents,
 * backgrounds, borders), column widths, row heights, hidden rows, columns
 * and sheets, merged cells, array formulas, named ranges and expressions,
 * the 1904 date system, and in settings.xml the active sheet and frozen
 * panes.
 *
 * ODF has no place for some of what a workbook says the way Excel says it:
 * a number format's own text and a sheet's default column width and row
 * height. Those go in attributes of libowf's own namespace beside ODF's,
 * which other programs ignore, as ODF lets them, and which libowf reads
 * back, so that a workbook goes through ODS unchanged.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_GFO "urn:org:amigachrome:names:experimental:gfoffice:xmlns:calc:1.0"
#define NS_DECLS \
    "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" " \
    "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" " \
    "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" " \
    "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" " \
    "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" " \
    "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" " \
    "xmlns:number=\"urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0\" " \
    "xmlns:of=\"urn:oasis:names:tc:opendocument:xmlns:of:1.2\" " \
    "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" " \
    "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" " \
    "xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\" " \
    "xmlns:calcext=\"urn:org:documentfoundation:names:experimental:calc:xmlns:calcext:1.0\" " \
    "xmlns:gfocalc=\"" NS_GFO "\" office:version=\"1.2\""
#define MIME "application/vnd.oasis.opendocument.spreadsheet"
#define XML_HEAD "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"

#define MAX_ROWS 1048576UL
#define MAX_COLS 16384UL

/* ---- XML text ---- */

/* UTF-8 for XML: &, <, > and " escaped; tabs and line feeds in attributes
 * as references; characters XML 1.0 cannot carry left out (*dropped
 * counts them); bad UTF-8 as U+FFFD. */
static void put_xml(owf_buf *b, const char *s, size_t n, int attribute, int *dropped)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + n;
    while (p < end) {
        unsigned c = *p;
        if (c >= 0x80) {
            const unsigned char *start = p;
            unsigned long cp = owf_utf8_next(&p, end);
            if (cp == 0xFFFD && !(p - start == 3 && start[0] == 0xEF && start[1] == 0xBF && start[2] == 0xBD))
                owf_buf_puts(b, "\xEF\xBF\xBD");
            else if (cp == 0xFFFE || cp == 0xFFFF)
                (*dropped)++;
            else
                owf_buf_put(b, start, (size_t)(p - start));
            continue;
        }
        p++;
        switch (c) {
        case '&': owf_buf_puts(b, "&amp;"); break;
        case '<': owf_buf_puts(b, "&lt;"); break;
        case '>': owf_buf_puts(b, "&gt;"); break;
        case '"': owf_buf_puts(b, attribute ? "&quot;" : "\""); break;
        case '\t':
        case '\n':
        case '\r':
            if (attribute)
                owf_buf_printf(b, "&#%u;", c);
            else if (c == '\r')
                owf_buf_puts(b, "&#13;");
            else
                owf_buf_putc(b, (int)c);
            break;
        default:
            if (c < 0x20)
                (*dropped)++;
            else
                owf_buf_putc(b, (int)c);
        }
    }
}

typedef struct {
    const owf_book *book;
    owf_report *report;
    int dropped;                 /* characters XML cannot hold */
    int *col_widths;             /* the column styles' widths */
    int ncolstyles, capcolstyles;
    int (*row_styles)[2];        /* the row styles' height and hidden */
    int nrowstyles, caprowstyles;
    int val_base;                /* the sheet being written: its first rule is "val<val_base + 1>" */
    int failed;
} ods_out;

static void attr(ods_out *w, owf_buf *b, const char *s)
{
    put_xml(b, s, strlen(s), 1, &w->dropped);
}

static void put_number(owf_buf *b, double v)
{
    char num[40];
    owf_number_exact(v, num);
    owf_buf_puts(b, num);
}

/* twips as points: "12.75pt" */
static void put_pt(owf_buf *b, long twips)
{
    if (twips < 0) {
        owf_buf_putc(b, '-');
        twips = -twips;
    }
    owf_buf_printf(b, "%ld", twips / 20);
    if (twips % 20)
        owf_buf_printf(b, ".%02ld", twips % 20 * 5);
    owf_buf_puts(b, "pt");
}

/* ---- Number formats: Excel's text as ODF's elements ---- */

typedef struct {
    const char *p;               /* the section */
    size_t n;
} section;

static int split_sections(const char *code, section *out, int max)
{
    const char *p = code, *start = code;
    int count = 0;
    while (*p && count < max) {
        if (*p == '"') {
            for (p++; *p && *p != '"'; p++)
                ;
            if (*p)
                p++;
            continue;
        }
        if ((*p == '\\' || *p == '_' || *p == '*') && p[1]) {
            p += 2;
            continue;
        }
        if (*p == '[') {
            const char *close = strchr(p, ']');
            p = close ? close + 1 : p + 1;
            continue;
        }
        if (*p == ';') {
            out[count].p = start;
            out[count++].n = (size_t)(p - start);
            start = ++p;
            continue;
        }
        p++;
    }
    if (count < max) {
        out[count].p = start;
        out[count++].n = (size_t)(p - start);
    }
    return count;
}

static int lower(int c)
{
    return owf_lower(c);
}

/* What a section is: 2 date or time (1 for time alone), 0 a number, 3 text
 * with "@". */
static int section_kind(const section *s, int *time_only)
{
    size_t i;
    int date = 0, time = 0, m = 0, digits = 0, at = 0;
    for (i = 0; i < s->n; i++) {
        int c = lower((unsigned char)s->p[i]);
        if (s->p[i] == '"') {
            for (i++; i < s->n && s->p[i] != '"'; i++)
                ;
        } else if ((s->p[i] == '\\' || s->p[i] == '_' || s->p[i] == '*') && i + 1 < s->n) {
            i++;
        } else if (s->p[i] == '[') {
            size_t j = i + 1;
            int lc = j < s->n ? lower((unsigned char)s->p[j]) : 0;
            if (lc == 'h' || lc == 'm' || lc == 's')
                time = 1;
            while (j < s->n && s->p[j] != ']')
                j++;
            i = j;
        } else if (c == 'y' || c == 'd' || (c == 'e' && !digits) || c == 'g' || c == 'b') {
            date = 1;
        } else if (c == 'h' || c == 's') {
            time = 1;
        } else if (c == 'm') {
            m = 1;
        } else if (c == 'a' && i + 4 < s->n && !strncmp(s->p + i, "AM/PM", 5)) {
            time = 1;
            i += 4;
        } else if (s->p[i] == '0' || s->p[i] == '#' || s->p[i] == '?') {
            digits = 1;
        } else if (s->p[i] == '@') {
            at = 1;
        }
    }
    *time_only = (time || m) && !date && (time || !m);
    if (date || time || m)
        return 2;
    if (at)
        return 3;
    return 0;
}

static const struct { const char *name; unsigned long rgb; } colour_names[8] = {
    { "black", 0x000000 }, { "blue", 0x0000FF }, { "cyan", 0x00FFFF }, { "green", 0x00FF00 },
    { "magenta", 0xFF00FF }, { "red", 0xFF0000 }, { "white", 0xFFFFFF }, { "yellow", 0xFFFF00 }
};

typedef struct {
    owf_buf body;                /* the style's children */
    owf_buf text;                /* literal text waiting */
    int kind;                    /* 0 number, 1 percent, 2 currency, 3 date, 4 time, 5 text, 6 boolean */
    int elapsed;
    long colour;                 /* -1 none */
    int approx;
} numstyle;

static void flush_text(ods_out *w, numstyle *ns)
{
    if (!ns->text.length)
        return;
    owf_buf_puts(&ns->body, "<number:text>");
    put_xml(&ns->body, (const char *)ns->text.data, ns->text.length, 0, &w->dropped);
    owf_buf_puts(&ns->body, "</number:text>");
    ns->text.length = 0;
}

/* The digits part of a number section: "#,##0.00", "0.0E+00", "# ?/?". */
static void number_part(numstyle *ns, const char *p, size_t n, int *percent)
{
    size_t i;
    int ints = 0, int_any = 0, grouping = 0, decimals = 0, min_dec = 0, in_dec = 0, commas = 0, exp = 0,
        exp_digits = 0, exp_plus = 1, frac = 0, num_digits = 0, den_digits = 0, den_value = 0, frac_int = -1;
    int saw_digit_after_comma = 0;
    for (i = 0; i < n; i++) {
        char c = p[i];
        if (exp) {
            if (c == '0' || c == '#' || c == '?')
                exp_digits++;
            continue;
        }
        if (frac) {
            if (frac == 1 && (c == '?' || c == '0' || c == '#'))
                num_digits++;
            else if (c == '/')
                frac = 2;
            else if (frac == 2 && (c == '?' || c == '0' || c == '#'))
                den_digits++;
            else if (frac == 2 && c >= '1' && c <= '9')
                den_value = den_value * 10 + (c - '0') < 100000 ? den_value * 10 + (c - '0') : den_value;
            else if (frac == 2 && c == '0' && den_value)
                den_value *= 10;
            continue;
        }
        if (c == '%') {
            *percent = 1;
        } else if (c == '.') {
            in_dec = 1;
        } else if (c == ',') {
            commas++;                    /* between digits: grouping; after the last: scaling */
            saw_digit_after_comma = 0;
        } else if (c == 'E' || c == 'e') {
            exp = 1;
            exp_plus = i + 1 < n && p[i + 1] == '+';
            i++;
        } else if (c == '/') {
            frac = 2;
        } else if (c == ' ' && !in_dec) {
            /* "# ?/?": the whole number, then the fraction */
            frac_int = ints;
            ints = 0;
            frac = 1;
        } else if (c == '0' || c == '#' || c == '?') {
            if (in_dec) {
                decimals++;
                commas = 0;
                if (c == '0')
                    min_dec = decimals;
            } else {
                if (commas)
                    grouping = 1;
                commas = 0;
                int_any = 1;
                if (c == '0')
                    ints++;
                saw_digit_after_comma = 1;
            }
        }
    }
    (void)saw_digit_after_comma;
    if (frac) {
        owf_buf_puts(&ns->body, "<number:fraction");
        if (frac_int >= 0)
            owf_buf_printf(&ns->body, " number:min-integer-digits=\"%d\"", frac_int);
        owf_buf_printf(&ns->body, " number:min-numerator-digits=\"%d\"", num_digits ? num_digits : 1);
        if (den_value)
            owf_buf_printf(&ns->body, " number:denominator-value=\"%d\"", den_value);
        else
            owf_buf_printf(&ns->body, " number:min-denominator-digits=\"%d\"", den_digits ? den_digits : 1);
        owf_buf_puts(&ns->body, "/>");
        return;
    }
    if (exp) {
        int interval = 1;
        size_t k;
        int placeholders = 0;
        for (k = 0; k < n && p[k] != '.' && p[k] != 'E' && p[k] != 'e'; k++)
            if (p[k] == '0' || p[k] == '#' || p[k] == '?')
                placeholders++;
        if (placeholders > 1 && memchr(p, '#', n))
            interval = placeholders;
        owf_buf_printf(&ns->body, "<number:scientific-number number:decimal-places=\"%d\" number:min-decimal-places=\"%d\""
                                  " number:min-integer-digits=\"%d\" number:min-exponent-digits=\"%d\"",
                       decimals, min_dec, ints ? ints : 1, exp_digits ? exp_digits : 1);
        if (interval > 1)
            owf_buf_printf(&ns->body, " number:exponent-interval=\"%d\"", interval);
        if (!exp_plus)
            owf_buf_puts(&ns->body, " number:forced-exponent-sign=\"false\"");
        owf_buf_puts(&ns->body, "/>");
        return;
    }
    (void)int_any;
    owf_buf_printf(&ns->body, "<number:number number:decimal-places=\"%d\" number:min-decimal-places=\"%d\""
                              " number:min-integer-digits=\"%d\"", decimals, min_dec, ints);
    if (grouping)
        owf_buf_puts(&ns->body, " number:grouping=\"true\"");
    if (commas) {
        long factor = 1;
        while (commas-- > 0 && factor < 1000000000L)
            factor *= 1000;
        owf_buf_printf(&ns->body, " number:display-factor=\"%ld\"", factor);
    }
    owf_buf_puts(&ns->body, "/>");
}

/* One section as an ODF data style's children. */
static void section_style(ods_out *w, const section *s, numstyle *ns)
{
    const char *p = s->p, *end = s->p + s->n;
    int time_only, kind, percent = 0, number_done = 0, last_hours = 0;
    owf_buf_init(&ns->body);
    owf_buf_init(&ns->text);
    ns->colour = -1;
    ns->elapsed = 0;
    ns->approx = 0;
    ns->kind = 0;
    if (s->n == 7 && owf_lower((unsigned char)p[0]) == 'g' && !strncmp(p + 1, "eneral", 6)) {
        owf_buf_puts(&ns->body, "<number:number number:min-integer-digits=\"1\"/>");
        return;
    }
    kind = section_kind(s, &time_only);
    ns->kind = kind == 2 ? (time_only ? 4 : 3) : kind == 3 ? 5 : 0;
    while (p < end) {
        int c = (unsigned char)*p, lc = lower(c);
        size_t run = 1;
        if (c == '"') {
            const char *q = p + 1;
            while (q < end && *q != '"')
                q++;
            owf_buf_put(&ns->text, p + 1, (size_t)(q - p - 1));
            p = q < end ? q + 1 : q;
            continue;
        }
        if (c == '\\' && p + 1 < end) {
            owf_buf_putc(&ns->text, p[1]);
            p += 2;
            continue;
        }
        if (c == '_' && p + 1 < end) {        /* a space as wide as the next character */
            owf_buf_putc(&ns->text, ' ');
            p += 2;
            continue;
        }
        if (c == '*' && p + 1 < end) {        /* fill with the next character: not in ODF 1.2 */
            ns->approx = 1;
            p += 2;
            continue;
        }
        if (c == '[') {
            const char *close = memchr(p, ']', (size_t)(end - p));
            size_t k, len;
            if (!close)
                break;
            len = (size_t)(close - p - 1);
            for (k = 0; k < 8; k++) {
                const char *nm = colour_names[k].name;
                size_t j;
                if (strlen(nm) != len)
                    continue;
                for (j = 0; j < len && lower((unsigned char)p[1 + j]) == nm[j]; j++)
                    ;
                if (j == len)
                    break;
            }
            {
                if (k < 8) {
                    ns->colour = (long)colour_names[k].rgb;
                } else if (len >= 1 && p[1] == '$') {   /* [$€-407] currency, [$-409] place */
                    const char *dash = memchr(p + 2, '-', len - 1);
                    size_t sym = dash ? (size_t)(dash - p - 2) : len - 1;
                    if (sym) {
                        flush_text(w, ns);
                        owf_buf_puts(&ns->body, "<number:currency-symbol>");
                        put_xml(&ns->body, p + 2, sym, 0, &w->dropped);
                        owf_buf_puts(&ns->body, "</number:currency-symbol>");
                        if (!ns->kind)
                            ns->kind = 2;
                    }
                } else if (len && (lower((unsigned char)p[1]) == 'h' || lower((unsigned char)p[1]) == 'm' ||
                                   lower((unsigned char)p[1]) == 's')) {
                    ns->elapsed = 1;
                    flush_text(w, ns);
                    lc = lower((unsigned char)p[1]);
                    owf_buf_printf(&ns->body, "<number:%s%s/>", lc == 'h' ? "hours" : lc == 'm' ? "minutes" : "seconds",
                                   len > 1 ? " number:style=\"long\"" : "");
                    if (lc == 'h')
                        last_hours = 1;
                } else {
                    ns->approx = 1;          /* a condition, [>100]: not kept */
                }
            }
            p = close + 1;
            continue;
        }
        if (kind == 3 && c == '@') {
            flush_text(w, ns);
            owf_buf_puts(&ns->body, "<number:text-content/>");
            p++;
            continue;
        }
        if (kind == 2) {
            while (p + run < end && lower((unsigned char)p[run]) == lc)
                run++;
            if (lc == 'y' || lc == 'e') {
                flush_text(w, ns);
                owf_buf_printf(&ns->body, "<number:year%s/>", run > 2 || lc == 'e' ? " number:style=\"long\"" : "");
            } else if (lc == 'd') {
                flush_text(w, ns);
                if (run >= 3)
                    owf_buf_printf(&ns->body, "<number:day-of-week%s/>", run > 3 ? " number:style=\"long\"" : "");
                else
                    owf_buf_printf(&ns->body, "<number:day%s/>", run > 1 ? " number:style=\"long\"" : "");
            } else if (lc == 'h') {
                flush_text(w, ns);
                owf_buf_printf(&ns->body, "<number:hours%s/>", run > 1 ? " number:style=\"long\"" : "");
                last_hours = 1;
            } else if (lc == 'm') {
                /* minutes after hours or before seconds; a month else */
                const char *q = p + run;
                int minutes = last_hours;
                while (q < end && !minutes && (*q == ':' || *q == ' ' || *q == '.'))
                    q++;
                if (q < end && lower((unsigned char)*q) == 's')
                    minutes = 1;
                flush_text(w, ns);
                if (minutes && run <= 2)
                    owf_buf_printf(&ns->body, "<number:minutes%s/>", run > 1 ? " number:style=\"long\"" : "");
                else
                    owf_buf_printf(&ns->body, "<number:month%s%s/>", run == 2 || run >= 4 ? " number:style=\"long\"" : "",
                                   run >= 3 ? " number:textual=\"true\"" : "");
                last_hours = 0;
            } else if (lc == 's') {
                int decimals = 0;
                const char *q = p + run;
                flush_text(w, ns);
                if (q < end && *q == '.') {
                    const char *r = q + 1;
                    while (r < end && *r == '0') {
                        decimals++;
                        r++;
                    }
                    if (decimals)
                        run = (size_t)(r - p);
                }
                owf_buf_printf(&ns->body, "<number:seconds%s", run > 1 && p[1] != '.' ? " number:style=\"long\"" : "");
                if (decimals)
                    owf_buf_printf(&ns->body, " number:decimal-places=\"%d\"", decimals);
                owf_buf_puts(&ns->body, "/>");
                last_hours = 0;
            } else if (lc == 'a' && end - p >= 5 && !strncmp(p, "AM/PM", 5)) {
                flush_text(w, ns);
                owf_buf_puts(&ns->body, "<number:am-pm/>");
                run = 5;
            } else if (lc == 'a' && end - p >= 3 && (!strncmp(p, "A/P", 3) || !strncmp(p, "a/p", 3))) {
                flush_text(w, ns);
                owf_buf_puts(&ns->body, "<number:am-pm/>");
                run = 3;
            } else if (lc == 'g' || lc == 'b') {
                ns->approx = 1;
            } else {
                owf_buf_put(&ns->text, p, run);
            }
            p += run;
            continue;
        }
        /* a number: its digits, with text before and after */
        if ((c == '0' || c == '#' || c == '?' || c == '.' || (c == ',' && !number_done)) && !number_done) {
            const char *q = p;
            while (q < end && (*q == '0' || *q == '#' || *q == '?' || *q == '.' || *q == ',' || *q == '%' ||
                               *q == 'E' || *q == 'e' || ((*q == '+' || *q == '-') && q > p && (q[-1] == 'E' || q[-1] == 'e')) ||
                               (*q == '/' && q + 1 < end) || (*q >= '1' && *q <= '9') ||
                               (*q == ' ' && q + 1 < end && (q[1] == '?' || q[1] == '#' || (q[1] >= '0' && q[1] <= '9')) &&
                                memchr(q, '/', (size_t)(end - q)))))
                q++;
            while (q > p && q[-1] == '%') {
                percent = 1;
                q--;
            }
            flush_text(w, ns);
            number_part(ns, p, (size_t)(q - p), &percent);
            number_done = 1;
            p = q;
            continue;
        }
        if (c == '%') {
            percent = 1;
            owf_buf_putc(&ns->text, '%');
            p++;
            continue;
        }
        owf_buf_putc(&ns->text, c);
        p++;
    }
    flush_text(w, ns);
    if (percent && !ns->kind)
        ns->kind = 1;
    if (percent && ns->kind == 1 && !strstr((const char *)(ns->body.data ? ns->body.data : (unsigned char *)""), "%</number:text>"))
        owf_buf_puts(&ns->body, "<number:text>%</number:text>");
}

static const char *const style_elements[7] = { "number-style", "percentage-style", "currency-style", "date-style",
                                               "time-style", "text-style", "boolean-style" };

static void open_style(owf_buf *b, const char *name, const numstyle *ns, int is_volatile)
{
    owf_buf_printf(b, "<number:%s style:name=\"%s\"", style_elements[ns->kind], name);
    if (is_volatile)
        owf_buf_puts(b, " style:volatile=\"true\"");
    if (ns->elapsed)
        owf_buf_puts(b, " number:truncate-on-overflow=\"false\"");
}

static void close_style(owf_buf *b, const numstyle *ns)
{
    if (ns->colour >= 0)
        owf_buf_printf(b, "<style:text-properties fo:color=\"#%06lx\"/>", (unsigned long)ns->colour);
}

/* The data style "N<i>" (and "N<i>P0"... for its other sections) for the
 * book's number format i, as LibreOffice lays such styles out. */
static void number_style(ods_out *w, owf_buf *b, int index)
{
    static const char *const conds[3][3] = { { "value()&gt;=0", NULL, NULL }, { "value()&gt;0", "value()&lt;0", NULL },
                                             { "value()&gt;0", "value()&lt;0", "value()=0" } };
    const char *code = w->book->numfmts[index];
    section sec[4];
    numstyle ns[4];
    char name[32];
    int n = split_sections(code, sec, 4), i, approx = 0;
    if (!strcmp(code, "\"TRUE\";\"TRUE\";\"FALSE\"")) {
        owf_buf_printf(b, "<number:boolean-style style:name=\"N%d\" gfocalc:format-code=\"", index);
        attr(w, b, code);
        owf_buf_puts(b, "\"><number:boolean/></number:boolean-style>");
        return;
    }
    for (i = 0; i < n; i++) {
        section_style(w, &sec[i], &ns[i]);
        approx |= ns[i].approx;
    }
    for (i = 0; i + 1 < n; i++) {         /* the sections but the last, mapped */
        snprintf(name, sizeof name, "N%dP%d", index, i);
        open_style(b, name, &ns[i], 1);
        owf_buf_putc(b, '>');
        owf_buf_put(b, ns[i].body.data ? ns[i].body.data : (const unsigned char *)"", ns[i].body.length);
        close_style(b, &ns[i]);
        owf_buf_printf(b, "</number:%s>", style_elements[ns[i].kind]);
    }
    snprintf(name, sizeof name, "N%d", index);
    open_style(b, name, &ns[n - 1], 0);
    owf_buf_puts(b, " gfocalc:format-code=\"");
    attr(w, b, code);
    owf_buf_puts(b, "\">");
    owf_buf_put(b, ns[n - 1].body.data ? ns[n - 1].body.data : (const unsigned char *)"", ns[n - 1].body.length);
    close_style(b, &ns[n - 1]);
    for (i = 0; i + 1 < n; i++)
        owf_buf_printf(b, "<style:map style:condition=\"%s\" style:apply-style-name=\"N%dP%d\"/>", conds[n - 2][i], index, i);
    owf_buf_printf(b, "</number:%s>", style_elements[ns[n - 1].kind]);
    for (i = 0; i < n; i++) {
        owf_buf_free(&ns[i].body);
        owf_buf_free(&ns[i].text);
    }
    if (approx)
        owf_report_add(w->report, OWF_NOTE_APPROX, "Some parts of number formats (conditions, fill characters, eras) are shown approximately by other programs");
}

/* ---- Fonts and cell styles ---- */

static void font_faces(ods_out *w, owf_buf *b)
{
    static const char *const generic[4] = { NULL, "roman", "swiss", "modern" };
    int i;
    owf_buf_puts(b, "<office:font-face-decls>");
    for (i = 0; i < w->book->nfonts; i++) {
        const owf_font *f = &w->book->fonts[i];
        owf_buf_puts(b, "<style:font-face style:name=\"");
        attr(w, b, f->name);
        owf_buf_puts(b, "\" svg:font-family=\"");
        if (strchr(f->name, ' ')) {
            owf_buf_puts(b, "&apos;");
            attr(w, b, f->name);
            owf_buf_puts(b, "&apos;");
        } else {
            attr(w, b, f->name);
        }
        owf_buf_putc(b, '"');
        if (f->kind > 0 && f->kind < 4)
            owf_buf_printf(b, " style:font-family-generic=\"%s\"", generic[f->kind]);
        owf_buf_puts(b, "/>");
    }
    owf_buf_puts(b, "</office:font-face-decls>");
}

static const char *const border_lines[14] = {
    "none", "0.74pt solid", "1.76pt solid", "0.74pt dashed", "0.74pt dotted", "2.49pt solid", "1.76pt double",
    "0.06pt solid", "1.76pt dashed", "0.74pt dash-dot", "1.76pt dash-dot", "0.74pt dash-dot-dot", "1.76pt dash-dot-dot",
    "1.76pt fine-dashed"
};

/* A cell style's properties, all of them, so that a style says the same
 * whatever the default is. */
static void cell_properties(ods_out *w, owf_buf *b, const owf_cellstyle *s)
{
    static const char *const sides[4] = { "left", "right", "top", "bottom" };
    static const char *const valign[3] = { "top", "middle", "bottom" };
    const owf_book *book = w->book;
    int k;
    owf_buf_puts(b, "<style:table-cell-properties");
    if (OWF_SHADE_SET(s->fill))
        owf_buf_printf(b, " fo:background-color=\"#%06lx\"", OWF_SHADE_RGB(s->fill));
    else
        owf_buf_puts(b, " fo:background-color=\"transparent\"");
    for (k = 0; k < 4; k++) {
        int line = s->border[k] <= OWF_LINE_SLANTED_DASH_DOT ? s->border[k] : OWF_LINE_THIN;
        owf_buf_printf(b, " fo:border-%s=\"%s", sides[k], border_lines[line]);
        if (line && OWF_SHADE_SET(s->border_colour[k]))
            owf_buf_printf(b, " #%06lx", OWF_SHADE_RGB(s->border_colour[k]));
        owf_buf_putc(b, '"');
    }
    owf_buf_printf(b, " style:vertical-align=\"%s\" fo:wrap-option=\"%s\" style:text-align-source=\"%s\" style:repeat-content=\"%s\"/>",
                   valign[s->valign < 3 ? s->valign : 2], s->wrap ? "wrap" : "no-wrap",
                   s->halign == OWF_HALIGN_GENERAL ? "value-type" : "fix", s->halign == OWF_HALIGN_FILL ? "true" : "false");
    owf_buf_puts(b, "<style:paragraph-properties");
    if (s->halign != OWF_HALIGN_GENERAL) {
        static const char *const align[7] = { "start", "start", "center", "end", "start", "justify", "center" };
        owf_buf_printf(b, " fo:text-align=\"%s\"", align[s->halign < 7 ? s->halign : 0]);
        if (s->halign == OWF_HALIGN_CENTRE_ACROSS)
            owf_report_add(w->report, OWF_NOTE_APPROX, "Text centred across the selection is centred in its cell");
    }
    owf_buf_puts(b, " fo:margin-left=\"");
    put_pt(b, (long)s->indent * 150);
    owf_buf_puts(b, "\"/><style:text-properties");
    if (s->font.font >= 0 && s->font.font < book->nfonts) {
        owf_buf_puts(b, " style:font-name=\"");
        attr(w, b, book->fonts[s->font.font].name);
        owf_buf_putc(b, '"');
    }
    if (s->font.size > 0) {
        owf_buf_puts(b, " fo:font-size=\"");
        put_pt(b, s->font.size);
        owf_buf_putc(b, '"');
    }
    owf_buf_printf(b, " fo:font-weight=\"%s\" fo:font-style=\"%s\" style:text-underline-style=\"%s\"",
                   s->font.flags & OWF_BOLD ? "bold" : "normal", s->font.flags & OWF_ITALIC ? "italic" : "normal",
                   s->font.flags & OWF_UNDERLINE ? "solid" : "none");
    if (s->font.flags & OWF_UNDERLINE)
        owf_buf_puts(b, " style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"");
    owf_buf_printf(b, " style:text-line-through-style=\"%s\" style:text-position=\"%s\"",
                   s->font.flags & OWF_STRIKE ? "solid" : "none",
                   s->font.flags & OWF_SUPER ? "super 58%" : s->font.flags & OWF_SUB ? "sub 58%" : "0% 100%");
    if (s->font.colour == OWF_COLOUR_AUTO)
        owf_buf_puts(b, " style:use-window-font-color=\"true\"/>");
    else
        owf_buf_printf(b, " fo:color=\"#%06lx\"/>", s->font.colour & 0xFFFFFFUL);
}

static int used_numfmt(const owf_book *book, int index)
{
    int i;
    for (i = 0; i < book->nstyles; i++)
        if (book->styles[i].number_format == index)
            return 1;
    return 0;
}

/* office:styles: the default cell style (the book's style 0), its number
 * format, and the defaults of columns and rows. */
/* ---- Conditional formatting's looks ---- */

static int cf_has_look(const owf_condfmt *cf)
{
    return cf->font_set || cf->font_colour || cf->fill;
}

/* A rule's number in the book (its look's style is "GfoCondition<n>"): the sheets' rules before it, and it. */
static int cf_number(const owf_book *book, int sheet, int index)
{
    int s, n = 0;
    for (s = 0; s < sheet; s++)
        n += book->sheets[s].ncondfmts;
    return n + index + 1;
}

/* Each rule's look as a cell style of its own, which its condition names. */
static void cf_styles(ods_out *w, owf_buf *b)
{
    const owf_book *book = w->book;
    int s, i;
    for (s = 0; s < book->nsheets; s++)
        for (i = 0; i < book->sheets[s].ncondfmts; i++) {
            const owf_condfmt *cf = &book->sheets[s].condfmts[i];
            if (!cf_has_look(cf))
                continue;
            owf_buf_printf(b, "<style:style style:name=\"GfoCondition%d\" style:family=\"table-cell\" "
                              "style:parent-style-name=\"Default\">", cf_number(book, s, i));
            if (cf->fill)
                owf_buf_printf(b, "<style:table-cell-properties fo:background-color=\"#%06lx\"/>", cf->fill & 0xFFFFFFUL);
            if (cf->font_colour || cf->font_set) {
                owf_buf_puts(b, "<style:text-properties");
                if (cf->font_colour)
                    owf_buf_printf(b, " fo:color=\"#%06lx\"", cf->font_colour & 0xFFFFFFUL);
                if (cf->font_set & OWF_BOLD)
                    owf_buf_printf(b, " fo:font-weight=\"%s\"", cf->font_flags & OWF_BOLD ? "bold" : "normal");
                if (cf->font_set & OWF_ITALIC)
                    owf_buf_printf(b, " fo:font-style=\"%s\"", cf->font_flags & OWF_ITALIC ? "italic" : "normal");
                if (cf->font_set & OWF_UNDERLINE)
                    owf_buf_puts(b, cf->font_flags & OWF_UNDERLINE ? " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" "
                                                                     "style:text-underline-color=\"font-color\""
                                                                   : " style:text-underline-style=\"none\"");
                if (cf->font_set & OWF_STRIKE)
                    owf_buf_printf(b, " style:text-line-through-style=\"%s\"", cf->font_flags & OWF_STRIKE ? "solid" : "none");
                owf_buf_puts(b, "/>");
            }
            owf_buf_puts(b, "</style:style>");
        }
}

static void common_styles(ods_out *w, owf_buf *b)
{
    const owf_book *book = w->book;
    const owf_cellstyle *s0 = book->nstyles ? &book->styles[0] : NULL;
    int width = book->nsheets ? book->sheets[book->active >= 0 && book->active < book->nsheets ? book->active : 0].default_width : 960;
    int height = book->nsheets ? book->sheets[book->active >= 0 && book->active < book->nsheets ? book->active : 0].default_height : 255;
    owf_buf_puts(b, "<office:styles>");
    if (s0 && s0->number_format > 0 && s0->number_format < book->nnumfmts)
        number_style(w, b, s0->number_format);
    owf_buf_puts(b, "<style:default-style style:family=\"table-column\"><style:table-column-properties style:column-width=\"");
    put_pt(b, width > 0 ? width : 960);
    owf_buf_puts(b, "\"/></style:default-style><style:default-style style:family=\"table-row\"><style:table-row-properties style:row-height=\"");
    put_pt(b, height > 0 ? height : 255);
    owf_buf_puts(b, "\" style:use-optimal-row-height=\"true\"/></style:default-style>");
    owf_buf_puts(b, "<style:default-style style:family=\"table-cell\"");
    if (s0 && s0->number_format > 0 && s0->number_format < book->nnumfmts)
        owf_buf_printf(b, " style:data-style-name=\"N%d\"", s0->number_format);
    owf_buf_putc(b, '>');
    if (s0)
        cell_properties(w, b, s0);
    owf_buf_puts(b, "</style:default-style><style:style style:name=\"Default\" style:family=\"table-cell\"/>");
    cf_styles(w, b);
    owf_buf_puts(b, "</office:styles>");
}

static int column_style(ods_out *w, int width)
{
    int i;
    for (i = 0; i < w->ncolstyles; i++)
        if (w->col_widths[i] == width)
            return i;
    if (w->ncolstyles == w->capcolstyles) {
        int want = w->capcolstyles ? w->capcolstyles * 2 : 16;
        int *p = realloc(w->col_widths, (size_t)want * sizeof *p);
        if (!p) {
            w->failed = 1;
            return 0;
        }
        w->col_widths = p;
        w->capcolstyles = want;
    }
    w->col_widths[w->ncolstyles] = width;
    return w->ncolstyles++;
}

static int row_style(ods_out *w, int height, int custom)
{
    int i;
    for (i = 0; i < w->nrowstyles; i++)
        if (w->row_styles[i][0] == height && w->row_styles[i][1] == custom)
            return i;
    if (w->nrowstyles == w->caprowstyles) {
        int want = w->caprowstyles ? w->caprowstyles * 2 : 16;
        int (*p)[2] = realloc(w->row_styles, (size_t)want * sizeof *p);
        if (!p) {
            w->failed = 1;
            return 0;
        }
        w->row_styles = p;
        w->caprowstyles = want;
    }
    w->row_styles[w->nrowstyles][0] = height;
    w->row_styles[w->nrowstyles][1] = custom;
    return w->nrowstyles++;
}

/* Every column and row style the sheets need, numbered in a first pass. */
static void collect_layout(ods_out *w)
{
    int i, k;
    for (i = 0; i < w->book->nsheets; i++) {
        const owf_sheet *sh = &w->book->sheets[i];
        column_style(w, sh->default_width);
        row_style(w, sh->default_height, 0);
        for (k = 0; k < sh->ncols; k++)
            column_style(w, sh->cols[k].width);
        for (k = 0; k < sh->nrows; k++)
            row_style(w, sh->rows[k].height, 1);
    }
}

/* A style with General needs to say so when the default has another
 * format. */
static int general_needed(const owf_book *book)
{
    int i;
    if (!book->nstyles || book->styles[0].number_format <= 0)
        return 0;
    for (i = 1; i < book->nstyles; i++)
        if (book->styles[i].number_format == 0)
            return 1;
    return 0;
}

static void automatic_styles(ods_out *w, owf_buf *b)
{
    const owf_book *book = w->book;
    int i;
    owf_buf_puts(b, "<office:automatic-styles>");
    if (general_needed(book))
        owf_buf_puts(b, "<number:number-style style:name=\"N0\" gfocalc:format-code=\"General\">"
                        "<number:number number:min-integer-digits=\"1\"/></number:number-style>");
    for (i = 1; i < book->nnumfmts; i++)
        if (used_numfmt(book, i) && !(book->nstyles && book->styles[0].number_format == i))
            number_style(w, b, i);
    for (i = 0; i < w->ncolstyles; i++) {
        owf_buf_printf(b, "<style:style style:name=\"co%d\" style:family=\"table-column\"><style:table-column-properties"
                          " fo:break-before=\"auto\" style:column-width=\"", i + 1);
        put_pt(b, w->col_widths[i] > 0 ? w->col_widths[i] : 0);
        owf_buf_puts(b, "\"/></style:style>");
    }
    for (i = 0; i < w->nrowstyles; i++) {
        owf_buf_printf(b, "<style:style style:name=\"ro%d\" style:family=\"table-row\"><style:table-row-properties"
                          " style:row-height=\"", i + 1);
        put_pt(b, w->row_styles[i][0] > 0 ? w->row_styles[i][0] : 0);
        owf_buf_printf(b, "\" fo:break-before=\"auto\" style:use-optimal-row-height=\"%s\"/></style:style>",
                       w->row_styles[i][1] ? "false" : "true");
    }
    owf_buf_puts(b, "<style:style style:name=\"ta1\" style:family=\"table\"><style:table-properties table:display=\"true\"/></style:style>"
                    "<style:style style:name=\"ta2\" style:family=\"table\"><style:table-properties table:display=\"false\"/></style:style>");
    for (i = 1; i < book->nstyles; i++) {
        const owf_cellstyle *s = &book->styles[i];
        owf_buf_printf(b, "<style:style style:name=\"ce%d\" style:family=\"table-cell\" style:parent-style-name=\"Default\"", i);
        if (s->number_format > 0 && s->number_format < book->nnumfmts)
            owf_buf_printf(b, " style:data-style-name=\"N%d\"", s->number_format);
        else if (general_needed(book))
            owf_buf_puts(b, " style:data-style-name=\"N0\"");   /* General, not the default's format */
        owf_buf_putc(b, '>');
        cell_properties(w, b, s);
        owf_buf_puts(b, "</style:style>");
    }
    owf_buf_puts(b, "</office:automatic-styles>");
}

/* ---- Cells ---- */

/* Text as paragraphs: runs of spaces as text:s, tabs as text:tab, each
 * line its own paragraph, as LibreOffice writes cells. */
static void put_paragraphs(ods_out *w, owf_buf *b, const char *s)
{
    const char *p = s;
    owf_buf_puts(b, "<text:p>");
    while (*p) {
        if (*p == ' ') {
            int n = 0, at_start = p == s || p[-1] == '\n';
            while (p[n] == ' ')
                n++;
            if (!at_start) {
                owf_buf_putc(b, ' ');
                n--;
            }
            if (n == 1)
                owf_buf_puts(b, "<text:s/>");
            else if (n > 1)
                owf_buf_printf(b, "<text:s text:c=\"%d\"/>", n);
            p += at_start ? n : n + 1;
            continue;
        }
        if (*p == '\t') {
            owf_buf_puts(b, "<text:tab/>");
            p++;
            continue;
        }
        if (*p == '\r' || *p == '\n') {
            if (*p == '\r') {
                owf_report_add(w->report, OWF_NOTE_APPROX, "Carriage returns in cells' text became line breaks");
                if (p[1] == '\n')
                    p++;
            }
            owf_buf_puts(b, "</text:p><text:p>");
            p++;
            continue;
        }
        {
            const char *q = p;
            while (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n')
                q++;
            put_xml(b, p, (size_t)(q - p), 0, &w->dropped);
            p = q;
        }
    }
    owf_buf_puts(b, "</text:p>");
}

static int numfmt_kind_of(const owf_book *book, int style)
{
    int f = style >= 0 && style < book->nstyles ? book->styles[style].number_format : 0;
    return f > 0 && f < book->nnumfmts ? owf_numfmt_kind(book->numfmts[f]) : OWF_NUMFMT_NUMBER;
}

static int same_array(const owf_sheet *sh, const owf_cell *c, unsigned long row, unsigned long col)
{
    const owf_cell *o = owf_sheet_find(sh, (unsigned)row, (unsigned)col);
    return o && (o->flags & OWF_CELL_ARRAY) && o->formula && !strcmp(o->formula, c->formula);
}

/* An array formula's block, from its first cell; 0 for the others. */
static int array_size(const owf_sheet *sh, const owf_cell *c, unsigned long *rows, unsigned long *cols)
{
    unsigned long w = 1, h = 1, k;
    if ((c->col && same_array(sh, c, c->row, c->col - 1)) || (c->row && same_array(sh, c, c->row - 1, c->col)))
        return 0;
    while (c->col + w < MAX_COLS && same_array(sh, c, c->row, c->col + w))
        w++;
    for (;;) {
        if (c->row + h >= MAX_ROWS)
            break;
        for (k = 0; k < w && same_array(sh, c, c->row + h, c->col + k); k++)
            ;
        if (k < w)
            break;
        h++;
    }
    *rows = h;
    *cols = w;
    return 1;
}

/* The style a column gives its cells, 0 for none. */
/* A row's own cell style (owf_rowinfo.style), or 0. */
static int row_cell_style(const owf_sheet *sh, unsigned row)
{
    int lo = 0, hi = sh->nrows;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (sh->rows[mid].row < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < sh->nrows && sh->rows[lo].row == row ? sh->rows[lo].style : 0;
}

static int column_cell_style(const owf_sheet *sh, unsigned long col)
{
    int i;
    for (i = sh->ncols - 1; i >= 0; i--)
        if (col >= sh->cols[i].first && col <= sh->cols[i].last)
            return sh->cols[i].style;
    return 0;
}

/* A comment, as the first thing in its cell. */
static void put_annotation(ods_out *w, owf_buf *b, const owf_comment *note)
{
    owf_buf_puts(b, "<office:annotation>");
    if (note->author && *note->author) {
        owf_buf_puts(b, "<dc:creator>");
        put_xml(b, note->author, strlen(note->author), 0, &w->dropped);
        owf_buf_puts(b, "</dc:creator>");
    }
    put_paragraphs(w, b, note->text ? note->text : "");
    owf_buf_puts(b, "</office:annotation>");
}

/* The end of a cell whose attributes are written: with its comment, or none. */
static void close_cell(ods_out *w, owf_buf *b, const owf_comment *note, int covered)
{
    if (!note) {
        owf_buf_puts(b, "/>");
        return;
    }
    owf_buf_putc(b, '>');
    put_annotation(w, b, note);
    owf_buf_puts(b, covered ? "</table:covered-table-cell>" : "</table:table-cell>");
}

/* The number of the rule of the cell at row, col (its table:content-validation-name "val<n>"), 0 none. */
static int val_at(const ods_out *w, const owf_sheet *sh, unsigned long row, unsigned long col)
{
    const owf_validation *v = sh->nvalidations ? owf_sheet_validation(sh, (unsigned)row, (unsigned)col) : NULL;
    return v ? w->val_base + (int)(v - sh->validations) + 1 : 0;
}

static void put_cell(ods_out *w, owf_buf *b, const owf_sheet *sh, const owf_cell *c, const owf_range *merge, int covered,
                     int val, const owf_comment *note)
{
    char *of = NULL;
    int kind = numfmt_kind_of(w->book, c ? c->style : 0);
    owf_buf_puts(b, covered ? "<table:covered-table-cell" : "<table:table-cell");
    if (val)
        owf_buf_printf(b, " table:content-validation-name=\"val%d\"", val);
    if (!c) {
        if (merge && !covered)
            owf_buf_printf(b, " table:number-columns-spanned=\"%u\" table:number-rows-spanned=\"%u\"",
                           merge->last_col - merge->first_col + 1, merge->last_row - merge->first_row + 1);
        close_cell(w, b, note, covered);
        return;
    }
    if (c->style > 0 && c->style < w->book->nstyles)
        owf_buf_printf(b, " table:style-name=\"ce%d\"", c->style);
    else if (column_cell_style(sh, c->col) > 0 || row_cell_style(sh, c->row) > 0)
        owf_buf_puts(b, " table:style-name=\"Default\"");   /* not the row's or the column's */
    if (merge && !covered)
        owf_buf_printf(b, " table:number-columns-spanned=\"%u\" table:number-rows-spanned=\"%u\"",
                       merge->last_col - merge->first_col + 1, merge->last_row - merge->first_row + 1);
    if (c->formula) {
        unsigned long rows = 1, cols = 1;
        int array = (c->flags & OWF_CELL_ARRAY) != 0, anchor = array ? array_size(sh, c, &rows, &cols) : 1;
        if (anchor) {
            int lost = 0;
            of = owf_excel_to_of_lossy(c->formula, &lost);
            if (lost)
                owf_report_add(w->report, OWF_NOTE_APPROX, "Names of one sheet or of another workbook in formulas lost their sheet or workbook: ODF formulas have no such names");
            if (!of) {
                owf_report_add(w->report, OWF_NOTE_LOST, "Some formulas could not be written; their cells keep their values");
            } else {
                owf_buf_puts(b, " table:formula=\"");
                attr(w, b, of);
                owf_buf_putc(b, '"');
                if (array)
                    owf_buf_printf(b, " table:number-matrix-columns-spanned=\"%lu\" table:number-matrix-rows-spanned=\"%lu\"",
                                   cols, rows);
            }
        }
    }
    switch (c->type) {
    case OWF_VALUE_NUMBER:
        if (kind == OWF_NUMFMT_DATE && c->number >= -693593.0 && c->number < 2958466.0) {
            owf_buf_puts(b, " office:value-type=\"date\" office:date-value=\"");
            owf_put_iso_date(b, c->number + (w->book->date1904 ? 1462 : 0), 0);
            owf_buf_putc(b, '"');
            close_cell(w, b, note, covered);
        } else if (kind == OWF_NUMFMT_TIME && c->number > -1e7 && c->number < 1e7) {
            owf_buf_puts(b, " office:value-type=\"time\" office:time-value=\"");
            owf_put_iso_duration(b, c->number);
            owf_buf_putc(b, '"');
            close_cell(w, b, note, covered);
        } else {
            owf_buf_printf(b, " office:value-type=\"%s\" office:value=\"", kind == OWF_NUMFMT_PERCENT ? "percentage" : "float");
            put_number(b, c->number);
            owf_buf_putc(b, '"');
            close_cell(w, b, note, covered);
        }
        break;
    case OWF_VALUE_BOOL:
        owf_buf_printf(b, " office:value-type=\"boolean\" office:boolean-value=\"%s\"", c->number ? "true" : "false");
        close_cell(w, b, note, covered);
        break;
    case OWF_VALUE_ERROR:
        owf_buf_puts(b, " office:value-type=\"string\" office:string-value=\"\" calcext:value-type=\"error\">");
        if (note)
            put_annotation(w, b, note);
        owf_buf_printf(b, "<text:p>%s</text:p>", owf_xl_error_text(c->error));
        owf_buf_puts(b, covered ? "</table:covered-table-cell>" : "</table:table-cell>");
        break;
    case OWF_VALUE_TEXT:
        owf_buf_puts(b, " office:value-type=\"string\">");
        if (note)
            put_annotation(w, b, note);
        put_paragraphs(w, b, c->text);
        owf_buf_puts(b, covered ? "</table:covered-table-cell>" : "</table:table-cell>");
        break;
    case OWF_VALUE_EMPTY:
        close_cell(w, b, note, covered);
        break;
    }
    free(of);
}

/* The merge whose first cell is at row, col; and whether one covers it. */
static const owf_range *merge_at(const owf_sheet *sh, unsigned long row, unsigned long col, int *covered)
{
    int i;
    *covered = 0;
    for (i = 0; i < sh->nmerges; i++) {
        const owf_range *m = &sh->merges[i];
        if (row < m->first_row || row > m->last_row || col < m->first_col || col > m->last_col)
            continue;
        if (row == m->first_row && col == m->first_col)
            return m;
        *covered = 1;
    }
    return NULL;
}

/* The next column at or after col in row that has a merge to draw: a
 * covered cell or a merge's first cell. */
static unsigned long next_merge_col(const owf_sheet *sh, unsigned long row, unsigned long col)
{
    unsigned long best = MAX_COLS;
    int i;
    for (i = 0; i < sh->nmerges; i++) {
        const owf_range *m = &sh->merges[i];
        if (row < m->first_row || row > m->last_row || m->last_col < col)
            continue;
        if ((m->first_col > col ? m->first_col : col) < best)
            best = m->first_col > col ? m->first_col : col;
    }
    return best;
}

static int row_has_merge(const owf_sheet *sh, unsigned long row)
{
    int i;
    for (i = 0; i < sh->nmerges; i++)
        if (row >= sh->merges[i].first_row && row <= sh->merges[i].last_row)
            return 1;
    return 0;
}

static void put_row_open(ods_out *w, owf_buf *b, const owf_sheet *sh, unsigned long row, unsigned long repeat, int *ri)
{
    int style, hidden = 0, cells = 0;
    while (*ri < sh->nrows && sh->rows[*ri].row < row)
        (*ri)++;
    if (*ri < sh->nrows && sh->rows[*ri].row == row) {
        style = row_style(w, sh->rows[*ri].height, sh->rows[*ri].height != sh->default_height || !sh->rows[*ri].style);
        hidden = sh->rows[*ri].hidden;
        cells = sh->rows[*ri].style;
    } else {
        style = row_style(w, sh->default_height, 0);
    }
    owf_buf_printf(b, "<table:table-row table:style-name=\"ro%d\"", style + 1);
    if (cells > 0 && cells < w->book->nstyles)       /* its own formatting, for its empty cells */
        owf_buf_printf(b, " table:default-cell-style-name=\"ce%d\"", cells);
    if (repeat > 1)
        owf_buf_printf(b, " table:number-rows-repeated=\"%lu\"", repeat);
    if (hidden)
        owf_buf_puts(b, " table:visibility=\"collapse\"");
    owf_buf_putc(b, '>');
}

static void put_columns(ods_out *w, owf_buf *b, const owf_sheet *sh, unsigned long width)
{
    unsigned long col = 0;
    int i, k, n = sh->ncols;
    owf_colinfo *cols = n ? malloc((size_t)n * sizeof *cols) : NULL;
    int def = column_style(w, sh->default_width);
    if (n && !cols) {
        w->failed = 1;
        return;
    }
    if (n)
        memcpy(cols, sh->cols, (size_t)n * sizeof *cols);
    for (i = 1; i < n; i++) {
        owf_colinfo c = cols[i];
        for (k = i; k > 0 && cols[k - 1].first > c.first; k--)
            cols[k] = cols[k - 1];
        cols[k] = c;
    }
    for (i = 0; i < n; i++) {
        unsigned long first = cols[i].first, last = cols[i].last;
        if (first < col)
            first = col;
        if (last >= MAX_COLS)
            last = MAX_COLS - 1;
        if (first > last)
            continue;
        if (first > col)
            owf_buf_printf(b, "<table:table-column table:style-name=\"co%d\" table:number-columns-repeated=\"%lu\"/>",
                           def + 1, first - col);
        owf_buf_printf(b, "<table:table-column table:style-name=\"co%d\"", column_style(w, cols[i].width) + 1);
        if (last > first)
            owf_buf_printf(b, " table:number-columns-repeated=\"%lu\"", last - first + 1);
        if (cols[i].hidden)
            owf_buf_puts(b, " table:visibility=\"collapse\"");
        if (cols[i].style > 0 && cols[i].style < w->book->nstyles)
            owf_buf_printf(b, " table:default-cell-style-name=\"ce%d\"", cols[i].style);
        owf_buf_puts(b, "/>");
        col = last + 1;
    }
    if (col < width)
        owf_buf_printf(b, "<table:table-column table:style-name=\"co%d\" table:number-columns-repeated=\"%lu\"/>", def + 1,
                       width - col);
    else if (!col)
        owf_buf_printf(b, "<table:table-column table:style-name=\"co%d\"/>", def + 1);
    free(cols);
}

static void put_names(ods_out *w, owf_buf *b, int sheet)
{
    const owf_book *book = w->book;
    int i, any = 0;
    for (i = 0; i < book->nnames; i++) {
        const owf_defname *n = &book->names[i];
        char *of;
        size_t len;
        if (n->sheet != sheet)
            continue;
        {
            int lost = 0;
            of = owf_excel_to_of_lossy(n->formula, &lost);
            if (lost)
                owf_report_add(w->report, OWF_NOTE_APPROX, "Names of one sheet or of another workbook in formulas lost their sheet or workbook: ODF formulas have no such names");
        }
        if (!of) {
            owf_report_add(w->report, OWF_NOTE_LOST, "Some defined names could not be written");
            continue;
        }
        if (!any)
            owf_buf_puts(b, "<table:named-expressions>");
        any = 1;
        len = strlen(of);
        if (len > 6 && of[4] == '[' && of[len - 1] == ']' && !memchr(of + 5, '[', len - 6)) {
            owf_buf_puts(b, "<table:named-range table:name=\"");
            attr(w, b, n->name);
            owf_buf_puts(b, "\" table:base-cell-address=\"");
            owf_buf_puts(b, "$");
            attr(w, b, book->nsheets ? book->sheets[sheet >= 0 ? sheet : 0].name : "Sheet1");
            owf_buf_puts(b, ".$A$1\" table:cell-range-address=\"");
            put_xml(b, of + 5, len - 6, 1, &w->dropped);
            owf_buf_puts(b, "\"/>");
        } else {
            owf_buf_puts(b, "<table:named-expression table:name=\"");
            attr(w, b, n->name);
            owf_buf_puts(b, "\" table:base-cell-address=\"$");
            attr(w, b, book->nsheets ? book->sheets[sheet >= 0 ? sheet : 0].name : "Sheet1");
            owf_buf_puts(b, ".$A$1\" table:expression=\"");
            attr(w, b, of);
            owf_buf_puts(b, "\"/>");
        }
        if (n->hidden)
            owf_report_add(w->report, OWF_NOTE_APPROX, "Hidden names are shown: ODF has no hidden names");
        free(of);
    }
    if (any)
        owf_buf_puts(b, "</table:named-expressions>");
}

/* A cell's address on a sheet as ODF writes it: 'My sheet'.D7 (the name quoted when it is not a plain word). */
static void put_address(ods_out *w, owf_buf *b, const char *sheet, unsigned row, unsigned col, int absolute)
{
    const char *p;
    char letters[8];
    int n = 0;
    unsigned c = col + 1;
    int plain = *sheet != 0;
    for (p = sheet; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_'))
            plain = 0;
    if (absolute)
        owf_buf_putc(b, '$');
    if (plain) {
        attr(w, b, sheet);
    } else {
        owf_buf_putc(b, '\'');
        for (p = sheet; *p;) {                   /* the name, its quotes doubled */
            size_t run = strcspn(p, "'");
            put_xml(b, p, run, 1, &w->dropped);
            p += run;
            if (*p == '\'') {
                owf_buf_puts(b, "''");
                p++;
            }
        }
        owf_buf_putc(b, '\'');
    }
    while (c && n < 7) {
        letters[n++] = (char)('A' + (c - 1) % 26);
        c = (c - 1) / 26;
    }
    owf_buf_puts(b, absolute ? ".$" : ".");
    while (n)
        owf_buf_putc(b, letters[--n]);
    owf_buf_printf(b, absolute ? "$%u" : "%u", row + 1);
}

/* An Excel formula as an ODF expression, without "of:=" (NULL when it can't be). */
static char *of_expr(ods_out *w, const char *excel)
{
    int lost = 0;
    char *of = excel ? owf_excel_to_of_lossy(excel, &lost) : NULL;
    if (lost)
        owf_report_add(w->report, OWF_NOTE_APPROX, "Names of one sheet or of another workbook in formulas lost their sheet or workbook: ODF formulas have no such names");
    if (of && !strncmp(of, "of:=", 4))
        memmove(of, of + 4, strlen(of + 4) + 1);
    return of;
}

/* ---- Conditional formatting ---- */

/* An attribute of the start tag of a kept rule's XML (Excel's cfRule), its entities undone, in out. 0 none. */
static int rule_attr(const char *xml, const char *name, char *out, size_t size)
{
    const char *end = strchr(xml, '>'), *p = xml;
    size_t n = strlen(name), k = 0;
    while (end && (p = strstr(p, name)) != NULL && p < end) {
        if (p > xml && p[-1] == ' ' && p[n] == '=' && (p[n + 1] == '"' || p[n + 1] == '\'')) {
            char q = p[n + 1];
            const char *v = p + n + 2, *stop = strchr(v, q);
            static const struct { const char *e; char c; } ents[] = {
                { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }
            };
            if (!stop || stop > end)
                return 0;
            while (v < stop && k + 1 < size) {
                size_t i;
                char c = *v;
                size_t len = 1;
                for (i = 0; *v == '&' && i < sizeof ents / sizeof ents[0]; i++)
                    if (!strncmp(v, ents[i].e, strlen(ents[i].e))) {
                        c = ents[i].c;
                        len = strlen(ents[i].e);
                        break;
                    }
                out[k++] = c;
                v += len;
            }
            out[k] = 0;
            return 1;
        }
        p += n;
    }
    return 0;
}

/* A text as an ODF formula's string: "Say ""hi""". */
static void put_of_string(owf_buf *b, const char *s)
{
    owf_buf_putc(b, '"');
    for (; *s; s++) {
        if (*s == '"')
            owf_buf_putc(b, '"');
        owf_buf_putc(b, *s);
    }
    owf_buf_putc(b, '"');
}

/* A kept rule's cfvo elements (a data bar's, an icon set's) as ODF's formatting entries. */
static void put_cfvos(ods_out *w, owf_buf *b, const char *xml)
{
    static const struct { const char *xl, *odf; } kinds[] = {
        { "min", "minimum" }, { "max", "maximum" }, { "num", "number" }, { "percent", "percent" },
        { "percentile", "percentile" }, { "formula", "formula" }, { "autoMin", "auto-minimum" }, { "autoMax", "auto-maximum" }
    };
    const char *p = xml;
    while ((p = strstr(p, "<cfvo")) != NULL) {
        char type[32], val[512];
        size_t i;
        const char *odf = "number";
        char *f = NULL;
        if (!rule_attr(p, "type", type, sizeof type))
            strcpy(type, "num");
        if (!rule_attr(p, "val", val, sizeof val))
            strcpy(val, "0");
        for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
            if (!strcmp(type, kinds[i].xl))
                odf = kinds[i].odf;
        if (!strcmp(type, "formula"))
            f = of_expr(w, val);
        owf_buf_puts(b, "<calcext:formatting-entry calcext:value=\"");
        attr(w, b, f ? f : val);
        owf_buf_printf(b, "\" calcext:type=\"%s\"/>", odf);
        free(f);
        p += 5;
    }
}

/* One rule, in a conditional-format of its ranges: a condition with its look's style, a colour scale, a data
 * bar or an icon set. 0 when ODF (LibreOffice's) has no such rule. */
static int put_cf_rule(ods_out *w, owf_buf *b, const owf_sheet *sh, int sheet, int index)
{
    static const char *const ops[] = { "", "", "=", "!=", ">", "<", ">=", "<=" };
    static const char *const scale_kinds[] = { "minimum", "maximum", "number", "percent", "percentile", "formula" };
    const owf_condfmt *cf = &sh->condfmts[index];
    const owf_range *at = &cf->ranges[0];
    owf_buf v;
    char *a = NULL, *z = NULL, type[32] = "", text[512];
    int ok = 1, k;
    owf_buf_init(&v);
    if (cf->type == OWF_CF_COLOUR_SCALE) {
        owf_buf_printf(b, "<calcext:color-scale gfocalc:priority=\"%d\">", cf->priority);
        for (k = 0; k < cf->npoints && k < 3; k++) {
            const owf_cfpoint *pt = &cf->points[k];
            char *f = pt->kind == OWF_CFV_FORMULA && pt->value ? of_expr(w, pt->value) : NULL;
            owf_buf_puts(b, "<calcext:color-scale-entry calcext:value=\"");
            attr(w, b, f ? f : pt->value && *pt->value ? pt->value : "0");
            owf_buf_printf(b, "\" calcext:type=\"%s\" calcext:color=\"#%06lx\"/>",
                           scale_kinds[pt->kind >= 0 && pt->kind <= OWF_CFV_FORMULA ? pt->kind : 0], pt->colour & 0xFFFFFFUL);
            free(f);
        }
        owf_buf_puts(b, "</calcext:color-scale>");
        return 1;
    }
    if (cf->type == OWF_CF_KEPT && cf->xml)
        rule_attr(cf->xml, "type", type, sizeof type);
    if (!strcmp(type, "dataBar")) {
        const char *c = strstr(cf->xml, "<color");
        char rgb[16] = "FF638EC6", lo[16] = "10", hi[16] = "90";
        if (c)
            rule_attr(c, "rgb", rgb, sizeof rgb);
        if (strstr(cf->xml, "<dataBar")) {
            rule_attr(strstr(cf->xml, "<dataBar"), "minLength", lo, sizeof lo);
            rule_attr(strstr(cf->xml, "<dataBar"), "maxLength", hi, sizeof hi);
        }
        owf_buf_puts(b, "<calcext:data-bar calcext:min-length=\"");
        attr(w, b, lo);
        owf_buf_puts(b, "\" calcext:max-length=\"");
        attr(w, b, hi);
        owf_buf_puts(b, "\" calcext:negative-color=\"#ff0000\" calcext:axis-position=\"none\" calcext:positive-color=\"#");
        for (k = 0; rgb[k]; k++)                     /* as LibreOffice writes colours */
            if (rgb[k] >= 'A' && rgb[k] <= 'F')
                rgb[k] = (char)(rgb[k] + 32);
        attr(w, b, strlen(rgb) == 8 ? rgb + 2 : rgb);
        owf_buf_printf(b, "\" calcext:axis-color=\"#000000\" gfocalc:priority=\"%d\">", cf->priority);
        put_cfvos(w, b, cf->xml);
        owf_buf_puts(b, "</calcext:data-bar>");
        return 1;
    }
    if (!strcmp(type, "iconSet")) {
        const char *set = strstr(cf->xml, "<iconSet");
        char name[64] = "3TrafficLights1", flag[8];
        if (set)
            rule_attr(set, "iconSet", name, sizeof name);
        owf_buf_puts(b, "<calcext:icon-set calcext:icon-set-type=\"");
        attr(w, b, name);
        owf_buf_putc(b, '"');
        if (set && rule_attr(set, "showValue", flag, sizeof flag) && !strcmp(flag, "0"))
            owf_buf_puts(b, " calcext:show-value=\"false\"");
        owf_buf_printf(b, " gfocalc:priority=\"%d\">", cf->priority);
        put_cfvos(w, b, cf->xml);
        owf_buf_puts(b, "</calcext:icon-set>");
        return 1;
    }
    /* a condition: its value as ODF's condition says it */
    if (cf->type == OWF_CF_CELL_IS) {
        int op = cf->op >= OWF_CF_BETWEEN && cf->op <= OWF_CF_LESS_EQUAL ? cf->op : OWF_CF_EQUAL;
        if (!(a = of_expr(w, cf->formula[0])) || ((op == OWF_CF_BETWEEN || op == OWF_CF_NOT_BETWEEN) && !(z = of_expr(w, cf->formula[1]))))
            ok = 0;
        else if (op == OWF_CF_BETWEEN || op == OWF_CF_NOT_BETWEEN)
            owf_buf_printf(&v, "%sbetween(%s,%s)", op == OWF_CF_NOT_BETWEEN ? "not-" : "", a, z);
        else
            owf_buf_printf(&v, "%s%s", ops[op], a);
    } else if (cf->type == OWF_CF_EXPRESSION) {
        if (!(a = of_expr(w, cf->formula[0])))
            ok = 0;
        else
            owf_buf_printf(&v, "formula-is(%s)", a);
    } else if (!strcmp(type, "containsText") || !strcmp(type, "notContainsText") || !strcmp(type, "beginsWith") ||
               !strcmp(type, "endsWith")) {
        ok = rule_attr(cf->xml, "text", text, sizeof text);
        owf_buf_puts(&v, type[0] == 'c' ? "contains-text(" : type[0] == 'n' ? "not-contains-text(" : type[0] == 'b' ? "begins-with(" : "ends-with(");
        put_of_string(&v, text);
        owf_buf_putc(&v, ')');
    } else if (!strcmp(type, "duplicateValues") || !strcmp(type, "uniqueValues")) {
        owf_buf_puts(&v, type[0] == 'd' ? "duplicate" : "unique");
    } else if (!strcmp(type, "top10")) {
        char rank[16] = "10", flag[8];
        int bottom = rule_attr(cf->xml, "bottom", flag, sizeof flag) && strcmp(flag, "0") && strcmp(flag, "false");
        int percent = rule_attr(cf->xml, "percent", flag, sizeof flag) && strcmp(flag, "0") && strcmp(flag, "false");
        rule_attr(cf->xml, "rank", rank, sizeof rank);
        owf_buf_printf(&v, "%s-%s(%ld)", bottom ? "bottom" : "top", percent ? "percent" : "elements", strtol(rank, NULL, 10));
    } else if (!strcmp(type, "aboveAverage")) {
        char flag[8];
        int below = rule_attr(cf->xml, "aboveAverage", flag, sizeof flag) && (!strcmp(flag, "0") || !strcmp(flag, "false"));
        int equal = rule_attr(cf->xml, "equalAverage", flag, sizeof flag) && strcmp(flag, "0") && strcmp(flag, "false");
        owf_buf_printf(&v, "%s%s-average", below ? "below" : "above", equal ? "-equal" : "");
    } else if (!strcmp(type, "containsErrors") || !strcmp(type, "notContainsErrors")) {
        owf_buf_puts(&v, type[0] == 'c' ? "is-error" : "is-no-error");
    } else if (cf->formula[0] && (a = of_expr(w, cf->formula[0])) != NULL) {
        owf_buf_printf(&v, "formula-is(%s)", a);      /* another kept rule: by the formula its file gave */
    } else {
        ok = 0;
    }
    owf_buf_putc(&v, 0);
    if (ok && !v.failed) {
        if (cf_has_look(cf))
            owf_buf_printf(b, "<calcext:condition calcext:apply-style-name=\"GfoCondition%d\"", cf_number(w->book, sheet, index));
        else
            owf_buf_puts(b, "<calcext:condition calcext:apply-style-name=\"Default\"");
        owf_buf_puts(b, " calcext:value=\"");
        attr(w, b, (const char *)v.data);
        owf_buf_puts(b, "\" calcext:base-cell-address=\"");
        put_address(w, b, sh->name, at->first_row, at->first_col, 0);
        owf_buf_printf(b, "\" gfocalc:priority=\"%d\"%s/>", cf->priority, cf->stop ? " gfocalc:stop-if-true=\"true\"" : "");
    }
    owf_buf_free(&v);
    free(a);
    free(z);
    return ok;
}

static int same_ranges(const owf_condfmt *a, const owf_condfmt *b)
{
    int i;
    if (a->nranges != b->nranges)
        return 0;
    for (i = 0; i < a->nranges; i++)
        if (memcmp(&a->ranges[i], &b->ranges[i], sizeof a->ranges[i]))
            return 0;
    return 1;
}

/* The sheet's rules as LibreOffice keeps them, after its rows: the rules over the same ranges together, by
 * priority (which, and stopIfTrue, go in libowf's own attributes too). */
static void put_condformats(ods_out *w, owf_buf *b, int sheet)
{
    const owf_sheet *sh = &w->book->sheets[sheet];
    int *order, *done, n = sh->ncondfmts, i, k, lost = 0;
    if (!n)
        return;
    order = malloc((size_t)n * sizeof *order);
    done = calloc((size_t)n, sizeof *done);
    if (!order || !done) {
        free(order);
        free(done);
        w->failed = 1;
        return;
    }
    for (i = 0; i < n; i++) {                        /* by priority */
        order[i] = i;
        for (k = i; k > 0 && sh->condfmts[order[k - 1]].priority > sh->condfmts[order[k]].priority; k--) {
            int t = order[k];
            order[k] = order[k - 1];
            order[k - 1] = t;
        }
    }
    owf_buf_puts(b, "<calcext:conditional-formats>");
    for (i = 0; i < n; i++) {
        const owf_condfmt *cf = &sh->condfmts[order[i]];
        int r, any = 0;
        if (done[order[i]] || !cf->nranges)
            continue;
        owf_buf_puts(b, "<calcext:conditional-format calcext:target-range-address=\"");
        for (r = 0; r < cf->nranges; r++) {
            if (r)
                owf_buf_putc(b, ' ');
            put_address(w, b, sh->name, cf->ranges[r].first_row, cf->ranges[r].first_col, 0);
            owf_buf_putc(b, ':');
            put_address(w, b, sh->name, cf->ranges[r].last_row, cf->ranges[r].last_col, 0);
        }
        owf_buf_puts(b, "\">");
        for (k = i; k < n; k++)
            if (!done[order[k]] && same_ranges(cf, &sh->condfmts[order[k]])) {
                done[order[k]] = 1;
                if (put_cf_rule(w, b, sh, sheet, order[k]))
                    any = 1;
                else
                    lost = 1;
            }
        if (!any)                                    /* LibreOffice wants one: a condition that never holds */
            owf_buf_puts(b, "<calcext:condition calcext:apply-style-name=\"Default\" calcext:value=\"formula-is(0)\"/>");
        owf_buf_puts(b, "</calcext:conditional-format>");
    }
    owf_buf_puts(b, "</calcext:conditional-formats>");
    if (lost)
        owf_report_add(w->report, OWF_NOTE_LOST, "Some conditional formatting rules ODS has no place for were left out");
    free(order);
    free(done);
}

/* The empty cells of row from column from up to to: a cell repeated for each run alike in its rule. */
static void put_empty_cells(ods_out *w, owf_buf *b, const owf_sheet *sh, unsigned long row, unsigned long from, unsigned long to)
{
    while (from < to) {
        unsigned long end = to;
        int val = val_at(w, sh, row, from), i, k;
        for (i = 0; i < sh->nvalidations; i++)       /* the next column where the rules change */
            for (k = 0; k < sh->validations[i].nranges; k++) {
                const owf_range *r = &sh->validations[i].ranges[k];
                if (row < r->first_row || row > r->last_row)
                    continue;
                if (r->first_col > from && r->first_col < end)
                    end = r->first_col;
                if ((unsigned long)r->last_col + 1 > from && (unsigned long)r->last_col + 1 < end)
                    end = (unsigned long)r->last_col + 1;
            }
        owf_buf_puts(b, "<table:table-cell");
        if (end - from > 1)
            owf_buf_printf(b, " table:number-columns-repeated=\"%lu\"", end - from);
        if (val)
            owf_buf_printf(b, " table:content-validation-name=\"val%d\"", val);
        owf_buf_puts(b, "/>");
        from = end;
    }
}

/* The column after the last one a rule covers in row, 0 none. */
static unsigned long rules_end(const owf_sheet *sh, unsigned long row)
{
    unsigned long end = 0;
    int i, k;
    for (i = 0; i < sh->nvalidations; i++)
        for (k = 0; k < sh->validations[i].nranges; k++) {
            const owf_range *r = &sh->validations[i].ranges[k];
            if (row >= r->first_row && row <= r->last_row && (unsigned long)r->last_col + 1 > end)
                end = (unsigned long)r->last_col + 1;
        }
    return end < MAX_COLS ? end : MAX_COLS;
}

static void put_sheet(ods_out *w, owf_buf *b, int index)
{
    const owf_sheet *sh = &w->book->sheets[index];
    unsigned long width = 0, last_row = 0, row, col;
    int i, ri = 0, ci = 0, any = 0;

    for (i = 0; i < sh->ncells; i++) {
        if (sh->cells[i].col < MAX_COLS && sh->cells[i].col + 1 > width)
            width = sh->cells[i].col + 1;
        if (sh->cells[i].row < MAX_ROWS && sh->cells[i].row + 1 > last_row)
            last_row = sh->cells[i].row + 1;
    }
    for (i = 0; i < sh->nmerges; i++) {
        if (sh->merges[i].last_col < MAX_COLS && sh->merges[i].last_col + 1 > width)
            width = sh->merges[i].last_col + 1;
        if (sh->merges[i].last_row < MAX_ROWS && sh->merges[i].last_row + 1 > last_row)
            last_row = sh->merges[i].last_row + 1;
    }
    for (i = 0; i < sh->nrows; i++)
        if (sh->rows[i].row < MAX_ROWS && sh->rows[i].row + 1 > last_row)
            last_row = sh->rows[i].row + 1;
    for (i = 0; i < sh->ncomments; i++) {            /* a comment's cell, empty or not, is written */
        if (sh->comments[i].col < MAX_COLS && sh->comments[i].col + 1 > width)
            width = sh->comments[i].col + 1;
        if (sh->comments[i].row < MAX_ROWS && sh->comments[i].row + 1 > last_row)
            last_row = sh->comments[i].row + 1;
    }
    for (i = 0; i < sh->nvalidations; i++) {         /* and the cells of a rule, empty or not */
        int k;
        for (k = 0; k < sh->validations[i].nranges; k++) {
            const owf_range *r = &sh->validations[i].ranges[k];
            if (r->last_col < MAX_COLS && (unsigned long)r->last_col + 1 > width)
                width = (unsigned long)r->last_col + 1;
            if (r->last_row < MAX_ROWS && (unsigned long)r->last_row + 1 > last_row)
                last_row = (unsigned long)r->last_row + 1;
        }
    }
    owf_buf_puts(b, "<table:table table:name=\"");
    attr(w, b, sh->name);
    owf_buf_printf(b, "\" table:style-name=\"%s\" gfocalc:default-column-width=\"%d\" gfocalc:default-row-height=\"%d\">",
                   sh->hidden ? "ta2" : "ta1", sh->default_width, sh->default_height);
    if (sh->hidden == 2)
        owf_report_add(w->report, OWF_NOTE_APPROX, "Very hidden sheets became hidden ones");
    if (sh->drawing)
        owf_report_add(w->report, OWF_NOTE_LOST, "Pictures and charts on sheets are kept in Excel workbooks (.xlsx) only, not yet in ODS");
    put_columns(w, b, sh, width);
    i = 0;
    for (row = 0; row < last_row && !w->failed;) {
        /* the rows with nothing up to the next with something, as one */
        unsigned long next = last_row;
        int k;
        while (i < sh->ncells && sh->cells[i].row < row)
            i++;
        while (ci < sh->ncomments && sh->comments[ci].row < row)
            ci++;
        if (i < sh->ncells && sh->cells[i].row < next)
            next = sh->cells[i].row;
        if (ci < sh->ncomments && sh->comments[ci].row < next)
            next = sh->comments[ci].row;
        for (k = 0; k < sh->nrows; k++)
            if (sh->rows[k].row >= row && sh->rows[k].row < next)
                next = sh->rows[k].row;
        for (k = 0; k < sh->nmerges; k++)
            if (sh->merges[k].last_row >= row && sh->merges[k].first_row <= next && next > row)
                next = sh->merges[k].first_row > row ? sh->merges[k].first_row : row;
        for (k = 0; k < sh->nvalidations; k++) {    /* rows alike in their rules, as one */
            int j;
            for (j = 0; j < sh->validations[k].nranges; j++) {
                const owf_range *r = &sh->validations[k].ranges[j];
                if (r->first_row > row && r->first_row < next)
                    next = r->first_row;
                if ((unsigned long)r->last_row + 1 > row && (unsigned long)r->last_row + 1 < next)
                    next = (unsigned long)r->last_row + 1;
            }
        }
        if (next > row) {
            unsigned long rules = rules_end(sh, row);
            put_row_open(w, b, sh, row, next - row, &ri);
            if (rules)
                put_empty_cells(w, b, sh, row, 0, rules);
            else
                owf_buf_puts(b, "<table:table-cell/>");
            owf_buf_puts(b, "</table:table-row>");
            row = next;
            continue;
        }
        put_row_open(w, b, sh, row, 1, &ri);
        any = 0;
        col = 0;
        for (;;) {
            unsigned long cell_col = i < sh->ncells && sh->cells[i].row == row ? sh->cells[i].col : MAX_COLS;
            unsigned long merge_col = row_has_merge(sh, row) ? next_merge_col(sh, row, col) : MAX_COLS;
            unsigned long note_col = ci < sh->ncomments && sh->comments[ci].row == row ? sh->comments[ci].col : MAX_COLS;
            unsigned long at = cell_col < merge_col ? cell_col : merge_col;
            const owf_range *m;
            int covered;
            if (note_col < at)
                at = note_col;
            if (at >= MAX_COLS)
                break;
            if (at > col)
                put_empty_cells(w, b, sh, row, col, at);
            m = merge_at(sh, row, at, &covered);
            put_cell(w, b, sh, cell_col == at ? &sh->cells[i] : NULL, m, covered, val_at(w, sh, row, at),
                     note_col == at ? &sh->comments[ci] : NULL);
            any = 1;
            if (cell_col == at)
                i++;
            if (note_col == at)
                ci++;
            col = at + 1;
        }
        if (rules_end(sh, row) > col) {              /* the rules' empty cells after the last */
            put_empty_cells(w, b, sh, row, col, rules_end(sh, row));
            any = 1;
        }
        if (!any)
            owf_buf_puts(b, "<table:table-cell/>");
        owf_buf_puts(b, "</table:table-row>");
        while (i < sh->ncells && sh->cells[i].row == row)
            i++;
        while (ci < sh->ncomments && sh->comments[ci].row == row)
            ci++;
        row++;
    }
    if (!last_row)
        owf_buf_printf(b, "<table:table-row table:style-name=\"ro%d\"><table:table-cell/></table:table-row>",
                       row_style(w, sh->default_height, 0) + 1);
    put_names(w, b, index);
    put_condformats(w, b, index);
    owf_buf_puts(b, "</table:table>");
}

/* ---- Data validation, AutoFilters ---- */

/* A rule's condition as ODF's table:condition says it ("of:cell-content-is-whole-number() and
 * cell-content-is-between(1,10)"), in out; 0 when it can't be written. */
static int dv_condition(ods_out *w, const owf_validation *v, owf_buf *out)
{
    static const char *const ops[] = { "", "", "=", "!=", ">", "<", ">=", "<=" };
    static const char *const kinds[] = { "", "cell-content-is-whole-number() and ", "cell-content-is-decimal-number() and ",
                                         "", "cell-content-is-date() and ", "cell-content-is-time() and ", "", "" };
    char *a = NULL, *z = NULL;
    int ok = 1, op = v->op >= OWF_CF_BETWEEN && v->op <= OWF_CF_LESS_EQUAL ? v->op : OWF_CF_BETWEEN;
    owf_buf_puts(out, "of:");
    switch (v->type) {
    case OWF_DV_LIST: {
        const char *f = v->formula[0];
        owf_buf_puts(out, "cell-content-is-in-list(");
        if (f && *f == '"') {                        /* "Yes,No": "Yes";"No" */
            const char *p = f + 1, *end = strrchr(p, '"');
            if (!end || end < p)
                end = p + strlen(p);
            for (;;) {
                const char *comma = memchr(p, ',', (size_t)(end - p)), *stop = comma ? comma : end;
                owf_buf_putc(out, '"');
                owf_buf_put(out, p, (size_t)(stop - p));
                owf_buf_putc(out, '"');
                if (!comma)
                    break;
                owf_buf_putc(out, ';');
                p = comma + 1;
            }
        } else if (!(a = of_expr(w, f))) {
            ok = 0;
        } else {
            owf_buf_puts(out, a);
        }
        owf_buf_putc(out, ')');
        break;
    }
    case OWF_DV_CUSTOM:
        if (!(a = of_expr(w, v->formula[0])))
            ok = 0;
        else
            owf_buf_printf(out, "is-true-formula(%s)", a);
        break;
    case OWF_DV_WHOLE:
    case OWF_DV_DECIMAL:
    case OWF_DV_DATE:
    case OWF_DV_TIME:
    case OWF_DV_TEXT_LENGTH: {
        int two = op == OWF_CF_BETWEEN || op == OWF_CF_NOT_BETWEEN;
        const char *what = v->type == OWF_DV_TEXT_LENGTH ? "cell-content-text-length" : "cell-content";
        if (!(a = of_expr(w, v->formula[0])) || (two && !(z = of_expr(w, v->formula[1])))) {
            ok = 0;
            break;
        }
        owf_buf_puts(out, kinds[v->type]);
        if (two)
            owf_buf_printf(out, "%s-is-%sbetween(%s,%s)", what, op == OWF_CF_NOT_BETWEEN ? "not-" : "", a, z);
        else
            owf_buf_printf(out, "%s()%s%s", what, ops[op], a);
        break;
    }
    default:
        ok = 0;
    }
    free(a);
    free(z);
    return ok && !out->failed;
}

/* A message (help-message, error-message) of a rule. */
static void dv_message(ods_out *w, owf_buf *b, const char *element, const char *type, const char *title, const char *text,
                       int display)
{
    owf_buf_printf(b, "<table:%s", element);
    if (type)
        owf_buf_printf(b, " table:message-type=\"%s\"", type);
    if (title && *title) {
        owf_buf_puts(b, " table:title=\"");
        attr(w, b, title);
        owf_buf_putc(b, '"');
    }
    owf_buf_printf(b, " table:display=\"%s\"", display ? "true" : "false");
    if (text && *text) {
        owf_buf_putc(b, '>');
        put_paragraphs(w, b, text);
        owf_buf_printf(b, "</table:%s>", element);
    } else {
        owf_buf_puts(b, "/>");
    }
}

/* Every sheet's rules, "val1"..., in the order the sheets' cells name them. */
static void put_validations(ods_out *w, owf_buf *b)
{
    static const char *const styles[] = { "stop", "warning", "information" };
    const owf_book *book = w->book;
    int s, i, n = 0, any = 0;
    for (s = 0; s < book->nsheets; s++)
        for (i = 0; i < book->sheets[s].nvalidations; i++) {
            const owf_validation *v = &book->sheets[s].validations[i];
            owf_buf cond;
            n++;
            if (!any)
                owf_buf_puts(b, "<table:content-validations>");
            any = 1;
            owf_buf_printf(b, "<table:content-validation table:name=\"val%d\"", n);
            owf_buf_init(&cond);
            if (v->type != OWF_DV_ANY) {
                if (dv_condition(w, v, &cond)) {
                    owf_buf_putc(&cond, 0);
                    owf_buf_puts(b, " table:condition=\"");
                    if (!cond.failed)
                        attr(w, b, (const char *)cond.data);
                    owf_buf_putc(b, '"');
                } else {
                    owf_report_add(w->report, OWF_NOTE_LOST, "Some data validation rules could not be written: their cells take anything");
                }
            }
            owf_buf_free(&cond);
            owf_buf_printf(b, " table:allow-empty-cell=\"%s\"", v->allow_blank ? "true" : "false");
            if (v->type == OWF_DV_LIST)
                owf_buf_printf(b, " table:display-list=\"%s\"", v->no_dropdown ? "none" : "unsorted");
            if (v->nranges) {
                owf_buf_puts(b, " table:base-cell-address=\"");
                put_address(w, b, book->sheets[s].name, v->ranges[0].first_row, v->ranges[0].first_col, 0);
                owf_buf_putc(b, '"');
            }
            owf_buf_putc(b, '>');
            if (v->show_prompt || (v->prompt && *v->prompt) || (v->prompt_title && *v->prompt_title))
                dv_message(w, b, "help-message", NULL, v->prompt_title, v->prompt, v->show_prompt);
            if (v->show_error || (v->error && *v->error) || (v->error_title && *v->error_title))
                dv_message(w, b, "error-message", styles[v->error_style >= 0 && v->error_style <= 2 ? v->error_style : 0],
                           v->error_title, v->error, v->show_error);
            owf_buf_puts(b, "</table:content-validation>");
        }
    if (any)
        owf_buf_puts(b, "</table:content-validations>");
}

/* The next <filter val="..."/> from p before end: its value (as escaped in the XML) and where to go on. */
static int filter_val(const char *p, const char *end, const char **v, size_t *len, const char **next)
{
    const char *q, *close;
    char quote;
    if (!(p = strstr(p, "<filter ")) || p >= end || !(close = strchr(p, '>')) || close > end)
        return 0;
    if (!(q = strstr(p, "val=")) || q > close)
        return 0;
    quote = q[4];
    *v = q + 5;
    if ((quote != '"' && quote != '\'') || !(q = strchr(*v, quote)) || q > close)
        return 0;
    *len = (size_t)(q - *v);
    *next = close;
    return 1;
}

/* One Excel filterColumn (x up to end, its close) as ODF's condition on that field: its value, or its values
 * as a set. 0 when it filters otherwise (a custom filter, a top ten, a colour, blanks, dates by group). */
static int af_column(owf_buf *b, const char *x, const char *end)
{
    static const char *const others[] = { "customFilters", "top10", "dynamicFilter", "colorFilter", "iconFilter",
                                          "dateGroupItem", "blank=\"1\"", "blank='1'" };
    const char *id = strstr(x, "colId="), *p = x, *v, *next;
    size_t len, i;
    int n = 0;
    long field;
    if (!id || id > end)
        return 0;
    field = strtol(id + 7, NULL, 10);
    for (i = 0; i < sizeof others / sizeof others[0]; i++) {
        const char *o = strstr(x, others[i]);
        if (o && o < end)
            return 0;
    }
    while (filter_val(p, end, &v, &len, &next)) {
        if (!n++) {
            owf_buf_printf(b, "<table:filter-condition table:field-number=\"%ld\" table:value=\"", field);
            owf_buf_put(b, v, len);
            owf_buf_puts(b, "\" table:operator=\"=\"");
        }
        p = next;
    }
    if (n <= 1) {
        if (n)
            owf_buf_puts(b, "/>");
        return n;
    }
    owf_buf_putc(b, '>');
    for (p = x; filter_val(p, end, &v, &len, &next); p = next) {
        owf_buf_puts(b, "<table:filter-set-item table:value=\"");
        owf_buf_put(b, v, len);
        owf_buf_puts(b, "\"/>");
    }
    owf_buf_puts(b, "</table:filter-condition>");
    return 1;
}

/* The sheets' AutoFilters, as LibreOffice keeps them: an unnamed database range with its buttons and what it
 * filters by. */
static void put_autofilters(ods_out *w, owf_buf *b)
{
    const owf_book *book = w->book;
    int s, any = 0;
    for (s = 0; s < book->nsheets; s++) {
        const owf_sheet *sh = &book->sheets[s];
        const char *x = sh->autofilter_xml;
        owf_buf conds;
        int n = 0, approx = 0;
        if (!sh->has_autofilter)
            continue;
        if (!any)
            owf_buf_puts(b, "<table:database-ranges>");
        any = 1;
        owf_buf_printf(b, "<table:database-range table:name=\"__Anonymous_Sheet_DB__%d\" table:target-range-address=\"", s);
        put_address(w, b, sh->name, sh->autofilter.first_row, sh->autofilter.first_col, 0);
        owf_buf_putc(b, ':');
        put_address(w, b, sh->name, sh->autofilter.last_row, sh->autofilter.last_col, 0);
        owf_buf_puts(b, "\" table:display-filter-buttons=\"true\"");
        owf_buf_init(&conds);
        while (x && (x = strstr(x, "<filterColumn")) != NULL) {
            const char *open = strchr(x, '>'), *end;
            if (!open)
                break;
            if (open[-1] == '/') {                   /* <filterColumn .../>: nothing to filter by */
                x = open + 1;
                continue;
            }
            if (!(end = strstr(open, "</filterColumn>")))
                break;
            if (af_column(&conds, x, end))
                n++;
            else
                approx = 1;
            x = end + 15;
        }
        if (sh->autofilter_xml && strstr(sh->autofilter_xml, "<sortState"))
            approx = 1;
        if (approx)
            owf_report_add(w->report, OWF_NOTE_APPROX, "Some AutoFilters' conditions (custom, top ten, colours) and sorting are not kept in ODS: their buttons are");
        if (n && !conds.failed) {
            owf_buf_puts(b, "><table:filter>");
            if (n > 1)
                owf_buf_puts(b, "<table:filter-and>");
            owf_buf_put(b, (const char *)conds.data, conds.length);
            if (n > 1)
                owf_buf_puts(b, "</table:filter-and>");
            owf_buf_puts(b, "</table:filter></table:database-range>");
        } else {
            owf_buf_puts(b, "/>");
        }
        owf_buf_free(&conds);
    }
    if (any)
        owf_buf_puts(b, "</table:database-ranges>");
}

static void body(ods_out *w, owf_buf *b)
{
    int i;
    owf_buf_puts(b, "<office:body><office:spreadsheet>");
    if (w->book->date1904)
        owf_buf_puts(b, "<table:calculation-settings><table:null-date table:date-value=\"1904-01-01\"/></table:calculation-settings>");
    put_validations(w, b);
    w->val_base = 0;
    for (i = 0; i < w->book->nsheets && !w->failed; i++) {
        put_sheet(w, b, i);
        w->val_base += w->book->sheets[i].nvalidations;
    }
    if (!w->book->nsheets)
        owf_buf_puts(b, "<table:table table:name=\"Sheet1\"><table:table-column/><table:table-row><table:table-cell/>"
                        "</table:table-row></table:table>");
    put_names(w, b, -1);
    put_autofilters(w, b);
    owf_buf_puts(b, "</office:spreadsheet></office:body>");
}

/* ---- Settings, meta ---- */

static void config_item(owf_buf *b, const char *name, const char *type, long value)
{
    owf_buf_printf(b, "<config:config-item config:name=\"%s\" config:type=\"%s\">%ld</config:config-item>", name, type, value);
}

static void settings(ods_out *w, owf_buf *b)
{
    const owf_book *book = w->book;
    int i, active = book->active >= 0 && book->active < book->nsheets ? book->active : 0;
    owf_buf_puts(b, "<office:settings><config:config-item-set config:name=\"ooo:view-settings\">"
                    "<config:config-item-map-indexed config:name=\"Views\"><config:config-item-map-entry>"
                    "<config:config-item config:name=\"ViewId\" config:type=\"string\">view1</config:config-item>"
                    "<config:config-item-map-named config:name=\"Tables\">");
    for (i = 0; i < book->nsheets; i++) {
        const owf_sheet *sh = &book->sheets[i];
        owf_buf_puts(b, "<config:config-item-map-entry config:name=\"");
        attr(w, b, sh->name);
        owf_buf_puts(b, "\">");
        if (sh->freeze_cols || sh->freeze_rows) {
            config_item(b, "HorizontalSplitMode", "short", sh->freeze_cols ? 2 : 0);
            config_item(b, "VerticalSplitMode", "short", sh->freeze_rows ? 2 : 0);
            config_item(b, "HorizontalSplitPosition", "int", (long)sh->freeze_cols);
            config_item(b, "VerticalSplitPosition", "int", (long)sh->freeze_rows);
            config_item(b, "ActiveSplitRange", "short", sh->freeze_rows ? 2 : 3);
            config_item(b, "PositionLeft", "int", 0);
            config_item(b, "PositionRight", "int", (long)sh->freeze_cols);
            config_item(b, "PositionTop", "int", 0);
            config_item(b, "PositionBottom", "int", (long)sh->freeze_rows);
        }
        owf_buf_puts(b, "</config:config-item-map-entry>");
    }
    owf_buf_puts(b, "</config:config-item-map-named><config:config-item config:name=\"ActiveTable\" config:type=\"string\">");
    if (book->nsheets)
        attr(w, b, book->sheets[active].name);
    owf_buf_puts(b, "</config:config-item></config:config-item-map-entry></config:config-item-map-indexed>"
                    "</config:config-item-set></office:settings>");
}

static const char meta[] = "<office:meta><meta:generator>OpenCalc libowf " OWF_VERSION "</meta:generator></office:meta>";

static void finish_notes(ods_out *w)
{
    if (w->dropped)
        owf_report_add(w->report, OWF_NOTE_LOST, "Characters ODF cannot hold (control characters) were left out");
}

static void free_writer(ods_out *w)
{
    free(w->col_widths);
    free(w->row_styles);
}

/* ---- The two files ---- */

static int add(owf_zip *zip, owf_buf *part, const char *name, int result, int failed)
{
    if (result == OWF_OK)
        result = part->failed || failed ? OWF_ERR_MEMORY : owf_zip_add(zip, name, part->data, part->length, 1);
    part->length = 0;
    part->failed = 0;
    return result;
}

int owf_export_ods(const owf_book *book, unsigned char **data, size_t *length, owf_report *report)
{
    ods_out w;
    owf_buf out, part;
    owf_zip *zip;
    int result = OWF_OK;

    *data = NULL;
    *length = 0;
    memset(&w, 0, sizeof w);
    w.book = book;
    w.report = report;
    collect_layout(&w);
    owf_buf_init(&out);
    owf_buf_init(&part);
    zip = owf_zip_new(&out);
    if (!zip || w.failed) {
        free_writer(&w);
        return OWF_ERR_MEMORY;
    }
    owf_zip_add(zip, "mimetype", MIME, sizeof MIME - 1, 0);
    owf_buf_puts(&part, XML_HEAD "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
                        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"" MIME "\"/>"
                        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
                        "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
                        "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>"
                        "<manifest:file-entry manifest:full-path=\"settings.xml\" manifest:media-type=\"text/xml\"/>"
                        "</manifest:manifest>\n");
    result = add(zip, &part, "META-INF/manifest.xml", result, 0);
    owf_buf_puts(&part, XML_HEAD "<office:document-meta " NS_DECLS ">");
    owf_buf_puts(&part, meta);
    owf_buf_puts(&part, "</office:document-meta>\n");
    result = add(zip, &part, "meta.xml", result, 0);
    owf_buf_puts(&part, XML_HEAD "<office:document-settings " NS_DECLS ">");
    settings(&w, &part);
    owf_buf_puts(&part, "</office:document-settings>\n");
    result = add(zip, &part, "settings.xml", result, w.failed);
    owf_buf_puts(&part, XML_HEAD "<office:document-styles " NS_DECLS ">");
    font_faces(&w, &part);
    common_styles(&w, &part);
    owf_buf_puts(&part, "</office:document-styles>\n");
    result = add(zip, &part, "styles.xml", result, w.failed);
    owf_buf_puts(&part, XML_HEAD "<office:document-content " NS_DECLS ">");
    font_faces(&w, &part);
    automatic_styles(&w, &part);
    body(&w, &part);
    owf_buf_puts(&part, "</office:document-content>\n");
    result = add(zip, &part, "content.xml", result, w.failed);
    owf_buf_free(&part);
    if (owf_zip_finish(zip) != OWF_OK && result == OWF_OK)
        result = OWF_ERR_MEMORY;
    finish_notes(&w);
    free_writer(&w);
    if (result != OWF_OK) {
        owf_buf_free(&out);
        return result;
    }
    return owf_buf_take(&out, data, length);
}

int owf_export_fods(const owf_book *book, unsigned char **data, size_t *length, owf_report *report)
{
    ods_out w;
    owf_buf out;
    int result;

    *data = NULL;
    *length = 0;
    memset(&w, 0, sizeof w);
    w.book = book;
    w.report = report;
    collect_layout(&w);
    owf_buf_init(&out);
    owf_buf_puts(&out, XML_HEAD "<office:document " NS_DECLS " office:mimetype=\"" MIME "\">");
    owf_buf_puts(&out, meta);
    settings(&w, &out);
    font_faces(&w, &out);
    common_styles(&w, &out);
    automatic_styles(&w, &out);
    body(&w, &out);
    owf_buf_puts(&out, "</office:document>\n");
    finish_notes(&w);
    result = w.failed || out.failed ? OWF_ERR_MEMORY : OWF_OK;
    free_writer(&w);
    if (result != OWF_OK) {
        owf_buf_free(&out);
        return result;
    }
    return owf_buf_take(&out, data, length);
}

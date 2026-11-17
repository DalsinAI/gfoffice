/*
 * libowf: Markdown out, as CommonMark 0.31 reads it, with GitHub's tables
 * and strike-through. Headings, paragraphs, hard line breaks (a backslash
 * at the line's end), bold, italic, strike-through, monospaced text as code
 * spans, links, lists (a paragraph that starts with its bullet or number and
 * a tab, as DOCX's, ODT's and Markdown's own come in), quotes (a paragraph
 * in from both sides), code blocks (paragraphs all in a monospaced font),
 * thematic breaks (an empty paragraph with a line below it) and tables.
 * Text that Markdown would read as markup is escaped. Fonts, sizes,
 * colours, underlining, alignment, pictures, headers, footers and page
 * breaks are reported as not kept; notes are listed at the end.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"
#include "owf_note.h"

#include <stdlib.h>
#include <string.h>

#define MW_MAX_LEVELS 16
#define MW_EMPH (OWF_BOLD | OWF_ITALIC | OWF_STRIKE)

enum { B_NONE, B_PARA, B_ITEM, B_CODE, B_TABLE };

typedef struct {
    const owf_doc *doc;
    owf_report *report;
    owf_buf out;
    int last;                /* the block before: B_... */
    int last_top_kind;       /* the last top-level item's kind: 1 bullet, 2 number */
    int levels[MW_MAX_LEVELS], widths[MW_MAX_LEVELS], nlevels;
    /* inside a paragraph */
    unsigned open[3];
    int nopen;
    const char *href;
    owf_buf ws;              /* spaces held back until what follows them is known */
    const char *line_prefix; /* what a hard break's next line starts with */
    int line_start;
    int in_table, mask;      /* in a table cell; flags left out (a header's bold) */
    int said_look, said_pictures, said_align, said_breaks;
} mw;

static int mono(const mw *w, const owf_run *r)
{
    owf_font_kind kind;
    const owf_font *f;
    if (r->fmt.font < 0 || r->fmt.font >= w->doc->nfonts)
        return 0;
    f = &w->doc->fonts[r->fmt.font];
    kind = f->kind;
    if (kind != OWF_FONT_MONO)
        owf_font_modern(f->name, &kind);
    return kind == OWF_FONT_MONO;
}

static void note_look(mw *w, const owf_run *r)
{
    if (w->said_look)
        return;
    if ((r->fmt.flags & ~(unsigned)MW_EMPH & ~(unsigned)w->mask) || r->fmt.size ||
        (r->fmt.colour != OWF_COLOUR_AUTO && r->fmt.colour) || (r->fmt.font >= 0 && !mono(w, r))) {
        owf_report_add(w->report, OWF_NOTE_LOST,
                       "Fonts, sizes, colours, underlining and raised or lowered text are not kept in Markdown");
        w->said_look = 1;
    }
}

/* Text, with what Markdown would take as markup escaped. */
static void put_escaped(mw *w, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (w->line_start) {
            size_t k = i;
            if (c == ' ' || c == '\t')
                continue;                            /* Markdown drops a line's leading spaces */
            w->line_start = 0;
            if (c == '#' || c == '-' || c == '+' || c == '=' || c == '>') {
                owf_buf_putc(&w->out, '\\');
                owf_buf_putc(&w->out, c);
                continue;
            }
            while (k < n && k - i < 10 && s[k] >= '0' && s[k] <= '9') k++;
            if (k > i && k < n && (s[k] == '.' || s[k] == ')')) {
                owf_buf_put(&w->out, s + i, k - i);
                owf_buf_putc(&w->out, '\\');
                owf_buf_putc(&w->out, s[k]);
                i = k;
                continue;
            }
        }
        if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '[' || c == ']' || c == '<' || c == '~' ||
            (c == '|' && w->in_table)) {
            owf_buf_putc(&w->out, '\\');
            owf_buf_putc(&w->out, c);
        } else if (c == '&' && i + 1 < n &&
                   ((s[i + 1] >= 'a' && s[i + 1] <= 'z') || (s[i + 1] >= 'A' && s[i + 1] <= 'Z') || s[i + 1] == '#')) {
            owf_buf_puts(&w->out, "\\&");
        } else if (c == '\n' || c == '\r') {
            owf_buf_putc(&w->out, ' ');
        } else {
            owf_buf_putc(&w->out, c);
        }
    }
}

static void put_code_span(mw *w, const char *s, size_t n)
{
    size_t i, run = 0, longest = 0, k;
    int pad;
    for (i = 0; i < n; i++) {
        run = s[i] == '`' ? run + 1 : 0;
        if (run > longest) longest = run;
    }
    pad = n && (s[0] == '`' || s[n - 1] == '`' || (s[0] == ' ' && s[n - 1] == ' '));
    for (k = 0; k <= longest; k++) owf_buf_putc(&w->out, '`');
    if (pad) owf_buf_putc(&w->out, ' ');
    for (i = 0; i < n; i++)
        owf_buf_putc(&w->out, s[i] == '\n' || s[i] == '\r' ? ' ' : (w->in_table && s[i] == '|') ? '|' : s[i]);
    if (pad) owf_buf_putc(&w->out, ' ');
    for (k = 0; k <= longest; k++) owf_buf_putc(&w->out, '`');
    w->line_start = 0;
}

static const char *marker(unsigned flag)
{
    return flag == OWF_BOLD ? "**" : flag == OWF_ITALIC ? "*" : "~~";
}

static void close_to(mw *w, int keep)
{
    while (w->nopen > keep)
        owf_buf_puts(&w->out, marker(w->open[--w->nopen]));
}

static int same_href(const char *a, const char *b)
{
    return (!a && !b) || (a && b && !strcmp(a, b));
}

static void put_href_close(mw *w)
{
    const char *h = w->href;
    owf_buf_puts(&w->out, "](");
    if (h[strcspn(h, " ()<>")] || !*h) {   /* (strcspn: the libraries' runtime has no strpbrk) */
        owf_buf_putc(&w->out, '<');
        for (; *h; h++) {
            if (*h == '<' || *h == '>' || *h == '\\') owf_buf_putc(&w->out, '\\');
            owf_buf_putc(&w->out, *h);
        }
        owf_buf_putc(&w->out, '>');
    } else {
        owf_buf_puts(&w->out, h);
    }
    owf_buf_putc(&w->out, ')');
}

static void flush_ws(mw *w)
{
    if (w->ws.length && !w->line_start)
        owf_buf_put(&w->out, w->ws.data, w->ws.length);
    w->ws.length = 0;
}

/* The markers for the next text: flags (OWF_BOLD...) in href (NULL: none), its leading spaces outside them. */
static void change(mw *w, unsigned flags, const char *href, const char *lead, size_t nlead)
{
    int keep, k;
    if (!same_href(href, w->href)) {
        close_to(w, 0);
        if (w->href)
            put_href_close(w);
        w->href = NULL;
        keep = 0;
    } else {
        for (keep = 0; keep < w->nopen && (flags & w->open[keep]); keep++)
            ;
        close_to(w, keep);
    }
    flush_ws(w);
    if (nlead && !w->line_start)
        owf_buf_put(&w->out, lead, nlead);
    if (href && !w->href) {
        owf_buf_putc(&w->out, '[');
        w->href = href;
        w->line_start = 0;
    }
    for (k = 0; k < 3; k++) {
        unsigned f = k == 0 ? OWF_STRIKE : k == 1 ? OWF_BOLD : OWF_ITALIC;
        int j, have = 0;
        for (j = 0; j < w->nopen; j++) if (w->open[j] == f) have = 1;
        if ((flags & f) && !have) {
            owf_buf_puts(&w->out, marker(f));
            w->open[w->nopen++] = f;
            w->line_start = 0;
        }
    }
}

static void end_inline(mw *w)
{
    close_to(w, 0);
    if (w->href)
        put_href_close(w);
    w->href = NULL;
    w->ws.length = 0;
}

static void put_text_run(mw *w, const char *s, size_t n, unsigned flags, const char *href, int code)
{
    size_t a = 0, z = n;
    while (a < n && (s[a] == ' ' || s[a] == '\t')) a++;
    if (a == n) {
        owf_buf_put(&w->ws, s, n);                   /* spaces alone: kept back */
        return;
    }
    while (z > a && (s[z - 1] == ' ' || s[z - 1] == '\t')) z--;
    change(w, flags, href, s, a);
    if (code)
        put_code_span(w, s + a, z - a);
    else
        put_escaped(w, s + a, z - a);
    owf_buf_put(&w->ws, s + z, n - z);
}

static void put_field_run(mw *w, const owf_run *r, unsigned flags)
{
    const char *t;
    switch (r->field) {
    case OWF_FIELD_PAGE: t = "#"; break;
    case OWF_FIELD_PAGES: t = "##"; break;
    case OWF_FIELD_DATE: t = "[date]"; break;
    case OWF_FIELD_TIME: t = "[time]"; break;
    default: t = r->text ? r->text : ""; break;
    }
    put_text_run(w, t, strlen(t), flags, NULL, 0);
}

static void put_runs(mw *w, const owf_para *p, int from, int depth)
{
    int j, k;
    for (j = from; j < p->nruns; j++) {
        const owf_run *r = &p->runs[j];
        unsigned flags = r->fmt.flags & MW_EMPH & ~(unsigned)w->mask;
        if ((r->fmt.flags & OWF_HIDDEN) && r->note <= 0 && !r->comment)
            continue;                                /* 1.14: hidden text is left out */
        if (r->kind == OWF_RUN_TEXT || r->kind == OWF_RUN_FIELD)
            note_look(w, r);
        switch (r->kind) {
        case OWF_RUN_TEXT:
            if (r->note > 0) {
                char mark[32], text[40];
                owf_note_mark_text(w->doc, r->note - 1, mark, sizeof mark);
                text[0] = '[';
                strcpy(text + 1, mark);
                strcat(text, "]");
                put_text_run(w, text, strlen(text), flags, NULL, 0);
            } else if (r->text && *r->text) {
                put_text_run(w, r->text, strlen(r->text), flags, r->href, mono(w, r));
            }
            break;
        case OWF_RUN_TAB:
            owf_buf_putc(&w->ws, '\t');
            break;
        case OWF_RUN_LINEBREAK:
            end_inline(w);
            if (w->in_table) {
                owf_buf_putc(&w->ws, ' ');
                break;
            }
            owf_buf_puts(&w->out, "\\\n");
            owf_buf_puts(&w->out, w->line_prefix);
            w->line_start = 1;
            break;
        case OWF_RUN_FIELD:
            if (r->field != OWF_FIELD_HIDE_PARA) put_field_run(w, r, flags);   /* (1.20: it shows nothing) */
            break;
        case OWF_RUN_IMAGE:
            if (!w->said_pictures++)
                owf_report_add(w->report, OWF_NOTE_LOST, "Pictures are not kept in Markdown");
            break;
        case OWF_RUN_FRAME:
            if (r->image >= 0 && r->image < w->doc->nframes && depth < 2) {
                const owf_frame *fr = &w->doc->frames[r->image];
                if (fr->image >= 0 && !w->said_pictures++)
                    owf_report_add(w->report, OWF_NOTE_LOST, "Pictures are not kept in Markdown");
                for (k = 0; k < fr->story.nparas; k++) {
                    owf_buf_putc(&w->ws, ' ');
                    put_runs(w, &fr->story.paras[k], 0, depth + 1);
                }
            }
            break;
        }
    }
}

static void put_inline(mw *w, const owf_para *p, int from, const char *line_prefix)
{
    w->line_prefix = line_prefix;
    w->nopen = 0;
    w->href = NULL;
    w->ws.length = 0;
    put_runs(w, p, from, 0);
    end_inline(w);
}

static int has_content(const owf_para *p)
{
    int j;
    for (j = 0; j < p->nruns; j++) {
        const owf_run *r = &p->runs[j];
        if ((r->kind == OWF_RUN_TEXT && ((r->text && *r->text) || r->note > 0)) ||
            r->kind == OWF_RUN_FIELD || r->kind == OWF_RUN_FRAME || r->kind == OWF_RUN_IMAGE)
            return 1;
    }
    return 0;
}

static int is_code(const mw *w, const owf_para *p)
{
    int j, text = 0;
    if (p->table_id >= 0 || p->fmt.heading)
        return 0;
    for (j = 0; j < p->nruns; j++) {
        const owf_run *r = &p->runs[j];
        if (r->kind == OWF_RUN_TAB || r->kind == OWF_RUN_LINEBREAK)
            continue;
        if (r->kind != OWF_RUN_TEXT || r->note > 0)
            return 0;
        if (!r->text || !*r->text)
            continue;
        if (!mono(w, r))
            return 0;
        text = 1;
    }
    return text;
}

static int is_rule(const owf_para *p)
{
    return p->table_id < 0 && (p->fmt.borders & OWF_BORDER_BOTTOM) && !has_content(p);
}

/* A list item: 1 bullet or 2 number, and its number's text in num; 0: not one. */
static int item_kind(const mw *w, const owf_para *p, char *num, size_t size)
{
    const owf_run *r;
    const char *t;
    size_t n, i;
    if (p->nruns < 2 || p->runs[1].kind != OWF_RUN_TAB || p->table_id >= 0 || p->fmt.heading)
        return 0;
    r = &p->runs[0];
    if (r->kind != OWF_RUN_TEXT || !r->text || r->note > 0 || mono(w, r))
        return 0;
    t = r->text;
    n = strlen(t);
    if (!strcmp(t, "\xE2\x80\xA2") || !strcmp(t, "\xE2\x97\xA6") || !strcmp(t, "\xE2\x96\xAA") ||
        !strcmp(t, "\xE2\x96\xA0") || !strcmp(t, "\xC2\xB7") || !strcmp(t, "\xE2\x80\x93") ||
        !strcmp(t, "-") || !strcmp(t, "*") || !strcmp(t, "o") || !strcmp(t, "\xE2\x80\xA3"))
        return 1;
    for (i = 0; i < n && t[i] >= '0' && t[i] <= '9'; i++)
        ;
    if (i && i < 10 && i + 1 == n && (t[i] == '.' || t[i] == ')') && i + 1 < size) {
        memcpy(num, t, n + 1);
        return 2;
    }
    return 0;
}

static void begin_block(mw *w, int kind, int tight)
{
    if (w->last != B_NONE && !tight)
        owf_buf_putc(&w->out, '\n');
    w->last = kind;
    w->line_start = 1;
}

static void put_spaces(mw *w, int n)
{
    while (n-- > 0) owf_buf_putc(&w->out, ' ');
}

static int put_code(mw *w, const owf_story *st, int i)
{
    int end = i, k, j;
    size_t longest = 0;
    /* its paragraphs, and empty ones between them */
    for (k = i; k < st->nparas; k++) {
        if (is_code(w, &st->paras[k])) end = k + 1;
        else if (has_content(&st->paras[k]) || st->paras[k].table_id >= 0 || st->paras[k].fmt.heading ||
                 is_rule(&st->paras[k]))
            break;
    }
    for (k = i; k < end; k++)
        for (j = 0; j < st->paras[k].nruns; j++) {
            const char *t = st->paras[k].runs[j].text;
            size_t run = 0;
            for (; t && *t; t++) {
                run = *t == '`' ? run + 1 : 0;
                if (run > longest) longest = run;
            }
        }
    begin_block(w, B_CODE, 0);
    for (j = 0; j < (int)(longest >= 3 ? longest + 1 : 3); j++) owf_buf_putc(&w->out, '`');
    owf_buf_putc(&w->out, '\n');
    for (k = i; k < end; k++) {
        for (j = 0; j < st->paras[k].nruns; j++) {
            const owf_run *r = &st->paras[k].runs[j];
            if (r->kind == OWF_RUN_TAB) owf_buf_putc(&w->out, '\t');
            else if (r->kind == OWF_RUN_LINEBREAK) owf_buf_putc(&w->out, '\n');
            else if (r->kind == OWF_RUN_TEXT && r->text && !(r->fmt.flags & OWF_HIDDEN)) owf_buf_puts(&w->out, r->text);
        }
        owf_buf_putc(&w->out, '\n');
    }
    for (j = 0; j < (int)(longest >= 3 ? longest + 1 : 3); j++) owf_buf_putc(&w->out, '`');
    owf_buf_putc(&w->out, '\n');
    return end;
}

static int put_table(mw *w, const owf_story *st, int i)
{
    int id = st->paras[i].table_id, cols = 0, end, k, row, c;
    for (end = i; end < st->nparas && st->paras[end].table_id == id; end++)
        if (st->paras[end].table_cols > cols) cols = st->paras[end].table_cols;
    if (cols < 1) cols = 1;
    if (cols > 256) cols = 256;
    begin_block(w, B_TABLE, 0);
    w->in_table = 1;
    for (k = i; k < end;) {
        row = st->paras[k].table_row;
        owf_buf_putc(&w->out, '|');
        for (c = 0; c < cols; c++) {
            int m;
            owf_buf_putc(&w->out, ' ');
            for (m = k; m < end && st->paras[m].table_row == row; m++)
                if (st->paras[m].table_col == c) {
                    w->mask = k == i ? OWF_BOLD : 0;
                    w->line_start = 0;
                    put_inline(w, &st->paras[m], 0, "");
                    w->mask = 0;
                    break;
                }
            owf_buf_puts(&w->out, " |");
        }
        owf_buf_putc(&w->out, '\n');
        if (k == i) {
            owf_buf_putc(&w->out, '|');
            for (c = 0; c < cols; c++) {
                owf_align a = OWF_ALIGN_LEFT;
                int m;
                for (m = k; m < end && st->paras[m].table_row == row; m++)
                    if (st->paras[m].table_col == c) a = st->paras[m].fmt.align;
                owf_buf_puts(&w->out, a == OWF_ALIGN_CENTRE ? " :---: |" : a == OWF_ALIGN_RIGHT ? " ---: |" : " --- |");
            }
            owf_buf_putc(&w->out, '\n');
        }
        while (k < end && st->paras[k].table_row == row) k++;
    }
    w->in_table = 0;
    w->nlevels = 0;
    return end;
}

static void put_item(mw *w, const owf_para *p, int kind, const char *num)
{
    int indent = p->fmt.indent_left, level, pad = 0, k, top;
    char prefix[16];
    while (w->nlevels && w->levels[w->nlevels - 1] > indent)
        w->nlevels--;
    if (!w->nlevels || w->levels[w->nlevels - 1] < indent) {
        if (w->nlevels == MW_MAX_LEVELS) w->nlevels--;
        w->levels[w->nlevels++] = indent;
    }
    level = w->nlevels - 1;
    if (kind == 1) strcpy(prefix, "- ");
    else { strcpy(prefix, num); strcat(prefix, " "); }
    w->widths[level] = (int)strlen(prefix);
    for (k = 0; k < level; k++) pad += w->widths[k];
    top = level == 0 ? kind : w->last_top_kind;
    begin_block(w, B_ITEM, w->last == B_ITEM && top == w->last_top_kind);
    if (level == 0) w->last_top_kind = kind;
    put_spaces(w, pad);
    owf_buf_puts(&w->out, prefix);
    {
        char lp[80];
        int n = pad + w->widths[level];
        if (n > 79) n = 79;
        memset(lp, ' ', (size_t)n);
        lp[n] = 0;
        w->line_start = 1;
        put_inline(w, p, 2, lp);
    }
    owf_buf_putc(&w->out, '\n');
}

static void put_story(mw *w, const owf_story *st)
{
    int i = 0;
    while (i < st->nparas) {
        const owf_para *p = &st->paras[i];
        char num[16];
        int kind;
        if (p->fmt.page_break_before && w->last != B_NONE && !w->said_breaks++)
            owf_report_add(w->report, OWF_NOTE_LOST, "Page breaks are not kept in Markdown");
        if (p->table_id >= 0) {
            i = put_table(w, st, i);
            continue;
        }
        if (owf_para_hidden(p)) { i++; continue; }   /* 1.20: a hidden paragraph is left out */
        if (is_code(w, p)) {
            i = put_code(w, st, i);
            w->nlevels = 0;
            continue;
        }
        if (is_rule(p)) {
            begin_block(w, B_PARA, 0);
            owf_buf_puts(&w->out, "---\n");
            w->nlevels = 0;
            i++;
            continue;
        }
        if (!has_content(p)) {
            i++;
            continue;
        }
        if (p->fmt.heading >= 1 && p->fmt.heading <= 6) {
            int k;
            begin_block(w, B_PARA, 0);
            for (k = 0; k < p->fmt.heading; k++) owf_buf_putc(&w->out, '#');
            owf_buf_putc(&w->out, ' ');
            w->line_start = 0;
            put_inline(w, p, 0, "");
            owf_buf_putc(&w->out, '\n');
            w->nlevels = 0;
            i++;
            continue;
        }
        if ((kind = item_kind(w, p, num, sizeof num)) > 0) {
            put_item(w, p, kind, num);
            i++;
            continue;
        }
        if (w->nlevels && p->fmt.indent_left >= w->levels[0] && p->fmt.indent_left > 0 && !p->fmt.indent_right) {
            /* more of a list item: under the deepest item it is in */
            int level = 0, pad = 0, k;
            char lp[80];
            while (level + 1 < w->nlevels && w->levels[level + 1] <= p->fmt.indent_left) level++;
            for (k = 0; k <= level; k++) pad += w->widths[k];
            if (pad > 79) pad = 79;
            memset(lp, ' ', (size_t)pad);
            lp[pad] = 0;
            begin_block(w, B_PARA, 0);
            w->last = B_ITEM;
            put_spaces(w, pad);
            put_inline(w, p, 0, lp);
            owf_buf_putc(&w->out, '\n');
            i++;
            continue;
        }
        w->nlevels = 0;
        begin_block(w, B_PARA, 0);
        if (p->fmt.indent_left > 0 && p->fmt.indent_right > 0) {
            owf_buf_puts(&w->out, "> ");
            put_inline(w, p, 0, "> ");
        } else {
            if (!w->said_align && (p->fmt.align == OWF_ALIGN_CENTRE || p->fmt.align == OWF_ALIGN_RIGHT ||
                                   p->fmt.indent_left || p->fmt.indent_first)) {
                owf_report_add(w->report, OWF_NOTE_APPROX, "Alignment and indents are not kept in Markdown");
                w->said_align = 1;
            }
            put_inline(w, p, 0, "");
        }
        owf_buf_putc(&w->out, '\n');
        i++;
    }
}

int owf_export_md(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    mw w;
    int i;
    memset(&w, 0, sizeof w);
    w.doc = doc;
    w.report = report;
    owf_buf_init(&w.out);
    owf_buf_init(&w.ws);
    put_story(&w, &doc->body);
    for (i = 0; i < 2; ++i)
        if (owf_doc_note_count(doc, i)) {
            owf_buf notes;
            size_t a = 0, k;
            owf_buf_init(&notes);
            owf_notes_listing(doc, i, &notes);
            for (k = 0; k <= notes.length && !notes.failed; k++)
                if (k == notes.length || notes.data[k] == '\n') {
                    if (k > a) {
                        begin_block(&w, B_PARA, 0);
                        put_escaped(&w, (const char *)notes.data + a, k - a);
                        owf_buf_putc(&w.out, '\n');
                    }
                    a = k + 1;
                }
            if (notes.failed) w.out.failed = 1;
            owf_buf_free(&notes);
        }
    if (doc->header.nparas || doc->footer.nparas || doc->header_left.nparas || doc->footer_left.nparas)
        owf_report_add(report, OWF_NOTE_LOST, "Headers and footers are not kept in Markdown");
    owf_report_add(report, OWF_NOTE_INFO, "Saved as Markdown (CommonMark, with GitHub's tables)");
    if (w.ws.failed) w.out.failed = 1;
    owf_buf_free(&w.ws);
    return owf_buf_take(&w.out, data, length);
}

/*
 * GFOpenProject: the task sheet and the resource sheet as documents, for printing and saving them through
 * libowf's writers (PDF, ODT, DOCX, RTF, HTML, text), as the reports (owf_preport.c) are. The task sheet
 * is the view's: its shown columns in order and its lines (filter and groups, a group's heading a bold
 * row), a summary's row bold, an over-allocated task's ID with "!" as the sheet draws it. The resource
 * sheet has every column. A4 landscape, the plan's title in the page header, "Page n of m" in the footer.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_core.h"

#include <stdio.h>
#include <string.h>

#define CELL_MAX 256

typedef struct {
    owf_doc *doc;
    int failed, ncols, row;
} sheet_build;

static const char *const sheet_names[OPJ_NSHEETS] = { "Task Sheet", "Resource Sheet" };

const char *opj_sheet_name(int sheet)
{
    return sheet >= 0 && sheet < OPJ_NSHEETS ? sheet_names[sheet] : NULL;
}

int opj_sheet_parse(const char *name)
{
    char s[24];
    size_t k = 0;
    if (!name) return -1;
    for (; *name && k + 1 < sizeof s; name++) {
        char c = *name;
        if (c == ' ') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        s[k++] = c;
    }
    s[k] = 0;
    if (!strcmp(s, "tasks") || !strcmp(s, "tasksheet") || !strcmp(s, "task")) return OPJ_SHEET_TASKS;
    if (!strcmp(s, "resources") || !strcmp(s, "resourcesheet") || !strcmp(s, "resource")) return OPJ_SHEET_RESOURCES;
    return -1;
}

static owf_para *add_para(sheet_build *b, owf_story *story, int heading, owf_align align)
{
    owf_parafmt pf;
    owf_para *para;
    if (b->failed) return NULL;
    owf_parafmt_init(&pf);
    pf.heading = heading;
    pf.align = align;
    if (!(para = owf_story_add(story, &pf))) b->failed = 1;
    return para;
}

static void add_text(sheet_build *b, owf_para *para, int bold, const char *utf8)
{
    owf_charfmt cf;
    if (!para || !utf8 || !*utf8 || b->failed) return;
    owf_charfmt_init(&cf);
    if (bold) cf.flags |= OWF_BOLD;
    if (owf_para_add_text(para, &cf, utf8, strlen(utf8))) b->failed = 1;
}

static void add_field(sheet_build *b, owf_para *para, owf_field field)
{
    owf_charfmt cf;
    if (!para || b->failed) return;
    owf_charfmt_init(&cf);
    if (owf_para_add_special(para, &cf, OWF_RUN_FIELD, field)) b->failed = 1;
}

/* One cell of table 0's current row. */
static void cell(sheet_build *b, int col, int bold, const char *text)
{
    owf_para *c = add_para(b, &b->doc->body, 0, OWF_ALIGN_LEFT);
    if (!c) return;
    c->table_id = 0;
    c->table_row = b->row;
    c->table_col = col;
    c->table_cols = b->ncols;
    add_text(b, c, bold, text);
}

static void tasks(sheet_build *b, const opj_doc *doc, const opj_view *v)
{
    const owf_project *p = doc->plan;
    int cols[OPJ_NCOLS], n = 0, c, line, lines = opj_view_lines(v, doc);
    char text[CELL_MAX];
    for (c = 0; c < OPJ_NCOLS; c++)
        if (v->columns[c] > 0) cols[n++] = c;
    b->ncols = n;
    for (c = 0; c < n; c++) cell(b, c, 1, opj_column_title(cols[c]));
    b->row++;
    for (line = 0; line < lines && !b->failed; line++) {
        int task = opj_view_line_task(v, doc, line);
        if (task < 0) {
            if (!v->lines) continue;
            opj_lines_heading_text(v->lines, doc, line, text, sizeof text);
            cell(b, 0, 1, text);
            for (c = 1; c < n; c++) cell(b, c, 0, NULL);
        } else {
            int bold = p->tasks[task].summary;
            for (c = 0; c < n; c++) {
                opj_cell_text(doc, task, cols[c], text, sizeof text);
                if (cols[c] == OPJ_COL_ID && owf_ptask_overallocated(p, task) && strlen(text) + 2 <= sizeof text)
                    strcat(text, "!");
                cell(b, c, bold, text);
            }
        }
        b->row++;
    }
    if (!lines) add_text(b, add_para(b, &b->doc->body, 0, OWF_ALIGN_LEFT), 0, "No tasks.");
}

static void resources(sheet_build *b, const opj_doc *doc)
{
    char text[CELL_MAX];
    int r, c;
    b->ncols = OPJ_NRCOLS;
    for (c = 0; c < OPJ_NRCOLS; c++) cell(b, c, 1, opj_resource_column_title(c));
    b->row++;
    for (r = 0; r < doc->plan->nresources && !b->failed; r++, b->row++)
        for (c = 0; c < OPJ_NRCOLS; c++) {
            opj_resource_cell_text(doc, r, c, text, sizeof text);
            cell(b, c, 0, text);
        }
    if (!doc->plan->nresources) add_text(b, add_para(b, &b->doc->body, 0, OWF_ALIGN_LEFT), 0, "No resources.");
}

int opj_sheet_document(const opj_doc *doc, const opj_view *v, int sheet, owf_doc **out)
{
    sheet_build b;
    owf_para *f;
    const char *title;
    char name[256];
    *out = NULL;
    if (!doc || (sheet == OPJ_SHEET_TASKS && !v) || sheet < 0 || sheet >= OPJ_NSHEETS) return OWF_ERR_FORMAT;
    memset(&b, 0, sizeof b);
    if (!(b.doc = owf_doc_new())) return OWF_ERR_MEMORY;
    b.doc->page.width = 16838;                   /* A4 landscape, in twips */
    b.doc->page.height = 11906;
    title = doc->plan->title && *doc->plan->title ? doc->plan->title : NULL;
    if (title) snprintf(name, sizeof name, "%s: %s", title, sheet_names[sheet]);
    else snprintf(name, sizeof name, "%s", sheet_names[sheet]);
    if (owf_doc_set_title(b.doc, name)) b.failed = 1;
    add_text(&b, add_para(&b, &b.doc->body, 1, OWF_ALIGN_LEFT), 0, sheet_names[sheet]);
    if (title) add_text(&b, add_para(&b, &b.doc->header, 0, OWF_ALIGN_LEFT), 0, title);
    if ((f = add_para(&b, &b.doc->footer, 0, OWF_ALIGN_CENTRE))) {
        add_text(&b, f, 0, "Page ");
        add_field(&b, f, OWF_FIELD_PAGE);
        add_text(&b, f, 0, " of ");
        add_field(&b, f, OWF_FIELD_PAGES);
    }
    if (sheet == OPJ_SHEET_TASKS) tasks(&b, doc, v);
    else resources(&b, doc);
    if (b.failed) {
        owf_doc_free(b.doc);
        return OWF_ERR_MEMORY;
    }
    *out = b.doc;
    return OWF_OK;
}

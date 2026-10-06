#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"

struct ow_editor {
    owf_doc *doc;
    ow_selection selection;
    ow_view_mode view;
    int zoom;
    int pages;
};

ow_editor *ow_editor_new(owf_doc *doc)
{
    ow_editor *editor;
    if (!doc) return NULL;
    editor = (ow_editor *)calloc(1, sizeof(*editor));
    if (!editor) return NULL;
    editor->doc = doc;
    editor->view = OW_VIEW_PAGE;
    editor->zoom = 100;
    editor->pages = 1;
    return editor;
}

void ow_editor_free(ow_editor *editor)
{
    free(editor);
}

owf_doc *ow_editor_document(ow_editor *editor)
{
    return editor ? editor->doc : NULL;
}

const owf_doc *ow_editor_document_const(const ow_editor *editor)
{
    return editor ? editor->doc : NULL;
}

void ow_editor_set_view(ow_editor *editor, ow_view_mode mode)
{
    if (editor) editor->view = mode;
}

ow_view_mode ow_editor_view(const ow_editor *editor)
{
    return editor ? editor->view : OW_VIEW_PAGE;
}

void ow_editor_set_zoom(ow_editor *editor, int percent)
{
    if (!editor) return;
    if (percent < 25) percent = 25;
    if (percent > 400) percent = 400;
    editor->zoom = percent;
}

int ow_editor_zoom(const ow_editor *editor)
{
    return editor ? editor->zoom : 100;
}

void ow_editor_set_selection(ow_editor *editor, const ow_selection *selection)
{
    if (editor && selection) editor->selection = *selection;
}

ow_selection ow_editor_selection(const ow_editor *editor)
{
    ow_selection empty;
    memset(&empty, 0, sizeof(empty));
    return editor ? editor->selection : empty;
}

int ow_editor_layout(ow_editor *editor)
{
    int page_breaks = 0;
    int i, j;
    if (!editor || !editor->doc) return OWF_ERR_FORMAT;

    for (i = 0; i < editor->doc->body.nparas; ++i) {
        const owf_para *p = &editor->doc->body.paras[i];
        if (p->fmt.page_break_before && i > 0) ++page_breaks;
        for (j = 0; j < p->nruns; ++j) {
            if (p->runs[j].kind == OWF_RUN_TEXT &&
                p->runs[j].text &&
                strchr(p->runs[j].text, '\f'))
                ++page_breaks;
        }
    }
    editor->pages = 1 + page_breaks;
    return OWF_OK;
}

int ow_editor_page_count(const ow_editor *editor)
{
    return editor ? editor->pages : 0;
}

int ow_editor_render_page(const ow_editor *editor, int page_index,
                          const ow_renderer *renderer)
{
    ow_page_info page;
    int i, j;
    int y;

    if (!editor || !editor->doc || !renderer) return OWF_ERR_FORMAT;
    if (page_index < 0 || page_index >= editor->pages) return OWF_ERR_FORMAT;

    page.width_twips = editor->doc->page.width;
    page.height_twips = editor->doc->page.height;
    page.page_index = page_index;

    if (renderer->begin_page) renderer->begin_page(renderer->userdata, &page);

    y = editor->doc->page.margin_top;
    for (i = 0; i < editor->doc->body.nparas; ++i) {
        const owf_para *p = &editor->doc->body.paras[i];
        int x = editor->doc->page.margin_left + p->fmt.indent_left;
        y += p->fmt.space_before;
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *run = &p->runs[j];
            if (run->kind == OWF_RUN_TEXT && run->text && renderer->text)
                renderer->text(renderer->userdata, x, y, run->text, &run->fmt);
        }
        y += p->fmt.space_after + 240;
    }

    if (renderer->end_page) renderer->end_page(renderer->userdata, &page);
    return OWF_OK;
}

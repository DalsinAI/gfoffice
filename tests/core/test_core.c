#include <stdio.h>
#include <string.h>
#include "openwrite_core.h"

static int text_calls;
static void on_text(void *ud, int x, int y, const char *utf8, const owf_charfmt *fmt)
{
    (void)ud; (void)x; (void)y; (void)fmt;
    if (utf8 && *utf8) ++text_calls;
}

int main(void)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    ow_editor *ed;
    ow_renderer r;

    if (!doc) return 1;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    p = owf_story_add(&doc->body, &pf);
    if (!p) return 2;
    if (owf_para_add_text(p, &cf, "OpenWrite", 9) != OWF_OK) return 3;

    ed = ow_editor_new(doc);
    if (!ed) return 4;
    if (ow_editor_zoom(ed) != 100) return 5;
    ow_editor_set_zoom(ed, 10);
    if (ow_editor_zoom(ed) != 25) return 6;
    if (ow_editor_layout(ed) != OWF_OK) return 7;
    if (ow_editor_page_count(ed) != 1) return 8;

    memset(&r, 0, sizeof(r));
    r.text = on_text;
    if (ow_editor_render_page(ed, 0, &r) != OWF_OK) return 9;
    if (text_calls != 1) return 10;

    ow_editor_free(ed);
    owf_doc_free(doc);
    puts("openwrite core smoke test passed");
    return 0;
}

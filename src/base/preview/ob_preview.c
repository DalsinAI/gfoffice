/*
 * GFOpenBase's print preview of a report (include/openbase_preview.h): the report's document laid out into
 * pages by GFOpenWrite's layout, as the PDF and the printer get them.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openbase_preview.h"

struct ob_preview {
    owf_doc *doc;
    ow_layout *layout;
    int pages;
};

static void say(char *err, size_t errsize, const char *text)
{
    if (err && errsize) snprintf(err, errsize, "%s", text);
}

ob_preview *ob_preview_new(ob_project *p, const ob_report *r, const char *const *params,
                           ol_measure_text_fn measure, void *measure_userdata, char *err, size_t errsize)
{
    ob_preview *v;
    if (err && errsize) err[0] = 0;
    if (!p || !r) { say(err, errsize, "No report to preview"); return NULL; }
    if (!measure) { say(err, errsize, "No way to measure the report's text"); return NULL; }
    if (!(v = calloc(1, sizeof *v))) { say(err, errsize, "Not enough memory for the preview"); return NULL; }
    if (ob_report_build(p, r, params, &v->doc, err, errsize) != 0 || !v->doc) {
        if (err && errsize && !err[0]) say(err, errsize, "The report could not be built");
        ob_preview_free(v);
        return NULL;
    }
    if (!(v->layout = ow_layout_new(v->doc, measure, measure_userdata))) {
        say(err, errsize, "Not enough memory to lay the report out");
        ob_preview_free(v);
        return NULL;
    }
    v->pages = ow_layout_page_count(v->layout);
    if (v->pages < 1) v->pages = 1;
    return v;
}

void ob_preview_free(ob_preview *v)
{
    if (!v) return;
    if (v->layout) ow_layout_free(v->layout);
    if (v->doc) owf_doc_free(v->doc);
    free(v);
}

int ob_preview_pages(const ob_preview *v) { return v ? v->pages : 0; }
const owf_doc *ob_preview_doc(const ob_preview *v) { return v ? v->doc : NULL; }
const ow_layout *ob_preview_layout(const ob_preview *v) { return v ? v->layout : NULL; }

int ob_preview_turn(const ob_preview *v, int page, int by)
{
    long to;
    if (!v) return 0;
    to = (long)page + by;
    if (to >= v->pages) to = v->pages - 1;
    if (to < 0) to = 0;
    return (int)to;
}

void ob_preview_label(const ob_preview *v, int page, char *out, size_t size)
{
    if (!out || !size) return;
    if (!v) { out[0] = 0; return; }
    snprintf(out, size, "Page %d of %d", ob_preview_turn(v, page, 0) + 1, v->pages);
}

/* n bytes of s onto out (len so far), cut to size. */
static void put(char *out, size_t size, size_t *len, const char *s, size_t n)
{
    if (*len + 1 >= size) return;
    if (n > size - 1 - *len) n = size - 1 - *len;
    memcpy(out + *len, s, n);
    *len += n;
    out[*len] = 0;
}

void ob_preview_band_text(const ob_preview *v, ow_story_kind which, int page, const char *date, char *out,
                          size_t size)
{
    ol_document *story;
    const char *last = NULL;            /* the last op's node text, and where that op ended in it */
    size_t k, len = 0, last_end = 0;
    if (!out || !size) return;
    out[0] = 0;
    if (!v || page < 0 || page >= v->pages) return;
    if (!(story = ow_layout_story(v->layout, which, page, date, NULL, NULL, NULL))) return;
    /* The text ops in drawing order. The words of one node are ops of their own, the spaces between them
     * not drawn: they come back from the node's text. A node's spaces after its last word come too, before
     * the next node's words. */
    for (k = 0; k < ol_display_count(story); ++k) {
        const ol_display_op *op = ol_display_get(story, k);
        const char *t;
        size_t tl;
        if (op->type != OL_OP_TEXT) continue;
        t = ol_node_text(ol_node_by_id(story, op->u.text.node_id));
        if (!t) continue;
        tl = strlen(t);
        if (op->u.text.text_offset > tl || op->u.text.text_length > tl - op->u.text.text_offset) continue;
        if (last && last == t && op->u.text.text_offset > last_end)
            put(out, size, &len, t + last_end, op->u.text.text_offset - last_end);
        else if (last && last != t)
            put(out, size, &len, last + last_end, strlen(last) - last_end);
        put(out, size, &len, t + op->u.text.text_offset, op->u.text.text_length);
        last = t;
        last_end = op->u.text.text_offset + op->u.text.text_length;
    }
    ol_document_free(story);
}

size_t ob_preview_latin1(const char *utf8, size_t n, char *out, size_t size, size_t *chars)
{
    size_t i = 0, k = 0;
    if (chars) *chars = 0;
    if (!out || !size) return 0;
    out[0] = 0;
    if (!utf8) return 0;
    while (i < n && k + 1 < size) {
        unsigned char c = (unsigned char)utf8[i];
        unsigned long u;
        size_t more, j;
        if (c < 0x80) { u = c; more = 0; }
        else if ((c & 0xe0) == 0xc0) { u = c & 0x1f; more = 1; }
        else if ((c & 0xf0) == 0xe0) { u = c & 0x0f; more = 2; }
        else if ((c & 0xf8) == 0xf0) { u = c & 0x07; more = 3; }
        else { u = '?'; more = 0; }     /* a stray continuation byte: one '?' */
        if (more && i + more >= n) break;                       /* cut off: left for the next piece */
        for (j = 1; j <= more; j++) {
            if (((unsigned char)utf8[i + j] & 0xc0) != 0x80) break;
            u = (u << 6) | ((unsigned char)utf8[i + j] & 0x3f);
        }
        if (j <= more) { u = '?'; more = j - 1; }               /* broken: what there was, as one '?' */
        out[k++] = u < 0x100 ? (char)u : '?';
        i += 1 + more;
    }
    out[k] = 0;
    if (chars) *chars = k;
    return i;
}

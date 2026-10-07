#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "openwrite_core.h"

#define OW_UNDO_LIMIT 16

typedef struct {
    owf_doc *doc;
    ow_selection selection;
    int dirty;
} ow_snapshot;

struct ow_editor {
    owf_doc *doc;
    ow_selection selection;
    ow_view_mode view;
    int zoom;
    int pages;
    int *para_page;
    int *para_y;
    int para_cap;
    int dirty;

    ow_snapshot undo[OW_UNDO_LIMIT];
    int nundo;
    ow_snapshot redo[OW_UNDO_LIMIT];
    int nredo;

    int typing_group;
    ow_position typing_next;
    owf_charfmt typing_fmt;
    int typing_fmt_set;
};

/* ---- small allocation helpers -------------------------------------------------- */

static char *copy_bytes(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void free_story_data(owf_story *story)
{
    int i, j;
    if (!story) return;
    for (i = 0; i < story->nparas; ++i) {
        for (j = 0; j < story->paras[i].nruns; ++j) {
            free(story->paras[i].runs[j].text);
            free(story->paras[i].runs[j].href);
        }
        free(story->paras[i].runs);
    }
    free(story->paras);
    memset(story, 0, sizeof(*story));
}

static void clear_doc_data(owf_doc *doc)
{
    int i;
    if (!doc) return;
    free_story_data(&doc->body);
    free_story_data(&doc->header);
    free_story_data(&doc->footer);
    for (i = 0; i < doc->nfonts; ++i) free(doc->fonts[i].name);
    free(doc->fonts);
    for (i = 0; i < doc->nimages; ++i) { free(doc->images[i].name); free(doc->images[i].mime); free(doc->images[i].alt); free(doc->images[i].data); }
    free(doc->images);
    free(doc->title);
    memset(doc, 0, sizeof(*doc));
}

static int clone_story(owf_story *dst, const owf_story *src)
{
    int i, j;
    memset(dst, 0, sizeof(*dst));
    if (!src->nparas) return 1;
    dst->paras = (owf_para *)calloc((size_t)src->nparas, sizeof(*dst->paras));
    if (!dst->paras) return 0;
    dst->nparas = dst->capparas = src->nparas;
    for (i = 0; i < src->nparas; ++i) {
        const owf_para *sp = &src->paras[i];
        owf_para *dp = &dst->paras[i];
        dp->fmt = sp->fmt;
        dp->table_id = sp->table_id; dp->table_row = sp->table_row;
        dp->table_col = sp->table_col; dp->table_cols = sp->table_cols;
        if (!sp->nruns) continue;
        dp->runs = (owf_run *)calloc((size_t)sp->nruns, sizeof(*dp->runs));
        if (!dp->runs) return 0;
        dp->nruns = dp->capruns = sp->nruns;
        for (j = 0; j < sp->nruns; ++j) {
            dp->runs[j] = sp->runs[j];
            dp->runs[j].text = NULL;
            dp->runs[j].href = NULL;
            if (sp->runs[j].text) {
                dp->runs[j].text = copy_bytes(sp->runs[j].text,
                                               strlen(sp->runs[j].text));
                if (!dp->runs[j].text) return 0;
            }
            if (sp->runs[j].href) {
                dp->runs[j].href = copy_bytes(sp->runs[j].href, strlen(sp->runs[j].href));
                if (!dp->runs[j].href) return 0;
            }
        }
    }
    return 1;
}

static owf_doc *clone_doc(const owf_doc *src)
{
    owf_doc *dst;
    int i;
    if (!src) return NULL;
    dst = (owf_doc *)calloc(1, sizeof(*dst));
    if (!dst) return NULL;
    dst->base = src->base;
    dst->page = src->page;
    if (src->title) {
        dst->title = copy_bytes(src->title, strlen(src->title));
        if (!dst->title) goto fail;
    }
    if (src->nfonts) {
        dst->fonts = (owf_font *)calloc((size_t)src->nfonts, sizeof(*dst->fonts));
        if (!dst->fonts) goto fail;
        dst->nfonts = dst->capfonts = src->nfonts;
        for (i = 0; i < src->nfonts; ++i) {
            dst->fonts[i].kind = src->fonts[i].kind;
            dst->fonts[i].name = copy_bytes(src->fonts[i].name,
                                             strlen(src->fonts[i].name));
            if (!dst->fonts[i].name) goto fail;
        }
    }
    if (src->nimages) {
        dst->images=(owf_image*)calloc((size_t)src->nimages,sizeof(*dst->images));if(!dst->images)goto fail;dst->nimages=dst->capimages=src->nimages;
        for(i=0;i<src->nimages;++i){const owf_image *si=&src->images[i];owf_image *di=&dst->images[i];di->width=si->width;di->height=si->height;di->length=si->length;
            if(si->name){di->name=copy_bytes(si->name,strlen(si->name));if(!di->name)goto fail;}if(si->mime){di->mime=copy_bytes(si->mime,strlen(si->mime));if(!di->mime)goto fail;}if(si->alt){di->alt=copy_bytes(si->alt,strlen(si->alt));if(!di->alt)goto fail;}if(si->length){di->data=(unsigned char*)malloc(si->length);if(!di->data)goto fail;memcpy(di->data,si->data,si->length);}
        }
    }
    if (!clone_story(&dst->body, &src->body) ||
        !clone_story(&dst->header, &src->header) ||
        !clone_story(&dst->footer, &src->footer))
        goto fail;
    return dst;
fail:
    owf_doc_free(dst);
    return NULL;
}

static void restore_doc(owf_doc *dst, owf_doc *snapshot)
{
    if (!dst || !snapshot) return;
    clear_doc_data(dst);
    *dst = *snapshot;               /* ownership of all children moves */
    free(snapshot);                 /* only the old outer shell */
}

/* ---- document/run helpers ------------------------------------------------------ */

static int same_position(ow_position a, ow_position b)
{
    return a.paragraph == b.paragraph && a.run == b.run &&
           a.byte_offset == b.byte_offset;
}

static int compare_position(ow_position a, ow_position b)
{
    if (a.paragraph != b.paragraph)
        return a.paragraph < b.paragraph ? -1 : 1;
    if (a.run != b.run)
        return a.run < b.run ? -1 : 1;
    if (a.byte_offset != b.byte_offset)
        return a.byte_offset < b.byte_offset ? -1 : 1;
    return 0;
}

static int first_text_run(const owf_para *p)
{
    int i;
    if (!p) return -1;
    for (i = 0; i < p->nruns; ++i)
        if (p->runs[i].kind == OWF_RUN_TEXT) return i;
    return -1;
}

static int last_text_run(const owf_para *p)
{
    int i;
    if (!p) return -1;
    for (i = p->nruns - 1; i >= 0; --i)
        if (p->runs[i].kind == OWF_RUN_TEXT) return i;
    return -1;
}

static int next_text_run(const owf_para *p, int from)
{
    int i;
    if (!p) return -1;
    for (i = from + 1; i < p->nruns; ++i)
        if (p->runs[i].kind == OWF_RUN_TEXT) return i;
    return -1;
}

static int prev_text_run(const owf_para *p, int from)
{
    int i;
    if (!p) return -1;
    for (i = from - 1; i >= 0; --i)
        if (p->runs[i].kind == OWF_RUN_TEXT) return i;
    return -1;
}

static int append_empty_text_run(owf_para *p, const owf_charfmt *fmt)
{
    owf_run *runs;
    int want;
    if (!p || !fmt) return 0;
    if (p->nruns == p->capruns) {
        want = p->capruns ? p->capruns * 2 : 4;
        runs = (owf_run *)realloc(p->runs, (size_t)want * sizeof(*runs));
        if (!runs) return 0;
        p->runs = runs;
        p->capruns = want;
    }
    memset(&p->runs[p->nruns], 0, sizeof(p->runs[p->nruns]));
    p->runs[p->nruns].kind = OWF_RUN_TEXT;
    p->runs[p->nruns].fmt = *fmt;
    p->runs[p->nruns].text = copy_bytes("", 0);
    if (!p->runs[p->nruns].text) return 0;
    ++p->nruns;
    return 1;
}

static int ensure_document_editable(ow_editor *editor)
{
    owf_para *p;
    int r;
    if (!editor || !editor->doc) return 0;
    if (!editor->doc->body.nparas) {
        p = owf_story_add(&editor->doc->body, NULL);
        if (!p) return 0;
    }
    p = &editor->doc->body.paras[0];
    r = first_text_run(p);
    if (r < 0) {
        if (!append_empty_text_run(p, &editor->doc->base)) return 0;
        r = p->nruns - 1;
    }
    editor->selection.anchor.paragraph = 0;
    editor->selection.anchor.run = r;
    editor->selection.anchor.byte_offset = 0;
    editor->selection.focus = editor->selection.anchor;
    return 1;
}

static ow_position clamp_position(ow_editor *editor, ow_position pos)
{
    owf_story *story;
    owf_para *p;
    int r;
    size_t n;
    ow_position zero = {0, 0, 0};

    if (!editor || !editor->doc) return zero;
    story = &editor->doc->body;
    if (!story->nparas && !ensure_document_editable(editor)) return zero;

    if (pos.paragraph < 0) pos.paragraph = 0;
    if (pos.paragraph >= story->nparas) pos.paragraph = story->nparas - 1;
    p = &story->paras[pos.paragraph];

    if (pos.run < 0 || pos.run >= p->nruns ||
        p->runs[pos.run].kind != OWF_RUN_TEXT) {
        r = first_text_run(p);
        if (r < 0) {
            if (!append_empty_text_run(p, &editor->doc->base)) {
                pos.run = 0;
                pos.byte_offset = 0;
                return pos;
            }
            r = p->nruns - 1;
        }
        pos.run = r;
    }
    n = p->runs[pos.run].text ? strlen(p->runs[pos.run].text) : 0;
    if (pos.byte_offset > n) pos.byte_offset = n;
    return pos;
}

static int same_charfmt_local(const owf_charfmt *a, const owf_charfmt *b)
{
    return a && b && a->flags == b->flags && a->font == b->font &&
           a->size == b->size && a->colour == b->colour;
}

static void sync_typing_fmt(ow_editor *editor)
{
    ow_position pos;
    owf_para *p;
    if (!editor || !editor->doc) return;
    pos = clamp_position(editor, editor->selection.focus);
    p = &editor->doc->body.paras[pos.paragraph];
    if (pos.run >= 0 && pos.run < p->nruns &&
        p->runs[pos.run].kind == OWF_RUN_TEXT) {
        editor->typing_fmt = p->runs[pos.run].fmt;
        editor->typing_fmt_set = 1;
    } else {
        editor->typing_fmt = editor->doc->base;
        editor->typing_fmt_set = 1;
    }
}

static size_t para_text_offset(const owf_para *p, ow_position pos)
{
    size_t out = 0;
    int i;
    if (!p) return 0;
    for (i = 0; i < p->nruns; ++i) {
        const owf_run *r = &p->runs[i];
        if (i == pos.run) {
            if (r->kind == OWF_RUN_TEXT && r->text) {
                size_t n = strlen(r->text);
                out += pos.byte_offset < n ? pos.byte_offset : n;
            }
            return out;
        }
        if (r->kind == OWF_RUN_TEXT && r->text) out += strlen(r->text);
    }
    return out;
}

static size_t para_text_length(const owf_para *p)
{
    size_t out = 0;
    int i;
    if (!p) return 0;
    for (i = 0; i < p->nruns; ++i)
        if (p->runs[i].kind == OWF_RUN_TEXT && p->runs[i].text)
            out += strlen(p->runs[i].text);
    return out;
}

static ow_position para_offset_position(ow_editor *editor, int paragraph,
                                        size_t offset)
{
    ow_position pos = { paragraph, 0, 0 };
    owf_para *p;
    size_t at = 0;
    int i, fallback = -1;
    if (!editor || !editor->doc || paragraph < 0 ||
        paragraph >= editor->doc->body.nparas) return pos;
    p = &editor->doc->body.paras[paragraph];
    for (i = 0; i < p->nruns; ++i) {
        owf_run *r = &p->runs[i];
        size_t n;
        if (r->kind != OWF_RUN_TEXT) continue;
        fallback = i;
        n = r->text ? strlen(r->text) : 0;
        if (offset <= at + n) {
            pos.run = i;
            pos.byte_offset = offset - at;
            return pos;
        }
        at += n;
    }
    if (fallback < 0) {
        if (!append_empty_text_run(p, &editor->doc->base)) return pos;
        fallback = p->nruns - 1;
    }
    pos.run = fallback;
    pos.byte_offset = p->runs[fallback].text ? strlen(p->runs[fallback].text) : 0;
    return pos;
}

static size_t utf8_prev(const char *s, size_t off)
{
    if (!s || !off) return 0;
    --off;
    while (off && (((unsigned char)s[off] & 0xC0) == 0x80)) --off;
    return off;
}

static size_t utf8_next(const char *s, size_t off)
{
    size_t n, p;
    if (!s) return off;
    n = strlen(s);
    if (off >= n) return n;
    p = off + 1;
    while (p < n && (((unsigned char)s[p] & 0xC0) == 0x80)) ++p;
    return p;
}

static int replace_run_slice(owf_run *run, size_t from, size_t to,
                             const char *insert, size_t insert_len)
{
    size_t old_len, tail;
    char *text;
    if (!run || run->kind != OWF_RUN_TEXT) return 0;
    if (!run->text) {
        run->text = copy_bytes("", 0);
        if (!run->text) return 0;
    }
    old_len = strlen(run->text);
    if (from > old_len) from = old_len;
    if (to < from) to = from;
    if (to > old_len) to = old_len;
    tail = old_len - to;
    text = (char *)malloc(from + insert_len + tail + 1);
    if (!text) return 0;
    if (from) memcpy(text, run->text, from);
    if (insert_len) memcpy(text + from, insert, insert_len);
    if (tail) memcpy(text + from + insert_len, run->text + to, tail);
    text[from + insert_len + tail] = 0;
    free(run->text);
    run->text = text;
    return 1;
}

static int ensure_para_capacity(owf_story *story, int want)
{
    owf_para *p;
    int cap;
    if (want <= story->capparas) return 1;
    cap = story->capparas ? story->capparas * 2 : 16;
    if (cap < want) cap = want;
    p = (owf_para *)realloc(story->paras, (size_t)cap * sizeof(*p));
    if (!p) return 0;
    story->paras = p;
    story->capparas = cap;
    return 1;
}

static owf_para *insert_paragraph(owf_story *story, int index,
                                  const owf_parafmt *fmt)
{
    if (!story || index < 0 || index > story->nparas) return NULL;
    if (!ensure_para_capacity(story, story->nparas + 1)) return NULL;
    if (index < story->nparas)
        memmove(&story->paras[index + 1], &story->paras[index],
                (size_t)(story->nparas - index) * sizeof(story->paras[0]));
    memset(&story->paras[index], 0, sizeof(story->paras[index]));
    story->paras[index].table_id = -1;
    if (fmt) story->paras[index].fmt = *fmt;
    else owf_parafmt_init(&story->paras[index].fmt);
    ++story->nparas;
    return &story->paras[index];
}

static int ensure_run_capacity(owf_para *p, int want)
{
    owf_run *r;
    int cap;
    if (want <= p->capruns) return 1;
    cap = p->capruns ? p->capruns * 2 : 4;
    if (cap < want) cap = want;
    r = (owf_run *)realloc(p->runs, (size_t)cap * sizeof(*r));
    if (!r) return 0;
    p->runs = r;
    p->capruns = cap;
    return 1;
}

static int insert_run_at(owf_para *p, int index, const owf_charfmt *fmt,
                         const char *text, size_t len)
{
    owf_run *r;
    if (!p || !fmt || index < 0 || index > p->nruns) return 0;
    if (!ensure_run_capacity(p, p->nruns + 1)) return 0;
    if (index < p->nruns)
        memmove(&p->runs[index + 1], &p->runs[index],
                (size_t)(p->nruns - index) * sizeof(p->runs[0]));
    r = &p->runs[index];
    memset(r, 0, sizeof(*r));
    r->kind = OWF_RUN_TEXT;
    r->fmt = *fmt;
    r->text = copy_bytes(text ? text : "", len);
    if (!r->text) {
        if (index < p->nruns)
            memmove(&p->runs[index], &p->runs[index + 1],
                    (size_t)(p->nruns - index) * sizeof(p->runs[0]));
        memset(&p->runs[p->nruns], 0, sizeof(p->runs[0]));
        return 0;
    }
    ++p->nruns;
    return 1;
}

static int insert_formatted_text(ow_editor *editor, ow_position pos,
                                 const char *utf8, size_t length,
                                 const owf_charfmt *fmt, ow_position *after)
{
    owf_para *p;
    owf_run *run;
    owf_charfmt old_fmt;
    size_t old_len;
    char *left = NULL, *right = NULL;
    int inserted;

    if (!editor || !utf8 || !fmt) return 0;
    pos = clamp_position(editor, pos);
    p = &editor->doc->body.paras[pos.paragraph];
    run = &p->runs[pos.run];
    old_fmt = run->fmt;
    old_len = run->text ? strlen(run->text) : 0;

    if (same_charfmt_local(&run->fmt, fmt)) {
        if (!replace_run_slice(run, pos.byte_offset, pos.byte_offset,
                               utf8, length)) return 0;
        if (after) { *after = pos; after->byte_offset += length; }
        return 1;
    }

    if (!old_len) {
        run->fmt = *fmt;
        if (!replace_run_slice(run, 0, 0, utf8, length)) return 0;
        if (after) { *after = pos; after->byte_offset = length; }
        return 1;
    }

    if (pos.byte_offset == 0) {
        if (!insert_run_at(p, pos.run, fmt, utf8, length)) return 0;
        if (after) {
            after->paragraph = pos.paragraph;
            after->run = pos.run;
            after->byte_offset = length;
        }
        return 1;
    }

    if (pos.byte_offset >= old_len) {
        inserted = pos.run + 1;
        if (!insert_run_at(p, inserted, fmt, utf8, length)) return 0;
        if (after) {
            after->paragraph = pos.paragraph;
            after->run = inserted;
            after->byte_offset = length;
        }
        return 1;
    }

    left = copy_bytes(run->text, pos.byte_offset);
    right = copy_bytes(run->text + pos.byte_offset, old_len - pos.byte_offset);
    if (!left || !right) { free(left); free(right); return 0; }
    free(run->text);
    run->text = left;
    left = NULL;
    inserted = pos.run + 1;
    if (!insert_run_at(p, inserted, fmt, utf8, length)) {
        /* The document remains valid even if this rare OOM path leaves the
         * run split point at the caret. Undo snapshot still protects callers. */
        free(right);
        return 0;
    }
    if (!insert_run_at(p, inserted + 1, &old_fmt, right, strlen(right))) {
        free(right);
        return 0;
    }
    free(right);
    if (after) {
        after->paragraph = pos.paragraph;
        after->run = inserted;
        after->byte_offset = length;
    }
    return 1;
}

static void free_para_runs(owf_para *p)
{
    int i;
    if (!p) return;
    for (i = 0; i < p->nruns; ++i) free(p->runs[i].text);
    free(p->runs);
    p->runs = NULL;
    p->nruns = p->capruns = 0;
}

static void apply_charfmt_fields(owf_charfmt *dst, const owf_charfmt *src,
                                 unsigned mask)
{
    if (mask & OW_CHARFMT_FLAGS) dst->flags = src->flags;
    if (mask & OW_CHARFMT_FONT) dst->font = src->font;
    if (mask & OW_CHARFMT_SIZE) dst->size = src->size;
    if (mask & OW_CHARFMT_COLOUR) dst->colour = src->colour;
}

static int rebuild_para_charfmt(owf_para *p, size_t from, size_t to,
                                const owf_charfmt *fmt, unsigned mask)
{
    owf_para tmp;
    size_t at = 0;
    int i;
    memset(&tmp, 0, sizeof(tmp));
    tmp.fmt = p->fmt;

    for (i = 0; i < p->nruns; ++i) {
        const owf_run *r = &p->runs[i];
        if (r->kind == OWF_RUN_TEXT) {
            const char *text = r->text ? r->text : "";
            size_t n = strlen(text), rs = at, re = at + n;
            size_t a = from > rs ? from : rs;
            size_t b = to < re ? to : re;
            owf_charfmt changed = r->fmt;
            if (a < b) apply_charfmt_fields(&changed, fmt, mask);
            if (a > rs && owf_para_add_text(&tmp, &r->fmt, text, a - rs) != OWF_OK)
                goto fail;
            if (a < b && owf_para_add_text(&tmp, &changed, text + (a - rs), b - a) != OWF_OK)
                goto fail;
            if (b < re && owf_para_add_text(&tmp, &r->fmt, text + (b - rs), re - b) != OWF_OK)
                goto fail;
            if (a >= b && n && owf_para_add_text(&tmp, &r->fmt, text, n) != OWF_OK)
                goto fail;
            if (!n && !tmp.nruns && !append_empty_text_run(&tmp, &r->fmt))
                goto fail;
            at = re;
        } else {
            if (owf_para_add_special(&tmp, &r->fmt, r->kind, r->field) != OWF_OK)
                goto fail;
        }
    }

    if (!tmp.nruns && !append_empty_text_run(&tmp, fmt)) goto fail;
    free_para_runs(p);
    p->runs = tmp.runs;
    p->nruns = tmp.nruns;
    p->capruns = tmp.capruns;
    return 1;
fail:
    free_para_runs(&tmp);
    return 0;
}

/* ---- snapshots / undo ---------------------------------------------------------- */

static void snapshot_free(ow_snapshot *s)
{
    if (!s) return;
    if (s->doc) owf_doc_free(s->doc);
    memset(s, 0, sizeof(*s));
}

static void stack_clear(ow_snapshot *stack, int *count)
{
    int i;
    for (i = 0; i < *count; ++i) snapshot_free(&stack[i]);
    *count = 0;
}

static int stack_push(ow_snapshot *stack, int *count, ow_snapshot snap)
{
    if (*count == OW_UNDO_LIMIT) {
        snapshot_free(&stack[0]);
        memmove(&stack[0], &stack[1],
                (OW_UNDO_LIMIT - 1) * sizeof(stack[0]));
        --*count;
    }
    stack[*count] = snap;
    ++*count;
    return 1;
}

static int capture_snapshot(const ow_editor *editor, ow_snapshot *out)
{
    memset(out, 0, sizeof(*out));
    out->doc = clone_doc(editor->doc);
    if (!out->doc) return 0;
    out->selection = editor->selection;
    out->dirty = editor->dirty;
    return 1;
}

static int remember_before_edit(ow_editor *editor)
{
    ow_snapshot snap;
    if (!capture_snapshot(editor, &snap)) return 0;
    stack_clear(editor->redo, &editor->nredo);
    return stack_push(editor->undo, &editor->nundo, snap);
}

static int restore_from_stack(ow_editor *editor,
                              ow_snapshot *from, int *nfrom,
                              ow_snapshot *to, int *nto)
{
    ow_snapshot current, snap;
    if (!editor || !*nfrom) return 0;
    if (!capture_snapshot(editor, &current)) return -1;
    snap = from[*nfrom - 1];
    memset(&from[*nfrom - 1], 0, sizeof(from[*nfrom - 1]));
    --*nfrom;
    stack_push(to, nto, current);
    restore_doc(editor->doc, snap.doc);
    snap.doc = NULL;
    editor->selection = snap.selection;
    editor->selection.anchor = clamp_position(editor, editor->selection.anchor);
    editor->selection.focus = clamp_position(editor, editor->selection.focus);
    editor->dirty = snap.dirty;
    editor->typing_group = 0;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return 1;
}

/* ---- positions and selection -------------------------------------------------- */

static ow_position move_left_pos(ow_editor *editor, ow_position pos)
{
    owf_para *p;
    owf_run *r;
    int pr;
    pos = clamp_position(editor, pos);
    p = &editor->doc->body.paras[pos.paragraph];
    r = &p->runs[pos.run];
    if (pos.byte_offset) {
        pos.byte_offset = utf8_prev(r->text, pos.byte_offset);
        return pos;
    }
    pr = prev_text_run(p, pos.run);
    if (pr >= 0) {
        pos.run = pr;
        pos.byte_offset = strlen(p->runs[pr].text);
        return pos;
    }
    if (pos.paragraph > 0) {
        --pos.paragraph;
        p = &editor->doc->body.paras[pos.paragraph];
        pr = last_text_run(p);
        if (pr < 0) {
            append_empty_text_run(p, &editor->doc->base);
            pr = p->nruns - 1;
        }
        pos.run = pr;
        pos.byte_offset = strlen(p->runs[pr].text);
    }
    return pos;
}

static ow_position move_right_pos(ow_editor *editor, ow_position pos)
{
    owf_para *p;
    owf_run *r;
    int nr;
    pos = clamp_position(editor, pos);
    p = &editor->doc->body.paras[pos.paragraph];
    r = &p->runs[pos.run];
    if (pos.byte_offset < strlen(r->text)) {
        pos.byte_offset = utf8_next(r->text, pos.byte_offset);
        return pos;
    }
    nr = next_text_run(p, pos.run);
    if (nr >= 0) {
        pos.run = nr;
        pos.byte_offset = 0;
        return pos;
    }
    if (pos.paragraph + 1 < editor->doc->body.nparas) {
        ++pos.paragraph;
        p = &editor->doc->body.paras[pos.paragraph];
        nr = first_text_run(p);
        if (nr < 0) {
            append_empty_text_run(p, &editor->doc->base);
            nr = p->nruns - 1;
        }
        pos.run = nr;
        pos.byte_offset = 0;
    }
    return pos;
}

static ow_position paragraph_home(ow_editor *editor, ow_position pos)
{
    owf_para *p;
    int r;
    pos = clamp_position(editor, pos);
    p = &editor->doc->body.paras[pos.paragraph];
    r = first_text_run(p);
    if (r < 0) {
        append_empty_text_run(p, &editor->doc->base);
        r = p->nruns - 1;
    }
    pos.run = r;
    pos.byte_offset = 0;
    return pos;
}

static ow_position paragraph_end(ow_editor *editor, ow_position pos)
{
    owf_para *p;
    int r;
    pos = clamp_position(editor, pos);
    p = &editor->doc->body.paras[pos.paragraph];
    r = last_text_run(p);
    if (r < 0) {
        append_empty_text_run(p, &editor->doc->base);
        r = p->nruns - 1;
    }
    pos.run = r;
    pos.byte_offset = strlen(p->runs[r].text);
    return pos;
}

static ow_position vertical_pos(ow_editor *editor, ow_position pos, int delta)
{
    owf_story *story = &editor->doc->body;
    owf_para *p;
    int target, r;
    size_t want = pos.byte_offset, n;
    pos = clamp_position(editor, pos);
    target = pos.paragraph + delta;
    if (target < 0) target = 0;
    if (target >= story->nparas) target = story->nparas - 1;
    pos.paragraph = target;
    p = &story->paras[target];
    if (pos.run < 0 || pos.run >= p->nruns ||
        p->runs[pos.run].kind != OWF_RUN_TEXT) {
        r = first_text_run(p);
        if (r < 0) {
            append_empty_text_run(p, &editor->doc->base);
            r = p->nruns - 1;
        }
        pos.run = r;
    }
    n = strlen(p->runs[pos.run].text);
    pos.byte_offset = want < n ? want : n;
    return pos;
}

/* ---- edit primitives ----------------------------------------------------------- */

static int merge_paragraph_with_previous(ow_editor *editor, int paragraph,
                                         ow_position *caret)
{
    owf_story *story = &editor->doc->body;
    owf_para *prev, *cur;
    int boundary, i;

    if (paragraph <= 0 || paragraph >= story->nparas) return 0;
    prev = &story->paras[paragraph - 1];
    cur = &story->paras[paragraph];
    boundary = prev->nruns;
    if (!ensure_run_capacity(prev, prev->nruns + cur->nruns)) return -1;
    for (i = 0; i < cur->nruns; ++i) {
        prev->runs[prev->nruns++] = cur->runs[i];
        memset(&cur->runs[i], 0, sizeof(cur->runs[i]));
    }
    free(cur->runs);
    cur->runs = NULL;
    cur->nruns = cur->capruns = 0;

    if (paragraph + 1 < story->nparas)
        memmove(&story->paras[paragraph], &story->paras[paragraph + 1],
                (size_t)(story->nparas - paragraph - 1) *
                sizeof(story->paras[0]));
    --story->nparas;
    memset(&story->paras[story->nparas], 0, sizeof(story->paras[0]));

    if (!prev->nruns && !append_empty_text_run(prev, &editor->doc->base))
        return -1;
    if (caret) {
        caret->paragraph = paragraph - 1;
        if (boundary < prev->nruns &&
            prev->runs[boundary].kind == OWF_RUN_TEXT) {
            caret->run = boundary;
            caret->byte_offset = 0;
        } else {
            caret->run = last_text_run(prev);
            caret->byte_offset = caret->run >= 0
                ? strlen(prev->runs[caret->run].text) : 0;
        }
    }
    return 1;
}

static int merge_paragraph_with_next(ow_editor *editor, int paragraph,
                                     ow_position *caret)
{
    owf_story *story = &editor->doc->body;
    owf_para *cur, *next;
    int boundary, i;

    if (paragraph < 0 || paragraph + 1 >= story->nparas) return 0;
    cur = &story->paras[paragraph];
    next = &story->paras[paragraph + 1];
    boundary = cur->nruns;
    if (!ensure_run_capacity(cur, cur->nruns + next->nruns)) return -1;
    for (i = 0; i < next->nruns; ++i) {
        cur->runs[cur->nruns++] = next->runs[i];
        memset(&next->runs[i], 0, sizeof(next->runs[i]));
    }
    free(next->runs);
    next->runs = NULL;
    next->nruns = next->capruns = 0;

    if (paragraph + 2 < story->nparas)
        memmove(&story->paras[paragraph + 1], &story->paras[paragraph + 2],
                (size_t)(story->nparas - paragraph - 2) *
                sizeof(story->paras[0]));
    --story->nparas;
    memset(&story->paras[story->nparas], 0, sizeof(story->paras[0]));

    if (!cur->nruns && !append_empty_text_run(cur, &editor->doc->base))
        return -1;
    if (caret) {
        caret->paragraph = paragraph;
        if (boundary < cur->nruns &&
            cur->runs[boundary].kind == OWF_RUN_TEXT) {
            caret->run = boundary;
            caret->byte_offset = 0;
        } else {
            caret->run = last_text_run(cur);
            caret->byte_offset = caret->run >= 0
                ? strlen(cur->runs[caret->run].text) : 0;
        }
    }
    return 1;
}

static int delete_one_before(ow_editor *editor, ow_position *pos)
{
    owf_para *p;
    owf_run *r;
    int pr;
    size_t from;

    *pos = clamp_position(editor, *pos);
    p = &editor->doc->body.paras[pos->paragraph];
    r = &p->runs[pos->run];

    if (pos->byte_offset) {
        from = utf8_prev(r->text, pos->byte_offset);
        if (!replace_run_slice(r, from, pos->byte_offset, NULL, 0)) return -1;
        pos->byte_offset = from;
        return 1;
    }

    pr = prev_text_run(p, pos->run);
    while (pr >= 0) {
        r = &p->runs[pr];
        if (strlen(r->text)) {
            pos->run = pr;
            pos->byte_offset = strlen(r->text);
            from = utf8_prev(r->text, pos->byte_offset);
            if (!replace_run_slice(r, from, pos->byte_offset, NULL, 0))
                return -1;
            pos->byte_offset = from;
            return 1;
        }
        pr = prev_text_run(p, pr);
    }

    if (pos->paragraph > 0)
        return merge_paragraph_with_previous(editor, pos->paragraph, pos);
    return 0;
}

static int delete_one_after(ow_editor *editor, ow_position *pos)
{
    owf_para *p;
    owf_run *r;
    int nr;
    size_t to;

    *pos = clamp_position(editor, *pos);
    p = &editor->doc->body.paras[pos->paragraph];
    r = &p->runs[pos->run];

    if (pos->byte_offset < strlen(r->text)) {
        to = utf8_next(r->text, pos->byte_offset);
        if (!replace_run_slice(r, pos->byte_offset, to, NULL, 0)) return -1;
        return 1;
    }

    nr = next_text_run(p, pos->run);
    while (nr >= 0) {
        r = &p->runs[nr];
        if (strlen(r->text)) {
            to = utf8_next(r->text, 0);
            if (!replace_run_slice(r, 0, to, NULL, 0)) return -1;
            return 1;
        }
        nr = next_text_run(p, nr);
    }

    if (pos->paragraph + 1 < editor->doc->body.nparas)
        return merge_paragraph_with_next(editor, pos->paragraph, pos);
    return 0;
}

static int delete_selection_internal(ow_editor *editor)
{
    ow_position start, end;
    int rc, guard = 0;
    if (same_position(editor->selection.anchor, editor->selection.focus))
        return 0;

    if (compare_position(editor->selection.anchor, editor->selection.focus) <= 0) {
        start = editor->selection.anchor;
        end = editor->selection.focus;
    } else {
        start = editor->selection.focus;
        end = editor->selection.anchor;
    }
    start = clamp_position(editor, start);
    end = clamp_position(editor, end);

    while (compare_position(end, start) > 0 && guard++ < 1000000) {
        rc = delete_one_before(editor, &end);
        if (rc < 0) return -1;
        if (!rc) break;
    }
    editor->selection.anchor = start;
    editor->selection.focus = start;
    return 1;
}

static int split_paragraph(ow_editor *editor, ow_position pos,
                           ow_position *new_caret)
{
    owf_story *story = &editor->doc->body;
    owf_para *p, *np;
    owf_run *run;
    owf_parafmt para_fmt;
    char *left = NULL, *right = NULL;
    int moved, i;

    pos = clamp_position(editor, pos);
    p = &story->paras[pos.paragraph];
    run = &p->runs[pos.run];
    para_fmt = p->fmt;

    left = copy_bytes(run->text, pos.byte_offset);
    right = copy_bytes(run->text + pos.byte_offset,
                       strlen(run->text) - pos.byte_offset);
    if (!left || !right) goto fail;

    /* Insert first; p may move if the story reallocates. */
    np = insert_paragraph(story, pos.paragraph + 1, &para_fmt);
    if (!np) goto fail;
    p = &story->paras[pos.paragraph];
    run = &p->runs[pos.run];

    moved = p->nruns - pos.run - 1;
    if (!ensure_run_capacity(np, 1 + moved)) {
        /* A newly inserted empty paragraph is safe to leave only if we undo
         * the structural insertion here. */
        if (pos.paragraph + 2 < story->nparas)
            memmove(&story->paras[pos.paragraph + 1],
                    &story->paras[pos.paragraph + 2],
                    (size_t)(story->nparas - pos.paragraph - 2) *
                    sizeof(story->paras[0]));
        --story->nparas;
        memset(&story->paras[story->nparas], 0, sizeof(story->paras[0]));
        goto fail;
    }

    np->runs[0] = *run;
    np->runs[0].text = right;
    right = NULL;
    np->nruns = 1;

    free(run->text);
    run->text = left;
    left = NULL;

    for (i = 0; i < moved; ++i) {
        np->runs[np->nruns++] = p->runs[pos.run + 1 + i];
        memset(&p->runs[pos.run + 1 + i], 0,
               sizeof(p->runs[pos.run + 1 + i]));
    }
    p->nruns = pos.run + 1;

    new_caret->paragraph = pos.paragraph + 1;
    new_caret->run = 0;
    new_caret->byte_offset = 0;
    return 1;

fail:
    free(left);
    free(right);
    return -1;
}

/* ---- public editor API --------------------------------------------------------- */

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
    if (!ensure_document_editable(editor)) {
        free(editor);
        return NULL;
    }
    sync_typing_fmt(editor);
    return editor;
}

void ow_editor_free(ow_editor *editor)
{
    if (!editor) return;
    stack_clear(editor->undo, &editor->nundo);
    stack_clear(editor->redo, &editor->nredo);
    free(editor->para_page);
    free(editor->para_y);
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

void ow_editor_break_edit_group(ow_editor *editor)
{
    if (editor) editor->typing_group = 0;
}

void ow_editor_set_selection(ow_editor *editor, const ow_selection *selection)
{
    if (!editor || !selection) return;
    editor->selection = *selection;
    editor->selection.anchor = clamp_position(editor, editor->selection.anchor);
    editor->selection.focus = clamp_position(editor, editor->selection.focus);
    editor->typing_group = 0;
    sync_typing_fmt(editor);
}

ow_selection ow_editor_selection(const ow_editor *editor)
{
    ow_selection empty;
    memset(&empty, 0, sizeof(empty));
    return editor ? editor->selection : empty;
}

int ow_editor_selection_empty(const ow_editor *editor)
{
    return !editor || same_position(editor->selection.anchor,
                                    editor->selection.focus);
}

void ow_editor_select_all(ow_editor *editor)
{
    owf_story *story;
    owf_para *p;
    int r;
    if (!editor || !editor->doc) return;
    story = &editor->doc->body;
    if (!story->nparas && !ensure_document_editable(editor)) return;
    editor->selection.anchor.paragraph = 0;
    p = &story->paras[0];
    r = first_text_run(p);
    if (r < 0) { append_empty_text_run(p, &editor->doc->base); r = p->nruns - 1; }
    editor->selection.anchor.run = r;
    editor->selection.anchor.byte_offset = 0;

    editor->selection.focus.paragraph = story->nparas - 1;
    p = &story->paras[story->nparas - 1];
    r = last_text_run(p);
    if (r < 0) { append_empty_text_run(p, &editor->doc->base); r = p->nruns - 1; }
    editor->selection.focus.run = r;
    editor->selection.focus.byte_offset = p->runs[r].text ? strlen(p->runs[r].text) : 0;
    editor->typing_group = 0;
    sync_typing_fmt(editor);
}

static int text_out_append(char **buf, size_t *used, size_t *cap,
                           const char *s, size_t n)
{
    char *b;
    size_t want;
    if (!n) return 1;
    if (*used + n + 1 > *cap) {
        want = *cap ? *cap * 2 : 128;
        while (want < *used + n + 1) want *= 2;
        b = (char *)realloc(*buf, want);
        if (!b) return 0;
        *buf = b; *cap = want;
    }
    memcpy(*buf + *used, s, n);
    *used += n;
    (*buf)[*used] = 0;
    return 1;
}

char *ow_editor_selection_text(const ow_editor *editor, size_t *length)
{
    ow_position start, end;
    char *out = NULL;
    size_t used = 0, cap = 0;
    int pi, ri;
    if (length) *length = 0;
    if (!editor || !editor->doc || ow_editor_selection_empty(editor)) return NULL;
    if (compare_position(editor->selection.anchor, editor->selection.focus) <= 0) {
        start = editor->selection.anchor; end = editor->selection.focus;
    } else {
        start = editor->selection.focus; end = editor->selection.anchor;
    }
    for (pi = start.paragraph; pi <= end.paragraph; ++pi) {
        const owf_para *p = &editor->doc->body.paras[pi];
        for (ri = 0; ri < p->nruns; ++ri) {
            const owf_run *r = &p->runs[ri];
            ow_position rs = { pi, ri, 0 }, re = rs;
            size_t n = r->kind == OWF_RUN_TEXT && r->text ? strlen(r->text) : 0;
            re.byte_offset = n;
            if (r->kind == OWF_RUN_TEXT) {
                size_t a = 0, b = n;
                if (compare_position(end, rs) <= 0 || compare_position(start, re) >= 0)
                    continue;
                if (start.paragraph == pi && start.run == ri) a = start.byte_offset < n ? start.byte_offset : n;
                if (end.paragraph == pi && end.run == ri) b = end.byte_offset < n ? end.byte_offset : n;
                if (b > a && !text_out_append(&out, &used, &cap, r->text + a, b - a)) goto fail;
            } else if (compare_position(start, rs) <= 0 && compare_position(rs, end) < 0) {
                const char c = r->kind == OWF_RUN_TAB ? '\t' : r->kind == OWF_RUN_LINEBREAK ? '\n' : 0;
                if (c && !text_out_append(&out, &used, &cap, &c, 1)) goto fail;
            }
        }
        if (pi < end.paragraph && !text_out_append(&out, &used, &cap, "\n", 1)) goto fail;
    }
    if (!out) {
        out = copy_bytes("", 0);
        if (!out) return NULL;
    }
    if (length) *length = used;
    return out;
fail:
    free(out);
    return NULL;
}

int ow_editor_current_charfmt(const ow_editor *editor, owf_charfmt *fmt)
{
    const owf_para *p;
    ow_position pos;
    if (!editor || !editor->doc || !fmt) return 0;
    if (ow_editor_selection_empty(editor) && editor->typing_fmt_set) {
        *fmt = editor->typing_fmt;
        return 1;
    }
    pos = editor->selection.focus;
    if (pos.paragraph < 0 || pos.paragraph >= editor->doc->body.nparas) return 0;
    p = &editor->doc->body.paras[pos.paragraph];
    if (pos.run < 0 || pos.run >= p->nruns || p->runs[pos.run].kind != OWF_RUN_TEXT) return 0;
    *fmt = p->runs[pos.run].fmt;
    return 1;
}

int ow_editor_apply_charfmt(ow_editor *editor, const owf_charfmt *fmt,
                            unsigned mask)
{
    ow_position start, end, old_a, old_f;
    int pi;
    size_t aoff, foff;
    if (!editor || !fmt || !mask) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (ow_editor_selection_empty(editor)) {
        if (!editor->typing_fmt_set) sync_typing_fmt(editor);
        apply_charfmt_fields(&editor->typing_fmt, fmt, mask);
        return OWF_OK;
    }

    old_a = editor->selection.anchor;
    old_f = editor->selection.focus;
    aoff = para_text_offset(&editor->doc->body.paras[old_a.paragraph], old_a);
    foff = para_text_offset(&editor->doc->body.paras[old_f.paragraph], old_f);
    if (compare_position(old_a, old_f) <= 0) { start = old_a; end = old_f; }
    else { start = old_f; end = old_a; }
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;

    for (pi = start.paragraph; pi <= end.paragraph; ++pi) {
        owf_para *p = &editor->doc->body.paras[pi];
        size_t from = pi == start.paragraph ? para_text_offset(p, start) : 0;
        size_t to = pi == end.paragraph ? para_text_offset(p, end) : para_text_length(p);
        if (to > from && !rebuild_para_charfmt(p, from, to, fmt, mask))
            return OWF_ERR_MEMORY;
    }
    editor->selection.anchor = para_offset_position(editor, old_a.paragraph, aoff);
    editor->selection.focus = para_offset_position(editor, old_f.paragraph, foff);
    editor->dirty = 1;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_toggle_char_flags(ow_editor *editor, unsigned flags)
{
    owf_charfmt fmt;
    if (!editor || !flags || !ow_editor_current_charfmt(editor, &fmt)) return OWF_ERR_FORMAT;
    fmt.flags ^= flags;
    return ow_editor_apply_charfmt(editor, &fmt, OW_CHARFMT_FLAGS);
}

int ow_editor_current_parafmt(const ow_editor *editor, owf_parafmt *fmt)
{
    int p;
    if (!editor || !editor->doc || !fmt) return 0;
    p = editor->selection.focus.paragraph;
    if (p < 0 || p >= editor->doc->body.nparas) return 0;
    *fmt = editor->doc->body.paras[p].fmt;
    return 1;
}

int ow_editor_apply_parafmt(ow_editor *editor, const owf_parafmt *fmt,
                            unsigned mask)
{
    int first, last, p;
    if (!editor || !editor->doc || !fmt || !mask) return OWF_ERR_FORMAT;
    first = editor->selection.anchor.paragraph;
    last = editor->selection.focus.paragraph;
    if (first > last) { int t = first; first = last; last = t; }
    if (first < 0) first = 0;
    if (last >= editor->doc->body.nparas) last = editor->doc->body.nparas - 1;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    for (p = first; p <= last; ++p) {
        owf_parafmt *dst = &editor->doc->body.paras[p].fmt;
        if (mask & OW_PARAFMT_HEADING) dst->heading = fmt->heading;
        if (mask & OW_PARAFMT_ALIGN) dst->align = fmt->align;
        if (mask & OW_PARAFMT_INDENTS) {
            dst->indent_left = fmt->indent_left;
            dst->indent_right = fmt->indent_right;
            dst->indent_first = fmt->indent_first;
        }
        if (mask & OW_PARAFMT_SPACING) {
            dst->space_before = fmt->space_before;
            dst->space_after = fmt->space_after;
            dst->line_spacing = fmt->line_spacing;
        }
    }
    editor->typing_group = 0;
    editor->dirty = 1;
    ow_editor_layout(editor);
    return OWF_OK;
}

static int paragraph_list_prefix(const owf_para *p, int *ordered, size_t *length)
{
    int r = first_text_run(p);
    const char *s;
    size_t i = 0;
    if (ordered) *ordered = -1;
    if (length) *length = 0;
    if (r < 0 || !p->runs[r].text) return 0;
    s = p->runs[r].text;
    if (!strncmp(s, "\xe2\x80\xa2 ", 4)) {
        if (ordered) *ordered = 0;
        if (length) *length = 4;
        return 1;
    }
    while (s[i] >= '0' && s[i] <= '9') ++i;
    if (i && s[i] == '.' && s[i + 1] == ' ') {
        if (ordered) *ordered = 1;
        if (length) *length = i + 2;
        return 1;
    }
    return 0;
}

int ow_editor_toggle_list(ow_editor *editor, int ordered)
{
    int first, last, p, all_same = 1, changed = 0;
    ow_position old_a, old_f;
    size_t aoff, foff;
    if (!editor || !editor->doc || (ordered != 0 && ordered != 1)) return OWF_ERR_FORMAT;
    first = editor->selection.anchor.paragraph;
    last = editor->selection.focus.paragraph;
    if (first > last) { int t = first; first = last; last = t; }
    if (first < 0) first = 0;
    if (last >= editor->doc->body.nparas) last = editor->doc->body.nparas - 1;
    for (p = first; p <= last; ++p) {
        int kind = -1;
        paragraph_list_prefix(&editor->doc->body.paras[p], &kind, NULL);
        if (kind != ordered) { all_same = 0; break; }
    }
    old_a = editor->selection.anchor;
    old_f = editor->selection.focus;
    aoff = para_text_offset(&editor->doc->body.paras[old_a.paragraph], old_a);
    foff = para_text_offset(&editor->doc->body.paras[old_f.paragraph], old_f);
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;

    for (p = first; p <= last; ++p) {
        owf_para *para = &editor->doc->body.paras[p];
        int r = first_text_run(para), old_kind = -1;
        size_t old_len = 0, add_len = 0;
        char prefix[32];
        if (r < 0) {
            if (!append_empty_text_run(para, &editor->doc->base)) return OWF_ERR_MEMORY;
            r = para->nruns - 1;
        }
        paragraph_list_prefix(para, &old_kind, &old_len);
        if (old_len) {
            if (!replace_run_slice(&para->runs[r], 0, old_len, NULL, 0)) return OWF_ERR_MEMORY;
            changed = 1;
            if (old_a.paragraph == p) aoff = aoff > old_len ? aoff - old_len : 0;
            if (old_f.paragraph == p) foff = foff > old_len ? foff - old_len : 0;
        }
        if (!all_same) {
            if (ordered == 0) snprintf(prefix, sizeof prefix, "%s", "\xe2\x80\xa2 ");
            else snprintf(prefix, sizeof prefix, "%d. ", p - first + 1);
            add_len = strlen(prefix);
            if (!replace_run_slice(&para->runs[r], 0, 0, prefix, add_len)) return OWF_ERR_MEMORY;
            if (old_a.paragraph == p) aoff += add_len;
            if (old_f.paragraph == p) foff += add_len;
            para->fmt.indent_left = 360;
            para->fmt.indent_first = -360;
            changed = 1;
        } else {
            para->fmt.indent_left = 0;
            para->fmt.indent_first = 0;
        }
    }
    if (!changed) {
        snapshot_free(&editor->undo[editor->nundo - 1]);
        --editor->nundo;
        return OWF_OK;
    }
    editor->selection.anchor = para_offset_position(editor, old_a.paragraph, aoff);
    editor->selection.focus = para_offset_position(editor, old_f.paragraph, foff);
    editor->typing_group = 0;
    editor->dirty = 1;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

static char *paragraph_plain_text(const owf_para *p, size_t *out_len)
{
    size_t n = para_text_length(p), used = 0;
    char *s = (char *)malloc(n + 1);
    int i;
    if (!s) return NULL;
    for (i = 0; i < p->nruns; ++i) {
        const owf_run *r = &p->runs[i];
        size_t rn;
        if (r->kind != OWF_RUN_TEXT || !r->text) continue;
        rn = strlen(r->text);
        memcpy(s + used, r->text, rn);
        used += rn;
    }
    s[used] = 0;
    if (out_len) *out_len = used;
    return s;
}

static unsigned char lower_ascii(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int text_matches_at(const char *hay, size_t hlen, size_t at,
                           const char *needle, size_t nlen, int match_case)
{
    size_t i;
    if (at + nlen > hlen) return 0;
    for (i = 0; i < nlen; ++i) {
        unsigned char a = (unsigned char)hay[at + i];
        unsigned char b = (unsigned char)needle[i];
        if (!match_case) { a = lower_ascii(a); b = lower_ascii(b); }
        if (a != b) return 0;
    }
    return 1;
}

static long find_forward_in(const char *hay, size_t hlen, const char *needle,
                            size_t nlen, size_t start, int match_case)
{
    size_t i;
    if (start > hlen) start = hlen;
    for (i = start; i + nlen <= hlen; ++i)
        if (text_matches_at(hay, hlen, i, needle, nlen, match_case)) return (long)i;
    return -1;
}

static long find_backward_in(const char *hay, size_t hlen, const char *needle,
                             size_t nlen, size_t before, int match_case)
{
    size_t i;
    if (nlen > hlen) return -1;
    if (before > hlen) before = hlen;
    i = before >= nlen ? before - nlen : 0;
    for (;;) {
        if (i + nlen <= before && text_matches_at(hay, hlen, i, needle, nlen, match_case)) return (long)i;
        if (!i) break;
        --i;
    }
    return -1;
}

int ow_editor_find(ow_editor *editor, const char *needle, int backwards,
                   int match_case, int wrap)
{
    ow_position startpos, endpos;
    size_t nlen;
    int total, step, pass;
    if (!editor || !editor->doc || !needle || !(nlen = strlen(needle))) return 0;
    total = editor->doc->body.nparas;
    if (!total) return 0;
    if (ow_editor_selection_empty(editor)) startpos = editor->selection.focus;
    else if (!backwards)
        startpos = compare_position(editor->selection.anchor, editor->selection.focus) >= 0
            ? editor->selection.anchor : editor->selection.focus;
    else
        startpos = compare_position(editor->selection.anchor, editor->selection.focus) <= 0
            ? editor->selection.anchor : editor->selection.focus;
    startpos = clamp_position(editor, startpos);
    step = backwards ? -1 : 1;

    for (pass = 0; pass < total + (wrap ? 1 : 0); ++pass) {
        int pi = startpos.paragraph + pass * step;
        const owf_para *p;
        char *plain;
        size_t plen, from;
        long hit;
        if (pi < 0 || pi >= total) {
            if (!wrap) break;
            pi %= total;
            if (pi < 0) pi += total;
        }
        p = &editor->doc->body.paras[pi];
        plain = paragraph_plain_text(p, &plen);
        if (!plain) return -1;
        if (pass == 0) from = para_text_offset(p, startpos);
        else from = backwards ? plen : 0;
        hit = backwards
            ? find_backward_in(plain, plen, needle, nlen, from, match_case)
            : find_forward_in(plain, plen, needle, nlen, from, match_case);
        free(plain);
        if (hit >= 0) {
            startpos = para_offset_position(editor, pi, (size_t)hit);
            endpos = para_offset_position(editor, pi, (size_t)hit + nlen);
            editor->selection.anchor = startpos;
            editor->selection.focus = endpos;
            editor->typing_group = 0;
            sync_typing_fmt(editor);
            return 1;
        }
    }
    return 0;
}

int ow_editor_insert_utf8(ow_editor *editor, const char *utf8, size_t length)
{
    ow_position pos;
    owf_run *run;
    int continuing;

    if (!editor || !utf8 || !length) return OWF_OK;
    pos = clamp_position(editor, editor->selection.focus);
    continuing = ow_editor_selection_empty(editor) &&
                 editor->typing_group &&
                 same_position(pos, editor->typing_next);

    if (!continuing && !remember_before_edit(editor)) return OWF_ERR_MEMORY;
    if (!ow_editor_selection_empty(editor)) {
        if (delete_selection_internal(editor) < 0) return OWF_ERR_MEMORY;
        pos = editor->selection.focus;
    }

    pos = clamp_position(editor, pos);
    run = &editor->doc->body.paras[pos.paragraph].runs[pos.run];
    if (!editor->typing_fmt_set) editor->typing_fmt = run->fmt;
    editor->typing_fmt_set = 1;
    if (!insert_formatted_text(editor, pos, utf8, length,
                               &editor->typing_fmt, &pos))
        return OWF_ERR_MEMORY;
    editor->selection.anchor = pos;
    editor->selection.focus = pos;
    editor->typing_group = 1;
    editor->typing_next = pos;
    editor->dirty = 1;
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_insert_text_block(ow_editor *editor, const char *utf8, size_t length)
{
    ow_position pos, next;
    size_t i = 0, start = 0;
    int had_selection, changed = 0;
    if (!editor || (!utf8 && length)) return OWF_ERR_FORMAT;
    had_selection = !ow_editor_selection_empty(editor);
    if (!length && !had_selection) return OWF_OK;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    if (had_selection) {
        if (delete_selection_internal(editor) < 0) return OWF_ERR_MEMORY;
        changed = 1;
    }
    pos = clamp_position(editor, editor->selection.focus);
    if (!editor->typing_fmt_set) sync_typing_fmt(editor);

    while (i <= length) {
        int newline = 0;
        if (i == length || utf8[i] == '\r' || utf8[i] == '\n') {
            if (i > start) {
                if (!insert_formatted_text(editor, pos, utf8 + start, i - start,
                                           &editor->typing_fmt, &pos))
                    return OWF_ERR_MEMORY;
                changed = 1;
            }
            if (i < length) {
                newline = 1;
                if (utf8[i] == '\r' && i + 1 < length && utf8[i + 1] == '\n') ++i;
                if (split_paragraph(editor, pos, &next) < 0) return OWF_ERR_MEMORY;
                pos = next;
                changed = 1;
            }
            start = i + 1;
        }
        ++i;
        (void)newline;
    }
    if (!changed) {
        snapshot_free(&editor->undo[editor->nundo - 1]);
        --editor->nundo;
        return OWF_OK;
    }
    editor->selection.anchor = pos;
    editor->selection.focus = pos;
    editor->dirty = 1;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_backspace(ow_editor *editor)
{
    ow_position pos;
    int rc;
    if (!editor) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;

    if (!ow_editor_selection_empty(editor)) {
        rc = delete_selection_internal(editor);
    } else {
        pos = editor->selection.focus;
        rc = delete_one_before(editor, &pos);
        editor->selection.anchor = pos;
        editor->selection.focus = pos;
    }

    if (rc < 0) return OWF_ERR_MEMORY;
    if (!rc) {
        /* Nothing changed: discard the unused undo point. */
        snapshot_free(&editor->undo[editor->nundo - 1]);
        --editor->nundo;
        return OWF_OK;
    }
    editor->dirty = 1;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_delete_forward(ow_editor *editor)
{
    ow_position pos;
    int rc;
    if (!editor) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;

    if (!ow_editor_selection_empty(editor)) {
        rc = delete_selection_internal(editor);
    } else {
        pos = editor->selection.focus;
        rc = delete_one_after(editor, &pos);
        editor->selection.anchor = pos;
        editor->selection.focus = pos;
    }

    if (rc < 0) return OWF_ERR_MEMORY;
    if (!rc) {
        snapshot_free(&editor->undo[editor->nundo - 1]);
        --editor->nundo;
        return OWF_OK;
    }
    editor->dirty = 1;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_newline(ow_editor *editor)
{
    ow_position pos, next;
    int rc;
    if (!editor) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;

    if (!ow_editor_selection_empty(editor)) {
        rc = delete_selection_internal(editor);
        if (rc < 0) return OWF_ERR_MEMORY;
    }
    pos = editor->selection.focus;
    rc = split_paragraph(editor, pos, &next);
    if (rc < 0) return OWF_ERR_MEMORY;
    editor->selection.anchor = next;
    editor->selection.focus = next;
    editor->dirty = 1;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_move_caret(ow_editor *editor, ow_move move, int extend_selection)
{
    ow_position pos, a, b;
    if (!editor) return 0;
    editor->typing_group = 0;

    if (!extend_selection && !ow_editor_selection_empty(editor) &&
        (move == OW_MOVE_LEFT || move == OW_MOVE_RIGHT)) {
        a = editor->selection.anchor;
        b = editor->selection.focus;
        pos = move == OW_MOVE_LEFT
            ? (compare_position(a, b) <= 0 ? a : b)
            : (compare_position(a, b) >= 0 ? a : b);
    } else {
        pos = editor->selection.focus;
        switch (move) {
        case OW_MOVE_LEFT:  pos = move_left_pos(editor, pos); break;
        case OW_MOVE_RIGHT: pos = move_right_pos(editor, pos); break;
        case OW_MOVE_UP:    pos = vertical_pos(editor, pos, -1); break;
        case OW_MOVE_DOWN:  pos = vertical_pos(editor, pos, 1); break;
        case OW_MOVE_HOME:  pos = paragraph_home(editor, pos); break;
        case OW_MOVE_END:   pos = paragraph_end(editor, pos); break;
        }
    }

    if (extend_selection) editor->selection.focus = pos;
    else {
        editor->selection.anchor = pos;
        editor->selection.focus = pos;
    }
    sync_typing_fmt(editor);
    return 1;
}

int ow_editor_can_undo(const ow_editor *editor)
{
    return editor && editor->nundo > 0;
}

int ow_editor_can_redo(const ow_editor *editor)
{
    return editor && editor->nredo > 0;
}

int ow_editor_undo(ow_editor *editor)
{
    return restore_from_stack(editor, editor->undo, &editor->nundo,
                              editor->redo, &editor->nredo);
}

int ow_editor_redo(ow_editor *editor)
{
    return restore_from_stack(editor, editor->redo, &editor->nredo,
                              editor->undo, &editor->nundo);
}

int ow_editor_is_dirty(const ow_editor *editor)
{
    return editor ? editor->dirty : 0;
}

void ow_editor_mark_saved(ow_editor *editor)
{
    if (editor) editor->dirty = 0;
}

static int utf8_character_count(const char *s)
{
    int n = 0;
    if (!s) return 0;
    while (*s) {
        if (((unsigned char)*s & 0xc0) != 0x80) ++n;
        ++s;
    }
    return n;
}

static int paragraph_estimated_height_width(const owf_doc *doc, const owf_para *p, int forced_width)
{
    int i, chars = 0, hard_lines = 1, max_size;
    int width, avg_char, chars_per_line, lines, line_height;
    max_size = doc->base.size ? doc->base.size : 240;
    if (p->fmt.heading == 1 && max_size < 360) max_size = 360;
    else if (p->fmt.heading == 2 && max_size < 320) max_size = 320;
    else if (p->fmt.heading == 3 && max_size < 280) max_size = 280;
    for (i = 0; i < p->nruns; ++i) {
        const owf_run *r = &p->runs[i];
        if (r->fmt.size > max_size) max_size = r->fmt.size;
        if (r->kind == OWF_RUN_TEXT && r->text) {
            const char *q = r->text;
            chars += utf8_character_count(q);
            while (*q) { if (*q++ == '\n') ++hard_lines; }
        } else if (r->kind == OWF_RUN_TAB) chars += 4;
        else if (r->kind == OWF_RUN_LINEBREAK) ++hard_lines;
        else if (r->kind == OWF_RUN_IMAGE && r->image >= 0 && r->image < doc->nimages) {
            int ih=doc->images[r->image].height; if(ih>max_size) max_size=ih;
        }
    }
    width = forced_width > 0 ? forced_width : doc->page.width - doc->page.margin_left - doc->page.margin_right;
    width -= p->fmt.indent_left + p->fmt.indent_right;
    if (width < 720) width = 720;
    avg_char = max_size / 2;
    if (avg_char < 80) avg_char = 80;
    chars_per_line = width / avg_char;
    if (chars_per_line < 8) chars_per_line = 8;
    lines = chars ? (chars + chars_per_line - 1) / chars_per_line : 1;
    if (lines < hard_lines) lines = hard_lines;
    line_height = max_size * 6 / 5;
    if (p->fmt.line_spacing > 0) line_height = line_height * p->fmt.line_spacing / 100;
    if (line_height < 240) line_height = 240;
    return lines * line_height;
}

static int paragraph_estimated_height(const owf_doc *doc, const owf_para *p)
{
    return paragraph_estimated_height_width(doc, p, 0);
}

static int paragraph_forces_page_after(const owf_para *p)
{
    int j;
    for (j = 0; j < p->nruns; ++j)
        if (p->runs[j].kind == OWF_RUN_TEXT && p->runs[j].text && strchr(p->runs[j].text, '\f'))
            return 1;
    return 0;
}

int ow_editor_layout(ow_editor *editor)
{
    owf_doc *doc;
    int i, page = 0, y, bottom, n;
    if (!editor || !(doc = editor->doc)) return OWF_ERR_FORMAT;
    n = doc->body.nparas;
    if (n > editor->para_cap) {
        int *pp, *py;
        pp = (int *)realloc(editor->para_page, (size_t)n * sizeof(*pp));
        if (!pp) return OWF_ERR_MEMORY;
        editor->para_page = pp;
        py = (int *)realloc(editor->para_y, (size_t)n * sizeof(*py));
        if (!py) return OWF_ERR_MEMORY;
        editor->para_y = py;
        editor->para_cap = n;
    }
    y = doc->page.margin_top;
    bottom = doc->page.height - doc->page.margin_bottom;
    if (bottom <= y + 240) bottom = doc->page.height;

    for (i = 0; i < n; ++i) {
        const owf_para *p = &doc->body.paras[i];
        if (p->table_id >= 0) {
            int id=p->table_id,row=p->table_row,j=i,cols=p->table_cols>0?p->table_cols:1;
            int cell_width=(doc->page.width-doc->page.margin_left-doc->page.margin_right)/cols;
            int row_height=240,needed;
            while(j<n&&doc->body.paras[j].table_id==id&&doc->body.paras[j].table_row==row){int h=paragraph_estimated_height_width(doc,&doc->body.paras[j],cell_width-240);if(h>row_height)row_height=h;++j;}
            needed=row_height+240;
            if(y>doc->page.margin_top&&y+needed>bottom){++page;y=doc->page.margin_top;}
            while(i<j){editor->para_page[i]=page;editor->para_y[i]=y+120;++i;}--i;y+=needed;continue;
        }
        {
            int height = paragraph_estimated_height(doc, p);
            int needed = p->fmt.space_before + height + p->fmt.space_after;
            if (p->fmt.page_break_before && i > 0) { ++page; y = doc->page.margin_top; }
            if (y > doc->page.margin_top && y + needed > bottom) { ++page; y = doc->page.margin_top; }
            editor->para_page[i] = page;
            editor->para_y[i] = y + p->fmt.space_before;
            y += needed;
            if (paragraph_forces_page_after(p) && i + 1 < n) { ++page; y = doc->page.margin_top; }
        }
    }
    editor->pages = page + 1;
    if (editor->pages < 1) editor->pages = 1;
    return OWF_OK;
}

int ow_editor_page_count(const ow_editor *editor)
{
    return editor ? editor->pages : 0;
}

int ow_editor_current_page(const ow_editor *editor)
{
    int p;
    if (!editor || !editor->doc || !editor->para_page || !editor->doc->body.nparas) return 0;
    p = editor->selection.focus.paragraph;
    if (p < 0) p = 0;
    if (p >= editor->doc->body.nparas) p = editor->doc->body.nparas - 1;
    return editor->para_page[p];
}

void ow_editor_page_setup(const ow_editor *editor, owf_page *page)
{
    if (!page) return;
    memset(page, 0, sizeof(*page));
    if (editor && editor->doc) *page = editor->doc->page;
}

int ow_editor_apply_page_setup(ow_editor *editor, const owf_page *page)
{
    if (!editor || !editor->doc || !page || page->width < 1440 || page->height < 1440)
        return OWF_ERR_FORMAT;
    if (page->margin_left < 0 || page->margin_right < 0 ||
        page->margin_top < 0 || page->margin_bottom < 0 ||
        page->margin_left + page->margin_right >= page->width ||
        page->margin_top + page->margin_bottom >= page->height)
        return OWF_ERR_FORMAT;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    editor->doc->page = *page;
    editor->typing_group = 0;
    editor->dirty = 1;
    return ow_editor_layout(editor);
}

int ow_editor_insert_page_break(ow_editor *editor)
{
    ow_position pos, next;
    int rc;
    if (!editor || !editor->doc) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    if (!ow_editor_selection_empty(editor)) {
        rc = delete_selection_internal(editor);
        if (rc < 0) return OWF_ERR_MEMORY;
    }
    pos = editor->selection.focus;
    rc = split_paragraph(editor, pos, &next);
    if (rc < 0) return OWF_ERR_MEMORY;
    editor->doc->body.paras[next.paragraph].fmt.page_break_before = 1;
    editor->selection.anchor = next;
    editor->selection.focus = next;
    editor->dirty = 1;
    sync_typing_fmt(editor);
    return ow_editor_layout(editor);
}

static int current_table_cell(const ow_editor *editor,int *id,int *row,int *col,int *cols)
{
    int p;if(!editor||!editor->doc)return 0;p=editor->selection.focus.paragraph;if(p<0||p>=editor->doc->body.nparas||editor->doc->body.paras[p].table_id<0)return 0;if(id)*id=editor->doc->body.paras[p].table_id;if(row)*row=editor->doc->body.paras[p].table_row;if(col)*col=editor->doc->body.paras[p].table_col;if(cols)*cols=editor->doc->body.paras[p].table_cols;return 1;
}

int ow_editor_in_table(const ow_editor *editor){return current_table_cell(editor,NULL,NULL,NULL,NULL);}

static void free_para_one(owf_para *p)
{
    int j;for(j=0;j<p->nruns;++j){free(p->runs[j].text);free(p->runs[j].href);}free(p->runs);memset(p,0,sizeof(*p));p->table_id=-1;
}

static void remove_para_at(owf_story *story,int index)
{
    if(index<0||index>=story->nparas)return;
    free_para_one(&story->paras[index]);
    if(index+1<story->nparas)memmove(&story->paras[index],&story->paras[index+1],(size_t)(story->nparas-index-1)*sizeof(story->paras[0]));
    --story->nparas;memset(&story->paras[story->nparas],0,sizeof(story->paras[0]));story->paras[story->nparas].table_id=-1;
}

static void caret_to_para(ow_editor *editor,int p)
{
    ow_position c;owf_para *para;if(p<0)p=0;if(p>=editor->doc->body.nparas)p=editor->doc->body.nparas-1;para=&editor->doc->body.paras[p];if(first_text_run(para)<0)append_empty_text_run(para,&editor->typing_fmt);c.paragraph=p;c.run=first_text_run(para);c.byte_offset=0;editor->selection.anchor=editor->selection.focus=c;
}

int ow_editor_table_move(ow_editor *editor,int delta)
{
    int id,p,target;if(!current_table_cell(editor,&id,NULL,NULL,NULL))return 0;p=editor->selection.focus.paragraph;target=p+(delta<0?-1:1);if(target>=0&&target<editor->doc->body.nparas&&editor->doc->body.paras[target].table_id==id){caret_to_para(editor,target);return 1;}return 0;
}

int ow_editor_table_insert_row(ow_editor *editor)
{
    int id,row,cols,i,pos,c;if(!current_table_cell(editor,&id,&row,NULL,&cols))return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;pos=editor->selection.focus.paragraph;while(pos+1<editor->doc->body.nparas&&editor->doc->body.paras[pos+1].table_id==id&&editor->doc->body.paras[pos+1].table_row==row)++pos;++pos;for(i=0;i<editor->doc->body.nparas;++i)if(editor->doc->body.paras[i].table_id==id&&editor->doc->body.paras[i].table_row>row)++editor->doc->body.paras[i].table_row;for(c=0;c<cols;++c){owf_para *p=insert_paragraph(&editor->doc->body,pos+c,NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}p->table_id=id;p->table_row=row+1;p->table_col=c;p->table_cols=cols;}caret_to_para(editor,pos);editor->dirty=1;return ow_editor_layout(editor);
}

int ow_editor_table_delete_row(ow_editor *editor)
{
    int id,row,i,start=-1,count=0;if(!current_table_cell(editor,&id,&row,NULL,NULL))return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;for(i=0;i<editor->doc->body.nparas;++i)if(editor->doc->body.paras[i].table_id==id&&editor->doc->body.paras[i].table_row==row){if(start<0)start=i;++count;}for(i=0;i<count;++i)remove_para_at(&editor->doc->body,start);for(i=0;i<editor->doc->body.nparas;++i)if(editor->doc->body.paras[i].table_id==id&&editor->doc->body.paras[i].table_row>row)--editor->doc->body.paras[i].table_row;if(!editor->doc->body.nparas){owf_para *p=owf_story_add(&editor->doc->body,NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}}caret_to_para(editor,start<editor->doc->body.nparas?start:editor->doc->body.nparas-1);editor->dirty=1;return ow_editor_layout(editor);
}

int ow_editor_table_insert_column(ow_editor *editor)
{
    int id,col,cols,i,maxrow=-1,r;if(!current_table_cell(editor,&id,NULL,&col,&cols))return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;for(i=0;i<editor->doc->body.nparas;++i)if(editor->doc->body.paras[i].table_id==id){if(editor->doc->body.paras[i].table_row>maxrow)maxrow=editor->doc->body.paras[i].table_row;if(editor->doc->body.paras[i].table_col>col)++editor->doc->body.paras[i].table_col;editor->doc->body.paras[i].table_cols=cols+1;}for(r=maxrow;r>=0;--r){int pos=-1;for(i=0;i<editor->doc->body.nparas;++i)if(editor->doc->body.paras[i].table_id==id&&editor->doc->body.paras[i].table_row==r&&editor->doc->body.paras[i].table_col==col){pos=i+1;break;}if(pos>=0){owf_para *p=insert_paragraph(&editor->doc->body,pos,NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}p->table_id=id;p->table_row=r;p->table_col=col+1;p->table_cols=cols+1;}}editor->dirty=1;return ow_editor_layout(editor);
}

int ow_editor_table_delete_column(ow_editor *editor)
{
    int id,col,cols,i;if(!current_table_cell(editor,&id,NULL,&col,&cols))return OWF_ERR_FORMAT;if(cols<=1)return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;for(i=editor->doc->body.nparas-1;i>=0;--i)if(editor->doc->body.paras[i].table_id==id&&editor->doc->body.paras[i].table_col==col)remove_para_at(&editor->doc->body,i);for(i=0;i<editor->doc->body.nparas;++i)if(editor->doc->body.paras[i].table_id==id){if(editor->doc->body.paras[i].table_col>col)--editor->doc->body.paras[i].table_col;editor->doc->body.paras[i].table_cols=cols-1;}caret_to_para(editor,editor->selection.focus.paragraph<editor->doc->body.nparas?editor->selection.focus.paragraph:editor->doc->body.nparas-1);editor->dirty=1;return ow_editor_layout(editor);
}

int ow_editor_insert_image(ow_editor *editor, int image_index)
{
    ow_position pos,after,caret;owf_story *story;owf_para *p;int rc,base;
    if(!editor||!editor->doc||image_index<0||image_index>=editor->doc->nimages)return OWF_ERR_FORMAT;
    editor->typing_group=0;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;
    if(!ow_editor_selection_empty(editor)&&delete_selection_internal(editor)<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    pos=editor->selection.focus;rc=split_paragraph(editor,pos,&after);if(rc<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    story=&editor->doc->body;base=after.paragraph;p=insert_paragraph(story,base,NULL);if(!p||owf_para_add_image(p,&editor->typing_fmt,image_index)!=OWF_OK){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    caret.paragraph=base+1;caret.run=0;caret.byte_offset=0;editor->selection.anchor=editor->selection.focus=caret;editor->dirty=1;return ow_editor_layout(editor);
}

int ow_editor_insert_table(ow_editor *editor, int rows, int cols)
{
    ow_position pos,after,caret;
    owf_story *story;
    int rc,base,k,r,c,id=0,i;
    if(!editor||rows<1||cols<1||rows>64||cols>32)return OWF_ERR_FORMAT;
    editor->typing_group=0;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;
    if(!ow_editor_selection_empty(editor)&&delete_selection_internal(editor)<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    for(i=0;i<editor->doc->body.nparas;++i)if(editor->doc->body.paras[i].table_id>=id)id=editor->doc->body.paras[i].table_id+1;
    pos=editor->selection.focus;rc=split_paragraph(editor,pos,&after);if(rc<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    story=&editor->doc->body;base=after.paragraph;
    for(k=0,r=0;r<rows;++r)for(c=0;c<cols;++c,++k){owf_para *p=insert_paragraph(story,base+k,NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}p->table_id=id;p->table_row=r;p->table_col=c;p->table_cols=cols;}
    caret.paragraph=base;caret.run=0;caret.byte_offset=0;editor->selection.anchor=editor->selection.focus=caret;editor->dirty=1;return ow_editor_layout(editor);
}

static const char *field_token(owf_field field)
{
    switch (field) {
    case OWF_FIELD_PAGE: return "{PAGE}";
    case OWF_FIELD_PAGES: return "{PAGES}";
    case OWF_FIELD_DATE: return "{DATE}";
    case OWF_FIELD_TIME: return "{TIME}";
    default: return "";
    }
}

static owf_story *story_for(ow_editor *editor, ow_story_kind which)
{
    if (!editor || !editor->doc) return NULL;
    return which == OW_STORY_HEADER ? &editor->doc->header :
           which == OW_STORY_FOOTER ? &editor->doc->footer : NULL;
}

static const owf_story *story_for_const(const ow_editor *editor, ow_story_kind which)
{
    if (!editor || !editor->doc) return NULL;
    return which == OW_STORY_HEADER ? &editor->doc->header :
           which == OW_STORY_FOOTER ? &editor->doc->footer : NULL;
}

char *ow_editor_story_text(const ow_editor *editor, ow_story_kind which)
{
    const owf_story *story = story_for_const(editor, which);
    size_t cap = 128, n = 0;
    char *out;
    int p, r;
    if (!story) return NULL;
    out = (char *)malloc(cap);
    if (!out) return NULL;
    out[0] = 0;
    for (p = 0; p < story->nparas; ++p) {
        const owf_para *para = &story->paras[p];
        for (r = 0; r < para->nruns; ++r) {
            const owf_run *run = &para->runs[r];
            const char *text = NULL;
            size_t add;
            if (run->kind == OWF_RUN_TEXT) text = run->text;
            else if (run->kind == OWF_RUN_TAB) text = "\t";
            else if (run->kind == OWF_RUN_LINEBREAK) text = "\n";
            else if (run->kind == OWF_RUN_FIELD) text = field_token(run->field);
            if (!text) continue;
            add = strlen(text);
            if (n + add + 2 > cap) {
                size_t want = (n + add + 2) * 2;
                char *q = (char *)realloc(out, want);
                if (!q) { free(out); return NULL; }
                out = q; cap = want;
            }
            memcpy(out + n, text, add); n += add; out[n] = 0;
        }
        if (p + 1 < story->nparas) {
            if (n + 2 > cap) {
                char *q = (char *)realloc(out, cap * 2);
                if (!q) { free(out); return NULL; }
                out = q; cap *= 2;
            }
            out[n++] = '\n'; out[n] = 0;
        }
    }
    return out;
}

static int add_story_template_line(owf_para *para, const owf_charfmt *fmt,
                                   const char *text, size_t length)
{
    size_t p = 0, start = 0;
    while (p < length) {
        owf_field field;
        size_t token = 0;
        if (p + 6 <= length && !memcmp(text + p, "{PAGE}", 6)) {
            field = OWF_FIELD_PAGE; token = 6;
        } else if (p + 7 <= length && !memcmp(text + p, "{PAGES}", 7)) {
            field = OWF_FIELD_PAGES; token = 7;
        } else if (p + 6 <= length && !memcmp(text + p, "{DATE}", 6)) {
            field = OWF_FIELD_DATE; token = 6;
        } else if (p + 6 <= length && !memcmp(text + p, "{TIME}", 6)) {
            field = OWF_FIELD_TIME; token = 6;
        }
        if (token) {
            if (p > start && owf_para_add_text(para, fmt, text + start, p - start) != OWF_OK)
                return 0;
            if (owf_para_add_special(para, fmt, OWF_RUN_FIELD, field) != OWF_OK)
                return 0;
            p += token; start = p;
        } else ++p;
    }
    if (p > start && owf_para_add_text(para, fmt, text + start, p - start) != OWF_OK)
        return 0;
    if (!para->nruns && owf_para_add_text(para, fmt, "", 0) != OWF_OK)
        return 0;
    return 1;
}

int ow_editor_set_story_text(ow_editor *editor, ow_story_kind which, const char *text)
{
    owf_story *story = story_for(editor, which);
    owf_charfmt fmt;
    const char *p, *line;
    if (!story || !text) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    free_story_data(story);
    fmt = editor->doc->base;
    line = p = text;
    for (;;) {
        if (*p == '\n' || *p == '\r' || !*p) {
            owf_para *para = owf_story_add(story, NULL);
            if (!para || !add_story_template_line(para, &fmt, line, (size_t)(p - line))) {
                ow_editor_undo(editor);
                return OWF_ERR_MEMORY;
            }
            if (!*p) break;
            if (*p == '\r' && p[1] == '\n') ++p;
            ++p; line = p;
        } else ++p;
    }
    editor->dirty = 1;
    return OWF_OK;
}

static char *copy_cstr(const char *s)
{
    return s ? copy_bytes(s, strlen(s)) : NULL;
}

static int set_link_segment(owf_para *para, int index, size_t from, size_t to,
                            const char *url)
{
    owf_run old, parts[3];
    size_t len;
    int n=0,i,extra;
    if(!para||index<0||index>=para->nruns)return 0;
    old=para->runs[index];
    if(old.kind!=OWF_RUN_TEXT||!old.text)return 1;
    len=strlen(old.text);if(from>len)from=len;if(to>len)to=len;if(to<=from)return 1;
    memset(parts,0,sizeof parts);
#define MAKE_PART(A,B,HREF) do { \
    parts[n]=old;parts[n].text=copy_bytes(old.text+(A),(B)-(A));parts[n].href=copy_cstr(HREF); \
    if(!parts[n].text||((HREF)&&!parts[n].href)){for(i=0;i<=n;++i){free(parts[i].text);free(parts[i].href);}return 0;} ++n; \
} while(0)
    if(from) MAKE_PART(0,from,old.href);
    MAKE_PART(from,to,url);
    if(to<len) MAKE_PART(to,len,old.href);
#undef MAKE_PART
    extra=n-1;
    if(extra>0&&!ensure_run_capacity(para,para->nruns+extra)){for(i=0;i<n;++i){free(parts[i].text);free(parts[i].href);}return 0;}
    if(extra>0&&index+1<para->nruns)memmove(&para->runs[index+n],&para->runs[index+1],(size_t)(para->nruns-index-1)*sizeof(para->runs[0]));
    free(old.text);free(old.href);
    for(i=0;i<n;++i)para->runs[index+i]=parts[i];
    para->nruns+=extra;
    return 1;
}

int ow_editor_set_link(ow_editor *editor, const char *url)
{
    ow_position a,b;
    int pi,ri;
    if(!editor||!url||!*url||ow_editor_selection_empty(editor))return OWF_ERR_FORMAT;
    a=editor->selection.anchor;b=editor->selection.focus;
    if(compare_position(a,b)>0){ow_position t=a;a=b;b=t;}
    editor->typing_group=0;
    if(!remember_before_edit(editor))return OWF_ERR_MEMORY;
    for(pi=b.paragraph;pi>=a.paragraph;--pi){
        owf_para *p=&editor->doc->body.paras[pi];
        for(ri=p->nruns-1;ri>=0;--ri){
            owf_run *r=&p->runs[ri];size_t len,from=0,to;
            if(r->kind!=OWF_RUN_TEXT||!r->text)continue;
            len=strlen(r->text);to=len;
            if(pi==a.paragraph&&ri<a.run)continue;
            if(pi==b.paragraph&&ri>b.run)continue;
            if(pi==a.paragraph&&ri==a.run)from=a.byte_offset;
            if(pi==b.paragraph&&ri==b.run)to=b.byte_offset;
            if(to>from&&!set_link_segment(p,ri,from,to,url)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
        }
    }
    editor->dirty=1;return OWF_OK;
}

int ow_editor_insert_link(ow_editor *editor, const char *text, const char *url)
{
    ow_position pos;
    owf_para *para;
    owf_run *run;
    owf_charfmt fmt;
    char *right,*oldhref,*linked;
    size_t right_len;
    int old_n,move;
    if(!editor||!text||!*text||!url||!*url)return OWF_ERR_FORMAT;
    editor->typing_group=0;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;
    if(!ow_editor_selection_empty(editor)&&delete_selection_internal(editor)<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    pos=clamp_position(editor,editor->selection.focus);para=&editor->doc->body.paras[pos.paragraph];run=&para->runs[pos.run];fmt=run->fmt;
    right_len=strlen(run->text)-pos.byte_offset;right=copy_bytes(run->text+pos.byte_offset,right_len);oldhref=copy_cstr(run->href);linked=copy_bytes(text,strlen(text));
    if(!right||!linked||(run->href&&!oldhref)){free(right);free(oldhref);free(linked);ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    if(!replace_run_slice(run,pos.byte_offset,strlen(run->text),NULL,0)){free(right);free(oldhref);free(linked);ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    old_n=para->nruns;if(!ensure_run_capacity(para,old_n+2)){free(right);free(oldhref);free(linked);ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    move=old_n-pos.run-1;if(move>0)memmove(&para->runs[pos.run+3],&para->runs[pos.run+1],(size_t)move*sizeof(para->runs[0]));
    memset(&para->runs[pos.run+1],0,2*sizeof(para->runs[0]));
    para->runs[pos.run+1].kind=OWF_RUN_TEXT;para->runs[pos.run+1].fmt=fmt;para->runs[pos.run+1].text=linked;para->runs[pos.run+1].href=copy_cstr(url);
    para->runs[pos.run+2].kind=OWF_RUN_TEXT;para->runs[pos.run+2].fmt=fmt;para->runs[pos.run+2].text=right;para->runs[pos.run+2].href=oldhref;
    if(!para->runs[pos.run+1].href){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    para->nruns=old_n+2;pos.run+=2;pos.byte_offset=0;editor->selection.anchor=editor->selection.focus=pos;editor->dirty=1;return ow_editor_layout(editor);
}

int ow_editor_insert_field(ow_editor *editor, owf_field field)
{
    ow_position pos;
    owf_para *para;
    owf_run *run;
    owf_charfmt fmt;
    char *right;
    size_t right_len;
    int old_n, move;
    if (!editor || field < OWF_FIELD_PAGE || field > OWF_FIELD_TIME) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    if (!ow_editor_selection_empty(editor) && delete_selection_internal(editor) < 0) {
        ow_editor_undo(editor); return OWF_ERR_MEMORY;
    }
    pos = clamp_position(editor, editor->selection.focus);
    para = &editor->doc->body.paras[pos.paragraph];
    run = &para->runs[pos.run];
    fmt = run->fmt;
    right_len = strlen(run->text) - pos.byte_offset;
    right = copy_bytes(run->text + pos.byte_offset, right_len);
    if (!right) { ow_editor_undo(editor); return OWF_ERR_MEMORY; }
    if (!replace_run_slice(run, pos.byte_offset, strlen(run->text), NULL, 0)) {
        free(right); ow_editor_undo(editor); return OWF_ERR_MEMORY;
    }
    old_n = para->nruns;
    if (!ensure_run_capacity(para, old_n + 2)) {
        free(right); ow_editor_undo(editor); return OWF_ERR_MEMORY;
    }
    move = old_n - pos.run - 1;
    if (move > 0)
        memmove(&para->runs[pos.run + 3], &para->runs[pos.run + 1],
                (size_t)move * sizeof(para->runs[0]));
    memset(&para->runs[pos.run + 1], 0, 2 * sizeof(para->runs[0]));
    para->runs[pos.run + 1].kind = OWF_RUN_FIELD;
    para->runs[pos.run + 1].fmt = fmt;
    para->runs[pos.run + 1].field = field;
    para->runs[pos.run + 2].kind = OWF_RUN_TEXT;
    para->runs[pos.run + 2].fmt = fmt;
    para->runs[pos.run + 2].text = right;
    para->nruns = old_n + 2;
    pos.run += 2; pos.byte_offset = 0;
    editor->selection.anchor = editor->selection.focus = pos;
    editor->typing_fmt = fmt; editor->typing_fmt_set = 1;
    editor->dirty = 1;
    return ow_editor_layout(editor);
}

static const char *field_display(owf_field field, int page, int pages,
                                 char *buffer, size_t size)
{
    time_t now;
    struct tm *tmv;
    switch (field) {
    case OWF_FIELD_PAGE: snprintf(buffer, size, "%d", page + 1); break;
    case OWF_FIELD_PAGES: snprintf(buffer, size, "%d", pages); break;
    case OWF_FIELD_DATE:
    case OWF_FIELD_TIME:
        now = time(NULL); tmv = localtime(&now);
        if (tmv) strftime(buffer, size, field == OWF_FIELD_DATE ? "%d/%m/%Y" : "%H:%M", tmv);
        else buffer[0] = 0;
        break;
    default: buffer[0] = 0; break;
    }
    return buffer;
}

static void render_story(const ow_editor *editor, const owf_story *story,
                         int para_base, int x, int y, int page_index,
                         const ow_renderer *renderer)
{
    int i, j;
    if (!story) return;
    for (i = 0; i < story->nparas; ++i) {
        const owf_para *p = &story->paras[i];
        int px = x + p->fmt.indent_left;
        y += p->fmt.space_before;
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *run = &p->runs[j];
            const char *text = NULL;
            char field[64];
            if (run->kind == OWF_RUN_TEXT) text = run->text;
            else if (run->kind == OWF_RUN_IMAGE) {
                if(renderer->image && run->image>=0 && run->image<editor->doc->nimages){const owf_image *im=&editor->doc->images[run->image];renderer->image(renderer->userdata,run->image,x,y,im->width,im->height);}
                continue;
            }
            else if (run->kind == OWF_RUN_TAB) text = "    ";
            else if (run->kind == OWF_RUN_LINEBREAK) text = "\n";
            else if (run->kind == OWF_RUN_FIELD)
                text = field_display(run->field, page_index, editor->pages, field, sizeof field);
            if (!text) continue;
            if (renderer->text_run)
                renderer->text_run(renderer->userdata, para_base - i, j, px, y, text, &run->fmt);
            else if (renderer->text)
                renderer->text(renderer->userdata, px, y, text, &run->fmt);
        }
        y += p->fmt.space_after + 240;
    }
}

int ow_editor_render_page(const ow_editor *editor, int page_index,
                          const ow_renderer *renderer)
{
    ow_page_info page;
    int i, j;
    if (!editor || !editor->doc || !renderer) return OWF_ERR_FORMAT;
    if (page_index < 0 || page_index >= editor->pages) return OWF_ERR_FORMAT;

    page.width_twips = editor->doc->page.width;
    page.height_twips = editor->doc->page.height;
    page.page_index = page_index;
    if (renderer->begin_page) renderer->begin_page(renderer->userdata, &page);

    if ((page_index > 0 || editor->doc->page.header_on_first) && editor->doc->header.nparas)
        render_story(editor, &editor->doc->header, -1000,
                     editor->doc->page.margin_left,
                     editor->doc->page.margin_top / 3,
                     page_index, renderer);

    for (i = 0; i < editor->doc->body.nparas; ++i) {
        const owf_para *p;
        int x, y;
        if (!editor->para_page || editor->para_page[i] != page_index) continue;
        p = &editor->doc->body.paras[i];
        if (p->table_id >= 0) {
            int cols=p->table_cols>0?p->table_cols:1;
            int cw=(editor->doc->page.width-editor->doc->page.margin_left-editor->doc->page.margin_right)/cols;
            int rh=paragraph_estimated_height_width(editor->doc,p,cw-240)+240;
            x=editor->doc->page.margin_left+p->table_col*cw+120+p->fmt.indent_left;
            y=editor->para_y?editor->para_y[i]:editor->doc->page.margin_top;
            if(renderer->rule){int left=editor->doc->page.margin_left+p->table_col*cw, top=y-120, right=left+cw, bottom=top+rh;renderer->rule(renderer->userdata,left,top,right,top,0x808080);renderer->rule(renderer->userdata,left,bottom,right,bottom,0x808080);renderer->rule(renderer->userdata,left,top,left,bottom,0x808080);renderer->rule(renderer->userdata,right,top,right,bottom,0x808080);}
        } else {
            x = editor->doc->page.margin_left + p->fmt.indent_left;
            y = editor->para_y ? editor->para_y[i] : editor->doc->page.margin_top;
        }
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *run = &p->runs[j];
            const char *text = NULL;
            char field[64];
            if (run->kind == OWF_RUN_TEXT) text = run->text;
            else if (run->kind == OWF_RUN_IMAGE) {
                if(renderer->image && run->image>=0 && run->image<editor->doc->nimages){const owf_image *im=&editor->doc->images[run->image];renderer->image(renderer->userdata,run->image,x,y,im->width,im->height);}
                continue;
            }
            else if (run->kind == OWF_RUN_TAB) text = "    ";
            else if (run->kind == OWF_RUN_LINEBREAK) text = "\n";
            else if (run->kind == OWF_RUN_FIELD)
                text = field_display(run->field, page_index, editor->pages, field, sizeof field);
            if (!text) continue;
            if (renderer->text_run)
                renderer->text_run(renderer->userdata, i, j, x, y, text, &run->fmt);
            else if (renderer->text)
                renderer->text(renderer->userdata, x, y, text, &run->fmt);
        }
    }

    if ((page_index > 0 || editor->doc->page.footer_on_first) && editor->doc->footer.nparas)
        render_story(editor, &editor->doc->footer, -2000,
                     editor->doc->page.margin_left,
                     editor->doc->page.height - editor->doc->page.margin_bottom / 2,
                     page_index, renderer);

    if (renderer->end_page) renderer->end_page(renderer->userdata, &page);
    return OWF_OK;
}

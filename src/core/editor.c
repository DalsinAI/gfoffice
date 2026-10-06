#include <stdlib.h>
#include <string.h>
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
    int dirty;

    ow_snapshot undo[OW_UNDO_LIMIT];
    int nundo;
    ow_snapshot redo[OW_UNDO_LIMIT];
    int nredo;

    int typing_group;
    ow_position typing_next;
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
        for (j = 0; j < story->paras[i].nruns; ++j)
            free(story->paras[i].runs[j].text);
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
        if (!sp->nruns) continue;
        dp->runs = (owf_run *)calloc((size_t)sp->nruns, sizeof(*dp->runs));
        if (!dp->runs) return 0;
        dp->nruns = dp->capruns = sp->nruns;
        for (j = 0; j < sp->nruns; ++j) {
            dp->runs[j] = sp->runs[j];
            dp->runs[j].text = NULL;
            if (sp->runs[j].text) {
                dp->runs[j].text = copy_bytes(sp->runs[j].text,
                                               strlen(sp->runs[j].text));
                if (!dp->runs[j].text) return 0;
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
    return editor;
}

void ow_editor_free(ow_editor *editor)
{
    if (!editor) return;
    stack_clear(editor->undo, &editor->nundo);
    stack_clear(editor->redo, &editor->nredo);
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
    if (!replace_run_slice(run, pos.byte_offset, pos.byte_offset,
                           utf8, length))
        return OWF_ERR_MEMORY;

    pos.byte_offset += length;
    editor->selection.anchor = pos;
    editor->selection.focus = pos;
    editor->typing_group = 1;
    editor->typing_next = pos;
    editor->dirty = 1;
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
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_move_caret(ow_editor *editor, ow_move move, int extend_selection)
{
    ow_position pos, a, b;
    if (!editor) return 0;
    editor->typing_group = 0;

    if (!extend_selection && !ow_editor_selection_empty(editor)) {
        a = editor->selection.anchor;
        b = editor->selection.focus;
        if (move == OW_MOVE_LEFT || move == OW_MOVE_HOME)
            pos = compare_position(a, b) <= 0 ? a : b;
        else if (move == OW_MOVE_RIGHT || move == OW_MOVE_END)
            pos = compare_position(a, b) >= 0 ? a : b;
        else
            pos = b;
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
            if (run->kind != OWF_RUN_TEXT || !run->text) continue;
            if (renderer->text_run)
                renderer->text_run(renderer->userdata, i, j, x, y,
                                   run->text, &run->fmt);
            else if (renderer->text)
                renderer->text(renderer->userdata, x, y,
                               run->text, &run->fmt);
        }
        y += p->fmt.space_after + 240;
    }

    if (renderer->end_page) renderer->end_page(renderer->userdata, &page);
    return OWF_OK;
}

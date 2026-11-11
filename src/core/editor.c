#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "openwrite_core.h"

#define OW_UNDO_LIMIT 16
/* 2.0.11: what the undo and redo snapshots may hold between them, about; the last edit's is always kept */
#define OW_UNDO_BYTES (4UL * 1024UL * 1024UL)

typedef struct {
    owf_doc *doc;               /* without the pictures: those stay the document's (clone_doc) */
    ow_selection selection;
    int dirty;
    unsigned long bytes;        /* about what doc holds */
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
    unsigned long changes;      /* every change to the document (ow_editor_changes) */

    ow_snapshot undo[OW_UNDO_LIMIT];
    int nundo;
    ow_snapshot redo[OW_UNDO_LIMIT];
    int nredo;

    int typing_group;
    ow_position typing_next;
    owf_charfmt typing_fmt;
    int typing_fmt_set;

    /* 2.1.0: tracked changes. track: new edits are recorded; author: who makes them; typing_rev: the change
     * text typed now is (0: none); ins_rev, del_rev and their minute: the last numbers made, used again by
     * the same author in the same minute. */
    int track;
    char *author;
    int typing_rev;
    int ins_rev, del_rev;
    char ins_minute[32], del_minute[32];
};

/* ---- small allocation helpers -------------------------------------------------- */

static char *copy_bytes(const char *s, size_t n)
{
    char *p = (char *)owf_malloc(n + 1);
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
            owf_free(story->paras[i].runs[j].text);
            owf_free(story->paras[i].runs[j].href);
        }
        owf_free(story->paras[i].runs);
        owf_free(story->paras[i].style);             /* 2.0.12: libowf 1.4's names */
        owf_free(story->paras[i].bookmarks);
    }
    owf_free(story->paras);
    memset(story, 0, sizeof(*story));
}

static void clear_doc_data(owf_doc *doc)
{
    int i;
    if (!doc) return;
    free_story_data(&doc->body);
    free_story_data(&doc->header);
    free_story_data(&doc->footer);
    free_story_data(&doc->header_left);              /* 2.0.23 */
    free_story_data(&doc->footer_left);
    for (i = 0; i < doc->nfonts; ++i) owf_free(doc->fonts[i].name);
    owf_free(doc->fonts);
    for (i = 0; i < doc->nimages; ++i) { owf_free(doc->images[i].name); owf_free(doc->images[i].mime); owf_free(doc->images[i].alt); owf_free(doc->images[i].data); }
    owf_free(doc->images);
    for (i = 0; i < doc->nframes; ++i) free_story_data(&doc->frames[i].story);   /* 2.0.10: frames */
    owf_free(doc->frames);
    owf_free(doc->title);
    for (i = 0; i < doc->nrevisions; ++i) { owf_free(doc->revisions[i].author); owf_free(doc->revisions[i].date); }
    owf_free(doc->revisions);                        /* 2.1.0 */
    for (i = 0; i < doc->ncomments; ++i) {          /* 2.2.0 */
        owf_free(doc->comments[i].author); owf_free(doc->comments[i].initials);
        owf_free(doc->comments[i].date); owf_free(doc->comments[i].text);
    }
    owf_free(doc->comments);
    memset(doc, 0, sizeof(*doc));
}

static int clone_story(owf_story *dst, const owf_story *src)
{
    int i, j;
    memset(dst, 0, sizeof(*dst));
    if (!src->nparas) return 1;
    dst->paras = (owf_para *)owf_calloc((size_t)src->nparas, sizeof(*dst->paras));
    if (!dst->paras) return 0;
    dst->nparas = dst->capparas = src->nparas;
    for (i = 0; i < src->nparas; ++i) {
        const owf_para *sp = &src->paras[i];
        owf_para *dp = &dst->paras[i];
        dp->fmt = sp->fmt;
        dp->table_id = sp->table_id; dp->table_row = sp->table_row;
        dp->table_col = sp->table_col; dp->table_cols = sp->table_cols;
        dp->cell_shading = sp->cell_shading;         /* 2.0.10: undo kept no cell colour */
        dp->section_columns = sp->section_columns;   /* 2.0.25: section starts survive undo */
        dp->section_gap = sp->section_gap;
        dp->section_break = sp->section_break;
        dp->mark_revision = sp->mark_revision;       /* 2.1.0 */
        dp->style = NULL;                            /* 2.0.12: its style and bookmarks too */
        dp->bookmarks = NULL;
        if (sp->style && !(dp->style = copy_bytes(sp->style, strlen(sp->style)))) return 0;
        if (sp->bookmarks && !(dp->bookmarks = copy_bytes(sp->bookmarks, strlen(sp->bookmarks)))) return 0;
        if (!sp->nruns) continue;
        dp->runs = (owf_run *)owf_calloc((size_t)sp->nruns, sizeof(*dp->runs));
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
    dst = (owf_doc *)owf_calloc(1, sizeof(*dst));
    if (!dst) return NULL;
    dst->base = src->base;
    dst->page = src->page;
    if (src->title) {
        dst->title = copy_bytes(src->title, strlen(src->title));
        if (!dst->title) goto fail;
    }
    if (src->nfonts) {
        dst->fonts = (owf_font *)owf_calloc((size_t)src->nfonts, sizeof(*dst->fonts));
        if (!dst->fonts) goto fail;
        dst->nfonts = dst->capfonts = src->nfonts;
        for (i = 0; i < src->nfonts; ++i) {
            dst->fonts[i].kind = src->fonts[i].kind;
            dst->fonts[i].name = copy_bytes(src->fonts[i].name,
                                             strlen(src->fonts[i].name));
            if (!dst->fonts[i].name) goto fail;
        }
    }
    /* 2.0.11: no pictures. An edit only ever adds one (ow_editor_insert_image, at the end, and the runs name
     * them by index), so the document keeps all it has had through undo and redo (restore_doc), and a
     * snapshot no longer copies them: one undo copied the plan's 0.5 MB cover JPEG and the rest each time. */
    if (!clone_story(&dst->body, &src->body) ||
        !clone_story(&dst->header, &src->header) ||
        !clone_story(&dst->footer, &src->footer) ||
        !clone_story(&dst->header_left, &src->header_left) ||          /* 2.0.23 */
        !clone_story(&dst->footer_left, &src->footer_left))
        goto fail;
    if (src->nframes) {                              /* 2.0.10: frames, their text boxes' paragraphs too */
        dst->frames = (owf_frame *)owf_calloc((size_t)src->nframes, sizeof(*dst->frames));
        if (!dst->frames) goto fail;
        dst->nframes = dst->capframes = src->nframes;
        for (i = 0; i < src->nframes; ++i) {
            dst->frames[i] = src->frames[i];
            memset(&dst->frames[i].story, 0, sizeof dst->frames[i].story);
            if (!clone_story(&dst->frames[i].story, &src->frames[i].story)) goto fail;
        }
    }
    return dst;
fail:
    owf_doc_free(dst);
    return NULL;
}

static void restore_doc(owf_doc *dst, owf_doc *snapshot)
{
    owf_image *images;
    owf_revision *revisions;
    owf_annotation *comments;
    int nimages, capimages, nrevisions, caprevisions, track, ncomments, capcomments;
    if (!dst || !snapshot) return;
    images = dst->images;           /* 2.0.11: the pictures stay the document's (clone_doc) */
    nimages = dst->nimages;
    capimages = dst->capimages;
    dst->images = NULL;
    dst->nimages = dst->capimages = 0;
    revisions = dst->revisions;     /* 2.1.0: and so do the changes' authors and dates, which only grow too */
    nrevisions = dst->nrevisions;
    caprevisions = dst->caprevisions;
    track = dst->track_changes;
    dst->revisions = NULL;
    dst->nrevisions = dst->caprevisions = 0;
    comments = dst->comments;       /* 2.2.0: the comments too: the runs name them, and they only grow */
    ncomments = dst->ncomments;
    capcomments = dst->capcomments;
    dst->comments = NULL;
    dst->ncomments = dst->capcomments = 0;
    clear_doc_data(dst);
    *dst = *snapshot;               /* ownership of all children moves */
    dst->images = images;
    dst->nimages = nimages;
    dst->capimages = capimages;
    dst->revisions = revisions;
    dst->nrevisions = nrevisions;
    dst->caprevisions = caprevisions;
    dst->track_changes = track;
    dst->comments = comments;
    dst->ncomments = ncomments;
    dst->capcomments = capcomments;
    owf_free(snapshot);                 /* only the old outer shell */
}

/* About the bytes a snapshot's stories hold (malloc's own a block counted as 8). */
static unsigned long story_bytes(const owf_story *story)
{
    unsigned long n = (unsigned long)story->nparas * sizeof(owf_para) + 8;
    int i, j;
    for (i = 0; i < story->nparas; ++i) {
        n += (unsigned long)story->paras[i].nruns * sizeof(owf_run) + 8;
        if (story->paras[i].style) n += strlen(story->paras[i].style) + 9;
        if (story->paras[i].bookmarks) n += strlen(story->paras[i].bookmarks) + 9;
        for (j = 0; j < story->paras[i].nruns; ++j) {
            const owf_run *r = &story->paras[i].runs[j];
            if (r->text) n += strlen(r->text) + 9;
            if (r->href) n += strlen(r->href) + 9;
        }
    }
    return n;
}

static unsigned long doc_bytes(const owf_doc *doc)
{
    unsigned long n = sizeof(owf_doc) + story_bytes(&doc->body) + story_bytes(&doc->header) +
                      story_bytes(&doc->footer) + story_bytes(&doc->header_left) + story_bytes(&doc->footer_left) +
                      (unsigned long)doc->nfonts * (sizeof(owf_font) + 24);
    int i;
    for (i = 0; i < doc->nframes; ++i) n += sizeof(owf_frame) + story_bytes(&doc->frames[i].story);
    return n;
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

/* 2.0.11: in a paragraph that has runs but no text (a cover's pictures and text boxes, a picture, a tab), the
 * text typed takes its first run's character format, the one its empty line is laid out in (ow_layout), as
 * Word types in the paragraph mark's: the line keeps its height, and what is anchored below it its place. */
static int append_empty_text_run(owf_para *p, const owf_charfmt *fmt)
{
    owf_run *runs;
    owf_charfmt use;
    int want;
    if (!p || !fmt) return 0;
    use = p->nruns ? p->runs[0].fmt : *fmt;              /* a copy: the runs may move below */
    if (p->nruns == p->capruns) {
        want = p->capruns ? p->capruns * 2 : 4;
        runs = (owf_run *)owf_realloc(p->runs, (size_t)want * sizeof(*runs));
        if (!runs) return 0;
        p->runs = runs;
        p->capruns = want;
    }
    memset(&p->runs[p->nruns], 0, sizeof(p->runs[p->nruns]));
    p->runs[p->nruns].kind = OWF_RUN_TEXT;
    p->runs[p->nruns].fmt = use;
    p->runs[p->nruns].text = copy_bytes("", 0);
    if (!p->runs[p->nruns].text) return 0;
    ++p->nruns;
    return 1;
}

/* 2.0.15: the paragraphs a position's box names: the body's (0), or a text box's (k: doc->frames[k - 1]); the
 * body's for a box that isn't there or has no paragraphs. */
static owf_story *story_of(const ow_editor *editor, int box)
{
    owf_doc *doc = editor->doc;
    if (box > 0 && box <= doc->nframes && doc->frames[box - 1].story.nparas) return &doc->frames[box - 1].story;
    return &doc->body;
}

static int box_of(const ow_editor *editor, int box)
{
    return story_of(editor, box) == &editor->doc->body ? 0 : box;
}

/* The story the selection is in, which every edit works on. */
static owf_story *ed_story(const ow_editor *editor)
{
    return story_of(editor, editor->selection.focus.box);
}

static int ensure_document_editable(ow_editor *editor)
{
    owf_para *p;
    int r;
    if (!editor || !editor->doc) return 0;
    if (!editor->doc->body.nparas) {
        p = owf_story_add(&editor->doc->body, NULL);
        if (!p) return 0;
        ++editor->changes;
    }
    p = &editor->doc->body.paras[0];
    r = first_text_run(p);
    if (r < 0) {
        if (!append_empty_text_run(p, &editor->doc->base)) return 0;
        r = p->nruns - 1;
        ++editor->changes;
    }
    editor->selection.anchor.paragraph = 0;
    editor->selection.anchor.run = r;
    editor->selection.anchor.byte_offset = 0;
    editor->selection.anchor.box = 0;
    editor->selection.focus = editor->selection.anchor;
    return 1;
}

static ow_position clamp_position(ow_editor *editor, ow_position pos)
{
    owf_story *story;
    owf_para *p;
    int r;
    size_t n;
    ow_position zero = {0, 0, 0, 0};

    if (!editor || !editor->doc) return zero;
    pos.box = box_of(editor, pos.box);
    story = story_of(editor, pos.box);
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
            ++editor->changes;
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
    p = &ed_story(editor)->paras[pos.paragraph];
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

/* ---- 2.0.21: a paragraph's bookmarks keep their places in its text (owf.h's owf_para.bookmarks:
 * "name@start-end") as it is edited ---- */

#define MAX_EDIT_MARKS 64
typedef struct { const char *name; size_t length; size_t start, end; } ed_mark;

static int marks_read(const char *list, ed_mark *m, int max)
{
    int n = 0;
    while (list && *list && n < max) {
        size_t len = strcspn(list, " "), name = len, k;
        m[n].start = m[n].end = 0;
        for (k = len; k > 1; --k)
            if (list[k - 1] == '@') {
                const char *q = list + k, *stop = list + len;
                size_t a = 0, b;
                int ok = q < stop && *q >= '0' && *q <= '9';
                while (ok && q < stop && *q >= '0' && *q <= '9') a = a * 10 + (size_t)(*q++ - '0');
                b = a;
                if (ok && q < stop && *q == '-') {
                    ++q;
                    ok = q < stop && *q >= '0' && *q <= '9';
                    b = 0;
                    while (ok && q < stop && *q >= '0' && *q <= '9') b = b * 10 + (size_t)(*q++ - '0');
                }
                if (ok && q == stop && b >= a) {
                    name = k - 1;
                    m[n].start = a;
                    m[n].end = b;
                }
                break;
            }
        m[n].name = list;
        m[n].length = name;
        ++n;
        list += len;
        while (*list == ' ') ++list;
    }
    return n;
}

/* The list again, into p->bookmarks; as it was when there is no memory for it. */
static void marks_write(owf_para *p, const ed_mark *m, int n)
{
    size_t size = 1;
    char *t, *o;
    int i;
    for (i = 0; i < n; ++i) size += m[i].length + 48;
    if (!n) {
        owf_free(p->bookmarks);
        p->bookmarks = NULL;
        return;
    }
    if (!(t = (char *)owf_malloc(size))) return;
    for (o = t, i = 0; i < n; ++i) {
        if (i) *o++ = ' ';
        memcpy(o, m[i].name, m[i].length);
        o += m[i].length;
        if (m[i].end > m[i].start) o += sprintf(o, "@%lu-%lu", (unsigned long)m[i].start, (unsigned long)m[i].end);
        else if (m[i].start) o += sprintf(o, "@%lu", (unsigned long)m[i].start);
    }
    *o = 0;
    owf_free(p->bookmarks);
    p->bookmarks = t;
}

/* An edit at text offset at: removed bytes went, inserted ones came. A place inside what went goes to at; one
 * at at stays (text typed where a bookmark begins goes into it, where one ends doesn't). */
static size_t mark_moved(size_t x, size_t at, size_t removed, size_t inserted)
{
    if (x <= at) return x;
    if (x < at + removed) return at;
    return x - removed + inserted;
}

static void marks_edit(owf_para *p, size_t at, size_t removed, size_t inserted)
{
    ed_mark m[MAX_EDIT_MARKS];
    int i, n;
    if (!p || !p->bookmarks || (!strchr(p->bookmarks, '@') && !removed && !at)) return;
    n = marks_read(p->bookmarks, m, MAX_EDIT_MARKS);
    for (i = 0; i < n; ++i) {
        m[i].start = mark_moved(m[i].start, at, removed, inserted);
        m[i].end = mark_moved(m[i].end, at, removed, inserted);
    }
    marks_write(p, m, n);
}

/* Return at text offset at: the bookmarks that begin there or after go with the text to np, the others end
 * by at. */
static void marks_split(owf_para *p, owf_para *np, size_t at)
{
    ed_mark m[MAX_EDIT_MARKS], keep[MAX_EDIT_MARKS], move[MAX_EDIT_MARKS];
    int i, n, nk = 0, nm = 0;
    memset(keep, 0, sizeof keep);
    memset(move, 0, sizeof move);
    char *old;
    if (!p->bookmarks) return;
    n = marks_read(p->bookmarks, m, MAX_EDIT_MARKS);
    for (i = 0; i < n; ++i) {
        if (m[i].start >= at) {
            move[nm] = m[i];
            move[nm].start -= at;
            move[nm].end -= at;
            ++nm;
        } else {
            keep[nk] = m[i];
            if (keep[nk].end > at) keep[nk].end = at;
            ++nk;
        }
    }
    old = p->bookmarks;                              /* the names point into it: written last */
    p->bookmarks = NULL;
    marks_write(np, move, nm);
    marks_write(p, keep, nk);
    if (!p->bookmarks && nk) p->bookmarks = old;      /* no memory: as it was */
    else owf_free(old);
}

static ow_position para_offset_position(ow_editor *editor, int paragraph,
                                        size_t offset)
{
    ow_position pos = { paragraph, 0, 0, 0 };
    owf_para *p;
    size_t at = 0;
    int i, fallback = -1;
    pos.box = editor ? box_of(editor, editor->selection.focus.box) : 0;
    if (!editor || !editor->doc || paragraph < 0 ||
        paragraph >= ed_story(editor)->nparas) return pos;
    p = &ed_story(editor)->paras[paragraph];
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
    text = (char *)owf_malloc(from + insert_len + tail + 1);
    if (!text) return 0;
    if (from) memcpy(text, run->text, from);
    if (insert_len) memcpy(text + from, insert, insert_len);
    if (tail) memcpy(text + from + insert_len, run->text + to, tail);
    text[from + insert_len + tail] = 0;
    owf_free(run->text);
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
    p = (owf_para *)owf_realloc(story->paras, (size_t)cap * sizeof(*p));
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
    r = (owf_run *)owf_realloc(p->runs, (size_t)cap * sizeof(*r));
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

static int insert_formatted_text_at(ow_editor *editor, ow_position pos,
                                    const char *utf8, size_t length,
                                    const owf_charfmt *fmt, ow_position *after);

/* 2.0.21: with the paragraph's bookmarks moved on past the text */
static int insert_formatted_text(ow_editor *editor, ow_position pos,
                                 const char *utf8, size_t length,
                                 const owf_charfmt *fmt, ow_position *after)
{
    owf_para *p;
    size_t at;
    if (!editor || !utf8 || !fmt) return 0;
    pos = clamp_position(editor, pos);
    p = &ed_story(editor)->paras[pos.paragraph];
    at = para_text_offset(p, pos);
    if (!insert_formatted_text_at(editor, pos, utf8, length, fmt, after)) return 0;
    marks_edit(&ed_story(editor)->paras[pos.paragraph], at, 0, length);
    return 1;
}

static int insert_formatted_text_at(ow_editor *editor, ow_position pos,
                                    const char *utf8, size_t length,
                                    const owf_charfmt *fmt, ow_position *after)
{
    owf_para *p;
    owf_run *run;
    owf_charfmt old_fmt;
    size_t old_len;
    char *left = NULL, *right = NULL;
    int inserted;

    int rev, old_rev;
    char *old_href;

    if (!editor || !utf8 || !fmt) return 0;
    pos = clamp_position(editor, pos);
    p = &ed_story(editor)->paras[pos.paragraph];
    run = &p->runs[pos.run];
    old_fmt = run->fmt;
    old_len = run->text ? strlen(run->text) : 0;
    rev = editor->typing_rev;                        /* 2.1.0: the text is this change's (0: none) */
    old_rev = run->revision;
    old_href = run->href;

    if (run->comment) {
        /* 2.1.1: a comment's place keeps no text: the text goes in a run of its own after it */
        inserted = pos.run + 1;
        if (!insert_run_at(p, inserted, fmt, utf8, length)) return 0;
        p->runs[inserted].revision = rev;
        if (after) {
            after->paragraph = pos.paragraph;
            after->run = inserted;
            after->byte_offset = length;
        }
        return 1;
    }

    if (same_charfmt_local(&run->fmt, fmt) && run->revision == rev) {
        if (!replace_run_slice(run, pos.byte_offset, pos.byte_offset,
                               utf8, length)) return 0;
        if (after) { *after = pos; after->byte_offset += length; }
        return 1;
    }

    if (!old_len) {
        run->fmt = *fmt;
        run->revision = rev;
        if (!replace_run_slice(run, 0, 0, utf8, length)) return 0;
        if (after) { *after = pos; after->byte_offset = length; }
        return 1;
    }

    if (pos.byte_offset == 0) {
        if (!insert_run_at(p, pos.run, fmt, utf8, length)) return 0;
        p->runs[pos.run].revision = rev;
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
        p->runs[inserted].revision = rev;
        if (after) {
            after->paragraph = pos.paragraph;
            after->run = inserted;
            after->byte_offset = length;
        }
        return 1;
    }

    left = copy_bytes(run->text, pos.byte_offset);
    right = copy_bytes(run->text + pos.byte_offset, old_len - pos.byte_offset);
    if (!left || !right) { owf_free(left); owf_free(right); return 0; }
    owf_free(run->text);
    run->text = left;
    left = NULL;
    inserted = pos.run + 1;
    if (!insert_run_at(p, inserted, fmt, utf8, length)) {
        /* The document remains valid even if this rare OOM path leaves the
         * run split point at the caret. Undo snapshot still protects callers. */
        owf_free(right);
        return 0;
    }
    p->runs[inserted].revision = rev;
    if (!insert_run_at(p, inserted + 1, &old_fmt, right, strlen(right))) {
        owf_free(right);
        return 0;
    }
    owf_free(right);
    /* 2.1.0: the run's second half is still its change's, and its link's (it had neither) */
    p->runs[inserted + 1].revision = old_rev;
    if (old_href) p->runs[inserted + 1].href = copy_bytes(old_href, strlen(old_href));
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
    for (i = 0; i < p->nruns; ++i) {
        owf_free(p->runs[i].text);
        owf_free(p->runs[i].href);                   /* 2.0.12: a link's target went unfreed */
    }
    owf_free(p->runs);
    p->runs = NULL;
    p->nruns = p->capruns = 0;
}

/* 2.0.12: what a paragraph merged into the one before it leaves it: its bookmarks, after the other's (the
 * contents' links still find the text); its style goes. 2.0.21: its bookmarks' places moved on by the other's
 * text (offset), where its text now begins. */
static void merge_names(owf_para *into, owf_para *from, size_t offset)
{
    if (from->bookmarks && offset) {
        ed_mark m[MAX_EDIT_MARKS];
        int i, n = marks_read(from->bookmarks, m, MAX_EDIT_MARKS);
        char *old = from->bookmarks;
        for (i = 0; i < n; ++i) {
            m[i].start += offset;
            m[i].end += offset;
        }
        from->bookmarks = NULL;
        marks_write(from, m, n);
        if (!from->bookmarks) from->bookmarks = old;
        else owf_free(old);
    }
    if (from->bookmarks) {
        if (!into->bookmarks) {
            into->bookmarks = from->bookmarks;
            from->bookmarks = NULL;
        } else {
            size_t a = strlen(into->bookmarks), b = strlen(from->bookmarks);
            char *both = (char *)owf_malloc(a + b + 2);
            if (both) {
                memcpy(both, into->bookmarks, a);
                both[a] = ' ';
                memcpy(both + a + 1, from->bookmarks, b + 1);
                owf_free(into->bookmarks);
                into->bookmarks = both;
            }
        }
    }
    owf_free(from->bookmarks);
    owf_free(from->style);
    from->bookmarks = from->style = NULL;
}

static void apply_charfmt_fields(owf_charfmt *dst, const owf_charfmt *src,
                                 unsigned mask)
{
    if (mask & OW_CHARFMT_FLAGS) dst->flags = src->flags;
    if (mask & OW_CHARFMT_FONT) dst->font = src->font;
    if (mask & OW_CHARFMT_SIZE) dst->size = src->size;
    if (mask & OW_CHARFMT_COLOUR) dst->colour = src->colour;
}

/* 2.1.1: a piece of run r's text for rebuild_para_charfmt. A tracked change's piece is a run of its own with
 * the change (owf_para_add_text would join it to the run before it, and the change would be lost). */
static int add_piece(owf_para *tmp, const owf_run *r, const owf_charfmt *f, const char *s, size_t len)
{
    owf_run *n;
    if (!r->revision)
        return (r->href ? owf_para_add_link_text(tmp, f, s, len, r->href) : owf_para_add_text(tmp, f, s, len)) == OWF_OK;
    if (!insert_run_at(tmp, tmp->nruns, f, s, len)) return 0;
    n = &tmp->runs[tmp->nruns - 1];
    n->revision = r->revision;
    if (r->href && !(n->href = copy_bytes(r->href, strlen(r->href)))) return 0;
    return 1;
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
        if (r->kind == OWF_RUN_TEXT && r->comment) {
            /* 2.1.1: a comment's place, as it is */
            if (!insert_run_at(&tmp, tmp.nruns, &r->fmt, "", 0)) goto fail;
            tmp.runs[tmp.nruns - 1].comment = r->comment;
        } else if (r->kind == OWF_RUN_TEXT) {
            const char *text = r->text ? r->text : "";
            size_t n = strlen(text), rs = at, re = at + n;
            size_t a = from > rs ? from : rs;
            size_t b = to < re ? to : re;
            owf_charfmt changed = r->fmt;
            if (a < b) apply_charfmt_fields(&changed, fmt, mask);
            /* 2.0.12: a link stays a link when part of it is made bold (it lost its target) */
#define ADD_PART(f, s, len) (add_piece(&tmp, r, (f), (s), (len)) ? OWF_OK : OWF_ERR_MEMORY)
            /* 2.0.13: a run outside the selection is copied once, as it is. Its split used to run too:
             * a run before the selection was copied on past its end, and one after it from before its
             * start, and then the whole of either again. */
            if (a < b) {
                if (a > rs && ADD_PART(&r->fmt, text, a - rs) != OWF_OK)
                    goto fail;
                if (ADD_PART(&changed, text + (a - rs), b - a) != OWF_OK)
                    goto fail;
                if (b < re && ADD_PART(&r->fmt, text + (b - rs), re - b) != OWF_OK)
                    goto fail;
            } else if (n && ADD_PART(&r->fmt, text, n) != OWF_OK) {
                goto fail;
            }
#undef ADD_PART
            if (!n && !tmp.nruns && !append_empty_text_run(&tmp, &r->fmt))
                goto fail;
            at = re;
        } else {
            if (owf_para_add_special(&tmp, &r->fmt, r->kind, r->field) != OWF_OK)
                goto fail;
            tmp.runs[tmp.nruns - 1].image = r->image;   /* a picture's or a frame's index goes with it */
            tmp.runs[tmp.nruns - 1].revision = r->revision;   /* 2.1.1: and a tab's change */
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

static void stack_drop_oldest(ow_snapshot *stack, int *count)
{
    snapshot_free(&stack[0]);
    memmove(&stack[0], &stack[1], (size_t)(*count - 1) * sizeof(stack[0]));
    memset(&stack[*count - 1], 0, sizeof(stack[0]));
    --*count;
}

static int stack_push(ow_snapshot *stack, int *count, ow_snapshot snap)
{
    if (*count == OW_UNDO_LIMIT) stack_drop_oldest(stack, count);
    stack[*count] = snap;
    ++*count;
    return 1;
}

/* 2.0.11: the oldest snapshots go while undo and redo hold more than OW_UNDO_BYTES between them; the newest
 * of each stays. On an Amiga with a document of 30 pages, sixteen copies were too many. */
static void stacks_trim(ow_editor *editor)
{
    for (;;) {
        unsigned long total = 0;
        int i;
        for (i = 0; i < editor->nundo; ++i) total += editor->undo[i].bytes;
        for (i = 0; i < editor->nredo; ++i) total += editor->redo[i].bytes;
        if (total <= OW_UNDO_BYTES) return;
        if (editor->nundo > 1) stack_drop_oldest(editor->undo, &editor->nundo);
        else if (editor->nredo > 1) stack_drop_oldest(editor->redo, &editor->nredo);
        else return;
    }
}

static int capture_snapshot(const ow_editor *editor, ow_snapshot *out)
{
    memset(out, 0, sizeof(*out));
    out->doc = clone_doc(editor->doc);
    if (!out->doc) return 0;
    out->selection = editor->selection;
    out->dirty = editor->dirty;
    out->bytes = doc_bytes(out->doc);
    return 1;
}

static int remember_before_edit(ow_editor *editor)
{
    ow_snapshot snap;
    if (!capture_snapshot(editor, &snap)) return 0;
    stack_clear(editor->redo, &editor->nredo);
    stack_push(editor->undo, &editor->nundo, snap);
    stacks_trim(editor);
    return 1;
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
    stacks_trim(editor);
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
    p = &ed_story(editor)->paras[pos.paragraph];
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
        p = &ed_story(editor)->paras[pos.paragraph];
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
    p = &ed_story(editor)->paras[pos.paragraph];
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
    if (pos.paragraph + 1 < ed_story(editor)->nparas) {
        ++pos.paragraph;
        p = &ed_story(editor)->paras[pos.paragraph];
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
    p = &ed_story(editor)->paras[pos.paragraph];
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
    p = &ed_story(editor)->paras[pos.paragraph];
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
    owf_story *story = ed_story(editor);
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
            ++editor->changes;
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
    owf_story *story = ed_story(editor);
    owf_para *prev, *cur;
    int boundary, i;
    size_t offset;

    if (paragraph <= 0 || paragraph >= story->nparas) return 0;
    prev = &story->paras[paragraph - 1];
    cur = &story->paras[paragraph];
    boundary = prev->nruns;
    offset = para_text_length(prev);
    if (!ensure_run_capacity(prev, prev->nruns + cur->nruns)) return -1;
    for (i = 0; i < cur->nruns; ++i) {
        prev->runs[prev->nruns++] = cur->runs[i];
        memset(&cur->runs[i], 0, sizeof(cur->runs[i]));
    }
    owf_free(cur->runs);
    cur->runs = NULL;
    cur->nruns = cur->capruns = 0;
    merge_names(prev, cur, offset);
    prev->mark_revision = cur->mark_revision;        /* 2.1.0: prev ends where cur ended */

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
    owf_story *story = ed_story(editor);
    owf_para *cur, *next;
    int boundary, i;
    size_t offset;

    if (paragraph < 0 || paragraph + 1 >= story->nparas) return 0;
    cur = &story->paras[paragraph];
    next = &story->paras[paragraph + 1];
    boundary = cur->nruns;
    offset = para_text_length(cur);
    if (!ensure_run_capacity(cur, cur->nruns + next->nruns)) return -1;
    for (i = 0; i < next->nruns; ++i) {
        cur->runs[cur->nruns++] = next->runs[i];
        memset(&next->runs[i], 0, sizeof(next->runs[i]));
    }
    owf_free(next->runs);
    next->runs = NULL;
    next->nruns = next->capruns = 0;
    merge_names(cur, next, offset);
    cur->mark_revision = next->mark_revision;        /* 2.1.0 */

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
    size_t from, at;

    *pos = clamp_position(editor, *pos);
    p = &ed_story(editor)->paras[pos->paragraph];
    r = &p->runs[pos->run];

    if (pos->byte_offset) {
        size_t to = pos->byte_offset;
        from = utf8_prev(r->text, pos->byte_offset);
        pos->byte_offset = from;
        at = para_text_offset(p, *pos);
        if (!replace_run_slice(r, from, to, NULL, 0)) return -1;
        marks_edit(p, at, to - from, 0);             /* 2.0.21 */
        return 1;
    }

    pr = prev_text_run(p, pos->run);
    while (pr >= 0) {
        r = &p->runs[pr];
        if (strlen(r->text)) {
            size_t to = strlen(r->text);
            pos->run = pr;
            from = utf8_prev(r->text, to);
            pos->byte_offset = from;
            at = para_text_offset(p, *pos);
            if (!replace_run_slice(r, from, to, NULL, 0))
                return -1;
            marks_edit(p, at, to - from, 0);
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
    p = &ed_story(editor)->paras[pos->paragraph];
    r = &p->runs[pos->run];

    if (pos->byte_offset < strlen(r->text)) {
        size_t at = para_text_offset(p, *pos);
        to = utf8_next(r->text, pos->byte_offset);
        if (!replace_run_slice(r, pos->byte_offset, to, NULL, 0)) return -1;
        marks_edit(p, at, to - pos->byte_offset, 0); /* 2.0.21 */
        return 1;
    }

    nr = next_text_run(p, pos->run);
    while (nr >= 0) {
        r = &p->runs[nr];
        if (strlen(r->text)) {
            ow_position here = *pos;
            size_t at;
            here.run = nr;
            here.byte_offset = 0;
            at = para_text_offset(p, here);
            to = utf8_next(r->text, 0);
            if (!replace_run_slice(r, 0, to, NULL, 0)) return -1;
            marks_edit(p, at, to, 0);
            return 1;
        }
        nr = next_text_run(p, nr);
    }

    if (pos->paragraph + 1 < ed_story(editor)->nparas)
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
    owf_story *story = ed_story(editor);
    owf_para *p, *np;
    owf_run *run;
    owf_parafmt para_fmt;
    char *left = NULL, *right = NULL;
    int moved, i;
    size_t at;

    pos = clamp_position(editor, pos);
    p = &story->paras[pos.paragraph];
    run = &p->runs[pos.run];
    para_fmt = p->fmt;

    left = copy_bytes(run->text, pos.byte_offset);
    right = copy_bytes(run->text + pos.byte_offset,
                       strlen(run->text) - pos.byte_offset);
    if (!left || !right) goto fail;

    /* Insert first; p may move if the story reallocates. */
    at = para_text_offset(p, pos);
    np = insert_paragraph(story, pos.paragraph + 1, &para_fmt);
    if (!np) goto fail;
    p = &story->paras[pos.paragraph];
    if (p->style) np->style = copy_bytes(p->style, strlen(p->style));   /* 2.0.12: the style goes on */
    run = &p->runs[pos.run];

    moved = p->nruns - pos.run - 1;
    if (!ensure_run_capacity(np, 1 + moved)) {
        /* A newly inserted empty paragraph is safe to leave only if we undo
         * the structural insertion here. */
        owf_free(np->style);
        np->style = NULL;
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
    /* 2.0.12: a link split by Return: each half its own copy of the target (both freed it, twice) */
    np->runs[0].href = run->href ? copy_bytes(run->href, strlen(run->href)) : NULL;
    right = NULL;
    np->nruns = 1;

    owf_free(run->text);
    run->text = left;
    left = NULL;

    for (i = 0; i < moved; ++i) {
        np->runs[np->nruns++] = p->runs[pos.run + 1 + i];
        memset(&p->runs[pos.run + 1 + i], 0,
               sizeof(p->runs[pos.run + 1 + i]));
    }
    p->nruns = pos.run + 1;
    marks_split(p, np, at);                          /* 2.0.21: the bookmarks after the caret go with the text */
    np->mark_revision = p->mark_revision;            /* 2.1.0: the old end is the new paragraph's; the new end */
    p->mark_revision = editor->typing_rev;           /* is the change being typed, if one is */

    new_caret->paragraph = pos.paragraph + 1;
    new_caret->run = 0;
    new_caret->byte_offset = 0;
    new_caret->box = pos.box;
    return 1;

fail:
    owf_free(left);
    owf_free(right);
    return -1;
}

/* ---- 2.1.0: tracked changes ------------------------------------------------------
 * While recording, typed text is an insertion of the author's and what is deleted stays, as a deletion,
 * struck through, until it is accepted or rejected. Text and paragraph marks name their change by number
 * (owf_run.revision, owf_para.mark_revision); the changes' authors and dates are the document's, kept
 * outside the undo snapshots as its pictures are (they only grow). A change, as the editor lists it, is a
 * row of runs and paragraph marks with the same number. */

static void review_now(char *out, size_t size)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm || t == (time_t)-1) { snprintf(out, size, "2026-01-01T00:00:00"); return; }
    snprintf(out, size, "%04d-%02d-%02dT%02d:%02d:%02d", (tm->tm_year + 1900) % 10000, (tm->tm_mon + 1) % 100,
             tm->tm_mday % 100, tm->tm_hour % 100, tm->tm_min % 100, tm->tm_sec % 100);
}

static int same_text_or_null(const char *a, const char *b)
{
    return (!a && !b) || (a && b && !strcmp(a, b));
}

static int rev_kind(const owf_doc *doc, int number)
{
    const owf_revision *r = OWF_DOC_REVISION(doc, number);
    return r ? r->kind : 0;
}

/* An insertion of this author's: deleting its text takes it out, as if it had never been typed. */
static int rev_mine(const ow_editor *editor, int number)
{
    const owf_revision *r = OWF_DOC_REVISION(editor->doc, number);
    return r && r->kind == OWF_REV_INSERT && same_text_or_null(r->author, editor->author);
}

/* The number of a change of the author's made now: the last one of the kind again within its minute. 0 when
 * memory runs out. */
static int editor_revision(ow_editor *editor, int kind)
{
    owf_doc *doc = editor->doc;
    char now[32];
    int *cached = kind == OWF_REV_INSERT ? &editor->ins_rev : &editor->del_rev;
    char *minute = kind == OWF_REV_INSERT ? editor->ins_minute : editor->del_minute;
    owf_revision *r;
    review_now(now, sizeof now);
    if (*cached > 0 && *cached <= doc->nrevisions && !strncmp(minute, now, 16) &&
        doc->revisions[*cached - 1].kind == kind &&
        same_text_or_null(doc->revisions[*cached - 1].author, editor->author))
        return *cached;
    if (doc->nrevisions == doc->caprevisions) {
        int want = doc->caprevisions ? doc->caprevisions * 2 : 8;
        owf_revision *grown = (owf_revision *)owf_realloc(doc->revisions, (size_t)want * sizeof(*grown));
        if (!grown) return 0;
        doc->revisions = grown;
        doc->caprevisions = want;
    }
    r = &doc->revisions[doc->nrevisions];
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    r->date = copy_bytes(now, strlen(now));
    if (editor->author) r->author = copy_bytes(editor->author, strlen(editor->author));
    if (!r->date || (editor->author && !r->author)) {
        owf_free(r->date);
        owf_free(r->author);
        memset(r, 0, sizeof(*r));
        return 0;
    }
    *cached = ++doc->nrevisions;
    memcpy(minute, now, sizeof now);
    return *cached;
}

/* Splits text run j at byte at (inside it): the second part is run j + 1, with the same format, link and
 * change. */
static int split_text_run(owf_para *p, int j, size_t at)
{
    owf_run *r = &p->runs[j];
    size_t len = r->text ? strlen(r->text) : 0;
    char *left, *right, *href = NULL;
    if (r->kind != OWF_RUN_TEXT || at == 0 || at >= len) return 1;
    left = copy_bytes(r->text, at);
    right = copy_bytes(r->text + at, len - at);
    if (r->href) href = copy_bytes(r->href, strlen(r->href));
    if (!left || !right || (r->href && !href) || !ensure_run_capacity(p, p->nruns + 1)) {
        owf_free(left); owf_free(right); owf_free(href);
        return 0;
    }
    r = &p->runs[j];
    memmove(&p->runs[j + 2], &p->runs[j + 1], (size_t)(p->nruns - j - 1) * sizeof(p->runs[0]));
    p->runs[j + 1] = *r;
    owf_free(r->text);
    r->text = left;
    p->runs[j + 1].text = right;
    p->runs[j + 1].href = href;
    ++p->nruns;
    return 1;
}

/* Joins neighbouring text runs with the same format, link and change (a deletion marked in pieces). */
static void join_runs(owf_para *p)
{
    int j = 0;
    while (j + 1 < p->nruns) {
        owf_run *a = &p->runs[j], *b = &p->runs[j + 1];
        if (a->kind == OWF_RUN_TEXT && b->kind == OWF_RUN_TEXT && a->revision == b->revision && !a->comment && !b->comment &&
            same_charfmt_local(&a->fmt, &b->fmt) && same_text_or_null(a->href, b->href)) {
            size_t la = a->text ? strlen(a->text) : 0, lb = b->text ? strlen(b->text) : 0;
            char *t = (char *)owf_malloc(la + lb + 1);
            if (!t) return;
            if (la) memcpy(t, a->text, la);
            if (lb) memcpy(t + la, b->text, lb);
            t[la + lb] = 0;
            owf_free(a->text);
            a->text = t;
            owf_free(b->text);
            owf_free(b->href);
            memmove(&p->runs[j + 1], &p->runs[j + 2], (size_t)(p->nruns - j - 2) * sizeof(p->runs[0]));
            --p->nruns;
            memset(&p->runs[p->nruns], 0, sizeof(p->runs[0]));
            continue;
        }
        ++j;
    }
}

static void remove_run(owf_para *p, int j)
{
    owf_free(p->runs[j].text);
    owf_free(p->runs[j].href);
    memmove(&p->runs[j], &p->runs[j + 1], (size_t)(p->nruns - j - 1) * sizeof(p->runs[0]));
    --p->nruns;
    memset(&p->runs[p->nruns], 0, sizeof(p->runs[0]));
}

/* Joins paragraph i + 1 of a story to paragraph i (its end gone). */
static int join_next_in(ow_editor *editor, owf_story *story, int i)
{
    owf_para *cur, *next;
    int k;
    size_t offset;
    if (i < 0 || i + 1 >= story->nparas) return 0;
    cur = &story->paras[i];
    next = &story->paras[i + 1];
    offset = para_text_length(cur);
    if (!ensure_run_capacity(cur, cur->nruns + next->nruns)) return -1;
    for (k = 0; k < next->nruns; ++k) {
        cur->runs[cur->nruns++] = next->runs[k];
        memset(&next->runs[k], 0, sizeof(next->runs[k]));
    }
    owf_free(next->runs);
    next->runs = NULL;
    next->nruns = next->capruns = 0;
    merge_names(cur, next, offset);
    cur->mark_revision = next->mark_revision;
    owf_free(next->style);
    if (i + 2 < story->nparas)
        memmove(&story->paras[i + 1], &story->paras[i + 2], (size_t)(story->nparas - i - 2) * sizeof(story->paras[0]));
    --story->nparas;
    memset(&story->paras[story->nparas], 0, sizeof(story->paras[0]));
    if (!cur->nruns && !append_empty_text_run(cur, &editor->doc->base)) return -1;
    return 1;
}

/* Marks [start, end) of the selection's story deleted, as the author's change: text and paragraph marks
 * already deleted stay as they are, the author's own insertions go. Backwards, so that each step leaves the
 * places before it as they were, start among them. 1 when something changed, 0 nothing, -1 no memory. *start
 * is where start is afterwards. */
static int tracked_delete_range(ow_editor *editor, ow_position *start_io, ow_position end)
{
    owf_story *story = ed_story(editor);
    ow_position start = *start_io;
    int del = editor_revision(editor, OWF_REV_DELETE);
    int i, j, changed = 0, last;
    size_t off;
    if (!del) return -1;
    for (i = end.paragraph; i >= start.paragraph; --i) {
        owf_para *p = &story->paras[i];
        int fr = i == start.paragraph ? start.run : 0;
        size_t fb = i == start.paragraph ? start.byte_offset : 0;
        int lr = i == end.paragraph ? end.run : p->nruns;
        int jstart = i == end.paragraph ? end.run : p->nruns - 1;   /* before its end joins the next */
        size_t lb = i == end.paragraph ? end.byte_offset : 0;
        if (i < end.paragraph) {
            /* its end */
            int k = rev_kind(editor->doc, p->mark_revision);
            if (k == OWF_REV_DELETE) {
                /* deleted already */
            } else if (rev_mine(editor, p->mark_revision)) {
                if (join_next_in(editor, story, i) < 0) return -1;
                changed = 1;
            } else {
                p->mark_revision = del;
                changed = 1;
            }
            p = &story->paras[i];
        }
        for (j = jstart; j >= fr && j >= 0; --j) {
            owf_run *r = &p->runs[j];
            if (r->kind == OWF_RUN_TEXT) {
                size_t len = r->text ? strlen(r->text) : 0;
                size_t a = j == fr ? fb : 0, b = j == lr ? lb : len;
                if (b > len) b = len;
                if (a >= b) continue;
                if (rev_kind(editor->doc, r->revision) == OWF_REV_DELETE) continue;
                if (rev_mine(editor, r->revision)) {
                    ow_position here;
                    here.paragraph = i; here.run = j; here.byte_offset = a; here.box = start.box;
                    off = para_text_offset(p, here);
                    if (!replace_run_slice(r, a, b, NULL, 0)) return -1;
                    marks_edit(p, off, b - a, 0);
                } else {
                    int target = j;
                    if (b < len && !split_text_run(p, j, b)) return -1;
                    if (a > 0) {
                        if (!split_text_run(p, j, a)) return -1;
                        target = j + 1;
                    }
                    p->runs[target].revision = del;
                }
                changed = 1;
            } else if (j < lr && (r->kind == OWF_RUN_TAB || r->kind == OWF_RUN_LINEBREAK || r->kind == OWF_RUN_FIELD)) {
                if (rev_kind(editor->doc, r->revision) == OWF_REV_DELETE) continue;
                if (rev_mine(editor, r->revision)) remove_run(p, j);
                else r->revision = del;
                changed = 1;
            }
        }
    }
    /* the pieces marked joined again; start kept by its place in the text */
    last = start.paragraph + (end.paragraph - start.paragraph);
    if (last >= story->nparas) last = story->nparas - 1;
    off = para_text_offset(&story->paras[start.paragraph], start);
    for (i = start.paragraph; i <= last; ++i) join_runs(&story->paras[i]);
    *start_io = para_offset_position(editor, start.paragraph, off);
    start_io->box = start.box;
    return changed;
}

/* The place after any deleted text at pos (where typing goes after a deletion). */
static ow_position skip_deleted(ow_editor *editor, ow_position pos)
{
    owf_story *story = ed_story(editor);
    int guard = 0;
    pos = clamp_position(editor, pos);
    while (guard++ < 100000) {
        owf_para *p = &story->paras[pos.paragraph];
        owf_run *r = &p->runs[pos.run];
        size_t len = r->text ? strlen(r->text) : 0;
        int nt;
        if (pos.byte_offset < len) {
            if (rev_kind(editor->doc, r->revision) != OWF_REV_DELETE) break;
            pos.byte_offset = len;
            continue;
        }
        nt = next_text_run(p, pos.run);
        if (nt >= 0) {
            owf_run *n = &p->runs[nt];
            if (rev_kind(editor->doc, n->revision) == OWF_REV_DELETE || !n->text || !*n->text) {
                pos.run = nt;
                pos.byte_offset = 0;
                continue;
            }
            break;
        }
        if (rev_kind(editor->doc, p->mark_revision) == OWF_REV_DELETE && pos.paragraph + 1 < story->nparas) {
            ow_position next = pos;
            next.paragraph = pos.paragraph + 1;
            next.run = -1;
            next.byte_offset = 0;
            pos = clamp_position(editor, next);
            continue;
        }
        break;
    }
    return pos;
}

/* The selection's text deleted as a tracked change; the caret after it. */
static int tracked_delete_selection(ow_editor *editor)
{
    ow_position start, end;
    int rc;
    if (same_position(editor->selection.anchor, editor->selection.focus)) return 0;
    if (compare_position(editor->selection.anchor, editor->selection.focus) <= 0) {
        start = editor->selection.anchor;
        end = editor->selection.focus;
    } else {
        start = editor->selection.focus;
        end = editor->selection.anchor;
    }
    start = clamp_position(editor, start);
    end = clamp_position(editor, end);
    rc = tracked_delete_range(editor, &start, end);
    if (rc < 0) return -1;
    start = skip_deleted(editor, start);
    editor->selection.anchor = editor->selection.focus = start;
    return 1;
}

/* One item of a change: a run (run >= 0) or a paragraph's end (run -1). */
typedef struct { int para, run; } ow_change_item;

/* The changes of a story in order: each a row of items with one number. The callback gets each change's
 * first and last item; it returns nonzero to stop. */
typedef int (*change_fn)(void *userdata, const owf_story *story, int rev, ow_change_item first, ow_change_item last);

static int walk_changes(const owf_story *story, change_fn fn, void *userdata)
{
    int i, j, cur = 0;
    ow_change_item first = { 0, 0 }, last = { 0, 0 };
    for (i = 0; i < story->nparas; ++i) {
        const owf_para *p = &story->paras[i];
        for (j = 0; j <= p->nruns; ++j) {
            int rev;
            ow_change_item it;
            it.para = i;
            if (j < p->nruns) {
                const owf_run *r = &p->runs[j];
                it.run = j;
                rev = r->revision;
                /* an empty text run with no change between two pieces of one is no gap */
                if (!rev && cur && r->kind == OWF_RUN_TEXT && (!r->text || !*r->text)) continue;
            } else {
                if (i + 1 >= story->nparas) break;
                it.run = -1;
                rev = p->mark_revision;
            }
            if (rev && rev == cur) {
                last = it;
                continue;
            }
            if (cur && fn(userdata, story, cur, first, last)) return 1;
            cur = rev;
            first = last = it;
        }
    }
    if (cur && fn(userdata, story, cur, first, last)) return 1;
    return 0;
}

/* Where an item begins and ends as caret places. */
static ow_position item_start(ow_editor *editor, ow_change_item it)
{
    ow_position pos;
    owf_para *p = &ed_story(editor)->paras[it.para];
    pos.paragraph = it.para;
    pos.box = box_of(editor, editor->selection.focus.box);
    if (it.run < 0) {
        pos.run = last_text_run(p);
        pos.byte_offset = pos.run >= 0 && p->runs[pos.run].text ? strlen(p->runs[pos.run].text) : 0;
    } else if (p->runs[it.run].kind == OWF_RUN_TEXT) {
        pos.run = it.run;
        pos.byte_offset = 0;
    } else {
        int t = prev_text_run(p, it.run);
        if (t >= 0) { pos.run = t; pos.byte_offset = p->runs[t].text ? strlen(p->runs[t].text) : 0; }
        else { pos.run = next_text_run(p, it.run); pos.byte_offset = 0; }
    }
    return clamp_position(editor, pos);
}

static ow_position item_end(ow_editor *editor, ow_change_item it)
{
    ow_position pos;
    owf_story *story = ed_story(editor);
    owf_para *p = &story->paras[it.para];
    pos.paragraph = it.para;
    pos.box = box_of(editor, editor->selection.focus.box);
    if (it.run < 0) {
        pos.paragraph = it.para + 1 < story->nparas ? it.para + 1 : it.para;
        pos.run = first_text_run(&story->paras[pos.paragraph]);
        pos.byte_offset = 0;
    } else if (p->runs[it.run].kind == OWF_RUN_TEXT) {
        pos.run = it.run;
        pos.byte_offset = p->runs[it.run].text ? strlen(p->runs[it.run].text) : 0;
    } else {
        int t = next_text_run(p, it.run);
        if (t >= 0) { pos.run = t; pos.byte_offset = 0; }
        else { pos.run = last_text_run(p); pos.byte_offset = pos.run >= 0 && p->runs[pos.run].text ? strlen(p->runs[pos.run].text) : 0; }
    }
    return clamp_position(editor, pos);
}

typedef struct {
    ow_editor *editor;
    int want, index, found;
    int rev;
    ow_change_item first, last;
    /* for picking by place */
    ow_position from, to;
    int mode;                   /* 0: the want-th; 1: the first starting at or after from; 2: the last ending
                                   at or before from; 3: the first containing from */
} change_find;

static int find_cb(void *userdata, const owf_story *story, int rev, ow_change_item first, ow_change_item last)
{
    change_find *f = (change_find *)userdata;
    int hit = 0;
    (void)story;
    if (f->mode == 0) hit = f->index == f->want;
    else {
        ow_position s = item_start(f->editor, first), e = item_end(f->editor, last);
        if (f->mode == 1) hit = compare_position(s, f->from) >= 0;
        else if (f->mode == 2) {
            if (compare_position(e, f->from) <= 0) { f->found = 1; f->rev = rev; f->first = first; f->last = last; f->want = f->index; }
        } else hit = compare_position(s, f->from) <= 0 && compare_position(f->from, e) <= 0;
    }
    if (hit) { f->found = 1; f->rev = rev; f->first = first; f->last = last; f->want = f->index; }
    ++f->index;
    return hit;
}

static int count_cb(void *userdata, const owf_story *story, int rev, ow_change_item first, ow_change_item last)
{
    (void)story; (void)rev; (void)first; (void)last;
    ++*(int *)userdata;
    return 0;
}

void ow_editor_set_track_changes(ow_editor *editor, int on)
{
    if (!editor) return;
    on = on ? 1 : 0;
    if (editor->track != on || editor->doc->track_changes != on) editor->dirty = 1;
    editor->track = on;
    editor->doc->track_changes = on;
    editor->typing_group = 0;
    ++editor->changes;
}

int ow_editor_track_changes(const ow_editor *editor)
{
    return editor ? editor->track : 0;
}

int ow_editor_set_review_author(ow_editor *editor, const char *utf8)
{
    char *copy = NULL;
    if (!editor) return OWF_ERR_FORMAT;
    if (utf8 && *utf8 && !(copy = copy_bytes(utf8, strlen(utf8)))) return OWF_ERR_MEMORY;
    owf_free(editor->author);
    editor->author = copy;
    editor->ins_rev = editor->del_rev = 0;
    editor->typing_group = 0;
    return OWF_OK;
}

const char *ow_editor_review_author(const ow_editor *editor)
{
    return editor ? editor->author : NULL;
}

int ow_editor_change_count(const ow_editor *editor)
{
    int n = 0;
    if (!editor || !editor->doc) return 0;
    walk_changes(ed_story(editor), count_cb, &n);
    return n;
}

static void fill_info(ow_editor *editor, const change_find *f, ow_change_info *info)
{
    const owf_revision *r = OWF_DOC_REVISION(editor->doc, f->rev);
    memset(info, 0, sizeof(*info));
    info->index = f->want;
    info->kind = r ? r->kind : 0;
    info->author = r ? r->author : NULL;
    info->date = r ? r->date : NULL;
    info->start = item_start(editor, f->first);
    info->end = item_end(editor, f->last);
}

int ow_editor_change_info(ow_editor *editor, int index, ow_change_info *info)
{
    change_find f;
    if (!editor || !info || index < 0) return 0;
    memset(&f, 0, sizeof f);
    f.editor = editor;
    f.want = index;
    walk_changes(ed_story(editor), find_cb, &f);
    if (!f.found) return 0;
    fill_info(editor, &f, info);
    return 1;
}

int ow_editor_current_change(ow_editor *editor, ow_change_info *info)
{
    change_find f;
    if (!editor || !info) return 0;
    memset(&f, 0, sizeof f);
    f.editor = editor;
    f.mode = 3;
    f.from = clamp_position(editor, editor->selection.focus);
    walk_changes(ed_story(editor), find_cb, &f);
    if (!f.found) return 0;
    fill_info(editor, &f, info);
    return 1;
}

int ow_editor_next_change(ow_editor *editor, int backwards)
{
    change_find f;
    ow_position a, b;
    int wrapped = 0;
    if (!editor || !editor->doc) return 0;
    a = clamp_position(editor, editor->selection.anchor);
    b = clamp_position(editor, editor->selection.focus);
    if (compare_position(a, b) > 0) { ow_position t = a; a = b; b = t; }
    memset(&f, 0, sizeof f);
    f.editor = editor;
    f.mode = backwards ? 2 : 1;
    f.from = backwards ? a : b;
    walk_changes(ed_story(editor), find_cb, &f);
    if (!f.found) {
        /* round to the other end of the story */
        int n = ow_editor_change_count(editor);
        if (!n) return 0;
        memset(&f, 0, sizeof f);
        f.editor = editor;
        f.want = backwards ? n - 1 : 0;
        walk_changes(ed_story(editor), find_cb, &f);
        if (!f.found) return 0;
        wrapped = 1;
    }
    editor->selection.anchor = item_start(editor, f.first);
    editor->selection.focus = item_end(editor, f.last);
    editor->typing_group = 0;
    sync_typing_fmt(editor);
    return wrapped ? 2 : 1;
}

/* The items of the changes picked: all of a story's, or those the selection touches. */
typedef struct {
    ow_editor *editor;
    int all;
    ow_position from, to;       /* the selection, from <= to */
    ow_change_item *items;
    int nitems, capitems, failed;
} change_pick;

static int pick_cb(void *userdata, const owf_story *story, int rev, ow_change_item first, ow_change_item last)
{
    change_pick *pk = (change_pick *)userdata;
    ow_change_item it;
    (void)rev;
    if (!pk->all) {
        ow_position s = item_start(pk->editor, first), e = item_end(pk->editor, last);
        int touch = same_position(pk->from, pk->to)
                  ? compare_position(s, pk->from) <= 0 && compare_position(pk->from, e) <= 0
                  : compare_position(s, pk->to) < 0 && compare_position(pk->from, e) < 0;
        if (!touch) return 0;
    }
    it = first;
    for (;;) {
        if (pk->nitems == pk->capitems) {
            int want = pk->capitems ? pk->capitems * 2 : 32;
            ow_change_item *grown = (ow_change_item *)owf_realloc(pk->items, (size_t)want * sizeof(*grown));
            if (!grown) { pk->failed = 1; return 1; }
            pk->items = grown;
            pk->capitems = want;
        }
        pk->items[pk->nitems++] = it;
        if (it.para == last.para && it.run == last.run) break;
        /* the next item */
        if (it.run >= 0 && it.run + 1 < story->paras[it.para].nruns) ++it.run;
        else if (it.run >= 0) it.run = -1;
        else { ++it.para; it.run = 0; if (!story->paras[it.para].nruns) it.run = -1; }
    }
    return 0;
}

/* Accepts or rejects the items, last first, so each leaves the ones before it where they were. 1 when
 * something changed, -1 no memory. */
static int resolve_items(ow_editor *editor, owf_story *story, const ow_change_item *items, int n, int accept)
{
    int k, changed = 0, low = story->nparas, high = -1;
    for (k = n - 1; k >= 0; --k) {
        owf_para *p = &story->paras[items[k].para];
        int number = items[k].run < 0 ? p->mark_revision : p->runs[items[k].run].revision;
        int kind = rev_kind(editor->doc, number);
        int keep = (kind == OWF_REV_INSERT) == (accept != 0);
        if (!number) continue;
        if (items[k].para < low) low = items[k].para;
        if (items[k].para > high) high = items[k].para;
        if (keep || !kind) {
            if (items[k].run < 0) p->mark_revision = 0;
            else p->runs[items[k].run].revision = 0;
        } else if (items[k].run < 0) {
            if (join_next_in(editor, story, items[k].para) < 0) return -1;
        } else {
            owf_run *r = &p->runs[items[k].run];
            if (r->kind == OWF_RUN_TEXT && r->text && *r->text) {
                ow_position here;
                here.paragraph = items[k].para; here.run = items[k].run; here.byte_offset = 0; here.box = 0;
                marks_edit(p, para_text_offset(p, here), strlen(r->text), 0);
            }
            remove_run(p, items[k].run);
        }
        changed = 1;
    }
    if (high >= story->nparas) high = story->nparas - 1;
    for (k = low; k <= high; ++k) {
        join_runs(&story->paras[k]);
        if (first_text_run(&story->paras[k]) < 0 && !append_empty_text_run(&story->paras[k], &editor->doc->base))
            return -1;
    }
    return changed;
}

static int resolve_story(ow_editor *editor, owf_story *story, int all, int accept, int *first_para)
{
    change_pick pk;
    int rc;
    memset(&pk, 0, sizeof pk);
    pk.editor = editor;
    pk.all = all;
    if (!all) {
        pk.from = clamp_position(editor, editor->selection.anchor);
        pk.to = clamp_position(editor, editor->selection.focus);
        if (compare_position(pk.from, pk.to) > 0) { ow_position t = pk.from; pk.from = pk.to; pk.to = t; }
    }
    walk_changes(story, pick_cb, &pk);
    if (pk.failed) { owf_free(pk.items); return -1; }
    if (first_para && pk.nitems) *first_para = pk.items[0].para;
    rc = pk.nitems ? resolve_items(editor, story, pk.items, pk.nitems, accept) : 0;
    owf_free(pk.items);
    return rc;
}

int ow_editor_accept_change(ow_editor *editor, int accept)
{
    int rc, first = -1;
    ow_position caret;
    if (!editor) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    caret = clamp_position(editor, editor->selection.anchor);
    if (compare_position(clamp_position(editor, editor->selection.focus), caret) < 0)
        caret = clamp_position(editor, editor->selection.focus);
    rc = resolve_story(editor, ed_story(editor), 0, accept, &first);
    if (rc < 0) return OWF_ERR_MEMORY;
    if (!rc) {
        snapshot_free(&editor->undo[editor->nundo - 1]);
        --editor->nundo;
        return OWF_ERR_UNSUPPORTED;
    }
    if (first >= 0 && first < caret.paragraph) { caret.paragraph = first; caret.run = -1; caret.byte_offset = 0; }
    if (caret.paragraph >= ed_story(editor)->nparas) caret.paragraph = ed_story(editor)->nparas - 1;
    caret = clamp_position(editor, caret);
    editor->selection.anchor = editor->selection.focus = caret;
    editor->dirty = 1; ++editor->changes;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

int ow_editor_accept_all_changes(ow_editor *editor, int accept)
{
    owf_doc *doc;
    int rc = 0, k, r;
    owf_story *stories[5];
    if (!editor) return OWF_ERR_FORMAT;
    doc = editor->doc;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    stories[0] = &doc->body; stories[1] = &doc->header; stories[2] = &doc->footer;
    stories[3] = &doc->header_left; stories[4] = &doc->footer_left;
    for (k = 0; k < 5; ++k) {
        if (!stories[k]->nparas) continue;
        r = resolve_story(editor, stories[k], 1, accept, NULL);
        if (r < 0) return OWF_ERR_MEMORY;
        rc |= r;
    }
    for (k = 0; k < doc->nframes; ++k) {
        if (!doc->frames[k].story.nparas) continue;
        r = resolve_story(editor, &doc->frames[k].story, 1, accept, NULL);
        if (r < 0) return OWF_ERR_MEMORY;
        rc |= r;
    }
    if (!rc) {
        snapshot_free(&editor->undo[editor->nundo - 1]);
        --editor->nundo;
        return OWF_ERR_UNSUPPORTED;
    }
    editor->selection.anchor = clamp_position(editor, editor->selection.anchor);
    editor->selection.focus = clamp_position(editor, editor->selection.focus);
    editor->dirty = 1; ++editor->changes;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

/* ---- 2.2.0: comments ----------------------------------------------------------------
 * A comment is the document's (owf_annotation in doc->comments) and is in the text where a run names it
 * (owf_run.comment: an empty text run, 1 + its index where it is anchored, -(1 + its index) where its range
 * starts). The list only grows and is kept outside the undo snapshots, as the changes are: removing a comment
 * removes its runs, and changing one (its text, done) is a new one that its runs name instead, so that undo
 * puts the runs back as they were and the comments with them. The editor lists a story's comments in the
 * order of their places. */

typedef struct {
    int record;                 /* its index in doc->comments */
    int para, run;              /* its anchor run */
    int spara, srun;            /* its range's start run (spara -1: none, a point) */
} ed_comment;

/* The comments of a story in the order of their anchors; NULL and 0 when none (or no memory: *failed) */
static ed_comment *story_comments(const owf_doc *doc, const owf_story *story, int *count, int *failed)
{
    ed_comment *list = NULL;
    int n = 0, cap = 0, i, j, k;
    *count = 0;
    if (failed) *failed = 0;
    for (i = 0; i < story->nparas; ++i)
        for (j = 0; j < story->paras[i].nruns; ++j) {
            int m = story->paras[i].runs[j].comment;
            if (m <= 0 || m > doc->ncomments) continue;
            if (n == cap) {
                int want = cap ? cap * 2 : 8;
                ed_comment *grown = (ed_comment *)owf_realloc(list, (size_t)want * sizeof(*grown));
                if (!grown) { owf_free(list); if (failed) *failed = 1; return NULL; }
                list = grown;
                cap = want;
            }
            list[n].record = m - 1;
            list[n].para = i;
            list[n].run = j;
            list[n].spara = list[n].srun = -1;
            ++n;
        }
    for (i = 0; i < story->nparas; ++i)
        for (j = 0; j < story->paras[i].nruns; ++j) {
            int m = story->paras[i].runs[j].comment;
            if (m >= 0) continue;
            for (k = 0; k < n; ++k)
                if (list[k].record == -m - 1 && list[k].spara < 0) { list[k].spara = i; list[k].srun = j; break; }
        }
    *count = n;
    return list;
}

static ow_position run_position(ow_editor *editor, int para, int run)
{
    ow_position pos;
    pos.paragraph = para;
    pos.run = run;
    pos.byte_offset = 0;
    pos.box = box_of(editor, editor->selection.focus.box);
    return pos;
}

/* A place as (paragraph, bytes of text before it): the same place whichever empty run it is in */
static int place_compare(const owf_story *story, ow_position a, ow_position b)
{
    size_t x, y;
    if (a.paragraph != b.paragraph) return a.paragraph < b.paragraph ? -1 : 1;
    x = para_text_offset(&story->paras[a.paragraph], a);
    y = para_text_offset(&story->paras[b.paragraph], b);
    return x < y ? -1 : x > y ? 1 : 0;
}

static ow_position comment_start(ow_editor *editor, const ed_comment *c)
{
    return c->spara >= 0 ? run_position(editor, c->spara, c->srun) : run_position(editor, c->para, c->run);
}

static void comment_fill(ow_editor *editor, const ed_comment *list, int n, int k, ow_comment_info *info)
{
    const owf_annotation *a = &editor->doc->comments[list[k].record];
    int i;
    memset(info, 0, sizeof(*info));
    info->index = k;
    info->author = a->author;
    info->initials = a->initials;
    info->date = a->date;
    info->text = a->text ? a->text : "";
    info->done = a->done;
    info->reply_to = -1;
    for (i = 0; a->parent >= 0 && i < n; ++i)
        if (list[i].record == a->parent) { info->reply_to = i; break; }
    info->start = comment_start(editor, &list[k]);
    info->end = run_position(editor, list[k].para, list[k].run);
}

/* The comment at the caret: one whose range holds it, or whose place it is at; the narrowest, a comment
 * before its replies. -1: none. */
static int comment_at(ow_editor *editor, const ed_comment *list, int n)
{
    owf_story *story = ed_story(editor);
    ow_position caret = clamp_position(editor, editor->selection.focus);
    int k, best = -1;
    size_t best_len = (size_t)-1;
    for (k = 0; k < n; ++k) {
        ow_position s = comment_start(editor, &list[k]), e = run_position(editor, list[k].para, list[k].run);
        size_t len;
        if (place_compare(story, s, caret) > 0 || place_compare(story, caret, e) > 0) continue;
        len = s.paragraph == e.paragraph
            ? para_text_offset(&story->paras[e.paragraph], e) - para_text_offset(&story->paras[s.paragraph], s)
            : (size_t)(e.paragraph - s.paragraph) * 100000u;
        if (best < 0 || len < best_len ||
            (len == best_len && editor->doc->comments[list[best].record].parent >= 0 &&
             editor->doc->comments[list[k].record].parent < 0)) {
            best = k;
            best_len = len;
        }
    }
    /* a reply stands where its comment is: the comment, when the caret is in it too */
    while (best >= 0 && editor->doc->comments[list[best].record].parent >= 0) {
        int parent = editor->doc->comments[list[best].record].parent, up = -1;
        for (k = 0; k < n; ++k)
            if (list[k].record == parent) { up = k; break; }
        if (up < 0 || up == best) break;
        if (place_compare(story, comment_start(editor, &list[up]), caret) > 0 ||
            place_compare(story, caret, run_position(editor, list[up].para, list[up].run)) > 0)
            break;
        best = up;
    }
    return best;
}

int ow_editor_comment_count(const ow_editor *editor)
{
    int n = 0, failed;
    ed_comment *list;
    if (!editor || !editor->doc) return 0;
    list = story_comments(editor->doc, ed_story(editor), &n, &failed);
    owf_free(list);
    return n;
}

int ow_editor_comment_info(ow_editor *editor, int index, ow_comment_info *info)
{
    int n = 0, failed;
    ed_comment *list;
    if (!editor || !info || index < 0) return 0;
    list = story_comments(editor->doc, ed_story(editor), &n, &failed);
    if (index >= n) { owf_free(list); return 0; }
    comment_fill(editor, list, n, index, info);
    owf_free(list);
    return 1;
}

int ow_editor_current_comment(ow_editor *editor, ow_comment_info *info)
{
    int n = 0, failed, k;
    ed_comment *list;
    if (!editor || !info) return 0;
    list = story_comments(editor->doc, ed_story(editor), &n, &failed);
    k = comment_at(editor, list, n);
    if (k >= 0) comment_fill(editor, list, n, k, info);
    owf_free(list);
    return k >= 0;
}

/* Selects comment k's range (a point: the caret there) */
static void select_comment(ow_editor *editor, const ed_comment *c)
{
    editor->selection.anchor = comment_start(editor, c);
    editor->selection.focus = run_position(editor, c->para, c->run);
    editor->typing_group = 0;
    sync_typing_fmt(editor);
}

int ow_editor_next_comment(ow_editor *editor, int backwards)
{
    owf_story *story;
    ed_comment *list;
    ow_position a, b;
    int n = 0, failed, k, pick = -1, wrapped = 0;
    if (!editor || !editor->doc) return 0;
    story = ed_story(editor);
    list = story_comments(editor->doc, story, &n, &failed);
    a = clamp_position(editor, editor->selection.anchor);
    b = clamp_position(editor, editor->selection.focus);
    if (place_compare(story, a, b) > 0) { ow_position t = a; a = b; b = t; }
    /* comments, not their replies (a reply is where its comment is) */
    if (!backwards) {
        for (k = 0; k < n && pick < 0; ++k)
            if (editor->doc->comments[list[k].record].parent < 0 &&
                place_compare(story, comment_start(editor, &list[k]), a) > 0)
                pick = k;
    } else {
        for (k = n - 1; k >= 0 && pick < 0; --k)
            if (editor->doc->comments[list[k].record].parent < 0 &&
                place_compare(story, comment_start(editor, &list[k]), a) < 0)
                pick = k;
    }
    if (pick < 0) {
        /* round to the other end of the story */
        for (k = backwards ? n - 1 : 0; k >= 0 && k < n; k += backwards ? -1 : 1)
            if (editor->doc->comments[list[k].record].parent < 0) { pick = k; break; }
        wrapped = 1;
    }
    if (pick >= 0) select_comment(editor, &list[pick]);
    owf_free(list);
    return pick < 0 ? 0 : wrapped ? 2 : 1;
}

/* A new comment of the review author's, made now: its index; -1 when memory runs out */
static int editor_comment_add(ow_editor *editor, const char *text, int parent, int done)
{
    owf_doc *doc = editor->doc;
    owf_annotation *c;
    char now[32];
    if (doc->ncomments == doc->capcomments) {
        int want = doc->capcomments ? doc->capcomments * 2 : 8;
        owf_annotation *grown = (owf_annotation *)owf_realloc(doc->comments, (size_t)want * sizeof(*grown));
        if (!grown) return -1;
        doc->comments = grown;
        doc->capcomments = want;
    }
    review_now(now, sizeof now);
    c = &doc->comments[doc->ncomments];
    memset(c, 0, sizeof(*c));
    c->parent = parent;
    c->done = done;
    c->date = copy_bytes(now, strlen(now));
    c->text = copy_bytes(text ? text : "", text ? strlen(text) : 0);
    if (editor->author) c->author = copy_bytes(editor->author, strlen(editor->author));
    if (!c->date || !c->text || (editor->author && !c->author)) {
        owf_free(c->date); owf_free(c->text); owf_free(c->author);
        memset(c, 0, sizeof(*c));
        return -1;
    }
    return doc->ncomments++;
}

/* A copy of comment record (its author, initials and date kept) with new text (NULL: the same) and done */
static int editor_comment_copy(ow_editor *editor, int record, const char *text, int done, int parent)
{
    owf_doc *doc = editor->doc;
    owf_annotation from, *c;
    if (doc->ncomments == doc->capcomments) {
        int want = doc->capcomments ? doc->capcomments * 2 : 8;
        owf_annotation *grown = (owf_annotation *)owf_realloc(doc->comments, (size_t)want * sizeof(*grown));
        if (!grown) return -1;
        doc->comments = grown;
        doc->capcomments = want;
    }
    from = doc->comments[record];
    if (!text) text = from.text ? from.text : "";
    c = &doc->comments[doc->ncomments];
    memset(c, 0, sizeof(*c));
    c->parent = parent;
    c->done = done;
    c->text = copy_bytes(text, strlen(text));
    if (from.author) c->author = copy_bytes(from.author, strlen(from.author));
    if (from.initials) c->initials = copy_bytes(from.initials, strlen(from.initials));
    if (from.date) c->date = copy_bytes(from.date, strlen(from.date));
    if (!c->text || (from.author && !c->author) || (from.initials && !c->initials) || (from.date && !c->date)) {
        owf_free(c->text); owf_free(c->author); owf_free(c->initials); owf_free(c->date);
        memset(c, 0, sizeof(*c));
        return -1;
    }
    return doc->ncomments++;
}

/* A comment's run (mark) at pos: the text run there split if pos is inside it. The run's index, -1 when
 * memory runs out. */
static int insert_comment_run(ow_editor *editor, ow_position pos, int mark)
{
    owf_para *p = &ed_story(editor)->paras[pos.paragraph];
    owf_charfmt fmt;
    int at;
    size_t len;
    if (pos.run < 0 || pos.run >= p->nruns) {
        at = p->nruns;
    } else {
        len = p->runs[pos.run].kind == OWF_RUN_TEXT && p->runs[pos.run].text ? strlen(p->runs[pos.run].text) : 0;
        if (pos.byte_offset == 0) at = pos.run;
        else if (pos.byte_offset >= len) at = pos.run + 1;
        else {
            if (!split_text_run(p, pos.run, pos.byte_offset)) return -1;
            at = pos.run + 1;
        }
    }
    fmt = p->nruns ? p->runs[at < p->nruns ? at : p->nruns - 1].fmt : editor->doc->base;
    if (!insert_run_at(p, at, &fmt, "", 0)) return -1;
    p->runs[at].comment = mark;
    return at;
}

static void edit_done(ow_editor *editor)
{
    editor->dirty = 1;
    ++editor->changes;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
}

/* An edit that came to nothing: its undo step goes */
static void edit_undone(ow_editor *editor)
{
    snapshot_free(&editor->undo[editor->nundo - 1]);
    --editor->nundo;
}

int ow_editor_insert_comment(ow_editor *editor, const char *utf8)
{
    ow_position a, b;
    int record, at, ranged;
    if (!editor || !editor->doc || !utf8) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    record = editor_comment_add(editor, utf8, -1, 0);
    if (record < 0) { edit_undone(editor); return OWF_ERR_MEMORY; }
    a = clamp_position(editor, editor->selection.anchor);
    b = clamp_position(editor, editor->selection.focus);
    if (compare_position(a, b) > 0) { ow_position t = a; a = b; b = t; }
    ranged = place_compare(ed_story(editor), a, b) < 0;
    /* the end first, so the start's place stays as it is */
    at = insert_comment_run(editor, b, record + 1);
    if (at < 0) return OWF_ERR_MEMORY;
    if (ranged && insert_comment_run(editor, a, -(record + 1)) < 0)
        return OWF_ERR_MEMORY;
    /* the caret after it */
    {
        owf_para *p = &ed_story(editor)->paras[b.paragraph];
        int j;
        for (j = 0; j < p->nruns; ++j)
            if (p->runs[j].comment == record + 1) { b.run = j; b.byte_offset = 0; break; }
    }
    editor->selection.anchor = editor->selection.focus = clamp_position(editor, b);
    edit_done(editor);
    return OWF_OK;
}

int ow_editor_reply_comment(ow_editor *editor, const char *utf8)
{
    ed_comment *list;
    int n = 0, failed, k, root, record;
    if (!editor || !editor->doc || !utf8) return OWF_ERR_FORMAT;
    list = story_comments(editor->doc, ed_story(editor), &n, &failed);
    k = comment_at(editor, list, n);
    if (k < 0) { owf_free(list); return failed ? OWF_ERR_MEMORY : OWF_ERR_UNSUPPORTED; }
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) { owf_free(list); return OWF_ERR_MEMORY; }
    root = list[k].record;
    while (editor->doc->comments[root].parent >= 0) root = editor->doc->comments[root].parent;   /* a reply's comment */
    record = editor_comment_add(editor, utf8, root, 0);
    if (record < 0) { owf_free(list); edit_undone(editor); return OWF_ERR_MEMORY; }
    /* its place: after the comment's and its replies' */
    {
        int last = k, i;
        ow_position at;
        for (i = 0; i < n; ++i)
            if ((list[i].record == root || editor->doc->comments[list[i].record].parent == root) &&
                (list[i].para > list[last].para || (list[i].para == list[last].para && list[i].run > list[last].run)))
                last = i;
        at = run_position(editor, list[last].para, list[last].run + 1);
        if (insert_comment_run(editor, at, record + 1) < 0) { owf_free(list); return OWF_ERR_MEMORY; }
    }
    owf_free(list);
    edit_done(editor);
    return OWF_OK;
}

/* Every run naming record (either sign) names to instead (0: removed) */
static void repoint_runs(ow_editor *editor, owf_story *story, int record, int to)
{
    int i, j;
    for (i = 0; i < story->nparas; ++i) {
        owf_para *p = &story->paras[i];
        for (j = p->nruns - 1; j >= 0; --j) {
            int m = p->runs[j].comment;
            if (m != record + 1 && m != -(record + 1)) continue;
            if (to < 0) {
                remove_run(p, j);
                if (!p->nruns) append_empty_text_run(p, &editor->doc->base);
            } else {
                p->runs[j].comment = m > 0 ? to + 1 : -(to + 1);
            }
        }
    }
}

/* The comment at the caret and its replies changed (text != NULL: the comment's text) or removed */
static int change_comment(ow_editor *editor, int remove, const char *text, int done)
{
    owf_doc *doc = editor->doc;
    owf_story *story = ed_story(editor);
    ed_comment *list;
    ow_position caret;
    int n = 0, failed, k, root, i, newroot = -1, rc = OWF_OK;
    list = story_comments(doc, story, &n, &failed);
    k = comment_at(editor, list, n);
    if (k < 0) { owf_free(list); return failed ? OWF_ERR_MEMORY : OWF_ERR_UNSUPPORTED; }
    root = list[k].record;
    if (text && doc->comments[root].parent >= 0) {
        /* editing a reply's text: that reply alone */
        int r;
        editor->typing_group = 0;
        if (!remember_before_edit(editor)) { owf_free(list); return OWF_ERR_MEMORY; }
        r = editor_comment_copy(editor, root, text, doc->comments[root].done, doc->comments[root].parent);
        owf_free(list);
        if (r < 0) { edit_undone(editor); return OWF_ERR_MEMORY; }
        repoint_runs(editor, story, root, r);
        edit_done(editor);
        return OWF_OK;
    }
    while (doc->comments[root].parent >= 0) root = doc->comments[root].parent;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) { owf_free(list); return OWF_ERR_MEMORY; }
    caret = clamp_position(editor, comment_start(editor, &list[k]));
    if (!remove) {
        newroot = editor_comment_copy(editor, root, text, done < 0 ? doc->comments[root].done : done, -1);
        if (newroot < 0) { owf_free(list); edit_undone(editor); return OWF_ERR_MEMORY; }
    }
    /* its replies first: each a copy answering the new comment, or gone */
    for (i = 0; i < n; ++i) {
        int r = list[i].record;
        if (doc->comments[r].parent != root) continue;
        if (remove) {
            repoint_runs(editor, story, r, -1);
        } else {
            int c = editor_comment_copy(editor, r, NULL, done < 0 ? doc->comments[r].done : done, newroot);
            if (c < 0) { rc = OWF_ERR_MEMORY; break; }
            repoint_runs(editor, story, r, c);
        }
    }
    repoint_runs(editor, story, root, remove ? -1 : newroot);
    owf_free(list);
    if (remove) {
        if (caret.paragraph >= story->nparas) caret.paragraph = story->nparas - 1;
        caret.run = -1;
        caret.byte_offset = 0;
        editor->selection.anchor = editor->selection.focus = clamp_position(editor, caret);
    } else {
        editor->selection.anchor = clamp_position(editor, editor->selection.anchor);
        editor->selection.focus = clamp_position(editor, editor->selection.focus);
    }
    edit_done(editor);
    return rc;
}

int ow_editor_set_comment_text(ow_editor *editor, const char *utf8)
{
    if (!editor || !editor->doc || !utf8) return OWF_ERR_FORMAT;
    return change_comment(editor, 0, utf8, -1);
}

int ow_editor_resolve_comment(ow_editor *editor, int done)
{
    if (!editor || !editor->doc) return OWF_ERR_FORMAT;
    return change_comment(editor, 0, NULL, done ? 1 : 0);
}

int ow_editor_delete_comment(ow_editor *editor)
{
    if (!editor || !editor->doc) return OWF_ERR_FORMAT;
    return change_comment(editor, 1, NULL, -1);
}

/* ---- public editor API --------------------------------------------------------- */

ow_editor *ow_editor_new(owf_doc *doc)
{
    ow_editor *editor;
    if (!doc) return NULL;
    editor = (ow_editor *)owf_calloc(1, sizeof(*editor));
    if (!editor) return NULL;
    editor->doc = doc;
    editor->view = OW_VIEW_PAGE;
    editor->zoom = 100;
    editor->pages = 1;
    editor->track = doc->track_changes ? 1 : 0;      /* 2.1.0: recording as the document was saved */
    if (!ensure_document_editable(editor)) {
        owf_free(editor);
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
    owf_free(editor->para_page);
    owf_free(editor->para_y);
    owf_free(editor->author);
    owf_free(editor);
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
    editor->selection.focus = clamp_position(editor, editor->selection.focus);
    /* 2.0.15: both ends in one story: a selection that would leave a text box collapses to its focus */
    if (box_of(editor, editor->selection.anchor.box) != editor->selection.focus.box)
        editor->selection.anchor = editor->selection.focus;
    editor->selection.anchor = clamp_position(editor, editor->selection.anchor);
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
    story = ed_story(editor);
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
        b = (char *)owf_realloc(*buf, want);
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
        const owf_para *p = &ed_story(editor)->paras[pi];
        for (ri = 0; ri < p->nruns; ++ri) {
            const owf_run *r = &p->runs[ri];
            ow_position rs = { pi, ri, 0, 0 }, re = rs;
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
    owf_free(out);
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
    if (pos.paragraph < 0 || pos.paragraph >= ed_story(editor)->nparas) return 0;
    p = &ed_story(editor)->paras[pos.paragraph];
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
    aoff = para_text_offset(&ed_story(editor)->paras[old_a.paragraph], old_a);
    foff = para_text_offset(&ed_story(editor)->paras[old_f.paragraph], old_f);
    if (compare_position(old_a, old_f) <= 0) { start = old_a; end = old_f; }
    else { start = old_f; end = old_a; }
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;

    for (pi = start.paragraph; pi <= end.paragraph; ++pi) {
        owf_para *p = &ed_story(editor)->paras[pi];
        size_t from = pi == start.paragraph ? para_text_offset(p, start) : 0;
        size_t to = pi == end.paragraph ? para_text_offset(p, end) : para_text_length(p);
        if (to > from && !rebuild_para_charfmt(p, from, to, fmt, mask))
            return OWF_ERR_MEMORY;
    }
    editor->selection.anchor = para_offset_position(editor, old_a.paragraph, aoff);
    editor->selection.focus = para_offset_position(editor, old_f.paragraph, foff);
    editor->dirty = 1; ++editor->changes;
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
    if (p < 0 || p >= ed_story(editor)->nparas) return 0;
    *fmt = ed_story(editor)->paras[p].fmt;
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
    if (last >= ed_story(editor)->nparas) last = ed_story(editor)->nparas - 1;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    for (p = first; p <= last; ++p) {
        owf_parafmt *dst = &ed_story(editor)->paras[p].fmt;
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
    editor->dirty = 1; ++editor->changes;
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
    if (last >= ed_story(editor)->nparas) last = ed_story(editor)->nparas - 1;
    for (p = first; p <= last; ++p) {
        int kind = -1;
        paragraph_list_prefix(&ed_story(editor)->paras[p], &kind, NULL);
        if (kind != ordered) { all_same = 0; break; }
    }
    old_a = editor->selection.anchor;
    old_f = editor->selection.focus;
    aoff = para_text_offset(&ed_story(editor)->paras[old_a.paragraph], old_a);
    foff = para_text_offset(&ed_story(editor)->paras[old_f.paragraph], old_f);
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;

    for (p = first; p <= last; ++p) {
        owf_para *para = &ed_story(editor)->paras[p];
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
            {   /* 2.0.21: the bookmarks after it move back */
                ow_position at;
                memset(&at, 0, sizeof at);
                at.paragraph = p;
                at.run = r;
                marks_edit(para, para_text_offset(para, at), old_len, 0);
            }
            changed = 1;
            if (old_a.paragraph == p) aoff = aoff > old_len ? aoff - old_len : 0;
            if (old_f.paragraph == p) foff = foff > old_len ? foff - old_len : 0;
        }
        if (!all_same) {
            if (ordered == 0) snprintf(prefix, sizeof prefix, "%s", "\xe2\x80\xa2 ");
            else snprintf(prefix, sizeof prefix, "%d. ", p - first + 1);
            add_len = strlen(prefix);
            if (!replace_run_slice(&para->runs[r], 0, 0, prefix, add_len)) return OWF_ERR_MEMORY;
            {
                ow_position at;
                memset(&at, 0, sizeof at);
                at.paragraph = p;
                at.run = r;
                marks_edit(para, para_text_offset(para, at), 0, add_len);
            }
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
    editor->dirty = 1; ++editor->changes;
    sync_typing_fmt(editor);
    ow_editor_layout(editor);
    return OWF_OK;
}

static char *paragraph_plain_text(const owf_para *p, size_t *out_len)
{
    size_t n = para_text_length(p), used = 0;
    char *s = (char *)owf_malloc(n + 1);
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
    total = ed_story(editor)->nparas;
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
        p = &ed_story(editor)->paras[pi];
        plain = paragraph_plain_text(p, &plen);
        if (!plain) return -1;
        if (pass == 0) from = para_text_offset(p, startpos);
        else from = backwards ? plen : 0;
        hit = backwards
            ? find_backward_in(plain, plen, needle, nlen, from, match_case)
            : find_forward_in(plain, plen, needle, nlen, from, match_case);
        owf_free(plain);
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
        if ((editor->track ? tracked_delete_selection(editor) : delete_selection_internal(editor)) < 0)
            return OWF_ERR_MEMORY;
        pos = editor->selection.focus;
    }

    pos = clamp_position(editor, pos);
    run = &ed_story(editor)->paras[pos.paragraph].runs[pos.run];
    if (!editor->typing_fmt_set) editor->typing_fmt = run->fmt;
    editor->typing_fmt_set = 1;
    /* 2.1.0: recording, the text is the author's insertion */
    editor->typing_rev = editor->track ? editor_revision(editor, OWF_REV_INSERT) : 0;
    if (editor->track && !editor->typing_rev) return OWF_ERR_MEMORY;
    if (!insert_formatted_text(editor, pos, utf8, length,
                               &editor->typing_fmt, &pos)) {
        editor->typing_rev = 0;
        return OWF_ERR_MEMORY;
    }
    editor->typing_rev = 0;
    editor->selection.anchor = pos;
    editor->selection.focus = pos;
    editor->typing_group = 1;
    editor->typing_next = pos;
    editor->dirty = 1; ++editor->changes;
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
        if ((editor->track ? tracked_delete_selection(editor) : delete_selection_internal(editor)) < 0)
            return OWF_ERR_MEMORY;
        changed = 1;
    }
    pos = clamp_position(editor, editor->selection.focus);
    if (!editor->typing_fmt_set) sync_typing_fmt(editor);
    editor->typing_rev = editor->track ? editor_revision(editor, OWF_REV_INSERT) : 0;   /* 2.1.0 */
    if (editor->track && !editor->typing_rev) return OWF_ERR_MEMORY;

    while (i <= length) {
        int newline = 0;
        if (i == length || utf8[i] == '\r' || utf8[i] == '\n') {
            if (i > start) {
                if (!insert_formatted_text(editor, pos, utf8 + start, i - start,
                                           &editor->typing_fmt, &pos)) {
                    editor->typing_rev = 0;
                    return OWF_ERR_MEMORY;
                }
                changed = 1;
            }
            if (i < length) {
                newline = 1;
                if (utf8[i] == '\r' && i + 1 < length && utf8[i + 1] == '\n') ++i;
                if (split_paragraph(editor, pos, &next) < 0) { editor->typing_rev = 0; return OWF_ERR_MEMORY; }
                pos = next;
                changed = 1;
            }
            start = i + 1;
        }
        ++i;
        (void)newline;
    }
    editor->typing_rev = 0;
    if (!changed) {
        snapshot_free(&editor->undo[editor->nundo - 1]);
        --editor->nundo;
        return OWF_OK;
    }
    editor->selection.anchor = pos;
    editor->selection.focus = pos;
    editor->dirty = 1; ++editor->changes;
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
        rc = editor->track ? tracked_delete_selection(editor) : delete_selection_internal(editor);
    } else if (editor->track) {
        /* 2.1.0: recording, the character before the caret is marked deleted; the caret goes before it */
        ow_position end = clamp_position(editor, editor->selection.focus);
        pos = move_left_pos(editor, end);
        rc = same_position(pos, end) ? 0 : tracked_delete_range(editor, &pos, end);
        editor->selection.anchor = pos;
        editor->selection.focus = pos;
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
    editor->dirty = 1; ++editor->changes;
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
        rc = editor->track ? tracked_delete_selection(editor) : delete_selection_internal(editor);
    } else if (editor->track) {
        /* 2.1.0: recording, the character after the caret is marked deleted; the caret goes past it */
        ow_position end;
        pos = clamp_position(editor, editor->selection.focus);
        end = move_right_pos(editor, pos);
        rc = same_position(pos, end) ? 0 : tracked_delete_range(editor, &pos, end);
        if (rc >= 0) pos = skip_deleted(editor, pos);
        editor->selection.anchor = pos;
        editor->selection.focus = pos;
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
    editor->dirty = 1; ++editor->changes;
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
        rc = editor->track ? tracked_delete_selection(editor) : delete_selection_internal(editor);
        if (rc < 0) return OWF_ERR_MEMORY;
    }
    pos = editor->selection.focus;
    /* 2.1.0: recording, the new paragraph mark is the author's insertion (split_paragraph) */
    editor->typing_rev = editor->track ? editor_revision(editor, OWF_REV_INSERT) : 0;
    if (editor->track && !editor->typing_rev) return OWF_ERR_MEMORY;
    rc = split_paragraph(editor, pos, &next);
    editor->typing_rev = 0;
    if (rc < 0) return OWF_ERR_MEMORY;
    editor->selection.anchor = next;
    editor->selection.focus = next;
    editor->dirty = 1; ++editor->changes;
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

unsigned long ow_editor_document_bytes(const ow_editor *editor)
{
    return editor && editor->doc ? doc_bytes(editor->doc) : 0;
}

unsigned long ow_editor_snapshot_bytes(const ow_editor *editor, int *nundo, int *nredo)
{
    unsigned long n = 0;
    int i;
    if (nundo) *nundo = editor ? editor->nundo : 0;
    if (nredo) *nredo = editor ? editor->nredo : 0;
    if (!editor) return 0;
    for (i = 0; i < editor->nundo; ++i) n += editor->undo[i].bytes;
    for (i = 0; i < editor->nredo; ++i) n += editor->redo[i].bytes;
    return n;
}

unsigned long ow_editor_changes(const ow_editor *editor)
{
    return editor ? editor->changes : 0;
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
    if (OWF_LINE_IS_HEIGHT(p->fmt.line_spacing) &&          /* 2.0.18: exactly, or at least, a height */
        (OWF_LINE_IS_EXACT(p->fmt.line_spacing) || OWF_LINE_HEIGHT(p->fmt.line_spacing) > line_height))
        line_height = OWF_LINE_HEIGHT(p->fmt.line_spacing);
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
    ++editor->changes;
    n = doc->body.nparas;
    if (n > editor->para_cap) {
        int *pp, *py;
        pp = (int *)owf_realloc(editor->para_page, (size_t)n * sizeof(*pp));
        if (!pp) return OWF_ERR_MEMORY;
        editor->para_page = pp;
        py = (int *)owf_realloc(editor->para_y, (size_t)n * sizeof(*py));
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
    if (box_of(editor, editor->selection.focus.box)) {    /* 2.0.15: in a text box, its paragraph's page */
        int k = editor->selection.focus.box - 1, j;
        for (p = 0; p < editor->doc->body.nparas; ++p) {
            const owf_para *q = &editor->doc->body.paras[p];
            for (j = 0; j < q->nruns && !(q->runs[j].kind == OWF_RUN_FRAME && q->runs[j].image == k); ++j) {}
            if (j < q->nruns) break;
        }
    }
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
    editor->dirty = 1; ++editor->changes;
    return ow_editor_layout(editor);
}

int ow_editor_current_section(const ow_editor *editor, int *start_paragraph,
                              int *columns, int *gap, owf_section_break *break_kind)
{
    const owf_story *story;
    int p, start = 0, cols = 1, space = 0;
    owf_section_break kind = OWF_SECTION_CONTINUOUS;
    if (!editor || !editor->doc || editor->selection.focus.box) return 0;
    story = &editor->doc->body;
    if (!story->nparas) return 0;
    p = editor->selection.focus.paragraph;
    if (p < 0) p = 0;
    if (p >= story->nparas) p = story->nparas - 1;
    for (; p >= 0; --p)
        if (story->paras[p].section_columns > 0) {
            start = p;
            cols = story->paras[p].section_columns;
            space = story->paras[p].section_gap;
            kind = (owf_section_break)story->paras[p].section_break;
            break;
        }
    if (start_paragraph) *start_paragraph = start;
    if (columns) *columns = cols;
    if (gap) *gap = space;
    if (break_kind) *break_kind = kind;
    return 1;
}

int ow_editor_apply_section(ow_editor *editor, int columns, int gap,
                            owf_section_break break_kind)
{
    owf_para *p;
    int at;
    if (!editor || !editor->doc || editor->selection.focus.box ||
        columns < 1 || columns > 16 || gap < 0 ||
        break_kind < OWF_SECTION_CONTINUOUS || break_kind > OWF_SECTION_EVEN_PAGE)
        return OWF_ERR_FORMAT;
    at = editor->selection.focus.paragraph;
    if (at < 0 || at >= editor->doc->body.nparas) return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    p = &editor->doc->body.paras[at];
    p->section_columns = columns;
    p->section_gap = gap;
    p->section_break = at ? (unsigned char)break_kind : OWF_SECTION_CONTINUOUS;
    if (at && break_kind != OWF_SECTION_CONTINUOUS) p->fmt.page_break_before = 1;
    else if (at) p->fmt.page_break_before = 0;
    editor->dirty = 1;
    ++editor->changes;
    return ow_editor_layout(editor);
}

int ow_editor_apply_document_columns(ow_editor *editor, int columns, int gap)
{
    int i;
    if (!editor || !editor->doc || columns < 1 || columns > 16 || gap < 0 ||
        editor->doc->body.nparas < 1)
        return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    for (i = 0; i < editor->doc->body.nparas; ++i) {
        owf_para *p = &editor->doc->body.paras[i];
        p->section_columns = 0;
        p->section_gap = 0;
        p->section_break = OWF_SECTION_CONTINUOUS;
    }
    editor->doc->body.paras[0].section_columns = columns;
    editor->doc->body.paras[0].section_gap = gap;
    editor->dirty = 1;
    ++editor->changes;
    return ow_editor_layout(editor);
}

int ow_editor_insert_page_break(ow_editor *editor)
{
    ow_position pos, next;
    int rc;
    if (!editor || !editor->doc) return OWF_ERR_FORMAT;
    if (box_of(editor, editor->selection.focus.box)) return OWF_ERR_FORMAT;   /* 2.0.15: not in a text box */
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    if (!ow_editor_selection_empty(editor)) {
        rc = delete_selection_internal(editor);
        if (rc < 0) return OWF_ERR_MEMORY;
    }
    pos = editor->selection.focus;
    rc = split_paragraph(editor, pos, &next);
    if (rc < 0) return OWF_ERR_MEMORY;
    ed_story(editor)->paras[next.paragraph].fmt.page_break_before = 1;
    editor->selection.anchor = next;
    editor->selection.focus = next;
    editor->dirty = 1; ++editor->changes;
    sync_typing_fmt(editor);
    return ow_editor_layout(editor);
}

typedef struct {
    int paragraph;
    int level;
    char *text;
    char *bookmark;
} ow_toc_heading;

static void toc_headings_free(ow_toc_heading *headings, int count)
{
    int i;
    if (!headings) return;
    for (i = 0; i < count; ++i) {
        owf_free(headings[i].text);
        owf_free(headings[i].bookmark);
    }
    owf_free(headings);
}

static char *toc_paragraph_text(const owf_para *para)
{
    size_t length = 0, at = 0;
    char *text;
    int i;
    if (!para) return NULL;
    for (i = 0; i < para->nruns; ++i) {
        const owf_run *run = &para->runs[i];
        if (run->kind == OWF_RUN_TEXT && run->text) length += strlen(run->text);
        else if (run->kind == OWF_RUN_TAB || run->kind == OWF_RUN_LINEBREAK) ++length;
    }
    text = (char *)owf_malloc(length + 1);
    if (!text) return NULL;
    for (i = 0; i < para->nruns; ++i) {
        const owf_run *run = &para->runs[i];
        size_t n;
        if (run->kind == OWF_RUN_TEXT && run->text) {
            n = strlen(run->text);
            memcpy(text + at, run->text, n);
            at += n;
        } else if (run->kind == OWF_RUN_TAB || run->kind == OWF_RUN_LINEBREAK) {
            text[at++] = ' ';
        }
    }
    while (at && text[at - 1] == ' ') --at;
    text[at] = 0;
    return text;
}

static int toc_bookmark_exists(const owf_doc *doc, const char *name)
{
    size_t wanted = strlen(name);
    int i;
    for (i = 0; i < doc->body.nparas; ++i) {
        const char *p = doc->body.paras[i].bookmarks;
        while (p && *p) {
            const char *end = strchr(p, ' ');
            const char *place = strchr(p, '@');
            size_t n = (size_t)((place && (!end || place < end) ? place : end ? end : p + strlen(p)) - p);
            if (n == wanted && !memcmp(p, name, n)) return 1;
            p = end ? end + 1 : NULL;
        }
    }
    return 0;
}

static int toc_add_bookmark(owf_para *para, const char *name, size_t text_length)
{
    size_t old = para->bookmarks ? strlen(para->bookmarks) : 0;
    size_t need = old + (old ? 1 : 0) + strlen(name) + 2 + 2 * sizeof(size_t) * 3;
    char *marks = (char *)owf_malloc(need);
    if (!marks) return 0;
    snprintf(marks, need, "%s%s%s@0-%lu", para->bookmarks ? para->bookmarks : "",
             old ? " " : "", name, (unsigned long)text_length);
    owf_free(para->bookmarks);
    para->bookmarks = marks;
    return 1;
}

static char *toc_href(const char *bookmark)
{
    size_t n = strlen(bookmark);
    char *href = (char *)owf_malloc(n + 2);
    if (!href) return NULL;
    href[0] = '#';
    memcpy(href + 1, bookmark, n + 1);
    return href;
}

int ow_editor_insert_table_of_contents(ow_editor *editor, int max_level,
                                       ow_toc_pages_fn resolve_pages,
                                       void *userdata)
{
    ow_toc_heading *headings = NULL;
    int *paragraphs = NULL, *pages = NULL;
    ow_position after;
    owf_story *story;
    owf_parafmt title_fmt, entry_fmt;
    owf_charfmt title_cf, entry_cf;
    int base, count = 0, capacity = 0, i, rc = OWF_ERR_MEMORY, inserted;
    unsigned bookmark_number = 1;
    if (!editor || !editor->doc || !resolve_pages || max_level < 1 || max_level > 6 ||
        editor->selection.focus.box || editor->selection.anchor.box)
        return OWF_ERR_FORMAT;
    story = &editor->doc->body;
    if (!ow_editor_selection_empty(editor) || !story->nparas || editor->selection.focus.paragraph < 0 ||
        editor->selection.focus.paragraph >= story->nparas ||
        story->paras[editor->selection.focus.paragraph].table_id >= 0 ||
        story->paras[editor->selection.focus.paragraph].fmt.heading ||
        editor->selection.focus.run < 0 ||
        editor->selection.focus.run >= story->paras[editor->selection.focus.paragraph].nruns ||
        story->paras[editor->selection.focus.paragraph].runs[editor->selection.focus.run].kind != OWF_RUN_TEXT ||
        !story->paras[editor->selection.focus.paragraph].runs[editor->selection.focus.run].text)
        return OWF_ERR_FORMAT;
    for (i = 0; i < story->nparas; ++i)
        if (story->paras[i].fmt.heading > 0 && story->paras[i].fmt.heading <= max_level)
            ++count;
    if (!count) return OWF_ERR_UNSUPPORTED;
    capacity = count;
    headings = (ow_toc_heading *)owf_calloc((size_t)count, sizeof(*headings));
    paragraphs = (int *)owf_malloc((size_t)count * sizeof(*paragraphs));
    pages = (int *)owf_malloc((size_t)count * sizeof(*pages));
    if (!headings || !paragraphs || !pages) goto done;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) goto done;
    if (split_paragraph(editor, editor->selection.focus, &after) < 0) goto undo;
    story = &editor->doc->body;
    base = after.paragraph;
    for (i = 0, count = 0; i < story->nparas; ++i) {
        owf_para *para = &story->paras[i];
        char name[40];
        if (para->fmt.heading < 1 || para->fmt.heading > max_level) continue;
        headings[count].paragraph = i;
        headings[count].level = para->fmt.heading;
        headings[count].text = toc_paragraph_text(para);
        if (!headings[count].text) goto undo;
        do {
            snprintf(name, sizeof name, "_GFTOC%u", bookmark_number++);
        } while (toc_bookmark_exists(editor->doc, name));
        headings[count].bookmark = copy_bytes(name, strlen(name));
        if (!headings[count].bookmark ||
            !toc_add_bookmark(para, name, strlen(headings[count].text))) goto undo;
        ++count;
    }
    inserted = count + 1;
    owf_parafmt_init(&title_fmt);
    title_cf = editor->doc->base;
    title_cf.flags |= OWF_BOLD;
    if (!title_cf.size || title_cf.size < 320) title_cf.size = 320;
    {
        owf_para *title = insert_paragraph(story, base, &title_fmt);
        if (!title || !(title->style = copy_bytes("TOCHeading", 10)) ||
            owf_para_add_text(title, &title_cf, "Contents", 8) != OWF_OK) goto undo;
    }
    for (i = 0; i < count; ++i) {
        owf_para *entry;
        owf_run *run;
        char style[16];
        owf_parafmt_init(&entry_fmt);
        entry_fmt.indent_left = (headings[i].level - 1) * 360;
        entry_fmt.ntabs = 1;
        entry_fmt.tabs[0].position = editor->doc->page.width - editor->doc->page.margin_left -
                                     editor->doc->page.margin_right - entry_fmt.indent_left;
        if (entry_fmt.tabs[0].position < 720) entry_fmt.tabs[0].position = 720;
        entry_fmt.tabs[0].kind = OWF_TAB_RIGHT;
        entry_fmt.tabs[0].leader = OWF_LEADER_DOT;
        entry = insert_paragraph(story, base + 1 + i, &entry_fmt);
        snprintf(style, sizeof style, "TOC%d", headings[i].level);
        if (!entry || !(entry->style = copy_bytes(style, strlen(style)))) goto undo;
        entry_cf = editor->doc->base;
        if (owf_para_add_text(entry, &entry_cf, headings[i].text, strlen(headings[i].text)) != OWF_OK)
            goto undo;
        run = &entry->runs[entry->nruns - 1];
        run->href = toc_href(headings[i].bookmark);
        if (!run->href || owf_para_add_special(entry, &entry_cf, OWF_RUN_TAB, OWF_FIELD_PAGE) != OWF_OK)
            goto undo;
        if (headings[i].paragraph >= base) headings[i].paragraph += inserted;
        paragraphs[i] = headings[i].paragraph;
    }
    if (!resolve_pages(userdata, editor->doc, paragraphs, count, pages)) goto undo;
    for (i = 0; i < count; ++i) {
        owf_para *entry = &story->paras[base + 1 + i];
        char label[48];
        int first = editor->doc->page.start_page > 0 ? editor->doc->page.start_page : 1;
        if (pages[i] < 0) goto undo;
        owf_page_number(&editor->doc->page, first + pages[i], label, sizeof label);
        if (owf_para_add_text(entry, &entry_cf, label, strlen(label)) != OWF_OK) goto undo;
        entry->runs[entry->nruns - 1].href = toc_href(headings[i].bookmark);
        if (!entry->runs[entry->nruns - 1].href) goto undo;
    }
    after.paragraph += inserted;
    editor->selection.anchor = editor->selection.focus = after;
    editor->dirty = 1;
    ++editor->changes;
    rc = ow_editor_layout(editor);
    goto done;
undo:
    ow_editor_undo(editor);
done:
    toc_headings_free(headings, capacity);
    owf_free(paragraphs);
    owf_free(pages);
    return rc;
}

static int current_table_cell(const ow_editor *editor,int *id,int *row,int *col,int *cols)
{
    int p;if(!editor||!editor->doc)return 0;p=editor->selection.focus.paragraph;if(p<0||p>=ed_story(editor)->nparas||ed_story(editor)->paras[p].table_id<0)return 0;if(id)*id=ed_story(editor)->paras[p].table_id;if(row)*row=ed_story(editor)->paras[p].table_row;if(col)*col=ed_story(editor)->paras[p].table_col;if(cols)*cols=ed_story(editor)->paras[p].table_cols;return 1;
}

int ow_editor_in_table(const ow_editor *editor){return current_table_cell(editor,NULL,NULL,NULL,NULL);}

int ow_editor_table_apply_style(ow_editor *editor, ow_table_scope scope,
                                unsigned long shading, int borders,
                                unsigned long border_colour)
{
    owf_story *story;
    int id, row, col, i, changed = 0;
    if (!current_table_cell(editor, &id, &row, &col, NULL) ||
        scope < OW_TABLE_CELL || scope > OW_TABLE_ALL ||
        (borders & ~(OWF_BORDER_TOP | OWF_BORDER_LEFT | OWF_BORDER_BOTTOM | OWF_BORDER_RIGHT)))
        return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    story = ed_story(editor);
    for (i = 0; i < story->nparas; ++i) {
        owf_para *p = &story->paras[i];
        if (p->table_id != id ||
            (scope == OW_TABLE_ROW && p->table_row != row) ||
            (scope == OW_TABLE_CELL && (p->table_row != row || p->table_col != col)))
            continue;
        p->cell_shading = shading;
        p->fmt.borders = borders;
        p->fmt.border_colour = border_colour;
        changed = 1;
    }
    if (!changed) {
        ow_editor_undo(editor);
        return OWF_ERR_FORMAT;
    }
    editor->dirty = 1;
    ++editor->changes;
    return ow_editor_layout(editor);
}

static void free_para_one(owf_para *p)
{
    int j;for(j=0;j<p->nruns;++j){owf_free(p->runs[j].text);owf_free(p->runs[j].href);}owf_free(p->runs);owf_free(p->style);owf_free(p->bookmarks);memset(p,0,sizeof(*p));p->table_id=-1;
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
    ow_position c;owf_para *para;if(p<0)p=0;if(p>=ed_story(editor)->nparas)p=ed_story(editor)->nparas-1;para=&ed_story(editor)->paras[p];if(first_text_run(para)<0)append_empty_text_run(para,&editor->typing_fmt);c.paragraph=p;c.run=first_text_run(para);c.byte_offset=0;c.box=editor->selection.focus.box;editor->selection.anchor=editor->selection.focus=c;
}

int ow_editor_table_move(ow_editor *editor,int delta)
{
    int id,p,target;if(!current_table_cell(editor,&id,NULL,NULL,NULL))return 0;p=editor->selection.focus.paragraph;target=p+(delta<0?-1:1);if(target>=0&&target<ed_story(editor)->nparas&&ed_story(editor)->paras[target].table_id==id){caret_to_para(editor,target);return 1;}return 0;
}

int ow_editor_table_insert_row(ow_editor *editor)
{
    int id,row,cols,i,pos,c;if(!current_table_cell(editor,&id,&row,NULL,&cols))return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;pos=editor->selection.focus.paragraph;while(pos+1<ed_story(editor)->nparas&&ed_story(editor)->paras[pos+1].table_id==id&&ed_story(editor)->paras[pos+1].table_row==row)++pos;++pos;for(i=0;i<ed_story(editor)->nparas;++i)if(ed_story(editor)->paras[i].table_id==id&&ed_story(editor)->paras[i].table_row>row)++ed_story(editor)->paras[i].table_row;for(c=0;c<cols;++c){owf_para *p=insert_paragraph(ed_story(editor),pos+c,NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}p->table_id=id;p->table_row=row+1;p->table_col=c;p->table_cols=cols;}caret_to_para(editor,pos);editor->dirty=1;++editor->changes;return ow_editor_layout(editor);
}

int ow_editor_table_delete_row(ow_editor *editor)
{
    int id,row,i,start=-1,count=0;if(!current_table_cell(editor,&id,&row,NULL,NULL))return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;for(i=0;i<ed_story(editor)->nparas;++i)if(ed_story(editor)->paras[i].table_id==id&&ed_story(editor)->paras[i].table_row==row){if(start<0)start=i;++count;}for(i=0;i<count;++i)remove_para_at(ed_story(editor),start);for(i=0;i<ed_story(editor)->nparas;++i)if(ed_story(editor)->paras[i].table_id==id&&ed_story(editor)->paras[i].table_row>row)--ed_story(editor)->paras[i].table_row;if(!ed_story(editor)->nparas){owf_para *p=owf_story_add(ed_story(editor),NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}}caret_to_para(editor,start<ed_story(editor)->nparas?start:ed_story(editor)->nparas-1);editor->dirty=1;++editor->changes;return ow_editor_layout(editor);
}

int ow_editor_table_insert_column(ow_editor *editor)
{
    int id,col,cols,i,maxrow=-1,r;if(!current_table_cell(editor,&id,NULL,&col,&cols))return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;for(i=0;i<ed_story(editor)->nparas;++i)if(ed_story(editor)->paras[i].table_id==id){if(ed_story(editor)->paras[i].table_row>maxrow)maxrow=ed_story(editor)->paras[i].table_row;if(ed_story(editor)->paras[i].table_col>col)++ed_story(editor)->paras[i].table_col;ed_story(editor)->paras[i].table_cols=cols+1;}for(r=maxrow;r>=0;--r){int pos=-1;for(i=0;i<ed_story(editor)->nparas;++i)if(ed_story(editor)->paras[i].table_id==id&&ed_story(editor)->paras[i].table_row==r&&ed_story(editor)->paras[i].table_col==col){pos=i+1;break;}if(pos>=0){owf_para *p=insert_paragraph(ed_story(editor),pos,NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}p->table_id=id;p->table_row=r;p->table_col=col+1;p->table_cols=cols+1;}}editor->dirty=1;++editor->changes;return ow_editor_layout(editor);
}

int ow_editor_table_delete_column(ow_editor *editor)
{
    int id,col,cols,i;if(!current_table_cell(editor,&id,NULL,&col,&cols))return OWF_ERR_FORMAT;if(cols<=1)return OWF_ERR_FORMAT;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;for(i=ed_story(editor)->nparas-1;i>=0;--i)if(ed_story(editor)->paras[i].table_id==id&&ed_story(editor)->paras[i].table_col==col)remove_para_at(ed_story(editor),i);for(i=0;i<ed_story(editor)->nparas;++i)if(ed_story(editor)->paras[i].table_id==id){if(ed_story(editor)->paras[i].table_col>col)--ed_story(editor)->paras[i].table_col;ed_story(editor)->paras[i].table_cols=cols-1;}caret_to_para(editor,editor->selection.focus.paragraph<ed_story(editor)->nparas?editor->selection.focus.paragraph:ed_story(editor)->nparas-1);editor->dirty=1;++editor->changes;return ow_editor_layout(editor);
}

int ow_editor_insert_image(ow_editor *editor, int image_index)
{
    ow_position pos,after,caret;owf_story *story;owf_para *p;int rc,base;
    if(!editor||!editor->doc||image_index<0||image_index>=editor->doc->nimages)return OWF_ERR_FORMAT;
    editor->typing_group=0;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;
    if(!ow_editor_selection_empty(editor)&&delete_selection_internal(editor)<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    pos=editor->selection.focus;rc=split_paragraph(editor,pos,&after);if(rc<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    story=ed_story(editor);base=after.paragraph;p=insert_paragraph(story,base,NULL);if(!p||owf_para_add_image(p,&editor->typing_fmt,image_index)!=OWF_OK){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    caret=after;caret.paragraph=base+1;caret.run=0;caret.byte_offset=0;editor->selection.anchor=editor->selection.focus=caret;editor->dirty=1;++editor->changes;return ow_editor_layout(editor);
}

/* Add a page-anchored frame to the current body paragraph. GFOpenLayout
 * already places it from that paragraph, wraps the following body lines and
 * hit-tests a text-box story; the editor only has to make the native model
 * object and put its frame run on the anchor paragraph. */
static int insert_anchored_frame(ow_editor *editor, owf_frame *frame, int enter_story)
{
    ow_position body_caret;
    owf_para *anchor;
    int frame_index;
    if (!editor || !editor->doc || !frame || editor->selection.focus.box)
        return OWF_ERR_FORMAT;
    if (frame->width <= 0 || frame->height <= 0 ||
        frame->wrap > OWF_WRAP_TOP_BOTTOM)
        return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    if (!ow_editor_selection_empty(editor) && delete_selection_internal(editor) < 0) {
        ow_editor_undo(editor);
        return OWF_ERR_MEMORY;
    }
    body_caret = clamp_position(editor, editor->selection.focus);
    anchor = &editor->doc->body.paras[body_caret.paragraph];
    frame_index = owf_doc_add_frame(editor->doc, frame);
    if (frame_index < 0 ||
        owf_para_add_frame(anchor, &editor->typing_fmt, frame_index) != OWF_OK) {
        if (frame_index < 0) free_story_data(&frame->story);
        else memset(&frame->story, 0, sizeof frame->story); /* now owned by doc; undo frees it */
        ow_editor_undo(editor);
        return OWF_ERR_MEMORY;
    }
    memset(&frame->story, 0, sizeof frame->story);         /* ownership moved to doc */
    if (enter_story) {
        const owf_story *story = &editor->doc->frames[frame_index].story;
        ow_position box_caret;
        memset(&box_caret, 0, sizeof box_caret);
        box_caret.box = frame_index + 1;
        if (story->nparas && story->paras[0].nruns) {
            const owf_run *run = &story->paras[0].runs[0];
            box_caret.byte_offset = run->kind == OWF_RUN_TEXT && run->text ? strlen(run->text) : 0;
        }
        editor->selection.anchor = editor->selection.focus = box_caret;
    } else {
        editor->selection.anchor = editor->selection.focus = body_caret;
    }
    editor->dirty = 1;
    ++editor->changes;
    return ow_editor_layout(editor);
}

int ow_editor_insert_text_box(ow_editor *editor, const char *utf8,
                              int width, int height, owf_frame_wrap wrap)
{
    owf_frame frame;
    owf_para *para;
    owf_parafmt pf;
    owf_charfmt cf;
    size_t length;
    if (!editor || !utf8) return OWF_ERR_FORMAT;
    memset(&frame, 0, sizeof frame);
    frame.image = -1;
    frame.width = width;
    frame.height = height;
    frame.x_from = OWF_FROM_MARGIN;
    frame.y_from = OWF_FROM_PARAGRAPH;
    frame.x_place = OWF_PLACE_END;
    frame.y_place = OWF_PLACE_AT;
    frame.wrap = (unsigned char)wrap;
    frame.inset[0] = frame.inset[1] = frame.inset[2] = frame.inset[3] = 120;
    frame.line = OWF_SHADE(0x000000UL);
    owf_parafmt_init(&pf);
    cf = editor->typing_fmt_set ? editor->typing_fmt : editor->doc->base;
    para = owf_story_add(&frame.story, &pf);
    length = strlen(utf8);
    if (!para || owf_para_add_text(para, &cf, utf8, length) != OWF_OK) {
        free_story_data(&frame.story);
        return OWF_ERR_MEMORY;
    }
    return insert_anchored_frame(editor, &frame, 1);
}

int ow_editor_insert_floating_image(ow_editor *editor, int image_index,
                                    owf_frame_wrap wrap)
{
    owf_frame frame;
    const owf_image *image;
    if (!editor || !editor->doc || image_index < 0 || image_index >= editor->doc->nimages)
        return OWF_ERR_FORMAT;
    image = &editor->doc->images[image_index];
    memset(&frame, 0, sizeof frame);
    frame.image = image_index;
    frame.width = image->width > 0 ? image->width : 4320;
    frame.height = image->height > 0 ? image->height : 2880;
    frame.x_from = OWF_FROM_MARGIN;
    frame.y_from = OWF_FROM_PARAGRAPH;
    frame.x_place = OWF_PLACE_END;
    frame.y_place = OWF_PLACE_AT;
    frame.wrap = (unsigned char)wrap;
    return insert_anchored_frame(editor, &frame, 0);
}

void ow_frame_adjust_rect(int *x, int *y, int *width, int *height,
                          ow_frame_handle handle, int dx, int dy,
                          int min_width, int min_height)
{
    int right, bottom;
    if (!x || !y || !width || !height || handle < OW_FRAME_MOVE ||
        handle > OW_FRAME_RESIZE_SE)
        return;
    if (min_width < 1) min_width = 1;
    if (min_height < 1) min_height = 1;
    right = *x + *width;
    bottom = *y + *height;
    switch (handle) {
    case OW_FRAME_MOVE: *x += dx; *y += dy; break;
    case OW_FRAME_RESIZE_NW: *x += dx; *y += dy; *width -= dx; *height -= dy; break;
    case OW_FRAME_RESIZE_NE: *y += dy; *width += dx; *height -= dy; break;
    case OW_FRAME_RESIZE_SW: *x += dx; *width -= dx; *height += dy; break;
    case OW_FRAME_RESIZE_SE: *width += dx; *height += dy; break;
    }
    if (*width < min_width) {
        if (handle == OW_FRAME_RESIZE_NW || handle == OW_FRAME_RESIZE_SW)
            *x = right - min_width;
        *width = min_width;
    }
    if (*height < min_height) {
        if (handle == OW_FRAME_RESIZE_NW || handle == OW_FRAME_RESIZE_NE)
            *y = bottom - min_height;
        *height = min_height;
    }
}

int ow_editor_apply_frame_geometry(ow_editor *editor, int frame_index,
                                   int x, int y, int width, int height,
                                   owf_frame_wrap wrap)
{
    owf_frame *frame;
    if (!editor || !editor->doc || frame_index < 0 || frame_index >= editor->doc->nframes ||
        width <= 0 || height <= 0 || wrap > OWF_WRAP_TOP_BOTTOM)
        return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    frame = &editor->doc->frames[frame_index];
    frame->x = x;
    frame->y = y;
    frame->width = width;
    frame->height = height;
    frame->x_from = OWF_FROM_PAGE;
    frame->y_from = OWF_FROM_PAGE;
    frame->x_place = OWF_PLACE_AT;
    frame->y_place = OWF_PLACE_AT;
    frame->in_line = 0;
    frame->wrap = (unsigned char)wrap;
    editor->dirty = 1;
    ++editor->changes;
    return ow_editor_layout(editor);
}

static int remove_frame_refs(ow_editor *editor, owf_story *story,
                             int frame_index, int body, int *anchor_para)
{
    int i, j;
    if (!story) return 1;
    for (i = 0; i < story->nparas; ++i) {
        owf_para *para = &story->paras[i];
        for (j = para->nruns - 1; j >= 0; --j) {
            owf_run *run = &para->runs[j];
            if (run->kind != OWF_RUN_FRAME) continue;
            if (run->image > frame_index) {
                --run->image;
                continue;
            }
            if (run->image != frame_index) continue;
            if (body && anchor_para && *anchor_para < 0) *anchor_para = i;
            owf_free(run->text);
            owf_free(run->href);
            if (j + 1 < para->nruns)
                memmove(&para->runs[j], &para->runs[j + 1],
                        (size_t)(para->nruns - j - 1) * sizeof(*para->runs));
            --para->nruns;
            memset(&para->runs[para->nruns], 0, sizeof(*para->runs));
        }
        if (body && !para->nruns && !append_empty_text_run(para, &editor->typing_fmt))
            return 0;
    }
    return 1;
}

int ow_editor_delete_frame(ow_editor *editor, int frame_index)
{
    owf_doc *doc;
    owf_story *fixed[5];
    int i, anchor = -1;
    if (!editor || !editor->doc || frame_index < 0 || frame_index >= editor->doc->nframes)
        return OWF_ERR_FORMAT;
    editor->typing_group = 0;
    if (!remember_before_edit(editor)) return OWF_ERR_MEMORY;
    doc = editor->doc;
    fixed[0] = &doc->body; fixed[1] = &doc->header; fixed[2] = &doc->footer;
    fixed[3] = &doc->header_left; fixed[4] = &doc->footer_left;
    for (i = 0; i < 5; ++i)
        if (!remove_frame_refs(editor, fixed[i], frame_index, i == 0, &anchor)) {
            ow_editor_undo(editor);
            return OWF_ERR_MEMORY;
        }
    for (i = 0; i < doc->nframes; ++i)
        if (i != frame_index &&
            !remove_frame_refs(editor, &doc->frames[i].story, frame_index, 0, NULL)) {
            ow_editor_undo(editor);
            return OWF_ERR_MEMORY;
        }
    free_story_data(&doc->frames[frame_index].story);
    if (frame_index + 1 < doc->nframes)
        memmove(&doc->frames[frame_index], &doc->frames[frame_index + 1],
                (size_t)(doc->nframes - frame_index - 1) * sizeof(*doc->frames));
    --doc->nframes;
    memset(&doc->frames[doc->nframes], 0, sizeof(*doc->frames));
    if (anchor < 0) anchor = editor->selection.focus.paragraph;
    if (anchor < 0) anchor = 0;
    if (anchor >= doc->body.nparas) anchor = doc->body.nparas - 1;
    editor->selection.anchor.box = editor->selection.focus.box = 0;
    caret_to_para(editor, anchor);
    editor->dirty = 1;
    ++editor->changes;
    return ow_editor_layout(editor);
}

int ow_editor_insert_table(ow_editor *editor, int rows, int cols)
{
    ow_position pos,after,caret;
    owf_story *story;
    int rc,base,k,r,c,id=0,i;
    if(!editor||rows<1||cols<1||rows>64||cols>32)return OWF_ERR_FORMAT;
    editor->typing_group=0;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;
    if(!ow_editor_selection_empty(editor)&&delete_selection_internal(editor)<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    for(i=0;i<ed_story(editor)->nparas;++i)if(ed_story(editor)->paras[i].table_id>=id)id=ed_story(editor)->paras[i].table_id+1;
    pos=editor->selection.focus;rc=split_paragraph(editor,pos,&after);if(rc<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    story=ed_story(editor);base=after.paragraph;
    for(k=0,r=0;r<rows;++r)for(c=0;c<cols;++c,++k){owf_para *p=insert_paragraph(story,base+k,NULL);if(!p||!append_empty_text_run(p,&editor->typing_fmt)){ow_editor_undo(editor);return OWF_ERR_MEMORY;}p->table_id=id;p->table_row=r;p->table_col=c;p->table_cols=cols;}
    caret=after;caret.paragraph=base;caret.run=0;caret.byte_offset=0;editor->selection.anchor=editor->selection.focus=caret;editor->dirty=1;++editor->changes;return ow_editor_layout(editor);
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
           which == OW_STORY_FOOTER ? &editor->doc->footer :
           which == OW_STORY_HEADER_LEFT ? &editor->doc->header_left :     /* 2.0.23 */
           which == OW_STORY_FOOTER_LEFT ? &editor->doc->footer_left : NULL;
}

static const owf_story *story_for_const(const ow_editor *editor, ow_story_kind which)
{
    return story_for((ow_editor *)editor, which);
}

char *ow_editor_story_text(const ow_editor *editor, ow_story_kind which)
{
    const owf_story *story = story_for_const(editor, which);
    size_t cap = 128, n = 0;
    char *out;
    int p, r;
    if (!story) return NULL;
    out = (char *)owf_malloc(cap);
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
                char *q = (char *)owf_realloc(out, want);
                if (!q) { owf_free(out); return NULL; }
                out = q; cap = want;
            }
            memcpy(out + n, text, add); n += add; out[n] = 0;
        }
        if (p + 1 < story->nparas) {
            if (n + 2 > cap) {
                char *q = (char *)owf_realloc(out, cap * 2);
                if (!q) { owf_free(out); return NULL; }
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
    if (!*text && (which == OW_STORY_HEADER_LEFT || which == OW_STORY_FOOTER_LEFT)) {
        editor->dirty = 1; ++editor->changes;        /* 2.0.23: none of their own: left pages have the others */
        return OWF_OK;
    }
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
    editor->dirty = 1; ++editor->changes;
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
    if(!parts[n].text||((HREF)&&!parts[n].href)){for(i=0;i<=n;++i){owf_free(parts[i].text);owf_free(parts[i].href);}return 0;} ++n; \
} while(0)
    if(from) MAKE_PART(0,from,old.href);
    MAKE_PART(from,to,url);
    if(to<len) MAKE_PART(to,len,old.href);
#undef MAKE_PART
    extra=n-1;
    if(extra>0&&!ensure_run_capacity(para,para->nruns+extra)){for(i=0;i<n;++i){owf_free(parts[i].text);owf_free(parts[i].href);}return 0;}
    if(extra>0&&index+1<para->nruns)memmove(&para->runs[index+n],&para->runs[index+1],(size_t)(para->nruns-index-1)*sizeof(para->runs[0]));
    owf_free(old.text);owf_free(old.href);
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
        owf_para *p=&ed_story(editor)->paras[pi];
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
    editor->dirty=1;++editor->changes;return OWF_OK;
}

int ow_editor_insert_link(ow_editor *editor, const char *text, const char *url)
{
    ow_position pos;
    owf_para *para;
    owf_run *run;
    owf_charfmt fmt;
    char *right,*oldhref,*linked;
    size_t right_len,link_at;
    int old_n,move;
    if(!editor||!text||!*text||!url||!*url)return OWF_ERR_FORMAT;
    editor->typing_group=0;if(!remember_before_edit(editor))return OWF_ERR_MEMORY;
    if(!ow_editor_selection_empty(editor)&&delete_selection_internal(editor)<0){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    pos=clamp_position(editor,editor->selection.focus);para=&ed_story(editor)->paras[pos.paragraph];run=&para->runs[pos.run];fmt=run->fmt;
    link_at=para_text_offset(para,pos);
    right_len=strlen(run->text)-pos.byte_offset;right=copy_bytes(run->text+pos.byte_offset,right_len);oldhref=copy_cstr(run->href);linked=copy_bytes(text,strlen(text));
    if(!right||!linked||(run->href&&!oldhref)){owf_free(right);owf_free(oldhref);owf_free(linked);ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    if(!replace_run_slice(run,pos.byte_offset,strlen(run->text),NULL,0)){owf_free(right);owf_free(oldhref);owf_free(linked);ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    old_n=para->nruns;if(!ensure_run_capacity(para,old_n+2)){owf_free(right);owf_free(oldhref);owf_free(linked);ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    move=old_n-pos.run-1;if(move>0)memmove(&para->runs[pos.run+3],&para->runs[pos.run+1],(size_t)move*sizeof(para->runs[0]));
    memset(&para->runs[pos.run+1],0,2*sizeof(para->runs[0]));
    para->runs[pos.run+1].kind=OWF_RUN_TEXT;para->runs[pos.run+1].fmt=fmt;para->runs[pos.run+1].text=linked;para->runs[pos.run+1].href=copy_cstr(url);
    para->runs[pos.run+2].kind=OWF_RUN_TEXT;para->runs[pos.run+2].fmt=fmt;para->runs[pos.run+2].text=right;para->runs[pos.run+2].href=oldhref;
    if(!para->runs[pos.run+1].href){ow_editor_undo(editor);return OWF_ERR_MEMORY;}
    para->nruns=old_n+2;marks_edit(para,link_at,0,strlen(text));   /* 2.0.21: the bookmarks after it move on */
    pos.run+=2;pos.byte_offset=0;editor->selection.anchor=editor->selection.focus=pos;editor->dirty=1;++editor->changes;return ow_editor_layout(editor);
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
    para = &ed_story(editor)->paras[pos.paragraph];
    run = &para->runs[pos.run];
    fmt = run->fmt;
    right_len = strlen(run->text) - pos.byte_offset;
    right = copy_bytes(run->text + pos.byte_offset, right_len);
    if (!right) { ow_editor_undo(editor); return OWF_ERR_MEMORY; }
    if (!replace_run_slice(run, pos.byte_offset, strlen(run->text), NULL, 0)) {
        owf_free(right); ow_editor_undo(editor); return OWF_ERR_MEMORY;
    }
    old_n = para->nruns;
    if (!ensure_run_capacity(para, old_n + 2)) {
        owf_free(right); ow_editor_undo(editor); return OWF_ERR_MEMORY;
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
    editor->dirty = 1; ++editor->changes;
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
    const owf_story *header, *footer;
    int i, j;
    if (!editor || !editor->doc || !renderer) return OWF_ERR_FORMAT;
    if (page_index < 0 || page_index >= editor->pages) return OWF_ERR_FORMAT;

    page.width_twips = editor->doc->page.width;
    page.height_twips = editor->doc->page.height;
    page.page_index = page_index;
    if (renderer->begin_page) renderer->begin_page(renderer->userdata, &page);

    {
        /* 2.0.23: an even-numbered page is a left one, with left pages' own when there are */
        int number = (editor->doc->page.start_page > 0 ? editor->doc->page.start_page : 1) + page_index;
        header = !(number & 1) && editor->doc->header_left.nparas ? &editor->doc->header_left : &editor->doc->header;
        footer = !(number & 1) && editor->doc->footer_left.nparas ? &editor->doc->footer_left : &editor->doc->footer;
    }
    if ((page_index > 0 || editor->doc->page.header_on_first) && header->nparas)
        render_story(editor, header, -1000,
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
            /* every cell of a row draws the row's height, so the row's lines meet */
            int rh=240,k;
            for(k=0;k<editor->doc->body.nparas;++k){const owf_para *q=&editor->doc->body.paras[k];if(q->table_id==p->table_id&&q->table_row==p->table_row){int h=paragraph_estimated_height_width(editor->doc,q,cw-240);if(h>rh)rh=h;}}
            rh+=240;
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

    if ((page_index > 0 || editor->doc->page.footer_on_first) && footer->nparas)
        render_story(editor, footer, -2000,
                     editor->doc->page.margin_left,
                     editor->doc->page.height - editor->doc->page.margin_bottom / 2,
                     page_index, renderer);

    if (renderer->end_page) renderer->end_page(renderer->userdata, &page);
    return OWF_OK;
}

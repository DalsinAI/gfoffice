/*
 * libowf: PowerPoint 97-2003 in (.ppt, .pps, .pot), into a deck (owf_deck.h).
 * Written from Microsoft's published [MS-PPT] (the records) and [MS-ODRAW]
 * (the drawings) only; no other program's code was read.
 *
 * A .ppt is a compound file (owf_cfb.c). Its "Current User" stream says
 * where the last save's UserEditAtom is in the "PowerPoint Document"
 * stream; each UserEditAtom points to the one before and to a
 * PersistDirectoryAtom, which says where each persist object (the document,
 * each slide, master and notes page) is. Newer saves win, so the
 * directories are read newest first and the first offset found for an id
 * is the one.
 *
 * Records are an 8-byte header (u16: version in the low 4 bits, instance
 * above; u16 type; u32 length) then their bytes; version 15 holds records.
 * The DocumentContainer holds the slide size (DocumentAtom), the fonts
 * (Environment's FontCollection), the default text styles
 * (TextMasterStyleAtom), the picture store (DrawingGroup's OfficeArt
 * BStore, the pictures themselves in the "Pictures" stream) and the lists
 * of masters, slides and notes (SlideListWithText, instance 1, 0 and 2):
 * one SlidePersistAtom for each, which names its persist object. A slide's
 * outline text follows its SlidePersistAtom there; its placeholders name it
 * by OutlineTextRefAtom.
 *
 * Each slide, master and notes page is a container with its atom (its
 * master, its notes, which of its master's it takes), its colour scheme
 * (eight colours) and an OfficeArt drawing: shapes, groups, a background
 * shape. A shape is an FSP (its kind as the instance, flips, deleted,
 * background), its properties (FOPT: fill, line, picture, crop, text
 * insets, rotation, name), its box (ClientAnchor in master units, 576 an
 * inch, or ChildAnchor in its group's own units), a PlaceholderAtom in its
 * ClientData and its text in its ClientTextbox: TextHeaderAtom, the text as
 * UTF-16 (TextCharsAtom) or as its low bytes (TextBytesAtom), and its
 * paragraph and character runs (StyleTextPropAtom). Colours may name the
 * slide's scheme.
 *
 * Read: the slide size, masters (title masters too) with their text styles,
 * placeholders and backgrounds, slides in order with their text frames,
 * placeholders, shapes the model has, lines, pictures (JPEG, PNG, DIB, WMF,
 * EMF), groups, text runs (font, size, bold, italic, underline, colour,
 * raised and lowered), paragraphs (alignment, spacing, list levels),
 * slide-number and date fields, backgrounds, hidden slides and speaker
 * notes. Approximated and reported: fills other than one colour, other
 * shapes (as rectangles), embedded objects (as their pictures). Left out
 * and reported: animations, transitions, sounds, password-protected files.
 * GFOpenPresentation saves what it read as ODP or PPTX.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_deck_int.h"

#include "owf_inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* record types ([MS-PPT] 2.13.24, [MS-ODRAW] 2.2) */
#define RT_DOCUMENT 0x03E8
#define RT_DOCUMENT_ATOM 0x03E9
#define RT_SLIDE 0x03EE
#define RT_SLIDE_ATOM 0x03EF
#define RT_NOTES 0x03F0
#define RT_NOTES_ATOM 0x03F1
#define RT_ENVIRONMENT 0x03F2
#define RT_SLIDE_PERSIST_ATOM 0x03F3
#define RT_MAIN_MASTER 0x03F8
#define RT_SLIDE_SHOW_SLIDE_INFO 0x03F9
#define RT_DRAWING_GROUP 0x040B
#define RT_DRAWING 0x040C
#define RT_FONT_COLLECTION 0x07D5
#define RT_COLOR_SCHEME_ATOM 0x07F0
#define RT_PLACEHOLDER_ATOM 0x0BC3
#define RT_OUTLINE_TEXT_REF 0x0F9E
#define RT_TEXT_HEADER 0x0F9F
#define RT_TEXT_CHARS 0x0FA0
#define RT_STYLE_TEXT_PROP 0x0FA1
#define RT_TEXT_MASTER_STYLE 0x0FA3
#define RT_TEXT_BYTES 0x0FA8
#define RT_FONT_ENTITY 0x0FB7
#define RT_SLIDE_NUMBER_MC 0x0FD8
#define RT_SLIDE_LIST_WITH_TEXT 0x0FF0
#define RT_USER_EDIT 0x0FF5
#define RT_CURRENT_USER 0x0FF6
#define RT_DATETIME_MC 0x0FF7
#define RT_GENERIC_DATE_MC 0x0FF8
#define RT_RTF_DATETIME_MC 0x1015
#define RT_PERSIST_DIRECTORY 0x1772

#define OA_DGG_CONTAINER 0xF000
#define OA_BSTORE 0xF001
#define OA_DG_CONTAINER 0xF002
#define OA_SPGR_CONTAINER 0xF003
#define OA_SP_CONTAINER 0xF004
#define OA_FBSE 0xF007
#define OA_FSPGR 0xF009
#define OA_FSP 0xF00A
#define OA_FOPT 0xF00B
#define OA_CLIENT_TEXTBOX 0xF00D
#define OA_CHILD_ANCHOR 0xF00F
#define OA_CLIENT_ANCHOR 0xF010
#define OA_CLIENT_DATA 0xF011
#define OA_TERTIARY_FOPT 0xF122

/* FSP flags ([MS-ODRAW] 2.2.40) */
#define FSP_GROUP 0x001
#define FSP_DELETED 0x008
#define FSP_OLE 0x010
#define FSP_FLIP_H 0x040
#define FSP_FLIP_V 0x080
#define FSP_BACKGROUND 0x400

/* properties ([MS-ODRAW] 2.3) */
#define PR_ROTATION 0x0004
#define PR_TEXT_LEFT 0x0081
#define PR_TEXT_TOP 0x0082
#define PR_TEXT_RIGHT 0x0083
#define PR_TEXT_BOTTOM 0x0084
#define PR_ANCHOR_TEXT 0x0087
#define PR_CROP_TOP 0x0100
#define PR_CROP_BOTTOM 0x0101
#define PR_CROP_LEFT 0x0102
#define PR_CROP_RIGHT 0x0103
#define PR_PIB 0x0104
#define PR_ADJUST 0x0147
#define PR_FILL_TYPE 0x0180
#define PR_FILL_COLOUR 0x0181
#define PR_FILL_BOOLS 0x01BF
#define PR_LINE_COLOUR 0x01C0
#define PR_LINE_WIDTH 0x01CB
#define PR_LINE_START_ARROW 0x01D0
#define PR_LINE_END_ARROW 0x01D1
#define PR_LINE_BOOLS 0x01FF
#define PR_NAME 0x0380
#define PR_DESCRIPTION 0x0381

/* shape kinds (MSOSPT, [MS-ODRAW] 2.4.24) */
#define SPT_RECT 1
#define SPT_ROUNDRECT 2
#define SPT_ELLIPSE 3
#define SPT_DIAMOND 4
#define SPT_TRIANGLE 5
#define SPT_RT_TRIANGLE 6
#define SPT_ARROW 13
#define SPT_LINE 20
#define SPT_STRAIGHT_CONNECTOR 32
#define SPT_LEFT_ARROW 66
#define SPT_PICTURE 75
#define SPT_TEXT_BOX 202

/* text types (TextTypeEnum) */
#define TX_TITLE 0
#define TX_BODY 1
#define TX_NOTES 2
#define TX_OTHER 4
#define TX_CENTER_BODY 5
#define TX_CENTER_TITLE 6
#define TX_HALF_BODY 7
#define TX_QUARTER_BODY 8

#define MAX_COORD 2000000            /* twips, as the other deck readers */
#define MAX_EDITS 256                /* user edits followed back */
#define MAX_TEXT 65536               /* characters in one shape's text */
#define MAX_RUNS 8192                /* paragraph or character runs in one text */
#define MAX_FONTS 1024
#define MAX_BLIPS 4096

/* ---- Spans and records ---- */

typedef struct {
    const unsigned char *p;
    size_t n;
} span;

static int has(span s, size_t at, size_t n)
{
    return s.p && at <= s.n && n <= s.n - at;
}

static unsigned rd8(span s, size_t at)
{
    return has(s, at, 1) ? s.p[at] : 0;
}

static unsigned rd16(span s, size_t at)
{
    return has(s, at, 2) ? (unsigned)(s.p[at] | s.p[at + 1] << 8) : 0;
}

static int rds16(span s, size_t at)
{
    unsigned v = rd16(s, at);
    return v & 0x8000u ? (int)v - 0x10000 : (int)v;
}

static unsigned long rd32(span s, size_t at)
{
    return has(s, at, 4) ? (unsigned long)s.p[at] | (unsigned long)s.p[at + 1] << 8 |
                           (unsigned long)s.p[at + 2] << 16 | (unsigned long)s.p[at + 3] << 24 : 0;
}

static long rds32(span s, size_t at)
{
    unsigned long v = rd32(s, at);
    return v & 0x80000000UL ? -(long)(~v & 0x7FFFFFFFUL) - 1 : (long)v;
}

static span sub(span s, size_t at, size_t n)
{
    span r = { NULL, 0 };
    if (has(s, at, 0)) {
        r.p = s.p + at;
        r.n = n <= s.n - at ? n : s.n - at;
    }
    return r;
}

typedef struct {
    unsigned ver, inst, type;
    span body;
    size_t at;                   /* where its header is, in the span it was read from */
} rec;

/* The record at offset at of s, whole; 0 when it is cut short. */
static int rec_at(span s, size_t at, rec *r)
{
    unsigned vi;
    unsigned long len;
    if (!has(s, at, 8))
        return 0;
    vi = rd16(s, at);
    r->ver = vi & 15;
    r->inst = vi >> 4;
    r->type = rd16(s, at + 2);
    len = rd32(s, at + 4);
    if (!has(s, at + 8, len))
        return 0;
    r->body = sub(s, at + 8, len);
    r->at = at;
    return 1;
}

/* The next record of s from *at, stepping past it. */
static int next_rec(span s, size_t *at, rec *r)
{
    if (!rec_at(s, *at, r))
        return 0;
    *at += 8 + r->body.n;
    return 1;
}

/* The first record of s of this type (and instance, when inst >= 0). */
static int find_rec(span s, unsigned type, int inst, rec *r)
{
    size_t at = 0;
    while (next_rec(s, &at, r))
        if (r->type == type && (inst < 0 || r->inst == (unsigned)inst))
            return 1;
    return 0;
}

/* ---- The reader ---- */

typedef struct {
    unsigned long id, offset;
} persist_entry;

typedef struct {
    unsigned long slide_id;      /* the master's own id, as slides name it */
    unsigned long persist;
    int master;                  /* into deck->masters */
    unsigned long scheme[8];     /* 0xRRGGBB */
} master_ref;

typedef struct {
    unsigned long slide_id, persist;
} notes_ref;

typedef struct {
    owf_deck *deck;
    owf_report *report;
    span doc;                    /* the PowerPoint Document stream */
    span pics;                   /* the Pictures stream (may be empty) */
    persist_entry *persist;
    size_t npersist;
    int *fonts;                  /* the file's font index as res->fonts' */
    int nfonts;
    int *blips;                  /* the picture store's pictures (1-based in files) as res->images' */
    int nblips;
    span dgg_opt;                /* the drawing group's default properties */
    unsigned ndgg_opt;
    span doc_styles[9];          /* the document's TextMasterStyleAtoms by text type */
    master_ref *masters;
    int nmasters;
    notes_ref *notes;
    int nnotes;
    int failed;                  /* memory ran out */
    unsigned said;               /* SAID_...: notes made once */
} ppt;

#define SAID_FILL 1
#define SAID_SHAPE 2
#define SAID_OLE 4
#define SAID_PICTURE 8
#define SAID_TRANSITION 16
#define SAID_ANIMATION 32
#define SAID_OBJS 64
#define SAID_DEPTH 128
#define SAID_TEXT 256

static void say(ppt *x, unsigned what, owf_note_level level, const char *text)
{
    if (x->said & what)
        return;
    x->said |= what;
    owf_report_add(x->report, level, "%s", text);
}

/* The persist object's record, by id. */
static int persist_rec(const ppt *x, unsigned long id, rec *r)
{
    size_t i;
    if (!id)
        return 0;
    for (i = 0; i < x->npersist; i++)
        if (x->persist[i].id == id)
            return rec_at(x->doc, x->persist[i].offset, r);
    return 0;
}

static int add_persist(ppt *x, unsigned long id, unsigned long offset, size_t *cap)
{
    size_t i;
    for (i = 0; i < x->npersist; i++)
        if (x->persist[i].id == id)
            return 1;                /* a newer save's */
    if (x->npersist == *cap) {
        size_t n = *cap ? *cap * 2 : 64;
        persist_entry *p = realloc(x->persist, n * sizeof *p);
        if (!p)
            return 0;
        x->persist = p;
        *cap = n;
    }
    x->persist[x->npersist].id = id;
    x->persist[x->npersist].offset = offset;
    x->npersist++;
    return 1;
}

/* The last UserEditAtom in the stream, for a file whose Current User stream is missing or wrong. */
static long last_user_edit(span doc)
{
    size_t at = 0;
    long found = -1;
    rec r;
    while (next_rec(doc, &at, &r))
        if (r.type == RT_USER_EDIT)
            found = (long)r.at;
    return found;
}

/* The persist directories from the newest edit back. *doc_ref gets the document's persist id.
 * OWF_OK, OWF_ERR_CORRUPT, OWF_ERR_UNSUPPORTED (encrypted) or OWF_ERR_MEMORY. */
static int read_persist(ppt *x, span current, unsigned long *doc_ref)
{
    rec r;
    long at = -1;
    size_t cap = 0;
    int edits = 0;

    if (rec_at(current, 0, &r) && r.type == RT_CURRENT_USER && r.body.n >= 12) {
        unsigned long token = rd32(r.body, 4);
        if (token == 0xF3D1C4DFUL) {
            owf_report_add(x->report, OWF_NOTE_LOST, "This presentation is protected by a password; it cannot be read");
            return OWF_ERR_UNSUPPORTED;
        }
        at = (long)rd32(r.body, 8);
    }
    if (at < 0 || !rec_at(x->doc, (size_t)at, &r) || r.type != RT_USER_EDIT)
        at = last_user_edit(x->doc);
    *doc_ref = 0;
    while (at >= 0 && edits++ < MAX_EDITS) {
        rec dir;
        unsigned long prev;
        size_t k;
        if (!rec_at(x->doc, (size_t)at, &r) || r.type != RT_USER_EDIT || r.body.n < 28)
            break;
        if (edits == 1) {
            *doc_ref = rd32(r.body, 16);
            if (r.body.n >= 32 && rd32(r.body, 28)) {
                owf_report_add(x->report, OWF_NOTE_LOST, "This presentation is protected by a password; it cannot be read");
                return OWF_ERR_UNSUPPORTED;
            }
        }
        if (rec_at(x->doc, rd32(r.body, 12), &dir) && dir.type == RT_PERSIST_DIRECTORY) {
            for (k = 0; has(dir.body, k, 4);) {
                unsigned long head = rd32(dir.body, k), id = head & 0xFFFFFUL, n = head >> 20, j;
                k += 4;
                for (j = 0; j < n && has(dir.body, k, 4); j++, k += 4)
                    if (!add_persist(x, id + j, rd32(dir.body, k), &cap))
                        return OWF_ERR_MEMORY;
            }
        }
        prev = rd32(r.body, 8);
        if (!prev || prev >= (unsigned long)at)
            break;                   /* older edits come first in the stream */
        at = (long)prev;
    }
    return *doc_ref && x->npersist ? OWF_OK : OWF_ERR_CORRUPT;
}

/* ---- Fonts and colours ---- */

static void read_fonts(ppt *x, span environment)
{
    rec coll, f;
    size_t at = 0;
    if (!find_rec(environment, RT_FONT_COLLECTION, -1, &coll))
        return;
    while (next_rec(coll.body, &at, &f) && !x->failed) {
        char name[100];
        size_t k = 0, i;
        int index;
        if (f.type != RT_FONT_ENTITY || f.inst >= MAX_FONTS)
            continue;
        for (i = 0; i < 32; i++) {
            unsigned c = rd16(f.body, 2 * i);
            if (!c)
                break;
            if (c < 0x80) {
                if (k < sizeof name - 1)
                    name[k++] = (char)c;
            } else if (c < 0x800) {
                if (k < sizeof name - 2) {
                    name[k++] = (char)(0xC0 | c >> 6);
                    name[k++] = (char)(0x80 | (c & 0x3F));
                }
            } else if (c < 0xD800 || c > 0xDFFF) {
                if (k < sizeof name - 3) {
                    name[k++] = (char)(0xE0 | c >> 12);
                    name[k++] = (char)(0x80 | (c >> 6 & 0x3F));
                    name[k++] = (char)(0x80 | (c & 0x3F));
                }
            }
        }
        name[k] = 0;
        if (!k)
            continue;
        if ((int)f.inst >= x->nfonts) {
            int n = (int)f.inst + 1, j;
            int *p = realloc(x->fonts, (size_t)n * sizeof *p);
            if (!p) {
                x->failed = 1;
                return;
            }
            for (j = x->nfonts; j < n; j++)
                p[j] = -1;
            x->fonts = p;
            x->nfonts = n;
        }
        index = owf_doc_font(x->deck->res, name, OWF_FONT_ANY);
        if (index < 0)
            x->failed = 1;
        x->fonts[f.inst] = index;
    }
}

static int font_of(const ppt *x, unsigned ref)
{
    return (int)ref < x->nfonts ? x->fonts[ref] : -1;
}

/* A colour scheme (SlideSchemeColorSchemeAtom): eight ColorStructs, red, green, blue, unused. */
static int read_scheme(span container, unsigned long scheme[8])
{
    rec r;
    int i;
    if (!find_rec(container, RT_COLOR_SCHEME_ATOM, 1, &r) || r.body.n < 32)
        return 0;
    for (i = 0; i < 8; i++)
        scheme[i] = (unsigned long)rd8(r.body, 4 * i) << 16 | (unsigned long)rd8(r.body, 4 * i + 1) << 8 |
                    rd8(r.body, 4 * i + 2);
    return 1;
}

/* PowerPoint's default scheme: white background, black text, grey shadow, black title, the rest. */
static const unsigned long default_scheme[8] = {
    0xFFFFFF, 0x000000, 0x808080, 0x000000, 0x00CC99, 0x3333CC, 0xCCCCFF, 0xB2B2B2
};

/* An OfficeArtCOLORREF (red low, then green, blue, flags) as 0xRRGGBB; -1 when it names no colour. */
static long colourref(unsigned long v, const unsigned long *scheme)
{
    unsigned flags = (unsigned)(v >> 24);
    if (flags == 0xFF)
        return -1;
    if (flags & 0x08)                /* fSchemeIndex: red is the slide scheme's colour */
        return (v & 0xFF) < 8 ? (long)scheme[v & 0xFF] : -1;
    if (flags & 0x10)                /* fSysIndex: the shape's own colours, which are not known here */
        return -1;
    return (long)((v & 0xFF) << 16 | (v & 0xFF00) | (v >> 16 & 0xFF));
}

/* A ColorIndexStruct (red, green, blue, index) as 0xRRGGBB; -1 when undefined. */
static long colour_index(unsigned long v, const unsigned long *scheme)
{
    unsigned index = (unsigned)(v >> 24);
    if (index == 0xFE)
        return (long)((v & 0xFF) << 16 | (v & 0xFF00) | (v >> 16 & 0xFF));
    return index < 8 ? (long)scheme[index] : -1;
}

/* ---- Shape properties ---- */

typedef struct {
    span fopt[2];                /* the primary and tertiary property tables */
    unsigned n[2];
    span dflt;                   /* the drawing group's defaults */
    unsigned ndflt;
} props;

/* A property's value from one table (u16 id with fBid 0x4000 and fComplex 0x8000, u32 value; complex data after
 * all of them, in table order). complex gets its data when it has some. */
static int prop_in(span t, unsigned n, unsigned id, unsigned long *value, span *complex)
{
    unsigned i;
    size_t data = (size_t)n * 6;
    for (i = 0; i < n && has(t, (size_t)i * 6, 6); i++) {
        unsigned opid = rd16(t, (size_t)i * 6);
        unsigned long v = rd32(t, (size_t)i * 6 + 2);
        if ((opid & 0x3FFF) == id) {
            *value = v;
            if (complex) {
                complex->p = NULL;
                complex->n = 0;
                if (opid & 0x8000)
                    *complex = sub(t, data, v);
            }
            return 1;
        }
        if (opid & 0x8000)
            data += v;
    }
    return 0;
}

static int prop(const props *p, unsigned id, unsigned long *value, span *complex)
{
    return prop_in(p->fopt[0], p->n[0], id, value, complex) || prop_in(p->fopt[1], p->n[1], id, value, complex) ||
           prop_in(p->dflt, p->ndflt, id, value, complex);
}

static long prop_or(const props *p, unsigned id, long dflt)
{
    unsigned long v;
    return prop(p, id, &v, NULL) ? (long)v : dflt;
}

/* A boolean of a boolean-properties set: bit, with its fUse bit 16 above; dflt when no table sets it. */
static int prop_bool(const props *p, unsigned id, unsigned long bit, int dflt)
{
    const span *t[3];
    unsigned n[3], i;
    unsigned long v;
    t[0] = &p->fopt[0];
    n[0] = p->n[0];
    t[1] = &p->fopt[1];
    n[1] = p->n[1];
    t[2] = &p->dflt;
    n[2] = p->ndflt;
    for (i = 0; i < 3; i++)
        if (prop_in(*t[i], n[i], id, &v, NULL) && (v & bit << 16))
            return (v & bit) != 0;
    return dflt;
}

/* UTF-16 text (a name) into a copy in UTF-8, or NULL. */
static char *utf16_copy(span s)
{
    owf_buf b;
    size_t i;
    unsigned char *out = NULL;
    size_t len;
    owf_buf_init(&b);
    for (i = 0; has(s, i, 2); i += 2) {
        unsigned long c = rd16(s, i);
        if (!c)
            break;
        if (c >= 0xD800 && c < 0xDC00 && has(s, i + 2, 2)) {
            unsigned long lo = rd16(s, i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (c >= 0x20 && (c < 0xD800 || c > 0xDFFF))
            owf_buf_put_utf8(&b, c);
    }
    if (!b.length || owf_buf_take(&b, &out, &len) != OWF_OK) {
        owf_buf_free(&b);
        return NULL;
    }
    return (char *)out;
}

/* ---- Text runs ---- */

typedef struct {
    unsigned long count;
    unsigned level;
    unsigned long mask;
    unsigned bullet_flags, bullet_char, bullet_font, align;
    int bullet_size, line_spacing, space_before, space_after, left_margin, indent;
    unsigned long bullet_colour;
} pf_run;

typedef struct {
    unsigned long count;
    unsigned long mask;
    unsigned style, font;
    int size, position;
    unsigned long colour;
} cf_run;

#define PF_HAS_BULLET 0x1UL
#define PF_BULLET_HAS_FONT 0x2UL
#define PF_BULLET_HAS_COLOUR 0x4UL
#define PF_BULLET_HAS_SIZE 0x8UL
#define PF_BULLET_FONT 0x10UL
#define PF_BULLET_COLOUR 0x20UL
#define PF_BULLET_SIZE 0x40UL
#define PF_BULLET_CHAR 0x80UL
#define PF_LEFT_MARGIN 0x100UL
#define PF_INDENT 0x400UL
#define PF_ALIGN 0x800UL
#define PF_LINE_SPACING 0x1000UL
#define PF_SPACE_BEFORE 0x2000UL
#define PF_SPACE_AFTER 0x4000UL
#define PF_DEFAULT_TAB 0x8000UL
#define PF_FONT_ALIGN 0x10000UL
#define PF_WRAP (0x20000UL | 0x40000UL | 0x80000UL)
#define PF_TAB_STOPS 0x100000UL
#define PF_TEXT_DIRECTION 0x200000UL
#define PF_BULLET_BLIP 0x800000UL
#define PF_BULLET_SCHEME 0x1000000UL

#define CF_BOLD 0x1UL
#define CF_ITALIC 0x2UL
#define CF_UNDERLINE 0x4UL
#define CF_STYLE_BITS 0xFFFFUL         /* any of them: the fontStyle field is there */
#define CF_TYPEFACE 0x10000UL
#define CF_SIZE 0x20000UL
#define CF_COLOUR 0x40000UL
#define CF_POSITION 0x80000UL
#define CF_PP10 0x100000UL
#define CF_OLD_EA 0x200000UL
#define CF_ANSI 0x400000UL
#define CF_SYMBOL 0x800000UL

/* A TextPFException at *at ([MS-PPT] 2.9.20); 0 when it runs off the end. */
static int read_pf(span s, size_t *at, pf_run *r)
{
    size_t k = *at;
    unsigned long m;
    if (!has(s, k, 4))
        return 0;
    m = r->mask = rd32(s, k);
    k += 4;
    if (m & (PF_HAS_BULLET | PF_BULLET_HAS_FONT | PF_BULLET_HAS_COLOUR | PF_BULLET_HAS_SIZE)) {
        r->bullet_flags = rd16(s, k);
        k += 2;
    }
    if (m & PF_BULLET_CHAR) { r->bullet_char = rd16(s, k); k += 2; }
    if (m & PF_BULLET_FONT) { r->bullet_font = rd16(s, k); k += 2; }
    if (m & PF_BULLET_SIZE) { r->bullet_size = rds16(s, k); k += 2; }
    if (m & PF_BULLET_COLOUR) { r->bullet_colour = rd32(s, k); k += 4; }
    if (m & PF_ALIGN) { r->align = rd16(s, k); k += 2; }
    if (m & PF_LINE_SPACING) { r->line_spacing = rds16(s, k); k += 2; }
    if (m & PF_SPACE_BEFORE) { r->space_before = rds16(s, k); k += 2; }
    if (m & PF_SPACE_AFTER) { r->space_after = rds16(s, k); k += 2; }
    if (m & PF_LEFT_MARGIN) { r->left_margin = rds16(s, k); k += 2; }
    if (m & PF_INDENT) { r->indent = rds16(s, k); k += 2; }
    if (m & PF_DEFAULT_TAB)
        k += 2;
    if (m & PF_TAB_STOPS)
        k += 2 + 4 * (size_t)rd16(s, k);
    if (m & PF_FONT_ALIGN)
        k += 2;
    if (m & PF_WRAP)
        k += 2;
    if (m & PF_TEXT_DIRECTION)
        k += 2;
    if (m & PF_BULLET_BLIP)
        k += 2;
    if (m & PF_BULLET_SCHEME)
        k += 4;
    if (k > s.n)
        return 0;
    *at = k;
    return 1;
}

/* A TextCFException ([MS-PPT] 2.9.14). */
static int read_cf(span s, size_t *at, cf_run *r)
{
    size_t k = *at;
    unsigned long m;
    if (!has(s, k, 4))
        return 0;
    m = r->mask = rd32(s, k);
    k += 4;
    if (m & CF_STYLE_BITS) { r->style = rd16(s, k); k += 2; }
    if (m & CF_TYPEFACE) { r->font = rd16(s, k); k += 2; }
    if (m & CF_OLD_EA)
        k += 2;
    if (m & CF_ANSI)
        k += 2;
    if (m & CF_SYMBOL)
        k += 2;
    if (m & CF_SIZE) { r->size = rds16(s, k); k += 2; }
    if (m & CF_COLOUR) { r->colour = rd32(s, k); k += 4; }
    if (m & CF_POSITION) { r->position = rds16(s, k); k += 2; }
    if (m & CF_PP10)
        k += 4;
    if (k > s.n)
        return 0;
    *at = k;
    return 1;
}

/* ---- Text levels: the masters' styles ---- */

/* Master units (576 an inch) as twips. */
static int mu(long v)
{
    long long t = (long long)v * 5 / 2;
    return t > MAX_COORD ? MAX_COORD : t < -MAX_COORD ? -MAX_COORD : (int)t;
}

static int spacing(int v, int size)
{
    if (v < 0)
        return mu(-(long)v);
    /* a share of the line: about 1.2 times the text's size */
    return (int)((long long)v * (size ? size : 18 * OWF_TWIPS_PER_POINT) * 12 / 1000);
}

static unsigned long bullet_unicode(unsigned c)
{
    if (c >= 0xF000 && c <= 0xF0FF)
        return owf_symbol_char(c);
    return c;
}

static void apply_pf(owf_textlevel *l, const pf_run *pf, const ppt *x, const unsigned long *scheme)
{
    if (pf->mask & PF_ALIGN)
        l->para.align = pf->align == 1 ? OWF_ALIGN_CENTRE : pf->align == 2 ? OWF_ALIGN_RIGHT
                      : pf->align == 3 || pf->align == 4 ? OWF_ALIGN_JUSTIFY : OWF_ALIGN_LEFT;
    if (pf->mask & PF_LEFT_MARGIN)
        l->para.indent_left = mu(pf->left_margin);
    if (pf->mask & PF_INDENT)
        l->para.indent_first = mu(pf->indent) - l->para.indent_left;
    if (pf->mask & PF_LINE_SPACING) {
        if (pf->line_spacing < 0)
            l->para.line_spacing = OWF_LINE_EXACT(mu(-(long)pf->line_spacing));
        else if (pf->line_spacing)
            l->para.line_spacing = pf->line_spacing;
    }
    if (pf->mask & PF_SPACE_BEFORE)
        l->para.space_before = spacing(pf->space_before, l->chars.size);
    if (pf->mask & PF_SPACE_AFTER)
        l->para.space_after = spacing(pf->space_after, l->chars.size);
    if ((pf->mask & PF_HAS_BULLET) && !(pf->bullet_flags & 1))
        l->bullet = 0;
    else if ((pf->mask & PF_HAS_BULLET) && !l->bullet)
        l->bullet = 0x2022;
    if ((pf->mask & PF_BULLET_CHAR) && l->bullet && pf->bullet_char)
        l->bullet = bullet_unicode(pf->bullet_char);
    if ((pf->mask & PF_BULLET_FONT) && (pf->bullet_flags & 2))
        l->bullet_font = font_of(x, pf->bullet_font);
    if ((pf->mask & PF_BULLET_SIZE) && (pf->bullet_flags & 8) && pf->bullet_size > 0 && pf->bullet_size <= 400)
        l->bullet_size = pf->bullet_size;
    if ((pf->mask & PF_BULLET_COLOUR) && (pf->bullet_flags & 4)) {
        long c = colour_index(pf->bullet_colour, scheme);
        if (c >= 0)
            l->bullet_colour = OWF_SHADE(c);
    }
}

static void apply_cf_flags(unsigned *flags, const cf_run *cf)
{
    static const struct { unsigned long bit; unsigned flag; } map[] = {
        { CF_BOLD, OWF_BOLD }, { CF_ITALIC, OWF_ITALIC }, { CF_UNDERLINE, OWF_UNDERLINE }
    };
    size_t i;
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (cf->mask & map[i].bit) {
            if (cf->style & map[i].bit)
                *flags |= map[i].flag;
            else
                *flags &= ~map[i].flag;
        }
}

static void apply_cf(owf_textlevel *l, const cf_run *cf, const ppt *x, const unsigned long *scheme)
{
    apply_cf_flags(&l->chars.flags, cf);
    if ((cf->mask & CF_TYPEFACE) && font_of(x, cf->font) >= 0)
        l->chars.font = font_of(x, cf->font);
    if ((cf->mask & CF_SIZE) && cf->size >= 1 && cf->size <= 4000)
        l->chars.size = cf->size * OWF_TWIPS_PER_POINT;
    if (cf->mask & CF_COLOUR) {
        long c = colour_index(cf->colour, scheme);
        if (c >= 0)
            l->chars.colour = (unsigned long)c;
    }
}

/* A TextMasterStyleAtom's levels into levels (n of them: 1, or the body's 9, whose last four copy the fifth). */
static void read_master_style(const ppt *x, span atom, unsigned type, owf_textlevel *levels, int n,
                              const unsigned long *scheme)
{
    unsigned count = rd16(atom, 0), i;
    size_t at = 2;
    if (count > 5)
        count = 5;
    for (i = 0; i < count; i++) {
        pf_run pf;
        cf_run cf;
        unsigned level = i;
        memset(&pf, 0, sizeof pf);
        memset(&cf, 0, sizeof cf);
        if (type >= TX_CENTER_BODY) {
            level = rd16(atom, at);
            at += 2;
        }
        if (!read_pf(atom, &at, &pf) || !read_cf(atom, &at, &cf))
            return;
        if ((int)level < n) {
            apply_cf(&levels[level], &cf, x, scheme);
            apply_pf(&levels[level], &pf, x, scheme);
        }
    }
    for (i = 5; (int)i < n; i++) {
        owf_textlevel *l = &levels[i];
        int indent = l->para.indent_left;
        *l = levels[4];
        l->para.indent_left = indent;
    }
}

static void master_styles(const ppt *x, owf_master *m, span container, const unsigned long *scheme)
{
    int pass;
    for (pass = 0; pass < 2; pass++) {
        unsigned type;
        for (type = 0; type < 9; type++) {
            rec r;
            span atom;
            owf_textlevel *levels;
            int n = 1;
            if (pass == 0) {
                atom = x->doc_styles[type];
            } else if (find_rec(container, RT_TEXT_MASTER_STYLE, (int)type, &r)) {
                atom = r.body;
            } else {
                continue;
            }
            if (!atom.p)
                continue;
            switch (type) {
            case TX_TITLE: levels = &m->title; break;
            case TX_BODY: levels = m->body; n = OWF_TEXT_LEVELS; break;
            case TX_NOTES: levels = &m->notes; break;
            case TX_OTHER: levels = &m->other; break;
            case TX_CENTER_BODY: levels = &m->subtitle; break;
            default: continue;
            }
            read_master_style(x, atom, type, levels, n, scheme);
        }
    }
}

/* ---- A shape's text ---- */

typedef struct {
    ppt *x;
    owf_slide *slide;            /* one of these is set */
    owf_master *master;
    int master_index;            /* the deck master the slide or notes takes its looks from */
    const unsigned long *scheme;
    span outline;                /* the slide's outline text in SlideListWithText */
    int notes;                   /* reading a notes page: only its notes text is kept */
} dctx;

static owf_obj *ctx_obj(dctx *c, int index)
{
    if (c->slide)
        return index >= 0 && index < c->slide->nobjs ? &c->slide->objs[index] : NULL;
    return index >= 0 && index < c->master->nobjs ? &c->master->objs[index] : NULL;
}

static int ctx_count(const dctx *c)
{
    return c->slide ? c->slide->nobjs : c->master->nobjs;
}

/* The text records for an OutlineTextRefAtom's index: from its TextHeaderAtom up to the next one. */
static span outline_text(span outline, long index)
{
    size_t at = 0, start = 0;
    long k = -1;
    rec r;
    span none = { NULL, 0 };
    if (index < 0)
        return none;
    while (next_rec(outline, &at, &r)) {
        if (r.type == RT_TEXT_HEADER) {
            if (k == index)
                return sub(outline, start, r.at - start);
            if (++k == index)
                start = r.at;
        }
    }
    return k == index ? sub(outline, start, outline.n - start) : none;
}

typedef struct {
    unsigned long at;
    owf_field field;
} field_mark;

static void add_text_run(dctx *c, owf_para *p, const owf_charfmt *cf, const unsigned short *u, size_t n)
{
    owf_buf b;
    size_t i;
    owf_buf_init(&b);
    for (i = 0; i < n; i++) {
        unsigned long ch = u[i];
        if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000) {
            ch = 0x10000 + ((ch - 0xD800) << 10) + (u[i + 1] - 0xDC00);
            i++;
        } else if (ch >= 0xD800 && ch <= 0xDFFF) {
            continue;
        }
        if (ch >= 0xF000 && ch <= 0xF0FF)          /* a symbol font's character in Word's U+F0xx form */
            ch = owf_symbol_char((unsigned)ch);
        owf_buf_put_utf8(&b, ch);
    }
    if (b.failed || (b.length && owf_para_add_text(p, cf, (const char *)b.data, b.length) < 0))
        c->x->failed = 1;
    owf_buf_free(&b);
}

/* Whether a placeholder's text is body text (in the master's body levels). */
static int body_ph(owf_placeholder ph)
{
    return ph == OWF_PH_BODY || ph == OWF_PH_OBJECT || ph == OWF_PH_TEXT;
}

/* The text in recs (a ClientTextbox's records, or the slide's outline text) into obj. */
static void read_text(dctx *c, int index, span recs)
{
    ppt *x = c->x;
    rec r;
    size_t at = 0, n = 0, i, start;
    unsigned short *u = NULL;
    pf_run *pfs = NULL;
    cf_run *cfs = NULL;
    size_t npf = 0, ncf = 0, pi = 0, ci = 0;
    unsigned long pf_end = 0, cf_end = 0;
    field_mark marks[16];
    int nmarks = 0;
    span style = { NULL, 0 };
    const owf_master *m;
    owf_obj *obj;

    /* the text, its runs and its fields */
    while (next_rec(recs, &at, &r)) {
        if ((r.type == RT_TEXT_CHARS || r.type == RT_TEXT_BYTES) && !u) {
            size_t len = r.type == RT_TEXT_CHARS ? r.body.n / 2 : r.body.n;
            if (len > MAX_TEXT) {
                len = MAX_TEXT;
                say(x, SAID_TEXT, OWF_NOTE_LOST, "Very long text in a shape was cut short");
            }
            u = malloc((len ? len : 1) * sizeof *u);
            if (!u) {
                x->failed = 1;
                return;
            }
            for (i = 0; i < len; i++)
                u[i] = (unsigned short)(r.type == RT_TEXT_CHARS ? rd16(r.body, 2 * i) : rd8(r.body, i));
            n = len;
        } else if (r.type == RT_STYLE_TEXT_PROP && !style.p) {
            style = r.body;
        } else if ((r.type == RT_SLIDE_NUMBER_MC || r.type == RT_DATETIME_MC || r.type == RT_GENERIC_DATE_MC ||
                    r.type == RT_RTF_DATETIME_MC) && nmarks < (int)(sizeof marks / sizeof marks[0])) {
            marks[nmarks].at = rd32(r.body, 0);
            marks[nmarks].field = r.type == RT_SLIDE_NUMBER_MC ? OWF_FIELD_PAGE : OWF_FIELD_DATE;
            nmarks++;
        }
    }
    if (!u)
        return;
    /* its runs: the paragraphs', then the characters', each covering count characters */
    if (style.p) {
        size_t k = 0;
        unsigned long total = 0;
        pfs = malloc(MAX_RUNS * sizeof *pfs);
        cfs = malloc(MAX_RUNS * sizeof *cfs);
        if (!pfs || !cfs) {
            free(pfs);
            free(cfs);
            free(u);
            x->failed = 1;
            return;
        }
        while (total < n + 1 && npf < MAX_RUNS && has(style, k, 6)) {
            pf_run *p = &pfs[npf];
            memset(p, 0, sizeof *p);
            p->count = rd32(style, k);
            p->level = rd16(style, k + 4);
            k += 6;
            if (!p->count || !read_pf(style, &k, p))
                break;
            total += p->count;
            npf++;
        }
        total = 0;
        while (total < n + 1 && ncf < MAX_RUNS && has(style, k, 4)) {
            cf_run *f = &cfs[ncf];
            memset(f, 0, sizeof *f);
            f->count = rd32(style, k);
            k += 4;
            if (!f->count || !read_cf(style, &k, f))
                break;
            total += f->count;
            ncf++;
        }
    }
    if (npf)
        pf_end = pfs[0].count;
    if (ncf)
        cf_end = cfs[0].count;

    m = c->master_index >= 0 && c->master_index < x->deck->nmasters ? &x->deck->masters[c->master_index]
                                                                     : c->master;
    obj = ctx_obj(c, index);
    if (c->notes)
        obj = &c->slide->notes;
    if (!obj) {
        free(u);
        free(pfs);
        free(cfs);
        return;
    }
    /* drop text the frame had (a notes page with two notes shapes keeps the last) */
    owf_deck_free_story(&obj->text);
    free(obj->levels);
    obj->levels = NULL;
    obj->caplevels = 0;

    for (start = 0; start <= n && !x->failed;) {
        size_t end = start, run;
        const pf_run *pf;
        int level = 0, para_index;
        owf_para *p;
        const owf_textlevel *lv;
        while (end < n && u[end] != 0x0D)
            end++;
        while (npf && pi + 1 < npf && start >= pf_end)
            pf_end += pfs[++pi].count;
        pf = npf ? &pfs[pi] : NULL;
        if (body_ph(obj->placeholder)) {
            level = (pf ? (int)(pf->level > 4 ? 4 : pf->level) : 0) + 1;
            if (pf && (pf->mask & PF_HAS_BULLET) && !(pf->bullet_flags & 1) && pf->level == 0)
                level = 0;
        }
        p = owf_deck_add_para(x->deck, m, obj, level);
        if (!p) {
            x->failed = 1;
            break;
        }
        para_index = obj->text.nparas - 1;
        lv = owf_deck_para_level(x->deck, m, obj, para_index);
        if (pf) {
            owf_textlevel own;
            owf_textlevel_init(&own);
            own.para = p->fmt;
            own.chars.size = lv ? lv->chars.size : 0;
            apply_pf(&own, pf, x, c->scheme);
            p->fmt = own.para;
        }
        /* the characters, a run each time their formatting changes or a tab, a line break or a field comes */
        for (run = start; run < end;) {
            size_t stop = run;
            owf_charfmt cf;
            const cf_run *f;
            int k, field = -1;
            while (ncf && ci + 1 < ncf && run >= cf_end)
                cf_end += cfs[++ci].count;
            f = ncf ? &cfs[ci] : NULL;
            cf.flags = lv ? lv->chars.flags : 0;
            cf.font = -1;
            cf.size = 0;
            cf.colour = OWF_COLOUR_AUTO;
            if (f) {
                apply_cf_flags(&cf.flags, f);
                if ((f->mask & CF_TYPEFACE) && font_of(x, f->font) >= 0)
                    cf.font = font_of(x, f->font);
                if ((f->mask & CF_SIZE) && f->size >= 1 && f->size <= 4000)
                    cf.size = f->size * OWF_TWIPS_PER_POINT;
                if (f->mask & CF_COLOUR) {
                    long col = colour_index(f->colour, c->scheme);
                    if (col >= 0)
                        cf.colour = (unsigned long)col;
                }
                if ((f->mask & CF_POSITION) && f->position)
                    cf.flags |= f->position > 0 ? OWF_SUPER : OWF_SUB;
            }
            for (k = 0; k < nmarks; k++)
                if (marks[k].at == run)
                    field = k;
            if (field >= 0) {
                if (owf_para_add_special(p, &cf, OWF_RUN_FIELD, marks[field].field) < 0)
                    x->failed = 1;
                run++;
                continue;
            }
            if (u[run] == 0x0B || u[run] == 0x09) {
                if (owf_para_add_special(p, &cf, u[run] == 0x09 ? OWF_RUN_TAB : OWF_RUN_LINEBREAK, OWF_FIELD_PAGE) < 0)
                    x->failed = 1;
                run++;
                continue;
            }
            /* up to where its character run ends, a tab, a line break or a field */
            while (stop < end && (!ncf || stop < cf_end || ci + 1 >= ncf) && u[stop] != 0x0B && u[stop] != 0x09) {
                int marked = 0;
                for (k = 0; k < nmarks; k++)
                    if (marks[k].at == stop)
                        marked = 1;
                if (marked)
                    break;
                stop++;
            }
            if (stop == run)
                stop++;              /* the walk always moves on */
            {
                /* leave out control characters */
                size_t a = run, b;
                while (a < stop) {
                    b = a;
                    while (b < stop && u[b] >= 0x20)
                        b++;
                    if (b > a)
                        add_text_run(c, p, &cf, u + a, b - a);
                    a = b + (b < stop);
                }
            }
            run = stop;
        }
        start = end + 1;
        if (end >= n)
            break;
    }
    if (obj->text.nparas == 0 && !owf_story_add(&obj->text, NULL))
        x->failed = 1;
    free(u);
    free(pfs);
    free(cfs);
}

/* ---- Shapes ---- */

typedef struct {
    long gx, gy, gw, gh;         /* the group's own units */
    long px, py, pw, ph;         /* where they go, twips */
} map;

static int map_x(const map *m, long v)
{
    long long t = m->px + (m->gw ? ((long long)v - m->gx) * m->pw / m->gw : 0);
    return t > MAX_COORD ? MAX_COORD : t < -MAX_COORD ? -MAX_COORD : (int)t;
}

static int map_y(const map *m, long v)
{
    long long t = m->py + (m->gh ? ((long long)v - m->gy) * m->ph / m->gh : 0);
    return t > MAX_COORD ? MAX_COORD : t < -MAX_COORD ? -MAX_COORD : (int)t;
}

static owf_placeholder placeholder_kind(unsigned id)
{
    switch (id) {
    case 0x01: case 0x03: case 0x0D: case 0x0F: case 0x11: return OWF_PH_TITLE;
    case 0x02: case 0x0E: case 0x12: return OWF_PH_BODY;
    case 0x04: case 0x10: return OWF_PH_SUBTITLE;
    case 0x05: case 0x0B: return OWF_PH_SLIDE_IMAGE;
    case 0x06: case 0x0C: return OWF_PH_NOTES;
    case 0x07: return OWF_PH_DATE;
    case 0x08: return OWF_PH_NUMBER;
    case 0x09: return OWF_PH_FOOTER;
    case 0x0A: return OWF_PH_HEADER;
    case 0x13: case 0x17: case 0x18: case 0x19: return OWF_PH_OBJECT;
    case 0x14: return OWF_PH_CHART;
    case 0x15: return OWF_PH_TABLE;
    case 0x16: case 0x1A: return OWF_PH_PICTURE;
    default: return OWF_PH_NONE;
    }
}

static owf_obj *new_obj(dctx *c, owf_obj_kind kind, int parent, int *index)
{
    owf_obj *o;
    if (ctx_count(c) >= OWF_DECK_MAX_OBJS) {
        say(c->x, SAID_OBJS, OWF_NOTE_LOST, "Objects past the most one slide holds were left out");
        return NULL;
    }
    o = c->slide ? owf_slide_add(c->slide, kind) : owf_master_add(c->master, kind);
    if (!o) {
        c->x->failed = 1;
        return NULL;
    }
    o->parent = parent;
    *index = ctx_count(c) - 1;
    return o;
}

/* The picture store's picture n (1-based), as res->images'; -1. */
static int blip_image(const ppt *x, unsigned long n)
{
    return n >= 1 && n <= (unsigned long)x->nblips ? x->blips[n - 1] : -1;
}

static void read_look(dctx *c, owf_obj *o, const props *p, int line_only)
{
    long fill_type = prop_or(p, PR_FILL_TYPE, 0);
    if (!line_only && prop_bool(p, PR_FILL_BOOLS, 0x10, 1)) {
        long col = colourref((unsigned long)prop_or(p, PR_FILL_COLOUR, 0xFFFFFF), c->scheme);
        if (col >= 0)
            o->look.fill = OWF_SHADE(col);
        if (fill_type != 0)
            say(c->x, SAID_FILL, OWF_NOTE_APPROX, "Gradient, pattern and picture fills are shown in one colour");
    }
    if (prop_bool(p, PR_LINE_BOOLS, 0x08, 1)) {
        long col = colourref((unsigned long)prop_or(p, PR_LINE_COLOUR, 0), c->scheme);
        long w = prop_or(p, PR_LINE_WIDTH, 9525);
        if (col >= 0)
            o->look.line = OWF_SHADE(col);
        o->look.line_width = (int)(w > 0 && w <= 20116800L ? w / 635 : 15);
        if (prop_or(p, PR_LINE_START_ARROW, 0))
            o->look.arrows |= OWF_ARROW_START;
        if (prop_or(p, PR_LINE_END_ARROW, 0))
            o->look.arrows |= OWF_ARROW_END;
    }
}

static void read_text_box(owf_obj *o, const props *p)
{
    static const unsigned ids[4] = { PR_TEXT_LEFT, PR_TEXT_TOP, PR_TEXT_RIGHT, PR_TEXT_BOTTOM };
    static const long dflt[4] = { 91440, 45720, 91440, 45720 };
    long anchor = prop_or(p, PR_ANCHOR_TEXT, 0);
    int i;
    for (i = 0; i < 4; i++) {
        long v = prop_or(p, ids[i], dflt[i]);
        o->inset[i] = v >= 0 && v <= 20116800L ? (int)(v / 635) : 0;
    }
    o->valign = anchor == 1 || anchor == 4 || anchor == 7 || anchor == 9 ? OWF_VALIGN_CENTRE
              : anchor == 2 || anchor == 5 || anchor == 8 ? OWF_VALIGN_BOTTOM : OWF_VALIGN_TOP;
}

static void read_shape(dctx *c, span sp, const map *mp, int parent);
static void read_group(dctx *c, span group, const map *mp, int parent, int depth);

/* A slide's footer, date or number placeholder: its master's, shown. */
static void slide_field(dctx *c, owf_placeholder ph)
{
    if (!c->slide)
        return;
    c->slide->show |= ph == OWF_PH_FOOTER ? OWF_SHOW_FOOTER : ph == OWF_PH_NUMBER ? OWF_SHOW_NUMBER : OWF_SHOW_DATE;
}

static void read_shape(dctx *c, span sp, const map *mp, int parent)
{
    ppt *x = c->x;
    rec fsp, r, anchor;
    props p;
    unsigned type, flags, placement = 0;
    owf_placeholder ph = OWF_PH_NONE;
    owf_obj *o;
    owf_obj_kind kind;
    int index, x0, y0, x1, y1;
    span text = { NULL, 0 };
    unsigned long v;
    span complex;

    if (!find_rec(sp, OA_FSP, -1, &fsp) || fsp.body.n < 8)
        return;
    type = fsp.inst;
    flags = (unsigned)rd32(fsp.body, 4);
    if (flags & FSP_DELETED)
        return;
    memset(&p, 0, sizeof p);
    p.dflt = x->dgg_opt;
    p.ndflt = x->ndgg_opt;
    if (find_rec(sp, OA_FOPT, -1, &r)) {
        p.fopt[0] = r.body;
        p.n[0] = r.inst;
    }
    if (find_rec(sp, OA_TERTIARY_FOPT, -1, &r)) {
        p.fopt[1] = r.body;
        p.n[1] = r.inst;
    }

    if (flags & FSP_BACKGROUND) {
        /* the slide's or master's background */
        long col = -1;
        if (prop_bool(&p, PR_FILL_BOOLS, 0x10, 1))
            col = colourref((unsigned long)prop_or(&p, PR_FILL_COLOUR, 0xFFFFFF), c->scheme);
        if (prop_or(&p, PR_FILL_TYPE, 0) != 0)
            say(x, SAID_FILL, OWF_NOTE_APPROX, "Gradient, pattern and picture fills are shown in one colour");
        if (col >= 0 && !c->notes) {
            if (c->slide)
                c->slide->background = OWF_SHADE(col);
            else
                c->master->background = OWF_SHADE(col);
        }
        return;
    }
    if (find_rec(sp, OA_CLIENT_DATA, -1, &r) && find_rec(r.body, RT_PLACEHOLDER_ATOM, -1, &anchor))
        placement = rd8(anchor.body, 4);
    ph = placeholder_kind(placement);
    if (find_rec(sp, OA_CLIENT_TEXTBOX, -1, &r))
        text = r.body;

    if (c->notes) {
        /* a notes page: only its notes text */
        if (ph == OWF_PH_NOTES && text.p)
            read_text(c, -1, text);
        return;
    }
    if (c->slide && (ph == OWF_PH_DATE || ph == OWF_PH_FOOTER || ph == OWF_PH_NUMBER || ph == OWF_PH_HEADER)) {
        slide_field(c, ph);
        return;
    }

    /* its box */
    if (find_rec(sp, OA_CHILD_ANCHOR, -1, &anchor) && anchor.body.n >= 16) {
        x0 = map_x(mp, rds32(anchor.body, 0));
        y0 = map_y(mp, rds32(anchor.body, 4));
        x1 = map_x(mp, rds32(anchor.body, 8));
        y1 = map_y(mp, rds32(anchor.body, 12));
    } else if (find_rec(sp, OA_CLIENT_ANCHOR, -1, &anchor) && anchor.body.n >= 8) {
        /* top, left, right, bottom: in 2 bytes each, or 4 */
        long t, l, rt, b;
        if (anchor.body.n >= 16) {
            t = rds32(anchor.body, 0);
            l = rds32(anchor.body, 4);
            rt = rds32(anchor.body, 8);
            b = rds32(anchor.body, 12);
        } else {
            t = rds16(anchor.body, 0);
            l = rds16(anchor.body, 2);
            rt = rds16(anchor.body, 4);
            b = rds16(anchor.body, 6);
        }
        x0 = mu(l);
        y0 = mu(t);
        x1 = mu(rt);
        y1 = mu(b);
    } else {
        return;                      /* nowhere to put it */
    }

    if (type == SPT_LINE || type == SPT_STRAIGHT_CONNECTOR)
        kind = OWF_OBJ_SHAPE;
    else if (prop(&p, PR_PIB, &v, NULL) && v && !text.p &&
             (type == SPT_PICTURE || (flags & FSP_OLE) || ph == OWF_PH_PICTURE || ph == OWF_PH_OBJECT ||
              ph == OWF_PH_CHART || ph == OWF_PH_TABLE || type == SPT_RECT))
        kind = OWF_OBJ_PICTURE;
    else if (type == SPT_PICTURE)
        kind = OWF_OBJ_PICTURE;
    else if (ph != OWF_PH_NONE || type == SPT_TEXT_BOX)
        kind = OWF_OBJ_TEXT;
    else
        kind = OWF_OBJ_SHAPE;

    o = new_obj(c, kind, parent, &index);
    if (!o)
        return;
    o->x = x0 < x1 ? x0 : x1;
    o->y = y0 < y1 ? y0 : y1;
    o->width = x0 < x1 ? x1 - x0 : x0 - x1;
    o->height = y0 < y1 ? y1 - y0 : y0 - y1;
    o->id = (long)(rd32(fsp.body, 0) & 0x7FFFFFFFUL);
    if (flags & FSP_FLIP_H)
        o->flags |= OWF_OBJ_FLIP_H;
    if (flags & FSP_FLIP_V)
        o->flags |= OWF_OBJ_FLIP_V;
    {
        /* 16.16 degrees clockwise; the model's are tenths anticlockwise */
        long rot = prop_or(&p, PR_ROTATION, 0);
        long tenths = (long)(((long long)rot * 10) / 65536) % 3600;
        if (tenths < 0)
            tenths += 3600;
        if (tenths) {
            o->rotation = (int)((3600 - tenths) % 3600);
            /* a box turned between 45 and 135 degrees (or 225 and 315) is kept turned a quarter: its width
               and height change places about its centre */
            if ((tenths >= 450 && tenths < 1350) || (tenths >= 2250 && tenths < 3150)) {
                int cx = o->x + o->width / 2, cy = o->y + o->height / 2, w = o->width;
                o->width = o->height;
                o->height = w;
                o->x = cx - o->width / 2;
                o->y = cy - o->height / 2;
            }
        }
    }
    if (prop(&p, PR_NAME, &v, &complex) && complex.p)
        o->name = utf16_copy(complex);
    if (prop(&p, PR_DESCRIPTION, &v, &complex) && complex.p)
        o->alt = utf16_copy(complex);
    if (ph != OWF_PH_NONE) {
        int i, k = 0;
        o->placeholder = ph;
        for (i = 0; i < index; i++)
            if (ctx_obj(c, i)->placeholder == ph)
                k++;
        o->ph_index = k;
    }

    if (kind == OWF_OBJ_PICTURE) {
        static const unsigned ids[4] = { PR_CROP_LEFT, PR_CROP_TOP, PR_CROP_RIGHT, PR_CROP_BOTTOM };
        int i;
        o->image = prop(&p, PR_PIB, &v, NULL) ? blip_image(x, v) : -1;
        if (flags & FSP_OLE)
            say(x, SAID_OLE, OWF_NOTE_APPROX, "Embedded objects (charts, tables, documents) are shown as their pictures");
        if (o->image < 0)
            say(x, SAID_PICTURE, OWF_NOTE_LOST, "A picture that is not in the file, or of a kind not read yet, was left out");
        for (i = 0; i < 4; i++) {
            long cv = prop_or(&p, ids[i], 0);
            o->crop[i] = cv > 0 && cv < 65536 ? (int)((long long)cv * 1000 / 65536) : 0;
        }
        /* a picture has a line only when it says so itself */
        {
            props own = p;
            own.dflt.p = NULL;
            own.ndflt = 0;
            read_look(c, o, &own, 1);
            if (!prop_bool(&own, PR_LINE_BOOLS, 0x08, 0))
                o->look.line = 0;
        }
        return;
    }
    if (kind == OWF_OBJ_SHAPE) {
        int shorter = o->width < o->height ? o->width : o->height;
        switch (type) {
        case SPT_LINE:
        case SPT_STRAIGHT_CONNECTOR: o->shape = OWF_SHAPE_LINE; break;
        case SPT_RECT: o->shape = OWF_SHAPE_RECT; break;
        case SPT_ROUNDRECT: {
            long adj = prop_or(&p, PR_ADJUST, 3600);
            o->shape = OWF_SHAPE_ROUNDRECT;
            o->radius = adj > 0 && adj <= 10800 ? (int)((long long)shorter * adj / 21600) : shorter / 6;
            break;
        }
        case SPT_ELLIPSE: o->shape = OWF_SHAPE_ELLIPSE; break;
        case SPT_DIAMOND: o->shape = OWF_SHAPE_DIAMOND; break;
        case SPT_TRIANGLE: o->shape = OWF_SHAPE_TRIANGLE; break;
        case SPT_ARROW: o->shape = OWF_SHAPE_ARROW; break;
        case SPT_LEFT_ARROW: o->shape = OWF_SHAPE_ARROW; o->flags ^= OWF_OBJ_FLIP_H; break;
        case SPT_RT_TRIANGLE: {
            int *pts = malloc(6 * sizeof *pts);
            if (!pts) {
                x->failed = 1;
                return;
            }
            pts[0] = 0; pts[1] = 0; pts[2] = 0; pts[3] = o->height; pts[4] = o->width; pts[5] = o->height;
            o->shape = OWF_SHAPE_POLYGON;
            o->points = pts;
            o->npoints = 3;
            break;
        }
        default:
            o->shape = OWF_SHAPE_RECT;
            say(x, SAID_SHAPE, OWF_NOTE_APPROX,
                "Shapes other than rectangles, ellipses, triangles, diamonds, arrows and lines are drawn as rectangles");
        }
        read_look(c, o, &p, o->shape == OWF_SHAPE_LINE);
    } else {
        /* a text frame: no fill or line unless it says */
        if (prop_bool(&p, PR_FILL_BOOLS, 0x10, 0) || prop_bool(&p, PR_LINE_BOOLS, 0x08, 0)) {
            props own = p;
            own.dflt.p = NULL;
            own.ndflt = 0;
            read_look(c, o, &own, 0);
            if (!prop_bool(&own, PR_FILL_BOOLS, 0x10, 0))
                o->look.fill = 0;
            if (!prop_bool(&own, PR_LINE_BOOLS, 0x08, 0))
                o->look.line = 0;
        }
    }
    read_text_box(o, &p);

    /* its text: its own, or the slide's outline text it names */
    if (text.p) {
        rec ref;
        if (find_rec(text, RT_OUTLINE_TEXT_REF, -1, &ref))
            text = outline_text(c->outline, rds32(ref.body, 0));
        if (text.p)
            read_text(c, index, text);
    }
    o = ctx_obj(c, index);
    if (o && o->placeholder != OWF_PH_NONE) {
        int empty = 1, i;
        for (i = 0; i < o->text.nparas; i++)
            if (o->text.paras[i].nruns)
                empty = 0;
        if (empty && o->kind == OWF_OBJ_TEXT)
            o->flags |= OWF_OBJ_EMPTY;
    }
}

static void read_group(dctx *c, span group, const map *mp, int parent, int depth)
{
    size_t at = 0;
    rec r;
    int first = 1;
    if (depth > OWF_DECK_MAX_DEPTH) {
        say(c->x, SAID_DEPTH, OWF_NOTE_LOST, "Groups nested too deeply were left out");
        return;
    }
    while (next_rec(group, &at, &r) && !c->x->failed) {
        if (r.type == OA_SP_CONTAINER) {
            if (first) {
                first = 0;           /* the group's own shape */
                continue;
            }
            read_shape(c, r.body, mp, parent);
        } else if (r.type == OA_SPGR_CONTAINER) {
            /* a group in the group: its own shape gives its box and its children's units */
            rec own, fspgr, anchor, fsp;
            map inner;
            int index = -1, x0, y0, x1, y1;
            owf_obj *g;
            first = 0;
            if (!rec_at(r.body, 0, &own) || own.type != OA_SP_CONTAINER)
                continue;
            if (find_rec(own.body, OA_FSP, -1, &fsp) && (rd32(fsp.body, 4) & FSP_DELETED))
                continue;
            if (find_rec(own.body, OA_CHILD_ANCHOR, -1, &anchor) && anchor.body.n >= 16) {
                x0 = map_x(mp, rds32(anchor.body, 0));
                y0 = map_y(mp, rds32(anchor.body, 4));
                x1 = map_x(mp, rds32(anchor.body, 8));
                y1 = map_y(mp, rds32(anchor.body, 12));
            } else if (find_rec(own.body, OA_CLIENT_ANCHOR, -1, &anchor) && anchor.body.n >= 8) {
                int big = anchor.body.n >= 16;
                long t = big ? rds32(anchor.body, 0) : rds16(anchor.body, 0);
                long l = big ? rds32(anchor.body, 4) : rds16(anchor.body, 2);
                long rt = big ? rds32(anchor.body, 8) : rds16(anchor.body, 4);
                long b = big ? rds32(anchor.body, 12) : rds16(anchor.body, 6);
                x0 = mu(l);
                y0 = mu(t);
                x1 = mu(rt);
                y1 = mu(b);
            } else {
                continue;
            }
            if (!c->notes) {
                g = new_obj(c, OWF_OBJ_GROUP, parent, &index);
                if (!g)
                    return;
                g->x = x0 < x1 ? x0 : x1;
                g->y = y0 < y1 ? y0 : y1;
                g->width = x0 < x1 ? x1 - x0 : x0 - x1;
                g->height = y0 < y1 ? y1 - y0 : y0 - y1;
                if (fsp.body.p)
                    g->id = (long)(rd32(fsp.body, 0) & 0x7FFFFFFFUL);
            }
            memset(&inner, 0, sizeof inner);
            if (find_rec(own.body, OA_FSPGR, -1, &fspgr) && fspgr.body.n >= 16) {
                inner.gx = rds32(fspgr.body, 0);
                inner.gy = rds32(fspgr.body, 4);
                inner.gw = rds32(fspgr.body, 8) - inner.gx;
                inner.gh = rds32(fspgr.body, 12) - inner.gy;
            }
            inner.px = x0;
            inner.py = y0;
            inner.pw = (long)x1 - x0;
            inner.ph = (long)y1 - y0;
            read_group(c, r.body, &inner, c->notes ? parent : index, depth + 1);
        }
    }
}

/* A slide's, master's or notes page's drawing (PPDrawing). */
static void read_drawing(dctx *c, span container)
{
    rec drawing, dg, r;
    size_t at = 0;
    map top;
    if (!find_rec(container, RT_DRAWING, -1, &drawing) || !find_rec(drawing.body, OA_DG_CONTAINER, -1, &dg))
        return;
    memset(&top, 0, sizeof top);
    top.gw = top.gh = 576;
    top.pw = top.ph = 1440;
    while (next_rec(dg.body, &at, &r) && !c->x->failed) {
        if (r.type == OA_SPGR_CONTAINER)
            read_group(c, r.body, &top, -1, 0);
        else if (r.type == OA_SP_CONTAINER)
            read_shape(c, r.body, &top, -1);   /* the background shape */
    }
}

/* ---- Pictures ---- */

static int add_blip(ppt *x, rec *b, int n)
{
    char name[24];
    unsigned uids;
    const char *mime = NULL;
    size_t skip;
    int index;
    unsigned inst = b->inst;
    snprintf(name, sizeof name, "picture%d", n);
    switch (b->type) {
    case 0xF01A: uids = inst == 0x3D5 ? 2 : 1; mime = "image/x-emf"; break;
    case 0xF01B: uids = inst == 0x217 ? 2 : 1; mime = "image/x-wmf"; break;
    case 0xF01D: case 0xF02A: uids = inst == 0x46B || inst == 0x6E3 ? 2 : 1; mime = "image/jpeg"; break;
    case 0xF01E: uids = inst == 0x6E1 ? 2 : 1; mime = "image/png"; break;
    case 0xF01F: uids = inst == 0x7A9 ? 2 : 1; mime = "image/x-dib"; break;
    default:
        say(x, SAID_PICTURE, OWF_NOTE_LOST, "A picture that is not in the file, or of a kind not read yet, was left out");
        return -1;
    }
    if (b->type == 0xF01A || b->type == 0xF01B) {
        /* a metafile: its ids, then a 34-byte header (u32 size, a box, a size, u32 saved size, compression
           0 deflate or 0xFE none, filter), then its bytes */
        size_t h = 16 * (size_t)uids;
        unsigned long full = rd32(b->body, h), saved = rd32(b->body, h + 28);
        unsigned compression = rd8(b->body, h + 32);
        span z = sub(b->body, h + 34, saved);
        unsigned char *out;
        int rc;
        if (!has(b->body, h, 34) || !full || full >= 0x4000000UL)
            return -1;
        if (compression == 0xFE) {
            index = owf_doc_add_image(x->deck->res, name, mime, z.p, z.n, 0, 0, NULL);
            if (index < 0)
                x->failed = 1;
            return index;
        }
        if (compression != 0 || z.n < 2)
            return -1;
        if (!(out = malloc(full))) {
            say(x, SAID_PICTURE, OWF_NOTE_LOST, "A picture is too big for the memory free; it is left out");
            return -1;
        }
        index = -1;
        rc = (z.p[0] & 0x0F) == 8 && ((unsigned)z.p[0] << 8 | z.p[1]) % 31 == 0 ? owf_inflate(out, full, z.p + 2, z.n - 2)
                                                                               : OWF_ERR_CORRUPT;
        if (rc == OWF_OK) {
            index = owf_doc_add_image(x->deck->res, name, mime, out, full, 0, 0, NULL);
            if (index < 0)
                x->failed = 1;
        }
        free(out);
        return index;
    }
    skip = 16 * (size_t)uids + 1;    /* the ids and a tag byte */
    if (!has(b->body, skip, 4))
        return -1;
    index = owf_doc_add_image(x->deck->res, name, mime, b->body.p + skip, b->body.n - skip, 0, 0, NULL);
    if (index < 0)
        x->failed = 1;
    return index;
}

static void read_pictures(ppt *x, span doc_container)
{
    rec group, dgg, store, e;
    size_t at = 0;
    int n = 0;
    if (!find_rec(doc_container, RT_DRAWING_GROUP, -1, &group) || !find_rec(group.body, OA_DGG_CONTAINER, -1, &dgg))
        return;
    if (find_rec(dgg.body, OA_FOPT, -1, &e)) {
        x->dgg_opt = e.body;
        x->ndgg_opt = e.inst;
    }
    if (!find_rec(dgg.body, OA_BSTORE, -1, &store))
        return;
    while (next_rec(store.body, &at, &e) && n < MAX_BLIPS && !x->failed) {
        int *p, image = -1;
        rec blip;
        if (e.type != OA_FBSE)
            continue;
        n++;
        p = realloc(x->blips, (size_t)n * sizeof *p);
        if (!p) {
            x->failed = 1;
            return;
        }
        x->blips = p;
        x->nblips = n;
        if (rd32(e.body, 20)) {
            size_t name_len = rd8(e.body, 33);
            if (e.body.n > 36 + name_len && rec_at(e.body, 36 + name_len, &blip))
                image = add_blip(x, &blip, n);      /* in the store itself */
            else if (rec_at(x->pics, rd32(e.body, 28), &blip))
                image = add_blip(x, &blip, n);
            else
                say(x, SAID_PICTURE, OWF_NOTE_LOST, "A picture that is not in the file, or of a kind not read yet, was left out");
        }
        p[n - 1] = image;
    }
}

/* ---- Masters, slides, notes ---- */

/* The SlidePersistAtoms of a SlideListWithText; for each, fn with the records that follow it. */
typedef void (*persist_fn)(ppt *x, span atom, span after, void *user);

static void each_persist(ppt *x, span list, persist_fn fn, void *user)
{
    size_t at = 0;
    rec r, next;
    while (next_rec(list, &at, &r) && !x->failed) {
        size_t end = at;
        if (r.type != RT_SLIDE_PERSIST_ATOM)
            continue;
        while (rec_at(list, end, &next) && next.type != RT_SLIDE_PERSIST_ATOM)
            end += 8 + next.body.n;
        fn(x, r.body, sub(list, at, end - at), user);
        at = end;
    }
}

static void add_master(ppt *x, span atom, span after, void *user)
{
    master_ref *m;
    (void)after;
    (void)user;
    if (x->nmasters >= OWF_DECK_MAX_MASTERS)
        return;
    m = realloc(x->masters, (size_t)(x->nmasters + 1) * sizeof *m);
    if (!m) {
        x->failed = 1;
        return;
    }
    x->masters = m;
    m = &x->masters[x->nmasters++];
    memset(m, 0, sizeof *m);
    m->persist = rd32(atom, 0);
    m->slide_id = rd32(atom, 12);
    m->master = -1;
    memcpy(m->scheme, default_scheme, sizeof m->scheme);
}

static void add_notes(ppt *x, span atom, span after, void *user)
{
    notes_ref *n;
    (void)after;
    (void)user;
    if (x->nnotes >= OWF_DECK_MAX_SLIDES)
        return;
    n = realloc(x->notes, (size_t)(x->nnotes + 1) * sizeof *n);
    if (!n) {
        x->failed = 1;
        return;
    }
    x->notes = n;
    n[x->nnotes].persist = rd32(atom, 0);
    n[x->nnotes].slide_id = rd32(atom, 12);
    x->nnotes++;
}

static const master_ref *master_by_id(const ppt *x, unsigned long id)
{
    int i;
    for (i = 0; i < x->nmasters; i++)
        if (x->masters[i].slide_id == id && x->masters[i].master >= 0)
            return &x->masters[i];
    for (i = 0; i < x->nmasters; i++)
        if (x->masters[i].master >= 0)
            return &x->masters[i];
    return NULL;
}

/* Masters: main masters first, then title masters (slides of the master list), which take their main master's
 * text styles. */
static void read_masters(ppt *x)
{
    int pass, i;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < x->nmasters && !x->failed; i++) {
            master_ref *mr = &x->masters[i];
            rec r;
            owf_master *m;
            dctx c;
            char name[32];
            int font;
            if (!persist_rec(x, mr->persist, &r) || r.type != (pass ? RT_SLIDE : RT_MAIN_MASTER))
                continue;
            snprintf(name, sizeof name, pass ? "Title Master %d" : "Master %d", i + 1);
            m = owf_deck_add_master(x->deck, name);
            if (!m) {
                x->failed = 1;
                return;
            }
            mr->master = x->deck->nmasters - 1;
            read_scheme(r.body, mr->scheme);
            font = font_of(x, 0);
            if (font < 0)
                font = owf_doc_font(x->deck->res, "Arial", OWF_FONT_SANS);
            owf_deck_default_levels(m, font);
            m->background = OWF_SHADE(mr->scheme[0]);
            if (pass) {
                /* a title master: its main master's styles */
                rec atom;
                const master_ref *main = find_rec(r.body, RT_SLIDE_ATOM, -1, &atom)
                                         ? master_by_id(x, rd32(atom.body, 12)) : NULL;
                if (main && main != mr && main->master >= 0 && main->master < x->deck->nmasters - 1) {
                    const owf_master *from = &x->deck->masters[main->master];
                    m->title = from->title;
                    m->subtitle = from->subtitle;
                    memcpy(m->body, from->body, sizeof m->body);
                    m->notes = from->notes;
                    m->other = from->other;
                }
            } else {
                for (font = 0; font < OWF_TEXT_LEVELS; font++)
                    m->body[font].chars.colour = mr->scheme[1];
                m->title.chars.colour = mr->scheme[3];
                m->subtitle.chars.colour = m->other.chars.colour = m->notes.chars.colour = mr->scheme[1];
                master_styles(x, m, r.body, mr->scheme);
            }
            memset(&c, 0, sizeof c);
            c.x = x;
            c.master = m;
            c.master_index = mr->master;
            c.scheme = mr->scheme;
            read_drawing(&c, r.body);
        }
}

typedef struct {
    int slides_read;
} slide_walk;

static void read_notes(ppt *x, owf_slide *slide, int master_index, unsigned long notes_id)
{
    int i;
    for (i = 0; i < x->nnotes; i++)
        if (x->notes[i].slide_id == notes_id) {
            rec r;
            dctx c;
            unsigned long scheme[8];
            if (!persist_rec(x, x->notes[i].persist, &r) || r.type != RT_NOTES)
                return;
            memcpy(scheme, x->masters && master_index >= 0 ? x->masters[0].scheme : default_scheme, sizeof scheme);
            read_scheme(r.body, scheme);
            memset(&c, 0, sizeof c);
            c.x = x;
            c.slide = slide;
            c.master_index = master_index;
            c.scheme = scheme;
            c.notes = 1;
            read_drawing(&c, r.body);
            return;
        }
}

static void add_slide(ppt *x, span atom, span after, void *user)
{
    rec r, sa, info;
    owf_slide *slide;
    const master_ref *mr;
    unsigned long scheme[8], notes_id = 0;
    unsigned flags = 0;
    dctx c;
    slide_walk *w = user;
    if (!persist_rec(x, rd32(atom, 0), &r) || r.type != RT_SLIDE) {
        owf_report_add(x->report, OWF_NOTE_LOST, "A slide that is not in the file was left out");
        return;
    }
    if (x->deck->nslides >= OWF_DECK_MAX_SLIDES)
        return;
    slide = owf_deck_add_slide(x->deck, -1, -1);
    if (!slide) {
        x->failed = 1;
        return;
    }
    w->slides_read++;
    mr = NULL;
    if (find_rec(r.body, RT_SLIDE_ATOM, -1, &sa) && sa.body.n >= 22) {
        mr = master_by_id(x, rd32(sa.body, 12));
        notes_id = rd32(sa.body, 16);
        flags = rd16(sa.body, 20);
    } else {
        mr = master_by_id(x, 0);
    }
    slide->master = mr && mr->master >= 0 ? mr->master : 0;
    memcpy(scheme, mr ? mr->scheme : default_scheme, sizeof scheme);
    if (!(flags & 2))                /* fMasterScheme off: its own scheme */
        read_scheme(r.body, scheme);
    if (find_rec(r.body, RT_SLIDE_SHOW_SLIDE_INFO, -1, &info) && info.body.n >= 12) {
        unsigned show = rd16(info.body, 10);
        if (show & 0x0004)
            slide->hidden = 1;
        if (rd8(info.body, 9))
            say(x, SAID_TRANSITION, OWF_NOTE_LOST, "Slide transitions are not read from PowerPoint 97-2003 files yet");
    }
    memset(&c, 0, sizeof c);
    c.x = x;
    c.slide = slide;
    c.master_index = slide->master;
    c.scheme = scheme;
    c.outline = after;
    read_drawing(&c, r.body);
    /* its pointer may have moved: slides are added only here, so the last is it */
    slide = &x->deck->slides[x->deck->nslides - 1];
    if (flags & 4)                   /* fMasterBackground: the master's */
        slide->background = 0;
    if (notes_id)
        read_notes(x, slide, slide->master, notes_id);
}

/* ---- The file ---- */

static int detect_ppt(const unsigned char *data, size_t length)
{
    owf_cfb cfb;
    int found;
    if (!owf_cfb_detect(data, length) || owf_cfb_open(&cfb, data, length) != OWF_OK)
        return 0;
    found = owf_cfb_has(&cfb, "PowerPoint Document");
    owf_cfb_close(&cfb);
    return found ? 100 : 0;
}

static int import_ppt(const unsigned char *data, size_t length, owf_deck *deck, owf_report *report)
{
    owf_cfb cfb;
    unsigned char *doc = NULL, *current = NULL, *pics = NULL;
    size_t ndoc = 0, ncurrent = 0, npics = 0;
    ppt x;
    span cur = { NULL, 0 };
    unsigned long doc_ref;
    rec document, atom, list;
    int result, cut;
    slide_walk walk;

    result = owf_cfb_open(&cfb, data, length);
    if (result != OWF_OK)
        return result;
    result = owf_cfb_read(&cfb, "PowerPoint Document", &doc, &ndoc);
    if (result == OWF_OK && owf_cfb_read(&cfb, "Current User", &current, &ncurrent) == OWF_OK) {
        cur.p = current;
        cur.n = ncurrent;
    }
    if (result == OWF_OK && owf_cfb_has(&cfb, "Pictures"))
        owf_cfb_read(&cfb, "Pictures", &pics, &npics);
    cut = cfb.cut_short;
    owf_cfb_close(&cfb);
    if (result != OWF_OK) {
        free(current);
        return result == OWF_ERR_MEMORY ? result : OWF_ERR_FORMAT;
    }

    memset(&x, 0, sizeof x);
    x.deck = deck;
    x.report = report;
    x.doc.p = doc;
    x.doc.n = ndoc;
    x.pics.p = pics;
    x.pics.n = pics ? npics : 0;
    memset(&walk, 0, sizeof walk);

    result = read_persist(&x, cur, &doc_ref);
    if (result != OWF_OK)
        goto done;
    if (!persist_rec(&x, doc_ref, &document) || document.type != RT_DOCUMENT) {
        owf_report_add(report, OWF_NOTE_LOST, "This is not a PowerPoint 97-2003 presentation (PowerPoint 95 and older are not read)");
        result = OWF_ERR_UNSUPPORTED;
        goto done;
    }
    /* the slide size, in master units */
    if (find_rec(document.body, RT_DOCUMENT_ATOM, -1, &atom) && atom.body.n >= 16) {
        long w = rds32(atom.body, 0), h = rds32(atom.body, 4), nw = rds32(atom.body, 8), nh = rds32(atom.body, 12);
        if (w > 0 && h > 0) {
            deck->width = mu(w) < 144 ? 144 : mu(w);
            deck->height = mu(h) < 144 ? 144 : mu(h);
        }
        if (nw > 0 && nh > 0) {
            deck->notes_page.width = mu(nw) < 144 ? 144 : mu(nw);
            deck->notes_page.height = mu(nh) < 144 ? 144 : mu(nh);
        }
    }
    if (find_rec(document.body, RT_ENVIRONMENT, -1, &atom)) {
        size_t at = 0;
        rec s;
        read_fonts(&x, atom.body);
        while (next_rec(atom.body, &at, &s))
            if (s.type == RT_TEXT_MASTER_STYLE && s.inst < 9 && !x.doc_styles[s.inst].p)
                x.doc_styles[s.inst] = s.body;
    }
    read_pictures(&x, document.body);
    if (find_rec(document.body, RT_SLIDE_LIST_WITH_TEXT, 1, &list))
        each_persist(&x, list.body, add_master, NULL);
    if (find_rec(document.body, RT_SLIDE_LIST_WITH_TEXT, 2, &list))
        each_persist(&x, list.body, add_notes, NULL);
    read_masters(&x);
    if (!deck->nmasters && !x.failed) {
        owf_master *m = owf_deck_add_master(deck, "Default");
        int font = owf_doc_font(deck->res, "Arial", OWF_FONT_SANS);
        if (!m || font < 0) {
            x.failed = 1;
        } else {
            owf_deck_default_levels(m, font);
            m->background = OWF_SHADE(0xFFFFFF);
        }
    }
    if (find_rec(document.body, RT_SLIDE_LIST_WITH_TEXT, 0, &list))
        each_persist(&x, list.body, add_slide, &walk);
    if (deck->nmasters) {
        deck->res->base.font = deck->masters[0].other.chars.font;
        deck->res->base.size = 18 * OWF_TWIPS_PER_POINT;
    }
    if (cut)
        owf_report_add(report, OWF_NOTE_LOST, "The file is cut short; the slides up to the cut were read");
    owf_report_add(report, OWF_NOTE_LOST, "Animations, transitions and sounds are not read from PowerPoint 97-2003 files yet");
    result = x.failed ? OWF_ERR_MEMORY : OWF_OK;
done:
    free(x.persist);
    free(x.fonts);
    free(x.blips);
    free(x.masters);
    free(x.notes);
    free(doc);
    free(current);
    free(pics);
    return result;
}

const owf_deck_format owf_deck_format_ppt = {
    "ppt", "PowerPoint 97-2003 presentation", "ppt pps pot", detect_ppt, import_ppt, NULL
};

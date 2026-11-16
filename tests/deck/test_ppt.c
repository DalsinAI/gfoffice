/*
 * test_ppt: PowerPoint 97-2003 (.ppt) in (libowf/src/in_ppt.c). The test makes its own .ppt, record by record
 * as [MS-PPT] and [MS-ODRAW] lay them out, in a compound file from libowf's writer (owf_cfbw.c): two saves (the
 * second puts a new first slide over the first's, as PowerPoint's quick save does), a main master with its
 * colour scheme, text styles, placeholders and background, two slides (outline text named by
 * OutlineTextRefAtom; a slide of its own scheme, hidden, with a background, an ellipse, a line, a text box with a
 * tab and a slide number, a group, a picture with a crop, a shape not in the model and a deleted one) and a
 * notes page. It checks what the deck holds, that a protected file and a file of no slides are refused, and that
 * every cut of the file, and damage through it, reads without harm (run it under AddressSanitizer too).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf.h"
#include "owf_deck.h"
#include "owf_internal.h"
#include "owf_deck_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

static int checks, failures;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- Records ---- */

typedef struct {
    owf_buf b;
    size_t open[32];
    int depth;
} out;

static void u8(out *o, unsigned v) { owf_buf_putc(&o->b, (int)(v & 0xFF)); }
static void u16(out *o, unsigned v) { u8(o, v); u8(o, v >> 8); }
static void u32(out *o, unsigned long v) { u16(o, (unsigned)(v & 0xFFFF)); u16(o, (unsigned)(v >> 16)); }

/* A record's header; its length is filled in by end(). */
static void begin(out *o, unsigned ver, unsigned inst, unsigned type)
{
    u16(o, ver | inst << 4);
    u16(o, type);
    o->open[o->depth++] = o->b.length;
    u32(o, 0);
}

static void end(out *o)
{
    size_t at = o->open[--o->depth];
    unsigned long n = (unsigned long)(o->b.length - at - 4);
    o->b.data[at] = (unsigned char)n;
    o->b.data[at + 1] = (unsigned char)(n >> 8);
    o->b.data[at + 2] = (unsigned char)(n >> 16);
    o->b.data[at + 3] = (unsigned char)(n >> 24);
}

static void atom32(out *o, unsigned type, unsigned inst, long v)
{
    begin(o, 0, inst, type);
    u32(o, (unsigned long)v);
    end(o);
}

static void text_header(out *o, unsigned type)
{
    atom32(o, 0x0F9F, 0, type);
}

static void text_chars(out *o, const char *ascii)
{
    begin(o, 0, 0, 0x0FA0);
    while (*ascii)
        u16(o, (unsigned char)*ascii++);
    end(o);
}

static void text_utf16(out *o, const unsigned short *u, size_t n)
{
    size_t i;
    begin(o, 0, 0, 0x0FA0);
    for (i = 0; i < n; i++)
        u16(o, u[i]);
    end(o);
}

static void text_bytes(out *o, const char *latin1)
{
    begin(o, 0, 0, 0x0FA8);
    while (*latin1)
        u8(o, (unsigned char)*latin1++);
    end(o);
}

/* A PlaceholderAtom. */
static void client_data_ph(out *o, unsigned placement)
{
    begin(o, 15, 0, 0xF011);
    begin(o, 0, 0, 0x0BC3);
    u32(o, 0);
    u8(o, placement);
    u8(o, 0);
    u16(o, 0);
    end(o);
    end(o);
}

/* A ClientAnchor in master units, as a SmallRectStruct: top, left, right, bottom. */
static void client_anchor(out *o, int left, int top, int right, int bottom)
{
    begin(o, 0, 0, 0xF010);
    u16(o, (unsigned)top);
    u16(o, (unsigned)left);
    u16(o, (unsigned)right);
    u16(o, (unsigned)bottom);
    end(o);
}

static void child_anchor(out *o, long left, long top, long right, long bottom)
{
    begin(o, 0, 0, 0xF00F);
    u32(o, (unsigned long)left);
    u32(o, (unsigned long)top);
    u32(o, (unsigned long)right);
    u32(o, (unsigned long)bottom);
    end(o);
}

static void fsp(out *o, unsigned type, unsigned long id, unsigned long flags)
{
    begin(o, 2, type, 0xF00A);
    u32(o, id);
    u32(o, flags | 0xA00);            /* fHaveAnchor, fHaveSpt */
    end(o);
}

/* An FOPT of n simple properties, ids and values in pairs. */
static void fopt(out *o, int n, const unsigned long *pv)
{
    int i;
    begin(o, 3, (unsigned)n, 0xF00B);
    for (i = 0; i < n; i++) {
        u16(o, (unsigned)pv[2 * i]);
        u32(o, pv[2 * i + 1]);
    }
    end(o);
}

#define SCHEME(i) (0x08000000UL | (i))
#define FILLED 0x00100010UL
#define NOT_FILLED 0x00100000UL
#define LINED 0x00080008UL
#define NOT_LINED 0x00080000UL

/* A StyleTextPropAtom: paragraph runs (count, level, align+1 or 0, bullet off when < 0), then character runs
 * (count, bold, size or 0, colour as ColorIndexStruct or 0). */
typedef struct { unsigned long count; unsigned level; int align; int no_bullet; } pfr;
typedef struct { unsigned long count; int bold; int size; unsigned long colour; int font; } cfr;

static void style(out *o, const pfr *p, int np, const cfr *c, int nc)
{
    int i;
    begin(o, 0, 0, 0x0FA1);
    for (i = 0; i < np; i++) {
        unsigned long mask = (p[i].align ? 0x800UL : 0) | (p[i].no_bullet ? 0x1UL : 0);
        u32(o, p[i].count);
        u16(o, p[i].level);
        u32(o, mask);
        if (p[i].no_bullet)
            u16(o, 0);                /* bulletFlags: no bullet */
        if (p[i].align)
            u16(o, (unsigned)(p[i].align - 1));
    }
    for (i = 0; i < nc; i++) {
        unsigned long mask = (c[i].bold ? 0x1UL : 0) | (c[i].size ? 0x20000UL : 0) | (c[i].colour ? 0x40000UL : 0) |
                             (c[i].font ? 0x10000UL : 0);
        u32(o, c[i].count);
        u32(o, mask);
        if (c[i].bold)
            u16(o, 1);
        if (c[i].font)
            u16(o, (unsigned)(c[i].font - 1));
        if (c[i].size)
            u16(o, (unsigned)c[i].size);
        if (c[i].colour)
            u32(o, c[i].colour);
    }
    end(o);
}

static void scheme_atom(out *o, const unsigned long rgb[8])
{
    int i;
    begin(o, 0, 1, 0x07F0);
    for (i = 0; i < 8; i++) {
        u8(o, (unsigned)(rgb[i] >> 16));
        u8(o, (unsigned)(rgb[i] >> 8));
        u8(o, (unsigned)rgb[i]);
        u8(o, 0);
    }
    end(o);
}

static void slide_atom(out *o, unsigned long master, unsigned long notes, unsigned flags)
{
    int i;
    begin(o, 2, 0, 0x03EF);
    u32(o, 0);
    for (i = 0; i < 8; i++)
        u8(o, 0);
    u32(o, master);
    u32(o, notes);
    u16(o, flags);
    u16(o, 0);
    end(o);
}

static void drawing_begin(out *o)
{
    begin(o, 15, 0, 0x040C);
    begin(o, 15, 0, 0xF002);
    begin(o, 0, 0, 0xF008);
    u32(o, 4);
    u32(o, 1024);
    end(o);
    begin(o, 15, 0, 0xF003);
    begin(o, 15, 0, 0xF004);          /* the patriarch */
    begin(o, 1, 0, 0xF009);
    u32(o, 0); u32(o, 0); u32(o, 0); u32(o, 0);
    end(o);
    fsp(o, 0, 1024, 0x005);
    end(o);
}

static void drawing_end(out *o, long background)
{
    end(o);                           /* the group */
    if (background >= 0) {
        unsigned long pv[] = { 0x0181, (unsigned long)background, 0x01BF, FILLED };
        begin(o, 15, 0, 0xF004);
        fsp(o, 1, 1025, 0x400);
        fopt(o, 2, pv);
        end(o);
    }
    end(o);                           /* the DgContainer */
    end(o);                           /* the PPDrawing */
}

/* A text placeholder: its box in master units, its text by outline reference or of its own. */
static void placeholder(out *o, unsigned long id, unsigned placement, int l, int t, int r, int b, long outline,
                        const char *own)
{
    unsigned long pv[] = { 0x01BF, NOT_FILLED, 0x01FF, NOT_LINED };
    begin(o, 15, 0, 0xF004);
    fsp(o, 1, id, 0x020);
    fopt(o, 2, pv);
    client_anchor(o, l, t, r, b);
    client_data_ph(o, placement);
    if (outline >= 0 || own) {
        begin(o, 15, 0, 0xF00D);
        if (outline >= 0) {
            atom32(o, 0x0F9E, 0, outline);
        } else {
            text_header(o, placement == 0x0C ? 2 : 0);
            text_chars(o, own);
        }
        end(o);
    }
    end(o);
}

/* ---- The test file ---- */

static const unsigned long master_scheme[8] = { 0xFFFFFF, 0x000000, 0x808080, 0x000080, 0x00FF00, 0x0000FF, 0, 0 };
static const unsigned long own_scheme[8] = { 0x101010, 0xF0F0F0, 0x808080, 0xFFFF00, 0x00FFFF, 0xFF00FF, 0, 0 };

/* A one-pixel PNG's first bytes (its signature and IHDR): enough for a picture of known kind. */
static const unsigned char png[] = {
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1, 8, 2, 0, 0, 0,
    0x90, 0x77, 0x53, 0xDE
};

typedef struct {
    unsigned char *file;
    size_t length;
} built;

/* The streams: options select a protected file (1) or one with no Current User stream (2). */
static built build(int options)
{
    out d, cu, pics;
    unsigned long offsets[8], edit1, dir1, edit2, dir2, slide1b, emf_at;
    built res = { NULL, 0 };
    owf_cfbw *w;
    int i;
    memset(&d, 0, sizeof d);
    memset(&cu, 0, sizeof cu);
    memset(&pics, 0, sizeof pics);
    owf_buf_init(&d.b);
    owf_buf_init(&cu.b);
    owf_buf_init(&pics.b);

    /* the Pictures stream: one PNG blip, one id, a tag byte */
    begin(&pics, 0, 0x6E0, 0xF01E);
    for (i = 0; i < 17; i++)
        u8(&pics, 0xAB);
    owf_buf_put(&pics.b, png, sizeof png);
    end(&pics);
    /* and an EMF, deflated, after its id and its 34-byte metafile header */
    emf_at = (unsigned long)pics.b.length;
    {
        unsigned char raw[200], z[400];
        uLongf zn = sizeof z;
        for (i = 0; i < 200; i++)
            raw[i] = (unsigned char)(i * 7);
        compress(z, &zn, raw, sizeof raw);
        begin(&pics, 0, 0x3D4, 0xF01A);
        for (i = 0; i < 16; i++)
            u8(&pics, 0xCD);
        u32(&pics, sizeof raw);
        for (i = 0; i < 4; i++)
            u32(&pics, 0);
        u32(&pics, 100); u32(&pics, 100);
        u32(&pics, (unsigned long)zn);
        u8(&pics, 0); u8(&pics, 0xFE);
        owf_buf_put(&pics.b, z, zn);
        end(&pics);
    }

    /* persist 1: the document */
    offsets[1] = (unsigned long)d.b.length;
    begin(&d, 15, 0, 0x03E8);
    begin(&d, 1, 0, 0x03E9);
    u32(&d, 5760); u32(&d, 4320);     /* 10 x 7.5 inches */
    u32(&d, 4320); u32(&d, 5760);
    u32(&d, 1); u32(&d, 2);
    u32(&d, 0); u32(&d, 0);
    u16(&d, 1); u16(&d, 0);
    u8(&d, 0); u8(&d, 0); u8(&d, 0); u8(&d, 0);
    end(&d);
    begin(&d, 15, 0, 0x03F2);         /* Environment */
    begin(&d, 15, 0, 0x07D5);
    {
        static const char *const names[] = { "Arial", "Times New Roman" };
        int f, k;
        for (f = 0; f < 2; f++) {
            begin(&d, 0, (unsigned)f, 0x0FB7);
            for (k = 0; k < 32; k++)
                u16(&d, k < (int)strlen(names[f]) ? (unsigned char)names[f][k] : 0);
            for (k = 0; k < 4; k++)
                u8(&d, 0);
            end(&d);
        }
    }
    end(&d);
    /* the document's title style: centred, 40 points */
    begin(&d, 0, 0, 0x0FA3);
    u16(&d, 1);
    u32(&d, 0x800); u16(&d, 1);       /* PF: centred */
    u32(&d, 0x20000); u16(&d, 40);    /* CF: 40 points */
    end(&d);
    end(&d);
    begin(&d, 15, 0, 0x040B);         /* DrawingGroup */
    begin(&d, 15, 0, 0xF000);
    {
        unsigned long pv[] = { 0x0181, SCHEME(4), 0x01C0, SCHEME(1), 0x01FF, LINED };
        fopt(&d, 3, pv);
    }
    begin(&d, 15, 2, 0xF001);
    begin(&d, 2, 6, 0xF007);
    u8(&d, 6); u8(&d, 6);
    for (i = 0; i < 16; i++)
        u8(&d, 0xAB);
    u16(&d, 0xFF);
    u32(&d, emf_at);
    u32(&d, 1);
    u32(&d, 0);                       /* foDelay: the start of Pictures */
    u8(&d, 0); u8(&d, 0); u8(&d, 0); u8(&d, 0);
    end(&d);
    begin(&d, 2, 2, 0xF007);
    u8(&d, 2); u8(&d, 2);
    for (i = 0; i < 16; i++)
        u8(&d, 0xCD);
    u16(&d, 0xFF);
    u32(&d, (unsigned long)pics.b.length - emf_at);
    u32(&d, 1);
    u32(&d, emf_at);
    u8(&d, 0); u8(&d, 0); u8(&d, 0); u8(&d, 0);
    end(&d);
    end(&d);
    end(&d);
    end(&d);
    begin(&d, 15, 1, 0x0FF0);         /* the masters */
    begin(&d, 0, 0, 0x03F3);
    u32(&d, 2); u32(&d, 0); u32(&d, 0); u32(&d, 0x80000000UL); u32(&d, 0);
    end(&d);
    end(&d);
    begin(&d, 15, 0, 0x0FF0);         /* the slides, with slide 1's outline text */
    begin(&d, 0, 0, 0x03F3);
    u32(&d, 3); u32(&d, 0); u32(&d, 2); u32(&d, 256); u32(&d, 0);
    end(&d);
    text_header(&d, 0);
    text_chars(&d, "Hello");
    text_header(&d, 1);
    text_bytes(&d, "One\rTwo\r\xE9t\xE9");
    {
        pfr p[] = { { 4, 0, 0, 0 }, { 4, 1, 3, 0 }, { 5, 0, 0, 1 } };
        cfr c[] = { { 3, 1, 0, 0, 0 }, { 10, 0, 24, 0xFE996633UL, 2 } };
        style(&d, p, 3, c, 2);
    }
    begin(&d, 0, 0, 0x03F3);
    u32(&d, 4); u32(&d, 0); u32(&d, 0); u32(&d, 257); u32(&d, 0);
    end(&d);
    end(&d);
    begin(&d, 15, 2, 0x0FF0);         /* the notes */
    begin(&d, 0, 0, 0x03F3);
    u32(&d, 5); u32(&d, 0); u32(&d, 0); u32(&d, 300); u32(&d, 0);
    end(&d);
    end(&d);
    end(&d);

    /* persist 2: the main master */
    offsets[2] = (unsigned long)d.b.length;
    begin(&d, 15, 0, 0x03F8);
    slide_atom(&d, 0, 0, 0);
    scheme_atom(&d, master_scheme);
    begin(&d, 0, 1, 0x0FA3);          /* body: two levels, 30 and 20 points, in scheme colour 5 */
    u16(&d, 2);
    u32(&d, 0); u32(&d, 0x60000UL); u16(&d, 30); u32(&d, 0x05000000UL);
    u32(&d, 0); u32(&d, 0x20000UL); u16(&d, 20);
    end(&d);
    drawing_begin(&d);
    placeholder(&d, 1026, 0x01, 288, 72, 5472, 720, -1, NULL);
    placeholder(&d, 1027, 0x02, 288, 864, 5472, 4032, -1, NULL);
    drawing_end(&d, SCHEME(0));
    end(&d);

    /* persist 3: slide 1 (overwritten by the second save) */
    offsets[3] = (unsigned long)d.b.length;
    begin(&d, 15, 0, 0x03EE);
    slide_atom(&d, 0x80000000UL, 300, 7);
    drawing_begin(&d);
    placeholder(&d, 2048, 0x0D, 288, 72, 5472, 720, -1, "Old title");
    drawing_end(&d, -1);
    end(&d);

    /* persist 4: slide 2 */
    offsets[4] = (unsigned long)d.b.length;
    begin(&d, 15, 0, 0x03EE);
    slide_atom(&d, 0x80000000UL, 0, 0);
    begin(&d, 0, 0, 0x03F9);
    u32(&d, 0); u32(&d, 0); u8(&d, 0); u8(&d, 0); u16(&d, 0x0004); u8(&d, 1); u8(&d, 0); u8(&d, 0); u8(&d, 0);
    end(&d);
    scheme_atom(&d, own_scheme);
    drawing_begin(&d);
    {
        /* an ellipse, filled from the scheme (4), a 1-point line */
        unsigned long pv[] = { 0x0181, SCHEME(4), 0x01CB, 12700, 0x01BF, FILLED, 0x01FF, LINED };
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 3, 3000, 0);
        fopt(&d, 4, pv);
        client_anchor(&d, 576, 576, 1152, 864);
        end(&d);
    }
    {
        /* a line from the right, with an arrow at its end, red */
        unsigned long pv[] = { 0x01C0, 0x000000FFUL, 0x01D1, 1, 0x01FF, LINED };
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 20, 3001, 0x40);
        fopt(&d, 3, pv);
        client_anchor(&d, 0, 0, 576, 288);
        end(&d);
    }
    {
        /* a text box: "Gr" u-umlaut "n" tab, then a slide number at position 5 */
        static const unsigned short u[] = { 'G', 'r', 0xFC, 'n', 0x09, '*', 0x0B, 'x' };
        unsigned long pv[] = { 0x01BF, NOT_FILLED, 0x01FF, NOT_LINED, 0x0087, 1, 0x0081, 0 };
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 202, 3002, 0);
        fopt(&d, 4, pv);
        client_anchor(&d, 1152, 1152, 2304, 1440);
        begin(&d, 15, 0, 0xF00D);
        text_header(&d, 4);
        text_utf16(&d, u, sizeof u / sizeof u[0]);
        atom32(&d, 0x0FD8, 0, 5);
        end(&d);
        end(&d);
    }
    {
        /* a group at one inch square, its units 0 to 1000, with a rectangle in its top left quarter */
        unsigned long pv[] = { 0x0181, 0x00FF0000UL, 0x01BF, FILLED };
        begin(&d, 15, 0, 0xF003);
        begin(&d, 15, 0, 0xF004);
        begin(&d, 1, 0, 0xF009);
        u32(&d, 0); u32(&d, 0); u32(&d, 1000); u32(&d, 1000);
        end(&d);
        fsp(&d, 0, 3003, 0x001);
        client_anchor(&d, 2880, 576, 3456, 1152);
        end(&d);
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 1, 3004, 0x002);
        fopt(&d, 2, pv);
        child_anchor(&d, 0, 0, 500, 500);
        end(&d);
        end(&d);
    }
    {
        /* a picture, a quarter cut from its left */
        unsigned long pv[] = { 0x0104 | 0x4000, 1, 0x0102, 0x4000 };
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 75, 3005, 0);
        fopt(&d, 2, pv);
        client_anchor(&d, 0, 2880, 1440, 4320);
        end(&d);
    }
    {
        /* the EMF, as PowerPoint keeps a chart's picture: an OLE shape */
        unsigned long pv[] = { 0x0104 | 0x4000, 2 };
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 75, 3008, 0x010);
        fopt(&d, 1, pv);
        client_anchor(&d, 1440, 2880, 2880, 4320);
        end(&d);
    }
    {
        /* a hexagon (not in the model) and a deleted shape */
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 9, 3006, 0);
        client_anchor(&d, 4000, 3000, 4500, 3500);
        end(&d);
        begin(&d, 15, 0, 0xF004);
        fsp(&d, 1, 3007, 0x008);
        client_anchor(&d, 0, 0, 10, 10);
        end(&d);
    }
    drawing_end(&d, 0x00332211UL);    /* RGB 11 22 33 as COLORREF: red low */
    end(&d);

    /* persist 5: slide 1's notes */
    offsets[5] = (unsigned long)d.b.length;
    begin(&d, 15, 0, 0x03F0);
    begin(&d, 1, 0, 0x03F1);
    u32(&d, 256); u16(&d, 0); u16(&d, 0);
    end(&d);
    drawing_begin(&d);
    placeholder(&d, 4000, 0x0C, 100, 2000, 3000, 5000, -1, "Speak slowly");
    drawing_end(&d, -1);
    end(&d);

    /* the first save's directory (ids 1 to 5) and edit */
    dir1 = (unsigned long)d.b.length;
    begin(&d, 0, 0, 0x1772);
    u32(&d, 1UL | 5UL << 20);
    for (i = 1; i <= 5; i++)
        u32(&d, offsets[i]);
    end(&d);
    edit1 = (unsigned long)d.b.length;
    begin(&d, 0, 0, 0x0FF5);
    u32(&d, 256); u16(&d, 0); u8(&d, 0); u8(&d, 3);
    u32(&d, 0); u32(&d, dir1); u32(&d, 1); u32(&d, 6); u16(&d, 1); u16(&d, 0);
    if (options == 1)
        u32(&d, 7);                   /* encryptSessionPersistIdRef */
    end(&d);

    /* the second save: slide 1 again, its title placeholder naming the outline text */
    slide1b = (unsigned long)d.b.length;
    begin(&d, 15, 0, 0x03EE);
    slide_atom(&d, 0x80000000UL, 300, 7);
    drawing_begin(&d);
    placeholder(&d, 2048, 0x0D, 288, 72, 5472, 720, 0, NULL);
    placeholder(&d, 2049, 0x0E, 288, 864, 5472, 4032, 1, NULL);
    placeholder(&d, 2050, 0x0E, 288, 4100, 5472, 4300, -1, NULL);       /* an empty body */
    placeholder(&d, 2051, 0x08, 5000, 4100, 5472, 4300, -1, NULL);      /* the slide number */
    drawing_end(&d, -1);
    end(&d);
    dir2 = (unsigned long)d.b.length;
    begin(&d, 0, 0, 0x1772);
    u32(&d, 3UL | 1UL << 20);
    u32(&d, slide1b);
    end(&d);
    edit2 = (unsigned long)d.b.length;
    begin(&d, 0, 0, 0x0FF5);
    u32(&d, 256); u16(&d, 0); u8(&d, 0); u8(&d, 3);
    u32(&d, edit1); u32(&d, dir2); u32(&d, 1); u32(&d, 6); u16(&d, 1); u16(&d, 0);
    end(&d);

    /* Current User: its atom names the second edit */
    begin(&cu, 0, 0, 0x0FF6);
    u32(&cu, 0x14);
    u32(&cu, options == 1 ? 0xF3D1C4DFUL : 0xE391C05FUL);
    u32(&cu, edit2);
    u16(&cu, 0); u16(&cu, 0x03F4); u8(&cu, 3); u8(&cu, 0); u16(&cu, 0);
    end(&cu);

    w = owf_cfbw_new();
    if (w && !d.b.failed && !cu.b.failed && !pics.b.failed &&
        owf_cfbw_add(w, "PowerPoint Document", d.b.data, d.b.length) == OWF_OK &&
        (options == 2 || owf_cfbw_add(w, "Current User", cu.b.data, cu.b.length) == OWF_OK) &&
        owf_cfbw_add(w, "Pictures", pics.b.data, pics.b.length) == OWF_OK)
        owf_cfbw_finish(w, &res.file, &res.length);
    owf_cfbw_free(w);
    owf_buf_free(&d.b);
    owf_buf_free(&cu.b);
    owf_buf_free(&pics.b);
    return res;
}

/* ---- Checks ---- */

static char *para_text(const owf_obj *o, int para)
{
    static char text[256];
    size_t k = 0;
    int i;
    text[0] = 0;
    if (para >= o->text.nparas)
        return text;
    for (i = 0; i < o->text.paras[para].nruns; i++) {
        const owf_run *r = &o->text.paras[para].runs[i];
        const char *s = r->kind == OWF_RUN_TEXT && r->text ? r->text : r->kind == OWF_RUN_TAB ? "\\t"
                      : r->kind == OWF_RUN_LINEBREAK ? "\\n" : r->kind == OWF_RUN_FIELD ? "#" : "";
        size_t n = strlen(s);
        if (k + n < sizeof text) {
            memcpy(text + k, s, n);
            k += n;
        }
    }
    text[k] = 0;
    return text;
}

static const owf_obj *find_obj(const owf_slide *s, long id)
{
    int i = owf_slide_obj_by_id(s, id);
    return i >= 0 ? &s->objs[i] : NULL;
}

static int has_note(const owf_report *r, const char *part)
{
    int i;
    for (i = 0; i < owf_report_count(r); i++)
        if (strstr(owf_report_note(r, i, NULL, NULL), part))
            return 1;
    return 0;
}

static void check_deck(const unsigned char *file, size_t length)
{
    owf_deck *deck = NULL;
    owf_report *report = owf_report_new();
    const owf_deck_format *used = NULL;
    const owf_slide *s;
    const owf_obj *o;
    const owf_master *m;
    int rc = owf_import_deck_memory(file, length, NULL, &deck, report, &used);

    CHECK(rc == OWF_OK, "import: %d", rc);
    CHECK(used && !strcmp(used->name, "ppt"), "read by %s", used ? used->name : "none");
    if (rc != OWF_OK || !deck) {
        owf_report_free(report);
        return;
    }
    CHECK(deck->width == 14400 && deck->height == 10800, "size %d x %d", deck->width, deck->height);
    CHECK(deck->notes_page.width == 10800 && deck->notes_page.height == 14400, "notes page %d x %d",
          deck->notes_page.width, deck->notes_page.height);
    CHECK(deck->res->nfonts >= 2, "fonts %d", deck->res->nfonts);

    /* the master: its scheme's background, the document's title style over its own, its body levels */
    CHECK(deck->nmasters == 1, "masters %d", deck->nmasters);
    m = &deck->masters[0];
    CHECK(m->background == OWF_SHADE(0xFFFFFF), "master background %06lx", m->background);
    CHECK(m->title.chars.size == 40 * 20 && m->title.para.align == OWF_ALIGN_CENTRE, "title level %d %d",
          m->title.chars.size, (int)m->title.para.align);
    CHECK(m->title.chars.colour == 0x000080, "title colour %06lx (the scheme's title colour)", m->title.chars.colour);
    CHECK(m->body[0].chars.size == 30 * 20 && m->body[1].chars.size == 20 * 20, "body levels %d %d",
          m->body[0].chars.size, m->body[1].chars.size);
    CHECK(m->body[0].chars.colour == 0x0000FF, "body colour %06lx", m->body[0].chars.colour);
    CHECK(m->body[0].chars.font >= 0 && !strcmp(deck->res->fonts[m->body[0].chars.font].name, "Arial"),
          "body font");
    CHECK(m->nobjs == 2, "master objects %d", m->nobjs);
    if (m->nobjs == 2) {
        CHECK(m->objs[0].placeholder == OWF_PH_TITLE && m->objs[1].placeholder == OWF_PH_BODY, "master placeholders");
        CHECK(m->objs[0].x == 720 && m->objs[0].y == 180 && m->objs[0].width == 12960 && m->objs[0].height == 1620,
              "master title box %d %d %d %d", m->objs[0].x, m->objs[0].y, m->objs[0].width, m->objs[0].height);
        CHECK(m->objs[0].flags & OWF_OBJ_EMPTY, "master title is a prompt");
    }

    CHECK(deck->nslides == 2, "slides %d", deck->nslides);
    if (deck->nslides < 2) {
        owf_deck_free(deck);
        owf_report_free(report);
        return;
    }

    /* slide 1, from the second save: its outline text */
    s = &deck->slides[0];
    CHECK(s->master == 0 && !s->hidden && s->background == 0, "slide 1 master %d hidden %d bg %06lx", s->master,
          s->hidden, s->background);
    o = find_obj(s, 2048);
    CHECK(o && o->kind == OWF_OBJ_TEXT && o->placeholder == OWF_PH_TITLE, "slide 1 title");
    if (o)
        CHECK(!strcmp(para_text(o, 0), "Hello") && !(o->flags & OWF_OBJ_EMPTY), "title text '%s' (not the first save's)",
              para_text(o, 0));
    o = find_obj(s, 2049);
    CHECK(o && o->placeholder == OWF_PH_BODY && o->text.nparas == 3, "body paragraphs %d", o ? o->text.nparas : -1);
    if (o && o->text.nparas == 3) {
        const owf_run *r = &o->text.paras[0].runs[0];
        CHECK(!strcmp(para_text(o, 0), "One") && !strcmp(para_text(o, 1), "Two"), "body text '%s' '%s'",
              para_text(o, 0), para_text(o, 1));
        CHECK(!strcmp(para_text(o, 2), "\xC3\xA9t\xC3\xA9"), "Latin-1 bytes as UTF-8: '%s'", para_text(o, 2));
        CHECK(owf_obj_level(o, 0) == 1 && owf_obj_level(o, 1) == 2 && owf_obj_level(o, 2) == 0, "levels %d %d %d",
              owf_obj_level(o, 0), owf_obj_level(o, 1), owf_obj_level(o, 2));
        CHECK(o->text.paras[1].fmt.align == OWF_ALIGN_RIGHT, "second paragraph right-aligned");
        CHECK((r->fmt.flags & OWF_BOLD) && r->fmt.size == 0 && r->fmt.colour == OWF_COLOUR_AUTO, "first run bold only");
        r = &o->text.paras[1].runs[0];
        CHECK(!(r->fmt.flags & OWF_BOLD) && r->fmt.size == 24 * 20 && r->fmt.colour == 0x336699, "second run %u %d %06lx",
              r->fmt.flags, r->fmt.size, r->fmt.colour);
        CHECK(r->fmt.font >= 0 && !strcmp(deck->res->fonts[r->fmt.font].name, "Times New Roman"), "second run's font");
    }
    o = find_obj(s, 2050);
    CHECK(o && (o->flags & OWF_OBJ_EMPTY) && o->ph_index == 1, "an empty body is a prompt, the second body");
    CHECK(find_obj(s, 2051) == NULL && (s->show & OWF_SHOW_NUMBER), "the number placeholder shows the master's");
    CHECK(s->notes.text.nparas == 1 && !strcmp(para_text(&s->notes, 0), "Speak slowly"), "notes '%s'",
          para_text(&s->notes, 0));

    /* slide 2: its own scheme, hidden, a background */
    s = &deck->slides[1];
    CHECK(s->hidden, "slide 2 hidden");
    CHECK(s->background == OWF_SHADE(0x112233), "slide 2 background %06lx", s->background);
    o = find_obj(s, 3000);
    CHECK(o && o->kind == OWF_OBJ_SHAPE && o->shape == OWF_SHAPE_ELLIPSE, "ellipse");
    if (o) {
        CHECK(o->look.fill == OWF_SHADE(0x00FFFF), "ellipse fill from the slide's scheme %06lx", o->look.fill);
        CHECK(o->look.line == OWF_SHADE(0xF0F0F0) && o->look.line_width == 20, "ellipse line %06lx %d", o->look.line,
              o->look.line_width);
        CHECK(o->x == 1440 && o->y == 1440 && o->width == 1440 && o->height == 720, "ellipse box");
    }
    o = find_obj(s, 3001);
    CHECK(o && o->shape == OWF_SHAPE_LINE && (o->flags & OWF_OBJ_FLIP_H) && o->look.arrows == OWF_ARROW_END &&
          o->look.line == OWF_SHADE(0xFF0000) && o->look.fill == 0, "line");
    o = find_obj(s, 3002);
    CHECK(o && o->kind == OWF_OBJ_TEXT && o->look.fill == 0 && o->look.line == 0, "text box");
    if (o) {
        CHECK(!strcmp(para_text(o, 0), "Gr\xC3\xBCn\\t#\\nx"), "text box text '%s'", para_text(o, 0));
        CHECK(o->valign == OWF_VALIGN_CENTRE && o->inset[0] == 0 && o->inset[1] == 72, "text box insets %d %d %d",
              (int)o->valign, o->inset[0], o->inset[1]);
        CHECK(o->text.paras[0].nruns >= 4 && o->text.paras[0].runs[2].kind == OWF_RUN_FIELD &&
              o->text.paras[0].runs[2].field == OWF_FIELD_PAGE, "slide number field");
    }
    o = find_obj(s, 3003);
    CHECK(o && o->kind == OWF_OBJ_GROUP && o->x == 7200 && o->y == 1440 && o->width == 1440 && o->height == 1440, "group");
    if (o) {
        int gi = (int)(o - s->objs);
        const owf_obj *c = find_obj(s, 3004);
        CHECK(c && c->parent == gi && c->x == 7200 && c->y == 1440 && c->width == 720 && c->height == 720 &&
              c->look.fill == OWF_SHADE(0x0000FF), "group child %d %d %d %d", c ? c->x : 0, c ? c->y : 0,
              c ? c->width : 0, c ? c->height : 0);
    }
    o = find_obj(s, 3005);
    CHECK(o && o->kind == OWF_OBJ_PICTURE && o->image >= 0 && o->crop[0] == 250, "picture");
    if (o && o->image >= 0)
        CHECK(!strcmp(deck->res->images[o->image].mime, "image/png") &&
              deck->res->images[o->image].length == sizeof png, "picture bytes");
    if (o)
        CHECK(o->look.line == 0, "a picture has no line of the drawing's defaults");
    o = find_obj(s, 3008);
    CHECK(o && o->kind == OWF_OBJ_PICTURE && o->image >= 0, "an embedded object as its picture");
    if (o && o->image >= 0)
        CHECK(!strcmp(deck->res->images[o->image].mime, "image/x-emf") && deck->res->images[o->image].length == 200 &&
              deck->res->images[o->image].data[199] == (unsigned char)(199 * 7), "the EMF inflated");
    CHECK(has_note(report, "Embedded objects"), "the report says embedded objects are pictures");
    o = find_obj(s, 3006);
    CHECK(o && o->shape == OWF_SHAPE_RECT, "a hexagon as a rectangle");
    CHECK(has_note(report, "drawn as rectangles"), "the report says so");
    CHECK(find_obj(s, 3007) == NULL, "a deleted shape is left out");

    owf_deck_free(deck);
    owf_report_free(report);
}

/* A persist directory of 2 x 320000 entries (an older save's, then a newer one's over every id): read in well
 * under the time limit (an n-squared directory took 42 s here, hours on a 68040), and the newer save's offsets win. */
static void big_directory(void)
{
    enum { IDS = 320000, RUN = 4095 };
    out d;
    unsigned long doc, dir_old, edit_old, dir_new, edit_new;
    unsigned char *file = NULL;
    size_t length = 0;
    owf_cfbw *w;
    int pass;
    memset(&d, 0, sizeof d);
    owf_buf_init(&d.b);
    doc = (unsigned long)d.b.length;
    begin(&d, 15, 0, 0x03E8);
    begin(&d, 1, 0, 0x03E9);
    u32(&d, 5760); u32(&d, 4320); u32(&d, 4320); u32(&d, 5760);
    u32(&d, 1); u32(&d, 2); u32(&d, 0); u32(&d, 0); u16(&d, 1); u16(&d, 0); u32(&d, 0);
    end(&d);
    end(&d);
    dir_old = edit_old = dir_new = 0;
    for (pass = 0; pass < 2; pass++) {
        unsigned long id = 1, at;
        at = (unsigned long)d.b.length;
        begin(&d, 0, 0, 0x1772);
        while (id <= IDS) {
            unsigned long n = IDS - id + 1 < RUN ? IDS - id + 1 : RUN, j;
            u32(&d, id | n << 20);
            for (j = 0; j < n; j++)
                u32(&d, pass ? doc : 0xFFFFFFUL);    /* the older save's offsets lead nowhere */
            id += n;
        }
        end(&d);
        if (pass)
            dir_new = at;
        else
            dir_old = at;
        at = (unsigned long)d.b.length;
        begin(&d, 0, 0, 0x0FF5);
        u32(&d, 256); u16(&d, 0); u8(&d, 0); u8(&d, 3);
        u32(&d, pass ? edit_old : 0); u32(&d, pass ? dir_new : dir_old); u32(&d, 1); u32(&d, IDS + 1);
        u16(&d, 1); u16(&d, 0);
        end(&d);
        if (pass)
            edit_new = at;
        else
            edit_old = at;
    }
    (void)edit_new;
    w = owf_cfbw_new();
    if (w && !d.b.failed && owf_cfbw_add(w, "PowerPoint Document", d.b.data, d.b.length) == OWF_OK)
        owf_cfbw_finish(w, &file, &length);
    owf_cfbw_free(w);
    owf_buf_free(&d.b);
    CHECK(file != NULL, "big directory built");
    if (file) {
        owf_deck *deck = NULL;
        owf_report *report = owf_report_new();
        clock_t t0 = clock();
        int rc = owf_import_deck_memory(file, length, "ppt", &deck, report, NULL);
        double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        CHECK(rc == OWF_OK && deck && deck->width == 14400, "big directory read, the newer save's offsets: %d", rc);
        CHECK(secs < 5.0, "big directory read in %.2f s (limit 5 s)", secs);
        printf("big directory: %d entries in %.3f s\n", 2 * IDS, secs);
        owf_deck_free(deck);
        owf_report_free(report);
        free(file);
    }
}

int main(void)
{
    built b = build(0), p = build(1), n = build(2);
    owf_deck *deck = NULL;
    owf_report *report;
    size_t cut;
    int rc;
    unsigned char *copy;

    CHECK(b.file && p.file && n.file, "built");
    if (!b.file || !p.file || !n.file)
        return 1;
    CHECK(owf_deck_format_named("ppt")->detect(b.file, b.length) == 100, "detected");
    CHECK(owf_file_kind(b.file, b.length) == OWF_KIND_DECK, "its kind is a deck");
    check_deck(b.file, b.length);
    /* no Current User stream: the last UserEditAtom in the stream */
    check_deck(n.file, n.length);
    big_directory();

    report = owf_report_new();
    rc = owf_import_deck_memory(p.file, p.length, "ppt", &deck, report, NULL);
    CHECK(rc == OWF_ERR_UNSUPPORTED && has_note(report, "password"), "a protected file: %d", rc);
    owf_deck_free(deck);
    deck = NULL;
    owf_report_free(report);

    /* a compound file of no presentation is not one */
    {
        owf_cfbw *w = owf_cfbw_new();
        unsigned char *f = NULL;
        size_t len = 0;
        static const unsigned char some[600] = { 1, 2, 3 };
        if (w && owf_cfbw_add(w, "WordDocument", some, sizeof some) == OWF_OK && owf_cfbw_finish(w, &f, &len) == OWF_OK)
            CHECK(owf_deck_format_named("ppt")->detect(f, len) == 0, "a Word file is not a presentation");
        owf_cfbw_free(w);
        free(f);
    }

    /* every cut and damage through the file reads without harm */
    for (cut = 0; cut < b.length; cut += cut < 4096 ? 61 : 509) {
        report = owf_report_new();
        deck = NULL;
        owf_import_deck_memory(b.file, cut, "ppt", &deck, report, NULL);
        owf_deck_free(deck);
        owf_report_free(report);
    }
    copy = malloc(b.length);
    if (copy) {
        size_t at;
        for (at = 512; at < b.length; at += 37) {
            memcpy(copy, b.file, b.length);
            copy[at] ^= 0xFF;
            copy[at + 1 < b.length ? at + 1 : at] ^= 0x5A;
            report = owf_report_new();
            deck = NULL;
            owf_import_deck_memory(copy, b.length, "ppt", &deck, report, NULL);
            owf_deck_free(deck);
            owf_report_free(report);
        }
        free(copy);
    }
    checks++;

    free(b.file);
    free(p.file);
    free(n.file);
    printf("test_ppt: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

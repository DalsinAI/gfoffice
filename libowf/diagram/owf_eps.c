/*
 * owf_eps.c: one page drawn as Encapsulated PostScript (EPSF-3.0, PostScript level 2), with the same calls as
 * libowf's page-drawing PDF writer (owf_pdf.h) so owf_dpdf.c draws a diagram's page into either: text in the
 * standard fonts (Helvetica, Times, Courier and their bold and italic faces) re-encoded to WinAnsiEncoding,
 * measured and fitted as the PDF does it; rectangles, rounded rectangles, ellipses, lines and polygons, solid or
 * dashed. Positions and sizes are twips from the page's top left; colours OWF_SHADE(0xRRGGBB), 0 for none.
 * Integer arithmetic only.
 * Written from Adobe's published PostScript Language Reference (third edition) and the Encapsulated PostScript
 * File Format Specification 3.0, not from any program's code.
 * GFOpenDiag step D6 (amicradle-teams designs/GFOPENDIAG-20261113.md).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"
#include "owf_pdf.h"

#include <stdlib.h>
#include <string.h>

struct owf_eps {
    owf_buf body;
    int width, height;                   /* twips */
    int dash;
    unsigned fonts;                      /* bit f: face f (0-11) is used */
};

/* the faces, in owf_pdf.c's order: 0-3 Helvetica, 4-7 Times, 8-11 Courier; +1 bold, +2 italic */
static const char *const face_names[12] = {
    "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
    "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
    "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"
};

/* WinAnsiEncoding where it differs from PostScript's ISOLatin1Encoding: 0x27, 0x2D (ISOLatin1 has minus there,
 * WinAnsi hyphen, as owf_pdffonts.c measures it), 0x60 and 0x80 to 0x9F */
static const char *const winansi_high[32] = {
    "Euro", NULL, "quotesinglbase", "florin", "quotedblbase", "ellipsis", "dagger", "daggerdbl",
    "circumflex", "perthousand", "Scaron", "guilsinglleft", "OE", NULL, "Zcaron", NULL,
    NULL, "quoteleft", "quoteright", "quotedblleft", "quotedblright", "bullet", "endash", "emdash",
    "tilde", "trademark", "scaron", "guilsinglright", "oe", NULL, "zcaron", "Ydieresis"
};

owf_eps *owf_eps_new(int width, int height)
{
    owf_eps *e = calloc(1, sizeof *e);
    if (!e)
        return NULL;
    owf_buf_init(&e->body);
    e->width = width > 20 ? width : 20;
    e->height = height > 20 ? height : 20;
    return e;
}

void owf_eps_free(owf_eps *e)
{
    if (!e)
        return;
    owf_buf_free(&e->body);
    free(e);
}

/* twips as points with two decimals */
static void put_num(owf_buf *out, long twips)
{
    long v = twips * 5;
    if (v < 0) {
        owf_buf_putc(out, '-');
        v = -v;
    }
    if (v % 100)
        owf_buf_printf(out, "%ld.%02ld", v / 100, v % 100);
    else
        owf_buf_printf(out, "%ld", v / 100);
}

/* a point, y turned up from the page's foot */
static void put_xy(owf_eps *e, long x, long y)
{
    put_num(&e->body, x);
    owf_buf_putc(&e->body, ' ');
    put_num(&e->body, e->height - y);
    owf_buf_putc(&e->body, ' ');
}

static void put_colour(owf_buf *out, unsigned long rgb)
{
    unsigned r = (unsigned)((rgb >> 16) & 255), g = (unsigned)((rgb >> 8) & 255), b = (unsigned)(rgb & 255);
    owf_buf_printf(out, "%u.%03u %u.%03u %u.%03u setrgbcolor\n", r / 255, (r % 255) * 1000 / 255, g / 255,
                   (g % 255) * 1000 / 255, b / 255, (b % 255) * 1000 / 255);
}

/* The path just made, filled and stroked as asked (gsave keeps it for the stroke after the fill). */
static void paint(owf_eps *e, unsigned long fill, unsigned long line, int line_width)
{
    owf_buf *o = &e->body;
    if (OWF_SHADE_SET(fill)) {
        if (OWF_SHADE_SET(line))
            owf_buf_puts(o, "gsave ");
        put_colour(o, OWF_SHADE_RGB(fill));
        owf_buf_puts(o, OWF_SHADE_SET(line) ? "fill grestore\n" : "fill\n");
    }
    if (OWF_SHADE_SET(line)) {
        int w = line_width > 0 ? line_width : 5;
        put_colour(o, OWF_SHADE_RGB(line));
        put_num(o, w);
        owf_buf_puts(o, " setlinewidth ");
        if (e->dash) {
            owf_buf_puts(o, "[");
            put_num(o, w * 4);
            owf_buf_puts(o, " ");
            put_num(o, w * 3);
            owf_buf_puts(o, "] 0 setdash ");
        }
        owf_buf_puts(o, "stroke\n");
    }
}

void owf_eps_dash(owf_eps *e, int dash)
{
    if (e)
        e->dash = dash;
}

int owf_eps_rect(owf_eps *e, int x, int y, int w, int h, unsigned long fill, unsigned long line, int line_width)
{
    if (!e)
        return -1;
    if (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line))
        return 0;
    owf_buf_puts(&e->body, "gsave newpath ");
    put_xy(e, x, y); owf_buf_puts(&e->body, "moveto ");
    put_xy(e, (long)x + w, y); owf_buf_puts(&e->body, "lineto ");
    put_xy(e, (long)x + w, (long)y + h); owf_buf_puts(&e->body, "lineto ");
    put_xy(e, x, (long)y + h); owf_buf_puts(&e->body, "lineto closepath\n");
    paint(e, fill, line, line_width);
    owf_buf_puts(&e->body, "grestore\n");
    return e->body.failed ? -1 : 0;
}

/* A quarter of a circle as a Bezier curve, as owf_pdf.c draws one: handles 0.5523 of the radius along. */
#define KAPPA(r) ((long)((long long)(r) * 5523 / 10000))

static void curve(owf_eps *e, long x1, long y1, long x2, long y2, long x3, long y3)
{
    put_xy(e, x1, y1);
    put_xy(e, x2, y2);
    put_xy(e, x3, y3);
    owf_buf_puts(&e->body, "curveto ");
}

int owf_eps_round_rect(owf_eps *e, int x, int y, int w, int h, int r, unsigned long fill, unsigned long line,
                       int line_width)
{
    long k, x2 = (long)x + w, y2 = (long)y + h;
    if (!e)
        return -1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0)
        return owf_eps_rect(e, x, y, w, h, fill, line, line_width);
    if (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line))
        return 0;
    k = KAPPA(r);
    owf_buf_puts(&e->body, "gsave newpath ");
    put_xy(e, x + r, y); owf_buf_puts(&e->body, "moveto ");
    put_xy(e, x2 - r, y); owf_buf_puts(&e->body, "lineto ");
    curve(e, x2 - r + k, y, x2, y + r - k, x2, y + r);
    put_xy(e, x2, y2 - r); owf_buf_puts(&e->body, "lineto ");
    curve(e, x2, y2 - r + k, x2 - r + k, y2, x2 - r, y2);
    put_xy(e, x + r, y2); owf_buf_puts(&e->body, "lineto ");
    curve(e, x + r - k, y2, x, y2 - r + k, x, y2 - r);
    put_xy(e, x, y + r); owf_buf_puts(&e->body, "lineto ");
    curve(e, x, y + r - k, x + r - k, y, x + r, y);
    owf_buf_puts(&e->body, "closepath\n");
    paint(e, fill, line, line_width);
    owf_buf_puts(&e->body, "grestore\n");
    return e->body.failed ? -1 : 0;
}

int owf_eps_ellipse(owf_eps *e, int x, int y, int w, int h, unsigned long fill, unsigned long line, int line_width)
{
    long rx = w / 2, ry = h / 2, cx = (long)x + rx, cy = (long)y + ry, kx = KAPPA(rx), ky = KAPPA(ry);
    if (!e)
        return -1;
    if (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line))
        return 0;
    owf_buf_puts(&e->body, "gsave newpath ");
    put_xy(e, cx + rx, cy); owf_buf_puts(&e->body, "moveto ");
    curve(e, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    curve(e, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    curve(e, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    curve(e, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    owf_buf_puts(&e->body, "closepath\n");
    paint(e, fill, line, line_width);
    owf_buf_puts(&e->body, "grestore\n");
    return e->body.failed ? -1 : 0;
}

int owf_eps_path(owf_eps *e, const int *xy, int n, int closed, unsigned long fill, unsigned long line, int line_width)
{
    int i;
    if (!e)
        return -1;
    if (n < 2 || !xy || (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line)))
        return 0;
    if (!closed)
        fill = 0;
    owf_buf_puts(&e->body, "gsave newpath ");
    for (i = 0; i < n; i++) {
        put_xy(e, xy[i * 2], xy[i * 2 + 1]);
        owf_buf_puts(&e->body, i ? "lineto " : "moveto ");
    }
    owf_buf_puts(&e->body, closed ? "closepath\n" : "\n");
    paint(e, fill, line, line_width);
    owf_buf_puts(&e->body, "grestore\n");
    return e->body.failed ? -1 : 0;
}

/* ---- Text ---- */

static int face(owf_font_kind kind, unsigned flags)
{
    int base = kind == OWF_FONT_MONO ? 8 : kind == OWF_FONT_SERIF ? 4 : 0;
    if (flags & OWF_BOLD)
        base += 1;
    if (flags & OWF_ITALIC)
        base += 2;
    return base;
}

/* The WinAnsi bytes a character is written as, as owf_pdf.c writes it: its own, a fallback's, or '?'. */
static int winansi_bytes(unsigned long c, int out[4])
{
    const char *fb;
    int b = owf_pdf_winansi(c), n = 0;
    if (b) {
        out[0] = b;
        return 1;
    }
    if ((fb = owf_pdf_fallback(c)) != NULL) {
        const unsigned char *q = (const unsigned char *)fb, *end = q + strlen(fb);
        while (q < end && n < 4) {
            int k = owf_pdf_winansi(owf_utf8_next(&q, end));
            out[n++] = k ? k : '?';
        }
        return n;
    }
    out[0] = '?';
    return 1;
}

int owf_eps_text(owf_eps *e, int x, int baseline, int width, const char *utf8, size_t length, owf_font_kind kind,
                 unsigned flags, int size, unsigned long colour)
{
    owf_buf *o;
    const unsigned char *s = (const unsigned char *)utf8, *end = s + length;
    int f = face(kind, flags), any = 0;
    long natural, tz = 1000;
    if (!e)
        return -1;
    if (!utf8 || !length || size <= 0)
        return 0;
    natural = owf_pdf_text_width(utf8, length, kind, flags, size);
    o = &e->body;
    e->fonts |= 1u << f;
    owf_buf_puts(o, "gsave ");
    put_colour(o, colour & 0xFFFFFFUL);
    owf_buf_printf(o, "/OW-%s ", face_names[f]);
    put_num(o, size);
    owf_buf_puts(o, " selectfont ");
    put_xy(e, x, baseline);
    owf_buf_puts(o, "translate ");
    if (width > 0 && natural > 0) {      /* fitted to the width, as the PDF's horizontal scaling does */
        tz = (long)((long long)width * 1000 / natural);
        if (tz < 100) tz = 100;
        if (tz > 10000) tz = 10000;
        if (tz < 995 || tz > 1005)
            owf_buf_printf(o, "%ld.%03ld 1 scale ", tz / 1000, tz % 1000);
    }
    owf_buf_puts(o, "0 0 moveto (");
    while (s < end) {
        unsigned long c = owf_utf8_next(&s, end);
        int b[4], n, k;
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
        n = winansi_bytes(c, b);
        for (k = 0; k < n; k++) {
            if (b[k] == '(' || b[k] == ')' || b[k] == '\\')
                owf_buf_printf(o, "\\%c", b[k]);
            else if (b[k] >= 127)
                owf_buf_printf(o, "\\%03o", b[k]);
            else
                owf_buf_putc(o, b[k]);
        }
        any = 1;
    }
    owf_buf_puts(o, ") show grestore\n");
    if (any && (flags & (OWF_UNDERLINE | OWF_STRIKE))) {
        long w = width > 0 ? width : natural, t = size / 18 > 5 ? size / 18 : 5;
        owf_buf_puts(o, "gsave ");
        put_colour(o, colour & 0xFFFFFFUL);
        put_num(o, t);
        owf_buf_puts(o, " setlinewidth newpath\n");
        if (flags & OWF_UNDERLINE) {
            put_xy(e, x, baseline + size / 8);
            owf_buf_puts(o, "moveto ");
            put_xy(e, x + w, baseline + size / 8);
            owf_buf_puts(o, "lineto\n");
        }
        if (flags & OWF_STRIKE) {
            put_xy(e, x, baseline - size * 3 / 10);
            owf_buf_puts(o, "moveto ");
            put_xy(e, x + w, baseline - size * 3 / 10);
            owf_buf_puts(o, "lineto\n");
        }
        owf_buf_puts(o, "stroke grestore\n");
    }
    return o->failed ? -1 : 0;
}

/* ---- The file ---- */

/* A DSC comment's text: printable ASCII, at most 200 bytes, no parentheses to balance. */
static void put_dsc_text(owf_buf *o, const char *s)
{
    int n = 0;
    for (; s && *s && n < 200; s++, n++)
        owf_buf_putc(o, (unsigned char)*s >= 32 && (unsigned char)*s < 127 && *s != '(' && *s != ')' ? *s : '?');
}

int owf_eps_finish(owf_eps *e, const char *title, unsigned char **data, size_t *length)
{
    owf_buf out;
    int i, f;
    long wp, hp;
    size_t n = 0;
    *data = NULL;
    if (length)
        *length = 0;
    if (!e || e->body.failed)
        return OWF_ERR_MEMORY;
    wp = ((long)e->width + 19) / 20;     /* whole points, rounded up */
    hp = ((long)e->height + 19) / 20;
    owf_buf_init(&out);
    owf_buf_puts(&out, "%!PS-Adobe-3.0 EPSF-3.0\n");
    owf_buf_printf(&out, "%%%%BoundingBox: 0 0 %ld %ld\n%%%%HiResBoundingBox: 0 0 ", wp, hp);
    put_num(&out, e->width);
    owf_buf_putc(&out, ' ');
    put_num(&out, e->height);
    owf_buf_puts(&out, "\n%%Creator: GFOpenDiag\n%%Title: ");
    put_dsc_text(&out, title && *title ? title : "Diagram");
    owf_buf_puts(&out, "\n%%LanguageLevel: 2\n%%Pages: 1\n");
    if (e->fonts) {
        owf_buf_puts(&out, "%%DocumentNeededResources:");
        for (f = 0, i = 0; f < 12; f++)
            if (e->fonts & (1u << f))
                owf_buf_printf(&out, "%sfont %s\n", i++ ? "%%+ " : " ", face_names[f]);
    }
    owf_buf_puts(&out, "%%EndComments\n%%BeginProlog\n");
    if (e->fonts) {                      /* the faces used, re-encoded to WinAnsiEncoding */
        owf_buf_puts(&out, "/OW-WinAnsi ISOLatin1Encoding 256 array copy def\n"
                           "OW-WinAnsi 39 /quotesingle put OW-WinAnsi 45 /hyphen put OW-WinAnsi 96 /grave put\n");
        for (i = 0; i < 32; i++)
            if (winansi_high[i])
                owf_buf_printf(&out, "OW-WinAnsi %d /%s put\n", 128 + i, winansi_high[i]);
        owf_buf_puts(&out, "/OW-font { findfont dup length dict begin { 1 index /FID ne { def } { pop pop } ifelse }"
                           " forall /Encoding OW-WinAnsi def currentdict end definefont pop } bind def\n");
        for (f = 0; f < 12; f++)
            if (e->fonts & (1u << f))
                owf_buf_printf(&out, "/OW-%s /%s OW-font\n", face_names[f], face_names[f]);
    }
    owf_buf_puts(&out, "%%EndProlog\n%%Page: 1 1\nsave\n");
    owf_buf_put(&out, e->body.data, e->body.length);
    owf_buf_puts(&out, "restore\nshowpage\n%%Trailer\n%%EOF\n");
    if (out.failed) {
        owf_buf_free(&out);
        return OWF_ERR_MEMORY;
    }
    if (owf_buf_take(&out, data, &n) != OWF_OK)
        return OWF_ERR_MEMORY;
    if (length)
        *length = n;
    return OWF_OK;
}

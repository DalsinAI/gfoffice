/*
 * libowf: the page-drawing PDF writer (owf_pdf.h). Each page's drawing is
 * a content stream; the standard fonts, pictures and links are objects
 * written once at the end. Numbers are written in hundredths of a point,
 * from twips (a twip is five hundredths), with no floating point.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_pdf.h"
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    owf_buf content;
    int width, height;           /* twips */
} pdf_page;

typedef struct {
    int page;
    int x, y, w, h;
    char *href;
} pdf_link;

typedef struct {
    const owf_image *image;
    owf_pdf_picture pic;
    int ok, object;
} pdf_pic;

struct owf_pdf {
    pdf_page *pages;
    int npages, cappages;
    pdf_link *links;
    int nlinks, caplinks;
    pdf_pic *pics;
    int npics, cappics;
    int dash;
    int failed;
};

owf_pdf *owf_pdf_new(void)
{
    return calloc(1, sizeof(owf_pdf));
}

void owf_pdf_free(owf_pdf *pdf)
{
    int i;
    if (!pdf)
        return;
    for (i = 0; i < pdf->npages; i++)
        owf_buf_free(&pdf->pages[i].content);
    free(pdf->pages);
    for (i = 0; i < pdf->nlinks; i++)
        free(pdf->links[i].href);
    free(pdf->links);
    for (i = 0; i < pdf->npics; i++)
        owf_pdf_picture_free(&pdf->pics[i].pic);
    free(pdf->pics);
    free(pdf);
}

int owf_pdf_page_count(const owf_pdf *pdf)
{
    return pdf ? pdf->npages : 0;
}

int owf_pdf_page(owf_pdf *pdf, int width, int height)
{
    pdf_page *p;
    if (!pdf || pdf->failed)
        return -1;
    if (pdf->npages == pdf->cappages) {
        int want = pdf->cappages ? pdf->cappages * 2 : 8;
        p = realloc(pdf->pages, (size_t)want * sizeof *p);
        if (!p) {
            pdf->failed = 1;
            return -1;
        }
        pdf->pages = p;
        pdf->cappages = want;
    }
    p = &pdf->pages[pdf->npages++];
    owf_buf_init(&p->content);
    p->width = width > 20 ? width : 20;
    p->height = height > 20 ? height : 20;
    pdf->dash = 0;
    return 0;
}

static pdf_page *current(owf_pdf *pdf)
{
    if (!pdf || pdf->failed || !pdf->npages)
        return NULL;
    return &pdf->pages[pdf->npages - 1];
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

static void put_xy(owf_buf *out, const pdf_page *p, long x, long y)
{
    put_num(out, x);
    owf_buf_putc(out, ' ');
    put_num(out, p->height - y);
    owf_buf_putc(out, ' ');
}

static void put_colour(owf_buf *out, unsigned long rgb, int stroke)
{
    unsigned r = (unsigned)((rgb >> 16) & 255), g = (unsigned)((rgb >> 8) & 255), b = (unsigned)(rgb & 255);
    owf_buf_printf(out, "%u.%03u %u.%03u %u.%03u %s\n", r / 255, (r % 255) * 1000 / 255, g / 255, (g % 255) * 1000 / 255,
                   b / 255, (b % 255) * 1000 / 255, stroke ? "RG" : "rg");
}

/* Fill and line colours, the line's width, and the operator that paints. */
static const char *paint(owf_pdf *pdf, owf_buf *out, unsigned long fill, unsigned long line, int line_width)
{
    int f = OWF_SHADE_SET(fill), l = OWF_SHADE_SET(line);
    if (f)
        put_colour(out, OWF_SHADE_RGB(fill), 0);
    if (l) {
        put_colour(out, OWF_SHADE_RGB(line), 1);
        put_num(out, line_width > 0 ? line_width : 5);
        owf_buf_puts(out, " w\n");
        if (pdf->dash) {
            int w = line_width > 0 ? line_width : 5;
            owf_buf_puts(out, "[");
            put_num(out, w * 4);
            owf_buf_puts(out, " ");
            put_num(out, w * 3);
            owf_buf_puts(out, "] 0 d\n");
        }
    }
    return f && l ? "B\n" : f ? "f\n" : l ? "S\n" : "n\n";
}

void owf_pdf_dash(owf_pdf *pdf, int dash)
{
    if (pdf)
        pdf->dash = dash;
}

int owf_pdf_rect(owf_pdf *pdf, int x, int y, int w, int h, unsigned long fill, unsigned long line, int line_width)
{
    pdf_page *p = current(pdf);
    const char *op;
    if (!p)
        return -1;
    if (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line))
        return 0;
    owf_buf_puts(&p->content, "q\n");
    op = paint(pdf, &p->content, fill, line, line_width);
    put_xy(&p->content, p, x, (long)y + h);
    put_num(&p->content, w);
    owf_buf_putc(&p->content, ' ');
    put_num(&p->content, h);
    owf_buf_puts(&p->content, " re ");
    owf_buf_puts(&p->content, op);
    owf_buf_puts(&p->content, "Q\n");
    return p->content.failed ? -1 : 0;
}

/* A quarter of a circle as a Bezier curve: its handles are 0.5523 of the
 * radius along the tangents. */
#define KAPPA(r) ((long)((long long)(r) * 5523 / 10000))

int owf_pdf_round_rect(owf_pdf *pdf, int x, int y, int w, int h, int r, unsigned long fill, unsigned long line,
                       int line_width)
{
    pdf_page *p = current(pdf);
    owf_buf *o;
    const char *op;
    long k, x2 = (long)x + w, y2 = (long)y + h;
    if (!p)
        return -1;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0)
        return owf_pdf_rect(pdf, x, y, w, h, fill, line, line_width);
    if (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line))
        return 0;
    k = KAPPA(r);
    o = &p->content;
    owf_buf_puts(o, "q\n");
    op = paint(pdf, o, fill, line, line_width);
    put_xy(o, p, x + r, y); owf_buf_puts(o, "m ");
    put_xy(o, p, x2 - r, y); owf_buf_puts(o, "l ");
    put_xy(o, p, x2 - r + k, y); put_xy(o, p, x2, y + r - k); put_xy(o, p, x2, y + r); owf_buf_puts(o, "c ");
    put_xy(o, p, x2, y2 - r); owf_buf_puts(o, "l ");
    put_xy(o, p, x2, y2 - r + k); put_xy(o, p, x2 - r + k, y2); put_xy(o, p, x2 - r, y2); owf_buf_puts(o, "c ");
    put_xy(o, p, x + r, y2); owf_buf_puts(o, "l ");
    put_xy(o, p, x + r - k, y2); put_xy(o, p, x, y2 - r + k); put_xy(o, p, x, y2 - r); owf_buf_puts(o, "c ");
    put_xy(o, p, x, y + r); owf_buf_puts(o, "l ");
    put_xy(o, p, x, y + r - k); put_xy(o, p, x + r - k, y); put_xy(o, p, x + r, y); owf_buf_puts(o, "c h ");
    owf_buf_puts(o, op);
    owf_buf_puts(o, "Q\n");
    return o->failed ? -1 : 0;
}

int owf_pdf_ellipse(owf_pdf *pdf, int x, int y, int w, int h, unsigned long fill, unsigned long line, int line_width)
{
    pdf_page *p = current(pdf);
    owf_buf *o;
    const char *op;
    long rx = w / 2, ry = h / 2, cx = (long)x + rx, cy = (long)y + ry, kx = KAPPA(rx), ky = KAPPA(ry);
    if (!p)
        return -1;
    if (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line))
        return 0;
    o = &p->content;
    owf_buf_puts(o, "q\n");
    op = paint(pdf, o, fill, line, line_width);
    put_xy(o, p, cx + rx, cy); owf_buf_puts(o, "m ");
    put_xy(o, p, cx + rx, cy + ky); put_xy(o, p, cx + kx, cy + ry); put_xy(o, p, cx, cy + ry); owf_buf_puts(o, "c ");
    put_xy(o, p, cx - kx, cy + ry); put_xy(o, p, cx - rx, cy + ky); put_xy(o, p, cx - rx, cy); owf_buf_puts(o, "c ");
    put_xy(o, p, cx - rx, cy - ky); put_xy(o, p, cx - kx, cy - ry); put_xy(o, p, cx, cy - ry); owf_buf_puts(o, "c ");
    put_xy(o, p, cx + kx, cy - ry); put_xy(o, p, cx + rx, cy - ky); put_xy(o, p, cx + rx, cy); owf_buf_puts(o, "c h ");
    owf_buf_puts(o, op);
    owf_buf_puts(o, "Q\n");
    return o->failed ? -1 : 0;
}

int owf_pdf_path(owf_pdf *pdf, const int *xy, int n, int closed, unsigned long fill, unsigned long line, int line_width)
{
    pdf_page *p = current(pdf);
    owf_buf *o;
    const char *op;
    int i;
    if (!p)
        return -1;
    if (n < 2 || !xy || (!OWF_SHADE_SET(fill) && !OWF_SHADE_SET(line)))
        return 0;
    if (!closed)
        fill = 0;
    o = &p->content;
    owf_buf_puts(o, "q\n");
    op = paint(pdf, o, fill, line, line_width);
    for (i = 0; i < n; i++) {
        put_xy(o, p, xy[i * 2], xy[i * 2 + 1]);
        owf_buf_puts(o, i ? "l " : "m ");
    }
    if (closed)
        owf_buf_puts(o, "h ");
    owf_buf_puts(o, op);
    owf_buf_puts(o, "Q\n");
    return o->failed ? -1 : 0;
}

/* ---- Text ---- */

int owf_pdf_winansi(unsigned long c)
{
    static const unsigned short high[32] = {   /* 0x80 to 0x9F */
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
    };
    int i;
    if (c >= 32 && c <= 126)
        return (int)c;
    if (c >= 160 && c <= 255)
        return (int)c;
    for (i = 0; i < 32; i++)
        if (high[i] && high[i] == c)
            return 0x80 + i;
    /* the bullets lists use, as the bullet WinAnsi has */
    if (c == 0x25CF || c == 0x25A0 || c == 0x25AA || c == 0x2219 || c == 0x25E6 || c == 0x25CB || c == 0x2023 ||
        c == 0x25C6 || c == 0x2043 || c == 0xF0B7)
        return 0x95;
    if (c == 0x2212)
        return '-';
    return 0;
}

static int face(owf_font_kind kind, unsigned flags)
{
    int base = kind == OWF_FONT_MONO ? 8 : kind == OWF_FONT_SERIF ? 4 : 0;
    if (flags & OWF_BOLD)
        base += 1;
    if (flags & OWF_ITALIC)
        base += 2;
    return base;      /* 0-3 Helvetica, 4-7 Times, 8-11 Courier: /F1 to /F12 */
}

static long natural_width(const char *s, size_t n, int f, int size)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + n;
    long long units = 0, w;
    while (p < end) {
        unsigned long c = owf_utf8_next(&p, end);
        int b = owf_pdf_winansi(c);
        if (c == '\t' || c == '\n' || c == '\r')
            continue;
        if (!b)
            b = '?';
        units += f >= 8 ? 600 : owf_pdf_widths[f][b - 32];
    }
    w = (units * size + 500) / 1000;
    return (long)(w > 100000000 ? 100000000 : w);     /* past any page */
}

int owf_pdf_text_width(const char *utf8, size_t length, owf_font_kind kind, unsigned flags, int size)
{
    return (int)natural_width(utf8, length, face(kind, flags), size);
}

int owf_pdf_text(owf_pdf *pdf, int x, int baseline, int width, const char *utf8, size_t length,
                 owf_font_kind kind, unsigned flags, int size, unsigned long colour)
{
    pdf_page *p = current(pdf);
    owf_buf *o;
    const unsigned char *s = (const unsigned char *)utf8, *end = s + length;
    int f = face(kind, flags), any = 0;
    long natural;
    if (!p)
        return -1;
    if (!utf8 || !length || size <= 0)
        return 0;
    natural = natural_width(utf8, length, f, size);
    o = &p->content;
    owf_buf_printf(o, "BT /F%d ", f + 1);
    put_num(o, size);
    owf_buf_puts(o, " Tf ");
    put_colour(o, colour & 0xFFFFFFUL, 0);
    if (width > 0 && natural > 0) {
        long tz = (long)((long long)width * 1000 / natural);     /* tenths of a percent */
        if (tz < 100) tz = 100;
        if (tz > 10000) tz = 10000;
        if (tz < 995 || tz > 1005)
            owf_buf_printf(o, "%ld.%ld Tz ", tz / 10, tz % 10);
    }
    put_xy(o, p, x, baseline);
    owf_buf_puts(o, "Td (");
    while (s < end) {
        unsigned long c = owf_utf8_next(&s, end);
        int b;
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
        b = owf_pdf_winansi(c);
        if (!b)
            b = '?';
        if (b == '(' || b == ')' || b == '\\')
            owf_buf_printf(o, "\\%c", b);
        else if (b >= 127)
            owf_buf_printf(o, "\\%03o", b);
        else
            owf_buf_putc(o, b);
        any = 1;
    }
    owf_buf_puts(o, ") Tj ET\n");
    if (any && (flags & (OWF_UNDERLINE | OWF_STRIKE))) {
        long w = width > 0 ? width : natural, t = size / 18 > 5 ? size / 18 : 5;
        owf_buf_puts(o, "q\n");
        put_colour(o, colour & 0xFFFFFFUL, 1);
        put_num(o, t);
        owf_buf_puts(o, " w\n");
        if (flags & OWF_UNDERLINE) {
            put_xy(o, p, x, baseline + size / 8);
            owf_buf_puts(o, "m ");
            put_xy(o, p, x + w, baseline + size / 8);
            owf_buf_puts(o, "l S\n");
        }
        if (flags & OWF_STRIKE) {
            put_xy(o, p, x, baseline - size * 3 / 10);
            owf_buf_puts(o, "m ");
            put_xy(o, p, x + w, baseline - size * 3 / 10);
            owf_buf_puts(o, "l S\n");
        }
        owf_buf_puts(o, "Q\n");
    }
    return o->failed ? -1 : 0;
}

/* ---- Pictures and links ---- */

int owf_pdf_image(owf_pdf *pdf, const owf_image *image, int x, int y, int w, int h, const int *clip)
{
    pdf_page *p = current(pdf);
    owf_buf *o;
    int i;
    if (!p)
        return -1;
    if (!image || w <= 0 || h <= 0)
        return 0;
    for (i = 0; i < pdf->npics; i++)
        if (pdf->pics[i].image == image)
            break;
    if (i == pdf->npics) {
        if (pdf->npics == pdf->cappics) {
            int want = pdf->cappics ? pdf->cappics * 2 : 8;
            pdf_pic *q = realloc(pdf->pics, (size_t)want * sizeof *q);
            if (!q) {
                pdf->failed = 1;
                return -1;
            }
            pdf->pics = q;
            pdf->cappics = want;
        }
        memset(&pdf->pics[i], 0, sizeof pdf->pics[i]);
        pdf->pics[i].image = image;
        /* only JPEG and PNG: the others' bytes are not looked at again */
        pdf->pics[i].ok = owf_pdf_encode_picture(image, &pdf->pics[i].pic);
        pdf->npics++;
    }
    if (!pdf->pics[i].ok)
        return 0;
    o = &p->content;
    owf_buf_puts(o, "q\n");
    if (clip) {
        put_xy(o, p, clip[0], (long)clip[1] + clip[3]);
        put_num(o, clip[2]);
        owf_buf_putc(o, ' ');
        put_num(o, clip[3]);
        owf_buf_puts(o, " re W n\n");
    }
    put_num(o, w);
    owf_buf_puts(o, " 0 0 ");
    put_num(o, h);
    owf_buf_putc(o, ' ');
    put_xy(o, p, x, (long)y + h);
    owf_buf_printf(o, "cm /Im%d Do Q\n", i + 1);
    return o->failed ? -1 : 0;
}

int owf_pdf_link(owf_pdf *pdf, int x, int y, int w, int h, const char *href)
{
    pdf_link *l;
    if (!current(pdf))
        return -1;
    if (!href || !*href)
        return 0;
    if (pdf->nlinks == pdf->caplinks) {
        int want = pdf->caplinks ? pdf->caplinks * 2 : 8;
        l = realloc(pdf->links, (size_t)want * sizeof *l);
        if (!l) {
            pdf->failed = 1;
            return -1;
        }
        pdf->links = l;
        pdf->caplinks = want;
    }
    l = &pdf->links[pdf->nlinks];
    l->href = malloc(strlen(href) + 1);
    if (!l->href) {
        pdf->failed = 1;
        return -1;
    }
    strcpy(l->href, href);
    l->page = pdf->npages - 1;
    l->x = x;
    l->y = y;
    l->w = w;
    l->h = h;
    pdf->nlinks++;
    return 0;
}

/* ---- The file ---- */

static void put_string(owf_buf *out, const char *s)
{
    owf_buf_putc(out, '(');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '(' || c == ')' || c == '\\')
            owf_buf_printf(out, "\\%c", c);
        else if (c < 32 || c >= 127)
            owf_buf_printf(out, "\\%03o", c);
        else
            owf_buf_putc(out, c);
    }
    owf_buf_putc(out, ')');
}

/* A title as PDF text: UTF-16BE with its byte-order mark. */
static void put_text_string(owf_buf *out, const char *utf8)
{
    const unsigned char *p = (const unsigned char *)utf8, *end = p + strlen(utf8);
    owf_buf_puts(out, "<FEFF");
    while (p < end) {
        unsigned long c = owf_utf8_next(&p, end);
        if (c > 0xFFFF) {
            c -= 0x10000;
            owf_buf_printf(out, "%04lX%04lX", 0xD800 + (c >> 10), 0xDC00 + (c & 0x3FF));
        } else {
            owf_buf_printf(out, "%04lX", c);
        }
    }
    owf_buf_putc(out, '>');
}

static const char *const font_names[12] = {
    "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
    "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
    "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"
};

int owf_pdf_finish(owf_pdf *pdf, const char *title, unsigned char **data, size_t *length, owf_report *report)
{
    owf_buf out;
    unsigned long *offs;
    int nobj, i, j, page_obj, pic_obj, link_obj, info_obj, lost = 0;

    *data = NULL;
    *length = 0;
    if (!pdf || pdf->failed)
        return OWF_ERR_MEMORY;
    if (!pdf->npages && owf_pdf_page(pdf, 11906, 16838) < 0)
        return OWF_ERR_MEMORY;
    for (i = 0; i < pdf->npages; i++)
        if (pdf->pages[i].content.failed)
            return OWF_ERR_MEMORY;
    /* 1 catalog, 2 pages, 3-14 fonts, then each page's content and page,
       the pictures (and their masks), the links, the information */
    page_obj = 15;
    pic_obj = page_obj + pdf->npages * 2;
    nobj = pic_obj;
    for (i = 0; i < pdf->npics; i++) {
        if (!pdf->pics[i].ok) {
            lost = 1;
            continue;
        }
        pdf->pics[i].object = nobj;
        nobj += pdf->pics[i].pic.mask ? 2 : 1;
    }
    if (lost)
        owf_report_add(report, OWF_NOTE_LOST, "Pictures in a format the PDF export does not draw yet (only JPEG and PNG) are left out");
    link_obj = nobj;
    nobj += pdf->nlinks;
    info_obj = nobj++;
    offs = calloc((size_t)nobj, sizeof *offs);
    if (!offs)
        return OWF_ERR_MEMORY;
    owf_buf_init(&out);
    owf_buf_puts(&out, "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
#define OBJ(n) do { offs[(n)] = (unsigned long)out.length; owf_buf_printf(&out, "%d 0 obj\n", (n)); } while (0)
    OBJ(1);
    owf_buf_puts(&out, "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    OBJ(2);
    owf_buf_printf(&out, "<< /Type /Pages /Count %d /Kids [", pdf->npages);
    for (i = 0; i < pdf->npages; i++)
        owf_buf_printf(&out, " %d 0 R", page_obj + i * 2 + 1);
    owf_buf_puts(&out, " ] >>\nendobj\n");
    for (i = 0; i < 12; i++) {
        OBJ(3 + i);
        owf_buf_printf(&out, "<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>\nendobj\n",
                       font_names[i]);
    }
    for (i = 0; i < pdf->npages; i++) {
        const pdf_page *p = &pdf->pages[i];
        int any = 0;
        OBJ(page_obj + i * 2);
        owf_buf_printf(&out, "<< /Length %lu >>\nstream\n", (unsigned long)p->content.length);
        owf_buf_put(&out, p->content.data, p->content.length);
        owf_buf_puts(&out, "endstream\nendobj\n");
        OBJ(page_obj + i * 2 + 1);
        owf_buf_puts(&out, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
        put_num(&out, p->width);
        owf_buf_putc(&out, ' ');
        put_num(&out, p->height);
        owf_buf_puts(&out, "] /Resources << /Font <<");
        for (j = 0; j < 12; j++)
            owf_buf_printf(&out, " /F%d %d 0 R", j + 1, 3 + j);
        owf_buf_puts(&out, " >>");
        for (j = 0; j < pdf->npics; j++)
            if (pdf->pics[j].ok) {
                if (!any)
                    owf_buf_puts(&out, " /XObject <<");
                any = 1;
                owf_buf_printf(&out, " /Im%d %d 0 R", j + 1, pdf->pics[j].object);
            }
        if (any)
            owf_buf_puts(&out, " >>");
        owf_buf_puts(&out, " >>");
        any = 0;
        for (j = 0; j < pdf->nlinks; j++)
            if (pdf->links[j].page == i) {
                owf_buf_puts(&out, any ? " " : " /Annots [");
                owf_buf_printf(&out, "%d 0 R", link_obj + j);
                any = 1;
            }
        if (any)
            owf_buf_puts(&out, "]");
        owf_buf_printf(&out, " /Contents %d 0 R >>\nendobj\n", page_obj + i * 2);
    }
    for (i = 0; i < pdf->npics; i++) {
        const owf_pdf_picture *im = &pdf->pics[i].pic;
        if (!pdf->pics[i].ok)
            continue;
        OBJ(pdf->pics[i].object);
        owf_buf_printf(&out, "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /%s"
                             " /BitsPerComponent 8 /Filter /%s /Length %lu",
                       im->w, im->h, im->gray ? "DeviceGray" : "DeviceRGB", im->dct ? "DCTDecode" : "FlateDecode",
                       (unsigned long)im->len);
        if (im->mask)
            owf_buf_printf(&out, " /SMask %d 0 R", pdf->pics[i].object + 1);
        owf_buf_puts(&out, " >>\nstream\n");
        owf_buf_put(&out, im->data, im->len);
        owf_buf_puts(&out, "\nendstream\nendobj\n");
        if (im->mask) {
            OBJ(pdf->pics[i].object + 1);
            owf_buf_printf(&out, "<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray"
                                 " /BitsPerComponent 8 /Filter /FlateDecode /Length %lu >>\nstream\n",
                           im->w, im->h, (unsigned long)im->mlen);
            owf_buf_put(&out, im->mask, im->mlen);
            owf_buf_puts(&out, "\nendstream\nendobj\n");
        }
    }
    for (i = 0; i < pdf->nlinks; i++) {
        const pdf_link *l = &pdf->links[i];
        const pdf_page *p = &pdf->pages[l->page];
        OBJ(link_obj + i);
        owf_buf_puts(&out, "<< /Type /Annot /Subtype /Link /Rect [");
        put_xy(&out, p, l->x, (long)l->y + l->h);
        put_xy(&out, p, (long)l->x + l->w, l->y);
        owf_buf_puts(&out, "] /Border [0 0 0] /A << /S /URI /URI ");
        put_string(&out, l->href);
        owf_buf_puts(&out, " >> >>\nendobj\n");
    }
    OBJ(info_obj);
    owf_buf_puts(&out, "<< /Producer (OpenPresentation libowf)");
    if (title && *title) {
        owf_buf_puts(&out, " /Title ");
        put_text_string(&out, title);
    }
    owf_buf_puts(&out, " >>\nendobj\n");
#undef OBJ
    {
        unsigned long xref = (unsigned long)out.length;
        owf_buf_printf(&out, "xref\n0 %d\n0000000000 65535 f \n", nobj);
        for (i = 1; i < nobj; i++)
            owf_buf_printf(&out, "%010lu 00000 n \n", offs[i]);
        owf_buf_printf(&out, "trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%lu\n%%%%EOF\n", nobj,
                       info_obj, xref);
    }
    free(offs);
    if (out.failed) {
        owf_buf_free(&out);
        return OWF_ERR_MEMORY;
    }
    return owf_buf_take(&out, data, length);
}

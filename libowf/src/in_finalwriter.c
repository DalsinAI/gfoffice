/*
 * libowf: Final Writer and Final Copy documents (SoftWood), IFF FORM SWRT.
 *
 * Final Writer's format is not published. This reader follows what we found
 * in real documents from Aminet (docs/formats/finalwriter.md), building on
 * fw2odf's findings (MIT, Charles Horn) and with EvenMore's plugin read for
 * reference only:
 *   FDTA  a font's name; ATTR's font number counts from the last FDTA
 *   TXOB  a text frame: font name at 4, size at 147, text at 178 with its
 *         length (word) at 176
 *   TBDY  the body starts; text before it is in table cells (CLLE)
 *   RULE  a paragraph starts (24 bytes, mostly not understood yet)
 *   ATTR  the next CHRS's format: length (long), font (word), size at 7,
 *         underline in bit 0 of 9; byte 11 is 1 for a tab, 17 for an
 *         endnote mark with its number at 12
 *   CHRS  text, ISO-8859-1; in the Symbol font, Symbol's characters
 *   RMST, LMST  the right and left master pages: the right one's text
 *         becomes the header (labels keep everything there)
 * Bold and italic come from the font's name ("SoftSans_Bold").
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FONTS 256

typedef struct {
    char name[64];
    int font;                /* document font */
    unsigned style;          /* OWF_BOLD and OWF_ITALIC from the name */
    int symbol;
} fw_font;

static int contains(const char *s, const char *word)
{
    size_t n = strlen(word);
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < n && s[i] && (s[i] | 0x20) == (word[i] | 0x20); i++)
            ;
        if (i == n)
            return 1;
    }
    return 0;
}

/* "SoftSans_Bold" is SoftSans, bold. */
static void name_font(owf_doc *doc, fw_font *f, const unsigned char *name, unsigned long max)
{
    unsigned long n = 0;
    char family[64];
    char *cut;
    owf_font_kind kind = OWF_FONT_ANY;

    while (n < max && name[n] && n < sizeof f->name - 1) {
        f->name[n] = (char)name[n];
        n++;
    }
    f->name[n] = 0;
    f->style = (contains(f->name, "bold") ? OWF_BOLD : 0) | (contains(f->name, "italic") || contains(f->name, "oblique") ? OWF_ITALIC : 0);
    f->symbol = contains(f->name, "symbol");
    strcpy(family, f->name);
    for (cut = family; *cut; cut++)
        if (*cut == '_' && (contains(cut, "bold") || contains(cut, "italic") || contains(cut, "oblique") ||
                            contains(cut, "roman") || contains(cut, "regular")))
            break;
    *cut = 0;
    if (!strncmp(family, "IF_", 3))
        memmove(family, family + 3, strlen(family + 3) + 1);
    if (!family[0])
        strcpy(family, "SoftSans");
    owf_font_modern(family, &kind);
    if (contains(family, "sans"))
        kind = OWF_FONT_SANS;
    else if (contains(family, "serif") && kind == OWF_FONT_ANY)
        kind = OWF_FONT_SERIF;
    f->font = owf_doc_font(doc, family, kind);
}

static void text(owf_builder *b, owf_buf *scratch, const unsigned char *s, unsigned long n, int symbol)
{
    unsigned long i, start = 0;
    scratch->length = 0;
    for (i = 0; i < n; i++) {
        unsigned c = s[i];
        if (c == '\t' || c == '\n' || c == '\r' || c < 0x20) {
            if (symbol) {
                for (; start < i; start++)
                    owf_buf_put_utf8(scratch, owf_symbol_char(s[start]));
            } else {
                owf_buf_put_latin1(scratch, s + start, i - start);
            }
            if (scratch->length)
                owf_builder_text(b, (const char *)scratch->data, scratch->length);
            scratch->length = 0;
            if (c == '\t')
                owf_builder_special(b, OWF_RUN_TAB, OWF_FIELD_PAGE);
            else if (c == '\n' || c == '\r')
                owf_builder_special(b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
            start = i + 1;
        }
    }
    if (symbol) {
        for (; start < n; start++)
            owf_buf_put_utf8(scratch, owf_symbol_char(s[start]));
    } else {
        owf_buf_put_latin1(scratch, s + start, n - start);
    }
    if (scratch->failed)
        b->failed = 1;
    else if (scratch->length)
        owf_builder_text(b, (const char *)scratch->data, scratch->length);
}

static int detect_finalwriter(const unsigned char *data, size_t length)
{
    return owf_iff_is_form(data, length, OWF_ID('S', 'W', 'R', 'T')) ? 100 : 0;
}

static int import_finalwriter(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_iff_reader iff;
    owf_iff_chunk c;
    fw_font *fonts;
    int nfonts = 0, master = 0, master_text = 0, cells = 0, in_body = 0, frames = 0, pictures = 0, endnotes = 0, result;
    int body_open = 0, header_open = 0, *open;   /* a RULE began a paragraph not yet ended */
    const unsigned char *attr = NULL;
    owf_builder body, header, *bp;
    owf_buf scratch;

    if (!owf_iff_open(&iff, data, length, OWF_ID('S', 'W', 'R', 'T')))
        return OWF_ERR_FORMAT;
    fonts = calloc(MAX_FONTS, sizeof *fonts);
    if (!fonts)
        return OWF_ERR_MEMORY;
    /* The fonts first: ATTR counts them from the end. */
    while (owf_iff_next(&iff, &c))
        if (c.id == OWF_ID('F', 'D', 'T', 'A') && nfonts < MAX_FONTS)
            name_font(doc, &fonts[nfonts++], c.data, c.size);

    owf_iff_open(&iff, data, length, OWF_ID('S', 'W', 'R', 'T'));
    owf_builder_init(&body, doc, &doc->body);
    owf_builder_init(&header, doc, &doc->header);
    owf_buf_init(&scratch);
    bp = &body;
    open = &body_open;
    while (owf_iff_next(&iff, &c)) {
        switch (c.id) {
        case OWF_ID('T', 'X', 'O', 'B'):
            /* A text frame: its text becomes a paragraph of its own. */
            if (bp && c.size >= 178) {
                unsigned long n = OWF_BE16(c.data + 176);
                fw_font f;
                if (n > c.size - 178)
                    n = c.size - 178;
                if (n) {
                    name_font(doc, &f, c.data + 4, 140);
                    if (bp->para || bp->text.length)
                        owf_builder_end_para(bp);
                    owf_charfmt_init(&bp->charfmt);
                    bp->charfmt.font = f.font;
                    bp->charfmt.size = c.data[147] ? c.data[147] * OWF_TWIPS_PER_POINT : 0;
                    bp->charfmt.flags = f.style;
                    text(bp, &scratch, c.data + 178, n, f.symbol);
                    owf_builder_end_para(bp);
                    frames++;
                }
            }
            break;
        case OWF_ID('T', 'B', 'D', 'Y'):
            in_body = 1;
            break;
        case OWF_ID('C', 'L', 'L', 'E'):
            if (!in_body)
                cells = 1;
            break;
        case OWF_ID('R', 'M', 'S', 'T'):
        case OWF_ID('L', 'M', 'S', 'T'):
            if (bp && (*open || bp->para || bp->text.length))
                owf_builder_end_para(bp);
            if (bp)
                *open = 0;
            master = c.id == OWF_ID('R', 'M', 'S', 'T') ? 1 : 2;
            bp = master == 1 ? &header : NULL;
            open = &header_open;
            break;
        case OWF_ID('R', 'U', 'L', 'E'):
            /* Every RULE is a paragraph, blank lines too. */
            if (bp) {
                if (*open || bp->para || bp->text.length)
                    owf_builder_end_para(bp);
                owf_parafmt_init(&bp->parafmt);
                *open = 1;
            }
            break;
        case OWF_ID('A', 'T', 'T', 'R'):
            attr = c.size >= 22 ? c.data : NULL;
            break;
        case OWF_ID('C', 'H', 'R', 'S'):
            if (master == 2 && c.size)
                master_text = 1;
            if (bp && c.size) {
                owf_charfmt f;
                int symbol = 0;
                owf_charfmt_init(&f);
                if (attr) {
                    unsigned index = OWF_BE16(attr + 4);
                    if (nfonts && index < (unsigned)nfonts) {
                        const fw_font *ff = &fonts[nfonts - 1 - index];
                        f.font = ff->font;
                        f.flags |= ff->style;
                        symbol = ff->symbol;
                    }
                    if (attr[7])
                        f.size = attr[7] * OWF_TWIPS_PER_POINT;
                    if (attr[9] & 1)
                        f.flags |= OWF_UNDERLINE;
                }
                if (memcmp(&f, &bp->charfmt, sizeof f) != 0) {
                    owf_builder_flush(bp);
                    bp->charfmt = f;
                }
                if (attr && attr[11] == 17) {
                    /* An endnote's mark: its number, raised. */
                    char mark[16];
                    owf_builder_flush(bp);
                    bp->charfmt.flags |= OWF_SUPER;
                    snprintf(mark, sizeof mark, "%lu", OWF_BE32(attr + 12));
                    owf_builder_text(bp, mark, strlen(mark));
                    owf_builder_flush(bp);
                    bp->charfmt.flags &= ~OWF_SUPER;
                    endnotes++;
                } else {
                    text(bp, &scratch, c.data, c.size, symbol);
                }
            }
            attr = NULL;
            break;
        case OWF_ID('F', 'O', 'R', 'M'):
            pictures++;
            break;
        default:
            break;
        }
    }
    if (body_open || body.para || body.text.length)
        owf_builder_end_para(&body);
    if (header_open || header.para || header.text.length)
        owf_builder_end_para(&header);
    if (frames)
        owf_report_add(report, OWF_NOTE_APPROX, "%d text frame(s) became paragraphs at the start", frames);
    if (pictures)
        owf_report_add(report, OWF_NOTE_LOST, "%d picture(s) are not brought across yet", pictures);
    if (endnotes)
        owf_report_add(report, OWF_NOTE_LOST, "Endnote marks are kept; the notes' text is not brought across yet");
    if (cells)
        owf_report_add(report, OWF_NOTE_APPROX, "Text in table cells became paragraphs");
    if (doc->header.nparas)
        owf_report_add(report, OWF_NOTE_APPROX, "The master page's text became the header");
    if (master_text)
        owf_report_add(report, OWF_NOTE_LOST, "The left-hand master page was left out");
    if (iff.truncated)
        owf_report_add(report, OWF_NOTE_LOST, "The file is cut short; the text up to the cut was read");
    owf_report_add(report, OWF_NOTE_INFO, "Final Writer's format is not published: alignment, indents and tab stops use the defaults");
    owf_buf_free(&scratch);
    free(fonts);
    result = owf_builder_done(&body);
    if (owf_builder_done(&header) != OWF_OK)
        result = OWF_ERR_MEMORY;
    return result;
}

const owf_format owf_format_finalwriter = {
    "finalwriter", "Final Writer and Final Copy (SoftWood, IFF SWRT)", "fw", detect_finalwriter, import_finalwriter, NULL
};

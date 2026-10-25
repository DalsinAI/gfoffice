# OpenWrite: formats

Version 1.0 | 7 October 2026 | AmigaChrome Team, MIT

Every format OpenWrite opens or saves, what we know about it, and where that
knowledge comes from. The design is in [../DESIGN.md](../DESIGN.md), section 4.

**Where the knowledge comes from:**
- **Spec:** a published specification.
- **Sample:** documents we make ourselves with the original program in the
  format lab (DESIGN.md, section 4.2), and our own old documents.
- **Reference:** someone else's converter, read for understanding only. Code
  is reused only when its licence allows it, with credit.

**State:** *To do* until a filter is written; then *Text* (the text comes
back), *Formatting* (paragraphs and character formatting) and *Full* (all
that the format holds and OpenWrite can show).

## 1. Today's documents

| Format | Open | Save | Knowledge | Phase | State |
| --- | --- | --- | --- | --- | --- |
| ODT, OpenDocument Text (`.odt`), its template (`.ott`) and flat XML (`.fodt`) | Yes | Yes | Spec: OASIS ODF 1.2 and 1.3, ISO/IEC 26300 | W1 | Formatting both ways: styles through parents, paragraphs, character formatting, tabs, headers, footers, fields and page setup. Simple tables are native editable tables and inline pictures are embedded; lists remain represented by their visible numbers/bullets; notes are appended to the document |
| DOCX, Office Open XML (`.docx`) and its template (`.dotx`) | Yes | Yes | Spec: ECMA-376 / ISO/IEC 29500 (transitional and strict) | W1 | Formatting both ways: styles/theme fonts, paragraphs, character formatting, tabs, headers, footers, fields, hyperlinks, page setup, simple native tables and inline embedded pictures. Tracked changes are accepted on import; notes are appended. Unsupported advanced Office objects are reported |
| RTF (`.rtf`) | No | No | Spec: Microsoft's RTF 1.9.1 | Later | To do |
| HTML (`.html`) | No | Yes | Spec: HTML and CSS | W1 | Export with document formatting; HTML import is reserved for the OpenHTML/html.datatype work |
| Plain text (`.txt`), Amiga ISO-8859-1 or UTF-8 | Yes | Yes | n/a | W1 | Full (libowf 0.1) |
| Markdown (`.md`) | No | No | Spec: CommonMark (a subset) | Later | To do |
| PDF | No | Yes, and print | Direct OpenWrite/libowf PDF exporter | W1 | Searchable PDF export is live; Print hands that PDF to OpenPrint (printer.device's text only without OpenPrint) |
| Word 97-2003 (`.doc`) | Later | No | Spec: Microsoft's [MS-DOC] | Later | Recognised (and password-protected Office files), with a clear message; not read yet |
| AbiWord (`.abw`), which was on AmigaOS 4 | No | No | Its XML, read from samples | Later | To do |

**Zip packages and z.library.** ODT, DOCX and the zip workbooks (XLSX, ODS)
are zip packages whose parts are nearly always deflated. libowf inflates
them through z.library when it is in LIBS:, and otherwise with its own
inflater (`libowf/src/owf_inflate.c`, written from RFC 1951), so none of
them needs z.library to be read. Saving still compresses only through
z.library: without it the parts are stored, which every reader accepts.

## 1a. Workbooks (the spreadsheet's)

Workbooks have their own model in libowf (`owf_book`: sheets of cells, each
with a value, a formula as Excel writes it, and a style) and their own table
of formats (`owf_book_format_*`). OWConvert converts them when the name it is
given ends in a workbook format's extension.

| Format | Open | Save | Knowledge | Phase | State |
| --- | --- | --- | --- | --- | --- |
| Excel 97-2003 (`.xls`, `.xlt`) | Yes | Later | Spec: Microsoft's [MS-XLS] (BIFF8) in [MS-CFB] compound files | S1 | Cells and values, shared strings (Unicode), fonts, number formats, alignment, borders, fills, column widths, row heights, merged cells, frozen panes, hidden sheets and columns, defined names, and formulas back as Excel's text (shared, array, 3-D and other-book references, add-in functions), with their last results. Comments, pictures, charts, conditional formatting, validation and data tables are reported. Excel 5/95 and 2-4 workbooks and password-protected ones are recognised with a clear message |
| CSV (`.csv`) | Later | Yes | Spec: RFC 4180 | S1 | Saves one sheet's values, UTF-8 |
| XLSX, Office Open XML | Later | Later | Spec: ECMA-376 | S2 | To do |
| ODS, OpenDocument Spreadsheet | Later | Later | Spec: OASIS ODF 1.2 | S2 | To do |

## 2. Amiga text formats

| Format | Open | Save | Knowledge | Phase | State |
| --- | --- | --- | --- | --- | --- |
| IFF FTXT, the Amiga's formatted text and its clipboard format | Yes | Yes (clipboard) | Spec: EA IFF 85; `CHRS` text with ISO 6429 style codes, `FONS` fonts | W2 | Formatting (libowf 0.1, from the spec; to check against files from real programs) |
| AmigaGuide (`.guide`) | No | No | Spec: AmigaGuide's `@node`, `@{b}`, links | Later | To do |
| ANSI text, with the Amiga console's style and colour codes | Yes | No | Spec: ECMA-48 | W2 | Formatting (libowf 0.1) |

## 3. Amiga word processors

The list we know of. More will turn up on old disks, and each gets a row
here.

| Program | Maker, years | Container | What we know | Phase | State |
| --- | --- | --- | --- | --- | --- |
| ProWrite | New Horizons Software, 1987-1993 | IFF `FORM WORD` | Spec (New Horizons, 1987): `FONT`, `COLR`, `DOC`, `HEAD`, `FOOT`, `PARA`, `TABS`, `PAGE`, `TEXT`, `FSCC` (font, style and colour changes), `PCTS`/`PINF` with an ILBM `BODY` for pictures; measures in decipoints (1/720 inch) | W2 | Formatting (libowf 0.1, from the spec; pictures not yet). To confirm with real files: the style bits, what the right margin is measured from, where tab positions start |
| QuickWrite | New Horizons Software | Probably IFF `FORM WORD` | To confirm from a sample | W2 | To do |
| Final Writer, Final Copy and Final Copy II | SoftWood, 1990-1997 | IFF `FORM SWRT` | Real documents from Aminet; fw2odf (MIT) for the fonts and `ATTR`; notes in [formats/finalwriter.md](formats/finalwriter.md) | W2 | Text and character formatting (libowf 0.1): fonts, sizes, bold and italic, underline, tabs, text frames, table cells, master-page text as the header, the Symbol font. Alignment, indents and tab positions not yet |
| Wordworth | Digita, 1992-1999 | IFF `FORM WOWO` | Real documents from Aminet (a 300 KB manual among them); notes in [formats/wordworth.md](formats/wordworth.md) | W2 | Text and character formatting (libowf 0.1): fonts, sizes, bold, italic, underline, colours, centred paragraphs, page breaks, headers and footers. Indents, margins and tab positions not yet |
| Excellence! and Scribble! | Micro-Systems Software | Their own | Sample | W2 | To do |
| Kindwords | The Disc Company, 1987 | Its own | Sample | W2 | To do |
| Textcraft and Textcraft Plus | Arktronics for Commodore, 1985-1986 | Their own | Sample | W2 | To do |
| Pen Pal | SoftWood, 1990 | Its own | Sample | W2 | To do |
| Protext | Arnor | Text with embedded commands | Sample, and its manual | W2 | To do |
| BeckerText | Data Becker | Its own | Sample | W2 | To do |
| WordPerfect for the Amiga | WordPerfect Corporation, 1987-1989 | WordPerfect 4.x | Believed close to WordPerfect 4.2 for DOS. Reference: libwpd. Sample | W2 | To do |
| AmigaWriter | Haage & Partner, about 2000-2001 (2.2) | To find out | Sample. A demo (2.20) is on Aminet as `text/dtp/AW2.20-Demo.lha` | W2 | To do |
| Desktop publishing: PageStream, Professional Page, PageSetter | Soft-Logik, Gold Disk | Their own | Text and pictures only, later | Later | To do |

## 4. How a new Amiga format gets in

1. Make the samples in the format lab: the standard test document, then one
   change per file (bold on, a font, a tab stop, a header, a picture). If the
   program can also save RTF or text, save that copy too.
2. Write down what each chunk or record holds, in `docs/formats/<format>.md`.
3. Write the importer against the samples; the samples and their expected
   result go into the tests.
4. Set this table's state, and the import report lists anything the filter
   cannot bring across.

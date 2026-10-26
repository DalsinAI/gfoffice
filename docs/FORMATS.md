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
| CSV (`.csv`) | Yes | Yes | Spec: RFC 4180 | S1 | Saves one sheet's values, UTF-8. Opens comma, semicolon and tab files (Excel's `sep=` line too) in UTF-8, UTF-16 or Windows-1252: numbers, percentages, ISO dates and times, TRUE and FALSE, error names, text |
| XLSX, Office Open XML (`.xlsx`; `.xltx`, `.xlsm`, `.xltm` open only) | Yes | Yes | Spec: ECMA-376 (transitional; strict's namespaces read) and [MS-XLSX]'s `_xlfn.` names | S2 | Full both ways, as the model holds it (notes below). Macros, comments, pictures, charts, conditional formats, validation, tables and pivot tables are reported. Binary `.xlsb` is recognised with a clear message |
| ODS, OpenDocument Spreadsheet (`.ods`, `.ots` opens; flat `.fods`) | Yes | Yes | Spec: OASIS ODF 1.2 and OpenFormula; LibreOffice's 1.3 additions read | S2 | Full both ways, as the model holds it (notes below), formulas translated to and from OpenFormula. Comments, pictures, charts, conditional formats and validation are reported |

What each keeps, both ways: sheets and their names; values (numbers,
text, booleans, errors; XLSX's shared strings and rich text flattened);
formulas as Excel's text with their last results; cell styles (fonts,
sizes, bold, italic, underline, strike-through, raised and lowered text,
colours, number formats, alignment, wrap, indents, fills, borders); column
widths, row heights, hidden rows, columns and sheets; merged cells; array
formulas; frozen panes; defined names; the active sheet; the 1904 date
system. Numbers are written in the fewest digits that read back as the same
double (up to 17), and read exactly.

- **XLSX.** Shared formulas are moved to each of their cells on reading;
  array formulas' ranges come back on each cell. Excel 2010's and later
  functions carry their `_xlfn.` names in the file and not in the model.
  Other workbooks named in formulas (`[Other.xls]Rates!A1`) become
  numbered links with externalLink parts, and back. Theme colours and
  their tints are worked out. Sheet names Excel does not allow (over 31
  characters, `: \ / ? * [ ]`) are changed on saving, and said so.
- **ODS and flat ODS.** Formulas are OpenFormula in the file
  (`of:=SUM([.A1:.A3])`) and Excel's text in the model: references,
  sheets, 3-D ranges, other files, unions (`~`), intersections (`!`),
  array constants, `TRUE()`, and the functions the two name differently
  (`COM.MICROSOFT.`, `LEGACY.`, `FDIST`/`F.DIST`...). Number formats are
  ODF's data styles, sections as LibreOffice maps them; libowf also keeps
  Excel's text of each format, and a sheet's default column width and row
  height, in attributes of its own namespace, which other programs ignore
  and libowf reads back, so a workbook goes through ODS unchanged. ODF
  cannot hold names of one sheet or of another workbook in formulas, hidden
  names, very hidden sheets, or text centred across cells: those are made
  the nearest ODF has, and reported. Dates counted from a null date other
  than 1899-12-30 or 1904-01-01 are reported.
- **CSV.** A field starting with `=` stays text: CSV is data. Fields CSV
  cannot tell apart stay what they look like (the text `007` is the number
  7).

## 1b. Presentations (OpenPresentation's)

Decks have their own model in libowf (`owf_deck`, in
`libowf/include/owf_deck.h`: slides, masters with their text levels and
placeholders, layouts, objects, notes and transitions; a frame's text is an
`owf_story`) and their own table of formats (`owf_deck_format_*`). The
design is [gfoffice/Design-OpenPresentation.md](gfoffice/Design-OpenPresentation.md);
what Impress does and where we are is
[gfoffice/Impress-Parity.md](gfoffice/Impress-Parity.md). OWConvert converts
a deck when the file it is given is one (by its contents).

| Format | Open | Save | Knowledge | Phase | State |
| --- | --- | --- | --- | --- | --- |
| ODP, OpenDocument Presentation (`.odp`, `.otp`; flat `.fodp`) | Yes | Yes | Spec: OASIS ODF 1.2 (presentation and drawing); LibreOffice's ODF 1.3 `style:shrink-to-fit` read and written | P1 | Masters and their presentation styles (title, subtitle, outline levels with their bullets, notes), layouts, slides, text frames, pictures (cropped too), tables, rectangles, rounded rectangles, ellipses, lines and arrows, polygons, the custom shapes we model (triangle, diamond, block arrow), groups, notes, footer, date and number fields, backgrounds, hidden slides, transitions and timings. Kept and written back as they were: other shapes, connectors, embedded charts and objects with their parts, animations, rare transitions, custom shows and the show's settings. Gradients and hatching are drawn in one colour |
| PDF | No | Export | Our page-drawing PDF writer (`owf_pdf.h`, Shared E4) | P1 | A page a slide at the slide's size, laid out by OpenLayout (`src/deck`); text searchable in the standard fonts, characters they lack written as their nearest (an arrow as `->`, a tick as `v`, less or equal as `<=`: `owf_pdf_fallback`), measured so for the layout too; JPEG and PNG pictures; hidden slides left out. Built into OWConvert when OpenLayout is there |
| PPTX, Office Open XML (`.pptx`; `.potx`, `.ppsx`, `.pptm`) | Yes | Later | Spec: ECMA-376 Part 1, PresentationML and DrawingML; tested with decks made by python-pptx (`tests/deck/make_pptx_fixtures.py`) | P2 | Read (`libowf/src/in_pptx.c`): the slide size, masters, layouts and slides with their placeholders matched by index and kind, text style inheritance (the presentation's defaults, the master's title, body and other styles, placeholders' and shapes' list styles, paragraphs and runs), the theme's colours (with tint, shade, luminance and the master's colour map) and its major and minor fonts, style references (`fillRef`, `lnRef`, `fontRef`), text boxes and their insets and anchoring, autofit (shrinking starts at the size PowerPoint shrank the text to, `a:normAutofit fontScale`), pictures (cropped too), tables (cells' fills and borders; table styles as PowerPoint's default one), the shapes the model has (rectangle, rounded rectangle, ellipse, triangle, diamond, block arrow, simple polygons, line and connector), groups (their child transforms), `mc:AlternateContent`'s fallback, notes, footer, date and slide number, backgrounds, hidden slides, and the transitions the model has. Approximated and reported: text in capitals or small capitals (`cap`, kept as capital letters, small capitals' small letters at four fifths of the size), spacing between letters (`spc`, drawn as normal), freeform and other shapes (drawn as rectangles), gradient and pattern fills (one colour), rotated objects (drawn unrotated), vertical text, bent and curved connectors, merged table cells, theme colours (read as fixed colours). Left out and reported: animations, ink, linked pictures, a layout's own pictures and shapes. Saved as ODP |
| PowerPoint 97-2003 (`.ppt`) | Later | No | Spec: Microsoft's [MS-PPT] | Later | To do |

- **ODP.** Each run keeps only what differs from its level's look, so a
  master's look stays the master's: a title without a font of its own takes
  the master's title font. A list paragraph's indents are its list level's.
  Lengths are written in points with two decimals, exactly the twips the
  deck holds, so a deck goes through ODP unchanged; automatic style names
  start with `op`, so they never meet those of styles kept from the file. A
  kept object's XML is written back with the automatic styles, drawing
  definitions (gradients, markers, dashes) and package members it names,
  and the file's graphic defaults, which those styles inherit. Rotation is
  kept and written back (`draw:transform`); the PDF draws objects
  unrotated. Flat ODP holds no package members: an embedded chart's are
  reported lost there.

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

## 5. Publications (OpenPublisher's)

OpenPublisher is the GFOffice Suite's desktop publisher
([gfoffice/Design-OpenPublisher.md](gfoffice/Design-OpenPublisher.md)).
Its files are read into their own model (`libowf/include/owf_pub.h`), not
into OpenWrite's documents.

| Format | Open | Save | Knowledge | Phase | State |
| --- | --- | --- | --- | --- | --- |
| Microsoft Publisher (`.pub`), 2002/2003 to 2013 and later | Yes (P0 reader) | P3, if wanted | No published spec: learnt from samples alone, in [MS-CFB] compound files; [gfoffice/PUB-FORMAT.md](gfoffice/PUB-FORMAT.md) | P0-P1 | The version, page size, pages, master pages, frames (boxes and kinds: text, picture, table, line, shape, group), text as paragraphs, tables' and WordArt's text, fonts, pictures (PNG, JPEG, GIF, DIB, WMF). Formatting, wrap and linked frames: P1. Not shown yet (P1) |
| Microsoft Publisher 98 and 2000 formats | Yes (P0 reader) | No | The same | P0-P1 | Version, page size, pages, frames, text, tables. Master pages and pictures: P1 |
| Microsoft Publisher 97 and older | Version and page size | No | One sample | P1 | To do |
| PageStream 1-5 (Amiga) | P2, import only | No | Samples needed (none found on GitHub) | P2 | To do |

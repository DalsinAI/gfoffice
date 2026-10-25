# GFOffice: the shared pieces, and what OpenWrite must change

Version 0.1, draft for Dale's approval before code | 25 October 2026 | AmigaChrome Team, MIT

The **GFOffice Suite** (Dale, 25 October 2026, 01:31) is four Open apps:
OpenWrite (exists), OpenCalc (being written: W105 the formula engine, W106
ODS and XLSX, W107 the program), OpenPresentation
([Design-OpenPresentation.md](Design-OpenPresentation.md)) and OpenBase
([Design-OpenBase.md](Design-OpenBase.md)). The apps keep their Open names.

Dale, 01:10: "we will need to enhance openwrite". This document turns that
into a list. It covers what the four apps share, and ends with what must
change in OpenWrite and libowf, in order (section 13). The changes are
numbered E1 to E16; the other two designs refer to them by those numbers.

Every statement about existing code was checked against `main` of
openamigawrite (`51fb1d2`), openamigalayout (`48e5cfc`), openamigaprint
(`47eddaa`) and the OpenUp checkout's `tools/build_pack.py` on 25 October
2026.

## 1. owf.library

### 1.1 Today

- owf.library 1.1 (`owf_library.c`'s id string) is libowf as a shared library,
  with no C library of its own: one Exec memory pool, its own string, printf
  and number calls (`README.md`).
- `libowf/library/owf_lib.sfd` lists **29 calls**, all for documents: memory,
  `owf_doc`, paragraphs, fonts, reading the import report, the format table,
  import and export.
- **Not exported:** the whole workbook model (`owf_book_*`, `owf_sheet_*`,
  `owf_cell_*`, `owf_cellstyle_init`, `owf_number_text`, `owf_error_name` and
  the book formats), and `owf_report_add`, whose `...` arguments can't go
  through a register call.
- `build-library.sh` leaves the workbook sources out on purpose: "The workbook
  code ... joins the library with the library's workbook calls and their
  double maths, next." `build-os3.sh` leaves them out of OpenWrite too
  ("OpenWrite reads documents only"); OWConvert carries them.
- `owf_number_text` turns a double into text with its own exact big-number
  code (`owf_book.c`), with no printf, so it can live in the library as it
  is.

### 1.2 Version 2

| Step | Adds | For |
| --- | --- | --- |
| 1 | The 27 calls of `owf.h`'s "Workbooks" section, `owf_error_name` and `owf_number_text` among them | OpenCalc, OpenBase's sheets, OWConvert on the library |
| 2 | `owf_report_note(report, level, text)`, a plain-text form of `owf_report_add` | Programs adding their own notes |
| 3 | The formula engine's calls (`owf_calc.h`, W105's, as it settles) | OpenCalc |
| 4 | Kind detection (2.1), the page PDF writer (9), the speller (7), deck and chart calls | Every app |

- **New calls go at the end**, as the sfd says. Each step is a library
  revision; programs open the version they need.
- **Structures are part of the library's ABI.** Programs index arrays of
  `owf_para`, `owf_run` and `owf_cell` themselves, so adding a field changes
  every element's place. The structure changes this design needs (a list
  level and bullet in `owf_parafmt`, E8; merge fields, E13) go in **together**,
  as owf.library version 2, and every program is rebuilt with it. OpenUp
  ships them together, so that is one step for users.
- **Doubles in register calls.** `owf_cell_set_number(cell, double)` passes a
  double. E3 first checks that `ndkgen` and the sfd carry a double in two
  data registers (as `mathieeedoubbas.library` takes them); if not, the
  library call takes a pointer to the double.
- **OpenWrite on the library.** Once a GFOffice Part ships owf.library
  (section 12), OpenWrite is built with `OWF=library`, so one copy of libowf
  is in memory however many of the four are running.

## 2. File kinds, and one Open requester

### 2.1 Knowing what a file is

The format tables answer "which reader scores highest", and
`owf_import_memory` takes the best score. That is not yet a safe answer to
"is this a document, a sheet, a deck or a database?":

- **ODT's detector scores 80 for any ODF package** with `content.xml` and
  `styles.xml` (`in_odt.c`, `detect_odt`), so an ODS or ODP goes to the ODT
  reader, which then fails with "not a document format OpenWrite knows"
  (`OWF_ERR_FORMAT`, no `office:text`).
- **DOCX's detector scores 50 for any Office Open XML package** (`in_docx.c`,
  `detect_docx`), so an XLSX or PPTX goes to the DOCX reader, which refuses it
  the same way.

E1 fixes both: ODF packages are told apart by their `mimetype` member (ODT
already checks it first), OOXML packages by their main part (`word/`, `xl/`,
`ppt/`). A new call answers the question:

```c
typedef enum { OWF_KIND_UNKNOWN, OWF_KIND_DOCUMENT, OWF_KIND_WORKBOOK,
               OWF_KIND_DECK, OWF_KIND_DATABASE } owf_kind;
/* What a file is, from its first bytes and its package's parts. */
owf_kind owf_file_kind(const unsigned char *data, size_t length);
```

A database is a file starting "SQLite format 3" with OpenBase's application
id (Design-OpenBase.md, 3.1).

### 2.2 One requester

Today OpenWrite's Open is an ASL requester without a pattern
(`app/openwrite2.c`, `ask_file`), remembering the last drawer per program
(`lastdir.h`). For GFOffice (E16):

- one call, `gfo_open_requester()`, shared by the four programs;
- the ASL requester with a pattern made from the format tables (documents,
  workbooks, decks, `.oadb`), so every GFOffice file shows and others hide; the
  pattern can be cleared in the requester to show everything;
- the last drawer kept per program, as now.

### 2.3 Handing a file to its program

When a program is given a file of another kind (from its requester, a
dropped icon or ARexx), it hands it on instead of failing:

1. `owf_file_kind` says whose it is.
2. If that program is running (`FindPort("OPENCALC")`), it sends it
   `OPEN <file>` over ARexx.
3. If not, it starts it, `SYS:Utilities/OpenCalc/OpenCalc <file>`, on its own,
   as OpenWrite starts OpenPrint (`app/ow_print.c`, `run_openprint`).

The programs live where OpenUp puts OpenWrite: `SYS:Utilities/<Name>/<Name>`.

**Icons.** OpenWrite saves no icon today. GFOffice programs save a project icon
with their program as its default tool, so a double-click opens the right
one (a question for Dale, 15).

## 3. Embedding a table or a chart

How an OpenCalc table or chart gets into an OpenWrite document or a slide.

| Way | What it means | For | Against |
| --- | --- | --- | --- |
| Live link (OLE-like) | The document points at a sheet file and redraws when it changes | Always current | The Amiga has no such service; a moved file breaks it; neither ODF nor OOXML readers elsewhere would see it |
| Embedded object | The document carries the data (a small sheet) and a picture of it | Reopens for editing; ODF has it (`draw:object` with a sub-document and a replacement picture) | Large to build: a sub-document reader and writer per format |
| Snapshot | The table becomes a native table; the chart becomes a chart object holding a copy of its data | Small; uses what exists; every reader of the file sees it | Not updated when the sheet changes |

**Recommended:** snapshots first, embedded objects later, live links never.

1. **Insert > Table from Sheet...** (OpenWrite, OpenPresentation): choose a
   file and a range; the cells become a native table, each value shown
   through its number format (`owf_calc_format`, W105's).
2. **Insert > Chart from Sheet...**: the chart's definition and a copy of its
   data become a chart object (section 4). In ODT and ODP it is saved as an
   embedded chart with its data; in DOCX and PPTX as a chart part with its
   cached values, as those formats store charts.
3. **Paste** from OpenCalc does the same, through the clipboard's rich chunk
   (section 5).
4. **Later, Edit in OpenCalc:** the object's data goes to `T:` as ODS, OpenCalc
   opens it, and on Save tells the owner over ARexx (`UPDATEOBJECT`) to take
   it back.

## 4. Charts

One chart model and one renderer, in libowf, for OpenCalc, OpenPresentation
and (through embedding) OpenWrite and OpenBase reports.

### 4.1 The model

```c
typedef enum { OWF_CHART_COLUMN, OWF_CHART_BAR, OWF_CHART_LINE, OWF_CHART_AREA,
               OWF_CHART_PIE, OWF_CHART_XY } owf_chart_kind;

typedef struct {
    char *name;                  /* UTF-8, the legend's text */
    int nvalues;
    double *values;              /* a copy: the chart draws without a sheet */
    double *x;                   /* OWF_CHART_XY only */
    char *source;                /* "Sheet1!$B$2:$B$9", or NULL */
    unsigned long colour;        /* OWF_SHADE(0xRRGGBB); 0: the default palette's */
} owf_chart_series;

typedef struct owf_chart {
    owf_chart_kind kind;
    int stacked;                 /* column, bar, area: 1 stacked, 2 to 100% */
    char *title;
    int ncategories;
    char **categories;           /* the category axis' labels */
    int nseries;
    owf_chart_series *series;
    int legend;                  /* none, right, bottom */
    int value_min_set, value_max_set;
    double value_min, value_max, major_unit;
    int number_format;           /* the value axis' labels, as a book's */
    int gridlines;
} owf_chart;
```

### 4.2 The renderer

Portable C that turns a chart and a box into drawing operations in twips:
rectangles, lines, polygons (wedges), text. They are drawn by OpenLayout
0.3's shape op on screen (Design-OpenPresentation.md, 5.2), by the page PDF
writer (9) into PDFs, and by OpenCalc's sheet view. Values are turned into
positions once each; pie angles use a fixed-point sine table, so drawing
needs no trigonometry under soft float.

### 4.3 Formats

| Where charts live | Format | Read | Write |
| --- | --- | --- | --- |
| XLSX, PPTX, DOCX | A DrawingML chart part (`c:chartSpace`: `c:barChart`, `c:lineChart`, `c:areaChart`, `c:pieChart`, `c:scatterChart`; series with cached values) | The cached values, so no calculation is needed | References and caches |
| ODS, ODP, ODT | An embedded chart document (`chart:chart` with `chart:class`), its data in a table of its own | Yes | Yes |
| XLS | BIFF8 chart records | Still reported as lost (`FORMATS.md` 1a) | No |

Other chart kinds (radar, bubble, stock, 3-D) are drawn as a labelled box and
kept where the format allows. The DrawingML reader is shared by W106's XLSX
and OpenPresentation's PPTX; the ODF one by ODS and ODP.

## 5. The clipboard

### 5.1 Today

OpenWrite copies the selection's text into clipboard unit 0 as `FORM FTXT`
with one `CHRS` chunk, up to 1 MB (`app/openwrite2.c`, `clipboard_set_text`,
`OW_MAX_CLIP`), and pastes `CHRS` back (`clipboard_get_text`), skipping other
chunks. **It writes the text as UTF-8 and reads it as UTF-8.** FTXT's text is
ISO-8859-1, and libowf's own FTXT writer and reader convert to and from it
(`in_ftxt.c`, `export_ftxt`). So an "é" copied from OpenWrite pastes as two
characters in other Amiga programs, and an "é" from them reaches OpenWrite as
a byte that isn't UTF-8.

### 5.2 GFOffice's clipboard (E10)

Still one `FORM FTXT` in unit 0, so every Amiga program can paste it, holding:

| Chunk | Holds | Read by |
| --- | --- | --- |
| `CHRS` | The text in ISO-8859-1, with bold, italic and underline codes, as libowf's FTXT writer makes it. Cells as tab-separated rows | Every Amiga program |
| `UTF8` | The same text in UTF-8, so characters outside ISO-8859-1 survive between GFOffice programs | GFOffice |
| `ODOC` | The selection as a whole ODF package: ODT for text, ODS for cells, ODP for objects and slides, written stored (no z.library needed) | GFOffice, through libowf's own readers |

IFF readers skip chunks they don't know (OpenWrite's paste already does), so
the extra chunks harm no one. Using whole ODF packages means no new format:
the writers and readers exist or are being written. A picture copied alone
goes out as ILBM through its datatype instead, which any paint program reads.

## 6. Look, menus and ARexx

### 6.1 Look

`docs/UX_DESIGN.md`'s rules hold for all four: OpenLook owns the window
frame; OpenGadTools the controls; the Open, Graphite, Ember, Clear and Classic
themes; 800 x 600 on first opening; an icons-only toolbar; every action in a
menu; help bubbles; "..." on commands that open a requester; panels that hide
on narrow screens; a status line. The four programs in this repository share
its one OpenGadTools copy (`third_party/opengadtools`, from opengadtools
`d5f4dff` with the PR #71 font files); changes go to opengadtools first, as
that copy's README says.

### 6.2 Menus

| Menu | In all four | Then |
| --- | --- | --- |
| Project | New, Open..., Save, Save As..., Print..., Export PDF..., About..., Quit | |
| Edit | Undo, Redo, Cut, Copy, Paste, Select All, Find..., Find Again, Find Previous, Replace... | |
| View | Zoom, Toolbar, Theme, the side panels | |
| Insert, Format | | Each program's own |
| Tools | Spell Check..., OpenPrint... | |
| Help | About | |
| Own menus | | OpenWrite: Table, Layout, Datatypes. OpenCalc: Sheet, Data. OpenPresentation: Slide, Arrange, Show. OpenBase: Records, Design |

Amiga-key shortcuts mean the same in all four, taken from OpenWrite's menus:
N, O, S, P, Q; Z, X, C, V, A; F, G; B, I, U; +, -, 0 for zoom; [ and ] for
the previous and next page, sheet or slide.

### 6.3 ARexx (E14)

Today OpenWrite's port is `OPENWRITE`. If that name is taken, a second copy
opens no port at all (`rexx_open`). Commands are matched by hand
(`starts_word_ci`), and every failure, an unknown command included, returns
10. OpenView (openamigaprint) does it differently: each window has
`MULTIVIEW.n`, the first also `OPENVIEW`, 5 means "not supported" and 10
"unknown command".

The GFOffice conventions:

| Rule | |
| --- | --- |
| Port | `OPENWRITE`, `OPENCALC`, `OPENPRESENTATION`, `OPENBASE` for the first copy; a second copy takes `NAME.2`, then `NAME.3` |
| Arguments | Each command has a `ReadArgs` template, so quotes and keywords work as in the Shell. `HELP` lists the commands; `HELP <command>` gives its template |
| Results | In `RESULT` when the script asks (`OPTIONS RESULTS`) |
| Return codes | 0 done; 5 not possible now (unsaved changes, nothing selected, not supported); 10 error (bad arguments, an unknown command, a failure); 20 fatal |
| Common commands | `VERSION`, `OPEN`, `SAVE`, `SAVEAS`, `EXPORT`, `PRINT`, `QUIT [FORCE]`, `HELP`, `ACTIVATE`, `GETFILE`, `ISDIRTY` |

OpenWrite moves to these, keeping its commands' names (`WORDCOUNT`,
`PAGECOUNT`, `INSERTTEXT`, `FIND`, `GETTEXT`, `COMMAND`).

## 7. Spelling (E11)

### 7.1 Today

- One dictionary ships: `Dictionaries/en_GB.words`, 1,171,550 bytes, 120,980
  lines.
- `ow_spell_open` reads the whole file and keeps a pointer for each word
  (`app/ow_spell.c`, `load_words_file`): about 1.65 MB of memory in each
  program that checks spelling.
- OpenWrite looks for it in `PROGDIR:Dictionaries/en_GB.words`, then
  `en_US.words`, then `SYS:Dictionaries/en_GB.words`; your own words go to
  `PROGDIR:Dictionaries/user.words` (`app/openwrite2.c`, `ensure_spell`).
- OpenUp installs it beside OpenWrite, in
  `SYS:Utilities/OpenWrite/Dictionaries/`.
- The check walks OpenWrite's editor (`ow_spell_next` takes an `ow_editor`).

### 7.2 Shared

- **One place:** `SYS:Dictionaries/`, which OpenWrite already searches. The
  programs look there first, then in `PROGDIR:Dictionaries` for an older
  install.
- **One copy in memory:** the word list, its lookup and the suggestions move
  into owf.library: loaded on first use, read only, shared by every program
  that opens the library. Three programs checking spelling cost 1.65 MB, not
  5.
- **One user dictionary** for all four (where: a question, 15).
- The walk stays in each program: OpenWrite's document; OpenPresentation's
  frames and notes; OpenCalc's text cells; OpenBase's text and memo fields.

## 8. z.library on every install

### 8.1 Today

- On the Amiga, libowf deflates and inflates through z.library 53 (ours, from
  amigachrome-guest's `libraries/openzlib`), opened the first time a document
  needs it (`libowf/amiga/owf_zlib_amiga.c`).
- Without it, reading a deflated zip member gives `OWF_ERR_UNSUPPORTED`
  (`owf_unzip.c`), and files are written stored (`README.md`).
- **The messages are wrong.** The ODT reader passes `OWF_ERR_UNSUPPORTED` on:
  "OpenWrite cannot do that with this format yet". The DOCX reader turns every
  failure to read its main part into `OWF_ERR_CORRUPT` (`in_docx.c`,
  `read_xml` returns NULL, then `read_docx` returns `OWF_ERR_CORRUPT`): "the
  document is damaged". That is what Instance-32 showed, which has no
  z.library (Team 4's printing handover: the lab printed a `.txt` instead).
- OpenUp's OpenWrite Part carries `LIBS:z.library`, and its OpenImage Part
  does too (`build_pack.py`). How OpenWrite reached Instance-32 without it is
  not recorded.

### 8.2 Which GFOffice files need it

| Needs inflate to open | Doesn't |
| --- | --- |
| ODT, ODS, ODP; DOCX, XLSX, PPTX (all zip packages); PNG pictures put into a PDF | FODT, FODS, FODP (flat XML); XLS (a compound file, `owf_cfb.c`); CSV; text; FTXT and the Amiga formats; `.oadb` |

### 8.3 What to do

1. **Say so (E1).** A new `OWF_ERR_NEED_ZLIB`: "This document is compressed,
   and LIBS:z.library is missing." Both readers pass it on.
2. **Inflate without it (E2).** Our own inflater in libowf, written from RFC
   1951 (about 500 lines), used when z.library can't be opened. Every
   GFOffice file then opens on any install. z.library stays the faster path,
   and is still needed to write compressed files (without it they are
   written stored, which every reader accepts).
3. **Ship it.** The shared GFOffice Part carries `LIBS:z.library` (section 12),
   and every Aminet archive of a GFOffice program carries it with its licence.

## 9. PDF and printing (E4)

### 9.1 Today

`out_pdf.c` is a format exporter: given an `owf_doc`, it lays the document out
by itself (`pdf_layout_document`):

- it measures text by counting characters (each 0.5 of the size, 0.6 in a
  monospaced font: `pdf_piece_width`), not with the fonts the screen uses;
- a table in the body comes out cell after cell, one under another; only
  header and footer tables go side by side (`pdf_story`);
- base-14 fonts in WinAnsi encoding; JPEG and PNG pictures only.

The screen lays out with OpenLayout and the Amiga fonts' own widths
(`app/openwrite2.c`, `measure_units`). So OpenWrite's PDF can break lines and
pages differently from its screen, where `NATIVE_CORE.md` 9 asks for "one
pagination model" for screen, print and PDF.

### 9.2 A page-drawing PDF writer

```c
typedef struct owf_pdf owf_pdf;
owf_pdf *owf_pdf_new(void);
int owf_pdf_page(owf_pdf *pdf, int width, int height);       /* twips */
/* Text placed at x on its baseline, fitted to width (PDF's horizontal
   scaling), so the PDF breaks where the layout broke, whatever its font. */
int owf_pdf_text(owf_pdf *pdf, int x, int baseline, int width, const char *utf8,
                 size_t length, owf_font_kind kind, unsigned flags, int size,
                 unsigned long colour);
int owf_pdf_rect(owf_pdf *pdf, int x, int y, int w, int h,
                 unsigned long fill, unsigned long line, int line_width);
int owf_pdf_path(owf_pdf *pdf, const int *xy, int npoints, int closed,
                 unsigned long fill, unsigned long line, int line_width);
int owf_pdf_image(owf_pdf *pdf, const owf_image *image, int x, int y, int w, int h);
int owf_pdf_link(owf_pdf *pdf, int x, int y, int w, int h, const char *href);
int owf_pdf_finish(owf_pdf *pdf, unsigned char **data, size_t *length, owf_report *report);
void owf_pdf_free(owf_pdf *pdf);
```

It is made from `out_pdf.c`'s object, font and picture code. Then:

- OpenWrite's Export PDF and Print draw its OpenLayout pages into it: the PDF
  is the screen's pages, tables included;
- OpenPresentation draws slides and handouts; OpenBase reports; OpenCalc
  printed sheets;
- the old `pdf` format stays for OWConvert, drawn through the same writer
  from a layout made without a screen.

### 9.3 Printing

All four print as OpenWrite does since #32: the PDF to `T:<name>.pdf`, then
`C:OpenPrint <file>` started on its own (`app/ow_print.c`). OpenPrint's own
window picks the printer and pages. `ow_print.c`'s OpenPrint half becomes a
shared `gfo_print_pdf(path)`.

## 10. Fonts (E12)

OpenWrite maps a document's fonts to four Amiga fonts
(`app/openwrite2.c`, `document_font_file`): `courier.font` for monospaced
fonts, `CGTimes.font` for serif ones, `helvetica.font` for Helvetica, and
`CGTriumvirate.font` for the rest. It opens them through openfont.library and
measures text from a table of 256 widths per font, taken at 64 pixels
(`font_widths`).

That mapping and those tables move into one shared module, so a letter, a
sheet, a slide and a report use the same fonts and measure them the same
way. The PDF writer chooses its base-14 font by the same kinds, as
`out_pdf.c`'s `pdf_font_id` does today.

## 11. Pictures and memory

| Today (checked) | Cost | Change |
| --- | --- | --- |
| A picture is decoded through a datatype to full-size ARGB, `width x height x 4` bytes, and up to 16 are kept (`image_cache_get`) | A 12-megapixel photo is 48 MB | **E9, a picture cache:** read the size first; decode only what fits; scale at once to the size needed; keep scaled copies within a budget; draw a labelled box when a picture can't fit |
| Each undo step is a copy of the whole document, picture bytes included (`editor.c`, `clone_doc`); up to 16 undo and 16 redo (`OW_UNDO_LIMIT`) | A document with 2 MB of pictures can hold 64 MB of copies | **E7:** snapshots share the pictures (they never change in place); unused ones go at save |
| Each program loads the dictionary | 1.65 MB each | **E11** (section 7) |
| Each program links libowf | One copy each | owf.library (section 1) |

The rule for all four: nothing big is held twice; caches have budgets set
from free memory; a program says what it couldn't do instead of failing.

## 12. OpenUp Parts

### 12.1 Today

OpenUp's OpenWrite Part, version 2.0.1 (`tools/build_pack.py`):
`SYS:Utilities/OpenWrite/OpenWrite` and its icon, `Dictionaries/*.words`
beside it, the licences and release notes, `SYS:C/OWConvert` and
`LIBS:z.library`. OpenWrite is built with libowf linked in (`build-os3.sh`'s
default), so the pack has no owf.library. A Part's lines copy, add, keep and
retire files and add startup lines; its `needs` lines are about the machine
(AmigaChrome, ACRTG, Kickstart), not about other Parts (`openuptool.c`).

### 12.2 Proposed

| Part | Holds |
| --- | --- |
| **GFOffice** (shared) | `LIBS:owf.library`, `LIBS:z.library`, `SYS:Dictionaries/en_GB.words`, `C:OWConvert`, the licences |
| **OpenWrite** | `SYS:Utilities/OpenWrite/` as today, built on owf.library; retires the old `Dictionaries` drawer beside it |
| **OpenCalc** | `SYS:Utilities/OpenCalc/` |
| **OpenPresentation** | `SYS:Utilities/OpenPresentation/` |
| **OpenBase** | `SYS:Utilities/OpenBase/`, with OpenBaseRun if Dale wants it |

Each app Part needs the GFOffice Part. Two ways:

1. **OpenUpTool learns `needs part GFOffice`**: a check beside its `needs`
   checks in pass 0. Recommended: the shared files have one owner, and
   removing an app leaves them for the others.
2. Each app Part carries the shared files with `add` lines. No OpenUpTool
   change, but four Parts own the same files.

## 13. What OpenWrite must change, in order

Each change is listed by what the other apps need. "Size" is a rough guide:
small is under a day's work, medium a few days, large a week or more.

### 13.1 The list

| # | Change | Why, and who needs it | Size | Where |
| --- | --- | --- | --- | --- |
| E1 | **Say what a file is, and what's missing.** ODT's and DOCX's detectors stop claiming ODS, ODP, XLSX and PPTX; `owf_file_kind`; `OWF_ERR_NEED_ZLIB` instead of "damaged" | Every app routes files by it; Instance-32's users get the true message | Small | `in_odt.c`, `in_docx.c`, `owf_format.c` |
| E2 | **Inflate without z.library** (RFC 1951) | Every zip format opens on any install | Medium | `libowf/src` |
| E3 | **owf.library 2:** the workbook calls, a plain report-note call, then the engine, deck, chart, PDF and speller calls; the structure changes of E8 and E13 in the same version | OpenCalc first; then all | Small to medium | `owf_lib.sfd`, `build-library.sh`, `scripts/gen-sdk.sh` |
| E4 | **The page-drawing PDF writer**, and OpenWrite's PDF drawn from its OpenLayout pages | OpenWrite (PDF as on screen, tables side by side); OpenPresentation P1; OpenBase reports; OpenCalc printing | Medium to large | `libowf/src`, `app` |
| E5 | **Any story in any frame:** `ow_layout` lays out an `owf_story` into a given OpenLayout node, with the fonts and pictures of a given `owf_doc` | OpenPresentation's text frames; OpenBase's report preview; OpenWrite's text boxes later | Medium | `src/core/ow_layout.c` |
| E6 | **The editor on any story:** `ow_editor` over a story of an `owf_doc`, not only `doc->body` (64 lines of `editor.c` name the body) | OpenPresentation's frames and notes; OpenBase's memo fields; OpenWrite's headers and footers edited on the page (today a text template in a requester, `ow_editor_set_story_text`) | Medium | `src/core/editor.c` |
| E7 | **Undo without copying pictures** | OpenWrite with pictures; OpenPresentation | Small to medium | `src/core/editor.c` |
| E8 | **Real lists:** a level and a bullet in `owf_parafmt`; ODT and DOCX lists read and written as lists; the list commands set them | OpenPresentation's body text; OpenWrite's lists, kept as bullet text today, round trip as lists | Medium | `owf.h`, readers, writers, `editor.c`, `ow_layout.c` |
| E9 | **The picture cache** (section 11) | All four | Medium | Shared app code |
| E10 | **The clipboard through libowf's FTXT**, plus the `UTF8` and `ODOC` chunks (section 5) | Fixes accented text with every Amiga program today; copying between GFOffice programs | Small | `app/openwrite2.c`, shared app code |
| E11 | **Spelling shared**: in owf.library, in `SYS:Dictionaries`, one user dictionary | Memory; one dictionary for four programs | Medium | `app/ow_spell.c` into libowf |
| E12 | **Fonts shared** (section 10) | The same text measures the same in all four | Small to medium | Out of `app/openwrite2.c` |
| E13 | **Merge fields:** a named field run in the model, read and written in ODT and DOCX | OpenBase's letters | Small to medium | `owf.h`, readers, writers |
| E14 | **ARexx conventions** (6.3), OpenWrite's port moved to them | Scripts that drive four programs the same way | Small | `app/openwrite2.c`, shared app code |
| E15 | **Embedding:** Table from Sheet, chart objects, saved as each format stores them (section 3) | OpenWrite and OpenPresentation showing OpenCalc's work | Large | libowf, `src/core`, apps |
| E16 | **One Open requester, and handing files on** (section 2) | Opening any GFOffice file from any of the four | Small to medium | Shared app code |

### 13.2 The order

| Step | Changes | Unblocks |
| --- | --- | --- |
| 1, now | E1, E3, E10 | OpenCalc on owf.library; true messages; a correct clipboard |
| 2 | E2, E4 | Every install opens zip formats; PDFs as on screen; OpenPresentation P1 and OpenBase B3 can print |
| 3 | E8, E5, E6, E7 | OpenPresentation P1 to P3; OpenBase's memo fields |
| 4 | E9, E12, E16, E14 | Pictures within memory; one look and one way in for all four |
| 5 | E11, E13, E15 | Shared spelling; OpenBase's letters; embedded tables and charts |

Step 1 is small and touches nothing W105 to W107 are writing. E3's
structure changes (E8, E13) wait for step 3 and go out as owf.library 2 in
one release, with all four programs rebuilt.

## 14. Found while checking

- OpenWrite's README says "OpenWrite 1.0.3"; the program says 2.0.1
  (`VERSION_TEXT` in `app/openwrite2.c`), and so does OpenUp's Part.
- The live window is `app/openwrite2.c` ("OpenWrite since 2.0.1"); the 1.x
  window `app/openwrite.c` is built only with `OW1=yes` (`build-os3.sh`).
- OpenUp's installer text says OpenWrite "Needs a 68020"
  (`pack/Install-OpenUp`); the builds are 68040 only (`build-os3.sh`).
- OpenLayout's README says its Amiga build needs a 68040 with an FPU, but
  `openlayout.c` has no floating point at all, and OpenWrite builds it with
  its soft-float specs.

## 15. Questions for Dale

1. owf.library 2, with its new calls and structure changes together and all four programs rebuilt with it: agreed?
2. Build OpenWrite on owf.library once the GFOffice Part ships, so libowf is in memory once?
3. Our own inflater in libowf, so every GFOffice file opens without z.library?
4. A shared OpenUp Part named "GFOffice", and OpenUpTool taught `needs part`?
5. The user dictionary for all four: `SYS:Dictionaries/user.words`, or `ENVARC:GFOffice/user.words`?
6. Embedding: snapshots first, embedded objects that reopen in OpenCalc later, never live links?
7. A second copy of a program gets the ARexx port `NAME.2` (today OpenWrite's gets none)?
8. GFOffice programs save a project icon with their program as default tool?
9. Charts: column, bar, line, area, pie and XY first, others drawn as a labelled box?
10. The rich clipboard as a whole ODF package inside the FTXT FORM: agreed?

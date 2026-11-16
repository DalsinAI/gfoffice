# GFOffice

GFOffice is a native office suite for AmigaOS 3.2. Its programs are small
68040 C applications with no FPU requirement, and they share one document
library, **libowf**, for every file format, PDF and printing.

The suite aims at the features of Apache OpenOffice and the other desktop
programs named below. It is written from the file formats' published
specifications, from samples and from our own code, never from OpenOffice's,
LibreOffice's or any other suite's source. The designs are in `docs/gfoffice/`.

![GFOpenWrite on AmigaOS 3.2.3: a Word document written in Word, opened as it is](docs/screenshot.png)

*GFOpenWrite on AmigaOS 3.2.3 (an AmiCradle instance with GFOpenRTG and the Open look), opening a DOCX written in Word.*

## The programs

| Program | What it is | Version | Parity (16 Nov 2026) |
| --- | --- | --- | --- |
| **GFOpenWrite** | Word processor | 2.0.48 | 85% of OpenOffice Writer |
| **GFOpenCalc** | Spreadsheet | 0.1.78 | 80% of OpenOffice Calc |
| **GFOpenPresentation** | Slides and slide shows | 0.6.35 | 89% of OpenOffice Impress |
| **GFOpenPressmaster** | Desktop publishing | 0.3.36 | 71% of Microsoft Publisher 2021 |
| **GFOpenBase** | Database | 0.15.0 | 64% of OpenOffice Base |
| **GFOpenProject** | Project planner | 0.1.20 | 64% of ProjectLibre 1.9 |
| **GFOpenDiag** | Diagram editor | 0.1.0 | 66% of draw.io, 72% of Dia |
| **OWConvert** | Shell converter, `C:OWConvert` | | |

Parity is a practical, workflow-weighted score: whether an ordinary user can
finish the comparator's usual work, with editing, Undo, save and reload. Only
merged work is scored. Each program's sheet, with every feature row, is in
`docs/parity/`; the method is in `docs/openoffice-parity-plan.md` and the
9 November gap analysis, `docs/gfoffice/PARITY-GAPS-2026-11-09.md`.

GFOpenPressmaster was called GFOpenPublisher until 16 November 2026; its
ARexx port answers to both names. GFOpenDraw, the suite's paint editor, has
its own repository, `DalsinAI/gfopendraw`, and the suite build pins it.
This repository was called `openamigawrite` until 25 October 2026, and GitHub
redirects the old name.

## GFOpenWrite

GFOpenWrite 2.0.48 is the suite's word processor (GFOpenWrite 1.0.7, the 1.0
line, is still built as `GFOpenWrite1`).

- Editing with grouped Undo and Redo; character and paragraph formatting,
  styles, bullets and numbering, tabs and the ruler.
- Tables with row and column editing, cell fills and borders; inline and
  floating pictures with text wrapping; anchored text boxes.
- Headers and footers, page setup, newspaper columns and mixed-column
  sections, page breaks, a linked Table of Contents.
- Track changes, comments in the margin, footnotes and endnotes, fields
  (IF, SEQ and references), hidden text.
- Find and Replace, spelling (en_GB plus a user dictionary), autosave and
  recovery, and an ARexx port.
- Searchable PDF export and printing through GFOpenPrint.

The architecture is in `docs/architecture/NATIVE_CORE.md`, the interaction
design in `docs/UX_DESIGN.md`.

## GFOpenCalc

GFOpenCalc 0.1.78 is the spreadsheet. Its portable core is `src/calc` and its
formula engine is in `libowf/calc` (`docs/CALC_ENGINE.md`).

- Editing, fill, insert and delete, styles, merged cells, comments, named
  ranges, sheets reordered, hidden and copied, with Undo throughout.
- Sort on up to eight keys, AutoFilter and Standard Filter, validation,
  conditional formats (colour scales, data bars and icon sets).
- Goal Seek, Multiple Operations, Subtotals, Consolidation, scenarios,
  pivot tables with subtotals and drill-down, tracked changes.
- Charts (column, bar, line, area, pie, scatter) with titles, axes, series
  styles, trendlines, error bars and a secondary axis, all saved in XLSX and ODS.
- PDF export and printing through GFOpenPrint.

## GFOpenPresentation

GFOpenPresentation 0.6.35 is the slide program (`docs/gfoffice/Design-OpenPresentation.md`).

- Slide sorter, slide editing and text editing with Undo; pictures with crop;
  tables; charts; masters; solid, gradient and hatched fills, see-through
  fills and shadows.
- Cut, Copy and Paste of text and of whole objects, shared with Pressmaster.
- A full-screen show with transitions, timings and loops (F5, Shift-F5).
- PDF export and printing through GFOpenPrint.

## GFOpenPressmaster

GFOpenPressmaster 0.3.36 is the desktop publisher (`docs/gfoffice/Design-OpenPressmaster.md`).
It opens Microsoft Publisher `.pub` files into its own model and edits them
with the shared page editor it has with Presentation.

- `.pub` pages, text with its original formatting, linked frames and columns,
  tables, pictures, master pages (one-page and two-page) and page numbers.
- Text boxes linked into stories, text wrap round pictures and shapes,
  columns, frame margins, grid and guide snapping, tables.
- Mail and catalog merge from the Mailings menu, eight built-in templates.
- PDF export with crop marks and bleed, booklet printing through GFOpenPrint,
  and spelling as you type.
- Saving as `.pub` is held until it is checked in Microsoft Publisher; the
  publication saves as ODP and PDF today. The `.pub` format notes are in
  `docs/gfoffice/PUB-FORMAT.md`.

## GFOpenBase

GFOpenBase 0.15.0 is the database (`docs/gfoffice/Design-OpenBase.md`). A
project is an `.oadb` file, an SQLite database with its own checks.

- Table designer with keys, indexes and relationships; a datasheet that sorts
  and filters, with a row editor and Undo.
- Queries designed in a grid with joins drawn, or typed as SQL, kept as views.
- Forms with subforms, Find and Filter by Form, and a form designer with
  buttons and ARexx events.
- Reports grouped and totalled on three levels, with a print preview, made as
  ODT, DOCX, PDF or text.
- CSV, dBASE III and IV, ODS and XLSX in and out; ARexx reads and changes rows.

## GFOpenProject

GFOpenProject 0.1.20 is the project planner.

- Task sheet with summaries, milestones, WBS numbers and Undo; links of all
  four kinds with lags; constraints; critical path and slack.
- Gantt chart, Tracking Gantt, Network Diagram, Task Usage, Resource Usage
  and Resource Graph views; a column chooser; sort, filter and group.
- Resources and assignments, levelling, working time, baselines, actuals and
  earned value.
- Gantt printing and PDF export.

## GFOpenDiag

GFOpenDiag 0.1.0 is the diagram editor. Its native file is `.drawio`, which
round-trips with diagrams.net.

- Shapes from Flowchart, Basic, UML and network sets; connectors that stay
  glued, with elbow routing and labels; layers; a Style window.
- Cut, Copy, Paste, Duplicate, drawing order, Group, Align and Distribute.
- Mind maps with their keys, and outlines in and out.
- Export as SVG and PNG (with the diagram inside), PDF, EPS, Mermaid, DOT,
  GraphML and Excalidraw.

## Formats

| Program | Opens and saves | Opens only | Saves only |
| --- | --- | --- | --- |
| GFOpenWrite | ODT, OTT, flat ODT; DOCX, DOTX; RTF; Markdown; text | ProWrite; Final Writer and Final Copy; Wordworth; IFF FTXT; ANSI text | HTML, PDF |
| GFOpenCalc | XLSX; ODS, flat ODS; CSV | Excel 97-2003 XLS | PDF |
| GFOpenPresentation | ODP, flat ODP; PPTX | | PDF |
| GFOpenPressmaster | | Publisher `.pub`, 98 to 2013 and later | ODP, PDF |
| GFOpenBase | `.oadb`; CSV; dBASE III and IV; ODS; XLSX | | ODT, DOCX, PDF, text reports |
| GFOpenProject | `.gfproj`; MS Project XML (MSPDI); CSV | MPX; GanttProject `.gan` | PDF |
| GFOpenDiag | `.drawio`; outlines | | SVG, PNG, PDF, EPS, Mermaid, DOT, GraphML, Excalidraw |

Word 97-2003 `.doc`, `.xlsb` and password-protected Office files are
recognised with a clear message. Every format, what is kept and where the
knowledge comes from is in `docs/FORMATS.md`. Imports report what they could
only approximate instead of pretending it was kept.

ODT, DOCX, XLSX and ODS are zip packages. libowf reads them through
z.library when it is in LIBS:, and otherwise with its own inflater
(`libowf/src/owf_inflate.c`, written from RFC 1951). Without z.library, files
are saved stored, which every reader accepts.

## Requirements

- AmigaOS 3.2 or later.
- A 68040; no FPU is needed.
- Around 8 MB of Fast RAM for small documents; 16 to 32 MB is comfortable.
- GFOpenPrint for printing laid-out pages (without it, GFOpenWrite prints its
  text through printer.device), and Amiga datatypes for pictures.
- GFOpenGadTools and the Open look for the interface; GFOpenRTG where it is
  available, with AGA and planar screens as the fallback.

## Building and testing

On Linux, with Python 3 and zlib, the host build and tests are:

```
make            # the host tools: owconvert, book-dump and the rest
make test       # every unit test
make check      # the same under AddressSanitizer and UBSan
```

Each part has its own pair of targets as well, for example `make calc-test`,
`make deck-test`, `make pressmaster-core-test`, `make base-test`,
`make project-test`, `make diagram-test` and `make library-test`, each with a
`-check` twin under the sanitizers. Layout and deck PDF tests need
GFOpenLayout (`DalsinAI/gfopenlayout`) checked out beside this repository, or
`OPENLAYOUT_SRC` pointing at it. `make attribution-check` and `make docs-test`
check the tree and this README.

For AmigaOS, with the os32-gcc16 stove (GFOpenAmigaGCC 16; set `OS32_GCC16`
if it is not in `~/AmiCradle/stoves/os32-gcc16`):

```
./build-os3.sh                 # libowf linked into each program
OWF=library ./build-os3.sh     # each program on its own shared library
./build-library.sh [OUT_DIR]   # the shared libraries alone
```

`build/os3/` then holds GFOpenWrite, GFOpenWrite1, GFOpenCalc,
GFOpenPresentation, GFOpenPressmaster, GFOpenBase, GFOpenProject, GFOpenDiag,
OWConvert, the toolbar images and the ARexx examples.

### The shared libraries

libowf 1.18.0 is split along the programs' lines into four Amiga libraries
(`docs/suite/owf-libraries.md`):

| Library | For | Holds |
| --- | --- | --- |
| `owfwrite.library` | GFOpenWrite | documents and their formats, PDF out |
| `owfcalc.library` | GFOpenCalc | workbooks, their formats, charts, the formula engine |
| `owfpub.library` | GFOpenPressmaster | the `.pub` reader |
| `owfpresent.library` | GFOpenPresentation | decks, ODP and PPTX, the page PDF writer |

Each is built from its SFD (`libowf/library/<name>_lib.sfd`) with its calls in
`<name>_calls.h`, made by `scripts/gen-lib-calls.py`; the SDK headers are made
by `scripts/gen-sdk.sh`. A program calls `owf_open()`, which opens its library
and refuses one whose shared structures differ in size. The libraries carry
their own C calls and soft double arithmetic, so they need no FPU and no
maths library. `OWFLibsTest` checks all four on an Amiga, and
`make sfd-check` checks the SFDs, the glue and the SDK. The older single
`owf.library` 2.5 is still built until GFOpenUp ships the four.

## Licence

MIT, Copyright (c) 2026 AmiCradle Team (`LICENSE`).

Third-party components and data keep their own licences and notices; see
`THIRD_PARTY.md` and `licenses/`.

If you use or build on this work, we ask, but do not require, that you credit
the AmiCradle Team, for example: "based on GFOffice by the AmiCradle Team".

## Contributors

GFOffice is created and maintained by
[SacredTrees](https://github.com/SacredTrees), copyright AmiCradle Team.
Everyone whose work it includes is credited in `CONTRIBUTORS.md`.

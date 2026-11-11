# GFOffice against Apache OpenOffice: gap matrix and plan

Team 2, 11 November 2026. Checked against GFOffice `main` at `027d095`
(after #129 and #130). It builds on `docs/gfoffice/PARITY-GAPS-2026-11-09.md`;
nothing since 9 November changed a feature's status except the ARexx ports
(Write, Calc and, with #130, Presentation).

Apache OpenOffice is the feature reference only. No OpenOffice or LibreOffice
code, dialogs, resources or help text is read for implementation or copied.
Features are written from the file-format specifications (ODF 1.2/1.3, ECMA-376,
MS-XLS/MS-PPT where public) and our own code. Bringing in any third-party code
needs Dale's "yes with review" first.

## 1. Where the suite stands

| OpenOffice app | GFOffice app | Version | Practical parity | Biggest gaps |
| --- | --- | --- | ---: | --- |
| Writer | GFOpenWrite | 2.0.26 | 63% | Track changes, comments, mail merge, footnotes, indexes, fields, forms, master documents |
| Calc | GFOpenCalc | 0.1.67 | 62% | Full pivot tables, Solver, scenarios, consolidate, named-range manager, change tracking |
| Impress | GFOpenPresentation | 0.6.22 | 64% | Slide sorter/outline/notes/handout views, PPTX save, master editing, custom animation, group |
| Draw | GFOpenDraw (gfopendraw) | 0.2.0 RC2 | 19% | It is a raster painter; Draw is a vector page editor |
| Base | GFOpenBase | design 0.1 | 4% | Nothing runs yet |
| Math | none | - | - | Not planned: Dale, 11 Nov 2026 |

Percentages are the workflow-weighted estimates from the 9 November gap
analysis: an ordinary user finishing OpenOffice's documented workflow, with
editing, save, reload and Undo working. GFOpenPublisher is compared with
Microsoft Publisher, not OpenOffice, and is outside this plan.

## 2. Gap matrix

Status: **Done** = edits, saves, reloads and undoes; **Part** = some of it
(often import or display only); **Gap** = missing. Phase is section 3's.

### Writer (GFOpenWrite)

| Feature (Writer) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Text editing, character/paragraph formatting | Done | - | |
| Paragraph and character styles | Done | - | Page/frame/list style families are Part |
| Lists, outline numbering | Done | - | |
| Tables (insert, edit, borders) | Done | - | Split/merge cells, repeat heading rows, table formulas: Gap (2) |
| Pictures inline and floating, text boxes | Done | - | |
| Headers/footers, page numbers | Done | - | First/left/right page styles: Part |
| Sections, newspaper columns | Done | - | |
| Table of contents | Done | - | |
| ODT/DOCX/RTF read and write, PDF, print | Done | - | |
| Spelling, autosave/recovery, ARexx | Done | - | |
| Track changes (record, show, accept/reject; ODT + DOCX) | Gap | 1 | Today imported revisions are accepted on load |
| Comments (insert, show in margin, reply; ODT + DOCX) | Gap | 1 | |
| Fields (doc info, cross-references, user fields, chapter) | Part | 1 | Page, page count, date and time exist |
| Footnotes and endnotes | Gap | 1 | Page allocation belongs in GFOpenLayout |
| Find & Replace with formats, regular expressions | Part | 2 | |
| Mail merge (CSV/Calc source), labels, envelopes | Gap | 2 | One merge service, later used by Publisher and Base reports |
| Alphabetical index, index of tables/figures, bibliography | Gap | 2 | |
| AutoCorrect, word completion, thesaurus, hyphenation | Gap | 2 | |
| Form controls in documents | Gap | 3 | |
| Master documents | Gap | 3 | |
| Embedded Calc sheets/charts (OLE-style objects) | Gap | 3 | Needs Calc chart renderer as a library call |
| Document compare | Gap | 3 | Reuses track changes |

### Calc (GFOpenCalc)

| Feature (Calc) | Status | Phase | Notes |
| --- | --- | --- | --- |
| XLS/XLSX/ODS/FODS/CSV, PDF, print | Done | - | |
| Formula engine (277 functions) | Done | - | OpenOffice has about 500; the rest are listed in C's parity file |
| Cell formatting, number formats, merge | Done | - | |
| Sort, AutoFilter, standard filter | Done | - | Advanced filter (criteria range): Gap (1) |
| Comments, validity, conditional formatting | Done | - | Colour scales/data bars/icon sets: Gap (2) |
| Charts | Done | - | Editing chart elements in place: Part (2) |
| Subtotals, Goal Seek, Multiple Operations | Done | - | Automatic data-table recalc: Part (1) |
| Pivot tables | Part | 1 | Refreshable summaries only; needs field layout dialog, page/row/column filters, several data fields, drill-down, ODS/XLSX pivot persistence |
| Solver (linear, then non-linear) | Gap | 1 | Own simplex/branch-and-bound; no third-party solver |
| Scenarios | Gap | 1 | ODS `table:scenario` |
| Consolidate | Gap | 1 | |
| Named ranges manager, database ranges | Part | 1 | Define Name exists; no list to edit or delete names |
| Freeze panes, split window | Part | 2 | |
| Sheet protection, cell protection | Part | 2 | |
| Text to columns, remove duplicates | Gap | 2 | |
| External links / DDE / web query | Gap | 3 | |
| Change tracking, shared workbooks | Gap | 3 | |
| Macro recorder (ARexx) | Gap | 3 | ARexx port exists; recording does not |

### Impress (GFOpenPresentation)

| Feature (Impress) | Status | Phase | Notes |
| --- | --- | --- | --- |
| ODP/FODP read/write, PPTX read | Done | - | |
| Slide add/delete/reorder/hide, layouts | Done | - | |
| Text editing, formatting, ruler, find/replace | Done | - | |
| Tables, pictures, shapes, crop, wrap | Done | - | Table cell merge, row heights: Gap (2) |
| Move/resize/rotate/arrange/align, grid/guides | Done | - | Multi-select, distribute, group/ungroup: Gap (1) |
| Transitions, slide show, PDF, print, ARexx | Done | - | |
| Slide sorter view | Gap | 1 | |
| Outline view | Gap | 1 | |
| Notes view, notes editing, notes printing | Gap | 1 | Model reads notes pages; no UI |
| Handout view and printing | Gap | 1 | |
| PPTX save | Gap | 1 | ECMA-376 PresentationML writer |
| Master slide editing | Gap | 2 | |
| Custom animation (entrance/emphasis/exit, timing) | Gap | 2 | ODP `anim:` and PPTX `p:timing` |
| Gradients, transparency, shadows | Part | 2 | |
| Charts and embedded objects | Part | 3 | Display of imported only |
| Sound/video in slides | Gap | 3 | Through datatypes/AHI |
| Spelling in slides | Gap | 2 | Calls GFOpenWrite's spell module unchanged |
| PPT (binary) read | Gap | 3 | |

### Draw (GFOpenDraw, DalsinAI/gfopendraw)

| Feature (Draw) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Raster painting, ARexx, two front ends | Done | - | Not a Draw feature, but the base of picture objects |
| Multi-page vector document, ODG read/write | Gap | 2 | Reuse `owf_deck` pages/objects; Impress stream's model work lands first |
| Shapes, lines, Bezier curves, text objects | Gap | 2 | |
| Layers, master pages | Gap | 2 | |
| Connectors and glue points | Gap | 3 | Snap/route geometry in GFOpenLayout |
| Dimension lines, technical drawing | Gap | 3 | |
| Group, combine, shape boolean ops | Gap | 3 | |
| SVG import/export, vector PDF | Gap | 3 | |

### Base (GFOpenBase)

| Feature (Base) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Create/open a database file | Gap | 1 | `.oadb` = SQLite file (gfopensqlite), per Design-OpenBase.md |
| Table designer, datasheet view with add/edit/delete | Gap | 1 | |
| Primary keys, relationships | Gap | 1 | Real FOREIGN KEY clauses |
| SQL view, query designer | Gap | 2 | |
| Forms and subforms | Gap | 2 | |
| Reports | Gap | 2 | Generated as GFOpenWrite documents |
| CSV/DBF/Calc import and export | Gap | 2 | CSV first (B1) |
| Locking, recovery | Gap | 1 | Dotfile-lock probe is B0's first job |
| ARexx | Gap | 2 | Per ARexx-RULE.md |
| Connect to other databases (ODBC/JDBC) | Gap | - | Not planned: no such stack on AmigaOS |

### Math

| Feature (Math) | Status | Phase | Notes |
| --- | --- | --- | --- |
| Formula editor (markup + palette), ODF MathML | Not planned | - | Dale, 11 Nov 2026: keep Math out of the suite |
| Formulas inside Writer/Impress | Not planned | - | |

## 3. Phases

- **Phase 1 (now, the four workstreams below).** The features that most often
  stop someone using GFOffice instead of OpenOffice: review in Writer, analysis
  in Calc, authoring views and PPTX save in Impress, a working Base.
- **Phase 2.** Mail merge and indexes; Calc's conditional-format scales,
  protection, text-to-columns; Impress masters and custom animation; Draw's
  vector page shell; Base queries, forms, reports and CSV.
- **Phase 3.** Forms, master documents, embedded objects, change tracking in
  Calc, connectors and technical drawing, media.

The suite parity target for the end of Phase 1 is about 62% (from 49%):
Writer 72%, Calc 74%, Impress 76%, Base 25%. Each workstream re-scores its app
in its own parity file (below) when a deliverable lands.

## 4. Four workstreams that do not share files

Each workstream owns a set of files. A workstream changes only its own files.
If it needs a change in another stream's file, it asks that stream (a message
to its thread) instead of making it. One deliverable at a time; each is a PR on
a `team2/<topic>-<yyyymmdd>` branch, marked ready, with host tests and an
Instance-32 lab check.

### Shared files nobody in the four streams edits

`README.md`, `DESIGN.md`, `docs/gfoffice/PARITY-GAPS-*.md`, this plan,
`Makefile` above the hook, `build-os3.sh` above the hook, `build-library.sh`,
`libowf/include/owf.h` (Writer may add, see W), `libowf/src/owf_internal.h`,
`libowf/src/owf_common.c`, `owf_xml.c`, `owf_zip.c`, `owf_unzip.c`,
`owf_inflate.*`, `owf_buf.c`, `owf_sys.c`, `tools/owconvert.c`,
`third_party/`, `app/ow_stack.*`, `app/ow_clipboard.*`, `app/ow_fontwidths.*`.
A bug found in one of these goes to this plan's thread, which makes the fix as
its own PR.

### Build hooks (added by this PR)

- `Makefile` ends with `-include $(sort $(wildcard mk/*.mk))`. Each stream adds
  its host test targets in its own `mk/<stream>.mk` and hooks them to `test:`
  and `check:` there.
- `build-os3.sh` runs every `build.d/*.sh` before copying the toolbar pictures.
  A stream that needs a new program or a changed link line adds
  `build.d/<stream>.sh`. Existing link lines already take every file in
  `src/calc/`, `src/deck/`, `src/core/` and `libowf/calc/` by wildcard, so a new
  `.c` file there needs no build change.
- New libowf code that only one app needs goes in that app's directory, never
  in `libowf/src/` (every `libowf/src/*.c` is linked into GFOpenWrite).

### W: Writer review and references (GFOpenWrite)

Owns: `app/openwrite.c`, `app/openwrite2.c`, `app/ow_autosave.*`,
`app/ow_print.*`, `app/ow_spell.*`, `src/core/` (except `ow_layout.c`'s
deck-facing functions, which it may extend but not change),
`include/openwrite_core.h`, `include/ow_layout.h`,
`libowf/src/owf_doc.c`, `owf_style.c`, `owf_format.c`, `in_odt.c`, `out_odt.c`,
`in_docx.c`, `out_docx.c`, `in_rtf.c`, `out_rtf.c`, `out_html.c`, `in_text.c`,
`in_ftxt.c`, `in_finalwriter.c`, `in_prowrite.c`, `in_wordworth.c`, the
document part of `libowf/include/owf.h` (adds only), `tools/owc_doc.c`,
`libowf/library/owfwrite_*` and its `sdk/` files, `tests/core/`, `Rexx/` Write scripts,
`Dictionaries/`, `mk/writer.mk`, `build.d/writer.sh`, `docs/parity/writer.md`.
Also GFOpenLayout (DalsinAI/gfopenlayout) for footnote page allocation.

Order: W1 track changes (model, record, show, accept/reject, ODT
`text:tracked-changes` and DOCX `w:ins`/`w:del` round trip); W2 comments; W3
fields (document info, cross-references, user fields); W4 footnotes and
endnotes; then Phase 2 (mail merge, indexes, AutoCorrect).

### C: Calc analysis (GFOpenCalc)

Owns: `app/opencalc.c`, `src/calc/`, `libowf/calc/`, `include/opencalc_core.h`,
`libowf/include/owf_calc.h`, `libowf/src/owf_book.c`, `owf_booknum.c`,
`owf_chart.c`, `owf_cfb.c` (shared read-only with Publisher: no behaviour
changes), `owf_xlformula.c`, `owf_xlfunc.c`, `in_xls.c`, `in_xlsx.c`,
`out_xlsx.c`, `in_ods.c`, `out_ods.c`, `in_csv.c`, `out_csv.c`,
`tools/owc_book.c`, `libowf/library/owfcalc_*` and its `sdk/` files, `tests/calc/`,
`tests/calc_core/`, `tests/book/`, `Rexx/` Calc scripts, `mk/calc.mk`,
`build.d/calc.sh`, `docs/CALC_ENGINE.md`, `docs/parity/calc.md`.

Order: C1 pivot table field layout (rows/columns/data/page fields, several
data fields, filters, refresh) with ODS `table:data-pilot-table` and XLSX
pivotCache round trip; C2 Solver (linear simplex, integer by branch and
bound); C3 scenarios; C4 consolidate; C5 named-range and database-range
manager plus advanced filter; C6 automatic data-table recalc.

### P: Impress authoring (GFOpenPresentation)

Owns: `app/openpresent.c` (shared with GFOpenPublisher: keep Publisher
building and its 67 adapter checks green), `src/deck/`,
`include/openpresentation_core.h`, `libowf/include/owf_deck.h`,
`libowf/src/owf_deck.c`, `owf_deck_int.h`, `in_odp.c`, `out_odp.c`,
`in_pptx.c`, `owf_pdf.c`, `owf_pdffonts.c`, `tools/owc_deck.c`,
`libowf/library/owfpresent_*` and its `sdk/` files, `tests/deck/`, `Rexx/` Presentation
scripts, `mk/present.mk`, `build.d/present.sh`,
`docs/gfoffice/Design-OpenPresentation.md`, `docs/gfoffice/Impress-Parity.md`,
`docs/parity/impress.md`. Not `src/pub/` or `in_pub.c`.

Order: P1 slide sorter view; P2 notes view, notes editing and notes/handout
printing; P3 outline view; P4 PPTX save (new writer under `src/deck/`, not
`libowf/src/`); P5 multi-select, distribute, group/ungroup.

### B: Base foundation (GFOpenBase)

Owns everything new: `app/openbase.c`, `src/base/`, `include/openbase_core.h`,
`libowf/base/` if it needs format code, `tests/base/`, `Rexx/` Base scripts,
`mk/base.mk`, `build.d/base.sh`, `docs/gfoffice/Design-OpenBase.md`,
`docs/parity/base.md`. Uses DalsinAI/gfopensqlite (already the design's
engine) and may make PRs there. Reads, never edits, `third_party/opengadtools`.

Order: B0a locking/journal/RAM: probe on an Instance-32 lab (design section
3); B0b core: create/open `.oadb`, schema metadata, table create/alter, rows
add/edit/delete in transactions, host tests; B0c the GFOpenBase window: table
list, table designer, datasheet; B0d primary keys and relationships.

### Later streams

Draw D2 (vector page shell in gfopendraw on `owf_deck`) starts after P5 lands,
because it extends the deck model the P stream owns. Math is not planned (Dale, 11 Nov 2026).

## 5. Rules every stream follows

- Branches `team2/<topic>-<yyyymmdd>`; author SacredTrees; the last line of
  each commit and PR body is "Team 2"; no AI names in repo text.
- Native only. OpenOffice is a reference for what a feature does, never a
  source of code, dialog layouts, strings or icons.
- Builds on origimagic or the stoves; never in a cloud container. Labs in
  copies of Instance-32 (WorkingSpace team2/labs while DevDrive is down);
  Instance-40 is never touched.
- A feature is done when it edits, saves, reloads and undoes, passes host tests
  and ASan, and is checked on an Instance-32 lab copy. Merging and deploying
  need Dale's word.

# GFOffice gap analysis against Apache OpenOffice and Microsoft Publisher

Verified 9 November 2026 against GFOffice `main` through implementation commit
`4e0d6db89b77afbfc1e243bf7a104ba2c6e314b2` (RC76 plus shared native table
authoring), GFOpenLayout
`2fb0eaee2f135fb6d94ef0ac88f3c32e2679cbce`, and GFOpenDraw
`ca07afc25e793e4c4fdc5ae16979bebccd203eaa`.

This replaces older phase/checklist impressions with the current code, tests,
cross-builds, release artefacts and application versions. Percentages are
workflow-weighted engineering estimates, not menu counts. They measure whether
an ordinary user can complete the comparator's documented workflow with
comparable editing and persistence. Writer, Calc, Presentation, Draw and Base
are compared with their Apache OpenOffice counterparts; GFOpenPublisher is
compared with Microsoft Publisher 2021, the final perpetual desktop release.
A feature that only imports or displays receives less credit than one that
edits, saves, reloads and undoes correctly.

## Current position

| GFOffice application | Comparator | Practical parity | Confidence | Current verified strengths | Largest remaining gaps |
| --- | --- | ---: | --- | --- | --- |
| GFOpenWrite 2.0.26 | Writer | **63%** | Medium-high | Native text editing; character and paragraph formatting; styles; lists; editable tables; inline/floating pictures; text boxes; headers/footers; TOC; sections and newspaper columns; DOCX/ODT/RTF; PDF/print; spelling, recovery and ARexx | Track changes; comments/review UI; mail merge/labels/envelopes; alphabetical and object indexes/bibliography; master documents; forms; richer style families; advanced tables; embedded charts/sheets/media; autocorrect/thesaurus/hyphenation |
| GFOpenCalc 0.1.67 | Calc | **62%** | High for delivered core, medium overall | XLS/XLSX/ODS/FODS/CSV; 277-function formula engine; editing/formatting; sort/filter; comments, validation and conditional formatting; charts; subtotals; refreshable pivot summaries; Goal Seek; durable, manually refreshable one/two-variable and multi-formula Multiple Operations; PDF/print; robust host corpus | Automatic data-table recalc and native Excel/Calc data-table metadata; full Pivot Table field layout/filter/drill/multiple data fields; Solver; scenarios; consolidation; richer database filters/functions; external data/links; sharing/change tracking; macro recorder/IDE; more chart/graphics editing |
| GFOpenPresentation 0.6.19 | Impress | **64%** | Medium-high | ODP/FODP read/write, PPTX import; slide CRUD/reorder/hide; masters/layout model; drag/Shift/Select-All character ranges with replacement typing and exact bold/italic/underline/size/RGB/font formatting; exact cross-paragraph FTXT Cut/Copy/Paste; system clipboard Cut/Copy/Paste for supported top-level objects with font/picture remapping; wrapped Find/Find Next across editable slide text; Replace plus atomic Replace All in the active text box; paragraph alignment plus direct left/first/right indent ruler; optional half-inch page grid plus fixed margin/centre guides with move/resize snapping; native table insertion and cell-safe rich-text editing with Tab navigation and ODP persistence; editable 1-16 column count, exact gap and four frame margins with ODP persistence/PPTX import; shared imported object-wrap rendering including ODF Tight contours; inserted text boxes/pictures/shapes; undoable four-edge picture crop; object move/resize/rotate/arrange/align/appearance; transitions; slideshow; PDF/print; undo | PPT/PPTX export; Normal/Outline/Notes/Handout/Sorter views; notes editing and notes/handout printing; multi-selection/distribute; custom guides; master editing; richer fills/transparency/shadows; table row/column/merge/style editing; charts/OLE/media editing; custom animation; group/chart/opaque-object and slide clipboard formats; spelling; ARexx |
| GFOpenPublisher 0.3.13 | Microsoft Publisher 2021 | **58%** | Medium | PUB import; positioned multi-page display; exact linked-story continuation across pages; ordered middle/tail detach and insertion with Undo and ODP persistence; layout-derived selected-frame overflow badge; drag/Shift/Select-All character ranges, replacement typing and exact run formatting; FTXT text and supported top-level object Cut/Copy/Paste; wrapped Find/Find Next across editable publication text; Replace plus atomic Replace All in the active text box; direct left/first/hanging/right paragraph-indent ruler; optional half-inch page grid plus fixed margin/centre guides with move/resize snapping; native table insertion and cell-safe rich-text editing with Tab navigation and ODP persistence; editable 1-16 column count, exact gap and four frame margins; undoable Square, Tight and Top/Bottom wrap with per-side distance and ODP persistence; Tight reuses GFOpenLayout 0.17 closed-shape paths for ellipses, rounded rectangles, polygons and rotation; editable inserted picture/shape objects; undoable four-edge picture crop; move/resize/rotate/arrange/align/look; ODP save; PDF/print | Native PUB save; decode original PUB formatting/columns/wrap/crop; picture-alpha/custom contours; master pages and facing-page publications; custom ruler guides and baseline grid; templates/building blocks; group/chart/opaque-object clipboard; spelling; recolour/transparency; table row/column/merge/style editing; mail/catalog merge; commercial-print/preflight/design checker |
| GFOpenDraw 0.2.0 RC2 | Draw | **19%** | High | Practical indexed-raster paint editor; two native front ends; ARexx; reusable host/m68k painting library; suite packaging | Multi-page vector document shell; ODG/SVG; editable text/shapes/pictures; layers/master pages; connectors/glue points; curves; multi-selection/grouping; snap/grid/guides; dimensions; technical drawing; vector PDF/clipboard. Raster-side text, polygons/curves, gradients, symmetry, spare page and CRNG persistence also remain |
| GFOpenBase design 0.1 | Base | **4%** | High | Current design, SQLite/OADB architecture and reusable suite components are documented | No executable or core yet. Required baseline: project create/open; table designer and datasheet CRUD; keys/relationships; SQL/query designer; forms/subforms; reports; CSV/DBF/Calc exchange; locking/recovery; ARexx/runtime |

The weighted suite position is approximately **49% practical parity against
the stated comparators**. This is not a release-readiness percentage: released
features have substantially stronger automated validation than the remaining
breadth suggests.

## Evidence behind the comparison

Apache documents Writer as including styles, frames/columns/tables, embedded
objects, master documents, tracked changes, database integration, mail merge,
indexes and PDF export. Its Writer overview and current guide contents are the
comparison baseline:

- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Writer_Guide/Introducing_Writer/What_is_Writer%3F>
- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Getting_Started/Getting_Started_with_Writer>

Apache's Calc guide makes Pivot Tables, consolidation, subtotals, scenarios,
Goal Seek and multi-variable Solver separate first-class analysis workflows.
RC64 now covers Goal Seek, subtotals and all principal Multiple Operations
shapes, but not the complete Pivot, consolidation, scenario or Solver
workflows:

- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Getting_Started/Getting_Started_with_Calc/Analyzing_Data>
- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Calc_Guide/Pivot_Tables/Functions_in_Detail/The_Pivot_Table_Dialog/Basic_Layout>

Apache Impress exposes Normal, Outline, Notes, Handout and Slide Sorter views,
plus masters, custom animation and transition sidebars. GFOpenPresentation's
core slide editor and show are real, while those authoring surfaces remain the
main breadth gap:

- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Getting_Started/Getting_Started_with_Impress/The_Main_Impress_Window>

Apache Draw is primarily a vector, page and layer application with connectors,
dimensions, snapping and Bezier curves. GFOpenDraw is currently primarily a
raster paint application, so its useful paint maturity must not be mistaken for
Draw parity:

- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Getting_Started/Getting_Started_with_Draw/What_Is_Draw%3F>
- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Draw_Guide/Tips_and_Tricks/Splitting_Drawings_on_Multiple_Layers>

Apache Base's essential loop is relational tables and relationships, queries,
forms/subforms and reports. A design document alone earns only architecture
credit:

- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Getting_Started/Getting_Started_with_Base/What_is_Base%3F>
- <https://wiki.openoffice.org/wiki/Documentation/AOO4_User_Guides/Getting_Started/Getting_Started_with_Base/Creating_Reports>

Microsoft Publisher is the DTP comparator. Its defining workflows include
independently formatted and columned text boxes, linked-story overflow across
pages, master pages and layout/baseline guides, plus data-driven catalog merge.
Microsoft retired Publisher from Microsoft 365 in October 2026 and ended
Publisher 2021 support on 13 October 2026; that makes PUB rescue and a faithful
replacement more important, not a reason to compare GFOpenPublisher with an
unrelated Apache application:

- <https://support.microsoft.com/en-us/publisher/add-text-and-link-text-boxes-in-publisher>
- <https://support.microsoft.com/en-us/publisher/structure-the-page-with-layout-guides>
- <https://support.microsoft.com/en-us/publisher/create-a-catalog-merge>
- <https://support.microsoft.com/en-us/publisher/microsoft-publisher-will-no-longer-be-supported-after-october-2026>

## What belongs in GFOpenLayout

GFOpenLayout already supplies the shared pagination, section/column flow,
positioned frames, wrap geometry, hit testing, page cuts, deck/object placement,
crop/rotation geometry and render planning used by Writer, Publisher and
Presentation. New shared layout work is justified when at least two apps need
the same geometry or flow rule.

| Gap | Put in GFOpenLayout? | Reason |
| --- | --- | --- |
| Publisher columns inside bounded linked frames | **Delivered in GFOpenLayout 0.15** | General bounded multi-column story flow also serves Writer text boxes and presentation frames |
| Publisher wrap around frames and shapes | **Delivered in GFOpenLayout 0.16** | Box-local exclusion geometry and line fitting reuse the established Writer wrap fitter without affecting sibling flows |
| Publisher Tight wrap around closed shapes | **Delivered in GFOpenLayout 0.17** | Shared path flattening supplies conservative contour bands for ellipses, rounded rectangles, polygons and rotation; Publisher only translates coordinates |
| Footnote/endnote page allocation | **Yes** | It changes pagination and region allocation, independent of Writer UI |
| Rich table pagination, repeated headings, split/keep rules | **Yes** | Shared document layout and PDF/screen fidelity |
| Incremental reflow and reusable thumbnail/page plans | **Yes** | Cross-app performance service with stable invalidation contracts |
| Draw/Presentation connector routing and snap targets | **Partly** | Geometry primitives and hit/snap queries belong in OpenLayout; document commands and object ownership do not |
| Calc Solver, Pivot, scenarios, consolidation, Multiple Operations metadata | **No** | Workbook model, formula engine and application-core operations |
| Writer change tracking, mail merge, indexes and forms | **No** | Document semantics and editing commands; only final layout uses OpenLayout |
| Presentation notes/handout commands and custom-animation timeline | **No** | Deck model and application state; OpenLayout should only render supplied page plans |
| Base tables, SQL, queries, forms and reports | **No** | Database/application model; generated reports may later consume Writer/OpenLayout |

## Ranked closure plan

1. **Calc C14 — durable what-if tables (delivered in RC65).** Store Multiple Operations table,
   formula and input references in hidden sheet-scoped names, survive XLSX/ODS,
   refresh atomically after source changes, then integrate refresh with normal
   recalculation. This closes the only known semantic weakness in RC64's newly
   delivered feature without adding anything to OpenLayout.
2. **Publisher P1c — rich text frames on OpenLayout (format controls, bounded
   columns and rectangular object wrap delivered).** GFOpenLayout 0.16 now
   owns fixed-height multi-column flow and scoped exclusion fitting; Publisher
   and Presentation share model, Undo/Redo and ODP interchange.
   Character-range selection, replacement and exact run formatting are now
   delivered in the shared deck editor, as is ordered middle-chain detach and
   insertion. Contour wrap, editable picture crop, exact text clipboard and
   the shared paragraph-indent ruler and supported top-level object clipboard
   exchange are now delivered. Editable
   frame margins, column gaps, per-side wrap distances, safe tail link/unlink
   and a layout-derived overflow badge are also delivered.
3. **Writer W-review — tracked changes first, mail merge second.** Preserve
   revisions as editable review records rather than accepting them on import;
   then build CSV/Calc-field merge as a reusable document-generation service
   which Publisher and Base reports can consume.
4. **Calc C15/C16 — full Pivot interaction, then Solver/scenarios/consolidate.**
   Extend the existing refreshable pivot metadata rather than introducing a
   second aggregation implementation.
5. **Presentation I7 — authoring views.** Notes editing/printing and a real
   slide sorter before custom animation; reuse deck/page plans and do not add a
   parallel layout engine.
6. **Draw D2 — vector page shell.** Reuse `owf_deck` and OpenLayout for pages,
   objects and display; keep `libgfopendraw` as the raster engine embedded by
   picture objects.
7. **Base B0 — executable foundation.** Only after the SQLite locking probe:
   create/open OADB, schema metadata, table designer and datasheet CRUD with
   transactions and host tests. Reports should target Writer's document model.

## Validation status

The immutable RC70 evidence remains the complete normal GFOffice host suite:
1,606 Calc-core checks, 10,491 formula/oracle checks, 279 shared deck-editor
checks, 67 Publisher-adapter checks, the damaged-input corpora, library ABI
checks and 3,943 cut-short document trials. RC70 was published from source
commit `1c08bb427230b412f937bb343742dc65895f2d76`; its public archive downloaded
back with SHA-256
`bbcedb75f99cf7fef39095168f74382551064a8ac9f629b34d6cab9c85ede9ad`,
and all 80 files covered by its internal manifest verified.

Immutable RC71 evidence is newer and focused on the post-RC70 work. The shared
deck editor/interchange path passed 298 optimized checks and
the same 298 checks under ASan/UBSan with leak detection disabled because
LeakSanitizer cannot run under the environment's tracer. These checks cover
character ranges, ordered linked-flow surgery, Square/Top-Bottom/Tight wrap,
measured ellipse corner gain, central contour exclusion, per-side distances,
Undo/Redo and ODP `style:wrap-contour` round trip. The deck core passed 201
checks; 1,250 cut-short plus 67 damaged ODP cases and 905 cut-short plus 95
damaged PPTX cases passed. Optional `odfpy` grammar validation was unavailable.

A clean exact-pinned GFOpenAmigaGCC 16.2.0-open6 build at implementation
commit `c092aeceb8c404d2c73b3ee95b48f5cfac0e3720` rebuilt all five linked
applications, Draw/DrawOL, OWConvert, five GFOffice resident libraries, both
library tests and the supported library-linked variants with warnings as
errors. It fetched GFOpenLayout 0.17 commit
`2fb0eaee2f135fb6d94ef0ac88f3c32e2679cbce`, GFOpenDraw
`ca07afc25e793e4c4fdc5ae16979bebccd203eaa` and GFOpenGadTools
`01c1fd9a7881863e3a2c462348f6eb373fb00c5c` from their recorded pins.

RC71 was published from source commit
`13001f4aa46e59d0d962c88bc53623d526fc8695`; its public archive downloaded
back with SHA-256
`2c47a365cb5f6b7abed73a0ac92afb52705cf9007a9e4e53e8430cf297879cbb`, and
all 80 files covered by its internal manifest verified.

RC72 packages the post-RC71 picture-crop and clipboard work. Commit
`d8295ea25bd90914fce5c43268f0e493f46b3d93` passed 302 focused optimized
checks and the same checks under ASan/UBSan with leak detection disabled for
the tracer. A clean Open6 build rebuilt all five linked apps with warnings as
errors: Calc 1,474,596 bytes, Presentation 793,560, Publisher 827,140, Writer
625,356 and OWConvert 752,528. RC72 was published from source commit
`f21a16b55430987f3308dd3488f4d5b8af501a24`; its public archive downloaded
back with SHA-256
`e1a6a24d761d922b868f37cac59f44869e91e77d533a64d5d5115605abe08908`, and
all 80 files covered by its internal manifest verified.

Post-RC72 Find/Find Next commit
`9017eebf0e6913b514b84fae28af72904a953ce6` passed 304 optimized deck-editor
checks and the same 304 checks under ASan/UBSan with leak detection disabled
for the tracer. A clean Open6 build rebuilt all five linked apps with warnings
as errors: Calc 1,474,596 bytes, Presentation 795,840, Publisher 829,408,
Writer 625,356 and OWConvert 752,528. This work is published on `main` but is
not part of immutable RC72.

Post-RC72 paragraph-ruler implementation commit
`bee699775e2f16af4b0bde13dbeaa320a692b8db` passed 307 optimized deck-editor
checks and the same 307 checks under ASan/UBSan with leak detection disabled
for the tracer. A fresh warnings-as-errors Open6 build produced Calc 1,474,596
bytes, Presentation 797,972, Publisher 831,560, Writer 625,356 and OWConvert
752,528. The existing OpenGadTools ruler is now shared by Presentation and
Publisher; deck core 0.22.0 owns validated paragraph-range indents, Undo/Redo
and ODP persistence.

RC73 packages the post-RC72 Find/Find Next and paragraph-ruler work from source
commit `5e39ca8565ab97c4c4dd6bd60c8cb7dddef67179`. Its public 4,536,661-byte
archive downloaded back with SHA-256
`c5ef4cef625951702faab30f9da02ee35d01083877310305413d87c29c0d9f94`, and
all 80 files covered by its internal manifest verified. The live annotated tag
peels to the source commit and the GitHub release remains marked prerelease.

Post-RC73 object-clipboard implementation commit
`3578c7ccd2e11680dd1afc607b9f0f886079791b` passed 316 optimized deck-editor
checks and the same 316 checks under ASan/UBSan with leak detection disabled
for the tracer. The tests export/import the source through actual ODP, then
paste text and picture objects into a different deck while verifying font and
image resource remapping plus Undo/Redo. A fresh warnings-as-errors Open6
build produced Calc 1,474,596 bytes, Presentation 803,256, Publisher 836,968,
Writer 625,356 and OWConvert 752,528. The feature is published on `main` but
is not part of immutable RC73.

RC74 packages that object-clipboard work from source commit
`c88499764d69e51ba5f21e97516526372cf632e4`. Its public 4,541,227-byte archive
downloaded back with SHA-256
`2ec34e2dc4b6fac3f673c68ad4626998590da0193ce14359841ab7946a28ae78`, and
all 80 files covered by its internal manifest verified. The live annotated tag
peels to the source commit and the GitHub release remains marked prerelease.

Post-RC74 Replace/Replace All implementation commit
`79a536be9e5249581b9fe805d605351794f61d6c` passed 321 optimized deck-editor
checks and the same 321 checks under ASan/UBSan with leak detection disabled
for the tracer. The regression covers case-insensitive non-overlapping
replacement, one-step Undo/Redo and stale-byte removal across adjacent
formatted runs. A fresh warnings-as-errors Open6 build produced Calc 1,474,596
bytes, Presentation 805,972, Publisher 840,452, Writer 625,356 and OWConvert
752,528. The feature is published on `main` but is not part of immutable RC74.

RC75 packages that replacement work from source commit
`54e88d5ad612703afdb252dd0ef797718f7a4afa`. Its public 4,551,833-byte
archive downloaded back with SHA-256
`b55e1fb611265a8fd80e8191488c73f709b40c6b7bd51f4e9914396d8b7f0a83`, and
all 80 files covered by its internal manifest verified. The live annotated tag
peels to the source commit and the GitHub release remains marked prerelease.

Post-RC75 grid/snap implementation commit
`f5b73c5dc6413a5c1595e49d529aec0a2ca2cb92` passed 322 optimized deck-editor
checks and the same 322 checks under ASan/UBSan with leak detection disabled
for the tracer. A fresh warnings-as-errors Open6 build produced Calc 1,474,596
bytes, Presentation 807,796, Publisher 842,296, Writer 625,356 and OWConvert
752,528. Grid and snap state stays in the shared editor shell; only completed
object boxes enter the existing Undo and document paths. This work is
published on `main` but is not part of immutable RC75.

RC76 packages that grid/snap work from source commit
`3b40018783cebb50116362c466fbdcf02e81ab4f`. Its public 4,554,137-byte
archive downloaded back with SHA-256
`7ef4ed996e2cac4b8083b0f5f5533ed5e713142051d0377a38b73d82485ed643`, and
all 80 files covered by its internal manifest verified. The live annotated tag
peels to the source commit and the GitHub release remains marked prerelease.

Post-RC76 fixed-guide implementation commit
`094c9528e7f050cf84931e84f348a65ed6ecdd1d` passed 323 optimized deck-editor
checks and the same 323 checks under ASan/UBSan with leak detection disabled
for the tracer. A fresh warnings-as-errors Open6 build produced Calc 1,474,596
bytes, Presentation 809,220, Publisher 843,780, Writer 625,356 and OWConvert
752,528. Fixed margin/centre guides are editor state; the portable core owns
only thresholded box-anchor geometry. This work is published on `main` but is
not part of immutable RC76.

Post-RC76 native-table implementation commit
`4e0d6db89b77afbfc1e243bf7a104ba2c6e314b2` passed 333 optimized deck-editor
checks and the same 333 checks under ASan/UBSan with leak detection disabled
for the tracer. Coverage creates a tagged-cell table, edits a cell, protects
cell boundaries, verifies transactional invalid input, Undo/Redo and ODP
save/reopen. A fresh warnings-as-errors Open6 build produced Calc 1,474,596
bytes, Presentation 812,684, Publisher 847,124, Writer 625,356 and OWConvert
752,528. This work is published on `main` but is not part of immutable RC76.

There is no recorded Dale on-Amiga smoke test for RC70 through RC76.
Host tests and cross-build success must not be reported as Amiga runtime
evidence. RC76 still requires launching Publisher, Presentation and Writer;
checking grid display and move/resize snapping at several zoom levels,
Replace/Replace All on plain and mixed-format text, cross-app object and FTXT
Cut/Copy/Paste plus Undo/Redo, Find across objects/pages, all three ruler
markers, picture crop, Square versus Tight wrap, and ODP save/reopen before
promotion.

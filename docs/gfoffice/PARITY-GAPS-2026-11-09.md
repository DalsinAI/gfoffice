# GFOffice gap analysis against Apache OpenOffice and Microsoft Publisher

Verified 9 November 2026 against GFOffice `main` through implementation commit
`be958ce169d0655aecf3988be045896998dabcb1` (GFOffice 2.0.25 RC67 candidate),
GFOpenLayout `08e9e98bd91c19f02369bda7511027bcdb4359ea`, and GFOpenDraw
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
| GFOpenPresentation 0.6.3 | Impress | **54%** | Medium-high | ODP/FODP read/write, PPTX import; slide CRUD/reorder/hide; masters/layout model; text editing plus paragraph-level bold/italic/underline/size/alignment; bounded equal text columns with ODP persistence/PPTX import; inserted text boxes/pictures/shapes; object move/resize/rotate/arrange/align/appearance; transitions; slideshow; PDF/print; undo | PPT/PPTX export; character-range selection and font/colour UI; Normal/Outline/Notes/Handout/Sorter views; notes editing and notes/handout printing; multi-selection/distribute; master editing; richer fills/transparency/shadows; table structure editing; charts/OLE/media editing; custom animation; clipboard; find/spell; ARexx |
| GFOpenPublisher 0.2.5 | Microsoft Publisher 2021 | **40%** | Medium | PUB import; positioned multi-page display; exact linked-story continuation across pages; editable text with paragraph-level bold/italic/underline/size/alignment; one/two/three-column bounded frames through GFOpenLayout with Undo and ODP persistence; PPTX/ODP column import; editable inserted picture/shape objects; move/resize/rotate/arrange/align/look; ODP save; PDF/print | Native PUB save; decode original PUB formatting/columns and add character-range/font/colour editing; interactive link/unlink and overflow UI; custom gaps/autofit/margins within text boxes; wrap/exclusion flow; master pages and facing-page publications; guides/snap/rulers/baselines; templates/building blocks; crop/recolour/transparency; richer tables; mail/catalog merge; commercial-print/preflight/design checker |
| GFOpenDraw 0.2.0 RC2 | Draw | **19%** | High | Practical indexed-raster paint editor; two native front ends; ARexx; reusable host/m68k painting library; suite packaging | Multi-page vector document shell; ODG/SVG; editable text/shapes/pictures; layers/master pages; connectors/glue points; curves; multi-selection/grouping; snap/grid/guides; dimensions; technical drawing; vector PDF/clipboard. Raster-side text, polygons/curves, gradients, symmetry, spare page and CRNG persistence also remain |
| GFOpenBase design 0.1 | Base | **4%** | High | Current design, SQLite/OADB architecture and reusable suite components are documented | No executable or core yet. Required baseline: project create/open; table designer and datasheet CRUD; keys/relationships; SQL/query designer; forms/subforms; reports; CSV/DBF/Calc exchange; locking/recovery; ARexx/runtime |

The weighted suite position is approximately **47% practical parity against
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
| Publisher wrap around frames and shapes | **Yes** | Exclusion geometry and line fitting are renderer/layout concerns shared with Writer |
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
2. **Publisher P1c — rich text frames on OpenLayout (format controls and
   bounded columns delivered).** GFOpenLayout 0.15 now owns fixed-height
   multi-column flow, clipping, hit testing and caret placement; Publisher and
   Presentation share commands, Undo/Redo and ODP/PPTX interchange. Next add
   exclusion/wrap services to GFOpenLayout, then interactive link/unlink,
   overflow UI and character-range formatting in the shared deck editor.
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

Current-session RC67 host evidence at this revision is the complete normal
GFOffice suite, including 1,606 Calc-core checks, 10,491 formula/oracle checks,
246 shared deck-editor/interchange checks, 67 Publisher-adapter checks, the
ODP/PPTX/PUB damaged-input corpora, library ABI checks and 3,943 cut-short
document trials. All passed against the pinned GFOpenLayout 0.15 candidate.
The new 246-check path also passed ASan/UBSan with leak detection disabled;
LeakSanitizer cannot run under this environment's tracer. Optional `odfpy` and
`openpyxl` third-party read-back checks were skipped because those packages are
not installed. A clean Open6 build from the exact pinned GFOpenLayout,
GFOpenDraw and GFOpenGadTools commits rebuilt all six apps, Draw/DrawOL,
OWConvert, five resident libraries, both library tests and the supported
library-linked variants. Public archive download/hash verification remains a
release task and must not be inferred from the build evidence above.

There is no recorded Dale on-Amiga smoke test for RC67. Host and cross-build
evidence must not be reported as Amiga runtime evidence.

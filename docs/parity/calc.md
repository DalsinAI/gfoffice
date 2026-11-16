# GFOpenCalc against OpenOffice Calc

Stream C of docs/openoffice-parity-plan.md. Practical parity is the
workflow-weighted estimate of the 9 November gap analysis
(docs/gfoffice/PARITY-GAPS-2026-11-09.md): an ordinary user finishing
OpenOffice Calc's documented workflow, with editing, save, reload and Undo
working. Phase 1's target is 74%.

| Date | Deliverable | Practical parity |
| --- | --- | ---: |
| 9 Nov 2026 | Gap analysis | 62% |
| 11 Nov 2026 | C1 pivot table field layout | 65% |
| 11 Nov 2026 | C2 Solver | 67% |
| 11 Nov 2026 | C3 Scenarios | 68% |
| 11 Nov 2026 | C4 Consolidate | 69% |
| 11 Nov 2026 | C5 Names manager, advanced filter | 71% |
| 11 Nov 2026 | C6 Multiple Operations kept live | 72% |
| 12 Nov 2026 | C7 Text to Columns, Remove Duplicates | 73% |
| 12 Nov 2026 | C9 Data bars and icon sets | 74% |
| 12 Nov 2026 | C8 Sheet and cell protection | 75% |
| 16 Nov 2026 | Re-score: pivot items sorted, hidden, grouped and shown as percentages (#221, #222, #226, #229, #289, #293, #299); Multiple Operations past 4096 results (#225); the shown scenario's frame (#302); Consolidate sources from named ranges (#303) | 76% |

## Phase 1 features

| Feature | Status | Deliverable | Notes |
| --- | --- | --- | --- |
| Pivot tables | Done (most) | C1 | Row, column, data and page fields; several data fields; page-field filters; grand totals on/off; refresh; layout afresh; row and column items sorted ascending or descending (as Data > Sort orders cells); single items hidden, their rows out of the totals; items grouped by number range, year, quarter or month, or by hand into named groups; data shown as % of row, column or grand total, or as an index; ODS `table:data-pilot-table` and XLSX pivot cache/table both ways (sorts, hidden items and date/number groups as XLSX fieldGroup; groups made by hand are saved ungrouped, with a note); Undo. Sorts, hidden items, groups and "show data as" chosen in the layout window (per field) and with ARexx PIVOTSET (groups by hand too; lab owed). Not yet: several pivot tables on one sheet |
| Solver (linear, integer) | Done (linear) | C2 | Max, min or value of a target; up to 200 variables and 32 constraints (<=, =, >=, whole, binary; limits as numbers, cells or ranges); variables kept >= 0 or not; own two-phase simplex and branch and bound; linearity checked; model kept in Excel's solver_* names (XLSX and ODS); ARexx SOLVE; Undo. Not yet: models that are not linear, reports, keeping answers as scenarios |
| Scenarios | Done | C3 | Add, show, update, delete; up to 32 a sheet, 32 ranges and 2048 cells each; a formula kept as its value, as Excel keeps it; ranges follow row and column edits; ODS `table:scenario` tables and XLSX `scenarios` both ways; ARexx SCENARIO; Undo; the shown scenario framed on the sheet with its name in a tab, whose drop-down shows another. Not yet: a summary report, protection |
| Consolidate | Done | C4 | Up to 16 sources on any sheets; the eleven functions; by position or by row labels, column labels or both (case aside, labels in first-seen order); linked (formulas over the sources) or values; settings kept on the sheet for the window; ARexx CONSOLIDATE; Undo; a source picked from the named ranges or typed as a name. Data > Refresh Consolidation (ARexx CONSOLIDATE REFRESH) runs it again over the sources as they are now: sources typed as ranges and the table follow rows and columns put in or taken out, so it grows and shrinks with them. |
| Named-range and database-range manager, advanced filter | Done (most) | C5 | Manage Names: list, add, change (name, formula, scope in one step; uses follow a rename), delete; ARexx NAMES, NAMEDEFINE, NAMERENAME, NAMEDELETE. Advanced filter: criteria range (AND across, OR down; <, <=, >, >=, <>, =, begins-with, wildcards), in place or copied to any sheet, unique rows; ARexx ADVFILTER; Undo. Not yet: database ranges as their own objects (a named range serves), criteria formulas (=C2>AVERAGE(C:C)), a rename followed into charts, validation and conditional formats |
| Automatic data-table (Multiple Operations) recalc | Done (most) | C6 | Every table worked out again after each change (edits, Undo, Redo, ARexx), no Undo step of its own; ARexx MULTIOP. Past 4096 results or 20,000 cells they are worked out a few at a time while the program is idle, each table of up to 1024 results (a bigger one: Refresh by hand). Not yet: tables as true formula arrays (MULTIPLE.OPERATIONS) in ODS |

## Phase 2 features

| Feature | Status | Deliverable | Notes |
| --- | --- | --- | --- |
| Text to Columns, Remove Duplicates | Done | C7 | Text to Columns: tab, comma, semicolon, space and any other delimiter, runs of them as one, a text qualifier (a doubled one inside is one), fixed widths; each field General, Text or skipped; a number's shown value splits; ARexx TEXTTOCOLUMNS; Undo. Remove Duplicates: chosen columns or all, text compared without case, a heading row, only the range's cells move (rows kept close up, references follow them); ARexx REMOVEDUPS; Undo. Not yet: a preview of the split, a date order per column (the book's order is used) |
| Colour scales, data bars, icon sets | Done | C9 | Colour scales (two or three colours) as before; data bars drawn in each cell from its lowest number to its highest (Excel's 10% to 90%, or the file's lengths), in any colour, the number shown or not; Excel's 17 icon sets drawn (arrows, grey arrows, traffic lights, signs, symbols and flags as lights, red to black, ratings as signal bars, quarters as greys), reversed or not, the number shown or not; added from Format > Conditional Format ("bar blue", "icons 3Arrows reverse only") and ARexx CONDFMT; kept in XLSX and ODS (show-value both ways, reverse in GFOffice's own attribute); Undo. Not yet: negative bars from an axis, gradient bars, the icons' own pictures (the shapes stand for them), Excel 2010's extensions (kept out) |
| Sheet and cell protection | Done | C8 | Protect a sheet with or without a password (Excel's 16-bit hash) and Excel's options (format cells, columns, rows; insert and delete rows and columns; sort...); cells locked unless made unlocked, formulas hidden; typing, clearing, pasting, cutting, filling, formatting, merging, sorting and row and column changes refused where they would change locked cells or what the sheet does not allow; Sheet menu and ARexx PROTECT, UNPROTECT, CELLPROTECT, ISPROTECTED; XLSX sheetProtection and xf protection, ODS table:protected and style:cell-protect both ways; workbook structure protection (no sheet added, duplicated, deleted, renamed, moved, hidden or shown; optional password; XLSX workbookProtection and ODS table:structure-protected both ways). Sheet > Protect Workbook and ARexx PROTECTBOOK, UNPROTECTBOOK, ISBOOKPROTECTED (lab owed). Not yet: ranges with their own passwords, an ODS password from LibreOffice or an XLSX SHA-512 one checked (kept and written back, not checked) |

## The 16 November re-score

Scored against gfoffice main 72b77cb, the same way as the 9 November gap
analysis: an ordinary user finishing OpenOffice Calc's documented
workflow, with editing, save, reload and Undo working. Only merged work
counts.

- Pivot tables gain the most: sort, hide, group and "show data as" work in
  the core and survive XLSX and ODS, so tables from Excel and LibreOffice
  keep them. They count for less than full marks because the window and
  ARexx can't choose them yet (+1).
- Multiple Operations tables past 4096 results are worked out while the
  program is idle instead of only by hand (+0.3, #225).
- The shown scenario is framed on the sheet with its name (+0.2, #302).
- Consolidate takes named ranges, typed or picked (+0.2, #303).

Waiting to merge, not scored: pivot subtotals under each row item and
after each column item (#294, #296), drill-down (#300), Refresh
Consolidation with tables that grow with their sources (#304), and the
scenario tab's drop-down (#305).

Top three gaps by workflow weight:

1. The pivot table window and ARexx: choosing sorts, hidden items, groups
   and "show data as" (in, lab owed: the layout window's per-field
   choices and ARexx PIVOTSET), then several pivot tables on one sheet.
2. External data and links: other workbooks' cells, a database or a web
   page as a source, refreshed on open. Started: XLSX and ODS keep each
   other book's full path and the values it last had, both ways.
3. Sharing and review: tracked changes in sheets (Edit > Track Changes).
   Workbook structure protection is in, with its Sheet menu items and
   ARexx (lab owed).

## How C1 was checked

- Host tests (tests/calc_core): layouts with two row fields, a column field
  and two data fields; a page field; column fields only; a filter that keeps
  nothing; layout afresh on the same sheet; Undo; XLSX and ODS save, reload
  and refresh; the definition text's escapes and the older form; bad layouts.
  Also under AddressSanitizer and UBSan (`make check`).
- Instance-32 lab copy: see the pull request.

## How C2 was checked

- Host tests (tests/calc_core): the linear programme alone (a small one, an
  equality, free variables, an infeasible one, an unbounded one, a degenerate
  one, whole numbers against the relaxed answer, a binary knapsack, the node
  limit); and on the sheet: maximise, minimise with a whole-number variable,
  a value goal, ranges against ranges of limits, binaries, no limit, no
  answer, not linear, bad models, Undo and Redo, the model in XLSX and ODS.
  Also under AddressSanitizer and UBSan (`make check`).
- Instance-32 lab copy: tests/arexx/calc-solver.rexx; see the pull request.

## How C3 was checked

- Host tests (tests/calc_core): add two scenarios over three ranges (numbers,
  text, a text that looks like a number, a formula kept as its value), names taken and bad
  cells refused, show each (the sheet recalculates), the shown one marked,
  Undo and Redo, a row put in above (the ranges follow), XLSX and ODS save
  and reopen (the ODS scenario tables do not become sheets), update in
  place, delete (the later one's names move down), Undo of a delete. Also
  under AddressSanitizer and UBSan.
- Instance-32 lab copy: tests/arexx/calc-scenario.rexx; see the pull request.

## How C4 was checked

- Host tests (tests/calc_core): two sources on two sheets matched by both
  labels (a label in another case is the same one; labels only one source
  has get their row or column), the settings read back, Undo; by position
  and linked (the formulas follow a changed source cell); Max and Count; a
  destination inside a source, a range that is not one, no sources and a
  wrong function refused. Also under AddressSanitizer and UBSan.
- Instance-32 lab copy: tests/arexx/calc-consolidate.rexx; see the pull
  request.

## How C5 was checked

- Host tests (tests/calc_core): a name renamed with its uses (in a function's
  argument, in another name, not in a string or a longer word, not on a sheet
  whose own name shadows it), Undo and Redo, a new formula, a new scope,
  refused changes; the advanced filter in place (two criteria rows), with
  unique rows, copied with a formula as its value, copied to another sheet,
  refused criteria, destination and lists. Also under AddressSanitizer and
  UBSan.
- Instance-32 lab copy: tests/arexx/calc-names.rexx; see the pull request.

## How C6 was checked

- Host tests (tests/calc_core): a column table follows its model changing on
  another sheet and a trial changing, with no work when nothing changed; Undo
  and Redo of the edits (the table follows, Redo is kept); the unsaved flag.
  Also under AddressSanitizer and UBSan.
- Instance-32 lab copy: tests/arexx/calc-multiop.rexx; see the pull request.

## How C7 was checked

- Host tests (tests/calc_core): commas with a quoted field, a field that
  begins with an apostrophe or "=" kept as text, an empty field, a row with
  fewer fields leaving the cell to its right alone, a formula reading a
  split cell, the selection widened, Undo; spaces merged with a Text column
  and a skipped one; fixed widths and widths that do not rise; a selection
  of two columns refused. Remove Duplicates over every column (text without
  case; formulas into the range follow the rows kept; a cell outside the
  range stays), by one column, nothing repeated (no Undo step), a column
  outside the range refused. Also under AddressSanitizer and UBSan.
- Instance-32 lab copy: tests/arexx/calc-textcols.rexx; see the pull request.
## How C9 was checked

- Host tests (tests/calc_core): a data bar's lengths (10%, 50%, 90% of the
  cell), a green bar with its numbers hidden (no bar for text), 3Arrows'
  three colours, all again after XLSX and after ODS; 5Quarters reversed;
  Undo; an unknown set and a bad bar refused. Also under AddressSanitizer
  and UBSan.
- Instance-32 lab copy: tests/arexx/calc-condfmt.rexx and a screenshot; see
  the pull request.
## How C8 was checked

- Host tests (tests/calc_core): Excel's hash of "test" (CBEB); an unlocked
  cell and a hidden formula; typing, clearing, filling, formatting, cell
  protection, deleting rows and pasting refused on locked cells, the
  unlocked cell taking a value and a formula, the hidden formula not shown;
  all again after XLSX and after ODS; inserting rows allowed; wrong and
  missing passwords; no password. Also under AddressSanitizer and UBSan.
  Conditional formats can't be added or cleared on a protected sheet.
- Amiga: OWF=library and linked builds. Instance-32 lab copy (GFOpenCalc
  0.1.76): tests/arexx/calc.rexx and calc-protect.rexx ALL PASS.

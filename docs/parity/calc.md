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

## Phase 1 features

| Feature | Status | Deliverable | Notes |
| --- | --- | --- | --- |
| Pivot tables | Done (most) | C1 | Row, column, data and page fields; several data fields; page-field filters; grand totals on/off; refresh; layout afresh; ODS `table:data-pilot-table` and XLSX pivot cache/table both ways; Undo. Not yet: subtotals per outer item, hiding single items, sorting items, date/number grouping, drill-down, "show data as" (percent of...), several pivot tables on one sheet |
| Solver (linear, integer) | Done (linear) | C2 | Max, min or value of a target; up to 200 variables and 32 constraints (<=, =, >=, whole, binary; limits as numbers, cells or ranges); variables kept >= 0 or not; own two-phase simplex and branch and bound; linearity checked; model kept in Excel's solver_* names (XLSX and ODS); ARexx SOLVE; Undo. Not yet: models that are not linear, reports, keeping answers as scenarios |
| Scenarios | Done | C3 | Add, show, update, delete; up to 32 a sheet, 32 ranges and 2048 cells each; a formula kept as its value, as Excel keeps it; ranges follow row and column edits; ODS `table:scenario` tables and XLSX `scenarios` both ways; ARexx SCENARIO; Undo. Not yet: the scenario frame and drop-down on the sheet, a summary report, protection |
| Consolidate | Done | C4 | Up to 16 sources on any sheets; the eleven functions; by position or by row labels, column labels or both (case aside, labels in first-seen order); linked (formulas over the sources) or values; settings kept on the sheet for the window; ARexx CONSOLIDATE; Undo. Not yet: a linked table that grows when its sources grow, picking a source from a list of named ranges (names can be typed) |
| Named-range and database-range manager, advanced filter | Done (most) | C5 | Manage Names: list, add, change (name, formula, scope in one step; uses follow a rename), delete; ARexx NAMES, NAMEDEFINE, NAMERENAME, NAMEDELETE. Advanced filter: criteria range (AND across, OR down; <, <=, >, >=, <>, =, begins-with, wildcards), in place or copied to any sheet, unique rows; ARexx ADVFILTER; Undo. Not yet: database ranges as their own objects (a named range serves), criteria formulas (=C2>AVERAGE(C:C)), a rename followed into charts, validation and conditional formats |
| Automatic data-table (Multiple Operations) recalc | Part | C6 | |

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

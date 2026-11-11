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

## Phase 1 features

| Feature | Status | Deliverable | Notes |
| --- | --- | --- | --- |
| Pivot tables | Done (most) | C1 | Row, column, data and page fields; several data fields; page-field filters; grand totals on/off; refresh; layout afresh; ODS `table:data-pilot-table` and XLSX pivot cache/table both ways; Undo. Not yet: subtotals per outer item, hiding single items, sorting items, date/number grouping, drill-down, "show data as" (percent of...), several pivot tables on one sheet |
| Solver (linear, integer) | Done (linear) | C2 | Max, min or value of a target; up to 200 variables and 32 constraints (<=, =, >=, whole, binary; limits as numbers, cells or ranges); variables kept >= 0 or not; own two-phase simplex and branch and bound; linearity checked; model kept in Excel's solver_* names (XLSX and ODS); ARexx SOLVE; Undo. Not yet: models that are not linear, reports, keeping answers as scenarios |
| Scenarios | Gap | C3 | ODS `table:scenario` |
| Consolidate | Gap | C4 | |
| Named-range and database-range manager, advanced filter | Part | C5 | Define Name exists; no list to edit or delete names |
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

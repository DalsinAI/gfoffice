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

## Phase 1 features

| Feature | Status | Deliverable | Notes |
| --- | --- | --- | --- |
| Pivot tables | Done (most) | C1 | Row, column, data and page fields; several data fields; page-field filters; grand totals on/off; refresh; layout afresh; ODS `table:data-pilot-table` and XLSX pivot cache/table both ways; Undo. Not yet: subtotals per outer item, hiding single items, sorting items, date/number grouping, drill-down, "show data as" (percent of...), several pivot tables on one sheet |
| Solver (linear, integer) | Gap | C2 | Own simplex and branch and bound |
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

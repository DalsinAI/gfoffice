# GFOpenProject's editing core and Gantt view (src/project)

Step 2 of amicradle-teams `designs/GFOPENPROJECT-20261113.md`: the portable part of the task sheet and the
Gantt view, host-built and tested (`make project-view-test`, `make project-view-check`; they run when
GFOpenLayout is at `OPENLAYOUT_SRC`, as the deck tests do). The Amiga shell (`app/openproject.c`) is
the next PR and only wires windows, menus and requesters to these calls.

## Editing (`opj_doc`)

- Every edit works on a copy of the plan (`owf_project_copy`). When it succeeds, the copy becomes the
  plan, is scheduled again, and the old plan becomes the Undo step. An edit that fails leaves the plan
  as it was, and so does one that would make a loop of links (a summary linked to its own subtask is
  one, as in MS Project).
- Undo and Redo keep at most `OPJ_UNDO_BUDGET` (512 KB) of saved plans; the oldest go first and
  `undo_lost` says so, as GFOpenCalc does.
- Edits: insert a task (one estimated day, at the level of the row it goes before), delete rows (a
  summary with its subtasks; links to them go), indent and outdent (with subtasks), Link Tasks and
  Unlink Tasks, percent complete, constraints, notes, and any typed cell.
- Typed cells, as MS Project takes them: Duration (`5d`, `3 days`, `2ed`, `1w?`), Start (sets Start No
  Earlier Than) and Finish (Finish No Earlier Than), Predecessors (`2,3SS+1d,4FF-2h,5SF+50%`, by row
  number), % Complete. A summary's duration and dates are worked out, so they can't be typed.
- Dates are shown day first, `Mon 16/11/26`, and read as `16/11/26`, `16/11/2026`, `16.11.26`,
  `2026-11-16`, with or without a weekday and a time.

## The Gantt view (`opj_view`)

- The table on the left (ID, Task Name, Duration, Start, Finish, Predecessors; % Complete and WBS can be
  shown), a 4-pixel bar, then the chart. Names are indented by outline level; summaries are bold.
- The timescale has two tiers: Days (weeks over days, 24 pixels a day), Weeks (months over weeks, 6),
  Months (quarters over months, 2) and Quarters (years over quarters, 1). Days off on the project's
  calendar are shaded at the Days and Weeks zooms.
- Bars: tasks blue, critical tasks red, the done part as a dark band through the middle third;
  summaries as a black band with tabs at both ends; milestones as diamonds at their date.
- Link lines leave the predecessor's start or finish, go 6 pixels out, then down to the successor's row
  and in to its start or finish with an arrow. When there isn't room to come straight in, the line goes
  round along the edge of the successor's row.
- All drawing is GFOpenLayout's (`oldraw_fill`, `oldraw_frame`, `oldraw_text_box`); lines are one-pixel
  fills. `opj_draw` draws only what crosses the area it is given, for `oldraw_redraw`'s damage.
- `opj_hit_test` says what is under the pointer: a column title, the timescale, a cell, the bar
  between the halves, a task's bar (its middle, its right end for stretching, its done part) or the
  chart.

## Resources

- **Resource Names** is a task sheet column (hidden in the Gantt view's table by default). Typing
  `Dale,Bob[50%],Steel[3]` replaces the task's assignments, adding a name not yet in the plan as a
  work resource, as one Undo step. The names are also drawn 6 pixels right of each task's bar.
- **The resource sheet** (`opj_resource_cell_text`, `opj_set_resource_cell`): ID ("1!" when
  over-allocated), Name, Type, Initials, Group, Max. Units, Std. Rate ("20.00/h"; typed as "20",
  "20/h", "160/d" by the project's day, a currency sign first skipped; a material's is a unit's), and
  the worked-out Work and Cost. Names must be unique (case ignored) and free of `,;[]`, so Resource
  Names always reads back. `opj_add_resource`, `opj_delete_resources` (their assignments go too) and
  `opj_set_assignment` are each one Undo step.
- **Resource usage** (`opj_usage_text`): a resource's work, or one assignment's, in a period
  (`opj_period_start`/`opj_period_next`: a day, a week from Monday, a month, a quarter). A period is
  marked over when the resource's work in it passes its calendar's working time there times its max
  units.

## Baseline

- `opj_set_baseline` and `opj_clear_baseline` (one Undo step each) keep or drop each row's scheduled
  start, finish, duration, work and cost (`owf_project_set_baseline`).
- Task sheet columns Baseline Start and Baseline Finish ("NA" with no baseline, as MS Project shows
  it), Start Var. and Finish Var.: working time on the task's calendar later than the baseline
  ("1d", "-1d", "0d").
- With `show_baseline` set, the Gantt view draws a thin grey bar under each bar at its baseline dates
  (MS Project's Tracking Gantt), a sixth of a row high, clear of the bar.

## Printing

`opj_print_layout` puts the Gantt view on pages of the printer's printable area, down then across as
MS Project prints. Each page has as many whole rows as fit under the timescale and above a footer row.
The first page across holds the table and the start of the chart; later pages across hold only more
chart, whole days each, to a day past the plan's finish. `opj_print_page_view` gives the view for a
page, and `opj_print_draw` draws it with the footer (the plan's title, and "Page n of m") onto any
`oldraw` target. The Amiga app's PRINT gives it a printer.device target.

## Checked

`tests/project/test_opj.c`: dates typed and shown; every edit, including the ones refused and the
plan staying as it was; outlines, WBS numbers and summaries; Undo and Redo and the Undo budget; every
bar's and link line's pixels worked out by hand; the drawing itself on a host pixel target (bar
colours, the diamond's shape, the lines and arrows, days off, the labels drawn at each zoom, and that
drawing only the chart touches nothing left of it); the hit tests; and resources: Resource Names
typed and refused, every resource sheet cell typed and shown, rates by the hour and the day,
over-allocation, periods at each zoom, usage worked out by hand, deleting with Undo, and the names
drawn beside the bars; Set and Clear Baseline with Undo, the variance columns after a slip either way,
and the baseline bar's place and colour; and print layouts by hand (rows and days a page, pages
down and across, pages too small) and drawn pages (no table after the first page across, the footer).

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

- **Dragging on the Gantt chart** (`opj_drag.c`), as MS Project's mouse does it. The middle of a task's
  bar moves the task by whole days: it gets Start No Earlier Than the first working time of the day dropped
  on. The right end stretches it: its duration becomes the working time to the end of the day dropped on
  (elapsed time for an elapsed duration). The done part, or the first 3 pixels of the bar, sets % Complete
  to where it is dropped. A bar dropped on another task's bar links the two, finish to start (refused when
  they are linked already or it would make a loop). The status line says what the drop will do while the
  mouse moves; a drop off the bar's row does nothing. Summaries and milestones only link. Each drop is one
  edit and one Undo step. Resource Usage refuses drags, as it refuses other edits.
- **Task Information** (Edit > Task Information..., T, or a double-click on a bar): the task's Name,
  Duration, % Complete, Predecessors, Resource Names, Constraint (a cycle of the eight types, as MS
  Project names them), its Date and Notes in one window. OK types only the fields changed, the
  constraint's type before its date, as one edit and one Undo step (`opj_set_cells`); a field refused
  leaves the plan as it was, names itself in the window's title and keeps the window open to be put right.
  Fields hold 399 bytes; a longer note is shown cut and left alone unless it is changed.
- **Constraint Type, Constraint Date and Notes** are task sheet columns too (typed as cells, by ARexx
  SETCELL, or shown by the column chooser). A type with a date and no date yet takes the task's start
  (its finish for the finish types); NA as the date makes it As Soon As Possible; a date on an As Soon or
  As Late As Possible task makes it Start No Earlier Than. A summary takes only As Soon As Possible, Start
  No Earlier Than and Finish No Later Than.
- **The column chooser** (View > Columns in the window, ARexx `COLUMN`): any of the 16 task sheet
  columns is shown or hidden, staying in the sheet's order (`opj_view_show_column`); a column shown gets
  its width (`opj_column_width`: 140 for Resource Names, 64 for % Complete), one with a width of its own
  keeps it, and the table always keeps one column. The selected cell moves to the next column shown when
  its own is hidden (`opj_view_near_column`). Showing Resource Names lets resources be typed onto tasks in
  the window.

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
- **The Resource Usage view** (`opj_usage_draw`), MS Project's: a row for each resource (its name
  bold, its ID with "!" when over-allocated, its total work), then a row for each of its assignments
  (the task's name, indented, and the assignment's work), in the order they were made. The chart
  holds each row's work in every period of the timescale's lower tier, right-aligned, in red where
  the resource is over its max units; a resource's row is shaded. Its table is its own (ID, Resource
  Name, Work: 32, 180 and 72 pixels), and it shares the Gantt view's zoom, timescale place, row
  height and font (`opj_usage_view` gives its places). `opj_usage_rows`, `opj_usage_row` and
  `opj_usage_cell_text` give its rows and cells. The app's View menu switches between Gantt Chart
  and Resource Usage (ARexx `VIEW`); the usage view is read only, and Resources... edits.
- **The Tracking Gantt** (`opj_view.tracking`), MS Project's: the Gantt chart with each task's bar in
  the upper half of its row and its baseline bar (grey) as high in the lower half, so a slip shows at a
  glance; the done part is solid, and each bar has its % Complete beside it in place of the resource
  names (`opj_label_text`). Baselines show whatever Show Baseline says. View > Tracking Gantt (J, or
  ARexx `VIEW TRACKING`) shows it; everything else is the Gantt chart's, edits included.
- **The Task Usage view** (`opj_task_usage_draw`), MS Project's: Resource Usage turned round. A row
  for each task (indented by its outline level, a summary's name bold, its ID and its work), then a row
  for each of its assignments (the resource's name, indented, and the assignment's work). The chart
  holds each row's work in every period of the timescale's lower tier; a task's row adds up its own
  assignments' and its subtasks' work and is shaded. Its table (ID, Task Name, Work) is as wide as
  Resource Usage's and it shares the same view fields. View > Task Usage (K, or ARexx `VIEW TASKUSAGE`)
  shows it; it is read only, as Resource Usage is (`opj_edit_refused` with `OPJ_VIEW_TASK_USAGE`).
- **The Resource Graph** (`opj_graph_draw`), MS Project's: one work resource's allocation in each
  period of the timescale's lower tier, as a bar of percent (its work there over its calendar's working
  time there, `opj_graph_percent`): blue up to its max units, red above, with its percent over the bar
  where the period is wide enough. A dark line marks its max units; the scale runs from 0 to
  `opj_graph_scale` (its peak in the periods shown or its max units, at least 100%, rounded up to 50).
  The table's place holds the resource's name, its peak and its max units, with the scale's ends beside
  the chart. View > Resource Graph (H, or ARexx `VIEW GRAPH`) shows it; up and down choose the resource.
  It is read only.
- **The Network Diagram** (`src/project/opj_network.c`), MS Project's: a box for every task that isn't
  a summary, 160 pixels wide and four rows high, holding its name (bold), its ID and duration, its start
  and its finish. A box stands in the column one past its furthest predecessor's (tasks with none in
  the first), and down its column in the sheet's order (`opj_network_layout`). Each link is a line with
  an arrow from the predecessor's box's right side to the successor's left side, turning in the gap
  before the successor's column; it is red when both tasks are critical. A critical task's box is edged
  in red; a milestone's edge is doubled; a task under way is struck through once and a finished one
  crossed. View > Network Diagram (D, or ARexx `VIEW NETWORK`) shows it in place of the Gantt chart:
  it keeps the same selected task, a click on a box selects it, a double-click opens Task Information,
  up and down move through the tasks (scrolled to show the box), and left and right (with Shift, a
  page) scroll. Every edit works on the selected task as in the Gantt chart, and the boxes are laid out
  again at each redraw.

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

`opj_pdf_write` (src/project/opj_pdf.c) prints the same pages to a PDF: A4 landscape with a 1 cm
margin, 12 twips (0.6 pt) a pixel, so 1308 by 897 pixels a page. It draws through an `oldraw` target
on libowf's page PDF writer (owf_pdf.h): fills become rectangles, text is Helvetica at 7.2 pt in rows
17 pixels high, whatever the screen's font, so the PDF is small and its text searchable. It is
printed black on white; white fills are left out, as the paper is white already. The window's columns
and zoom are kept. The app's Project menu has Export PDF..., and ARexx has EXPORT with FORMAT PDF or a
.pdf name.

## Reports

Project > Reports... picks one of MS Project's six reports: Project Summary, Critical Tasks,
Milestones, Who Does What, Overallocated Resources or Cash Flow. Print sends it down the suite's print
route (`app/ow_print.c`, as GFOpenWrite's): GFOpenPrint when it is on the disk, else printer.device.
Save... writes it as PDF, ODT, DOCX, RTF, HTML or text by the name's extension, PDF when it has none.
`owf_project_report` (libowf/project/owf_preport.c) builds each report as a libowf document, with
headings, tables, the plan's title in the page header and "Page n of m" in the footer. libowf's own
writers then print and save it, so GFOpenProject has no report writer of its own. Cash Flow puts each
task's cost into weeks in proportion to its working time. ARexx's REPORT does the same as Save.
`tests/project/test_preport.c` checks every report against figures worked out by hand.

`opj_sheet_document` (src/project/opj_sheetdoc.c) builds the task sheet or the resource sheet as a
document in the same way. The task sheet takes the view's shown columns in order and its lines (filter
and groups, with a group's heading as a bold row); summaries are bold, and an over-allocated task's ID
gets the sheet's "!". The resource sheet takes every column. Both print A4 landscape. ARexx's SAVESHEET
saves them.

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

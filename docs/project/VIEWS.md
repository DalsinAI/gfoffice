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

## Checked

`tests/project/test_opj.c`: dates typed and shown; every edit, including the ones refused and the
plan staying as it was; outlines, WBS numbers and summaries; Undo and Redo and the Undo budget; every
bar's and link line's pixels worked out by hand; the drawing itself on a host pixel target (bar
colours, the diamond's shape, the lines and arrows, days off, the labels drawn at each zoom, and that
drawing only the chart touches nothing left of it); and the hit tests.

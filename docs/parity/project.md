# GFOpenProject against ProjectLibre (OpenProj)

GFOpenProject is scored against ProjectLibre 1.9, the maintained successor of OpenProj, the way the 9 November
gap analysis scored the other apps (`docs/gfoffice/PARITY-GAPS-2026-11-09.md`). The score is practical,
workflow-weighted parity: it measures whether an ordinary user can complete ProjectLibre's documented workflow,
with comparable editing and saving, in the GFOpenProject window. It is not a count of menu items. A feature the
core has but the window can't reach (only ARexx or a file can) gets part credit. Statuses are Done, Part and Gap,
as in the other sheets.

## Score

| Date | Main | Score | What moved it |
| --- | --- | ---: | --- |
| 16 November 2026 | `72b77cb` | **49%** | First score. GFOpenProject 0.1.2: steps 1 to 2 of the design (#181, #184, #186 and the follow-ups to the Amiga window), the Gantt chart printed to PDF (#237) and the Resource Usage view (#240) |

Confidence: medium. The scheduler, files and views have hand-worked host tests, plain and under ASan and UBSan.
The window has had one Cradle lab pass (project.rexx, 66 checks, 15 November) and no daily use yet.

Waiting to merge (not scored): no GFOpenProject PRs are open.

## Features

The weights add up to 100, and the score is the weighted sum of what each row delivers (Done 100%, Part as
noted).

| Feature (ProjectLibre) | Weight | Status | Credit | Notes |
| --- | ---: | --- | ---: | --- |
| Task sheet: enter, insert, delete, indent and outdent tasks; summaries; milestones; WBS; Undo | 12 | Part | 70% | Done: typed cells as MS Project takes them (`5d`, `3ed`, `1w?`, dates day first), Insert and Delete Task, Indent and Outdent, summaries rolled up, milestones as diamonds, WBS numbers, Undo and Redo (512 KB of plans). Not yet: a Task Information dialog, task notes (read and written in MSPDI but not editable), Cut, Copy and Paste of rows, dragging rows, recurring tasks, split tasks |
| Dependencies and constraints | 10 | Part | 60% | Done in the core: FS, SS, FF and SF links with lags and leads in time or percent; MSO, MFO, SNET, FNET, SNLT, FNLT and ALAP constraints; loops reported. Window: the Predecessors cell (`2,3SS+1d,5FF-2h`), Link to Next Task and Unlink; typing a Start sets Start No Earlier Than. Not yet: choosing any other constraint in the window, deadlines, dragging a link between bars |
| Calendars | 8 | Part | 30% | Done in the core: up to five working spans a weekday and day exceptions, resource calendars read from MSPDI, elapsed durations, integer arithmetic only. Not yet: a calendar editor (Change Working Time), several base calendars chosen in the window, a task's own calendar set in the window |
| Scheduling: critical path, slack, summaries | 10 | Part | 80% | Done: forward and backward passes, total and free slack, critical tasks in red, constraints winning over links as MS Project's default, FINISH and CRITICAL over ARexx. Not yet: task types (fixed units, work or duration) and effort-driven scheduling, resource levelling, scheduling from the finish |
| Gantt chart | 12 | Part | 50% | Done: two-tier timescale at Days, Weeks, Months and Quarters; bars, critical bars, done part, milestones, link lines with arrows; baseline bars; days off shaded; clicking selects a row. Not yet: dragging bars to move or stretch them, dragging to link, progress lines, bar styles, a column chooser (Resource Names, % Complete, WBS and the baseline columns exist but the window's table can't show or hide them) |
| Resources and assignments | 12 | Part | 45% | Done: the resource sheet (Resources...: ID, name, type work/material/cost, initials, group, max units, rate, work, cost), over-allocated resources flagged "!", work and cost rolled up to tasks, assignments with units. Window gap: assignments are made by the Resource Names column, which the window can't show, so a user assigns only by ARexx ASSIGN or a file. Not yet: an Assign Resources dialog, resource calendars edited in the window, levelling |
| Other views: Network diagram, Task Usage, Resource Usage, Resource Graph, Tracking Gantt, WBS/RBS charts | 10 | Part | 20% | Done: Resource Usage (#240): each resource and its assignments with work per period at the Gantt zoom, over-allocation in red, read only. Not yet: Network (PERT) diagram, Task Usage, Resource Graph (histogram), Tracking Gantt, WBS and RBS charts |
| Tracking: % complete, actuals, baselines, variances, earned value | 8 | Part | 30% | Done: % Complete (drawn as the done band), Set and Clear Baseline (Baseline 0, whole plan or rows), Baseline Start, Baseline Finish and Start and Finish Variance columns, baseline bars. Not yet: actual start, finish and work; Update Project; the other ten baselines; earned value (BCWS, BCWP, ACWP, SV, CV) |
| Files | 10 | Part | 65% | Done: `.gfproj` (own, zip) read and write; MS Project XML (MSPDI) read and write, with calendars, tasks, links, constraints, resources, assignments and Baseline 0; CSV task table read and write; MPX and GanttProject `.gan` read. Not yet: MS Project `.mpp` read, which ProjectLibre has; ProjectLibre's own `.pod` (not planned: no published format) |
| Printing, PDF and reports | 5 | Part | 45% | Done: the Gantt view printed down then across with a footer (printer.device), and the same pages as a searchable PDF (#237, EXPORT FORMAT PDF). Not yet: printing other views, page setup in the window, the reports (project summary, who does what, cash flow) |
| Sort, filter, group, find, custom fields | 3 | Gap | 0% | Not yet |

**Not in ProjectLibre and not scored:** the GFOPENPROJECT ARexx port (30 plan commands in `docs/project/AREXX.md`:
open, save and export, tasks, cells, links, baselines, resources, assignments, usage, finish, critical rows;
plus the window's own, such as VIEW).

## Top gaps, in the order they would move the score

1. **Editing in the window:** a Task Information dialog (constraints, notes, calendar, resources), a column
   chooser so Resource Names and the tracking columns can be shown, and dragging bars and links on the chart.
   This turns most of the Part rows above into Done, because the core already does the work.
2. **The other views:** the Network diagram first, then Task Usage, Resource Graph and Tracking Gantt.
3. **Tracking and calendars:** actual dates and work with Update Project, a calendar editor, and resource
   levelling.

After those: `.mpp` reading (cleanroom, from Microsoft's published description only), reports, and sort and
filter.

## Evidence

- Code on main `72b77cb`: `libowf/project` (model, calendars, scheduler, files), `src/project` (editing core,
  views, print, PDF), `app/openproject.c` (the Amiga window), docs in `docs/project/` (SCHEDULER, FILES, VIEWS,
  AREXX).
- Host tests: `make project-test project-check` (223 project and 837 file checks), `make project-view-test
  project-view-check` (574 view and 275 ARexx checks), plain and under ASan and UBSan.
- Amiga: `./build-os3.sh` with the os32-gcc16 open8 stove builds GFOpenProject (464,088 bytes at `bdaf638`);
  Cradle lab of 15 November, `tests/arexx/project.rexx` ALL PASS 66.
- ProjectLibre's feature list and user guide: <https://www.projectlibre.com/product/projectlibre-open-source>.
  Ideas only, as the design says: no ProjectLibre code (CPAL) has been read or used.

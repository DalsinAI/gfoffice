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
| 16 November 2026 (second) | `3569cf3` | **64%** | GFOpenProject 0.1.10, gaps 1 and 2 of the first score: the column chooser (#341), Task Information (#343), dragging bars, % Complete and links (#346); the Network Diagram (#347), Task Usage (#350), the Resource Graph (#351) and the Tracking Gantt (#353) |

Confidence: medium for the core, lower for the window. The scheduler, files and views have hand-worked host
tests (1013 view checks), plain and under ASan and UBSan, and every PR builds for the Amiga. The window has had
one Cradle lab pass (project.rexx, 66 checks, 15 November); the seven window PRs scored here each owe a lab
(Cradle labs are paused for the rebuild), so their credit is from the code and unit tests only.

Waiting to merge (not scored): no GFOpenProject PRs are open.

## Features

The weights add up to 100, and the score is the weighted sum of what each row delivers (Done 100%, Part as
noted).

| Feature (ProjectLibre) | Weight | Status | Credit | Notes |
| --- | ---: | --- | ---: | --- |
| Task sheet: enter, insert, delete, indent and outdent tasks; summaries; milestones; WBS; Undo | 12 | Part | 80% | Done: typed cells as MS Project takes them (`5d`, `3ed`, `1w?`, dates day first), Insert and Delete Task, Indent and Outdent, summaries rolled up, milestones as diamonds, WBS numbers, Undo and Redo (512 KB of plans); Task Information (#343: name, duration, % complete, predecessors, resources, constraint, date and notes as one Undo step); Notes, Constraint Type and Constraint Date columns. Not yet: Cut, Copy and Paste of rows, dragging rows, recurring tasks, split tasks |
| Dependencies and constraints | 10 | Part | 85% | Done in the core: FS, SS, FF and SF links with lags and leads in time or percent; MSO, MFO, SNET, FNET, SNLT, FNLT and ALAP constraints; loops reported. Window: the Predecessors cell (`2,3SS+1d,5FF-2h`), Link to Next Task and Unlink; typing a Start sets Start No Earlier Than; all eight constraints in Task Information and as cells (#343); a bar dragged onto another links them finish to start (#346). Not yet: deadlines; link type and lag set on a link itself (Task Dependency dialog) |
| Calendars | 8 | Part | 30% | Done in the core: up to five working spans a weekday and day exceptions, resource calendars read from MSPDI, elapsed durations, integer arithmetic only. Not yet: a calendar editor (Change Working Time), several base calendars chosen in the window, a task's own calendar set in the window |
| Scheduling: critical path, slack, summaries | 10 | Part | 80% | Done: forward and backward passes, total and free slack, critical tasks in red, constraints winning over links as MS Project's default, FINISH and CRITICAL over ARexx. Not yet: task types (fixed units, work or duration) and effort-driven scheduling, resource levelling, scheduling from the finish |
| Gantt chart | 12 | Part | 80% | Done: two-tier timescale at Days, Weeks, Months and Quarters; bars, critical bars, done part, milestones, link lines with arrows; baseline bars; days off shaded; clicking selects a row, a double-click on a bar opens Task Information; the column chooser (#341, View > Columns and ARexx COLUMN, 16 columns); dragging a bar to move it (Start No Earlier Than), its end to stretch it, its done part to set % Complete, and onto another bar to link (#346), each one Undo step. Not yet: progress lines, bar styles (Format Bar), dragging the split between table and chart |
| Resources and assignments | 12 | Part | 60% | Done: the resource sheet (Resources...: ID, name, type work/material/cost, initials, group, max units, rate, work, cost), over-allocated resources flagged "!", work and cost rolled up to tasks, assignments with units; assigning in the window, by the Resource Names column (shown by the column chooser, #341) or Task Information's Resources field (#343), as `Dale,Bob[50%]`. Not yet: an Assign Resources dialog (pick from the sheet), resource calendars edited in the window, levelling |
| Other views: Network diagram, Task Usage, Resource Usage, Resource Graph, Tracking Gantt, WBS/RBS charts | 10 | Part | 80% | Done: Resource Usage (#240): each resource and its assignments with work per period, over-allocation in red; the Network Diagram (#347): a box per task in columns by its predecessors, links with arrows, critical in red, done tasks crossed, click and double-click to edit; Task Usage (#350): each task and its assignments with work per period, summaries rolled up; the Resource Graph (#351): a resource's percent allocated per period, red above its max units; the Tracking Gantt (#353): bars over their baselines with % Complete. The usage views and the graph are read only. Not yet: WBS and RBS charts, editing in the usage views, dragging boxes in the Network Diagram |
| Tracking: % complete, actuals, baselines, variances, earned value | 8 | Part | 30% | Done: % Complete (drawn as the done band), Set and Clear Baseline (Baseline 0, whole plan or rows), Baseline Start, Baseline Finish and Start and Finish Variance columns, baseline bars. Not yet: actual start, finish and work; Update Project; the other ten baselines; earned value (BCWS, BCWP, ACWP, SV, CV) |
| Files | 10 | Part | 65% | Done: `.gfproj` (own, zip) read and write; MS Project XML (MSPDI) read and write, with calendars, tasks, links, constraints, resources, assignments and Baseline 0; CSV task table read and write; MPX and GanttProject `.gan` read. Not yet: MS Project `.mpp` read, which ProjectLibre has; ProjectLibre's own `.pod` (not planned: no published format) |
| Printing, PDF and reports | 5 | Part | 45% | Done: the Gantt view printed down then across with a footer (printer.device), and the same pages as a searchable PDF (#237, EXPORT FORMAT PDF). Not yet: printing other views, page setup in the window, the reports (project summary, who does what, cash flow) |
| Sort, filter, group, find, custom fields | 3 | Gap | 0% | Not yet |

**Not in ProjectLibre and not scored:** the GFOPENPROJECT ARexx port (30 plan commands in `docs/project/AREXX.md`:
open, save and export, tasks, cells, links, baselines, resources, assignments, usage, finish, critical rows;
plus the window's own, such as VIEW).

## Top gaps, in the order they would move the score

The first score's gaps 1 (editing in the window) and 2 (the other views) are merged; these are what is left.

1. **Tracking and calendars:** actual start, finish and work with Update Project, the other ten baselines and
   earned value; a calendar editor (Change Working Time), base calendars chosen in the window and a task's own
   calendar. Tracking (8) and Calendars (8) are the two rows still at 30%.
2. **Resources:** an Assign Resources dialog, resource calendars in the window, and resource levelling, with
   task types and effort-driven scheduling in the scheduler.
3. **Sort, filter, group and find**, then reports and printing the other views.

After those: `.mpp` reading (cleanroom, from Microsoft's published description only), recurring and split
tasks, and progress lines.

## Evidence

- Code on main `3569cf3`: `libowf/project` (model, calendars, scheduler, files), `src/project` (editing core,
  views, network diagram, dragging, print, PDF), `app/openproject.c` (the Amiga window), docs in
  `docs/project/` (SCHEDULER, FILES, VIEWS, AREXX).
- Host tests: `make project-test project-check` (223 project and 837 file checks), `make project-view-test
  project-view-check` (1013 view and 311 ARexx checks), plain and under ASan and UBSan.
- Amiga: `./build-os3.sh` with the os32-gcc16 open8 stove builds GFOpenProject (568,260 bytes for the Tracking
  Gantt PR, 0.1.10); Cradle lab of 15 November, `tests/arexx/project.rexx` ALL PASS 66. Labs are owed for
  #341, #343, #346, #347, #350, #351 and #353.
- ProjectLibre's feature list and user guide: <https://www.projectlibre.com/product/projectlibre-open-source>.
  Ideas only, as the design says: no ProjectLibre code (CPAL) has been read or used.

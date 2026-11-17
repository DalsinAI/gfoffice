# GFOpenProject's ARexx commands (GFOPENPROJECT)

`src/project/opj_rexx.c` answers the plan's commands, so they run and are tested on the PC.
`opj_rexx_command(rx, line, result, size)` takes a command line as a script sends it. The Amiga app
owns the port (`GFOPENPROJECT`, a second copy `GFOPENPROJECT.2`). It answers `VERSION`,
`ACTIVATE`, `DEACTIVATE`, `FAULT`, `PRINT`, `VIEW` and `QUIT [FORCE]` itself, and hands every other
command here. It redraws when `rx->doc` or `rx->doc->changes` changes.

These follow the GFOffice conventions (docs/gfoffice/Design-Suite-Shared.md 6.3):

- **Arguments** use a ReadArgs template: quoted words (`*"` for a quote inside),
  `KEYWORD=value` or `KEYWORD value`, `/S` switches, and `/F` for the rest of the line.
- **Numbering:** rows and resources are numbered from 1, as the sheets' ID columns.
  A ROW or COUNT past the end of the plan is an error (10), however large it is.
- **File names** are at most 255 bytes; OPEN and SAVEAS refuse a longer one (10).
- **Return codes:**
  - 0: done.
  - 5: not possible now (unsaved changes, nothing to undo, no file yet).
  - 10: an error (bad arguments, out of range, a loop of links, a file that can't be read or
    written).
  - 20: an unknown command, or out of memory.
- **LASTERROR** gives the reason after any return code but 0. No requester is shown.

The window (`app/openproject.c`, built by `build.d/project.sh`) answers `VERSION`, `ACTIVATE`,
`DEACTIVATE` (5: it has no icon to go to), `FAULT CODE`, `PRINT` (every page of the Gantt view through
printer.device), `VIEW` (`VIEW` gives `GANTT`, `TRACKING`, `USAGE`, `TASKUSAGE`, `GRAPH` or `NETWORK`; `VIEW TRACKING`
shows the Tracking Gantt, `VIEW USAGE` the
Resource Usage view, `VIEW TASKUSAGE` the Task Usage view, `VIEW GRAPH` the Resource Graph (of the
first resource), `VIEW NETWORK` the Network Diagram and
`VIEW GANTT` the Gantt chart; anything else is 10) and `QUIT` itself; `QUIT` gives 5 while the plan has unsaved changes and `QUIT FORCE`
drops them. A second copy's port is `GFOPENPROJECT.2`. `tests/arexx/project.rexx` runs the port on the
Amiga (start GFOpenProject, then `rx project.rexx RAM:`).

| Command | Template | Result |
| --- | --- | --- |
| HELP | `COMMAND` | every command, or one command's template |
| LASTERROR | | the last failure's reason |
| ISDIRTY | | `1` or `0` |
| GETFILE | | the plan's file, or nothing |
| NEW | `START/K,FORCE/S` | an empty plan from START (a date), or from the plan's start |
| OPEN | `FILE/A,FORCE/S` | .gfproj, MS Project XML (MSPDI), MPX, GanttProject .gan or CSV, by what the file holds |
| SAVE | | to the plan's file, in the format its extension names |
| SAVEAS | `FILE/A` | .xml is MSPDI, .csv is CSV, anything else .gfproj |
| EXPORT | `FILE/A,FORMAT/K` | a copy (MSPDI, CSV or GFPROJ), or the printed Gantt view as a PDF (FORMAT PDF, or a .pdf name; 5 when the plan has no tasks); the plan keeps its file |
| TASKS | | how many rows |
| ADDTASK | `NAME/A,AT/K/N,DURATION/K` | the new row's number (before row AT, or at the end) |
| DELETETASK | `ROW/A/N,COUNT/N` | |
| GETCELL | `ROW/A/N,COLUMN/A` | the cell as the sheet shows it |
| SETCELL | `ROW/A/N,COLUMN/A,TEXT/A/F` | |
| INDENT, OUTDENT | `ROW/A/N,COUNT/N` | |
| LINK | `ROW/A/N,COUNT/N` | links COUNT rows (2 when not given) one to the next, finish to start |
| UNLINK | `ROW/A/N,COUNT/N` | |
| UNDO, REDO | | |
| BASELINE | `ROW/N,COUNT/N,CLEAR/S,NUMBER/K/N` | sets (or with CLEAR clears) the baseline; no ROW: the whole plan. NUMBER=1 to 10: Baseline 1 to 10 (0, the default, is Baseline) |
| BASELINES | | the baselines some task has saved, as `0 3 10`, or `none` |
| REPLACE | `ROW/A/N,FROM/A,TO/A,UNITS/K` | Assign Resources' Replace: the row's assignment of FROM goes to TO, keeping its units unless UNITS gives them ("50%", "2.5", "120.00") |
| LEVEL | `WITHINSLACK/S,ORDER/K` | Level Resources: delays tasks until no work resource is over-allocated where a move can mend it; how many tasks were delayed. WITHINSLACK: only within each task's slack. ORDER: `Standard` (the default: earlier start, less slack, then ID), `ID` (ID Only) or `Priority` (Priority, Standard). Tasks of Priority 1000 are never moved |
| CLEARLEVELING | | every task's Leveling Delay back to 0 |
| SORT | `BY/A,DESC/S,THEN/K,THENDESC/S,LAST/K,LASTDESC/S` | Project > Sort, renumbering the tasks and keeping the outline: by up to three columns, each ascending unless its DESC is given |
| FIND | `TEXT/A,COLUMN/K,FROM/K/N,BACK/S,EXACT/S,BEGINS/S,RESOURCES/S` | Edit > Find: the next row (after FROM, or from the top) whose COLUMN (default Name) holds TEXT, case ignored, going round the end; EXACT: the whole cell, BEGINS: its start; BACK: upwards; RESOURCES: on the resource sheet. RC 5 when none |
| REPLACETEXT | `COLUMN/A,FIND/A,WITH/A,ROW/K/N` | Edit > Replace All (or only ROW): FIND becomes WITH in that column's cells, case ignored, as one edit; how many cells changed. When a cell wouldn't take its new text, nothing changes |
| UPDATETASK | `ROW/A/N,PERCENT/K,ACTUAL/K,REMAINING/K,START/K,FINISH/K` | Tools > Tracking > Update Tasks for one task, as one edit: the actual START, then the ACTUAL and REMAINING durations (`1d`: an actual duration sets % Complete against the duration; a remaining one makes the duration the actual plus it), then PERCENT, then the actual FINISH, each as typed in its cell. A summary takes only PERCENT |
| PROGRESSLINES | `ON/S,OFF/S` | View > Progress Lines in the window: a line down the Gantt chart at the status date (Update Project's) that jags to each task under way, or due to be, at how far it has got. Gives 1 when shown. RC 5 with no window |
| FILTER | `NAME/A,COLUMN/K,TEXT/K,EXACT/S,BEGINS/S,SUMMARIES/S` | Project > Filtered For in the window: NAME is All, Critical, Milestones, Incomplete, Completed, Summary, Unstarted, or AutoFilter (COLUMN's cell contains TEXT; EXACT equals it, BEGINS starts with it; no TEXT keeps every task). SUMMARIES also shows the summaries above the tasks kept. Gives how many tasks are shown. RC 5 with no window |
| GROUP | `BY/K,DESC/S` | Project > Group By a column (no BY: no group). The summaries go, and the tasks sit under a heading for each value, in the column's order (DESC: the other way). Gives how many groups |
| LINES | | how many lines the window shows: the tasks the filter keeps, and the group headings |
| LINE | `LINE/A/N` | a line's task number, or a heading's text, `Duration: 2 days` |
| RESOURCES | | how many resources |
| ADDRESOURCE | `NAME/A` | the new resource's number |
| DELETERESOURCE | `ROW/A/N,COUNT/N` | |
| GETRESOURCE | `ROW/A/N,COLUMN/A` | the resource sheet's cell |
| SETRESOURCE | `ROW/A/N,COLUMN/A,TEXT/A/F` | |
| ASSIGN | `ROW/A/N,RESOURCE/A,UNITS/N` | RESOURCE by number or name; UNITS 100 when not given, 0 unassigns |
| USAGE | `RESOURCE/A,FROM/A,TO/A` | the resource's work between two dates, `12h` |
| FINISH | | the plan's finish, `Fri 20/11/26 17:00` |
| CRITICAL | | the critical rows (not summaries), `1 2 3` |
| COLUMN | `COLUMN/A,SHOW/S,HIDE/S` | `1` when the window's table shows the column, after SHOW or HIDE; 5 when it would hide the last column shown, or there is no window; 10 for no such column. The column by its title (`"Resource Names"`) or a short name (`Resources`, `Percent`, `StartVariance`, `Constraint`, `ActualStart`, `ActualFinish`, `ActualDuration`, `Remaining`) |
| CALENDARS | | how many base calendars |
| ADDCALENDAR | `NAME/A,FROM/K` | a new calendar's number; a copy of calendar FROM (its week and holidays), or Standard's hours. 10 for a name in use, empty, or a number |
| WORKTIME | `CALENDAR/A,DAY/A,TO/K,HOURS/F` | Change Working Time. DAY is a weekday (`Mon`, `Friday`: every week) or a date (that day, or through TO). HOURS is `08:00-12:00, 13:00-17:00`, `off`, or for dates `DEFAULT` (back to the week's hours). Without HOURS: the day's hours, `08:00-12:00, 13:00-17:00` or `off`. CALENDAR by name or number |
| SETCALENDAR | `CALENDAR/A,ROW/K/N` | the plan's calendar, or with ROW that task's own (CALENDAR `""` or `-`: the plan's again) |
| UPDATE | `STATUS/A,WHOLE/S` | Update Project: work complete through STATUS (a date; 17:00 when no time is given), how many tasks changed. WHOLE: 0% or 100% only |

Columns are named by their titles, with case, spaces and punctuation ignored. For example `Name`,
`"Task Name"`, `Duration`, `Start`, `Finish`, `Predecessors`, `"% Complete"` or `percent`, `WBS`,
`"Resource Names"` or `resources`, `"Baseline Start"`, `"Baseline Finish"`, `"Start Var."`,
`"Finish Var."`, `Type` (`"Fixed Units"`, `"Fixed Duration"` or `"Fixed Work"`, or `units`, `duration`,
`work`), `"Effort Driven"` (`Yes` or `No`; Fixed Work is always `Yes`), `Work` (typed: Fixed Units and
Fixed Work change the duration, Fixed Duration the units), and the worked-out `"Actual Work"`,
`"Remaining Work"`, `Cost` and `"Actual Cost"`. Edits keep what the task's type fixes, as MS Project does:
on Fixed Units, changing a resource's units changes the duration, and on Fixed Work a longer duration
lowers the units. With Effort Driven, adding or removing a resource keeps the work.
`Priority` takes 0 to 1000 (500 for a new task; 1000 is Do Not Level, which LEVEL never moves).
`Overallocated` (or `over`) is worked out: `Yes` when one of the task's work resources has more
assigned at some time within the task than its max units. Such a task's `ID` reads with a `!` after it,
as an over-allocated resource's does.

On the resource sheet: `Name`, `Type`, `Initials`, `Group`, `"Max. Units"` or `max`,
`"Std. Rate"` or `rate`, `Work`, `Cost` and `"Base Calendar"` or `calendar` (a calendar's name or number,
`""` or `-` for the plan's). A task with no calendar of its own is scheduled on the calendar of its first
work resource that has one.

```rexx
/* A small plan */
ADDRESS GFOPENPROJECT
OPTIONS RESULTS
'ADDTASK Plan DURATION=2d'
'ADDTASK Build DURATION=3d'
'LINK 1'
'ADDRESOURCE Dale'
'ASSIGN 1 Dale'
'SETRESOURCE 1 rate 20/h'
'BASELINE'
'FINISH'; SAY 'Finishes' RESULT
'SAVEAS RAM:Plan.gfproj'
```

`tests/project/test_opj_rexx.c` runs every command on the PC, with its return codes, results and
LASTERROR texts.

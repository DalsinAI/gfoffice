# GFOpenProject's ARexx commands (GFOPENPROJECT)

`src/project/opj_rexx.c` answers the plan's commands, so they run and are tested on the PC.
`opj_rexx_command(rx, line, result, size)` takes a command line as a script sends it. The Amiga app
owns the port (`GFOPENPROJECT`, a second copy `GFOPENPROJECT.2`). It answers `VERSION`,
`ACTIVATE`, `DEACTIVATE`, `FAULT`, `PRINT` and `QUIT [FORCE]` itself, and hands every other
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

| Command | Template | Result |
| --- | --- | --- |
| HELP | `COMMAND` | every command, or one command's template |
| LASTERROR | | the last failure's reason |
| ISDIRTY | | `1` or `0` |
| GETFILE | | the plan's file, or nothing |
| NEW | `START/K,FORCE/S` | an empty plan from START (a date), or from the plan's start |
| OPEN | `FILE/A,FORCE/S` | .gfproj, MS Project XML (MSPDI), MPX or CSV, by what the file holds |
| SAVE | | to the plan's file, in the format its extension names |
| SAVEAS | `FILE/A` | .xml is MSPDI, .csv is CSV, anything else .gfproj |
| EXPORT | `FILE/A,FORMAT/K` | a copy (MSPDI, CSV or GFPROJ); the plan keeps its file |
| TASKS | | how many rows |
| ADDTASK | `NAME/A,AT/K/N,DURATION/K` | the new row's number (before row AT, or at the end) |
| DELETETASK | `ROW/A/N,COUNT/N` | |
| GETCELL | `ROW/A/N,COLUMN/A` | the cell as the sheet shows it |
| SETCELL | `ROW/A/N,COLUMN/A,TEXT/A/F` | |
| INDENT, OUTDENT | `ROW/A/N,COUNT/N` | |
| LINK | `ROW/A/N,COUNT/N` | links COUNT rows (2 when not given) one to the next, finish to start |
| UNLINK | `ROW/A/N,COUNT/N` | |
| UNDO, REDO | | |
| BASELINE | `ROW/N,COUNT/N,CLEAR/S` | sets (or with CLEAR clears) the baseline; no ROW: the whole plan |
| RESOURCES | | how many resources |
| ADDRESOURCE | `NAME/A` | the new resource's number |
| DELETERESOURCE | `ROW/A/N,COUNT/N` | |
| GETRESOURCE | `ROW/A/N,COLUMN/A` | the resource sheet's cell |
| SETRESOURCE | `ROW/A/N,COLUMN/A,TEXT/A/F` | |
| ASSIGN | `ROW/A/N,RESOURCE/A,UNITS/N` | RESOURCE by number or name; UNITS 100 when not given, 0 unassigns |
| USAGE | `RESOURCE/A,FROM/A,TO/A` | the resource's work between two dates, `12h` |
| FINISH | | the plan's finish, `Fri 20/11/26 17:00` |
| CRITICAL | | the critical rows (not summaries), `1 2 3` |

Columns are named by their titles, with case, spaces and punctuation ignored. For example `Name`,
`"Task Name"`, `Duration`, `Start`, `Finish`, `Predecessors`, `"% Complete"` or `percent`, `WBS`,
`"Resource Names"` or `resources`, `"Baseline Start"`, `"Baseline Finish"`, `"Start Var."` and
`"Finish Var."`.

On the resource sheet: `Name`, `Type`, `Initials`, `Group`, `"Max. Units"` or `max`,
`"Std. Rate"` or `rate`, `Work` and `Cost`.

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

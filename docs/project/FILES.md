# GFOpenProject's files (libowf/project/owf_pfile.c)

Step 1b of amicradle-teams `designs/GFOPENPROJECT-20261113.md`: reading and writing plans, tested on
the host (`make project-test`, `make project-check`).

`owf_project_read` reads any of the three and works out which from the bytes (`owf_project_detect`).
`owf_project_write` writes one. Nothing is scheduled on reading: call `owf_project_schedule` after.

## .gfproj (GFOpenProject's own)

- A zip holding `mimetype` (stored, first: `application/x-gfopenproject`), `content.xml` and
  `meta.xml`.
- `content.xml` uses MSPDI's element names (below), in GFOpenProject's own namespace
  `urn:amicradle:gfopenproject:1`, so one reader serves both. Later steps add their own elements
  there (resources, baselines).
- `meta.xml` names the program, the format version (1) and the title.

## MSPDI (MS Project's XML)

Written from Microsoft's published Project XML Data Interchange schema reference.

- **Project:** Name and Title, StartDate, FinishDate, CalendarUID, MinutesPerDay, MinutesPerWeek.
- **Calendars:** base calendars only. A resource's own calendar (IsBaseCalendar 0) is read as the
  base calendar it is built on.
  - WeekDay with DayType 1 (Sunday) to 7 (Saturday), DayWorking and up to five WorkingTimes. A
    ToTime of 00:00:00 ends at midnight. A weekday the file leaves out keeps Standard's hours.
  - Exceptions are written as WeekDay with DayType 0 and a TimePeriod, one per run of days with the
    same hours. Both that form and the later Exceptions/Exception are read; a recurring exception is
    read as its date range only.
- **Tasks:** UID, ID, Name, OutlineLevel, Duration (`PT16H0M0S`), DurationFormat, Estimated,
  Milestone, PercentComplete, ConstraintType (0 ASAP to 7 FNLT, the same order as `OWF_CON_`),
  ConstraintDate, CalendarUID (-1: the project's), Notes, and PredecessorLink.
  - The worked-out dates and slack (Start, Finish, Early/Late, FreeSlack, TotalSlack, Summary,
    Critical) are written for other programs; they are not read back.
  - Read but left out: the project's own summary task (OutlineLevel 0) and blank rows (IsNull).
  - DurationFormat: 3 m, 5 h, 7 d; 4, 6, 8 the elapsed ones; 32 more when estimated (39 is "d?").
- **Work and Cost** of a task (its assignments', a summary's its subtasks') are written for other
  programs (Cost in cents) when not 0; GFOpenProject works them out on reading.
- **Baseline:** Baseline 0 (MS Project's "Baseline") is written and read: Number, Start, Finish,
  Duration, DurationFormat, Work and Cost (cents). A Baseline with no Number is taken as 0;
  Baselines 1 to 10 are left out. The CSV keeps no baseline.
- **PredecessorLink:** PredecessorUID, Type (0 FF, 1 FS, 2 SF, 3 SS), LinkLag in tenths of a
  minute, LagFormat. A percent lag (LagFormat 19) has LinkLag as the percent itself (50 is 50%).
  Links to tasks not in the file, and to the task itself, are left out.
- **Resources:** UID, ID, Name, Type (1 work, 0 material, 2 cost; Type 0 with IsCostResource 1 is a
  cost too, as Project writes it), Initials, Group, MaxUnits (1.00 is 100%, work only), StandardRate
  (per hour for work, StandardRateFormat 2; per unit for material, StandardRateFormat 8) and
  CalendarUID. UID 0 and blank resources (IsNull) are left out.
- **Assignments:** UID, TaskUID, ResourceUID, Units (1.00 is 100%; for material, the quantity) and
  Work (written for other programs; GFOpenProject works it out). A cost resource's amount is the
  assignment's Cost, in cents; Units carries it too. Assignments to tasks or resources not in the
  file are left out.
- Numbers too big for the model are cut to 200,000,000 minutes (about 380 years), so that tenths of
  a minute still fit a 32-bit `long` on the Amiga.

## CSV (the task table)

- RFC 4180: quoted fields, doubled quotes, line breaks inside quotes, CRLF or LF. A UTF-8 BOM is
  skipped. The separator is the header row's most common of comma, semicolon and tab.
- Columns are found by their header, in any order, case ignored; unknown ones are skipped. Only
  Name (or Task Name) is needed. Written in this order: ID, Unique ID, Name, Outline Level,
  Duration, Start, Finish, Predecessors, Constraint Type, Constraint Date, % Complete, Milestone,
  Notes, Resource Names.
- **Durations** as MS Project shows them: `5d`, `5 days`, `2.5h`, `3ed`, `1w?` (`e` elapsed, `?`
  estimated; a number alone is days; a month is 20 working days or 30 elapsed ones).
  `owf_pduration_parse` and `owf_pduration_text` do this for the task sheet too.
- **Predecessors:** `2,3SS+1d,4FF-2h,5SF+50%`, by the ID column (the row number when there is none).
- **Dates:** `2026-11-16 08:00` (ISO 8601; `T` and seconds are read too). Start and Finish are
  written for reading elsewhere; on reading, the earliest Start is taken as the project's start.
- **Resource Names:** `Dale,Bob[50%]` as MS Project writes them; no units means 100%. A name not
  yet in the plan is made a work resource. `owf_presource_names_parse` and `_text` do this for the
  task sheet too.
- A CSV keeps no calendars: its tasks are on the project's calendar.

## Resources (all formats)

Version 1 keeps a task's duration as typed (fixed units, not effort driven). An assignment's work
is the task's working duration times its units; its cost is the work times the hourly rate, the
quantity times the unit rate for a material, or the amount itself for a cost resource. A resource
is over-allocated when more of it is assigned at one time than its MaxUnits. Nothing is levelled.

## Checked

- `tests/project/test_pfile.c`: exact text of the written MSPDI and CSV, each format written then
  read back field by field and scheduled to the same dates, a hand-written MSPDI file and a
  hand-written MS Project style CSV read field by field, and damaged or foreign input.
- MPXJ 16.11.0 run as a black box on the host (no source read): it read our MSPDI with every task,
  duration, link, lag, constraint, calendar and exception as written, and showed the same dates,
  and every resource's type, max units and rate and every assignment's units, work and cost, and
  every task's baseline start, finish, duration, work and cost and its finish variance;
  the MSPDI it wrote from that was read back by `owf_project_read` and scheduled to the same dates.

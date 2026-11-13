# GFOpenProject's scheduler (libowf/project)

Step 1 of amicradle-teams `designs/GFOPENPROJECT-20261113.md`: the plan model, working calendars and
the critical path scheduler, host-built and tested (`make project-test`, `make project-check`).

## Times and calendars

- Times are minutes since 1 January 2000, 00:00, as a `long`; dates follow the Gregorian calendar.
- A calendar has up to five working spans for each weekday, and exceptions for single days (a day
  off, or its own hours). `owf_pcal_standard` gives Monday to Friday, 08:00-12:00 and 13:00-17:00.
- Durations, lags and slack are working minutes on the task's calendar (the project's unless the task
  names one). An elapsed task counts every minute.
- Everything is integer arithmetic: no FPU is needed.

## Rules

- **Forward pass:** a task starts at the project's start, or later as its links require:
  - FS: after the predecessor's finish plus the lag;
  - SS: after its start plus the lag;
  - FF: it finishes after the predecessor's finish plus the lag;
  - SF: it finishes after the predecessor's start plus the lag.

  Lags are counted on the project's calendar. A percent lag is that share of the predecessor's
  duration, and a negative lag is a lead. A start that falls outside working time moves to the next
  working minute; a milestone stays where its predecessor ends.
- **Constraints:**
  - MSO and MFO fix the start or finish.
  - SNET and FNET push later.
  - SNLT and FNLT pull earlier, winning over links as MS Project does by default. The result shows
    as negative slack.
  - ALAP tasks are moved to their late dates, and the plan is worked out again.
- **Backward pass:** from the project's finish (the latest early finish), with the same four link
  types read the other way, and the constraints' latest dates.
- **Slack:**
  - Total slack is the lesser of late start less early start and late finish less early finish.
  - Free slack is how far a task can slip before any successor moves. For a task with no
    successors, it is how far it can slip before the project's finish moves.
  - A task is critical when its total slack is 0 or less.
- **Summaries:**
  - A task followed by deeper ones is a summary, spanning its subtasks.
  - A summary's predecessors hold back each subtask, and a summary's successors wait for its finish.
  - A summary is critical when any subtask is, with the least slack among them.
  - A link between a summary and its own subtask is a loop.
- **Loops:** links that go round in a circle make scheduling fail with `OWF_ERR_FORMAT`, and
  `loop_task` names a task in the loop.

## Sources

Cleanroom (Dale, 13 Nov 2026): written from the critical path method as published (forward and
backward passes, total and free slack) and MS Project's public documentation of link types, lags
and constraint types. No other program's code or source was read.
